#include "PicoATE/Core/ModuleBindingRegistrar.h"

#include "PicoATE/Core/PersistentQProcessTransport.h"
#include "PicoATE/Core/QProcessTransport.h"
#include "PicoATE/Core/DeviceTransportSession.h"
#include "PicoATE/Core/VariableResolver.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QRegularExpression>
#include <QSet>
#include <utility>

namespace PicoATE::Core {

namespace {

VariableResolverOptions variableOptions(const ModuleBindingRegistrationOptions& options)
{
    VariableResolverOptions resolverOptions;
    resolverOptions.sequenceFilePath = options.sequenceFilePath;
    resolverOptions.projectDir = options.projectDir;
    resolverOptions.variables = options.variables;
    return resolverOptions;
}

QString normalized(QString value)
{
    value = value.trimmed().toLower();
    value.remove('-');
    value.remove('_');
    value.remove(' ');
    return value;
}

QString resolveProgramPath(QString program,
                           const VariableResolver& resolver)
{
    const QFileInfo info(program);
    if (info.isAbsolute() || program.isEmpty()) {
        return program;
    }
    if (!program.contains('/') && !program.contains('\\')) {
        return program;
    }

    const QDir base(resolver.sequenceDir());
    return QFileInfo(base.filePath(program)).absoluteFilePath();
}

void addError(ModuleBindingRegistrationResult& result,
              const ModuleId& moduleId,
              QString message,
              QString suggestion = {})
{
    result.errors.push_back({moduleId, std::move(message), std::move(suggestion)});
}

void addResolutionErrors(ModuleBindingRegistrationResult& result,
                         const ModuleId& moduleId,
                         const QVector<VariableResolutionError>& errors)
{
    for (const auto& error : errors) {
        auto message = error.message;
        if (!error.path.isEmpty()) {
            message = QString("%1 at %2").arg(message, error.path);
        }
        addError(result, moduleId, message, error.suggestion);
    }
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
    QString moduleId;
    QString dllPath;
};

QVector<RegistryPlugin> loadPluginRegistry(
    const StationConfig& station,
    const StationPluginRegistrationOptions& options,
    ModuleBindingRegistrationResult& result)
{
    VariableResolverOptions resolverOptions;
    resolverOptions.sequenceFilePath = options.stationFilePath;
    resolverOptions.projectDir = options.projectDir;
    resolverOptions.variables = options.variables;
    const VariableResolver resolver(resolverOptions);

    QVector<VariableResolutionError> resolutionErrors;
    auto registryPath = resolver.resolveString(
        station.pluginRegistryPath,
        resolutionErrors,
        QStringLiteral("pluginRegistry"));
    if (!resolutionErrors.isEmpty()) {
        addResolutionErrors(result, QStringLiteral("plugins"), resolutionErrors);
        return {};
    }
    registryPath = resolveProgramPath(registryPath, resolver);

    QFile file(registryPath);
    if (!QFileInfo::exists(registryPath)) {
        return {};
    }
    if (!file.open(QIODevice::ReadOnly)) {
        addError(result,
                 QStringLiteral("plugins"),
                 QStringLiteral("Plugin registry cannot be opened: %1")
                     .arg(registryPath),
                 QStringLiteral("Run Scan Plugins or correct Station pluginRegistry"));
        return {};
    }
    QJsonParseError parseError;
    const auto document = QJsonDocument::fromJson(file.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        addError(result,
                 QStringLiteral("plugins"),
                 QStringLiteral("Plugin registry JSON is invalid: %1")
                     .arg(parseError.errorString()),
                 QStringLiteral("Run Scan Plugins again"));
        return {};
    }
    const auto entries = document.object().value(QStringLiteral("plugins"));
    if (!entries.isArray()) {
        addError(result,
                 QStringLiteral("plugins"),
                 QStringLiteral("Plugin registry must contain a plugins array"));
        return {};
    }

    QVector<RegistryPlugin> plugins;
    QSet<QString> moduleIds;
    const auto registryDirectory = QFileInfo(registryPath).absoluteDir();
    for (int index = 0; index < entries.toArray().size(); ++index) {
        const auto entry = entries.toArray()[index].toObject();
        const auto dllValue = entry.value(QStringLiteral("dll")).toString().trimmed();
        auto moduleId = entry.value(QStringLiteral("moduleId")).toString().trimmed();
        if (dllValue.isEmpty()) {
            addError(result,
                     QStringLiteral("plugins[%1]").arg(index),
                     QStringLiteral("Plugin registry entry has no DLL path"));
            continue;
        }
        const auto dllPath = QFileInfo(
            registryDirectory.absoluteFilePath(dllValue)).absoluteFilePath();
        if (moduleId.isEmpty()) {
            moduleId = moduleIdFromDllPath(dllPath);
        }
        if (moduleId.isEmpty() || moduleIds.contains(moduleId)) {
            addError(result,
                     QStringLiteral("plugins[%1]").arg(index),
                     moduleId.isEmpty()
                         ? QStringLiteral("Plugin moduleId is empty")
                         : QStringLiteral("Duplicate plugin moduleId: %1").arg(moduleId));
            continue;
        }
        if (!QFileInfo::exists(dllPath)) {
            addError(result,
                     moduleId,
                     QStringLiteral("Plugin DLL does not exist: %1").arg(dllPath),
                     QStringLiteral("Copy the DLL into plugins and run Scan Plugins"));
            continue;
        }
        moduleIds.insert(moduleId);
        plugins.push_back({moduleId, dllPath});
    }
    return plugins;
}

} // namespace

ModuleBindingRegistrationResult registerConfiguredModules(
    ExecutionSession& session,
    const SequenceDef& sequence,
    const ModuleBindingRegistrationOptions& options)
{
    ModuleBindingRegistrationResult result;
    const VariableResolver resolver(variableOptions(options));

    for (int bindingIndex = 0; bindingIndex < sequence.moduleBindings.size(); ++bindingIndex) {
        const auto& binding = sequence.moduleBindings[bindingIndex];
        if (!binding.enabled) {
            continue;
        }

        QVector<VariableResolutionError> resolutionErrors;
        const auto bindingPath = QString("moduleBindings[%1]").arg(bindingIndex);
        const auto resolvedProgram = resolveProgramPath(
            resolver.resolveString(binding.program, resolutionErrors, bindingPath + ".program"),
            resolver);

        QStringList resolvedArguments;
        for (int argumentIndex = 0; argumentIndex < binding.arguments.size(); ++argumentIndex) {
            resolvedArguments.push_back(
                resolver.resolveString(binding.arguments[argumentIndex],
                                       resolutionErrors,
                                       QString("%1.arguments[%2]").arg(bindingPath).arg(argumentIndex)));
        }

        if (!resolutionErrors.isEmpty()) {
            addResolutionErrors(result, binding.moduleId, resolutionErrors);
            continue;
        }

        const auto transportKind = normalized(binding.transport);
        if (transportKind != "qprocess" && transportKind != "persistentqprocess") {
            addError(result,
                     binding.moduleId,
                     QString("Unsupported module transport: %1").arg(binding.transport),
                     "Use qprocess or persistent-qprocess");
            continue;
        }

        std::shared_ptr<IModuleTransport> transport;
        if (transportKind == "persistentqprocess") {
            transport = std::make_shared<PersistentQProcessTransport>(resolvedProgram, resolvedArguments);
        } else {
            transport = std::make_shared<QProcessTransport>(resolvedProgram, resolvedArguments);
        }
        auto adapter = std::make_shared<TransportModuleAdapter>(binding.moduleId, transport, binding.timeoutMs);
        if (!session.registerModule(adapter)) {
            addError(result,
                     binding.moduleId,
                     "Failed to register module binding",
                     "Check for duplicate moduleId values");
            continue;
        }

        session.devices().registerFactory(
            std::make_shared<TransportDeviceSessionFactory>(
                binding.moduleId, transport, binding.timeoutMs));

        result.registeredModuleIds.push_back(binding.moduleId);
    }

    return result;
}

ModuleBindingRegistrationResult registerStationPluginModules(
    ExecutionSession& session,
    const StationConfig& station,
    const StationPluginRegistrationOptions& options)
{
    ModuleBindingRegistrationResult result;
    if (!session.registerModule(std::make_shared<LogicalDeviceModule>())) {
        addError(result,
                 QStringLiteral("device"),
                 QStringLiteral("Failed to register logical device module"),
                 QStringLiteral("Do not reuse moduleId 'device' for another module"));
        return result;
    }
    result.registeredModuleIds.push_back(QStringLiteral("device"));

    const auto plugins = loadPluginRegistry(station, options, result);
    if (!plugins.isEmpty() &&
        (options.nativeHostProgram.trimmed().isEmpty() ||
         !QFileInfo::exists(options.nativeHostProgram))) {
        addError(result,
                 QStringLiteral("plugins"),
                 QStringLiteral("PicoATE.NativeHost.exe was not found"),
                 QStringLiteral("Deploy NativeHost beside the application"));
        return result;
    }

    QHash<DeviceDriverId, std::shared_ptr<IModuleTransport>> transports;
    for (const auto& plugin : plugins) {
        auto transport = std::make_shared<PersistentQProcessTransport>(
            QFileInfo(options.nativeHostProgram).absoluteFilePath(),
            QStringList{QStringLiteral("--dll"), plugin.dllPath,
                        QStringLiteral("--vendor-stdio"), QStringLiteral("discard")});
        transports.insert(plugin.moduleId, transport);
        if (session.hasModule(plugin.moduleId)) {
            continue;
        }
        if (!session.registerModule(std::make_shared<TransportModuleAdapter>(
                plugin.moduleId, transport, 30000))) {
            addError(result,
                     plugin.moduleId,
                     QStringLiteral("Failed to register plugin module"),
                     QStringLiteral("Remove a duplicate module binding"));
            continue;
        }
        result.registeredModuleIds.push_back(plugin.moduleId);
    }

    QSet<DeviceDriverId> registeredDrivers;
    for (const auto& device : station.devices) {
        if (registeredDrivers.contains(device.driverId)) {
            continue;
        }
        const auto transport = transports.value(device.driverId);
        if (!transport) {
            continue;
        }
        if (!session.devices().registerFactory(
                std::make_shared<TransportDeviceSessionFactory>(
                    device.driverId, transport, device.timeoutMs))) {
            addError(result,
                     device.driverId,
                     QStringLiteral("Failed to register Station plugin driver"));
            continue;
        }
        registeredDrivers.insert(device.driverId);
    }
    return result;
}

} // namespace PicoATE::Core
