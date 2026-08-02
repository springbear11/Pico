#pragma once

#include <QJsonObject>
#include <QByteArray>
#include <QString>
#include <QStringList>
#include <QVariant>
#include <QVector>

#include <optional>

namespace PicoATE::Ui {

enum class PluginParameterType {
    String,
    Integer,
    Number,
    Boolean,
    Enumeration,
    HexBytes
};

struct PluginCatalogDiagnostic {
    QString path;
    QString message;
};

struct PluginBindingDiagnostic {
    QString path;
    QString message;
    QString suggestion;
    bool warning = false;
};

struct PluginParameterOption {
    QString label;
    QVariant value;
};

struct PluginParameterDefinition {
    QString key;
    QString name;
    PluginParameterType type = PluginParameterType::String;
    bool required = false;
    QVariant defaultValue;
    std::optional<double> minimum;
    std::optional<double> maximum;
    QString unit;
    QVector<PluginParameterOption> options;
};

struct PluginOutputDefinition {
    QString key;
    QString name;
    PluginParameterType type = PluginParameterType::String;
    QString unit;
};

struct PluginFunctionDefinition {
    QString id;
    QString name;
    QString description;
    QString stepKind = QStringLiteral("action");
    int timeoutMs = 0;
    QVector<PluginParameterDefinition> inputs;
    QVector<PluginOutputDefinition> outputs;
    QJsonObject stepTemplate;
};

struct PluginManifest {
    QString sourcePath;
    QString dllPath;
    int abiVersion = 0;
    QString moduleId;
    QString name;
    QString category;
    QString vendor;
    QString version;
    QStringList connectionKinds;
    QVector<PluginFunctionDefinition> functions;
    QJsonObject description;
};

struct PluginManifestResult {
    PluginManifest manifest;
    QVector<PluginCatalogDiagnostic> errors;

    bool ok() const { return errors.isEmpty(); }
};

struct PluginRegistryResult {
    QVector<PluginManifest> plugins;
    QVector<PluginCatalogDiagnostic> errors;

    bool ok() const { return errors.isEmpty(); }
};

struct PluginScanResult {
    QVector<PluginManifest> plugins;
    QVector<PluginCatalogDiagnostic> errors;
    int discoveredDllCount = 0;
    bool registrySaved = false;

    bool ok() const { return errors.isEmpty() && registrySaved; }
};

class PluginCatalog final
{
public:
    static PluginManifestResult parse(const QByteArray& json,
                                      const QString& sourcePath = {});
    static PluginManifestResult parseDescription(const QByteArray& json,
                                                 const QString& dllPath,
                                                 int abiVersion);
    static QStringList discoverPluginFiles(const QString& rootDirectory);
    static bool nativeHostSupportsDescribe(const QString& nativeHostProgram,
                                           int timeoutMs = 3000,
                                           QString* errorMessage = nullptr);
    static PluginScanResult scanPlugins(const QString& rootDirectory,
                                        const QString& nativeHostProgram,
                                        const QString& registryFilePath,
                                        int timeoutMs = 5000);
    static bool saveRegistry(const QString& filePath,
                             const QVector<PluginManifest>& plugins,
                             QString* errorMessage = nullptr);
    static PluginRegistryResult loadRegistry(const QString& filePath);
    static QVector<PluginBindingDiagnostic> validateStationBindings(
        const QJsonObject& station,
        const QVector<PluginManifest>& plugins,
        const QString& stationFilePath,
        const QString& projectDir);
    static QJsonObject createStep(const PluginManifest& manifest,
                                  const PluginFunctionDefinition& function,
                                  const QString& stepId) ;
};

QString pluginParameterTypeName(PluginParameterType type);
PluginManifest builtInDataParserManifest();

} // namespace PicoATE::Ui
