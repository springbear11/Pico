#pragma once

#include "PicoATE/Core/ModuleRuntime.h"

#include <QByteArray>
#include <QProcess>
#include <QStringList>

#include <memory>

namespace PicoATE::Core {

class PersistentQProcessTransport final : public IModuleTransport {
public:
    explicit PersistentQProcessTransport(QString program, QStringList arguments = {});
    ~PersistentQProcessTransport() override;

    ModuleTransportStatus call(const ModuleTransportRequest& request,
                               ModuleTransportResponse& response,
                               int timeoutMs) override;

    bool isRunning() const;
    void shutdown(int timeoutMs = 2000);

    QString program() const;
    QStringList arguments() const;

private:
    bool ensureStarted(int timeoutMs, ModuleTransportResponse& response);
    ModuleTransportStatus readResponseLine(QString& line,
                                           int timeoutMs,
                                           ModuleTransportResponse& response);
    bool takeBufferedLine(QString& line);
    void killProcess();
    void setTransportError(ModuleTransportResponse& response,
                           QString errorCode,
                           QString errorMessage) const;

    QString m_program;
    QStringList m_arguments;
    std::unique_ptr<QProcess> m_process;
    QByteArray m_stdoutBuffer;
};

} // namespace PicoATE::Core
