#include "PicoATE/Core/DeviceDiscovery.h"

#include "PicoATE/Core/QProcessTransport.h"

#include <QJsonArray>
#include <QLibrary>
#include <QRegularExpression>
#include <QSettings>
#include <QUuid>

#include <algorithm>

namespace PicoATE::Core {

namespace {

QString baseDeviceId(const QString& deviceId)
{
    static const QRegularExpression channelSuffix(
        QStringLiteral("\\.CH\\d+$"), QRegularExpression::CaseInsensitiveOption);
    auto result = deviceId.trimmed();
    result.remove(channelSuffix);
    return result;
}

DeviceDiscoveryResult serialPorts()
{
    DeviceDiscoveryResult result;
#ifdef Q_OS_WIN
    QSettings ports(QStringLiteral("HKEY_LOCAL_MACHINE\\HARDWARE\\DEVICEMAP\\SERIALCOMM"),
                    QSettings::NativeFormat);
    QStringList names;
    for (const auto& key : ports.allKeys()) {
        const auto name = ports.value(key).toString().trimmed();
        if (!name.isEmpty() && !names.contains(name, Qt::CaseInsensitive)) {
            names.push_back(name);
        }
    }
    std::sort(names.begin(), names.end(), [](const QString& left, const QString& right) {
        return left.localeAwareCompare(right) < 0;
    });
    for (const auto& name : names) {
        result.resources.push_back({name, name, {}, {}, {{QStringLiteral("portName"), name}}});
    }
#else
    result.errorCode = QStringLiteral("UnsupportedPlatform");
    result.errorMessage = QStringLiteral("Serial port discovery is currently implemented for Windows");
#endif
    return result;
}

DeviceDiscoveryResult visaResources()
{
    using ViStatus = qint32;
    using ViSession = quint32;
    using ViFindList = quint32;
    using ViUInt32 = quint32;
    using OpenDefaultRm = ViStatus (*)(ViSession*);
    using FindRsrc = ViStatus (*)(ViSession, const char*, ViFindList*, ViUInt32*, char*);
    using FindNext = ViStatus (*)(ViFindList, char*);
    using Close = ViStatus (*)(ViSession);

    DeviceDiscoveryResult result;
    QLibrary visa(QStringLiteral("visa64"));
    if (!visa.load()) {
        visa.setFileName(QStringLiteral("visa32"));
    }
    if (!visa.isLoaded() && !visa.load()) {
        result.errorCode = QStringLiteral("VisaLibraryNotFound");
        result.errorMessage = QStringLiteral("VISA runtime was not found (visa64.dll/visa32.dll)");
        return result;
    }

    const auto openDefaultRm = reinterpret_cast<OpenDefaultRm>(visa.resolve("viOpenDefaultRM"));
    const auto findRsrc = reinterpret_cast<FindRsrc>(visa.resolve("viFindRsrc"));
    const auto findNext = reinterpret_cast<FindNext>(visa.resolve("viFindNext"));
    const auto close = reinterpret_cast<Close>(visa.resolve("viClose"));
    if (!openDefaultRm || !findRsrc || !findNext || !close) {
        result.errorCode = QStringLiteral("VisaSymbolMissing");
        result.errorMessage = QStringLiteral("The installed VISA runtime is missing resource discovery functions");
        return result;
    }

    ViSession resourceManager = 0;
    if (openDefaultRm(&resourceManager) < 0) {
        result.errorCode = QStringLiteral("VisaOpenFailed");
        result.errorMessage = QStringLiteral("viOpenDefaultRM failed");
        return result;
    }
    ViFindList list = 0;
    ViUInt32 count = 0;
    char description[1024]{};
    const auto status = findRsrc(resourceManager, "?*INSTR", &list, &count, description);
    if (status >= 0) {
        for (ViUInt32 index = 0; index < count; ++index) {
            if (index > 0 && findNext(list, description) < 0) {
                break;
            }
            const auto resource = QString::fromLocal8Bit(description).trimmed();
            if (!resource.isEmpty()) {
                result.resources.push_back(
                    {resource, resource, {}, {}, {{QStringLiteral("visaResource"), resource}}});
            }
        }
        close(list);
    }
    close(resourceManager);
    return result;
}

DeviceDiscoveryResult canDevices(const DeviceDiscoveryRequest& request)
{
    DeviceDiscoveryResult result;
    if (request.nativeHostProgram.trimmed().isEmpty() ||
        request.pluginDllPath.trimmed().isEmpty()) {
        result.errorCode = QStringLiteral("CanDiscoveryConfigurationMissing");
        result.errorMessage = QStringLiteral("NativeHost and CAN plugin DLL are required");
        return result;
    }

    QProcessTransport transport(request.nativeHostProgram,
                                {QStringLiteral("--dll"), request.pluginDllPath,
                                 QStringLiteral("--vendor-stdio"), QStringLiteral("discard")});
    ModuleTransportRequest call;
    call.traceId = QUuid::createUuid().toString(QUuid::WithoutBraces);
    call.moduleId = request.driverId;
    call.functionName = QStringLiteral("findDevices");
    call.context.inputs = request.options;
    ModuleTransportResponse response;
    const auto status = transport.call(call, response, request.timeoutMs);
    if (status != ModuleTransportStatus::Ok || response.outcome != ModuleOutcome::Passed) {
        result.errorCode = response.errorCode.isEmpty()
            ? QStringLiteral("CanDiscoveryFailed") : response.errorCode;
        result.errorMessage = response.errorMessage.isEmpty()
            ? QStringLiteral("CAN device discovery failed") : response.errorMessage;
        return result;
    }

    const auto devices = response.outputs.value(QStringLiteral("devices")).toList();
    for (const auto& value : devices) {
        const auto device = value.toMap();
        const auto serial = device.value(QStringLiteral("serialNumber")).toString().trimmed();
        if (serial.isEmpty()) {
            continue;
        }
        DiscoveredDeviceResource resource;
        resource.resourceId = serial;
        resource.serialNumber = serial;
        resource.model = device.value(QStringLiteral("model")).toString();
        resource.displayName = resource.model.isEmpty()
            ? serial : QStringLiteral("%1  |  %2").arg(serial, resource.model);
        resource.runtimeLocator = device;
        result.resources.push_back(std::move(resource));
    }
    return result;
}

} // namespace

DeviceDiscoveryResult SystemDeviceDiscoveryService::discover(
    const DeviceDiscoveryRequest& request)
{
    switch (request.kind) {
    case DeviceDiscoveryKind::SerialPort:
        return serialPorts();
    case DeviceDiscoveryKind::VisaResource:
        return visaResources();
    case DeviceDiscoveryKind::CanDevice:
        return canDevices(request);
    }
    return {};
}

QVector<StationFieldDevice> stationFieldDevices(const QJsonObject& station)
{
    QVector<StationFieldDevice> result;
    QHash<QString, int> positions;
    const auto devices = station.value(QStringLiteral("devices")).toArray();
    for (const auto& value : devices) {
        if (!value.isObject()) {
            continue;
        }
        const auto object = value.toObject();
        if (!object.value(QStringLiteral("enabled")).toBool(true)) {
            continue;
        }
        const auto memberId = object.value(QStringLiteral("deviceId")).toString().trimmed();
        const auto logicalId = baseDeviceId(memberId);
        if (logicalId.isEmpty()) {
            continue;
        }
        auto position = positions.value(logicalId, -1);
        if (position < 0) {
            StationFieldDevice device;
            device.logicalId = logicalId;
            device.deviceType = object.value(QStringLiteral("deviceType")).toString().trimmed().toUpper();
            device.driverId = object.value(QStringLiteral("driverId")).toString().trimmed();
            device.address = object.value(QStringLiteral("address")).toString().trimmed();
            device.serialNumber = object.value(QStringLiteral("options")).toObject()
                                      .value(QStringLiteral("serialNumber")).toString().trimmed();
            result.push_back(std::move(device));
            position = result.size() - 1;
            positions.insert(logicalId, position);
        }
        result[position].memberDeviceIds.push_back(memberId);
    }
    return result;
}

bool applyStationFieldBinding(QJsonObject& station,
                              const QString& logicalId,
                              const QString& resourceId,
                              QString* errorMessage)
{
    auto devices = station.value(QStringLiteral("devices")).toArray();
    bool found = false;
    for (int index = 0; index < devices.size(); ++index) {
        if (!devices[index].isObject()) {
            continue;
        }
        auto object = devices[index].toObject();
        if (baseDeviceId(object.value(QStringLiteral("deviceId")).toString()) != logicalId) {
            continue;
        }
        found = true;
        const auto type = object.value(QStringLiteral("deviceType")).toString().trimmed().toUpper();
        if (type == QStringLiteral("CAN")) {
            auto options = object.value(QStringLiteral("options")).toObject();
            options.insert(QStringLiteral("serialNumber"), resourceId.trimmed());
            options.remove(QStringLiteral("deviceIndex"));
            object.insert(QStringLiteral("options"), options);
        } else {
            object.insert(QStringLiteral("address"), resourceId.trimmed());
        }
        devices[index] = object;
    }
    if (!found) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("Station device %1 no longer exists").arg(logicalId);
        }
        return false;
    }
    station.insert(QStringLiteral("devices"), devices);
    return true;
}

QJsonObject effectiveStationSnapshot(
    const QJsonObject& persistedStation,
    const QHash<QString, int>& canDeviceIndices)
{
    auto result = persistedStation;
    auto devices = result.value(QStringLiteral("devices")).toArray();
    for (int index = 0; index < devices.size(); ++index) {
        if (!devices[index].isObject()) {
            continue;
        }
        auto object = devices[index].toObject();
        if (object.value(QStringLiteral("deviceType")).toString()
                .compare(QStringLiteral("CAN"), Qt::CaseInsensitive) != 0) {
            continue;
        }
        auto options = object.value(QStringLiteral("options")).toObject();
        options.remove(QStringLiteral("deviceIndex"));
        const auto logicalId = baseDeviceId(object.value(QStringLiteral("deviceId")).toString());
        const auto deviceIndex = canDeviceIndices.constFind(logicalId);
        if (deviceIndex != canDeviceIndices.constEnd()) {
            options.insert(QStringLiteral("deviceIndex"), deviceIndex.value());
        }
        object.insert(QStringLiteral("options"), options);
        devices[index] = object;
    }
    result.insert(QStringLiteral("devices"), devices);
    return result;
}

} // namespace PicoATE::Core
