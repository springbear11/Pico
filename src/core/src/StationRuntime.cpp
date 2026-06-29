#include "PicoATE/Core/StationRuntime.h"

#include <utility>

namespace PicoATE::Core {

bool StationRuntimeResult::ok() const
{
    return errors.isEmpty();
}

StationRuntimeResult StationRuntime::loadStationConfigFile(const QString& filePath,
                                                           VariableResolverOptions resolverOptions)
{
    StationRuntimeResult result;
    const auto load = PicoATE::Core::loadStationConfigFile(filePath, std::move(resolverOptions));
    if (!load.ok()) {
        result.errors = load.errors;
        return result;
    }

    return applyStationConfig(load.config);
}

StationRuntimeResult StationRuntime::applyStationConfig(const StationConfig& config)
{
    StationRuntimeResult result;
    result.errors = configureDeviceSessions(config, m_devices);
    if (!result.ok()) {
        return result;
    }

    m_stationConfig = config;
    m_hasStationConfig = true;
    return result;
}

bool StationRuntime::hasStationConfig() const
{
    return m_hasStationConfig;
}

const StationConfig& StationRuntime::stationConfig() const
{
    return m_stationConfig;
}

DeviceSessionManager& StationRuntime::devices()
{
    return m_devices;
}

const DeviceSessionManager& StationRuntime::devices() const
{
    return m_devices;
}

} // namespace PicoATE::Core
