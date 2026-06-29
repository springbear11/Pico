#include "PicoATE/Core/ModuleBindingRegistrar.h"

#include "PicoATE/Core/PersistentQProcessTransport.h"
#include "PicoATE/Core/QProcessTransport.h"
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

        result.registeredModuleIds.push_back(binding.moduleId);
    }

    return result;
}

} // namespace PicoATE::Core
