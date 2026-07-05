#include "PicoATE/Core/PersistentQProcessTransport.h"

#include "PicoATE/Core/ModuleTransportJson.h"

#include <QElapsedTimer>
#include <QJsonDocument>
#include <QJsonParseError>

#include <utility>

namespace PicoATE::Core {

namespace {

int effectiveTimeout(int timeoutMs)
{
    return timeoutMs > 0 ? timeoutMs : 30000;
}

int remainingMs(const QElapsedTimer& timer, int timeoutMs)
{
    const auto remaining = timeoutMs - static_cast<int>(timer.elapsed());
    return remaining > 0 ? remaining : 0;
}

void publishLogs(const ModuleTransportRequest& request,
                 const QVector<ModuleLogRecord>& logs)
{
    if (!request.context.logSink) {
        return;
    }
    for (const auto& log : logs) {
        request.context.logSink->publishModuleLog(log);
    }
}

} // namespace

PersistentQProcessTransport::PersistentQProcessTransport(QString program, QStringList arguments)
    : m_program(std::move(program))
    , m_arguments(std::move(arguments))
{
}

PersistentQProcessTransport::~PersistentQProcessTransport()
{
    shutdown();
}

ModuleTransportStatus PersistentQProcessTransport::call(const ModuleTransportRequest& request,
                                                        ModuleTransportResponse& response,
                                                        int timeoutMs)
{
    const auto timeout = effectiveTimeout(timeoutMs);
    const bool reusedProcess = isRunning();
    QElapsedTimer timer;
    timer.start();
    qint64 processReadyAt = -1;
    qint64 requestWrittenAt = -1;

    const auto finish = [&](ModuleTransportStatus status) {
        const auto totalMs = timer.elapsed();
        QVariantMap timing;
        timing.insert("reusedProcess", reusedProcess);
        timing.insert("processStartMs", reusedProcess ? 0 : processReadyAt);
        timing.insert("requestWriteMs",
                      processReadyAt >= 0 && requestWrittenAt >= 0
                          ? requestWrittenAt - processReadyAt
                          : -1);
        timing.insert("responseWaitMs",
                      requestWrittenAt >= 0 ? totalMs - requestWrittenAt : -1);
        timing.insert("totalMs", totalMs);
        response.diagnostics.insert("persistentQprocess", timing);
        if (totalMs >= 10000 && request.context.logSink) {
            const auto host = response.diagnostics.value("nativeHost").toMap();
            ModuleLogRecord record;
            record.timestampUtc = QDateTime::currentDateTimeUtc();
            record.message = QString("[transport] slow call kind=persistent-qprocess total=%1 ms hostInvoke=%2 ms vendorFlush=%3 ms")
                                 .arg(totalMs)
                                 .arg(host.value("dllInvokeMs").toLongLong())
                                 .arg(host.value("vendorFlushMs").toLongLong());
            request.context.logSink->publishModuleLog(record);
        }
        return status;
    };

    if (!ensureStarted(timeout, response)) {
        processReadyAt = timer.elapsed();
        return finish(ModuleTransportStatus::TransportError);
    }
    processReadyAt = timer.elapsed();

    const auto requestBytes = QJsonDocument(moduleTransportRequestToJson(request))
                                  .toJson(QJsonDocument::Compact) + '\n';
    m_process->write(requestBytes);
    if (!m_process->waitForBytesWritten(remainingMs(timer, timeout))) {
        setTransportError(response,
                          "PersistentProcessWriteFailed",
                          m_process->errorString());
        killProcess();
        return finish(ModuleTransportStatus::TransportError);
    }
    requestWrittenAt = timer.elapsed();

    while (remainingMs(timer, timeout) > 0) {
        QString line;
        const auto status = readResponseLine(line, remainingMs(timer, timeout), response);
        if (status != ModuleTransportStatus::Ok) {
            return finish(status);
        }

        QJsonParseError parseError;
        const auto document = QJsonDocument::fromJson(line.toUtf8(), &parseError);
        if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
            setTransportError(response,
                              "InvalidPersistentJsonResponse",
                              parseError.errorString());
            killProcess();
            return finish(ModuleTransportStatus::TransportError);
        }

        const auto message = moduleProtocolMessageFromJson(document.object());
        if (!message.traceId.isEmpty() && message.traceId != request.traceId) {
            setTransportError(response,
                              "PersistentTraceIdMismatch",
                              QString("Expected %1, got %2").arg(request.traceId, message.traceId));
            killProcess();
            return finish(ModuleTransportStatus::TransportError);
        }
        if (message.kind == ModuleProtocolMessageKind::Log) {
            publishLogs(request, {message.log});
            continue;
        }
        if (message.kind == ModuleProtocolMessageKind::LogBatch) {
            publishLogs(request, message.logs);
            continue;
        }
        if (message.kind == ModuleProtocolMessageKind::Response) {
            response = message.response;
            return finish(ModuleTransportStatus::Ok);
        }

        setTransportError(response,
                          "InvalidPersistentModuleProtocol",
                          message.errorMessage);
        killProcess();
        return finish(ModuleTransportStatus::TransportError);
    }

