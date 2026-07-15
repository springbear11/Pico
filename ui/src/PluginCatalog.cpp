#include "PluginCatalog.h"

#include "PicoATE/Core/VariableResolver.h"

#include <QDirIterator>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QProcess>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSet>

#include <algorithm>

namespace PicoATE::Ui {

namespace {

constexpr int SupportedPluginAbiVersion = 1;

void addError(QVector<PluginCatalogDiagnostic>& errors,
              QString path,
              QString message)
{
    errors.push_back({std::move(path), std::move(message)});
}

QString requiredString(const QJsonObject& object,
                       const QString& key,
                       const QString& path,
                       QVector<PluginCatalogDiagnostic>& errors)
{
    const auto fieldPath = path.isEmpty() ? key : path + '.' + key;
    const auto value = object.value(key);
    if (!value.isString() || value.toString().trimmed().isEmpty()) {
        addError(errors, fieldPath, QStringLiteral("Expected a non-empty string"));
        return {};
    }
    return value.toString().trimmed();
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

QJsonObject registryEntry(const PluginManifest& plugin,
                          const QString& registryDirectory)
{
    QJsonObject entry;
    entry.insert(QStringLiteral("dll"),
                 QDir(registryDirectory).relativeFilePath(plugin.dllPath));
    entry.insert(QStringLiteral("abiVersion"), plugin.abiVersion);
    entry.insert(QStringLiteral("description"), plugin.description);
    return entry;
}

std::optional<PluginParameterType> parameterType(const QString& value)
{
    const auto normalized = value.trimmed().toLower();
    if (normalized == QStringLiteral("string")) return PluginParameterType::String;
    if (normalized == QStringLiteral("integer")) return PluginParameterType::Integer;
    if (normalized == QStringLiteral("number")) return PluginParameterType::Number;
    if (normalized == QStringLiteral("boolean")) return PluginParameterType::Boolean;
    if (normalized == QStringLiteral("enum")) return PluginParameterType::Enumeration;
    if (normalized == QStringLiteral("hex-bytes")) return PluginParameterType::HexBytes;
    return std::nullopt;
}

PluginParameterDefinition parseInput(
    const QJsonObject& object,
    const QString& path,
    QVector<PluginCatalogDiagnostic>& errors)
{
    PluginParameterDefinition result;
    result.key = requiredString(object, QStringLiteral("key"), path, errors);
    result.name = object.value(QStringLiteral("name")).toString(result.key).trimmed();
    const auto typeText = requiredString(object, QStringLiteral("type"), path, errors);
    const auto type = parameterType(typeText);
    if (type) {
        result.type = *type;
    } else if (!typeText.isEmpty()) {
        addError(errors, path + QStringLiteral(".type"),
                 QStringLiteral("Unsupported parameter type: %1").arg(typeText));
    }
    result.required = object.value(QStringLiteral("required")).toBool(false);
    if (object.contains(QStringLiteral("default"))) {
        result.defaultValue = object.value(QStringLiteral("default")).toVariant();
    }
    if (object.contains(QStringLiteral("minimum"))) {
        if (object.value(QStringLiteral("minimum")).isDouble()) {
            result.minimum = object.value(QStringLiteral("minimum")).toDouble();
        } else {
            addError(errors, path + QStringLiteral(".minimum"), QStringLiteral("Expected number"));
        }
    }
    if (object.contains(QStringLiteral("maximum"))) {
        if (object.value(QStringLiteral("maximum")).isDouble()) {
            result.maximum = object.value(QStringLiteral("maximum")).toDouble();
        } else {
            addError(errors, path + QStringLiteral(".maximum"), QStringLiteral("Expected number"));
        }
    }
    if (result.minimum && result.maximum && *result.minimum > *result.maximum) {
        addError(errors, path, QStringLiteral("minimum must not exceed maximum"));
    }
    result.unit = object.value(QStringLiteral("unit")).toString();

    const auto optionsValue = object.value(QStringLiteral("options"));
    if (result.type == PluginParameterType::Enumeration) {
        if (!optionsValue.isArray() || optionsValue.toArray().isEmpty()) {
            addError(errors, path + QStringLiteral(".options"),
                     QStringLiteral("Enum parameter requires a non-empty options array"));
        } else {
            const auto options = optionsValue.toArray();
            for (int index = 0; index < options.size(); ++index) {
                const auto optionPath = QStringLiteral("%1.options[%2]").arg(path).arg(index);
                if (!options[index].isObject()) {
                    addError(errors, optionPath, QStringLiteral("Expected object"));
                    continue;
                }
                const auto option = options[index].toObject();
                PluginParameterOption parsed;
                parsed.label = requiredString(option, QStringLiteral("label"), optionPath, errors);
                if (!option.contains(QStringLiteral("value"))) {
                    addError(errors, optionPath + QStringLiteral(".value"),
                             QStringLiteral("Option value is required"));
                } else {
                    parsed.value = option.value(QStringLiteral("value")).toVariant();
                }
                result.options.push_back(std::move(parsed));
            }
        }
    }
    return result;
}

PluginOutputDefinition parseOutput(
    const QJsonObject& object,
    const QString& path,
    QVector<PluginCatalogDiagnostic>& errors)
{
    PluginOutputDefinition result;
    result.key = requiredString(object, QStringLiteral("key"), path, errors);
    result.name = object.value(QStringLiteral("name")).toString(result.key).trimmed();
    const auto typeText = requiredString(object, QStringLiteral("type"), path, errors);
    const auto type = parameterType(typeText);
    if (type) {
        result.type = *type;
    } else if (!typeText.isEmpty()) {
        addError(errors, path + QStringLiteral(".type"),
                 QStringLiteral("Unsupported parameter type: %1").arg(typeText));
    }
    result.unit = object.value(QStringLiteral("unit")).toString();
    return result;
}

PluginFunctionDefinition parseFunction(
    const QJsonObject& object,
    const QString& path,
    QVector<PluginCatalogDiagnostic>& errors)
{
    PluginFunctionDefinition result;
    result.id = requiredString(object, QStringLiteral("id"), path, errors);
    result.name = requiredString(object, QStringLiteral("name"), path, errors);
    result.description = object.value(QStringLiteral("description")).toString();
    result.stepKind = object.value(QStringLiteral("stepKind")).toString(
        QStringLiteral("action")).trimmed().toLower();
    const QSet<QString> supportedKinds = {QStringLiteral("action"),
                                          QStringLiteral("cleanup")};
    if (!supportedKinds.contains(result.stepKind)) {
        addError(errors, path + QStringLiteral(".stepKind"),
                 QStringLiteral("Use action or cleanup"));
    }
    if (object.contains(QStringLiteral("timeoutMs"))) {
        if (!object.value(QStringLiteral("timeoutMs")).isDouble()) {
            addError(errors, path + QStringLiteral(".timeoutMs"), QStringLiteral("Expected number"));
        } else {
            result.timeoutMs = object.value(QStringLiteral("timeoutMs")).toInt();
            if (result.timeoutMs < 0) {
                addError(errors, path + QStringLiteral(".timeoutMs"),
                         QStringLiteral("timeoutMs must not be negative"));
            }
        }
    }
    if (object.contains(QStringLiteral("stepTemplate"))) {
        if (object.value(QStringLiteral("stepTemplate")).isObject()) {
            result.stepTemplate = object.value(QStringLiteral("stepTemplate")).toObject();
        } else {
            addError(errors, path + QStringLiteral(".stepTemplate"), QStringLiteral("Expected object"));
        }
    }

    const auto inputsValue = object.value(QStringLiteral("inputs"));
    if (!inputsValue.isUndefined() && !inputsValue.isArray()) {
        addError(errors, path + QStringLiteral(".inputs"), QStringLiteral("Expected array"));
    } else {
        QSet<QString> keys;
        const auto inputs = inputsValue.toArray();
        for (int index = 0; index < inputs.size(); ++index) {
            const auto inputPath = QStringLiteral("%1.inputs[%2]").arg(path).arg(index);
            if (!inputs[index].isObject()) {
                addError(errors, inputPath, QStringLiteral("Expected object"));
                continue;
            }
            auto input = parseInput(inputs[index].toObject(), inputPath, errors);
            if (!input.key.isEmpty() && keys.contains(input.key)) {
                addError(errors, inputPath + QStringLiteral(".key"),
                         QStringLiteral("Duplicate input key: %1").arg(input.key));
            }
            keys.insert(input.key);
            result.inputs.push_back(std::move(input));
        }
    }

    const auto outputsValue = object.value(QStringLiteral("outputs"));
    if (!outputsValue.isUndefined() && !outputsValue.isArray()) {
        addError(errors, path + QStringLiteral(".outputs"), QStringLiteral("Expected array"));
    } else {
        QSet<QString> keys;
        const auto outputs = outputsValue.toArray();
        for (int index = 0; index < outputs.size(); ++index) {
            const auto outputPath = QStringLiteral("%1.outputs[%2]").arg(path).arg(index);
            if (!outputs[index].isObject()) {
                addError(errors, outputPath, QStringLiteral("Expected object"));
                continue;
            }
            auto output = parseOutput(outputs[index].toObject(), outputPath, errors);
            if (!output.key.isEmpty() && keys.contains(output.key)) {
                addError(errors, outputPath + QStringLiteral(".key"),
                         QStringLiteral("Duplicate output key: %1").arg(output.key));
            }
            keys.insert(output.key);
            result.outputs.push_back(std::move(output));
        }
    }
    return result;
}

} // namespace

PluginManifestResult PluginCatalog::parse(const QByteArray& json,
                                          const QString& sourcePath)
{
    PluginManifestResult result;
    result.manifest.sourcePath = sourcePath;
    QJsonParseError parseError;
    const auto document = QJsonDocument::fromJson(json, &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        addError(result.errors, QStringLiteral("$"),
                 parseError.error == QJsonParseError::NoError
                     ? QStringLiteral("Manifest root must be an object")
                     : parseError.errorString());
        return result;
    }

    const auto root = document.object();
    result.manifest.description = root;
    const bool legacyManifest = root.contains(QStringLiteral("schema"));
    if (legacyManifest) {
        if (root.value(QStringLiteral("schema")).toString() != QStringLiteral("picoate.plugin")) {
            addError(result.errors, QStringLiteral("schema"),
                     QStringLiteral("Expected picoate.plugin"));
        }
        if (root.value(QStringLiteral("schemaVersion")).toInt(-1) != 1) {
            addError(result.errors, QStringLiteral("schemaVersion"),
                     QStringLiteral("Only schemaVersion 1 is supported"));
        }
        result.manifest.pluginId = requiredString(
            root, QStringLiteral("pluginId"), {}, result.errors);
        result.manifest.moduleId = requiredString(
            root, QStringLiteral("moduleId"), {}, result.errors);
    } else {
        result.manifest.moduleId = moduleIdFromDllPath(sourcePath);
        result.manifest.pluginId = result.manifest.moduleId;
    }
    result.manifest.name = requiredString(root, QStringLiteral("name"), {}, result.errors);
    result.manifest.category = requiredString(root, QStringLiteral("category"), {}, result.errors);
    result.manifest.vendor = root.value(QStringLiteral("vendor")).toString().trimmed();
    result.manifest.version = root.value(QStringLiteral("version")).toString().trimmed();

    const auto functionsValue = root.value(QStringLiteral("functions"));
    if (!functionsValue.isArray() || functionsValue.toArray().isEmpty()) {
        addError(result.errors, QStringLiteral("functions"),
                 QStringLiteral("Expected a non-empty array"));
        return result;
    }
    QSet<QString> functionIds;
    const auto functions = functionsValue.toArray();
    for (int index = 0; index < functions.size(); ++index) {
        const auto path = QStringLiteral("functions[%1]").arg(index);
        if (!functions[index].isObject()) {
            addError(result.errors, path, QStringLiteral("Expected object"));
            continue;
        }
        auto function = parseFunction(functions[index].toObject(), path, result.errors);
        if (!function.id.isEmpty() && functionIds.contains(function.id)) {
            addError(result.errors, path + QStringLiteral(".id"),
                     QStringLiteral("Duplicate function id: %1").arg(function.id));
        }
        functionIds.insert(function.id);
        result.manifest.functions.push_back(std::move(function));
    }
    return result;
}

PluginManifestResult PluginCatalog::parseDescription(const QByteArray& json,
                                                     const QString& dllPath,
                                                     int abiVersion)
{
    auto result = parse(json, QFileInfo(dllPath).absoluteFilePath());
    result.manifest.dllPath = QFileInfo(dllPath).absoluteFilePath();
    result.manifest.abiVersion = abiVersion;
    if (abiVersion != SupportedPluginAbiVersion) {
        addError(result.errors,
                 QStringLiteral("abiVersion"),
                 QStringLiteral("Unsupported plugin ABI version: %1").arg(abiVersion));
    }
    return result;
}

PluginManifestResult PluginCatalog::load(const QString& filePath)
{
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly)) {
        PluginManifestResult result;
        result.manifest.sourcePath = QFileInfo(filePath).absoluteFilePath();
        addError(result.errors, QStringLiteral("$"), file.errorString());
        return result;
    }
    return parse(file.readAll(), QFileInfo(filePath).absoluteFilePath());
}

