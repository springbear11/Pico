#pragma once

#include "PicoATE/Core/BarrierController.h"
#include "PicoATE/Core/ErrorPolicyEngine.h"
#include "PicoATE/Core/LoopController.h"
#include "PicoATE/Core/NodeRunner.h"
#include "PicoATE/Core/ResourceManager.h"
#include "PicoATE/Core/RuntimeEvent.h"

namespace PicoATE::Core {

struct SchedulerResult {
    bool completed = false;
    bool hasError = false;
    QVector<NodeResult> nodeResults;
};

struct SchedulerStepResult {
    bool progressed = false;
    bool blocked = false;
    bool hasError = false;
    QVector<NodeResult> nodeResults;
};

class ExecutionGraphScheduler {
public:
    ExecutionGraphScheduler(const ExecutionPlan& plan,
                            ResourceManager& resources,
                            BarrierController& barriers,
                            LoopController& loops,
                            ErrorPolicyEngine& errorPolicy,
                            NodeRunner& runner,
                            RuntimeEventEmitter* events = nullptr);

    SchedulerResult run(UutExecution& uut, const FrameId& frameId = "root");
    SchedulerStepResult pumpOnce(UutExecution& uut, const FrameId& frameId = "root");
    void setCohortUuts(const QSet<UutId>& uutIds);
    void releaseBarrierNodes(const BarrierReleaseDecision& decision);
    void applyBarrierReleases(const QVector<UutExecution*>& uuts);
    void activateAllCleanup(UutExecution& uut);
    void skipPendingNonAlwaysRun(UutExecution& uut, const FrameId& frameId = "root");

private:
    QVector<NodeId> findReadyNodes(const UutExecution& uut) const;
    bool dependenciesSatisfied(const UutExecution& uut, const ExecNode& node) const;
    NodeResult executeNode(UutExecution& uut, const ExecNode& node, const FrameId& frameId);
    NodeResult executeBarrierNode(UutExecution& uut, const ExecNode& node, const FrameId& frameId);
    NodeResult executeLoopNode(UutExecution& uut, const ExecNode& node, const FrameId& frameId);
    NodeResult executeTestItemNode(UutExecution& uut, const ExecNode& node, const FrameId& frameId);
    bool testItemControllerReady(const TestItemRegion& region, const UutExecution& uut) const;
    bool testItemChildMayRun(const TestItemRegion& region, const UutExecution& uut) const;
    void activateCleanup(UutExecution& uut, const CleanupRegionId& cleanupRegionId);
    void handleNodeFailureForBarriers(UutExecution& uut,
                                      const ExecNode& failedNode,
                                      const NodeResult& result,
                                      const FrameId& frameId);
    bool hasPathToNode(const NodeId& from, const NodeId& to) const;
    LoopIterationContext loopIterationForAttempt(const UutExecution& uut, const ExecNode& node) const;
    BarrierNodePayload barrierPayloadFromNode(const ExecNode& node) const;
    BarrierInstanceId barrierInstanceForNode(const ExecNode& node, const UutId& uutId);
    void appendSyntheticAttempt(NodeActivation& activation, NodeOutcome outcome, const QString& message = {});
    void publishNodeEvent(RuntimeEventKind kind,
                          const UutExecution& uut,
                          const ExecNode& node,
                          ActivationState state,
                          NodeOutcome outcome = NodeOutcome::Unknown,
                          const QString& message = {},
                          const LoopIterationContext& loopIteration = {});
    void publishAttemptEvent(RuntimeEventKind kind,
                             const UutExecution& uut,
                             const ExecNode& node,
                             const NodeAttempt& attempt,
                             const QString& message = {});

    const ExecutionPlan& m_plan;
    ResourceManager& m_resources;
    BarrierController& m_barriers;
    LoopController& m_loops;
    ErrorPolicyEngine& m_errorPolicy;
    NodeRunner& m_runner;
    RuntimeEventEmitter* m_events = nullptr;
    QSet<UutId> m_cohortUuts;
    QHash<BarrierInstanceId, BarrierReleaseDecision> m_releasedBarriers;
    QHash<NodeId, BarrierInstanceId> m_barrierByNode;
    QHash<BarrierInstanceId, NodeId> m_nodeByBarrier;
};

} // namespace PicoATE::Core
