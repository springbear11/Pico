#include "PicoATE/Core/StationRunPreparation.h"

#include "PicoATE/Core/VariableResolver.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QRegularExpression>
#include <QSet>

#include <algorithm>
#include <utility>

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

void addError(StationRunPreparationResult& result,
              QString path,
              QString message,
              QString suggestion = {})
{
    result.errors.push_back(
        {std::move(path), std::move(message), std::move(suggestion)});
}

void appendErrors(StationRunPreparationResult& result,
                  const QVector<StationConfigDiagnostic>& errors)
{
    result.errors += errors;
}

void addUnavailableDevice(StationRunPreparationResult& result,
                          QHash<QString, QString>& reasons,
                          const QString& deviceId,
                          QString path,
                          QString message,
                          QString suggestion = {})
{
    if (reasons.contains(deviceId)) {
        return;
    }
    reasons.insert(deviceId, message);
    result.warnings.push_back(
        {std::move(path), std::move(message), std::move(suggestion)});
}

QJsonObject markUnavailableDevices(
    QJsonObject station,
    const QHash<QString, QString>& reasons)
{
    if (reasons.isEmpty()) {
        return station;
    }
    auto devices = station.value(QStringLiteral("devices")).toArray();
    for (int index = 0; index < devices.size(); ++index) {
        if (!devices[index].isObject()) {
            continue;
        }
        auto device = devices[index].toObject();
        const auto deviceId = device.value(QStringLiteral("deviceId"))
                                  .toString().trimmed();
        auto reason = reasons.value(deviceId);
        if (reason.isEmpty()) {
            reason = reasons.value(baseDeviceId(deviceId));
        }
        if (reason.isEmpty()) {
            continue;
        }
        auto deviceOptions = device.value(QStringLiteral("options")).toObject();
        deviceOptions.remove(QStringLiteral("deviceIndex"));
        deviceOptions.insert(QStringLiteral("__picoateUnavailable"), true);
        deviceOptions.insert(QStringLiteral("__picoateUnavailableReason"), reason);
        device.insert(QStringLiteral("options"), deviceOptions);
        devices[index] = device;
    }
    station.insert(QStringLiteral("devices"), devices);
    return station;
}

void setEffectiveResult(StationRunPreparationResult& result,
                        const QJsonObject& resolvedStation,
                        const QHash<QString, int>& runtimeIndices,
                        const QHash<QString, QString>& unavailableReasons,
                        const VariableResolverOptions& resolverOptions)
{
    result.effectiveStation = markUnavailableDevices(
        effectiveStationSnapshot(resolvedStation, runtimeIndices),
        unavailableReasons);
    const auto effectiveConfig = parseStationConfigJson(
        result.effectiveStation, resolverOptions);
    result.stationConfig = effectiveConfig.config;
    appendErrors(result, effectiveConfig.errors);
}

QString absolutePath(QString path, const QDir& baseDirectory)
{
    path = path.trimmed();
    if (path.isEmpty() || QFileInfo(path).isAbsolute()) {
        return QFileInfo(path).absoluteFilePath();
    }
    return QFileInfo(baseDirectory.absoluteFilePath(path)).absoluteFilePath();
}

QString moduleIdFromDllPath(const QString& filePath)
{
    auto name = QFileInfo(filePath).completeBaseName().trimmed().toLower();
    if (name.startsWith(QStringLiteral("picoate."))) {
        name.remove(0, QStringLiteral("picoate.").size());
    }
    name.replace(QRegularExpression(QStringLiteral("[^a-z0-9]+")),
                 QStringLiteral("."));
    while (name.startsWith('.')) name.remove(0, 1);
    while (name.endsWith('.')) name.chop(1);
    return name.isEmpty() ? QString() : QStringLiteral("plugin.") + name;
}

struct RegistryPlugin {
    QString dllPath;
    QStringList connectionKinds;
};

