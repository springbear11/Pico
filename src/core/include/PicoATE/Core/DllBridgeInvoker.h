#pragma once

#include "PicoATE/Core/ModuleRuntime.h"

namespace PicoATE::Core {

class DllBridgeInvoker final : public IModuleTransport {
public:
    explicit DllBridgeInvoker(QString libraryPath,
                              QString symbolName = "PicoATE_Execute",
                              int responseBufferSize = 65536);

    ModuleTransportStatus call(const ModuleTransportRequest& request,
                               ModuleTransportResponse& response,
                               int timeoutMs) override;

    QString libraryPath() const;
    QString symbolName() const;
    int responseBufferSize() const;

private:
    QString m_libraryPath;
    QString m_symbolName;
    int m_responseBufferSize = 65536;
};

} // namespace PicoATE::Core
