#pragma once

#include "PicoATE/Core/DeviceDiscovery.h"
#include "PicoATE/Core/StationConfig.h"

#include <QHash>
#include <QJsonObject>
#include <QString>
#include <QVector>

namespace PicoATE::Core {

struct StationRunPreparationOptions {
    QString stationFilePath;
    QString projectDir;
    QString nativeHostProgram;
    QHash<QString, QString> variables;
    int discoveryTimeoutMs = 12000;
};

struct StationRunPreparationResult {
    QJsonObject effectiveStation;
    StationConfig stationConfig;
    QVector<StationConfigDiagnostic> errors;
    QVector<StationConfigDiagnostic> warnings;

    bool ok() const { return errors.isEmpty(); }
};

class StationRunPreparationService {
public:
    explicit StationRunPreparationService(
        IDeviceDiscoveryService* discoveryService = nullptr);

    StationRunPreparationResult prepare(
        const QJsonObject& persistedStation,
        const StationRunPreparationOptions& options = {}) const;

private:
    IDeviceDiscoveryService* m_discoveryService = nullptr;
};

} // namespace PicoATE::Core
