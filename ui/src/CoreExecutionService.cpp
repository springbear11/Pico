#include "CoreExecutionService.h"

#include "PicoATE/Core/ExecutionSession.h"
#include "PicoATE/Core/ModuleBindingRegistrar.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonParseError>

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
    result.diagnostics = readSequenceJson(request.sequencePath, sequenceObject);
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
        const auto stationResult = PicoATE::Core::loadStationConfigFile(
            request.stationPath,
            resolverOptions(request.stationPath));
        for (const auto& diagnostic : stationResult.errors) {
            result.diagnostics.push_back(
                error(diagnostic.path, diagnostic.message, diagnostic.suggestion));
        }
        if (!stationResult.ok()) {
            return result;
        }
        artifact.station = stationResult.config;
    }

    result.success = true;
    result.sequenceId = artifact.sequence.id;
    result.sequenceName = artifact.sequence.name;
    result.sequenceVersion = artifact.sequence.version;
    result.nodeCount = artifact.plan.nodes.size();
    m_compiled = std::move(artifact);
    return result;
}

RunServiceResult CoreExecutionService::run(
    const RunRequest& request,
    const std::shared_ptr<PicoATE::Core::StopToken>& stopToken,
    PicoATE::Core::IRuntimeEventSink* eventSink)
{
    RunServiceResult result;
    result.requestId = request.requestId;
    if (!m_compiled) {
        result.diagnostics.push_back(
            error({}, "No compiled sequence is available", "Compile the selected sequence before running"));
        return result;
    }
    if (request.uutCount <= 0) {
        result.diagnostics.push_back(
            error("uutCount", "UUT count must be greater than zero"));
        return result;
    }

    PicoATE::Core::ExecutionSession session(m_compiled->plan, stopToken, eventSink);
    if (m_compiled->station) {
        const auto stationErrors = PicoATE::Core::configureDeviceSessions(
            *m_compiled->station,
            session.devices());
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
        session,
        m_compiled->sequence,
        bindingOptions);
    for (const auto& diagnostic : bindingResult.errors) {
        result.diagnostics.push_back(
            error(diagnostic.moduleId, diagnostic.message, diagnostic.suggestion));
    }
    if (!bindingResult.ok()) {
        return result;
    }

    const QString prefix = request.uutPrefix.trimmed().isEmpty()
        ? QStringLiteral("UUT")
        : request.uutPrefix.trimmed();
    for (int index = 1; index <= request.uutCount; ++index) {
        session.addUut(QStringLiteral("%1-%2").arg(prefix).arg(index));
    }

    result.executed = true;
    session.run();
    result.report = session.report();
    result.stopRequested = stopToken && stopToken->isStopRequested();
    return result;
}

QVector<UiDiagnostic> CoreExecutionService::readSequenceJson(
    const QString& filePath,
    QJsonObject& object) const
{
    QVector<UiDiagnostic> diagnostics;
    const QFileInfo info(filePath);
    if (filePath.trimmed().isEmpty()) {
        diagnostics.push_back(error({}, "Sequence path is empty", "Select a sequence JSON file"));
        return diagnostics;
    }

    QFile file(info.absoluteFilePath());
    if (!file.open(QIODevice::ReadOnly)) {
        diagnostics.push_back(
            error({},
                  QString("Failed to open sequence file: %1").arg(info.absoluteFilePath()),
                  file.errorString()));
        return diagnostics;
    }

    QJsonParseError parseError;
    const auto document = QJsonDocument::fromJson(file.readAll(), &parseError);
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

PicoATE::Core::VariableResolverOptions CoreExecutionService::resolverOptions(
    const QString& filePath) const
{
    PicoATE::Core::VariableResolverOptions options;
    options.sequenceFilePath = QFileInfo(filePath).absoluteFilePath();
    options.projectDir = m_projectDir;
    return options;
}

} // namespace PicoATE::Ui
