#pragma once

#include "PicoATE/Core/DeviceSessionManager.h"
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

DeviceSessionLifetime deviceSessionLifetimeFromString(const QString& value,
                                                      bool* ok = nullptr);

} // namespace PicoATE::Core
