#pragma once

#include <QHash>
#include <QJsonObject>
#include <QString>
#include <QStringList>
#include <QVariantMap>
#include <QVector>

namespace PicoATE::Core {

enum class DeviceDiscoveryKind {
    SerialPort,
    VisaResource,
    CanDevice
};

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
    QString address;
    QString serialNumber;
    QStringList memberDeviceIds;
};

QVector<StationFieldDevice> stationFieldDevices(const QJsonObject& station);

bool applyStationFieldBinding(QJsonObject& station,
                              const QString& logicalId,
                              const QString& resourceId,
                              QString* errorMessage = nullptr);

QJsonObject effectiveStationSnapshot(
    const QJsonObject& persistedStation,
    const QHash<QString, int>& canDeviceIndices);

} // namespace PicoATE::Core
