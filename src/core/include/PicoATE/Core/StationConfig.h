#pragma once

#include "PicoATE/Core/DeviceSessionManager.h"
#include "PicoATE/Core/ErrorPolicyEngine.h"
#include "PicoATE/Core/VariableResolver.h"

#include <QJsonObject>
#include <QString>
#include <QVariantMap>
#include <QVector>

namespace PicoATE::Core {

struct StationConfigDiagnostic {
    QString path;
    QString message;
    QString suggestion;
};

struct StationConfig {
    QString stationId;
    QString name;
    bool stopOnFailure = true;
    bool scanDialogEnabled = true;
    bool txtLogEnabled = false;
    bool csvReportEnabled = false;
    bool xlsxReportEnabled = false;
    bool loopTestEnabled = false;
    int loopTestCount = 1;
    QString pluginRegistryPath = QStringLiteral("plugins/PluginRegistry.json");
    QString reportOutputDirectory;
    int snLength = 0;
    QVariantMap metadata;
    QVector<DeviceSessionConfig> devices;
};

struct StationConfigResult {
    StationConfig config;
    QVector<StationConfigDiagnostic> errors;

    bool ok() const;
};

StationConfigResult parseStationConfigJson(const QJsonObject& object,
                                           const VariableResolverOptions& resolverOptions = {});

StationConfigResult loadStationConfigFile(const QString& filePath,
                                          VariableResolverOptions resolverOptions = {});

QVector<StationConfigDiagnostic> configureDeviceSessions(const StationConfig& config,
                                                         DeviceSessionManager& manager);

FailureHandlingMode failureHandlingMode(const StationConfig& config);

DeviceSessionLifetime deviceSessionLifetimeFromString(const QString& value,
                                                      bool* ok = nullptr);

} // namespace PicoATE::Core