QStringList PluginCatalog::discoverManifestFiles(const QString& rootDirectory)
{
    QStringList result;
    QDirIterator iterator(rootDirectory,
                          {QStringLiteral("*.picoate-plugin.json")},
                          QDir::Files,
                          QDirIterator::Subdirectories);
    while (iterator.hasNext()) {
        result.push_back(QFileInfo(iterator.next()).absoluteFilePath());
    }
    std::sort(result.begin(), result.end(), [](const QString& left, const QString& right) {
        return left.compare(right, Qt::CaseInsensitive) < 0;
    });
    return result;
}

QStringList PluginCatalog::discoverPluginFiles(const QString& rootDirectory)
{
    QStringList result;
    QDirIterator iterator(rootDirectory,
                          {QStringLiteral("PicoATE.*.dll")},
                          QDir::Files,
                          QDirIterator::Subdirectories);
    while (iterator.hasNext()) {
        result.push_back(QFileInfo(iterator.next()).absoluteFilePath());
    }
    std::sort(result.begin(), result.end(), [](const QString& left, const QString& right) {
        return left.compare(right, Qt::CaseInsensitive) < 0;
    });
    return result;
}

bool PluginCatalog::nativeHostSupportsDescribe(const QString& nativeHostProgram,
                                               int timeoutMs,
                                               QString* errorMessage)
{
    if (!QFileInfo::exists(nativeHostProgram)) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("PicoATE.NativeHost executable does not exist");
        }
        return false;
    }
    QProcess process;
    process.setProgram(nativeHostProgram);
    process.setArguments({QStringLiteral("--help")});
    process.setProcessChannelMode(QProcess::MergedChannels);
    process.start();
    if (!process.waitForStarted(qMin(timeoutMs, 1000))) {
        if (errorMessage) *errorMessage = process.errorString();
        return false;
    }
    if (!process.waitForFinished(timeoutMs)) {
        process.kill();
        process.waitForFinished(1000);
        if (errorMessage) {
            *errorMessage = QStringLiteral("NativeHost capability check timed out");
        }
        return false;
    }
    const auto help = QString::fromUtf8(process.readAll()).toLower();
    if (process.exitStatus() != QProcess::NormalExit ||
        process.exitCode() != 0 ||
        !help.contains(QStringLiteral("--describe"))) {
        if (errorMessage) {
            *errorMessage = QStringLiteral(
                "NativeHost is an older version without --describe support");
        }
        return false;
    }
    return true;
}

