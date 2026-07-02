#include "PicoATE/Core/QProcessTransport.h"

#include "PicoATE/Core/ModuleTransportJson.h"

#include <QElapsedTimer>
#include <QJsonDocument>
#include <QProcess>
#include <utility>

namespace PicoATE::Core {

namespace {

int remainingMs(const QElapsedTimer& timer, int timeoutMs)
{
    if (timeoutMs <= 0) {
        return 0;
    }
    const auto remaining = timeoutMs - static_cast<int>(timer.elapsed());
    return remaining > 0 ? remaining : 0;
}

void appendBounded(QByteArray& target, const QByteArray& data, int maximumBytes = 16384)
{
    target += data;
    if (target.size() > maximumBytes) {
        target = target.right(maximumBytes);
    }
}

void setTransportError(ModuleTransportResponse& response,
                       QString errorCode,
                       QString errorMessage)
{
    response.outcome = ModuleOutcome::Error;
    response.errorCode = std::move(errorCode);
    response.errorMessage = std::move(errorMessage);
}

bool processProtocolLine(const QByteArray& rawLine,
                         const ModuleTransportRequest& request,
                         ModuleTransportResponse& response,
                         bool& responseReceived,
                         QString& protocolError)
{
    const auto line = rawLine.trimmed();
    if (line.isEmpty()) {
        return true;
    }

    QJsonParseError parseError;
    const auto document = QJsonDocument::fromJson(line, &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        protocolError = QString("Invalid JSON protocol line: %1").arg(parseError.errorString());
        return false;
    }

    const auto message = moduleProtocolMessageFromJson(document.object());
    if (!message.traceId.isEmpty() && message.traceId != request.traceId) {
        protocolError = QString("Module protocol traceId mismatch: %1").arg(message.traceId);
        return false;
    }
    if (message.kind == ModuleProtocolMessageKind::Log) {
        if (request.context.logSink) {
            request.context.logSink->publishModuleLog(message.log);
        }
        return true;
    }
    if (message.kind == ModuleProtocolMessageKind::Response) {
        if (responseReceived) {
            protocolError = "Module host returned more than one response";
            return false;
        }
        response = message.response;
        responseReceived = true;
        return true;
    }

    protocolError = message.errorMessage.isEmpty()
        ? QString("Invalid module protocol message")
        : message.errorMessage;
    return false;
}

bool processCompleteLines(QByteArray& buffer,
                          const ModuleTransportRequest& request,
                          ModuleTransportResponse& response,
                          bool& responseReceived,
                          QString& protocolError,
                          bool includeFinalPartialLine = false)
{
    while (true) {
        const auto newline = buffer.indexOf('\n');
        if (newline < 0) {
            break;
        }
        const auto line = buffer.left(newline);
        buffer.remove(0, newline + 1);
        if (!processProtocolLine(line, request, response, responseReceived, protocolError)) {
            return false;
        }
    }
    if (includeFinalPartialLine && !buffer.trimmed().isEmpty()) {
        const auto line = buffer;
        buffer.clear();
        return processProtocolLine(line, request, response, responseReceived, protocolError);
    }
    return true;
}

} // namespace

QProcessTransport::QProcessTransport(QString program, QStringList arguments)
    : m_program(std::move(program))
    , m_arguments(std::move(arguments))
{
}

ModuleTransportStatus QProcessTransport::call(const ModuleTransportRequest& request,
                                              ModuleTransportResponse& response,
                                              int timeoutMs)
{
    const int effectiveTimeoutMs = timeoutMs > 0 ? timeoutMs : 30000;
    QElapsedTimer timer;
    timer.start();

    QProcess process;
    process.setProgram(m_program);
    process.setArguments(m_arguments);
    process.setProcessChannelMode(QProcess::SeparateChannels);
    process.start();

    if (!process.waitForStarted(effectiveTimeoutMs)) {
        setTransportError(response,
                          "ProcessStartFailed",
                          process.errorString());
        return ModuleTransportStatus::TransportError;
    }

    const auto requestBytes = QJsonDocument(moduleTransportRequestToJson(request))
                                  .toJson(QJsonDocument::Compact) + '\n';
    process.write(requestBytes);
    if (!process.waitForBytesWritten(remainingMs(timer, effectiveTimeoutMs))) {
        process.kill();
        process.waitForFinished(1000);
        setTransportError(response,
                          "ProcessWriteFailed",
                          process.errorString());
        return ModuleTransportStatus::TransportError;
    }

    process.closeWriteChannel();
    QByteArray stdoutBuffer;
    QByteArray stderrTail;
    bool responseReceived = false;
    QString protocolError;
    while (process.state() != QProcess::NotRunning) {
        stdoutBuffer += process.readAllStandardOutput();
        appendBounded(stderrTail, process.readAllStandardError());
        if (!processCompleteLines(stdoutBuffer,
                                  request,
                                  response,
                                  responseReceived,
                                  protocolError)) {
            process.kill();
            process.waitForFinished(1000);
            setTransportError(response, "InvalidModuleProtocol", protocolError);
            return ModuleTransportStatus::TransportError;
        }

        const auto remaining = remainingMs(timer, effectiveTimeoutMs);
        if (remaining <= 0) {
            process.kill();
            process.waitForFinished(1000);
            response.outcome = ModuleOutcome::Timeout;
            response.errorCode = "ProcessTimeout";
            response.errorMessage = "Module host process timed out";
            return ModuleTransportStatus::Timeout;
        }
        process.waitForReadyRead(qMin(remaining, 50));
    }

    stdoutBuffer += process.readAllStandardOutput();
    appendBounded(stderrTail, process.readAllStandardError());
    if (!processCompleteLines(stdoutBuffer,
                              request,
                              response,
                              responseReceived,
                              protocolError,
                              true)) {
        setTransportError(response, "InvalidModuleProtocol", protocolError);
        return ModuleTransportStatus::TransportError;
    }

    if (process.exitStatus() == QProcess::CrashExit) {
        setTransportError(response,
                          "ProcessCrashed",
                          QString::fromUtf8(stderrTail).trimmed());
        return ModuleTransportStatus::TransportError;
    }

    if (process.exitCode() != 0) {
        auto message = QString::fromUtf8(stderrTail).trimmed();
        if (message.isEmpty()) {
            message = QString("Module host exited with code %1").arg(process.exitCode());
        }
        setTransportError(response, "ProcessExitError", message);
        return ModuleTransportStatus::TransportError;
    }

    if (!responseReceived) {
        setTransportError(response,
                          "EmptyResponse",
                          "Module host did not write a JSON response");
        return ModuleTransportStatus::TransportError;
    }
    return ModuleTransportStatus::Ok;
}

QString QProcessTransport::program() const
{
    return m_program;
}

QStringList QProcessTransport::arguments() const
{
    return m_arguments;
}

} // namespace PicoATE::Core
