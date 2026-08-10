#include "PicoATE/Core/ProductRouting.h"
#include "PicoATE/Core/StationConfig.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QQueue>
#include <QRegularExpression>
#include <QSet>
#include <QStringList>

#include <algorithm>
#include <cmath>
#include <iterator>
#include <utility>

namespace PicoATE::Core {

namespace {

void addError(QVector<ProductRoutingDiagnostic>& errors,
              QString path,
              QString message,
              QString suggestion = {})
{
    errors.push_back({std::move(path), std::move(message), std::move(suggestion)});
}

QString routingBasePath(const QString& sourcePath)
{
    const QFileInfo source(sourcePath);
    return sourcePath.isEmpty() ? QDir::currentPath()
                                : source.absolutePath();
}

QString resolvedPath(const QString& value, const QString& basePath)
{
    const QFileInfo candidate(value);
    return candidate.isAbsolute()
        ? candidate.absoluteFilePath()
        : QFileInfo(QDir(basePath).absoluteFilePath(value)).absoluteFilePath();
}

bool isSequenceCandidate(const QFileInfo& fileInfo)
{
    return fileInfo.isFile() &&
           fileInfo.completeBaseName().contains(
               QStringLiteral("seq"), Qt::CaseInsensitive);
}

QString routeDisplayName(const ProductRoute& route)
{
    return route.name.isEmpty() ? route.pattern : route.name;
}

bool validateStationSnCharacters(ProductRouteResolution& result,
                                 const QString& serialNumber)
{
    const auto station = loadStationConfigFile(result.stationPath);
    const auto allowedRegex = station.config.snAllowedRegex.trimmed();
    if (allowedRegex.isEmpty()) {
        return true;
    }

    const QRegularExpression expression(allowedRegex);
    if (!expression.isValid()) {
        addError(result.errors,
                 QStringLiteral("routes.station.snAllowedRegex"),
                 QStringLiteral("Target Station has an invalid SN character rule"),
                 expression.errorString());
        return false;
    }
    const auto match = expression.match(serialNumber);
    if (match.hasMatch() && match.capturedStart() == 0 &&
        match.capturedLength() == serialNumber.size()) {
        return true;
    }

    addError(result.errors,
             QStringLiteral("serialNumber.characters"),
             QStringLiteral("SN contains characters not allowed by Station for route '%1'")
                 .arg(result.routeName),
             QStringLiteral("Update Allowed Characters in the target Station configuration"));
    return false;
}

bool wildcardTokensOverlap(QChar left, QChar right)
{
    return left == QLatin1Char('*') || left == QLatin1Char('?') ||
           right == QLatin1Char('*') || right == QLatin1Char('?') ||
           left == right;
}

bool wildcardPatternsOverlap(const QString& left, const QString& right)
{
    QQueue<QPair<int, int>> pending;
    QSet<quint64> visited;
    const auto enqueue = [&pending, &visited](int leftIndex, int rightIndex) {
        const auto key = (quint64(quint32(leftIndex)) << 32) |
                         quint32(rightIndex);
        if (!visited.contains(key)) {
            visited.insert(key);
            pending.enqueue({leftIndex, rightIndex});
        }
    };
    enqueue(0, 0);

    while (!pending.isEmpty()) {
        const auto [leftIndex, rightIndex] = pending.dequeue();
        if (leftIndex == left.size() && rightIndex == right.size()) {
            return true;
        }

        const bool leftStar = leftIndex < left.size() &&
                              left.at(leftIndex) == QLatin1Char('*');
        const bool rightStar = rightIndex < right.size() &&
                               right.at(rightIndex) == QLatin1Char('*');
        if (leftStar) {
            enqueue(leftIndex + 1, rightIndex);
        }
        if (rightStar) {
            enqueue(leftIndex, rightIndex + 1);
        }

        if (leftIndex >= left.size() || rightIndex >= right.size() ||
            !wildcardTokensOverlap(left.at(leftIndex), right.at(rightIndex))) {
            continue;
        }
        enqueue(leftStar ? leftIndex : leftIndex + 1,
                rightStar ? rightIndex : rightIndex + 1);
    }
    return false;
}

} // namespace

ProductRoutingResult parseProductRoutingJson(const QJsonObject& object,
                                             const QString& sourcePath)
{
    ProductRoutingResult result;
    result.config.sourcePath = sourcePath.isEmpty()
        ? QString{}
        : QFileInfo(sourcePath).absoluteFilePath();
    const auto basePath = routingBasePath(result.config.sourcePath);

    const auto projectRootValue = object.value(QStringLiteral("projectRoot"));
    if (!projectRootValue.isUndefined() && !projectRootValue.isString()) {
        addError(result.errors,
                 QStringLiteral("projectRoot"),
                 QStringLiteral("Expected string"),
                 QStringLiteral("Use a directory such as projects"));
    }
    const auto configuredProjectRoot = projectRootValue.isString()
        ? projectRootValue.toString().trimmed()
        : QStringLiteral("projects");
    result.config.projectRootPath = resolvedPath(
        configuredProjectRoot.isEmpty() ? QStringLiteral("projects")
                                        : configuredProjectRoot,
        basePath);

    const auto manualValue = object.value(QStringLiteral("allowManualInTest"));
    if (!manualValue.isUndefined() && !manualValue.isBool()) {
        addError(result.errors,
                 QStringLiteral("allowManualInTest"),
                 QStringLiteral("Expected bool"),
                 QStringLiteral("Use true or false"));
    } else if (manualValue.isBool()) {
        result.config.allowManualInTest = manualValue.toBool();
    }

    const auto routesValue = object.value(QStringLiteral("routes"));
    if (!routesValue.isArray()) {
        addError(result.errors,
                 QStringLiteral("routes"),
                 QStringLiteral("Expected array"),
                 QStringLiteral("Use an array of SN route objects"));
        return result;
    }

    QSet<QString> exactPatterns;
    const auto routes = routesValue.toArray();
    result.config.routes.reserve(routes.size());
    for (int index = 0; index < routes.size(); ++index) {
        const auto path = QStringLiteral("routes[%1]").arg(index);
        if (!routes.at(index).isObject()) {
            addError(result.errors, path,
                     QStringLiteral("Expected object"));
            continue;
        }

        const auto routeObject = routes.at(index).toObject();
        ProductRoute route;
        const auto enabledValue = routeObject.value(QStringLiteral("enabled"));
        if (!enabledValue.isUndefined() && !enabledValue.isBool()) {
            addError(result.errors,
                     path + QStringLiteral(".enabled"),
                     QStringLiteral("Expected bool"));
        } else if (enabledValue.isBool()) {
            route.enabled = enabledValue.toBool();
        }

        const auto nameValue = routeObject.value(QStringLiteral("name"));
        if (!nameValue.isUndefined() && !nameValue.isString()) {
            addError(result.errors,
                     path + QStringLiteral(".name"),
                     QStringLiteral("Expected string"));
        } else {
            route.name = nameValue.toString().trimmed();
        }

        auto patternValue = routeObject.value(QStringLiteral("pattern"));
        if (patternValue.isUndefined()) {
            patternValue = routeObject.value(QStringLiteral("snPattern"));
        }
        if (!patternValue.isString() || patternValue.toString().trimmed().isEmpty()) {
            addError(result.errors,
                     path + QStringLiteral(".pattern"),
                     QStringLiteral("A non-empty SN wildcard pattern is required"),
                     QStringLiteral("Examples: BTSN* or *C1234567*"));
        } else {
            route.pattern = patternValue.toString().trimmed();
            if (exactPatterns.contains(route.pattern)) {
                addError(result.errors,
                         path + QStringLiteral(".pattern"),
                         QStringLiteral("Duplicate SN route pattern: %1")
                             .arg(route.pattern),
                         QStringLiteral("Each exact pattern may appear only once"));
            }
            exactPatterns.insert(route.pattern);
        }

        const auto lengthValue = routeObject.value(QStringLiteral("snLength"));
        if (!lengthValue.isUndefined()) {
            if (!lengthValue.isDouble()) {
                addError(result.errors,
                         path + QStringLiteral(".snLength"),
                         QStringLiteral("Expected whole number"),
                         QStringLiteral("Use 0 for any length or 1-256 for an exact length"));
            } else {
                const double numericLength = lengthValue.toDouble();
                if (std::floor(numericLength) != numericLength ||
                    numericLength < 0.0 || numericLength > 256.0) {
                    addError(result.errors,
                             path + QStringLiteral(".snLength"),
                             QStringLiteral("SN length must be a whole number from 0 to 256"),
                             QStringLiteral("Use 0 for any length"));
                } else {
                    route.snLength = static_cast<int>(numericLength);
                }
            }
        }

        auto projectValue = routeObject.value(QStringLiteral("project"));
        if (projectValue.isUndefined()) {
            projectValue = routeObject.value(QStringLiteral("projectPath"));
        }
        auto sequenceValue = routeObject.value(QStringLiteral("sequence"));
        if (sequenceValue.isUndefined()) {
            sequenceValue = routeObject.value(QStringLiteral("sequencePath"));
        }
        const bool hasProject = projectValue.isString() &&
                                !projectValue.toString().trimmed().isEmpty();
        const bool hasSequence = sequenceValue.isString() &&
                                 !sequenceValue.toString().trimmed().isEmpty();
        if (!projectValue.isUndefined() && !projectValue.isString()) {
            addError(result.errors,
                     path + QStringLiteral(".project"),
                     QStringLiteral("Expected string"));
        }
        if (!sequenceValue.isUndefined() && !sequenceValue.isString()) {
            addError(result.errors,
                     path + QStringLiteral(".sequence"),
                     QStringLiteral("Expected string"));
        }
        if (hasProject && hasSequence) {
            addError(result.errors,
                     path + QStringLiteral(".project"),
                     QStringLiteral("Route cannot contain both project and sequence"),
                     QStringLiteral("Use project for the new package format"));
        } else if (hasProject) {
            route.projectPath = resolvedPath(
                projectValue.toString().trimmed(),
                result.config.projectRootPath);
        } else if (hasSequence) {
            route.sequencePath = resolvedPath(
                sequenceValue.toString().trimmed(), basePath);
        } else {
            addError(result.errors,
                     path + QStringLiteral(".project"),
                     QStringLiteral("A product project is required"),
                     QStringLiteral("Select a folder under projectRoot"));
        }

        result.config.routes.push_back(std::move(route));
    }

    for (int rightIndex = 0; rightIndex < result.config.routes.size(); ++rightIndex) {
        const auto& right = result.config.routes.at(rightIndex);
        if (!right.enabled || right.pattern.isEmpty()) {
            continue;
        }
        for (int leftIndex = 0; leftIndex < rightIndex; ++leftIndex) {
            const auto& left = result.config.routes.at(leftIndex);
            if (!left.enabled || left.pattern.isEmpty() ||
                !wildcardPatternsOverlap(left.pattern, right.pattern)) {
                continue;
            }
            addError(result.errors,
                     QStringLiteral("routes[%1].pattern").arg(rightIndex),
                     QStringLiteral("SN pattern overlaps route '%1': %2")
                         .arg(routeDisplayName(left), left.pattern),
                     QStringLiteral("Enabled routes must match mutually exclusive SN values"));
        }
    }
    return result;
}

ProductRoutingResult loadProductRoutingFile(const QString& filePath)
{
    ProductRoutingResult result;
    result.config.sourcePath = QFileInfo(filePath).absoluteFilePath();
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly)) {
        addError(result.errors,
                 QStringLiteral("ProductRouting.json"),
                 QStringLiteral("Cannot read ProductRouting file: %1")
                     .arg(QFileInfo(filePath).absoluteFilePath()),
                 file.errorString());
        return result;
    }

    QJsonParseError parseError;
    const auto document = QJsonDocument::fromJson(file.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        addError(result.errors,
                 QStringLiteral("ProductRouting.json"),
                 QStringLiteral("ProductRouting is not a valid JSON object"),
                 parseError.errorString());
        return result;
    }
    return parseProductRoutingJson(document.object(), filePath);
}

