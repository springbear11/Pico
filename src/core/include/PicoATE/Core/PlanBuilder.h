#pragma once

#include "PicoATE/Core/SequenceDef.h"

namespace PicoATE::Core {

struct PlanBuildError {
    QString message;
    QString suggestion;
};

struct PlanBuildResult {
    QVector<PlanBuildError> errors;
    ExecutionPlan plan;

    bool ok() const { return errors.isEmpty(); }
};

class PlanBuilder {
public:
    PlanBuildResult build(const SequenceDef& sequence) const;

private:
    struct GroupBuildInfo {
        QVector<NodeId> nodeIds;
        StepGroupKind kind = StepGroupKind::Custom;
    };

    struct StepBuildInfo {
        NodeId controlNodeId;
        QVector<NodeId> allNodeIds;
    };

    bool validateSequence(const SequenceDef& sequence, PlanBuildResult& result) const;
    GroupBuildInfo buildGroup(const StepGroupDef& group,
                              const CleanupRegionId& cleanupRegionId,
                              ExecutionPlan& plan) const;
    StepBuildInfo buildStep(const StepDef& step,
                            const CleanupRegionId& cleanupRegionId,
                            StepGroupKind groupKind,
                            ExecutionPlan& plan) const;
    StepBuildInfo buildLoopStep(const StepDef& step,
                                const CleanupRegionId& cleanupRegionId,
                                StepGroupKind groupKind,
                                ExecutionPlan& plan) const;
    StepBuildInfo buildTestItemStep(const StepDef& step,
                                    const CleanupRegionId& cleanupRegionId,
                                    StepGroupKind groupKind,
                                    ExecutionPlan& plan) const;
    ExecNode buildNode(const StepDef& step,
                       const CleanupRegionId& cleanupRegionId,
                       StepGroupKind groupKind) const;
    void addSerialEdges(const QVector<NodeId>& nodeIds,
                        ExecutionPlan& plan,
                        const QString& edgePrefix,
                        EdgeTrigger trigger = EdgeTrigger::OnSuccess) const;
    void addGroupBridge(const GroupBuildInfo& from,
                        const GroupBuildInfo& to,
                        ExecutionPlan& plan,
                        const QString& edgePrefix,
                        EdgeKind kind = EdgeKind::Control,
                        EdgeTrigger trigger = EdgeTrigger::OnSuccess) const;
    void addCleanupRegion(const QVector<GroupBuildInfo>& cleanupGroups,
                          const CleanupRegionId& cleanupRegionId,
                          ExecutionPlan& plan) const;
    bool validatePlanReferences(const ExecutionPlan& plan, PlanBuildResult& result) const;
};

} // namespace PicoATE::Core