QStringList fallbackConnectionKinds(const QJsonObject& description,
                                    const QString& moduleId)
{
    auto category = description.value(QStringLiteral("category"))
                        .toString().trimmed().toUpper();
    if (category.isEmpty()) {
        const auto id = moduleId.toLower();
        if (id.contains(QStringLiteral(".can."))) category = QStringLiteral("CAN");
        else if (id.contains(QStringLiteral(".dmm."))) category = QStringLiteral("DMM");
        else if (id.contains(QStringLiteral(".psu."))) category = QStringLiteral("PSU");
        else if (id.contains(QStringLiteral(".scope."))) category = QStringLiteral("SCOPE");
        else if (id.contains(QStringLiteral(".serial."))) category = QStringLiteral("SERIAL");
        else if (id.contains(QStringLiteral(".modbus."))) category = QStringLiteral("MODBUS");
    }
    if (category == QStringLiteral("CAN")) return {QStringLiteral("canSerial")};
    if (category == QStringLiteral("DMM") || category == QStringLiteral("PSU") ||
        category == QStringLiteral("SCOPE")) return {QStringLiteral("visa")};
    if (category == QStringLiteral("SERIAL")) return {QStringLiteral("serialPort")};
    if (category == QStringLiteral("MODBUS")) {
        return {QStringLiteral("serialPort"), QStringLiteral("tcpIp")};
    }
    return {QStringLiteral("manual")};
}

QHash<QString, RegistryPlugin> loadPluginDlls(
    const StationConfig& station,
    const StationRunPreparationOptions& options,
    StationRunPreparationResult& result)
{
    VariableResolverOptions resolverOptions;
    resolverOptions.sequenceFilePath = QFileInfo(options.stationFilePath).absoluteFilePath();
    resolverOptions.projectDir = options.projectDir;
    resolverOptions.variables = options.variables;
    const VariableResolver resolver(resolverOptions);

    QVector<VariableResolutionError> resolutionErrors;
    auto registryValue = resolver.resolveString(
        station.pluginRegistryPath, resolutionErrors, QStringLiteral("pluginRegistry"));
    for (const auto& error : resolutionErrors) {
        addError(result, error.path, error.message, error.suggestion);
    }
    if (!resolutionErrors.isEmpty()) return {};

    const QDir stationDirectory(QFileInfo(options.stationFilePath).absoluteDir());
    const auto registryPath = absolutePath(registryValue, stationDirectory);
    QFile file(registryPath);
    if (!file.open(QIODevice::ReadOnly)) {
        addError(result,
                 QStringLiteral("pluginRegistry"),
                 QStringLiteral("Plugin registry cannot be opened: %1").arg(registryPath),
                 QStringLiteral("Run Scan Plugins or correct Station pluginRegistry"));
        return {};
    }

    QJsonParseError parseError;
    const auto document = QJsonDocument::fromJson(file.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        addError(result,
                 QStringLiteral("pluginRegistry"),
                 QStringLiteral("Plugin registry JSON is invalid: %1")
                     .arg(parseError.errorString()),
                 QStringLiteral("Run Scan Plugins again"));
        return {};
    }
    const auto entries = document.object().value(QStringLiteral("plugins"));
    if (!entries.isArray()) {
        addError(result,
                 QStringLiteral("pluginRegistry"),
                 QStringLiteral("Plugin registry must contain a plugins array"));
        return {};
    }

    QHash<QString, RegistryPlugin> plugins;
    const QDir registryDirectory(QFileInfo(registryPath).absoluteDir());
    for (int index = 0; index < entries.toArray().size(); ++index) {
        const auto entry = entries.toArray()[index].toObject();
        resolutionErrors.clear();
        auto dllValue = resolver.resolveString(
            entry.value(QStringLiteral("dll")).toString(),
            resolutionErrors,
            QStringLiteral("pluginRegistry.plugins[%1].dll").arg(index));
        for (const auto& error : resolutionErrors) {
            addError(result, error.path, error.message, error.suggestion);
        }
        if (!resolutionErrors.isEmpty()) continue;

        const auto dllPath = absolutePath(dllValue, registryDirectory);
        auto moduleId = entry.value(QStringLiteral("moduleId")).toString().trimmed();
        if (moduleId.isEmpty()) moduleId = moduleIdFromDllPath(dllPath);
        if (moduleId.isEmpty() || dllValue.trimmed().isEmpty()) continue;
        if (plugins.contains(moduleId) && plugins.value(moduleId).dllPath != dllPath) {
            addError(result,
                     QStringLiteral("pluginRegistry.plugins[%1]").arg(index),
                     QStringLiteral("Duplicate plugin moduleId: %1").arg(moduleId));
            continue;
        }
        QStringList connectionKinds;
        const auto description = entry.value(QStringLiteral("description")).toObject();
        const auto kinds = description.value(QStringLiteral("connectionKinds"));
        if (kinds.isArray()) {
            for (const auto& value : kinds.toArray()) {
                const auto parsed = deviceConnectionKindFromString(value.toString());
                if (!parsed) {
                    addError(result,
                             QStringLiteral("pluginRegistry.plugins[%1].description.connectionKinds")
                                 .arg(index),
                             QStringLiteral("Unsupported plugin connection kind: %1")
                                 .arg(value.toString()));
                    continue;
                }
                const auto name = deviceConnectionKindName(*parsed);
                if (!connectionKinds.contains(name)) connectionKinds.push_back(name);
            }
        }
        if (connectionKinds.isEmpty()) {
            connectionKinds = fallbackConnectionKinds(description, moduleId);
        }
        plugins.insert(moduleId, {dllPath, connectionKinds});
    }
    return plugins;
}