PluginScanResult PluginCatalog::scanPlugins(const QString& rootDirectory,
                                            const QString& nativeHostProgram,
                                            const QString& registryFilePath,
                                            int timeoutMs)
{
    PluginScanResult result;
    const auto dllFiles = discoverPluginFiles(rootDirectory);
    result.discoveredDllCount = dllFiles.size();
    QString hostError;
    if (!nativeHostSupportsDescribe(nativeHostProgram, 3000, &hostError)) {
        addError(result.errors,
                 nativeHostProgram,
                 hostError);
        return result;
    }

    for (const auto& dllPath : dllFiles) {
        QProcess process;
        process.setProgram(nativeHostProgram);
        process.setArguments({QStringLiteral("--describe"),
                              QStringLiteral("--dll"), dllPath,
                              QStringLiteral("--buffer-size"), QStringLiteral("65536")});
        process.setProcessChannelMode(QProcess::SeparateChannels);
        process.start();
        if (!process.waitForStarted(qMin(timeoutMs, 3000))) {
            addError(result.errors, dllPath,
                     QStringLiteral("Could not start NativeHost: %1").arg(process.errorString()));
            continue;
        }
        if (!process.waitForFinished(timeoutMs)) {
            process.kill();
            process.waitForFinished(1000);
            addError(result.errors, dllPath,
                     QStringLiteral("Plugin description timed out"));
            continue;
        }
        const auto standardError = QString::fromUtf8(process.readAllStandardError()).trimmed();
        if (process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0) {
            addError(result.errors, dllPath,
                     standardError.isEmpty()
                         ? QStringLiteral("NativeHost exited with code %1").arg(process.exitCode())
                         : standardError);
            continue;
        }

        QJsonParseError parseError;
        const auto envelope = QJsonDocument::fromJson(
            process.readAllStandardOutput().trimmed(), &parseError);
        if (parseError.error != QJsonParseError::NoError || !envelope.isObject()) {
            addError(result.errors, dllPath,
                     QStringLiteral("NativeHost returned invalid description JSON: %1")
                         .arg(parseError.errorString()));
            continue;
        }
        const auto object = envelope.object();
        if (!object.value(QStringLiteral("description")).isObject()) {
            addError(result.errors, dllPath,
                     QStringLiteral("NativeHost response has no description object"));
            continue;
        }
        const auto descriptionBytes = QJsonDocument(
            object.value(QStringLiteral("description")).toObject())
                                          .toJson(QJsonDocument::Compact);
        auto parsed = parseDescription(descriptionBytes,
                                       dllPath,
                                       object.value(QStringLiteral("abiVersion")).toInt(-1));
        if (!parsed.ok()) {
            for (auto error : parsed.errors) {
                error.path = dllPath + QStringLiteral(":") + error.path;
                result.errors.push_back(std::move(error));
            }
            continue;
        }
        result.plugins.push_back(std::move(parsed.manifest));
    }

    QString saveError;
    result.registrySaved = saveRegistry(registryFilePath, result.plugins, &saveError);
    if (!result.registrySaved) {
        addError(result.errors, registryFilePath, saveError);
    }
    return result;
}

