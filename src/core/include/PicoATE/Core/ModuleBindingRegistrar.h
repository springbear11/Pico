#pragma once

#include "PicoATE/Core/ExecutionSession.h"
#include "PicoATE/Core/SequenceDef.h"
#include "PicoATE/Core/StationConfig.h"

#include <QHash>

namespace PicoATE::Core {

struct ModuleBindingRegistrationError {
    QString moduleId;
    QString message;
    QString suggestion;
};

struct ModuleBindingRegistrationOptions {
    QString sequenceFilePath;
    QString projectDir;
    QHash<QString, QString> variables;
};

struct ModuleBindingRegistrationResult {
    QVector<ModuleBindingRegistrationError> errors;
    QVector<ModuleId> registeredModuleIds;

    bool ok() const { return errors.isEmpty(); }
};

ModuleBindingRegistrationResult registerConfiguredModules(
    ExecutionSession& session,
    const SequenceDef& sequence,
    const ModuleBindingRegistrationOptions& options = {});

struct StationPluginRegistrationOptions {
    QString stationFilePath;
    QString projectDir;
    QString nativeHostProgram;
    QHash<QString, QString> variables;
};

ModuleBindingRegistrationResult registerStationPluginModules(
    ExecutionSession& session,
    const StationConfig& station,
    const StationPluginRegistrationOptions& options = {});

} // namespace PicoATE::Core
