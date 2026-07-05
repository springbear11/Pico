#include "PicoATE/Core/DllBridgeInvoker.h"
#include "PicoATE/Core/ModuleTransportJson.h"
#include "PicoATE/Core/NativeHostManifest.h"
#include "NativeHostDiagnosticCapture.h"
#include "NativeHostOutputPump.h"

#include <QCommandLineOption>
#include <QCommandLineParser>
#include <QCoreApplication>
#include <QElapsedTimer>
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
    NativeHostDiagnosticsConfig diagnostics;
};

ModuleTransportResponse errorResponse(const QString& code, const QString& message)
{
    ModuleTransportResponse response;
    response.outcome = ModuleOutcome::Error;
    response.errorCode = code;
    response.errorMessage = message;
    return response;
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

bool parseVendorStdioMode(const QString& value, NativeHostVendorStdioMode& output)
{
    const auto normalized = value.trimmed().toLower();
    if (normalized == "strict") {
        output = NativeHostVendorStdioMode::Strict;
        return true;
    }
    if (normalized == "discard") {
        output = NativeHostVendorStdioMode::Discard;
        return true;
    }
    return false;
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
    config.diagnostics = load.manifest.diagnostics;
    return true;
}

bool loadConfigFromLegacyOptions(const QCommandLineParser& parser,
                                 NativeHostRuntimeConfig& config,
                                 QTextStream& err)
{
    const auto dllValue = parser.value("dll").trimmed();
    if (dllValue.isEmpty()) {
        err << "--dll is required when --manifest is not used.\n";
        return false;
    }
    config.dllPath = QFileInfo(dllValue).absoluteFilePath();

    if (parser.isSet("symbol")) {
        config.symbol = parser.value("symbol");
    }
    if (parser.isSet("buffer-size") &&
        !parsePositiveInt(parser.value("buffer-size"), config.bufferSize)) {
        err << "--buffer-size must be a positive integer.\n";
        return false;
    }
    if (parser.isSet("dll-timeout-ms") &&
        !parsePositiveInt(parser.value("dll-timeout-ms"), config.dllTimeoutMs)) {
        err << "--dll-timeout-ms must be a positive integer.\n";
        return false;
    }
    return true;
}

bool applyDiagnosticsOverrides(const QCommandLineParser& parser,
                               NativeHostRuntimeConfig& config,
                               QTextStream& err)
{
    struct PositiveOverride {
        const char* option;
        int* target;
    };
    const PositiveOverride positiveOverrides[] = {
        {"log-queue-capacity", &config.diagnostics.maximumBufferedLogs},
        {"log-message-characters", &config.diagnostics.maximumMessageCharacters},
        {"log-batch-records", &config.diagnostics.maximumBatchRecords},
        {"log-batch-bytes", &config.diagnostics.maximumBatchBytes},
        {"log-flush-ms", &config.diagnostics.batchFlushMs},
    };

    for (const auto& overrideValue : positiveOverrides) {
        if (parser.isSet(overrideValue.option) &&
            !parsePositiveInt(parser.value(overrideValue.option), *overrideValue.target)) {
            err << "--" << overrideValue.option << " must be a positive integer.\n";
            return false;
        }
    }

    if (parser.isSet("vendor-stdio") &&
        !parseVendorStdioMode(parser.value("vendor-stdio"),
                              config.diagnostics.vendorStdioMode)) {
        err << "--vendor-stdio must be strict or discard.\n";
        return false;
    }
    return true;
}

} // namespace