bool PluginCatalog::saveRegistry(const QString& filePath,
                                 const QVector<PluginManifest>& plugins,
                                 QString* errorMessage)
{
    QJsonArray entries;
    const auto directory = QFileInfo(filePath).absolutePath();
    for (const auto& plugin : plugins) {
        entries.push_back(registryEntry(plugin, directory));
    }
    QJsonObject root;
    root.insert(QStringLiteral("plugins"), entries);

    QSaveFile file(filePath);
    if (!file.open(QIODevice::WriteOnly)) {
        if (errorMessage) *errorMessage = file.errorString();
        return false;
    }
    if (file.write(QJsonDocument(root).toJson(QJsonDocument::Indented)) < 0 ||
        !file.commit()) {
        if (errorMessage) *errorMessage = file.errorString();
        return false;
    }
    return true;
}

PluginRegistryResult PluginCatalog::loadRegistry(const QString& filePath)
{
    PluginRegistryResult result;
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly)) {
        addError(result.errors, filePath, file.errorString());
        return result;
    }
    QJsonParseError parseError;
    const auto document = QJsonDocument::fromJson(file.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        addError(result.errors, QStringLiteral("$"), parseError.errorString());
        return result;
    }
    const auto pluginsValue = document.object().value(QStringLiteral("plugins"));
    if (!pluginsValue.isArray()) {
        addError(result.errors, QStringLiteral("plugins"), QStringLiteral("Expected array"));
        return result;
    }
    const auto entries = pluginsValue.toArray();
    const auto registryDirectory = QFileInfo(filePath).absolutePath();
    for (int index = 0; index < entries.size(); ++index) {
        const auto path = QStringLiteral("plugins[%1]").arg(index);
        if (!entries[index].isObject()) {
            addError(result.errors, path, QStringLiteral("Expected object"));
            continue;
        }
        const auto entry = entries[index].toObject();
        const auto dllValue = entry.value(QStringLiteral("dll"));
        if (!dllValue.isString() || dllValue.toString().trimmed().isEmpty()) {
            addError(result.errors, path + QStringLiteral(".dll"),
                     QStringLiteral("Expected a non-empty string"));
            continue;
        }
        if (!entry.value(QStringLiteral("description")).isObject()) {
            addError(result.errors, path + QStringLiteral(".description"),
                     QStringLiteral("Expected object"));
            continue;
        }
        const auto dllPath = QDir(registryDirectory).absoluteFilePath(
            dllValue.toString());
        auto parsed = parseDescription(
            QJsonDocument(entry.value(QStringLiteral("description")).toObject())
                .toJson(QJsonDocument::Compact),
            dllPath,
            entry.value(QStringLiteral("abiVersion")).toInt(-1));
        if (!parsed.ok()) {
            for (auto error : parsed.errors) {
                error.path = path + QLatin1Char('.') + error.path;
                result.errors.push_back(std::move(error));
            }
            continue;
        }
        result.plugins.push_back(std::move(parsed.manifest));
    }
    return result;
}

