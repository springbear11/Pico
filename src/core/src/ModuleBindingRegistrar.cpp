#include "PicoATE/Core/ModuleBindingRegistrar.h"

#include "PicoATE/Core/PersistentQProcessTransport.h"
#include "PicoATE/Core/QProcessTransport.h"
#include "PicoATE/Core/DeviceTransportSession.h"
#include "PicoATE/Core/VariableResolver.h"

#include <QDir>
#include <QFileInfo>
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

    VariableResolverOptions resolverOptions;
    resolverOptions.sequenceFilePath = options.stationFilePath;
    resolverOptions.projectDir = options.projectDir;
    resolverOptions.variables = options.variables;
    const VariableResolver resolver(resolverOptions);

    QHash<DeviceDriverId, QString> registeredPluginPaths;
    for (int index = 0; index < station.devices.size(); ++index) {
        const auto& device = station.devices[index];
        if (device.pluginPath.trimmed().isEmpty()) {
            continue;
        }

        QVector<VariableResolutionError> resolutionErrors;
        const auto path = QStringLiteral("devices[%1].pluginPath").arg(index);
        const auto pluginPath = resolveProgramPath(
            resolver.resolveString(device.pluginPath, resolutionErrors, path),
            resolver);
        if (!resolutionErrors.isEmpty()) {
            addResolutionErrors(result, device.driverId, resolutionErrors);
            continue;
        }
        if (!QFileInfo::exists(pluginPath)) {
            addError(result,
                     device.driverId,
                     QStringLiteral("Plugin DLL does not exist: %1").arg(pluginPath),
                     QStringLiteral("Rescan plugins or update the Station plugin binding"));
            continue;
        }
        if (options.nativeHostProgram.trimmed().isEmpty() ||
            !QFileInfo::exists(options.nativeHostProgram)) {
            addError(result,
                     device.driverId,
                     QStringLiteral("PicoATE.NativeHost.exe was not found"),
                     QStringLiteral("Deploy NativeHost beside the application"));
            continue;
        }

        const auto existing = registeredPluginPaths.constFind(device.driverId);
        if (existing != registeredPluginPaths.constEnd()) {
            if (QFileInfo(existing.value()).absoluteFilePath() !=
                QFileInfo(pluginPath).absoluteFilePath()) {
                addError(result,
                         device.driverId,
                         QStringLiteral("Driver id is bound to multiple plugin DLLs"),
                         QStringLiteral("Use one concrete plugin per driverId"));
            }
            continue;
        }

        auto transport = std::make_shared<PersistentQProcessTransport>(
            QFileInfo(options.nativeHostProgram).absoluteFilePath(),
            QStringList{QStringLiteral("--dll"), pluginPath,
                        QStringLiteral("--vendor-stdio"), QStringLiteral("discard")});
        if (!session.devices().registerFactory(
                std::make_shared<TransportDeviceSessionFactory>(
                    device.driverId, transport, device.timeoutMs))) {
            addError(result,
                     device.driverId,
                     QStringLiteral("Failed to register Station plugin driver"));
            continue;
        }
        registeredPluginPaths.insert(device.driverId, pluginPath);
        result.registeredModuleIds.push_back(device.driverId);
    }
    return result;
}

} // namespace PicoATE::Core