int main(int argc, char* argv[])
{
    QCoreApplication app(argc, argv);
    QCoreApplication::setApplicationName("PicoATE.NativeHost");
    QCoreApplication::setApplicationVersion("0.2.0");

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
    parser.addOption(QCommandLineOption("symbol", "Exported function symbol. Default: PicoATE_Execute.", "name"));
    parser.addOption(QCommandLineOption("buffer-size", "Response buffer size. Default: 65536.", "bytes"));
    parser.addOption(QCommandLineOption("dll-timeout-ms",
                                        "In-host DLL call timeout. Default: 30000.",
                                        "ms"));
    parser.addOption(QCommandLineOption("vendor-stdio",
                                        "Vendor stdout/stderr mode: strict or discard. Default: strict.",
                                        "mode"));
    parser.addOption(QCommandLineOption("log-queue-capacity",
                                        "Maximum buffered live log records. Default: 1024.",
                                        "count"));
    parser.addOption(QCommandLineOption("log-message-characters",
                                        "Maximum characters retained per live log message. Default: 4096.",
                                        "count"));
    parser.addOption(QCommandLineOption("log-batch-records",
                                        "Maximum records per protocol log batch. Default: 64.",
                                        "count"));
    parser.addOption(QCommandLineOption("log-batch-bytes",
                                        "Approximate maximum bytes per protocol log batch. Default: 16384.",
                                        "bytes"));
    parser.addOption(QCommandLineOption("log-flush-ms",
                                        "Maximum live-log batching delay. Default: 20.",
                                        "ms"));
    parser.process(app);

    QTextStream err(stderr);
    NativeHostRuntimeConfig config;
    const auto loaded = parser.isSet(manifestOption)
        ? loadConfigFromManifest(parser, manifestOption, projectDirOption, variableOption, config, err)
        : loadConfigFromLegacyOptions(parser, config, err);
    if (!loaded || !applyDiagnosticsOverrides(parser, config, err)) {
        return 2;
    }

    if (!QFileInfo::exists(config.dllPath)) {
        err << "DLL does not exist: " << config.dllPath << '\n';
        return 2;
    }

    NativeHostOutputPump outputPump(config.diagnostics.maximumBufferedLogs,
                                    config.diagnostics.maximumMessageCharacters,
                                    config.diagnostics.maximumBatchRecords,
                                    config.diagnostics.maximumBatchBytes,
                                    config.diagnostics.batchFlushMs);
    const bool strictVendorStdio =
        config.diagnostics.vendorStdioMode == NativeHostVendorStdioMode::Strict;
    NativeHostDiagnosticCapture diagnosticCapture(
        strictVendorStdio ? static_cast<IModuleLogSink*>(&outputPump) : nullptr,
        config.diagnostics.maximumMessageCharacters);
    if (!diagnosticCapture.isActive()) {
        err << "Failed to isolate vendor stdout/stderr: "
            << diagnosticCapture.errorString() << '\n';
        return 2;
    }
    DllBridgeInvoker invoker(config.dllPath, config.symbol, config.bufferSize);

    QTextStream in(stdin);
    while (!in.atEnd()) {
        const auto line = in.readLine().trimmed();
        if (line.isEmpty()) {
            continue;
        }

        QElapsedTimer requestTimer;
        requestTimer.start();
        QJsonParseError parseError;
        const auto document = QJsonDocument::fromJson(line.toUtf8(), &parseError);
        if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
            outputPump.beginRequest({});
            outputPump.writeResponse(errorResponse("InvalidRequest", parseError.errorString()));
            continue;
        }
        const auto requestParsedAt = requestTimer.elapsed();

        ModuleTransportResponse response;
        auto request = moduleTransportRequestFromJson(document.object());
        request.context.logSink = &outputPump;
        outputPump.beginRequest(request.traceId);

        QElapsedTimer invokeTimer;
        invokeTimer.start();
        invoker.call(request, response, config.dllTimeoutMs);
        const auto dllInvokeMs = invokeTimer.elapsed();

        qint64 vendorFlushMs = 0;
        bool vendorFlushSucceeded = true;
        if (strictVendorStdio) {
            QElapsedTimer flushTimer;
            flushTimer.start();
            vendorFlushSucceeded = diagnosticCapture.flush();
            vendorFlushMs = flushTimer.elapsed();
            if (!vendorFlushSucceeded) {
                ModuleLogRecord warning;
                warning.timestampUtc = QDateTime::currentDateTimeUtc();
                warning.message = "PicoATE vendor diagnostic flush timed out";
                outputPump.publishModuleLog(warning);
            }
        }

        QVariantMap hostTiming;
        hostTiming.insert("requestParseMs", requestParsedAt);
        hostTiming.insert("dllInvokeMs", dllInvokeMs);
        hostTiming.insert("vendorFlushMs", vendorFlushMs);
        hostTiming.insert("beforeResponseMs", requestTimer.elapsed());
        hostTiming.insert("vendorFlushSucceeded", vendorFlushSucceeded);
        hostTiming.insert("vendorStdioMode",
                          nativeHostVendorStdioModeToString(
                              config.diagnostics.vendorStdioMode));
        response.diagnostics.insert("nativeHost", hostTiming);
        outputPump.writeResponse(response);
    }

    return 0;
}