QJsonObject productRoutingToJson(const ProductRoutingConfig& config,
                                 const QString& targetFilePath)
{
    QJsonObject object;
    object.insert(QStringLiteral("allowManualInTest"), config.allowManualInTest);

    const QDir baseDirectory = targetFilePath.isEmpty()
        ? QDir::current()
        : QFileInfo(targetFilePath).absoluteDir();
    auto projectRootPath = config.projectRootPath.trimmed();
    if (projectRootPath.isEmpty()) {
        projectRootPath = baseDirectory.absoluteFilePath(
            QStringLiteral("projects"));
    }
    if (QFileInfo(projectRootPath).isAbsolute()) {
        projectRootPath = baseDirectory.relativeFilePath(projectRootPath);
    }
    object.insert(QStringLiteral("projectRoot"),
                  QDir::fromNativeSeparators(projectRootPath));

    const QDir projectRoot(config.projectRootPath.trimmed().isEmpty()
                               ? baseDirectory.absoluteFilePath(
                                     QStringLiteral("projects"))
                               : config.projectRootPath);
    QJsonArray routes;
    for (const auto& route : config.routes) {
        QJsonObject routeObject{
            {QStringLiteral("name"), route.name},
            {QStringLiteral("pattern"), route.pattern},
            {QStringLiteral("enabled"), route.enabled},
        };
        if (route.snLength > 0) {
            routeObject.insert(QStringLiteral("snLength"), route.snLength);
        }
        if (!route.projectPath.trimmed().isEmpty()) {
            auto projectPath = route.projectPath.trimmed();
            if (QFileInfo(projectPath).isAbsolute()) {
                projectPath = projectRoot.relativeFilePath(projectPath);
            }
            routeObject.insert(QStringLiteral("project"),
                               QDir::fromNativeSeparators(projectPath));
        } else {
            auto sequencePath = route.sequencePath.trimmed();
            if (!targetFilePath.isEmpty() && QFileInfo(sequencePath).isAbsolute()) {
                sequencePath = baseDirectory.relativeFilePath(sequencePath);
            }
            routeObject.insert(QStringLiteral("sequence"),
                               QDir::fromNativeSeparators(sequencePath));
        }
        routes.push_back(routeObject);
    }
    object.insert(QStringLiteral("routes"), routes);
    return object;
}

