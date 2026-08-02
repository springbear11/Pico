#include "CoreExecutionService.h"

#include "PluginCatalog.h"

#include "PicoATE/Core/ExecutionSession.h"
#include "PicoATE/Core/ModuleBindingRegistrar.h"
#include "PicoATE/Core/StationRunPreparation.h"

#include <QDir>
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QSet>

#include <algorithm>
#include <utility>

namespace PicoATE::Ui {

namespace {

UiDiagnostic error(QString path, QString message, QString suggestion = {})
{
    return {UiDiagnosticSeverity::Error,
            std::move(path),
            std::move(message),
            std::move(suggestion)};
}

QString pluginRegistryPath(const QJsonObject& stationObject,
                           const QString& stationPath)
{
    auto value = stationObject.value(QStringLiteral("pluginRegistry"))
                     .toString(QStringLiteral("plugins/PluginRegistry.json"))
                     .trimmed();
    if (QFileInfo(value).isAbsolute()) {
        return QFileInfo(value).absoluteFilePath();
    }
    return QFileInfo(QFileInfo(stationPath).absoluteDir().absoluteFilePath(value))
        .absoluteFilePath();
}

class RunEventSequencer final : public PicoATE::Core::IRuntimeEventSink
{
public:
    explicit RunEventSequencer(PicoATE::Core::IRuntimeEventSink* target)
        : m_target(target)
    {
    }

