#include "PicoATE/Core/NativeHostManifest.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>

namespace PicoATE::Core {

namespace {

void addError(NativeHostManifestResult& result,
              const QString& path,
              const QString& message,
              const QString& suggestion = {})
{
    result.errors.push_back({path, message, suggestion});
}

QString jsonTypeName(const QJsonValue& value)
{
    if (value.isNull()) {
        return "null";
    }
    if (value.isBool()) {
        return "bool";
    }
    if (value.isDouble()) {
        return "number";
    }
    if (value.isString()) {
        return "string";
    }
    if (value.isArray()) {
        return "array";
    }
    if (value.isObject()) {
        return "object";
    }
    return "undefined";
}

QString readString(const QJsonObject& object,
                   const QString& key,
                   NativeHostManifestResult& result,
                   const QString& fallback = {})
{
    if (!object.contains(key)) {
        return fallback;
    }

    const auto value = object.value(key);
    if (!value.isString()) {
        addError(result,
                 key,
                 QString("Expected string, got %1").arg(jsonTypeName(value)),
                 "Fix the manifest field type");
        return fallback;
    }
    return value.toString();
}

int readPositiveInt(const QJsonObject& object,
                    const QString& key,
                    NativeHostManifestResult& result,
                    int fallback)
{
    if (!object.contains(key)) {
        return fallback;
    }

    const auto value = object.value(key);
    if (!value.isDouble()) {
        addError(result,
                 key,
                 QString("Expected number, got %1").arg(jsonTypeName(value)),
                 "Fix the manifest field type");
        return fallback;
    }

    const auto parsed = value.toInt();
    if (parsed <= 0) {
        addError(result,
                 key,
                 "Expected a positive integer",
                 "Use a value greater than zero");
        return fallback;
    }
    return parsed;
}

QString resolveDllPath(QString path, const VariableResolver& resolver)
{
    const QFileInfo info(path);
    if (path.isEmpty() || info.isAbsolute()) {
        return path;
    }

    const QDir base(resolver.sequenceDir());
    return QFileInfo(base.filePath(path)).absoluteFilePath();
}

void appendResolutionErrors(NativeHostManifestResult& result,
                            const QVector<VariableResolutionError>& errors)
{
    for (const auto& error : errors) {
        addError(result, error.path, error.message, error.suggestion);
    }
}

} // namespace

NativeHostManifestResult loadNativeHostManifest(const QString& manifestPath,
                                                VariableResolverOptions resolverOptions)
{
    NativeHostManifestResult result;
    const auto absoluteManifestPath = QFileInfo(manifestPath).absoluteFilePath();
    if (resolverOptions.sequenceFilePath.isEmpty()) {
        resolverOptions.sequenceFilePath = absoluteManifestPath;
    }

    QFile file(absoluteManifestPath);
    if (!file.open(QIODevice::ReadOnly)) {
        addError(result,
                 "manifest",
                 QString("Failed to open manifest: %1").arg(absoluteManifestPath),
                 file.errorString());
        return result;
    }

    QJsonParseError parseError;
    const auto document = QJsonDocument::fromJson(file.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError) {
        addError(result,
                 "manifest",
                 QString("Failed to parse manifest JSON at offset %1").arg(parseError.offset),
                 parseError.errorString());
        return result;
    }

    if (!document.isObject()) {
        addError(result,
                 "manifest",
                 "NativeHost manifest must be a JSON object",
                 "Use an object with a dll field");
        return result;
    }

    const auto object = document.object();
    const auto dllKey = object.contains("dll") ? QString("dll") : QString("dllPath");
    auto dllPath = readString(object, dllKey, result);
    if (dllPath.trimmed().isEmpty()) {
        addError(result, dllKey, "DLL path is required", "Set dll to the module DLL path");
    }

    result.manifest.symbol = readString(object, "symbol", result, result.manifest.symbol);
    result.manifest.bufferSize = readPositiveInt(object, "bufferSize", result, result.manifest.bufferSize);
    result.manifest.dllTimeoutMs = readPositiveInt(object, "dllTimeoutMs", result, result.manifest.dllTimeoutMs);

    if (object.contains("metadata")) {
        const auto metadata = object.value("metadata");
        if (metadata.isObject()) {
            result.manifest.metadata = metadata.toObject().toVariantMap();
        } else {
            addError(result,
                     "metadata",
                     QString("Expected object, got %1").arg(jsonTypeName(metadata)),
                     "Fix the manifest field type");
        }
    }

    VariableResolver resolver(std::move(resolverOptions));
    QVector<VariableResolutionError> resolutionErrors;
    dllPath = resolver.resolveString(dllPath, resolutionErrors, dllKey);
    result.manifest.symbol = resolver.resolveString(result.manifest.symbol, resolutionErrors, "symbol");
    result.manifest.metadata = resolver.resolveMap(result.manifest.metadata, resolutionErrors, "metadata");
    appendResolutionErrors(result, resolutionErrors);

    result.manifest.dllPath = resolveDllPath(dllPath, resolver);
    return result;
}

} // namespace PicoATE::Core
