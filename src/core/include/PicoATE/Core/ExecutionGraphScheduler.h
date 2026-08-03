#pragma once

#include "PicoATE/Core/BarrierController.h"
#include "PicoATE/Core/ErrorPolicyEngine.h"
#include "PicoATE/Core/ExecutionControl.h"
#include "PicoATE/Core/ExecutionResultStore.h"
#include "PicoATE/Core/LoopController.h"
#include "PicoATE/Core/NodeRunner.h"
#include "PicoATE/Core/PeriodicTaskController.h"
#include "PicoATE/Core/ResourceManager.h"
#include "PicoATE/Core/RuntimeEvent.h"
#include "PicoATE/Core/StopToken.h"
#include "PicoATE/Core/TimerService.h"

#include <chrono>

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
    NodeId nodeId;
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
                            ExecutionResultStore& results,
                            RuntimeEventEmitter* events = nullptr,
                            ExecutionControl* executionControl = nullptr,
                            const StopToken* stopToken = nullptr);

    SchedulerResult run(UutExecution& uut, const FrameId& frameId = "root");
    SchedulerStepResult pumpOnce(UutExecution& uut,
                                 const FrameId& frameId = "root",
                                 std::optional<ExecutionPhase> phase = std::nullopt);
    SchedulerStepResult pumpPendingRequestOnce(
        UutExecution& uut,
        const FrameId& frameId = "root",
        std::optional<ExecutionPhase> phase = std::nullopt);
    std::optional<NodeId> nextReadyNodeId(
        const UutExecution& uut,
        std::optional<ExecutionPhase> phase = std::nullopt) const;
    void setCohortUuts(const QSet<UutId>& uutIds);
    void releaseBarrierNodes(const BarrierReleaseDecision& decision);
    void applyBarrierReleases(const QVector<UutExecution*>& uuts);
    void activateAllCleanup(UutExecution& uut);
    void skipPendingNonAlwaysRun(UutExecution& uut,
                                 const FrameId& frameId = "root",
                                 std::optional<ExecutionPhase> phase = std::nullopt,
                                 const QString& reason = QStringLiteral("skipped after stop policy"),
                                 bool includeAlwaysRun = false);
    void closeAllOperatorPrompts(const QString& reason);
    void releaseAllResourceRegions(const UutId& uutId,
                                   const FrameId& frameId = "root");
    bool hasPendingRequests() const;
    bool hasPendingRequestForUut(const UutId& uutId) const;
    bool waitForPendingRequest(
        std::chrono::milliseconds maximumWait = std::chrono::milliseconds(20));
    SchedulerStepResult pumpPeriodicTaskOnce();
    bool stopAllPeriodicTasks();
    int activePeriodicTaskCount() const;