    void publish(const PicoATE::Core::RuntimeEvent& source) override
    {
        if (!m_target) return;
        auto event = source;
        event.sequenceNumber = m_nextSequence++;
        m_target->publish(event);
    }

private:
    PicoATE::Core::IRuntimeEventSink* m_target = nullptr;
    quint64 m_nextSequence = 1;
};

} // namespace

CoreExecutionService::CoreExecutionService(QString projectDir)
    : m_projectDir(std::move(projectDir))
{
    if (m_projectDir.isEmpty()) {
#ifdef PICOATE_PROJECT_DIR
        m_projectDir = QString::fromUtf8(PICOATE_PROJECT_DIR);
#else
        m_projectDir = QDir::currentPath();
#endif
    }
    m_projectDir = QFileInfo(m_projectDir).absoluteFilePath();
}

CompileServiceResult CoreExecutionService::compile(const CompileRequest& request)
{
    CompileServiceResult result;
    result.requestId = request.requestId;
    m_compiled.reset();

    QJsonObject sequenceObject;
    result.diagnostics = readSequenceJson(
        request.sequencePath, request.sequenceJson, sequenceObject);
    if (!result.diagnostics.isEmpty()) {
        return result;
    }

    PicoATE::Core::SequenceCompiler compiler;
    const auto compileResult = compiler.compileJson(sequenceObject);
    for (const auto& diagnostic : compileResult.errors) {
        result.diagnostics.push_back(
            error(diagnostic.path, diagnostic.message, diagnostic.suggestion));
    }
    for (const auto& diagnostic : compileResult.warnings) {
        result.diagnostics.push_back({UiDiagnosticSeverity::Warning,
                                      diagnostic.path,
                                      diagnostic.message,
                                      diagnostic.suggestion});
    }
    if (!compileResult.ok()) {
        return result;
    }

    CompiledArtifact artifact;
    artifact.sequencePath = QFileInfo(request.sequencePath).absoluteFilePath();
    artifact.sequence = compileResult.sequence;
    artifact.plan = compileResult.plan;

    if (!request.stationPath.trimmed().isEmpty()) {
        QJsonObject stationObject;
        const auto stationReadDiagnostics = readStationJson(
            request.stationPath, request.stationJson, stationObject);
        result.diagnostics += stationReadDiagnostics;
        if (!stationReadDiagnostics.isEmpty()) {
            return result;
        }
        const auto stationResult = PicoATE::Core::parseStationConfigJson(
            stationObject, resolverOptions(request.stationPath));
        for (const auto& diagnostic : stationResult.errors) {
            result.diagnostics.push_back(
                error(diagnostic.path, diagnostic.message, diagnostic.suggestion));
        }
        if (!stationResult.ok()) {
            return result;
        }
        artifact.station = stationResult.config;
        artifact.stationDocument = stationObject;
        artifact.stationPath = QFileInfo(request.stationPath).absoluteFilePath();
        const auto registryPath = pluginRegistryPath(stationObject,
                                                     request.stationPath);
        const auto registry = PluginCatalog::loadRegistry(registryPath);
        const auto pluginDiagnostics = PluginCatalog::validateStationBindings(
            stationObject,
            registry.plugins,
            request.stationPath,
            m_projectDir);
        for (const auto& diagnostic : pluginDiagnostics) {
            result.diagnostics.push_back(
                {diagnostic.warning ? UiDiagnosticSeverity::Warning
                                    : UiDiagnosticSeverity::Error,
                 diagnostic.path,
                 diagnostic.message,
                 diagnostic.suggestion});
        }

        QSet<QString> deviceIds;
        for (const auto& device : stationResult.config.devices) {
            deviceIds.insert(device.deviceId);
        }
        for (const auto* step : artifact.sequence.allSteps()) {
            if (!step || step->moduleId != QStringLiteral("device")) {
                continue;
            }
            const auto deviceId = step->inputs.value(QStringLiteral("deviceId"))
                                      .toString().trimmed();
            if (deviceId.isEmpty()) {
                result.diagnostics.push_back(error(
                    step->id,
                    QStringLiteral("Logical device step requires inputs.deviceId"),
                    QStringLiteral("Select a Station logical device in Flow Editor")));
            } else if (!deviceIds.contains(deviceId)) {
                result.diagnostics.push_back(error(
                    step->id,
                    QStringLiteral("Logical device is not configured: %1").arg(deviceId),
                    QStringLiteral("Add or enable the device in Station Config")));
            }
        }
        if (std::any_of(result.diagnostics.cbegin(), result.diagnostics.cend(),
                        [](const UiDiagnostic& diagnostic) {
                            return diagnostic.severity == UiDiagnosticSeverity::Error;
                        })) {
            return result;
        }
    }

    result.success = true;
    result.sequenceId = artifact.sequence.id;
    result.sequenceName = artifact.sequence.name;
    result.sequenceVersion = artifact.sequence.version;
    result.nodeCount = artifact.plan.nodes.size();
    result.loopTestCount = artifact.station && artifact.station->loopTestEnabled
        ? qMax(1, artifact.station->loopTestCount)
        : 1;
    PicoATE::Core::ExecutionSession previewSession(artifact.plan);
    previewSession.addUut(QStringLiteral("UUT"));
    result.previewReport = previewSession.report();
    m_compiled = std::move(artifact);
    return result;
}

RunServiceResult CoreExecutionService::run(
    const RunRequest& request,
    const std::shared_ptr<PicoATE::Core::StopToken>& stopToken,
    PicoATE::Core::IRuntimeEventSink* eventSink,
    const std::shared_ptr<PicoATE::Core::ExecutionControl>& executionControl)
{
    RunServiceResult result;
    result.requestId = request.requestId;
    if (!m_compiled) {
        result.diagnostics.push_back(
            error({}, "No compiled sequence is available", "Compile the selected sequence before running"));
        return result;
    }
    if (request.uuts.isEmpty() && request.uutCount <= 0) {
        result.diagnostics.push_back(
            error("uutCount", "UUT count must be greater than zero"));
        return result;
    }

    QVector<RunRequest::UutInput> uutInputs = request.uuts;
    if (uutInputs.isEmpty()) {
        const QString prefix = request.uutPrefix.trimmed().isEmpty()
            ? QStringLiteral("UUT")
            : request.uutPrefix.trimmed();
        for (int index = 1; index <= request.uutCount; ++index) {
            RunRequest::UutInput input;
            input.uutId = QStringLiteral("%1-%2").arg(prefix).arg(index);
            uutInputs.push_back(std::move(input));
        }
    }

    QSet<PicoATE::Core::UutId> registeredUuts;
    for (auto& input : uutInputs) {
        input.uutId = input.uutId.trimmed();
        if (input.uutId.isEmpty()) {
            result.diagnostics.push_back(error("uuts", "UUT ID cannot be empty"));
            return result;
        }
        if (registeredUuts.contains(input.uutId)) {
            result.diagnostics.push_back(
                error("uuts", QString("Duplicate UUT ID: %1").arg(input.uutId)));
            return result;
        }
        registeredUuts.insert(input.uutId);

    }

    auto runStation = m_compiled->station;
    if (runStation && !m_compiled->stationDocument.isEmpty()) {
        PicoATE::Core::StationRunPreparationOptions preparationOptions;
        preparationOptions.stationFilePath = m_compiled->stationPath;
        preparationOptions.projectDir = m_projectDir;
        preparationOptions.nativeHostProgram = nativeHostProgram();
        const auto preparation = PicoATE::Core::StationRunPreparationService().prepare(
            m_compiled->stationDocument, preparationOptions);
        for (const auto& diagnostic : preparation.errors) {
            result.diagnostics.push_back(
                error(diagnostic.path, diagnostic.message, diagnostic.suggestion));
        }
        for (const auto& diagnostic : preparation.warnings) {
            result.diagnostics.push_back(
                {UiDiagnosticSeverity::Warning,
                 diagnostic.path,
                 diagnostic.message,
                 diagnostic.suggestion});
        }
        if (!preparation.ok()) {
            return result;
        }
        runStation = preparation.stationConfig;
    }

    const auto failureHandling = runStation
        ? PicoATE::Core::failureHandlingMode(*runStation)
        : PicoATE::Core::FailureHandlingMode::UseNodePolicy;
    RunEventSequencer eventSequencer(eventSink);
    PicoATE::Core::ExecutionSession session(
        m_compiled->plan,
        stopToken,
        eventSink ? &eventSequencer : nullptr,
        executionControl,
        failureHandling);
    if (runStation) {
        const auto stationErrors = PicoATE::Core::configureDeviceSessions(
            *runStation, session.devices());
        for (const auto& diagnostic : stationErrors) {
            result.diagnostics.push_back(
                error(diagnostic.path, diagnostic.message, diagnostic.suggestion));
        }
        if (!stationErrors.isEmpty()) {
            return result;
        }
    }

    PicoATE::Core::ModuleBindingRegistrationOptions bindingOptions;
    bindingOptions.sequenceFilePath = m_compiled->sequencePath;
    bindingOptions.projectDir = m_projectDir;
    const auto bindingResult = PicoATE::Core::registerConfiguredModules(
        session, m_compiled->sequence, bindingOptions);
    for (const auto& diagnostic : bindingResult.errors) {
        result.diagnostics.push_back(
            error(diagnostic.moduleId, diagnostic.message, diagnostic.suggestion));
    }
    if (!bindingResult.ok()) {
        return result;
    }

    if (runStation) {
        PicoATE::Core::StationPluginRegistrationOptions pluginOptions;
        pluginOptions.stationFilePath = m_compiled->stationPath;
        pluginOptions.projectDir = m_projectDir;
        pluginOptions.nativeHostProgram = nativeHostProgram();
        const auto pluginResult = PicoATE::Core::registerStationPluginModules(
            session, *runStation, pluginOptions);
        for (const auto& diagnostic : pluginResult.errors) {
            result.diagnostics.push_back(
                error(diagnostic.moduleId, diagnostic.message,
                      diagnostic.suggestion));
        }
        if (!pluginResult.ok()) {
            return result;
        }
    }

    for (int index = 0; index < uutInputs.size(); ++index) {
        const auto& input = uutInputs[index];
        const auto binding = PicoATE::Core::bindSequenceVariablesForUut(
            m_compiled->plan.variables, index, input.uutId, input.variables);
        for (const auto& diagnostic : binding.errors) {
            result.diagnostics.push_back(error(
                diagnostic.variableName.isEmpty()
                    ? QStringLiteral("variables")
                    : QStringLiteral("variables.%1").arg(diagnostic.variableName),
                diagnostic.message,
                QStringLiteral("Configure a value for this UUT in Flow > Variables")));
        }
        if (!binding.ok()) {
            return result;
        }
        auto& uut = session.addUut(input.uutId);
        uut.variables = binding.variables;
    }

    result.executed = true;
    session.run();
    result.report = session.report();
    result.stopRequested = stopToken && stopToken->isStopRequested();
    return result;
}

DeviceConnectionTestResult CoreExecutionService::testDeviceConnection(
    const DeviceConnectionTestRequest& request,
    const std::shared_ptr<PicoATE::Core::StopToken>& stopToken)
{
    DeviceConnectionTestResult result;
    result.requestId = request.requestId;
    result.deviceId = request.deviceId.trimmed();
    QElapsedTimer timer;
    timer.start();

    auto finish = [&](DeviceConnectionTestOutcome outcome,
                      QString errorCode = {},
                      QString errorMessage = {},
                      QString suggestion = {}) {
        result.outcome = outcome;
        result.errorCode = std::move(errorCode);
        result.errorMessage = std::move(errorMessage);
        result.suggestion = std::move(suggestion);
        result.elapsedMs = timer.elapsed();
        return result;
    };
    auto cancelled = [&] {
        return stopToken && stopToken->isStopRequested();
    };
    auto failureOutcome = [&](const QString& message) {
        if (cancelled()) {
            return DeviceConnectionTestOutcome::Cancelled;
        }
        return message.contains("timed out", Qt::CaseInsensitive) ||
               message.contains("timeout", Qt::CaseInsensitive)
            ? DeviceConnectionTestOutcome::TimedOut
            : DeviceConnectionTestOutcome::Failed;
    };

    if (result.deviceId.isEmpty()) {
        return finish(DeviceConnectionTestOutcome::Failed,
                      "DeviceIdMissing",
                      "Logical device id is empty");
    }
    if (cancelled()) {
        return finish(DeviceConnectionTestOutcome::Cancelled,
                      "ConnectionTestCancelled",
                      "Device connection test was cancelled");
    }

    QJsonObject sequenceObject;
    const auto sequenceDiagnostics = readSequenceJson(
        request.sequencePath, request.sequenceJson, sequenceObject);
    if (!sequenceDiagnostics.isEmpty()) {
        const auto& diagnostic = sequenceDiagnostics.first();
        return finish(DeviceConnectionTestOutcome::Failed,
                      "SequenceReadFailed",
                      diagnostic.message,
                      diagnostic.suggestion);
    }
    PicoATE::Core::SequenceCompiler compiler;
    auto sequenceResult = compiler.compileJson(sequenceObject);
    if (!sequenceResult.ok()) {
        const auto& diagnostic = sequenceResult.errors.first();
        return finish(DeviceConnectionTestOutcome::Failed,
                      "SequenceCompileFailed",
                      diagnostic.message,
                      diagnostic.suggestion);
    }

    QJsonObject stationObject;
    const auto stationDiagnostics = readStationJson(
        request.stationPath, request.stationJson, stationObject);
    if (!stationDiagnostics.isEmpty()) {
        const auto& diagnostic = stationDiagnostics.first();
        return finish(DeviceConnectionTestOutcome::Failed,
                      "StationReadFailed",
                      diagnostic.message,
                      diagnostic.suggestion);
    }
    PicoATE::Core::StationRunPreparationOptions preparationOptions;
    preparationOptions.stationFilePath = request.stationPath;
    preparationOptions.projectDir = m_projectDir;
    preparationOptions.nativeHostProgram = nativeHostProgram();
    preparationOptions.discoveryTimeoutMs = qBound(100, request.timeoutMs, 60000);
    const auto stationResult = PicoATE::Core::StationRunPreparationService().prepare(
        stationObject, preparationOptions);
    if (!stationResult.ok()) {
        const auto& diagnostic = stationResult.errors.first();
        return finish(DeviceConnectionTestOutcome::Failed,
                      "StationPreparationFailed",
                      diagnostic.message,
                      diagnostic.suggestion);
    }

    const auto deviceIt = std::find_if(
        stationResult.stationConfig.devices.cbegin(),
        stationResult.stationConfig.devices.cend(),
        [&](const PicoATE::Core::DeviceSessionConfig& config) {
            return config.deviceId == result.deviceId;
        });
    if (deviceIt == stationResult.stationConfig.devices.cend()) {
        return finish(DeviceConnectionTestOutcome::Failed,
                      "DeviceNotConfigured",
                      QString("Enabled device is not configured: %1").arg(result.deviceId),
                      "Enable the device and check its logical id");
    }

    const int timeoutMs = qBound(100, request.timeoutMs, 60000);
    for (auto& binding : sequenceResult.sequence.moduleBindings) {
        if (binding.moduleId == deviceIt->driverId) {
            binding.timeoutMs = timeoutMs;
        }
    }

    PicoATE::Core::ExecutionPlan emptyPlan;
    PicoATE::Core::ExecutionSession session(emptyPlan, stopToken);
    PicoATE::Core::ModuleBindingRegistrationOptions bindingOptions;
    bindingOptions.sequenceFilePath = request.sequencePath;
    bindingOptions.projectDir = m_projectDir;
    const auto bindingResult = PicoATE::Core::registerConfiguredModules(
        session, sequenceResult.sequence, bindingOptions);
    if (!bindingResult.ok()) {
        const auto& diagnostic = bindingResult.errors.first();
        return finish(DeviceConnectionTestOutcome::Failed,
                      "DriverBindingFailed",
                      diagnostic.message,
                      diagnostic.suggestion);
    }

    PicoATE::Core::StationPluginRegistrationOptions pluginOptions;
    pluginOptions.stationFilePath = request.stationPath;
    pluginOptions.projectDir = m_projectDir;
    pluginOptions.nativeHostProgram = nativeHostProgram();
    const auto pluginResult = PicoATE::Core::registerStationPluginModules(
        session, stationResult.stationConfig, pluginOptions);
    if (!pluginResult.ok()) {
        const auto& diagnostic = pluginResult.errors.first();
        return finish(DeviceConnectionTestOutcome::Failed,
                      "PluginBindingFailed",
                      diagnostic.message,
                      diagnostic.suggestion);
    }

    auto& devices = session.devices();
    PicoATE::Core::DeviceSessionError configureError;
    auto deviceConfig = *deviceIt;
    deviceConfig.timeoutMs = timeoutMs;
    if (!devices.configureDevice(deviceConfig, &configureError)) {
        return finish(DeviceConnectionTestOutcome::Failed,
                      configureError.errorCode,
                      configureError.message,
                      configureError.suggestion);
    }

    auto finishWithCleanup = [&](DeviceConnectionTestOutcome outcome,
                                 QString errorCode = {},
                                 QString errorMessage = {},
                                 QString suggestion = {}) {
        devices.closeSession(result.deviceId);
        return finish(outcome,
                      std::move(errorCode),
                      std::move(errorMessage),
                      std::move(suggestion));
    };

    if (cancelled()) {
        return finishWithCleanup(DeviceConnectionTestOutcome::Cancelled,
                                 "ConnectionTestCancelled",
                                 "Device connection test was cancelled");
    }
    const auto open = devices.openSession(result.deviceId);
    if (!open.ok()) {
        return finishWithCleanup(
            failureOutcome(open.error.message),
            open.error.errorCode,
            open.error.message,
            open.error.suggestion);
    }
    if (cancelled()) {
        return finishWithCleanup(DeviceConnectionTestOutcome::Cancelled,
                                 "ConnectionTestCancelled",
                                 "Device connection test was cancelled");
    }

    QString healthError;
    if (!open.session->isHealthy(healthError)) {
        return finishWithCleanup(
            failureOutcome(healthError),
            "DeviceHealthCheckFailed",
            healthError.isEmpty() ? QString("Device health check failed") : healthError,
            "Check the device connection and driver health implementation");
    }
    result.metadata = open.session->metadata();
    return finishWithCleanup(DeviceConnectionTestOutcome::Passed);
}

QVector<UiDiagnostic> CoreExecutionService::readSequenceJson(
    const QString& filePath,
    const QByteArray& jsonSnapshot,
    QJsonObject& object) const
{
    QVector<UiDiagnostic> diagnostics;
    const QFileInfo info(filePath);
    if (filePath.trimmed().isEmpty()) {
        diagnostics.push_back(error({}, "Sequence path is empty", "Select a sequence JSON file"));
        return diagnostics;
    }

    QByteArray json = jsonSnapshot;
    if (json.isEmpty()) {
        QFile file(info.absoluteFilePath());
        if (!file.open(QIODevice::ReadOnly)) {
            diagnostics.push_back(
                error({},
                      QString("Failed to open sequence file: %1").arg(info.absoluteFilePath()),
                      file.errorString()));
            return diagnostics;
        }
        json = file.readAll();
    }

    QJsonParseError parseError;
    const auto document = QJsonDocument::fromJson(json, &parseError);
    if (parseError.error != QJsonParseError::NoError) {
        diagnostics.push_back(
            error(QString("offset %1").arg(parseError.offset),
                  parseError.errorString(),
                  "Fix the JSON syntax and compile again"));
        return diagnostics;
    }
    if (!document.isObject()) {
        diagnostics.push_back(error({}, "Sequence JSON root must be an object"));
        return diagnostics;
    }

    object = document.object();
    return diagnostics;
}

QVector<UiDiagnostic> CoreExecutionService::readStationJson(
    const QString& filePath,
    const QByteArray& jsonSnapshot,
    QJsonObject& object) const
{
    QVector<UiDiagnostic> diagnostics;
    const QFileInfo info(filePath);
    QByteArray json = jsonSnapshot;
    if (json.isEmpty()) {
        QFile file(info.absoluteFilePath());
        if (!file.open(QIODevice::ReadOnly)) {
            diagnostics.push_back(
                error({},
                      QString("Failed to open station file: %1").arg(info.absoluteFilePath()),
                      file.errorString()));
            return diagnostics;
        }
        json = file.readAll();
    }

    QJsonParseError parseError;
    const auto document = QJsonDocument::fromJson(json, &parseError);
    if (parseError.error != QJsonParseError::NoError) {
        diagnostics.push_back(
            error(QString("offset %1").arg(parseError.offset),
                  parseError.errorString(),
                  "Fix the station JSON syntax and compile again"));
        return diagnostics;
    }
    if (!document.isObject()) {
        diagnostics.push_back(error({}, "Station JSON root must be an object"));
        return diagnostics;
    }

    object = document.object();
    return diagnostics;
}

PicoATE::Core::VariableResolverOptions CoreExecutionService::resolverOptions(
    const QString& filePath) const
{
    PicoATE::Core::VariableResolverOptions options;
    options.sequenceFilePath = QFileInfo(filePath).absoluteFilePath();
    options.projectDir = m_projectDir;
    return options;
}

QString CoreExecutionService::nativeHostProgram() const
{
    const auto applicationDirectory = QCoreApplication::applicationDirPath();
    const QStringList candidates = {
        QDir(applicationDirectory).absoluteFilePath(
            QStringLiteral("PicoATE.NativeHost.exe")),
        QDir(applicationDirectory).absoluteFilePath(
            QStringLiteral("../../../src/nativehost/Debug/PicoATE.NativeHost.exe")),
        QDir(m_projectDir).absoluteFilePath(
            QStringLiteral("out/build/vs2022-qt6-all/src/nativehost/Debug/PicoATE.NativeHost.exe")),
    };
    for (const auto& candidate : candidates) {
        if (QFileInfo::exists(candidate)) {
            return QFileInfo(candidate).absoluteFilePath();
        }
    }
    return {};
}

} // namespace PicoATE::Ui
