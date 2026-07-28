#pragma once

#include <QHash>
#include <QJsonObject>
#include <QString>
#include <QStringList>
#include <QVariantMap>
#include <QVector>

#include <optional>

namespace PicoATE::Core {

enum class DeviceDiscoveryKind {
    SerialPort,
    VisaResource,
    CanDevice
};

enum class DeviceConnectionKind {
    CanSerial,
    Visa,
    SerialPort,
    TcpIp,
    Manual
};

QString deviceConnectionKindName(DeviceConnectionKind kind);
std::optional<DeviceConnectionKind> deviceConnectionKindFromString(
    const QString& value);
DeviceConnectionKind inferDeviceConnectionKind(const QString& deviceType,
                                               const QString& resource);
std::optional<DeviceDiscoveryKind> discoveryKindForConnection(
    DeviceConnectionKind kind);

struct DeviceDiscoveryRequest {
    DeviceDiscoveryKind kind = DeviceDiscoveryKind::SerialPort;
    QString driverId;
    QString pluginDllPath;
    QString nativeHostProgram;
    QVariantMap options;
    int timeoutMs = 5000;
};

struct DiscoveredDeviceResource {
    QString resourceId;
    QString displayName;
    QString serialNumber;
    QString model;
    QVariantMap runtimeLocator;
};

struct DeviceDiscoveryResult {
    QVector<DiscoveredDeviceResource> resources;
    QString errorCode;
    QString errorMessage;

    bool ok() const { return errorCode.isEmpty(); }
};

class IDeviceDiscoveryService {
public:
    virtual ~IDeviceDiscoveryService() = default;
    virtual DeviceDiscoveryResult discover(const DeviceDiscoveryRequest& request) = 0;
};

class SystemDeviceDiscoveryService final : public IDeviceDiscoveryService {
public:
    DeviceDiscoveryResult discover(const DeviceDiscoveryRequest& request) override;
};

struct StationFieldDevice {
    QString logicalId;
    QString deviceType;
    QString driverId;
    QString connectionKind;
    QString resource;
    // Compatibility projections for older UI consumers.
    QString address;
    QString serialNumber;
    QStringList memberDeviceIds;
};

QVector<StationFieldDevice> stationFieldDevices(const QJsonObject& station);

bool applyStationFieldBinding(QJsonObject& station,
                              const QString& logicalId,
                              const QString& resourceId,
                              QString* errorMessage = nullptr);

bool applyStationFieldBinding(QJsonObject& station,
                              const QString& logicalId,
                              const QString& connectionKind,
                              const QString& resourceId,
                              QString* errorMessage = nullptr);

QJsonObject effectiveStationSnapshot(
    const QJsonObject& persistedStation,
    const QHash<QString, int>& canDeviceIndices);

} // namespace PicoATE::Core
