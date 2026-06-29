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
    if (!ensureStarted(timeout, response)) {
        return ModuleTransportStatus::TransportError;
    }

    const auto requestBytes = QJsonDocument(moduleTransportRequestToJson(request))
                                  .toJson(QJsonDocument::Compact) + '\n';
    m_process->write(requestBytes);
    if (!m_process->waitForBytesWritten(timeout)) {
        setTransportError(response,
                          "PersistentProcessWriteFailed",
                          m_process->errorString());
        killProcess();
        return ModuleTransportStatus::TransportError;
    }

    QString line;
    const auto status = readResponseLine(line, timeout, response);
    if (status != ModuleTransportStatus::Ok) {
        return status;
    }

    QJsonParseError parseError;
    const auto document = QJsonDocument::fromJson(line.toUtf8(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        setTransportError(response,
                          "InvalidPersistentJsonResponse",
                          parseError.errorString());
        killProcess();
        return ModuleTransportStatus::TransportError;
    }

    response = moduleTransportResponseFromJson(document.object());
    return ModuleTransportStatus::Ok;
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