    response.outcome = ModuleOutcome::Timeout;
    response.errorCode = "PersistentProcessTimeout";
    response.errorMessage = "Persistent module host timed out";
    killProcess();
    return finish(ModuleTransportStatus::Timeout);
}

bool PersistentQProcessTransport::isRunning() const
{
    return m_process && m_process->state() == QProcess::Running;
}

void PersistentQProcessTransport::shutdown(int timeoutMs)
{
    if (!m_process) {
        return;
    }

    if (m_process->state() != QProcess::NotRunning) {
        m_process->closeWriteChannel();
        if (!m_process->waitForFinished(timeoutMs)) {
            m_process->terminate();
            if (!m_process->waitForFinished(timeoutMs)) {
                m_process->kill();
                m_process->waitForFinished(timeoutMs);
            }
        }
    }
    m_process.reset();
    m_stdoutBuffer.clear();
}

QString PersistentQProcessTransport::program() const
{
    return m_program;
}

QStringList PersistentQProcessTransport::arguments() const
{
    return m_arguments;
}

bool PersistentQProcessTransport::ensureStarted(int timeoutMs, ModuleTransportResponse& response)
{
    if (isRunning()) {
        return true;
    }

    m_process = std::make_unique<QProcess>();
    m_process->setProgram(m_program);
    m_process->setArguments(m_arguments);
    m_process->setProcessChannelMode(QProcess::SeparateChannels);
    m_process->start();

    if (!m_process->waitForStarted(timeoutMs)) {
        setTransportError(response,
                          "PersistentProcessStartFailed",
                          m_process->errorString());
        m_process.reset();
        return false;
    }

    m_stdoutBuffer.clear();
    return true;
}

ModuleTransportStatus PersistentQProcessTransport::readResponseLine(QString& line,
                                                                    int timeoutMs,
                                                                    ModuleTransportResponse& response)
{
    QElapsedTimer timer;
    timer.start();

    while (remainingMs(timer, timeoutMs) > 0) {
        m_stdoutBuffer += m_process->readAllStandardOutput();
        if (takeBufferedLine(line)) {
            return ModuleTransportStatus::Ok;
        }

        if (m_process->state() == QProcess::NotRunning) {
            auto message = QString::fromUtf8(m_process->readAllStandardError()).trimmed();
            if (message.isEmpty()) {
                message = QString("Persistent host exited with code %1").arg(m_process->exitCode());
            }
            setTransportError(response, "PersistentProcessExited", message);
            return ModuleTransportStatus::TransportError;
        }

        if (!m_process->waitForReadyRead(remainingMs(timer, timeoutMs))) {
            m_stdoutBuffer += m_process->readAllStandardOutput();
            if (takeBufferedLine(line)) {
                return ModuleTransportStatus::Ok;
            }
        }
    }

    response.outcome = ModuleOutcome::Timeout;
    response.errorCode = "PersistentProcessTimeout";
    response.errorMessage = "Persistent module host timed out";
    killProcess();
    return ModuleTransportStatus::Timeout;
}

bool PersistentQProcessTransport::takeBufferedLine(QString& line)
{
    while (true) {
        const auto newline = m_stdoutBuffer.indexOf('\n');
        if (newline < 0) {
            return false;
        }

        const auto raw = m_stdoutBuffer.left(newline);
        m_stdoutBuffer.remove(0, newline + 1);
        const auto candidate = QString::fromUtf8(raw).trimmed();
        if (!candidate.isEmpty()) {
            line = candidate;
            return true;
        }
    }
}

void PersistentQProcessTransport::killProcess()
{
    if (!m_process) {
        return;
    }
    if (m_process->state() != QProcess::NotRunning) {
        m_process->kill();
        m_process->waitForFinished(1000);
    }
    m_process.reset();
    m_stdoutBuffer.clear();
}

void PersistentQProcessTransport::setTransportError(ModuleTransportResponse& response,
                                                    QString errorCode,
                                                    QString errorMessage) const
{
    response.outcome = ModuleOutcome::Error;
    response.errorCode = std::move(errorCode);
    response.errorMessage = std::move(errorMessage);
}

} // namespace PicoATE::Core