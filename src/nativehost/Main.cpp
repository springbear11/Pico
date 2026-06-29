#include "PicoATE/Core/DllBridgeInvoker.h"
#include "PicoATE/Core/ModuleTransportJson.h"
#include "PicoATE/Core/NativeHostManifest.h"

#include <QCommandLineOption>
#include <QCommandLineParser>
#include <QCoreApplication>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QRegularExpression>
#include <QTextStream>

using namespace PicoATE::Core;

namespace {

struct NativeHostRuntimeConfig {
    QString dllPath;
    QString symbol = "PicoATE_Execute";
    int bufferSize = 65536;
    int dllTimeoutMs = 30000;
};

ModuleTransportResponse errorResponse(const QString& code, const QString& message)
{
    ModuleTransportResponse response;
    response.outcome = ModuleOutcome::Error;
    response.errorCode = code;
    response.errorMessage = message;
    return response;
}

void writeResponse(const ModuleTransportResponse& response)
{
    QTextStream out(stdout);
    out << QString::fromUtf8(QJsonDocument(moduleTransportResponseToJson(response))
                                 .toJson(QJsonDocument::Compact))
        << Qt::endl;
}

bool parsePositiveInt(const QString& value, int& output)
{
    bool ok = false;
    const auto parsed = value.toInt(&ok);
    if (!ok || parsed <= 0) {
        return false;
    }
    output = parsed;
    return true;
}

bool parseVariableAssignment(const QString& assignment, QString& name, QString& value)
{
    const auto equals = assignment.indexOf('=');
    if (equals <= 0) {
        return false;
    }

    name = assignment.left(equals).trimmed();
    value = assignment.mid(equals + 1);
    static const QRegularExpression namePattern(R"(^[A-Za-z_][A-Za-z0-9_]*$)");
    return namePattern.match(name).hasMatch();
}

bool loadConfigFromManifest(const QCommandLineParser& parser,
                            const QCommandLineOption& manifestOption,
                            const QCommandLineOption& projectDirOption,
                            const QCommandLineOption& variableOption,
                            NativeHostRuntimeConfig& config,
                            QTextStream& err)
{
    const auto manifestValue = parser.value(manifestOption).trimmed();
    if (manifestValue.isEmpty()) {
        err << "--manifest requires a path.\n";
        return false;
    }
    const auto manifestPath = QFileInfo(manifestValue).absoluteFilePath();

    VariableResolverOptions resolverOptions;
    resolverOptions.sequenceFilePath = manifestPath;
    resolverOptions.projectDir = parser.value(projectDirOption);

    for (const auto& assignment : parser.values(variableOption)) {
        QString name;
        QString value;
        if (!parseVariableAssignment(assignment, name, value)) {
            err << "--var must use NAME=VALUE with a valid variable name: " << assignment << '\n';
            return false;
        }
        resolverOptions.variables.insert(name, value);
    }

    const auto load = loadNativeHostManifest(manifestPath, resolverOptions);
    if (!load.ok()) {
        err << "Failed to load NativeHost manifest: " << manifestPath << '\n';
        for (const auto& error : load.errors) {
            err << "  - " << (error.path.isEmpty() ? QString("<root>") : error.path)
                << ": " << error.message;
            if (!error.suggestion.isEmpty()) {
                err << " (" << error.suggestion << ')';
            }
            err << '\n';
        }
        return false;
    }

    config.dllPath = load.manifest.dllPath;
    config.symbol = load.manifest.symbol;
    config.bufferSize = load.manifest.bufferSize;
    config.dllTimeoutMs = load.manifest.dllTimeoutMs;
    return true;
}

bool loadConfigFromLegacyOptions(const QCommandLineParser& parser,
                                 NativeHostRuntimeConfig& config,
                                 QTextStream& err)
{
    config.dllPath = QFileInfo(parser.value("dll")).absoluteFilePath();
    if (parser.value("dll").trimmed().isEmpty()) {
        err << "--dll is required when --manifest is not used.\n";
        return false;
    }

    config.symbol = parser.value("symbol");

    if (!parsePositiveInt(parser.value("buffer-size"), config.bufferSize)) {
        err << "--buffer-size must be a positive integer.\n";
        return false;
    }

    if (!parsePositiveInt(parser.value("dll-timeout-ms"), config.dllTimeoutMs)) {
        err << "--dll-timeout-ms must be a positive integer.\n";
        return false;
    }
    return true;
}

} // namespace

int main(int argc, char* argv[])
{
    QCoreApplication app(argc, argv);
    QCoreApplication::setApplicationName("PicoATE.NativeHost");
    QCoreApplication::setApplicationVersion("0.1.0");

    QCommandLineParser parser;
    parser.setApplicationDescription("PicoATE native DLL host over stdio JSON.");
    parser.addHelpOption();
    parser.addVersionOption();

    QCommandLineOption manifestOption("manifest", "NativeHost manifest JSON file.", "path");
    QCommandLineOption projectDirOption("project-dir", "Project directory used by manifest variable resolution.", "path");
    QCommandLineOption variableOption("var", "Manifest variable assignment. Can be repeated.", "NAME=VALUE");

    parser.addOption(manifestOption);
    parser.addOption(projectDirOption);
    parser.addOption(variableOption);
    parser.addOption(QCommandLineOption("dll", "DLL path to load when --manifest is not used.", "path"));
    parser.addOption(QCommandLineOption("symbol", "Exported function symbol.", "name", "PicoATE_Execute"));
    parser.addOption(QCommandLineOption("buffer-size", "Response buffer size.", "bytes", "65536"));
    parser.addOption(QCommandLineOption("dll-timeout-ms",
                                        "Optional in-host DLL call timeout. Parent process timeout still applies.",
                                        "ms",
                                        "30000"));
    parser.process(app);

    QTextStream err(stderr);
    NativeHostRuntimeConfig config;
    const auto loaded = parser.isSet(manifestOption)
        ? loadConfigFromManifest(parser, manifestOption, projectDirOption, variableOption, config, err)
        : loadConfigFromLegacyOptions(parser, config, err);
    if (!loaded) {
        return 2;
    }

    if (!QFileInfo::exists(config.dllPath)) {
        err << "DLL does not exist: " << config.dllPath << '\n';
        return 2;
    }

    DllBridgeInvoker invoker(config.dllPath, config.symbol, config.bufferSize);

    QTextStream in(stdin);
    while (!in.atEnd()) {
        const auto line = in.readLine().trimmed();
        if (line.isEmpty()) {
            continue;
        }

        QJsonParseError parseError;
        const auto document = QJsonDocument::fromJson(line.toUtf8(), &parseError);
        if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
            writeResponse(errorResponse("InvalidRequest", parseError.errorString()));
            continue;
        }

        ModuleTransportResponse response;
        const auto request = moduleTransportRequestFromJson(document.object());
        invoker.call(request, response, config.dllTimeoutMs);
        writeResponse(response);
    }

    return 0;
}