private:
    QVector<NodeId> findReadyNodes(
        const UutExecution& uut,
        std::optional<ExecutionPhase> phase = std::nullopt) const;
    bool dependenciesSatisfied(const UutExecution& uut,
                               const ExecNode& node,
                               std::optional<ExecutionPhase> phase = std::nullopt) const;
    NodeResult scheduleWaitNode(UutExecution& uut,
                                const ExecNode& node,
                                const FrameId& frameId,
                                const ResourceLeaseId& leaseId);
    std::optional<NodeResult> completeReadyWait(
        UutExecution& uut,
        const FrameId& frameId,
        std::optional<ExecutionPhase> phase = std::nullopt);
    bool cancelPendingWait(UutExecution& uut,
                           const ExecNode& node,
                           const FrameId& frameId,
                           const QString& reason);
    void discardObsoletePendingWaits(UutExecution& uut);
    NodeResult executeNode(UutExecution& uut, const ExecNode& node, const FrameId& frameId);
    NodeResult registerPeriodicTask(UutExecution& uut,
                                    const ExecNode& node,
                                    const FrameId& frameId);
    NodeResult executeBarrierNode(UutExecution& uut, const ExecNode& node, const FrameId& frameId);
    NodeResult executeLoopNode(UutExecution& uut, const ExecNode& node, const FrameId& frameId);
    bool isWhileLoopBodyNode(const NodeId& nodeId) const;
    void handleBreakRequest(UutExecution& uut,
                            const ExecNode& node,
                            const NodeResult& result,
                            const FrameId& frameId);
    void waitForLoopInterval(int intervalMs) const;
    NodeResult executeTestItemNode(UutExecution& uut, const ExecNode& node, const FrameId& frameId);
    bool testItemControllerReady(const TestItemRegion& region, const UutExecution& uut) const;
    bool testItemChildMayRun(const TestItemRegion& region, const UutExecution& uut) const;
    void handleTestItemChildFailure(UutExecution& uut,
                                    const ExecNode& childNode,
                                    const NodeResult& result,
                                    ErrorAction action,
                                    const FrameId& frameId);
    void skipNodeSubtree(UutExecution& uut,
                         const NodeId& rootNodeId,
                         const FrameId& frameId,
                         const QString& reason);
    void resetTestItemForRetry(UutExecution& uut,
                               const ExecNode& testItemNode,
                               const FrameId& frameId);
    void closeOperatorPromptsForTestItemRetry(const UutExecution& uut,
                                              const NodeId& testItemNodeId);
    bool isNodeOrDescendantOf(const NodeId& nodeId, const NodeId& rootNodeId) const;
    void activateCleanup(UutExecution& uut, const CleanupRegionId& cleanupRegionId);
    bool cleanupRegionContainsNode(const CleanupRegion& region,
                                   const NodeId& nodeId) const;
    bool cleanupRegionIsActive(const CleanupRegion& region,
                               const UutExecution& uut) const;
    bool bestEffortCleanupApplies(const UutExecution& uut,
                                  const NodeId& nodeId) const;
    bool bestEffortCleanupEdgeActive(const UutExecution& uut,
                                     const NodeId& from,
                                     const NodeId& to) const;
    bool finalizeBlockedCleanup(UutExecution& uut, const FrameId& frameId);
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
                          const LoopIterationContext& loopIteration = {},
                          const QString& errorCode = {});
    void publishAttemptEvent(RuntimeEventKind kind,
                             const UutExecution& uut,
                             const ExecNode& node,
                             const NodeAttempt& attempt,
                             const QString& message = {});
    NodeId operatorPromptCloseTarget(const ExecNode& node) const;
    void trackOperatorPrompt(const UutExecution& uut,
                             const ExecNode& node,
                             const NodeResult& result);
    void closeOperatorPromptsForNode(const UutExecution& uut,
                                     const ExecNode& completedNode,
                                     const NodeResult& result);
    void publishOperatorPromptClosed(const UutId& uutId,
                                     const NodeId& sourceNodeId,
                                     const QString& instanceId,
                                     const QString& reason,
                                     const NodeId& closedByNodeId = {});
    bool acquireResourceRegionForNode(UutExecution& uut,
                                      const ExecNode& node,
                                      const FrameId& frameId);
    void releaseCompletedResourceRegions(const UutExecution& uut,
                                         const FrameId& frameId);
    QSet<ResourceId> activeRegionResourceIds(const UutId& uutId,
                                             const FrameId& frameId) const;
    QString resourceRegionLeaseKey(const UutId& uutId,
                                   const FrameId& frameId,
                                   const ResourceRegionId& regionId) const;

    struct ActiveOperatorPrompt {
        QString instanceId;
        UutId uutId;
        NodeId sourceNodeId;
        NodeId closeTargetNodeId;
    };

    struct PendingWait {
        RequestId requestId;
        UutId uutId;
        FrameId frameId;
        NodeId nodeId;
        AttemptId attemptId;
        ResourceLeaseId leaseId;
    };

    const ExecutionPlan& m_plan;
    ResourceManager& m_resources;
    BarrierController& m_barriers;
    LoopController& m_loops;
    ErrorPolicyEngine& m_errorPolicy;
    NodeRunner& m_runner;
    ExecutionResultStore& m_results;
    ExecutionControl* m_executionControl = nullptr;
    const StopToken* m_stopToken = nullptr;
    RuntimeEventEmitter* m_events = nullptr;
    QSet<UutId> m_cohortUuts;
    QHash<BarrierInstanceId, BarrierReleaseDecision> m_releasedBarriers;
    QHash<NodeId, BarrierInstanceId> m_barrierByNode;
    QHash<BarrierInstanceId, NodeId> m_nodeByBarrier;
    QVector<ActiveOperatorPrompt> m_activeOperatorPrompts;
    TimerService m_timers;
    QHash<RequestId, PendingWait> m_pendingWaits;
    PeriodicTaskController m_periodicTasks;
    struct ActiveResourceRegion {
        ResourceRegionId regionId;
        UutId uutId;
        FrameId frameId;
        ResourceLease lease;
    };
    QHash<QString, ActiveResourceRegion> m_activeResourceRegions;
};

} // namespace PicoATE::Core
