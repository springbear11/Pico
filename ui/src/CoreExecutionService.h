#pragma once

#include "IExecutionService.h"

#include "PicoATE/Core/SequenceCompiler.h"
#include "PicoATE/Core/StationConfig.h"

#include <optional>

namespace PicoATE::Ui {

class CoreExecutionService final : public IExecutionService
{
public:
    explicit CoreExecutionService(QString projectDir = {});

    CompileServiceResult compile(const CompileRequest& request) override;
    RunServiceResult run(
        const RunRequest& request,
        const std::shared_ptr<PicoATE::Core::StopToken>& stopToken,
        PicoATE::Core::IRuntimeEventSink* eventSink = nullptr) override;

private:
    struct CompiledArtifact {
        QString sequencePath;
        PicoATE::Core::SequenceDef sequence;
        PicoATE::Core::ExecutionPlan plan;
        std::optional<PicoATE::Core::StationConfig> station;
    };

    QVector<UiDiagnostic> readSequenceJson(const QString& filePath,
                                           QJsonObject& object) const;
    PicoATE::Core::VariableResolverOptions resolverOptions(const QString& filePath) const;

    QString m_projectDir;
    std::optional<CompiledArtifact> m_compiled;
};

} // namespace PicoATE::Ui