ProductProject inspectProductProject(const QString& directoryPath)
{
    ProductProject project;
    const QFileInfo directoryInfo(directoryPath);
    project.directoryPath = directoryInfo.absoluteFilePath();
    project.name = directoryInfo.fileName();

    QDir directory(project.directoryPath);
    if (!directory.exists()) {
        addError(project.errors,
                 QStringLiteral("project"),
                 QStringLiteral("Product project directory does not exist: %1")
                     .arg(project.directoryPath));
        return project;
    }

    const auto stationPath = directory.absoluteFilePath(
        QStringLiteral("StationSystem.json"));
    if (!QFileInfo(stationPath).isFile()) {
        addError(project.errors,
                 QStringLiteral("project.station"),
                 QStringLiteral("Product project is missing StationSystem.json"));
    } else {
        project.stationPath = QFileInfo(stationPath).absoluteFilePath();
    }

    QVector<QFileInfo> sequenceFiles;
    const auto jsonFiles = directory.entryInfoList(
        {QStringLiteral("*.json")},
        QDir::Files | QDir::Readable,
        QDir::Name | QDir::IgnoreCase);
    std::copy_if(jsonFiles.cbegin(), jsonFiles.cend(),
                 std::back_inserter(sequenceFiles), isSequenceCandidate);
    if (sequenceFiles.isEmpty()) {
        addError(project.errors,
                 QStringLiteral("project.sequence"),
                 QStringLiteral("Product project has no Sequence JSON"),
                 QStringLiteral("Add one JSON file whose name contains sequence or seq"));
    } else if (sequenceFiles.size() > 1) {
        QStringList names;
        for (const auto& file : sequenceFiles) {
            names.push_back(file.fileName());
        }
        addError(project.errors,
                 QStringLiteral("project.sequence"),
                 QStringLiteral("Product project contains multiple Sequence candidates: %1")
                     .arg(names.join(QStringLiteral(", "))),
                 QStringLiteral("Keep exactly one sequence/seq JSON in the project folder"));
    } else {
        project.sequencePath = sequenceFiles.first().absoluteFilePath();
    }
    return project;
}