struct CanBinding {
    QString logicalId;
    QString driverId;
    QString connectionKind;
    QString serialNumber;
};

QVector<CanBinding> canBindings(const StationConfig& station,
                                StationRunPreparationResult& result)
{
    QVector<CanBinding> bindings;
    QHash<QString, int> positions;
    for (const auto& device : station.devices) {
        if (device.deviceType.compare(QStringLiteral("CAN"), Qt::CaseInsensitive) != 0) {
            continue;
        }
        if (!device.driverId.startsWith(QStringLiteral("plugin."),
                                        Qt::CaseInsensitive)) {
            continue;
        }
        const auto logicalId = baseDeviceId(device.deviceId);
        const auto serial = device.resource.trimmed();
        auto position = positions.value(logicalId, -1);
        if (position < 0) {
            positions.insert(logicalId, bindings.size());
            bindings.push_back({logicalId, device.driverId,
                                device.connectionKind, serial});
            position = bindings.size() - 1;
        } else if (bindings[position].driverId != device.driverId ||
                   bindings[position].connectionKind != device.connectionKind ||
                   bindings[position].serialNumber != serial) {
            addError(result,
                     QStringLiteral("devices.%1").arg(logicalId),
                     QStringLiteral("CAN channels in %1 do not share the same driver, connection kind, and resource")
                         .arg(logicalId),
                     QStringLiteral("Configure one stable device identity for all channels"));
        }
    }

    QSet<QString> identities;
    for (const auto& binding : bindings) {
        if (binding.connectionKind != QStringLiteral("canSerial")) continue;
        if (binding.serialNumber.isEmpty()) {
            continue;
        }
        const auto identity = binding.driverId + QLatin1Char('\n') +
                              binding.serialNumber.toCaseFolded();
        if (identities.contains(identity)) {
            addError(result,
                     QStringLiteral("devices.%1.resource").arg(binding.logicalId),
                     QStringLiteral("CAN serial number %1 is bound to more than one logical device")
                         .arg(binding.serialNumber),
                     QStringLiteral("Bind each physical CAN device only once"));
        }
        identities.insert(identity);
    }
    bindings.erase(std::remove_if(
        bindings.begin(), bindings.end(), [](const CanBinding& binding) {
            return binding.connectionKind != QStringLiteral("canSerial");
        }), bindings.end());
    return bindings;
}

} // namespace

StationRunPreparationService::StationRunPreparationService(
    IDeviceDiscoveryService* discoveryService)
    : m_discoveryService(discoveryService)
{
}

