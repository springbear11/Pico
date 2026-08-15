#pragma once

#include "PicoATE/Core/BarrierController.h"
#include "PicoATE/Core/BarrierRuntimeCoordinator.h"
#include "PicoATE/Core/CleanupRuntimeCoordinator.h"
#include "PicoATE/Core/ErrorPolicyEngine.h"
#include "PicoATE/Core/ExecutionControl.h"
#include "PicoATE/Core/ExecutionResultStore.h"
#include "PicoATE/Core/LoopController.h"
#include "PicoATE/Core/NodeRunner.h"
#include "PicoATE/Core/OperatorPromptRuntimeCoordinator.h"
#include "PicoATE/Core/PeriodicTaskController.h"
#include "PicoATE/Core/ResourceManager.h"
#include "PicoATE/Core/ResourceRegionController.h"
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
                            StopToken* stopToken = nullptr);

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
    bool sessionCleanupRequested() const;
    QString sessionCleanupReason() const;

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
        const TimerCompletion& completion,
        std::optional<ExecutionPhase> phase = std::nullopt);
    bool scheduleRetryDelay(UutExecution& uut,
                            const ExecNode& node,
                            const FrameId& frameId);
    bool completeReadyRetry(UutExecution& uut,
                            const FrameId& frameId,
                            const TimerCompletion& completion,
                            std::optional<ExecutionPhase> phase = std::nullopt);
    bool cancelPendingWait(UutExecution& uut,
                           const ExecNode& node,
                           const FrameId& frameId,
                           const QString& reason);
    bool cancelPendingRetry(UutExecution& uut,
                            const ExecNode& node,
                            const FrameId& frameId,
                            const QString& reason);
    bool cancelPendingOperatorPrompt(UutExecution& uut,
                                     const ExecNode& node,
                                     const FrameId& frameId,
                                     const QString& reason);
    void discardObsoletePendingWaits(UutExecution& uut);
    void discardObsoletePendingRetries(UutExecution& uut);
    void discardObsoletePendingOperatorPrompts(UutExecution& uut);
    bool completePendingOperatorPrompt(
        UutExecution& uut,
        const FrameId& frameId,
        std::optional<ExecutionPhase> phase,
        SchedulerStepResult& step);
    NodeResult executeNode(UutExecution& uut, const ExecNode& node, const FrameId& frameId);
    NodeResult registerPeriodicTask(UutExecution& uut,
                                    const ExecNode& node,
                                    const FrameId& frameId);
    NodeResult executeBarrierNode(UutExecution& uut, const ExecNode& node, const FrameId& frameId);
    NodeResult executeLoopNode(UutExecution& uut, const ExecNode& node, const FrameId& frameId);
    bool isLoopBodyNode(const NodeId& nodeId) const;
    std::optional<ErrorAction> inheritedErrorAction(
        const ExecNode& node,
        NodeOutcome outcome) const;
    void requestSessionAbort();
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
    void handleLoopBodyFailure(UutExecution& uut,
                               const ExecNode& childNode,
                               const NodeResult& result,
                               ErrorAction action,
                               const FrameId& frameId);
    void skipNodeSubtree(UutExecution& uut,
                         const NodeId& rootNodeId,
                         const FrameId& frameId,
                         const QString& reason,
                         bool preserveAlwaysRun = false);
    bool shouldPreserveForAlwaysRun(const ExecNode& node) const;
    void resetTestItemForRetry(UutExecution& uut,
                               const ExecNode& testItemNode,
                               const FrameId& frameId);
    void closeOperatorPromptsForTestItemRetry(const UutExecution& uut,
                                              const NodeId& testItemNodeId);
    bool isNodeOrDescendantOf(const NodeId& nodeId, const NodeId& rootNodeId) const;
    bool finalizeBlockedCleanup(UutExecution& uut, const FrameId& frameId);
    void handleNodeFailureForBarriers(UutExecution& uut,
                                      const ExecNode& failedNode,
                                      const NodeResult& result,
                                      const FrameId& frameId);
    LoopIterationContext loopIterationForAttempt(const UutExecution& uut, const ExecNode& node) const;
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
    void closeOperatorPromptsForNode(const UutExecution& uut,
                                     const ExecNode& completedNode,
                                     const NodeResult& result);
    void publishOperatorPromptClosed(const UutId& uutId,
                                     const NodeId& sourceNodeId,
                                     const QString& instanceId,
                                     const QString& reason,
                                     const NodeId& closedByNodeId = {},
                                     NodeOutcome outcome = NodeOutcome::Passed,
                                     const QString& message = {});
    struct PendingWait {
        RequestId requestId;
        UutId uutId;
        FrameId frameId;
        NodeId nodeId;
        AttemptId attemptId;
        ResourceLeaseId leaseId;
    };

    struct PendingRetry {
        RequestId requestId;
        UutId uutId;
        FrameId frameId;
        NodeId nodeId;
        ActivationId activationId;
    };

    const ExecutionPlan& m_plan;
    ResourceManager& m_resources;
    ResourceRegionController m_resourceRegions;
    BarrierRuntimeCoordinator m_barrierRuntime;
    CleanupRuntimeCoordinator m_cleanupRuntime;
    LoopController& m_loops;
    ErrorPolicyEngine& m_errorPolicy;
    NodeRunner& m_runner;
    ExecutionResultStore& m_results;
    ExecutionControl* m_executionControl = nullptr;
    StopToken* m_stopToken = nullptr;
    RuntimeEventEmitter* m_events = nullptr;
    OperatorPromptRuntimeCoordinator m_operatorPromptRuntime;
    TimerService m_timers;
    QHash<RequestId, PendingWait> m_pendingWaits;
    QHash<RequestId, PendingRetry> m_pendingRetries;
    QHash<QString, ErrorAction> m_testItemFailureEscalations;
    QHash<QString, ErrorAction> m_loopFailureEscalations;
    PeriodicTaskController m_periodicTasks;
};

} // namespace PicoATE::Core