QVector<PluginBindingDiagnostic> PluginCatalog::validateStationBindings(
    const QJsonObject& station,
    const QVector<PluginManifest>& plugins,
    const QString& stationFilePath,
    const QString& projectDir)
{
    QVector<PluginBindingDiagnostic> result;
    PicoATE::Core::VariableResolverOptions options;
    options.sequenceFilePath = stationFilePath;
    options.projectDir = projectDir;
    const PicoATE::Core::VariableResolver resolver(options);
    struct DriverBinding {
        QString deviceId;
        QString resolvedPath;
    };
    QHash<QString, DriverBinding> bindingByDriver;

    const auto devices = station.value(QStringLiteral("devices")).toArray();
    for (int index = 0; index < devices.size(); ++index) {
        const auto device = devices[index].toObject();
        if (device.isEmpty() || !device.value(QStringLiteral("enabled")).toBool(true)) {
            continue;
        }
        const auto basePath = QStringLiteral("devices[%1]").arg(index);
        const auto driverId = device.value(QStringLiteral("driverId")).toString(
            device.value(QStringLiteral("driver")).toString()).trimmed();
        const auto deviceId = device.value(QStringLiteral("deviceId")).toString(
            device.value(QStringLiteral("id")).toString()).trimmed();
        if (driverId.isEmpty()) {
            continue;
        }
        const auto plugin = std::find_if(
            plugins.cbegin(), plugins.cend(), [&driverId](const PluginManifest& item) {
                return item.moduleId == driverId;
            });
        const auto sourcePath = device.value(QStringLiteral("pluginPath"))
                                    .toString().trimmed();
        if (sourcePath.isEmpty()) {
            if (plugin != plugins.cend()) {
                result.push_back({basePath + QStringLiteral(".pluginPath"),
                                  QStringLiteral("Plugin DLL path is not configured for %1")
                                      .arg(driverId),
                                  QStringLiteral("Select the plugin model again or set pluginPath"),
                                  false});
            }
            continue;
        }

        QVector<PicoATE::Core::VariableResolutionError> resolutionErrors;
        auto resolvedPath = resolver.resolveString(
            sourcePath, resolutionErrors, basePath + QStringLiteral(".pluginPath"));
        for (const auto& error : resolutionErrors) {
            result.push_back({error.path, error.message, error.suggestion, false});
        }
        if (!resolutionErrors.isEmpty()) {
            continue;
        }
        if (QFileInfo(resolvedPath).isRelative()) {
            resolvedPath = QFileInfo(QFileInfo(stationFilePath).absoluteDir()
                                         .filePath(resolvedPath)).absoluteFilePath();
        } else {
            resolvedPath = QFileInfo(resolvedPath).absoluteFilePath();
        }
        if (!QFileInfo::exists(resolvedPath)) {
            result.push_back({basePath + QStringLiteral(".pluginPath"),
                              QStringLiteral("Plugin DLL does not exist: %1").arg(resolvedPath),
                              QStringLiteral("Rescan plugins or correct the Station binding"),
                              false});
        }
        if (plugin == plugins.cend()) {
            result.push_back({basePath + QStringLiteral(".driverId"),
                              QStringLiteral("Plugin description is not in PluginRegistry: %1")
                                  .arg(driverId),
                              QStringLiteral("Run Scan Plugins to refresh function descriptions"),
                              true});
        }
        const auto previous = bindingByDriver.value(driverId);
        if (!previous.resolvedPath.isEmpty() &&
            QFileInfo(previous.resolvedPath).canonicalFilePath() !=
                QFileInfo(resolvedPath).canonicalFilePath()) {
            result.push_back({basePath + QStringLiteral(".pluginPath"),
                              QStringLiteral(
                                  "Driver '%1' uses different DLL files: %2 -> %3; %4 -> %5")
                                  .arg(driverId,
                                       previous.deviceId.isEmpty()
                                           ? QStringLiteral("previous device")
                                           : previous.deviceId,
                                       QDir::toNativeSeparators(previous.resolvedPath),
                                       deviceId.isEmpty()
                                           ? QStringLiteral("current device")
                                           : deviceId,
                                       QDir::toNativeSeparators(resolvedPath)),
                              QStringLiteral(
                                  "Choose the same Plugin / Model for these devices, apply the device settings, and save Station Config"),
                              false});
        } else if (previous.resolvedPath.isEmpty()) {
            bindingByDriver.insert(driverId, {deviceId, resolvedPath});
        }
    }
    return result;
}

