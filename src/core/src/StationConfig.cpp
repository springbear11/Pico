#include "PicoATE/Core/StationConfig.h"

#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QSet>

namespace PicoATE::Core {

namespace {

void addError(StationConfigResult& result,
              const QString& path,
              const QString& message,
              const QString& suggestion = {})
{
    result.errors.push_back({path, message, suggestion});
}

QString typeName(const QJsonValue& value)
{
    if (value.isString()) {
        return "string";
    }
    if (value.isBool()) {
        return "bool";
    }
    if (value.isDouble()) {
        return "number";
    }
    if (value.isArray()) {
        return "array";
    }
    if (value.isObject()) {
        return "object";
    }
    if (value.isNull()) {
        return "null";
    }
    return "undefined";
}

QString normalized(QString value)
{
    value = value.trimmed().toLower();
    value.remove('-');
    value.remove('_');
    value.remove(' ');
    return value;
}

QString readString(const QJsonObject& object,
                   const QString& key,
                   StationConfigResult& result,
                   const QString& path,
                   const QString& fallback = {})
{
    if (!object.contains(key)) {
        return fallback;
    }
    const auto value = object.value(key);
    if (!value.isString()) {
        addError(result,
                 path,
                 QString("Expected string, got %1").arg(typeName(value)),
                 "Fix the station config field type");
        return fallback;
    }
    return value.toString();
}

bool readBool(const QJsonObject& object,
              const QString& key,
              StationConfigResult& result,
              const QString& path,
              bool fallback = true)
{
    if (!object.contains(key)) {
        return fallback;
    }
    const auto value = object.value(key);
    if (!value.isBool()) {
        addError(result,
                 path,
                 QString("Expected bool, got %1").arg(typeName(value)),
                 "Fix the station config field type");
        return fallback;
    }
    return value.toBool();
}

QVariantMap readObjectMap(const QJsonObject& object,
                          const QString& key,
                          StationConfigResult& result,
                          const QString& path)
{
    if (!object.contains(key)) {
        return {};
    }
    const auto value = object.value(key);
    if (!value.isObject()) {
        addError(result,
                 path,
                 QString("Expected object, got %1").arg(typeName(value)),
                 "Fix the station config field type");
        return {};
    }
    return value.toObject().toVariantMap();
}

void appendResolutionErrors(StationConfigResult& result,
                            const QVector<VariableResolutionError>& errors)
{
    for (const auto& error : errors) {
        addError(result, error.path, error.message, error.suggestion);
    }
}

QString resolveStringField(const QString& value,
                           const VariableResolver& resolver,
                           StationConfigResult& result,
                           const QString& path)
{
    QVector<VariableResolutionError> errors;
    const auto resolved = resolver.resolveString(value, errors, path);
    appendResolutionErrors(result, errors);
    return resolved;
}

QVariantMap resolveMapField(const QVariantMap& value,
                            const VariableResolver& resolver,
                            StationConfigResult& result,
                            const QString& path)
{
    QVector<VariableResolutionError> errors;
    const auto resolved = resolver.resolveMap(value, errors, path);
    appendResolutionErrors(result, errors);
    return resolved;
}

QString deviceIdFromObject(const QJsonObject& object,
                           StationConfigResult& result,
                           const QString& path)
{
    const auto key = object.contains("deviceId") ? QString("deviceId") : QString("id");
    return readString(object, key, result, QString("%1.%2").arg(path, key));
}

DeviceSessionConfig parseDevice(const QJsonObject& object,
                                int index,
                                const VariableResolver& resolver,
                                StationConfigResult& result)
{
    const auto path = QString("devices[%1]").arg(index);

    DeviceSessionConfig config;
    config.deviceId = deviceIdFromObject(object, result, path);
    config.deviceType = readString(object, "deviceType", result, QString("%1.deviceType").arg(path));
    if (config.deviceType.isEmpty() && object.contains("type")) {
        config.deviceType = readString(object, "type", result, QString("%1.type").arg(path));
    }

    config.driverId = readString(object, "driverId", result, QString("%1.driverId").arg(path));
    if (config.driverId.isEmpty() && object.contains("driver")) {
        config.driverId = readString(object, "driver", result, QString("%1.driver").arg(path));
    }

    config.address = readString(object, "address", result, QString("%1.address").arg(path));
    if (config.address.isEmpty() && object.contains("visaAddress")) {
        config.address = readString(object, "visaAddress", result, QString("%1.visaAddress").arg(path));
    }

    const auto lifetimeText = readString(object,
                                         "lifetime",
                                         result,
                                         QString("%1.lifetime").arg(path),
                                         deviceSessionLifetimeName(config.lifetime));
    bool lifetimeOk = false;
    config.lifetime = deviceSessionLifetimeFromString(lifetimeText, &lifetimeOk);
    if (!lifetimeOk) {
        addError(result,
                 QString("%1.lifetime").arg(path),
                 QString("Unsupported device lifetime: %1").arg(lifetimeText),
                 "Use step, run, or station");
    }

    config.options = readObjectMap(object, "options", result, QString("%1.options").arg(path));

    config.deviceId = resolveStringField(config.deviceId, resolver, result, QString("%1.deviceId").arg(path));
    config.deviceType = resolveStringField(config.deviceType, resolver, result, QString("%1.deviceType").arg(path));
    config.driverId = resolveStringField(config.driverId, resolver, result, QString("%1.driverId").arg(path));
    config.address = resolveStringField(config.address, resolver, result, QString("%1.address").arg(path));
    config.options = resolveMapField(config.options, resolver, result, QString("%1.options").arg(path));

    if (config.deviceId.trimmed().isEmpty()) {
        addError(result, QString("%1.deviceId").arg(path), "Device id is required", "Set deviceId or id");
    }
    if (config.driverId.trimmed().isEmpty()) {
        addError(result, QString("%1.driverId").arg(path), "Device driver id is required", "Set driverId or driver");
    }

    return config;
}

} // namespace

bool StationConfigResult::ok() const
{
    return errors.isEmpty();
}

StationConfigResult parseStationConfigJson(const QJsonObject& object,
                                           const VariableResolverOptions& resolverOptions)
{
    StationConfigResult result;
    VariableResolver resolver(resolverOptions);

    result.config.stationId = readString(object, "stationId", result, "stationId");
    if (result.config.stationId.isEmpty() && object.contains("id")) {
        result.config.stationId = readString(object, "id", result, "id");
    }
    result.config.name = readString(object, "name", result, "name", result.config.stationId);
    result.config.metadata = readObjectMap(object, "metadata", result, "metadata");

    result.config.stationId = resolveStringField(result.config.stationId, resolver, result, "stationId");
    result.config.name = resolveStringField(result.config.name, resolver, result, "name");
    result.config.metadata = resolveMapField(result.config.metadata, resolver, result, "metadata");

    if (!object.contains("devices")) {
        addError(result, "devices", "Station config must contain devices array", "Add devices: []");
        return result;
    }
    if (!object.value("devices").isArray()) {
        addError(result,
                 "devices",
                 QString("Expected array, got %1").arg(typeName(object.value("devices"))),
                 "Use an array of device objects");
        return result;
    }

    QSet<DeviceId> seenDeviceIds;
    const auto devices = object.value("devices").toArray();
    for (int i = 0; i < devices.size(); ++i) {
        const auto path = QString("devices[%1]").arg(i);
        if (!devices[i].isObject()) {
            addError(result,
                     path,
                     QString("Expected object, got %1").arg(typeName(devices[i])),
                     "Use a device object");
            continue;
        }

        const auto deviceObject = devices[i].toObject();
        if (!readBool(deviceObject, "enabled", result, QString("%1.enabled").arg(path), true)) {
            continue;
        }

        auto config = parseDevice(deviceObject, i, resolver, result);
        if (!config.deviceId.isEmpty()) {
            if (seenDeviceIds.contains(config.deviceId)) {
                addError(result,
                         QString("%1.deviceId").arg(path),
                         QString("Duplicate device id: %1").arg(config.deviceId),
                         "Use unique logical device ids per station");
            } else {
                seenDeviceIds.insert(config.deviceId);
            }
        }
        result.config.devices.push_back(config);
    }

    return result;
}

StationConfigResult loadStationConfigFile(const QString& filePath,
                                          VariableResolverOptions resolverOptions)
{
    StationConfigResult result;
    const auto absolutePath = QFileInfo(filePath).absoluteFilePath();
    if (resolverOptions.sequenceFilePath.isEmpty()) {
        resolverOptions.sequenceFilePath = absolutePath;
    }

    QFile file(absolutePath);
    if (!file.open(QIODevice::ReadOnly)) {
        addError(result,
                 "stationConfig",
                 QString("Failed to open station config: %1").arg(absolutePath),
                 file.errorString());
        return result;
    }

    QJsonParseError parseError;
    const auto document = QJsonDocument::fromJson(file.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError) {
        addError(result,
                 "stationConfig",
                 QString("Failed to parse station config JSON at offset %1").arg(parseError.offset),
                 parseError.errorString());
        return result;
    }
    if (!document.isObject()) {
        addError(result,
                 "stationConfig",
                 "Station config must be a JSON object",
                 "Use an object with stationId and devices");
        return result;
    }

    return parseStationConfigJson(document.object(), resolverOptions);
}

QVector<StationConfigDiagnostic> configureDeviceSessions(const StationConfig& config,
                                                         DeviceSessionManager& manager)
{
    QVector<StationConfigDiagnostic> errors;
    for (int i = 0; i < config.devices.size(); ++i) {
        DeviceSessionError error;
        if (!manager.configureDevice(config.devices[i], &error)) {
            errors.push_back({
                QString("devices[%1]").arg(i),
                error.message,
                error.suggestion,
            });
        }
    }
    return errors;
}

DeviceSessionLifetime deviceSessionLifetimeFromString(const QString& value, bool* ok)
{
    const auto text = normalized(value);
    if (ok) {
        *ok = true;
    }

    if (text == "step") {
        return DeviceSessionLifetime::Step;
    }
    if (text == "run") {
        return DeviceSessionLifetime::Run;
    }
    if (text == "station") {
        return DeviceSessionLifetime::Station;
    }

    if (ok) {
        *ok = false;
    }
    return DeviceSessionLifetime::Station;
}

} // namespace PicoATE::Core