QVector<ProductProject> discoverProductProjects(const QString& projectRootPath)
{
    QVector<ProductProject> projects;
    QDir root(projectRootPath);
    if (!root.exists()) {
        return projects;
    }
    const auto directories = root.entryInfoList(
        QDir::Dirs | QDir::Readable | QDir::NoDotAndDotDot,
        QDir::Name | QDir::IgnoreCase);
    projects.reserve(directories.size());
    for (const auto& directory : directories) {
        projects.push_back(inspectProductProject(directory.absoluteFilePath()));
    }
    return projects;
}

ProductRouteResolution resolveProductRoute(const ProductRoutingConfig& config,
                                           const QString& serialNumber)
{
    ProductRouteResolution result;
    const auto sn = serialNumber.trimmed();
    if (sn.isEmpty()) {
        addError(result.errors,
                 QStringLiteral("serialNumber"),
                 QStringLiteral("SN cannot be empty"));
        return result;
    }

    QVector<const ProductRoute*> matches;
    for (const auto& route : config.routes) {
        if (!route.enabled || route.pattern.isEmpty()) {
            continue;
        }
        QRegularExpression expression(
            QRegularExpression::wildcardToRegularExpression(
                route.pattern,
                QRegularExpression::NonPathWildcardConversion));
        if (!expression.isValid()) {
            addError(result.errors,
                     QStringLiteral("routes"),
                     QStringLiteral("Invalid SN pattern: %1").arg(route.pattern),
                     expression.errorString());
            return result;
        }
        if (expression.match(sn).hasMatch()) {
            matches.push_back(&route);
        }
    }

    if (matches.isEmpty()) {
        addError(result.errors,
                 QStringLiteral("serialNumber"),
                 QStringLiteral("No product route matches SN: %1").arg(sn),
                 QStringLiteral("Add or enable a matching route in ProductRouting.json"));
        return result;
    }
    if (matches.size() > 1) {
        QStringList names;
        names.reserve(matches.size());
        for (const auto* route : matches) {
            names.push_back(routeDisplayName(*route));
        }
        addError(result.errors,
                 QStringLiteral("serialNumber"),
                 QStringLiteral("SN matches multiple product routes: %1")
                     .arg(names.join(QStringLiteral(", "))),
                 QStringLiteral("Make the route patterns mutually exclusive"));
        return result;
    }

    const auto& route = *matches.first();
    result.routeName = routeDisplayName(route);
    result.pattern = route.pattern;
    if (route.snLength > 0 && sn.size() != route.snLength) {
        addError(result.errors,
                 QStringLiteral("serialNumber.length"),
                 QStringLiteral("SN length is %1; route '%2' requires exactly %3 characters")
                     .arg(sn.size())
                     .arg(result.routeName)
                     .arg(route.snLength),
                 QStringLiteral("Scan the complete SN or update the route length"));
        return result;
    }
    if (!route.projectPath.isEmpty()) {
        const auto project = inspectProductProject(route.projectPath);
        if (!project.ok()) {
            result.errors = project.errors;
            return result;
        }
        result.projectName = project.name;
        result.projectPath = project.directoryPath;
        result.sequencePath = project.sequencePath;
        result.stationPath = project.stationPath;
        validateStationSnCharacters(result, sn);
        return result;
    }

    if (!QFileInfo(route.sequencePath).isFile()) {
        addError(result.errors,
                 QStringLiteral("routes.sequence"),
                 QStringLiteral("Routed Sequence file does not exist: %1")
                     .arg(route.sequencePath));
        return result;
    }
    const auto stationPath = QFileInfo(route.sequencePath).absoluteDir().filePath(
        QStringLiteral("StationSystem.json"));
    if (!QFileInfo(stationPath).isFile()) {
        addError(result.errors,
                 QStringLiteral("routes.station"),
                 QStringLiteral("Legacy route is missing StationSystem.json: %1")
                     .arg(stationPath),
                 QStringLiteral("Move Sequence and Station into one product project folder"));
        return result;
    }
    result.projectName = QFileInfo(route.sequencePath).absoluteDir().dirName();
    result.projectPath = QFileInfo(route.sequencePath).absolutePath();
    result.sequencePath = QFileInfo(route.sequencePath).absoluteFilePath();
    result.stationPath = QFileInfo(stationPath).absoluteFilePath();
    validateStationSnCharacters(result, sn);
    return result;
}

} // namespace PicoATE::Core