QJsonObject PluginCatalog::createStep(const PluginManifest& manifest,
                                      const PluginFunctionDefinition& function,
                                      const QString& stepId)
{
    auto step = function.stepTemplate;
    step.insert(QStringLiteral("id"), stepId);
    step.insert(QStringLiteral("name"), function.name);
    step.insert(QStringLiteral("kind"), function.stepKind);
    step.insert(QStringLiteral("enabled"), true);
    step.insert(QStringLiteral("moduleId"), manifest.moduleId);
    step.insert(QStringLiteral("function"), function.id);
    if (function.timeoutMs > 0) {
        step.insert(QStringLiteral("timeoutMs"), function.timeoutMs);
    }
    QJsonObject inputs;
    for (const auto& input : function.inputs) {
        if (input.defaultValue.isValid()) {
            inputs.insert(input.key, QJsonValue::fromVariant(input.defaultValue));
        }
    }
    if (!inputs.isEmpty()) {
        step.insert(QStringLiteral("inputs"), inputs);
    }
    return step;
}

QString pluginParameterTypeName(PluginParameterType type)
{
    switch (type) {
    case PluginParameterType::String: return QStringLiteral("string");
    case PluginParameterType::Integer: return QStringLiteral("integer");
    case PluginParameterType::Number: return QStringLiteral("number");
    case PluginParameterType::Boolean: return QStringLiteral("boolean");
    case PluginParameterType::Enumeration: return QStringLiteral("enum");
    case PluginParameterType::HexBytes: return QStringLiteral("hex-bytes");
    }
    return {};
}

} // namespace PicoATE::Ui
