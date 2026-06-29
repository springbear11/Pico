#pragma once

#include <QHash>
#include <QString>
#include <QVariantMap>
#include <QVector>

#include <memory>
#include <optional>

namespace PicoATE::Core {

class RuntimeEventEmitter;

using DeviceId = QString;
using DeviceDriverId = QString;

enum class DeviceSessionLifetime {
    Step,
    Run,
    Station
};

enum class DeviceConnectionState {
    Disconnected,
    Connecting,
    Connected,
    Error
};

struct DeviceSessionConfig {
    DeviceId deviceId;
    QString deviceType;
    DeviceDriverId driverId;
    QString address;
    DeviceSessionLifetime lifetime = DeviceSessionLifetime::Station;
    QVariantMap options;
};

struct DeviceSessionError {
    DeviceId deviceId;
    QString errorCode;
    QString message;
    QString suggestion;

    bool hasError() const;
};

class IDeviceSession {
public:
    virtual ~IDeviceSession() = default;

    virtual DeviceId deviceId() const = 0;
    virtual QString deviceType() const = 0;
    virtual DeviceConnectionState state() const = 0;
    virtual bool connect(QString& errorMessage) = 0;
    virtual void disconnect() = 0;
    virtual QVariantMap metadata() const;
    virtual bool isHealthy(QString& errorMessage) const;
};

class IDeviceSessionFactory {
public:
    virtual ~IDeviceSessionFactory() = default;

    virtual DeviceDriverId driverId() const = 0;
    virtual std::shared_ptr<IDeviceSession> createSession(const DeviceSessionConfig& config,
                                                          DeviceSessionError& error) = 0;
};

struct DeviceSessionOpenResult {
    std::shared_ptr<IDeviceSession> session;
    bool reusedExisting = false;
    DeviceSessionError error;

    bool ok() const;
};

class DeviceSessionManager {
public:
    void setRuntimeEventEmitter(RuntimeEventEmitter* events);
    bool registerFactory(std::shared_ptr<IDeviceSessionFactory> factory);
    bool configureDevice(const DeviceSessionConfig& config, DeviceSessionError* error = nullptr);

    std::optional<DeviceSessionConfig> deviceConfig(const DeviceId& deviceId) const;
    std::shared_ptr<IDeviceSession> session(const DeviceId& deviceId) const;
    DeviceConnectionState stateOf(const DeviceId& deviceId) const;

    DeviceSessionOpenResult openSession(const DeviceId& deviceId);
    DeviceSessionError closeSession(const DeviceId& deviceId);
    void closeAll();

    QVector<DeviceId> configuredDeviceIds() const;
    QVector<DeviceId> openedDeviceIds() const;

private:
    void publishState(const DeviceId& deviceId,
                      DeviceConnectionState state,
                      const QString& message = {},
                      const QString& errorCode = {}) const;
    DeviceSessionError makeError(const DeviceId& deviceId,
                                 QString errorCode,
                                 QString message,
                                 QString suggestion = {}) const;

    QHash<DeviceDriverId, std::shared_ptr<IDeviceSessionFactory>> m_factories;
    QHash<DeviceId, DeviceSessionConfig> m_configs;
    QHash<DeviceId, std::shared_ptr<IDeviceSession>> m_sessions;
    RuntimeEventEmitter* m_events = nullptr;
};

QString deviceConnectionStateName(DeviceConnectionState state);
QString deviceSessionLifetimeName(DeviceSessionLifetime lifetime);

} // namespace PicoATE::Core
