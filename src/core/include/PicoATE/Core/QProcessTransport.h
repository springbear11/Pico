#pragma once

#include "PicoATE/Core/ModuleRuntime.h"

#include <QStringList>

namespace PicoATE::Core {

class QProcessTransport final : public IModuleTransport {
public:
    explicit QProcessTransport(QString program, QStringList arguments = {});

    ModuleTransportStatus call(const ModuleTransportRequest& request,
                               ModuleTransportResponse& response,
                               int timeoutMs) override;

    QString program() const;
    QStringList arguments() const;

private:
    QString m_program;
    QStringList m_arguments;
};

} // namespace PicoATE::Core
