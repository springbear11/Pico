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

QString firstNonEmptyLine(const QByteArray& data)
{
    const auto lines = QString::fromUtf8(data).split('\n');
    for (auto line : lines) {
        line = line.trimmed();
        if (!line.isEmpty()) {
            return line;
        }
    }
    return {};
}

void setTransportError(ModuleTransportResponse& response,
                       QString errorCode,
                       QString errorMessage)
{
    response.outcome = ModuleOutcome::Error;
    response.errorCode = std::move(errorCode);
    response.errorMessage = std::move(errorMessage);
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
    if (!process.waitForFinished(remainingMs(timer, effectiveTimeoutMs))) {
        process.kill();
        process.waitForFinished(1000);
        response.outcome = ModuleOutcome::Timeout;
        response.errorCode = "ProcessTimeout";
        response.errorMessage = "Module host process timed out";
        return ModuleTransportStatus::Timeout;
    }

    if (process.exitStatus() == QProcess::CrashExit) {
        setTransportError(response,
                          "ProcessCrashed",
                          QString::fromUtf8(process.readAllStandardError()).trimmed());
        return ModuleTransportStatus::TransportError;
    }

    if (process.exitCode() != 0) {
        auto message = QString::fromUtf8(process.readAllStandardError()).trimmed();
        if (message.isEmpty()) {
            message = QString("Module host exited with code %1").arg(process.exitCode());
        }
        setTransportError(response, "ProcessExitError", message);
        return ModuleTransportStatus::TransportError;
    }

    const auto line = firstNonEmptyLine(process.readAllStandardOutput());
    if (line.isEmpty()) {
        setTransportError(response,
                          "EmptyResponse",
                          "Module host did not write a JSON response");
        return ModuleTransportStatus::TransportError;
    }

    QJsonParseError parseError;
    const auto document = QJsonDocument::fromJson(line.toUtf8(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        setTransportError(response,
                          "InvalidJsonResponse",
                          parseError.errorString());
        return ModuleTransportStatus::TransportError;
    }

    response = moduleTransportResponseFromJson(document.object());
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
