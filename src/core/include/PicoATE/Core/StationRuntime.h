#pragma once

#include "PicoATE/Core/DeviceSessionManager.h"
#include "PicoATE/Core/StationConfig.h"

namespace PicoATE::Core {

struct StationRuntimeResult {
    QVector<StationConfigDiagnostic> errors;

    bool ok() const;
};

class StationRuntime {
public:
    StationRuntimeResult loadStationConfigFile(const QString& filePath,
                                               VariableResolverOptions resolverOptions = {});
    StationRuntimeResult applyStationConfig(const StationConfig& config);

    bool hasStationConfig() const;
    const StationConfig& stationConfig() const;

    DeviceSessionManager& devices();
    const DeviceSessionManager& devices() const;

private:
    StationConfig m_stationConfig;
    DeviceSessionManager m_devices;
    bool m_hasStationConfig = false;
};

} // namespace PicoATE::Core
