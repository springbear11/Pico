#pragma once

#include "PicoATE/Core/CoreExport.h"

#include <QJsonObject>
#include <QString>
#include <QVector>

namespace PicoATE::Core {

struct ProductRoute {
    QString name;
    QString pattern;
    QString projectPath;
    QString sequencePath;
    bool enabled = true;
};

struct ProductRoutingConfig {
    bool allowManualInTest = false;
    QString projectRootPath;
    QVector<ProductRoute> routes;
    QString sourcePath;
};

struct ProductRoutingDiagnostic {
    QString path;
    QString message;
    QString suggestion;
};

struct ProductRoutingResult {
    ProductRoutingConfig config;
    QVector<ProductRoutingDiagnostic> errors;

    bool ok() const { return errors.isEmpty(); }
};

struct ProductRouteResolution {
    QString routeName;
    QString pattern;
    QString projectName;
    QString projectPath;
    QString sequencePath;
    QString stationPath;
    QVector<ProductRoutingDiagnostic> errors;

    bool ok() const
    {
        return errors.isEmpty() && !sequencePath.isEmpty() &&
               !stationPath.isEmpty();
    }
};

struct ProductProject {
    QString name;
    QString directoryPath;
    QString sequencePath;
    QString stationPath;
    QVector<ProductRoutingDiagnostic> errors;

    bool ok() const
    {
        return errors.isEmpty() && !sequencePath.isEmpty() &&
               !stationPath.isEmpty();
    }
};

PICOATE_CORE_EXPORT ProductRoutingResult parseProductRoutingJson(
    const QJsonObject& object,
    const QString& sourcePath = {});

PICOATE_CORE_EXPORT ProductRoutingResult loadProductRoutingFile(
    const QString& filePath);

PICOATE_CORE_EXPORT QJsonObject productRoutingToJson(
    const ProductRoutingConfig& config,
    const QString& targetFilePath = {});

PICOATE_CORE_EXPORT ProductProject inspectProductProject(
    const QString& directoryPath);

PICOATE_CORE_EXPORT QVector<ProductProject> discoverProductProjects(
    const QString& projectRootPath);

PICOATE_CORE_EXPORT ProductRouteResolution resolveProductRoute(
    const ProductRoutingConfig& config,
    const QString& serialNumber);

} // namespace PicoATE::Core
