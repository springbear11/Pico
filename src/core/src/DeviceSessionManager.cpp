#include "PicoATE/Core/DeviceSessionManager.h"
#include "PicoATE/Core/RuntimeEvent.h"

#include <algorithm>
#include <utility>

namespace PicoATE::Core {

void DeviceSessionManager::setRuntimeEventEmitter(RuntimeEventEmitter* events)
{
    m_events = events;
}

bool DeviceSessionError::hasError() const
{
    return !errorCode.isEmpty();
}

QVariantMap IDeviceSession::metadata() const
{
    return {};
}

bool IDeviceSession::isHealthy(QString& errorMessage) const
{
    errorMessage.clear();
    return state() == DeviceConnectionState::Connected;
}

bool DeviceSessionOpenResult::ok() const
{
    return session != nullptr && !error.hasError();
}

bool DeviceSessionManager::registerFactory(std::shared_ptr<IDeviceSessionFactory> factory)
{
    if (!factory || factory->driverId().trimmed().isEmpty()) {
        return false;
    }

    m_factories.insert(factory->driverId(), std::move(factory));
    return true;
}

bool DeviceSessionManager::configureDevice(const DeviceSessionConfig& config,
                                           DeviceSessionError* error)
{
    if (config.deviceId.trimmed().isEmpty()) {
        if (error) {
            *error = makeError({}, "DeviceIdMissing", "Device id is required");
        }
        return false;
    }
    if (config.driverId.trimmed().isEmpty()) {
        if (error) {
            *error = makeError(config.deviceId,
                               "DeviceDriverMissing",
                               "Device driver id is required",
                               "Set driverId to a registered device session factory");
        }
        return false;
    }

    m_configs.insert(config.deviceId, config);
    publishState(config.deviceId, DeviceConnectionState::Disconnected, "device configured");
    return true;
}

std::optional<DeviceSessionConfig> DeviceSessionManager::deviceConfig(const DeviceId& deviceId) const
{
    const auto it = m_configs.constFind(deviceId);
    if (it == m_configs.constEnd()) {
        return std::nullopt;
    }
    return it.value();
}

std::shared_ptr<IDeviceSession> DeviceSessionManager::session(const DeviceId& deviceId) const
{
    return m_sessions.value(deviceId);
}

DeviceConnectionState DeviceSessionManager::stateOf(const DeviceId& deviceId) const
{
    const auto existing = session(deviceId);
    return existing ? existing->state() : DeviceConnectionState::Disconnected;
}

DeviceSessionOpenResult DeviceSessionManager::openSession(const DeviceId& deviceId)
{
    DeviceSessionOpenResult result;

    const auto configIt = m_configs.constFind(deviceId);
    if (configIt == m_configs.constEnd()) {
        result.error = makeError(deviceId,
                                 "DeviceNotConfigured",
                                 QString("Device is not configured: %1").arg(deviceId),
                                 "Add the device to station configuration before opening it");
        publishState(deviceId,
                     DeviceConnectionState::Error,
                     result.error.message,
                     result.error.errorCode);
        return result;
    }

    auto existing = session(deviceId);
    if (existing && existing->state() == DeviceConnectionState::Connected) {
        QString healthError;
        if (existing->isHealthy(healthError)) {
            result.session = existing;
            result.reusedExisting = true;
            publishState(deviceId, DeviceConnectionState::Connected, "healthy session reused");
            return result;
        }
        existing->disconnect();
        publishState(deviceId,
                     DeviceConnectionState::Disconnected,
                     healthError.isEmpty() ? "unhealthy session disconnected" : healthError);
    }

    const auto& config = configIt.value();
    if (!existing) {
        const auto factoryIt = m_factories.constFind(config.driverId);
        if (factoryIt == m_factories.constEnd()) {
            result.error = makeError(deviceId,
                                     "DeviceDriverNotRegistered",
                                     QString("Device driver is not registered: %1").arg(config.driverId),
                                     "Register an IDeviceSessionFactory for this driver id");
            publishState(deviceId,
                         DeviceConnectionState::Error,
                         result.error.message,
                         result.error.errorCode);
            return result;
        }

        DeviceSessionError createError;
        existing = factoryIt.value()->createSession(config, createError);
        if (!existing || createError.hasError()) {
            result.error = createError.hasError()
                ? createError
                : makeError(deviceId,
                            "DeviceSessionCreateFailed",
                            QString("Device session factory returned no session: %1").arg(deviceId));
            publishState(deviceId,
                         DeviceConnectionState::Error,
                         result.error.message,
                         result.error.errorCode);
            return result;
        }
        m_sessions.insert(deviceId, existing);
    }

    publishState(deviceId, DeviceConnectionState::Connecting, "connecting");
    QString connectError;
    if (!existing->connect(connectError)) {
        result.error = makeError(deviceId,
                                 "DeviceConnectFailed",
                                 connectError.isEmpty()
                                     ? QString("Device connection failed: %1").arg(deviceId)
                                     : connectError,
                                 "Check station wiring, address, driver installation, and device power");
        publishState(deviceId,
                     DeviceConnectionState::Error,
                     result.error.message,
                     result.error.errorCode);
        return result;
    }

    result.session = existing;
    publishState(deviceId, DeviceConnectionState::Connected, "connected");
    return result;
}

DeviceSessionError DeviceSessionManager::closeSession(const DeviceId& deviceId)
{
    const auto existing = session(deviceId);
    if (!existing) {
        return {};
    }

    existing->disconnect();
    publishState(deviceId, DeviceConnectionState::Disconnected, "disconnected");
    return {};
}

void DeviceSessionManager::closeAll()
{
    for (const auto& session : m_sessions) {
        if (session) {
            session->disconnect();
            publishState(session->deviceId(), DeviceConnectionState::Disconnected, "disconnected");
        }
    }
}

QVector<DeviceId> DeviceSessionManager::configuredDeviceIds() const
{
    QVector<DeviceId> ids;
    ids.reserve(m_configs.size());
    for (auto it = m_configs.constBegin(); it != m_configs.constEnd(); ++it) {
        ids.push_back(it.key());
    }
    std::sort(ids.begin(), ids.end());
    return ids;
}

QVector<DeviceId> DeviceSessionManager::openedDeviceIds() const
{
    QVector<DeviceId> ids;
    ids.reserve(m_sessions.size());
    for (auto it = m_sessions.constBegin(); it != m_sessions.constEnd(); ++it) {
        ids.push_back(it.key());
    }
    std::sort(ids.begin(), ids.end());
    return ids;
}

DeviceSessionError DeviceSessionManager::makeError(const DeviceId& deviceId,
                                                   QString errorCode,
                                                   QString message,
                                                   QString suggestion) const
{
    DeviceSessionError error;
    error.deviceId = deviceId;
    error.errorCode = std::move(errorCode);
    error.message = std::move(message);
    error.suggestion = std::move(suggestion);
    return error;
}

void DeviceSessionManager::publishState(const DeviceId& deviceId,
                                        DeviceConnectionState state,
                                        const QString& message,
                                        const QString& errorCode) const
{
    if (!m_events) {
        return;
    }

    RuntimeEvent event;
    event.kind = RuntimeEventKind::DeviceStateChanged;
    event.deviceId = deviceId;
    event.deviceState = state;
    event.message = message;
    event.errorCode = errorCode;
    const auto config = deviceConfig(deviceId);
    if (config) {
        event.details.insert("deviceType", config->deviceType);
        event.details.insert("driverId", config->driverId);
        event.details.insert("address", config->address);
        event.details.insert("lifetime", deviceSessionLifetimeName(config->lifetime));
    }
    m_events->publish(event);
}

QString deviceConnectionStateName(DeviceConnectionState state)
{
    switch (state) {
    case DeviceConnectionState::Disconnected:
        return "Disconnected";
    case DeviceConnectionState::Connecting:
        return "Connecting";
    case DeviceConnectionState::Connected:
        return "Connected";
    case DeviceConnectionState::Error:
        return "Error";
    }
    return "Unknown";
}

QString deviceSessionLifetimeName(DeviceSessionLifetime lifetime)
{
    switch (lifetime) {
    case DeviceSessionLifetime::Step:
        return "Step";
    case DeviceSessionLifetime::Run:
        return "Run";
    case DeviceSessionLifetime::Station:
        return "Station";
    }
    return "Unknown";
}

} // namespace PicoATE::Core