StationRunPreparationResult StationRunPreparationService::prepare(
    const QJsonObject& persistedStation,
    const StationRunPreparationOptions& options) const
{
    StationRunPreparationResult result;
    QHash<QString, QString> unavailableReasons;
    VariableResolverOptions resolverOptions;
    resolverOptions.sequenceFilePath = QFileInfo(options.stationFilePath).absoluteFilePath();
    resolverOptions.projectDir = options.projectDir;
    resolverOptions.variables = options.variables;

    const auto persistedConfig = parseStationConfigJson(persistedStation, resolverOptions);
    appendErrors(result, persistedConfig.errors);
    if (!result.ok()) return result;

    for (const auto& device : persistedConfig.config.devices) {
        if (device.resource.trimmed().isEmpty() &&
            device.connectionKind != QStringLiteral("manual") &&
            device.connectionKind != QStringLiteral("canSerial")) {
            const auto message = QStringLiteral(
                "Device %1 is unavailable because its %2 resource is not configured")
                                     .arg(device.deviceId, device.connectionKind);
            addUnavailableDevice(
                result, unavailableReasons, device.deviceId,
                QStringLiteral("devices.%1.resource").arg(device.deviceId),
                message,
                QStringLiteral("Configure the resource before using this device; only related steps will fail"));
        }
    }
    if (!result.ok()) return result;

    const VariableResolver stationResolver(resolverOptions);
    QVector<VariableResolutionError> stationResolutionErrors;
    const auto resolvedStationMap = stationResolver.resolveMap(
        persistedStation.toVariantMap(),
        stationResolutionErrors,
        QStringLiteral("station"));
    for (const auto& error : stationResolutionErrors) {
        addError(result, error.path, error.message, error.suggestion);
    }
    if (!result.ok()) return result;
    const auto resolvedStation = QJsonObject::fromVariantMap(resolvedStationMap);

    const auto bindings = canBindings(persistedConfig.config, result);
    if (!result.ok()) return result;
    for (const auto& binding : bindings) {
        if (!binding.serialNumber.isEmpty()) continue;
        addUnavailableDevice(
            result, unavailableReasons, binding.logicalId,
            QStringLiteral("devices.%1.resource").arg(binding.logicalId),
            QStringLiteral("CAN device %1 is unavailable because no stable serial number is configured")
                .arg(binding.logicalId),
            QStringLiteral("Select a CAN serial number before using this device; only related steps will fail"));
    }
    const bool hasPluginDevices = std::any_of(
        persistedConfig.config.devices.cbegin(),
        persistedConfig.config.devices.cend(),
        [](const auto& device) {
            return device.driverId.startsWith(
                QStringLiteral("plugin."), Qt::CaseInsensitive);
        });
    if (!hasPluginDevices) {
        setEffectiveResult(result, resolvedStation, {}, unavailableReasons,
                           resolverOptions);
        return result;
    }

    const auto pluginDlls = loadPluginDlls(persistedConfig.config, options, result);
    if (!result.ok()) return result;

    for (const auto& device : persistedConfig.config.devices) {
        if (!device.driverId.startsWith(QStringLiteral("plugin."), Qt::CaseInsensitive)) {
            continue;
        }
        const auto plugin = pluginDlls.constFind(device.driverId);
        if (plugin == pluginDlls.constEnd()) {
            addError(result,
                     QStringLiteral("devices.%1.driverId").arg(device.deviceId),
                     QStringLiteral("Driver %1 is not present in pluginRegistry")
                         .arg(device.driverId),
                     QStringLiteral("Scan plugins or correct the Station driver"));
            continue;
        }
        if (!plugin->connectionKinds.contains(device.connectionKind)) {
            addError(result,
                     QStringLiteral("devices.%1.connectionKind").arg(device.deviceId),
                     QStringLiteral("Driver %1 does not support connection kind %2")
                         .arg(device.driverId, device.connectionKind),
                     QStringLiteral("Select a connection kind declared by the plugin"));
        }
    }
    if (!result.ok()) return result;

    if (bindings.isEmpty()) {
        setEffectiveResult(result, resolvedStation, {}, unavailableReasons,
                           resolverOptions);
        return result;
    }

    SystemDeviceDiscoveryService systemDiscovery;
    auto* discovery = m_discoveryService ? m_discoveryService : &systemDiscovery;
    QHash<QString, QVector<DiscoveredDeviceResource>> resourcesByDriver;
    QSet<QString> scannedDrivers;
    for (const auto& binding : bindings) {
        if (binding.serialNumber.isEmpty() || scannedDrivers.contains(binding.driverId)) continue;
        scannedDrivers.insert(binding.driverId);
        const auto dllPath = pluginDlls.value(binding.driverId).dllPath;
        if (dllPath.isEmpty() || !QFileInfo::exists(dllPath)) {
            addError(result,
                     QStringLiteral("devices.%1.driverId").arg(binding.logicalId),
                     QStringLiteral("Plugin DLL for CAN driver %1 was not found")
                         .arg(binding.driverId),
                     QStringLiteral("Scan plugins and verify pluginRegistry"));
            continue;
        }
        DeviceDiscoveryRequest request;
        request.kind = DeviceDiscoveryKind::CanDevice;
        request.driverId = binding.driverId;
        request.pluginDllPath = dllPath;
        request.nativeHostProgram = options.nativeHostProgram;
        request.timeoutMs = options.discoveryTimeoutMs;
        const auto discovered = discovery->discover(request);
        if (!discovered.ok()) {
            const auto message = QStringLiteral("CAN discovery failed for %1: %2")
                                     .arg(binding.driverId, discovered.errorMessage);
            for (const auto& affected : bindings) {
                if (affected.driverId == binding.driverId) {
                    addUnavailableDevice(
                        result, unavailableReasons, affected.logicalId,
                        QStringLiteral("devices.%1").arg(affected.logicalId),
                        message,
                        QStringLiteral("Check the CAN connection; only related steps will fail"));
                }
            }
            continue;
        }
        resourcesByDriver.insert(binding.driverId, discovered.resources);
    }
    if (!result.ok()) return result;

    QHash<QString, int> runtimeIndices;
    for (const auto& binding : bindings) {
        if (binding.serialNumber.isEmpty()) continue;
        const auto resources = resourcesByDriver.value(binding.driverId);
        const auto found = std::find_if(
            resources.cbegin(), resources.cend(), [&binding](const auto& resource) {
                return resource.serialNumber.compare(
                           binding.serialNumber, Qt::CaseInsensitive) == 0;
            });
        if (found == resources.cend()) {
            const auto message = QStringLiteral(
                "CAN device %1 is unavailable because serial number %2 was not found")
                                     .arg(binding.logicalId, binding.serialNumber);
            addUnavailableDevice(
                result, unavailableReasons, binding.logicalId,
                QStringLiteral("devices.%1.resource").arg(binding.logicalId),
                message,
                QStringLiteral("Reconnect the device or update the binding; only related steps will fail"));
            continue;
        }
        bool indexOk = false;
        const auto deviceIndex = found->runtimeLocator.value(QStringLiteral("deviceIndex"))
                                     .toInt(&indexOk);
        if (!indexOk || deviceIndex < 0) {
            const auto message = QStringLiteral(
                "CAN device %1 is unavailable because discovery returned no valid runtime index")
                                     .arg(binding.logicalId);
            addUnavailableDevice(
                result, unavailableReasons, binding.logicalId,
                QStringLiteral("devices.%1").arg(binding.logicalId),
                message,
                QStringLiteral("Update the CAN plugin findDevices implementation; only related steps will fail"));
            continue;
        }
        runtimeIndices.insert(binding.logicalId, deviceIndex);
    }
    if (!result.ok()) return result;

    setEffectiveResult(result, resolvedStation, runtimeIndices,
                       unavailableReasons, resolverOptions);
    return result;
}

} // namespace PicoATE::Core
