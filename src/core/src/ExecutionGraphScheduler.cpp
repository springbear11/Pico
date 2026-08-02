#include "PicoATE/Core/ExecutionGraphScheduler.h"
#include <QThread>

#include <algorithm>
#include <utility>

namespace PicoATE::Core {

namespace {

class AttemptModuleLogSink final : public IModuleLogSink {
public:
    AttemptModuleLogSink(RuntimeEventEmitter* events,
                         const UutExecution& uut,
                         const ExecNode& node,
                         const std::optional<NodeId>& parentNodeId,
                         const NodeAttempt& attempt,
                         const FrameId& frameId)
        : m_events(events)
        , m_uutId(uut.uutId)
        , m_nodeId(node.id)
        , m_nodeLocalId(node.localId.isEmpty() ? node.id : node.localId)
        , m_parentNodeId(parentNodeId.value_or(NodeId{}))
        , m_nodeDisplayName(node.displayName)
        , m_nodeKind(node.kind)
        , m_attemptId(attempt.id)
        , m_requestId(attempt.requestId)
        , m_attemptIndex(attempt.attemptIndex + 1)
        , m_frameId(frameId)
    {
    }

    void publishModuleLog(const ModuleLogRecord& record) override
    {
        if (!m_events || record.message.isEmpty()) {
            return;
        }

        RuntimeEvent event;
        event.kind = RuntimeEventKind::ModuleLog;
        event.uutId = m_uutId;
        event.nodeId = m_nodeId;
        event.nodeLocalId = m_nodeLocalId;
        event.parentNodeId = m_parentNodeId;
        event.nodeDisplayName = m_nodeDisplayName;
        event.nodeKind = m_nodeKind;
        event.attemptId = m_attemptId;
        event.requestId = m_requestId;
        event.attemptIndex = m_attemptIndex;
        event.frameId = m_frameId;
        event.message = record.message;
        if (record.sourceSequence > 0) {
            event.details.insert("sourceSequence", record.sourceSequence);
        }
        if (record.droppedBefore > 0) {
            event.details.insert("droppedBefore", record.droppedBefore);
        }
        if (record.timestampUtc.isValid()) {
            event.details.insert("sourceTimestampUtc", record.timestampUtc);
        }
        m_events->publish(event);
    }

private:
    RuntimeEventEmitter* m_events = nullptr;
    UutId m_uutId;
    NodeId m_nodeId;
    NodeId m_nodeLocalId;
    NodeId m_parentNodeId;
    QString m_nodeDisplayName;
    ExecNodeKind m_nodeKind = ExecNodeKind::Action;
    AttemptId m_attemptId;
    RequestId m_requestId;
    int m_attemptIndex = 0;
    FrameId m_frameId;
};

} // namespace

ActivationState UutExecution::stateOf(const NodeId& nodeId) const
{
    auto it = activations.constFind(nodeId);
    if (it == activations.constEnd()) {
        return ActivationState::Created;
    }
    return it.value().state;
}

NodeOutcome UutExecution::outcomeOf(const NodeId& nodeId) const
{
    auto it = activations.constFind(nodeId);
    if (it == activations.constEnd() ||
        !isTerminalActivation(it.value().state) ||
        it.value().attempts.isEmpty()) {
        return NodeOutcome::Unknown;
    }
    return it.value().attempts.last().result.outcome;
}

NodeActivation& UutExecution::ensureActivation(const NodeId& nodeId, const FrameId& frameId)
{
    if (!activations.contains(nodeId)) {
        NodeActivation activation;
        activation.id = QString("%1:%2").arg(frameId, nodeId);
        activation.frameId = frameId;
        activation.nodeId = nodeId;
        activations.insert(nodeId, activation);
    }
    return activations[nodeId];
}

ActivationState outcomeToActivationState(NodeOutcome outcome)
{
    switch (outcome) {
    case NodeOutcome::Passed:
        return ActivationState::Passed;
    case NodeOutcome::Failed:
        return ActivationState::Failed;
    case NodeOutcome::Error:
        return ActivationState::Error;
    case NodeOutcome::Timeout:
        return ActivationState::Timeout;
    case NodeOutcome::Cancelled:
        return ActivationState::Cancelled;
    case NodeOutcome::Skipped:
        return ActivationState::Skipped;
    case NodeOutcome::Unknown:
        return ActivationState::Created;
    }
    return ActivationState::Created;
}

bool isTerminalActivation(ActivationState state)
{
    switch (state) {
    case ActivationState::Passed:
    case ActivationState::Failed:
    case ActivationState::Error:
    case ActivationState::Timeout:
    case ActivationState::Cancelled:
    case ActivationState::Skipped:
        return true;
    default:
        return false;
    }
}

ExecutionGraphScheduler::ExecutionGraphScheduler(const ExecutionPlan& plan,
                                                 ResourceManager& resources,
                                                 BarrierController& barriers,
                                                 LoopController& loops,
                                                 ErrorPolicyEngine& errorPolicy,
                                                 NodeRunner& runner,
                                                 ExecutionResultStore& results,
                                                 RuntimeEventEmitter* events,
                                                 ExecutionControl* executionControl,
                                                 const StopToken* stopToken)
    : m_plan(plan)
    , m_resources(resources)
    , m_barriers(barriers)
    , m_loops(loops)
    , m_errorPolicy(errorPolicy)
    , m_runner(runner)
    , m_results(results)
    , m_executionControl(executionControl)
    , m_stopToken(stopToken)
    , m_events(events)
{
}

bool ExecutionGraphScheduler::hasPendingRequests() const
{
    return m_timers.hasPendingRequests();
}

bool ExecutionGraphScheduler::hasPendingRequestForUut(const UutId& uutId) const
{
    return m_timers.hasPendingRequestForUut(uutId);
}

bool ExecutionGraphScheduler::waitForPendingRequest(std::chrono::milliseconds maximumWait)
{
    return m_timers.waitForNextDeadline(maximumWait);
}

SchedulerResult ExecutionGraphScheduler::run(UutExecution& uut, const FrameId& frameId)
{
    SchedulerResult schedulerResult;
    while (true) {
        auto step = pumpOnce(uut, frameId);
        schedulerResult.nodeResults += step.nodeResults;
        if (step.hasError) {
            schedulerResult.hasError = true;
        }
        if (step.progressed) {
            continue;
        }
        if (hasPendingRequestForUut(uut.uutId)) {
            waitForPendingRequest();
            continue;
        }
        break;
    }

    schedulerResult.completed = true;
    for (auto it = m_plan.nodes.constBegin(); it != m_plan.nodes.constEnd(); ++it) {
        if (it.value().kind == ExecNodeKind::Cleanup &&
            !uut.activations.contains(it.key()) &&
            m_plan.incomingEdges(it.key()).isEmpty()) {
            continue;
        }
        const auto state = uut.stateOf(it.key());
        if (!isTerminalActivation(state)) {
            schedulerResult.completed = false;
            break;
        }
    }

    return schedulerResult;
}

SchedulerStepResult ExecutionGraphScheduler::pumpOnce(
    UutExecution& uut,
    const FrameId& frameId,
    std::optional<ExecutionPhase> phase)
{
    SchedulerStepResult step;
    const auto pendingStep = pumpPendingRequestOnce(uut, frameId, phase);
    if (pendingStep.progressed) {
        return pendingStep;
    }

    const auto readyNodes = findReadyNodes(uut, phase);
    if (readyNodes.isEmpty()) {
        if ((!phase || *phase == ExecutionPhase::Cleanup) &&
            finalizeBlockedCleanup(uut, frameId)) {
            step.progressed = true;
            return step;
        }
        step.blocked = true;
        return step;
    }

    const auto& nodeId = readyNodes.first();
    const ExecNode* node = m_plan.node(nodeId);
    if (!node) {
        step.blocked = true;
        return step;
    }
    step.nodeId = nodeId;

    const auto previousState = uut.stateOf(nodeId);
    NodeResult result;
    if (acquireResourceRegionForNode(uut, *node, frameId)) {
        result = executeNode(uut, *node, frameId);
    } else {
        result.nodeId = nodeId;
        result.outcome = NodeOutcome::Unknown;
    }
    if (node->kind == ExecNodeKind::OperatorPrompt &&
        result.outcome == NodeOutcome::Passed &&
        result.outputs.value("mode").toString() == "notice") {
        trackOperatorPrompt(uut, *node, result);
    }
    closeOperatorPromptsForNode(uut, *node, result);
    if (result.outcome != NodeOutcome::Unknown) {
        step.nodeResults.push_back(result);
        if (node->kind == ExecNodeKind::Loop ||
            node->kind == ExecNodeKind::TestItem ||
            node->kind == ExecNodeKind::Barrier) {
            const auto activation = uut.activations.constFind(nodeId);
            const int attemptIndex = activation == uut.activations.constEnd() ||
                                             activation->attempts.isEmpty()
                ? 0
                : activation->attempts.last().attemptIndex;
            m_results.commit(uut.uutId, frameId, nodeId, attemptIndex, result);
        }
    }

    const auto currentState = uut.stateOf(nodeId);
    discardObsoletePendingWaits(uut);
    releaseCompletedResourceRegions(uut, frameId);
    step.progressed = previousState != currentState || result.outcome != NodeOutcome::Unknown;
    step.blocked = !step.progressed;
    step.hasError = !m_plan.isInsideTestItem(nodeId) &&
                    !isWhileLoopBodyNode(nodeId) &&
                    (result.outcome == NodeOutcome::Failed ||
                     result.outcome == NodeOutcome::Error ||
                     result.outcome == NodeOutcome::Timeout);
    return step;
}

SchedulerStepResult ExecutionGraphScheduler::pumpPendingRequestOnce(
    UutExecution& uut,
    const FrameId& frameId,
    std::optional<ExecutionPhase> phase)
{
    SchedulerStepResult step;
    discardObsoletePendingWaits(uut);
    if (auto completedWait = completeReadyWait(uut, frameId, phase)) {
        step.progressed = true;
        step.nodeId = completedWait->nodeId;
        step.nodeResults.push_back(*completedWait);
        releaseCompletedResourceRegions(uut, frameId);
        return step;
    }

    step.blocked = true;
    return step;
}

std::optional<NodeId> ExecutionGraphScheduler::nextReadyNodeId(
    const UutExecution& uut,
    std::optional<ExecutionPhase> phase) const
{
    const auto readyNodes = findReadyNodes(uut, phase);
    if (readyNodes.isEmpty()) {
        return std::nullopt;
    }
    return readyNodes.first();
}

void ExecutionGraphScheduler::setCohortUuts(const QSet<UutId>& uutIds)
{
    m_cohortUuts = uutIds;
}

void ExecutionGraphScheduler::releaseBarrierNodes(const BarrierReleaseDecision& decision)
{
    m_releasedBarriers.insert(decision.barrierId, decision);
}

void ExecutionGraphScheduler::applyBarrierReleases(const QVector<UutExecution*>& uuts)
{
    QVector<BarrierInstanceId> applied;
    for (auto it = m_releasedBarriers.constBegin(); it != m_releasedBarriers.constEnd(); ++it) {
        const auto nodeIt = m_nodeByBarrier.constFind(it.key());
        if (nodeIt == m_nodeByBarrier.constEnd()) {
            continue;
        }

        const auto& nodeId = nodeIt.value();
        for (auto* uut : uuts) {
            if (!uut || !it.value().releasedUuts.contains(uut->uutId)) {
                continue;
            }

            auto& activation = uut->ensureActivation(nodeId, "root");
            if (isTerminalActivation(activation.state)) {
                continue;
            }
            appendSyntheticAttempt(activation, NodeOutcome::Passed, "barrier released");
            activation.state = ActivationState::Passed;
            activation.completedAt = QDateTime::currentDateTimeUtc();
            if (const auto* node = m_plan.node(nodeId)) {
                publishNodeEvent(RuntimeEventKind::BarrierReleased,
                                 *uut,
                                 *node,
                                 activation.state,
                                 NodeOutcome::Passed,
                                 "barrier released");
            }
        }
        applied.push_back(it.key());
    }

    for (const auto& barrierId : applied) {
        m_releasedBarriers.remove(barrierId);
    }
}

void ExecutionGraphScheduler::activateAllCleanup(UutExecution& uut)
{
    for (const auto& region : m_plan.cleanupRegions) {
        activateCleanup(uut, region.id);
    }
}

void ExecutionGraphScheduler::skipPendingNonAlwaysRun(
    UutExecution& uut,
    const FrameId& frameId,
    std::optional<ExecutionPhase> phase,
    const QString& reason,
    bool includeAlwaysRun)
{
    for (auto it = m_plan.nodes.constBegin(); it != m_plan.nodes.constEnd(); ++it) {
        const auto& node = it.value();
        if (phase && executionPhaseOf(node) != *phase) {
            continue;
        }
        if (node.kind == ExecNodeKind::Cleanup ||
            (node.alwaysRun && !includeAlwaysRun)) {
            continue;
        }

        auto& activation = uut.ensureActivation(node.id, frameId);
        if (isTerminalActivation(activation.state)) {
            continue;
        }

        if (cancelPendingWait(uut, node, frameId, reason)) {
            continue;
        }

        appendSyntheticAttempt(activation, NodeOutcome::Skipped, reason);
        activation.state = ActivationState::Skipped;
        activation.completedAt = QDateTime::currentDateTimeUtc();
        publishNodeEvent(RuntimeEventKind::NodeStateChanged,
                         uut,
                         node,
                         activation.state,
                         NodeOutcome::Skipped,
                         reason);
    }
    releaseCompletedResourceRegions(uut, frameId);
}

QVector<NodeId> ExecutionGraphScheduler::findReadyNodes(
    const UutExecution& uut,
    std::optional<ExecutionPhase> phase) const
{
    QVector<NodeId> ready;
    for (auto it = m_plan.nodes.constBegin(); it != m_plan.nodes.constEnd(); ++it) {
        const auto& node = it.value();
        if (phase && executionPhaseOf(node) != *phase) {
            continue;
        }
        const auto state = uut.stateOf(node.id);
        if (isTerminalActivation(state) ||
            state == ActivationState::Running ||
            state == ActivationState::WaitingForTimer ||
            state == ActivationState::WaitingAtBarrier) {
            continue;
        }
        if (node.kind == ExecNodeKind::Cleanup &&
            !uut.activations.contains(node.id) &&
            m_plan.incomingEdges(node.id).isEmpty() &&
            !m_plan.isInsideTestItem(node.id)) {
            continue;
        }
        const auto bodyRegion = m_plan.loopRegionForBodyNode(node.id);
        if (bodyRegion && !m_loops.bodyNodeMayRun(*bodyRegion, uut, node.id)) {
            continue;
        }
        const auto testItemBody = m_plan.testItemRegionForChild(node.id);
        if (testItemBody && !testItemChildMayRun(*testItemBody, uut)) {
            continue;
        }
        const auto loopRegion = m_plan.loopRegionForController(node.id);
        if (loopRegion &&
            (!m_loops.controllerReady(*loopRegion, uut) ||
             !dependenciesSatisfied(uut, node, phase))) {
            continue;
        }
        const auto testItemRegion = m_plan.testItemRegionForController(node.id);
        if (testItemRegion &&
            (!testItemControllerReady(*testItemRegion, uut) ||
             !dependenciesSatisfied(uut, node, phase))) {
            continue;
        }
        if (dependenciesSatisfied(uut, node, phase)) {
            ready.push_back(node.id);
        }
    }
    return ready;
}

bool ExecutionGraphScheduler::dependenciesSatisfied(
    const UutExecution& uut,
    const ExecNode& node,
    std::optional<ExecutionPhase> phase) const
{
    auto activationIt = uut.activations.constFind(node.id);
    if (node.alwaysRun &&
        activationIt != uut.activations.constEnd() &&
        activationIt.value().frameId == "cleanup") {
        return true;
    }

    const auto incoming = m_plan.incomingEdges(node.id);
    if (incoming.isEmpty()) {
        return true;
    }

    for (const auto& edge : incoming) {
        if (phase) {
            const auto* sourceNode = m_plan.node(edge.from);
            if (sourceNode && executionPhaseOf(*sourceNode) != *phase) {
                continue;
            }
        }
        if (!isTerminalActivation(uut.stateOf(edge.from))) {
            return false;
        }

        if (bestEffortCleanupEdgeActive(uut, edge.from, node.id)) {
            continue;
        }

        const auto sourceOutcome = uut.outcomeOf(edge.from);
        if (triggerMatchesOutcome(edge.trigger, sourceOutcome)) {
            continue;
        }

        const bool mayContinueFailure =
            edge.trigger == EdgeTrigger::OnSuccess &&
            (sourceOutcome == NodeOutcome::Failed ||
             sourceOutcome == NodeOutcome::Error ||
             sourceOutcome == NodeOutcome::Timeout);
        if (!mayContinueFailure) {
            return false;
        }

        const auto* sourceNode = m_plan.node(edge.from);
        const auto activation = uut.activations.constFind(edge.from);
        if (!sourceNode || activation == uut.activations.constEnd() ||
            activation->attempts.isEmpty()) {
            return false;
        }
        const auto decision = m_errorPolicy.decide(
            *sourceNode,
            activation->attempts.last().result,
            activation->attempts.size());
        if (decision.action != ErrorAction::Continue) {
            return false;
        }
    }
    return true;
}

NodeResult ExecutionGraphScheduler::scheduleWaitNode(UutExecution& uut,
                                                     const ExecNode& node,
                                                     const FrameId& frameId,
                                                     const ResourceLeaseId& leaseId)
{
    auto& activation = uut.ensureActivation(node.id, frameId);
    NodeAttempt attempt;
    attempt.id = QString("%1:attempt-%2").arg(activation.id).arg(activation.attempts.size());
    attempt.requestId = createRequestId(QStringLiteral("wait"));
    attempt.activationId = activation.id;
    attempt.attemptIndex = activation.attempts.size();
    attempt.loopIteration = loopIterationForAttempt(uut, node);
    attempt.state = AttemptState::Running;
    attempt.leaseId = leaseId;
    attempt.result.nodeId = node.id;
    attempt.result.startedAt = QDateTime::currentDateTimeUtc();
    activation.attempts.push_back(attempt);

    TimerRequest timer;
    timer.requestId = attempt.requestId;
    timer.uutId = uut.uutId;
    timer.frameId = frameId;
    timer.nodeId = node.id;
    timer.activationId = activation.id;
    timer.attemptId = attempt.id;
    timer.durationMs = std::max(0, node.payload.value(QStringLiteral("ms"), 0).toInt());
    timer.startedAt = attempt.result.startedAt;

    PendingWait pending;
    pending.requestId = timer.requestId;
    pending.uutId = timer.uutId;
    pending.frameId = timer.frameId;
    pending.nodeId = timer.nodeId;
    pending.attemptId = timer.attemptId;
    pending.leaseId = leaseId;

    if (!m_timers.schedule(timer)) {
        auto& storedAttempt = activation.attempts.last();
        storedAttempt.state = AttemptState::Completed;
        storedAttempt.result.outcome = NodeOutcome::Error;
        storedAttempt.result.errorCode = QStringLiteral("TimerScheduleFailed");
        storedAttempt.result.errorMessage = QStringLiteral("Wait timer could not be scheduled");
        storedAttempt.result.finishedAt = QDateTime::currentDateTimeUtc();
        activation.state = ActivationState::Error;
        activation.completedAt = storedAttempt.result.finishedAt;
        m_results.commit(uut.uutId,
                         frameId,
                         node.id,
                         storedAttempt.attemptIndex,
                         storedAttempt.result);
        publishAttemptEvent(RuntimeEventKind::AttemptCompleted,
                            uut,
                            node,
                            storedAttempt,
                            storedAttempt.result.errorMessage);
        publishNodeEvent(RuntimeEventKind::NodeStateChanged,
                         uut,
                         node,
                         activation.state,
                         storedAttempt.result.outcome,
                         storedAttempt.result.errorMessage,
                         storedAttempt.loopIteration,
                         storedAttempt.result.errorCode);
        if (!leaseId.isEmpty()) {
            m_resources.release(leaseId);
        }
        return storedAttempt.result;
    }

    m_pendingWaits.insert(timer.requestId, pending);
    publishAttemptEvent(RuntimeEventKind::AttemptStarted,
                        uut,
                        node,
                        activation.attempts.last());
    activation.state = ActivationState::WaitingForTimer;
    publishNodeEvent(RuntimeEventKind::NodeStateChanged,
                     uut,
                     node,
                     activation.state,
                     NodeOutcome::Unknown,
                     QStringLiteral("waiting for timer (%1 ms)").arg(timer.durationMs),
                     attempt.loopIteration);

    NodeResult waiting;
    waiting.nodeId = node.id;
    waiting.outcome = NodeOutcome::Unknown;
    return waiting;
}

std::optional<NodeResult> ExecutionGraphScheduler::completeReadyWait(
    UutExecution& uut,
    const FrameId& frameId,
    std::optional<ExecutionPhase> phase)
{
    const auto completion = m_timers.takeReadyForContext(uut.uutId, frameId);
    if (!completion) {
        return std::nullopt;
    }

    auto pendingIt = m_pendingWaits.find(completion->requestId);
    if (pendingIt == m_pendingWaits.end()) {
        return std::nullopt;
    }
    const auto pending = pendingIt.value();
    m_pendingWaits.erase(pendingIt);

    const auto* node = m_plan.node(pending.nodeId);
    if (!node || (phase && executionPhaseOf(*node) != *phase)) {
        if (!pending.leaseId.isEmpty()) {
            m_resources.release(pending.leaseId);
        }
        return std::nullopt;
    }

    auto activationIt = uut.activations.find(pending.nodeId);
    if (activationIt == uut.activations.end() ||
        activationIt->state != ActivationState::WaitingForTimer ||
        activationIt->id != completion->activationId) {
        if (!pending.leaseId.isEmpty()) {
            m_resources.release(pending.leaseId);
        }
        return std::nullopt;
    }

    auto attemptIt = std::find_if(
        activationIt->attempts.begin(),
        activationIt->attempts.end(),
        [&completion](const NodeAttempt& attempt) {
            return attempt.id == completion->attemptId &&
                   attempt.requestId == completion->requestId;
        });
    if (attemptIt == activationIt->attempts.end() ||
        attemptIt->state != AttemptState::Running) {
        if (!pending.leaseId.isEmpty()) {
            m_resources.release(pending.leaseId);
        }
        return std::nullopt;
    }

    NodeResult result;
    result.nodeId = node->id;
    result.outcome = NodeOutcome::Passed;
    result.startedAt = attemptIt->result.startedAt;
    result.finishedAt = completion->finishedAt;

    attemptIt->state = AttemptState::Completed;
    attemptIt->result = result;
    activationIt->state = ActivationState::Passed;
    activationIt->completedAt = result.finishedAt;
    m_results.commit(uut.uutId, frameId, node->id, attemptIt->attemptIndex, result);
    publishAttemptEvent(RuntimeEventKind::AttemptCompleted, uut, *node, *attemptIt);
    publishNodeEvent(RuntimeEventKind::NodeStateChanged,
                     uut,
                     *node,
                     activationIt->state,
                     result.outcome,
                     {},
                     attemptIt->loopIteration);
    closeOperatorPromptsForNode(uut, *node, result);

    if (!pending.leaseId.isEmpty()) {
        m_resources.release(pending.leaseId);
    }
    return result;
}

bool ExecutionGraphScheduler::cancelPendingWait(UutExecution& uut,
                                                const ExecNode& node,
                                                const FrameId& frameId,
                                                const QString& reason)
{
    auto pendingIt = std::find_if(
        m_pendingWaits.begin(),
        m_pendingWaits.end(),
        [&uut, &node, &frameId](const PendingWait& pending) {
            return pending.uutId == uut.uutId && pending.nodeId == node.id &&
                   pending.frameId == frameId;
        });
    if (pendingIt == m_pendingWaits.end()) {
        return false;
    }

    const auto pending = pendingIt.value();
    m_pendingWaits.erase(pendingIt);
    m_timers.cancel(pending.requestId);

    auto activationIt = uut.activations.find(node.id);
    if (activationIt == uut.activations.end()) {
        if (!pending.leaseId.isEmpty()) {
            m_resources.release(pending.leaseId);
        }
        return true;
    }

    auto attemptIt = std::find_if(
        activationIt->attempts.begin(),
        activationIt->attempts.end(),
        [&pending](const NodeAttempt& attempt) {
            return attempt.requestId == pending.requestId;
        });
    if (attemptIt != activationIt->attempts.end()) {
        attemptIt->state = AttemptState::Cancelled;
        attemptIt->result.nodeId = node.id;
        attemptIt->result.outcome = NodeOutcome::Skipped;
        attemptIt->result.errorMessage = reason;
        attemptIt->result.finishedAt = QDateTime::currentDateTimeUtc();
        m_results.commit(uut.uutId,
                         frameId,
                         node.id,
                         attemptIt->attemptIndex,
                         attemptIt->result);
        publishAttemptEvent(RuntimeEventKind::AttemptCompleted,
                            uut,
                            node,
                            *attemptIt,
                            reason);
    }

    activationIt->state = ActivationState::Skipped;
    activationIt->completedAt = QDateTime::currentDateTimeUtc();
    publishNodeEvent(RuntimeEventKind::NodeStateChanged,
                     uut,
                     node,
                     activationIt->state,
                     NodeOutcome::Skipped,
                     reason,
                     attemptIt == activationIt->attempts.end()
                         ? LoopIterationContext{}
                         : attemptIt->loopIteration);
    if (!pending.leaseId.isEmpty()) {
        m_resources.release(pending.leaseId);
    }
    return true;
}

void ExecutionGraphScheduler::discardObsoletePendingWaits(UutExecution& uut)
{
    QVector<RequestId> obsolete;
    for (auto it = m_pendingWaits.cbegin(); it != m_pendingWaits.cend(); ++it) {
        if (it->uutId != uut.uutId) {
            continue;
        }
        const auto activation = uut.activations.constFind(it->nodeId);
        if (activation == uut.activations.constEnd() ||
            activation->state != ActivationState::WaitingForTimer) {
            obsolete.push_back(it.key());
        }
    }

    for (const auto& requestId : obsolete) {
        const auto pending = m_pendingWaits.take(requestId);
        m_timers.cancel(requestId);
        if (!pending.leaseId.isEmpty()) {
            m_resources.release(pending.leaseId);
        }
    }
}

NodeResult ExecutionGraphScheduler::executeNode(UutExecution& uut,
                                                const ExecNode& node,
                                                const FrameId& frameId)
{
    if (node.kind == ExecNodeKind::Loop) {
        return executeLoopNode(uut, node, frameId);
    }
    if (node.kind == ExecNodeKind::TestItem) {
        return executeTestItemNode(uut, node, frameId);
    }

    const bool isTestItemChild = m_plan.isInsideTestItem(node.id);
    const bool isWhileLoopChild = isWhileLoopBodyNode(node.id);

    auto& activation = uut.ensureActivation(node.id, frameId);
    activation.state = ActivationState::Running;
    publishNodeEvent(RuntimeEventKind::NodeStateChanged,
                     uut,
                     node,
                     activation.state);

    if (node.kind == ExecNodeKind::Barrier) {
        return executeBarrierNode(uut, node, frameId);
    }

    ResourceLease lease;
    bool hasLease = false;
    QVector<ResourceRequirement> nodeRequirements;
    const auto regionResourceIds = activeRegionResourceIds(uut.uutId, frameId);
    for (const auto& requirement : node.resources) {
        if (!regionResourceIds.contains(requirement.resourceId)) {
            nodeRequirements.push_back(requirement);
        }
    }
    if (!nodeRequirements.isEmpty()) {
        ResourceRequest request;
        request.requestId = QString("%1:%2").arg(uut.uutId, node.id);
        request.uutId = uut.uutId;
        request.frameId = frameId;
        request.nodeId = node.id;
        request.requirements = nodeRequirements;

        auto maybeLease = m_resources.tryAcquire(request);
        if (!maybeLease) {
            activation.state = ActivationState::WaitingForResource;
            publishNodeEvent(RuntimeEventKind::NodeStateChanged,
                             uut,
                             node,
                             activation.state,
                             NodeOutcome::Unknown,
                             "waiting for resource");
            NodeResult waiting;
            waiting.nodeId = node.id;
            waiting.outcome = NodeOutcome::Unknown;
            return waiting;
        }
        lease = *maybeLease;
        hasLease = true;
    }

    if (node.kind == ExecNodeKind::Wait) {
        return scheduleWaitNode(uut, node, frameId, hasLease ? lease.leaseId : ResourceLeaseId{});
    }

    NodeResult result;
    ErrorDecision finalDecision;
    bool shouldRetry = true;
    const auto loopIteration = loopIterationForAttempt(uut, node);
    while (shouldRetry) {
        NodeAttempt attempt;
        attempt.id = QString("%1:attempt-%2").arg(activation.id).arg(activation.attempts.size());
        attempt.requestId = createRequestId(QStringLiteral("node"));
        attempt.activationId = activation.id;
        attempt.attemptIndex = activation.attempts.size();
        attempt.loopIteration = loopIteration;
        attempt.state = AttemptState::Running;
        if (hasLease) {
            attempt.leaseId = lease.leaseId;
        }

        NodeExecutionContext context;
        context.uutId = uut.uutId;
        context.frameId = frameId;
        context.attemptId = attempt.id;
        context.requestId = attempt.requestId;
        context.currentNodeId = node.id;
        context.attemptIndex = attempt.attemptIndex;
        context.variables = uut.variables;
        context.resultStore = &m_results;
        context.executionControl = m_executionControl;
        context.stopToken = m_stopToken;
        context.runtimeEvents = m_events;
        AttemptModuleLogSink moduleLogSink(
            m_events, uut, node, m_plan.structuralParentOf(node.id), attempt, frameId);
        context.logSink = m_events ? &moduleLogSink : nullptr;

        publishAttemptEvent(RuntimeEventKind::AttemptStarted, uut, node, attempt);
        result = m_runner.run(node, context);
        attempt.state = AttemptState::Completed;
        attempt.result = result;
        activation.attempts.push_back(attempt);
        m_results.commit(uut.uutId,
                         frameId,
                         node.id,
                         activation.attempts.last().attemptIndex,
                         result);
        publishAttemptEvent(RuntimeEventKind::AttemptCompleted,
                            uut,
                            node,
                            activation.attempts.last(),
                            result.errorMessage);

        const auto decision = m_errorPolicy.decide(
            node,
            result,
            activation.attempts.size() - activation.retryAttemptBase);
        finalDecision = decision;
        shouldRetry = decision.action == ErrorAction::Retry;
        if (shouldRetry) {
            publishAttemptEvent(RuntimeEventKind::RetryScheduled,
                                uut,
                                node,
                                activation.attempts.last(),
                                decision.reason);
        }
        if (decision.action == ErrorAction::RunCleanup &&
            !isTestItemChild && !isWhileLoopChild) {
            activateCleanup(uut, decision.cleanupRegionId);
        }
    }

    if (result.outcome != NodeOutcome::Passed &&
        result.outcome != NodeOutcome::Skipped &&
        result.outcome != NodeOutcome::Unknown &&
        bestEffortCleanupApplies(uut, node.id)) {
        finalDecision.action = ErrorAction::Continue;
        finalDecision.reason = QStringLiteral("best-effort cleanup continues after error");
    }

    activation.state = outcomeToActivationState(result.outcome);
    activation.completedAt = QDateTime::currentDateTimeUtc();
    publishNodeEvent(RuntimeEventKind::NodeStateChanged,
                     uut,
                     node,
                     activation.state,
                     result.outcome,
                     result.errorMessage,
                     loopIteration);

    handleBreakRequest(uut, node, result, frameId);

    if (hasLease) {
        m_resources.release(lease.leaseId);
    }

    if (result.outcome != NodeOutcome::Passed &&
        result.outcome != NodeOutcome::Skipped &&
        result.outcome != NodeOutcome::Unknown) {
        if (isTestItemChild) {
            handleTestItemChildFailure(uut,
                                       node,
                                       result,
                                       finalDecision.action,
                                       frameId);
        } else if (!isWhileLoopChild) {
            handleNodeFailureForBarriers(uut, node, result, frameId);
            if (finalDecision.action == ErrorAction::StopUut ||
                finalDecision.action == ErrorAction::RunCleanup ||
                finalDecision.action == ErrorAction::Abort) {
                skipPendingNonAlwaysRun(uut, frameId, executionPhaseOf(node));
            }
        }
    }

    return result;
}

QString ExecutionGraphScheduler::resourceRegionLeaseKey(
    const UutId& uutId,
    const FrameId& frameId,
    const ResourceRegionId& regionId) const
{
    return QString("%1|%2|%3").arg(uutId, frameId, regionId);
}

bool ExecutionGraphScheduler::acquireResourceRegionForNode(
    UutExecution& uut,
    const ExecNode& node,
    const FrameId& frameId)
{
    const auto region = m_plan.resourceRegionStartingAt(node.id);
    if (!region) {
        return true;
    }
    const auto key = resourceRegionLeaseKey(uut.uutId, frameId, region->id);
    if (m_activeResourceRegions.contains(key)) {
        return true;
    }

    ResourceRequest request;
    request.requestId = QString("resource-region:%1:%2:%3")
                            .arg(uut.uutId, frameId, region->id);
    request.uutId = uut.uutId;
    request.frameId = frameId;
    request.nodeId = QString("resource-region:%1").arg(region->id);
    request.requirements = region->requirements;
    auto lease = m_resources.tryAcquire(request);
    if (!lease) {
        auto& activation = uut.ensureActivation(node.id, frameId);
        activation.state = ActivationState::WaitingForResource;
        publishNodeEvent(RuntimeEventKind::NodeStateChanged,
                         uut,
                         node,
                         activation.state,
                         NodeOutcome::Unknown,
                         QString("waiting for resource region %1").arg(region->id));
        return false;
    }

    m_activeResourceRegions.insert(
        key, ActiveResourceRegion{region->id, uut.uutId, frameId, *lease});
    return true;
}

void ExecutionGraphScheduler::releaseCompletedResourceRegions(
    const UutExecution& uut,
    const FrameId& frameId)
{
    QVector<QString> completedKeys;
    for (auto it = m_activeResourceRegions.constBegin();
         it != m_activeResourceRegions.constEnd(); ++it) {
        const auto& active = it.value();
        if (active.uutId != uut.uutId || active.frameId != frameId) {
            continue;
        }
        const auto ending = std::find_if(
            m_plan.resourceRegions.cbegin(),
            m_plan.resourceRegions.cend(),
            [&active](const ResourceRegion& region) {
                return region.id == active.regionId;
            });
        if (ending != m_plan.resourceRegions.cend() &&
            isTerminalActivation(uut.stateOf(ending->exitNodeId))) {
            completedKeys.push_back(it.key());
        }
    }
    for (const auto& key : completedKeys) {
        const auto active = m_activeResourceRegions.take(key);
        m_resources.release(active.lease.leaseId);
        m_resources.cancelRequest(active.lease.requestId);
    }
}

QSet<ResourceId> ExecutionGraphScheduler::activeRegionResourceIds(
    const UutId& uutId,
    const FrameId& frameId) const
{
    QSet<ResourceId> resourceIds;
    for (const auto& active : m_activeResourceRegions) {
        if (active.uutId != uutId || active.frameId != frameId) {
            continue;
        }
        for (const auto& requirement : active.lease.requirements) {
            resourceIds.insert(requirement.resourceId);
        }
    }
    return resourceIds;
}

void ExecutionGraphScheduler::releaseAllResourceRegions(
    const UutId& uutId,
    const FrameId& frameId)
{
    QVector<QString> keys;
    for (auto it = m_activeResourceRegions.constBegin();
         it != m_activeResourceRegions.constEnd(); ++it) {
        if (it->uutId == uutId && it->frameId == frameId) {
            keys.push_back(it.key());
        }
    }
    for (const auto& key : keys) {
        const auto active = m_activeResourceRegions.take(key);
        m_resources.release(active.lease.leaseId);
        m_resources.cancelRequest(active.lease.requestId);
    }
    for (const auto& region : m_plan.resourceRegions) {
        m_resources.cancelRequest(
            QString("resource-region:%1:%2:%3").arg(uutId, frameId, region.id));
    }
}

NodeId ExecutionGraphScheduler::operatorPromptCloseTarget(const ExecNode& node) const
{
    QString requested = node.payload.value("closeOnStep").toString().trimmed();
    if (requested.startsWith("step:", Qt::CaseInsensitive)) {
        requested = requested.mid(5).trimmed();
    }
    if (!requested.isEmpty()) {
        if (m_plan.node(requested)) {
            return requested;
        }

        QVector<NodeId> matches;
        const auto parent = m_plan.structuralParentOf(node.id);
        for (auto it = m_plan.nodes.constBegin(); it != m_plan.nodes.constEnd(); ++it) {
            const auto& candidate = it.value();
            if (candidate.localId != requested && candidate.key != requested) {
                continue;
            }
            if (parent == m_plan.structuralParentOf(candidate.id)) {
                matches.push_back(candidate.id);
            }
        }
        if (matches.size() == 1) {
            return matches.first();
        }
        return {};
    }

    auto edges = m_plan.outgoingEdges(node.id);
    std::sort(edges.begin(), edges.end(), [](const ExecEdge& left, const ExecEdge& right) {
        return left.priority > right.priority;
    });
    for (const auto& edge : edges) {
        if (edge.kind != EdgeKind::Finally &&
            (edge.trigger == EdgeTrigger::OnSuccess || edge.trigger == EdgeTrigger::Always)) {
            return edge.to;
        }
    }
    return {};
}

void ExecutionGraphScheduler::trackOperatorPrompt(const UutExecution& uut,
                                                  const ExecNode& node,
                                                  const NodeResult& result)
{
    const auto instanceId = result.outputs.value("promptInstanceId").toString();
    if (instanceId.isEmpty()) {
        return;
    }
    ActiveOperatorPrompt prompt;
    prompt.instanceId = instanceId;
    prompt.uutId = uut.uutId;
    prompt.sourceNodeId = node.id;
    prompt.closeTargetNodeId = operatorPromptCloseTarget(node);
    m_activeOperatorPrompts.push_back(std::move(prompt));
}

void ExecutionGraphScheduler::closeOperatorPromptsForNode(const UutExecution& uut,
                                                          const ExecNode& completedNode,
                                                          const NodeResult& result)
{
    if (!isTerminalOutcome(result.outcome)) {
        return;
    }
    for (int index = m_activeOperatorPrompts.size() - 1; index >= 0; --index) {
        const auto& prompt = m_activeOperatorPrompts[index];
        if (prompt.uutId != uut.uutId || prompt.sourceNodeId == completedNode.id ||
            prompt.closeTargetNodeId != completedNode.id) {
            continue;
        }
        publishOperatorPromptClosed(prompt.uutId,
                                    prompt.sourceNodeId,
                                    prompt.instanceId,
                                    "target-completed",
                                    completedNode.id);
        m_activeOperatorPrompts.removeAt(index);
    }
}

void ExecutionGraphScheduler::closeAllOperatorPrompts(const QString& reason)
{
    for (const auto& prompt : std::as_const(m_activeOperatorPrompts)) {
        publishOperatorPromptClosed(prompt.uutId,
                                    prompt.sourceNodeId,
                                    prompt.instanceId,
                                    reason);
    }
    m_activeOperatorPrompts.clear();
}

void ExecutionGraphScheduler::publishOperatorPromptClosed(const UutId& uutId,
                                                           const NodeId& sourceNodeId,
                                                           const QString& instanceId,
                                                           const QString& reason,
                                                           const NodeId& closedByNodeId)
{
    if (!m_events || !m_events->hasSink()) {
        return;
    }
    RuntimeEvent event;
    event.kind = RuntimeEventKind::OperatorPromptClosed;
    event.uutId = uutId;
    event.nodeId = sourceNodeId;
    event.outcome = NodeOutcome::Passed;
    event.message = reason;
    event.details.insert("promptInstanceId", instanceId);
    event.details.insert("reason", reason);
    if (!closedByNodeId.isEmpty()) {
        event.details.insert("closedByStep", closedByNodeId);
    }
    if (const auto* source = m_plan.node(sourceNodeId)) {
        event.nodeDisplayName = source->displayName;
        event.nodeKind = source->kind;
        event.nodeLocalId = source->localId;
        event.nodePhase = executionPhaseOf(*source);
    }
    m_events->publish(event);
}

bool ExecutionGraphScheduler::testItemControllerReady(const TestItemRegion& region,
                                                       const UutExecution& uut) const
{
    const auto activation = uut.activations.constFind(region.controllerNodeId);
    if (activation == uut.activations.constEnd() ||
        activation->state != ActivationState::WaitingForDependency) {
        return true;
    }
    for (const auto& childNodeId : region.childNodeIds) {
        if (!isTerminalActivation(uut.stateOf(childNodeId))) {
            return false;
        }
    }
    return true;
}

bool ExecutionGraphScheduler::testItemChildMayRun(const TestItemRegion& region,
                                                  const UutExecution& uut) const
{
    const auto activation = uut.activations.constFind(region.controllerNodeId);
    return activation != uut.activations.constEnd() &&
           activation->state == ActivationState::WaitingForDependency;
}

void ExecutionGraphScheduler::handleTestItemChildFailure(UutExecution& uut,
                                                         const ExecNode& childNode,
                                                         const NodeResult& result,
                                                         ErrorAction action,
                                                         const FrameId& frameId)
{
    if (result.outcome == NodeOutcome::Passed ||
        result.outcome == NodeOutcome::Skipped ||
        result.outcome == NodeOutcome::Unknown ||
        action == ErrorAction::Continue) {
        return;
    }

    const auto region = m_plan.testItemRegionForChild(childNode.id);
    if (!region) {
        return;
    }

    const auto failedIndex = region->childNodeIds.indexOf(childNode.id);
    if (failedIndex < 0) {
        return;
    }

    const auto reason = QString("skipped after TestItem child %1 returned %2")
                            .arg(childNode.id, nodeOutcomeName(result.outcome));
    for (int index = failedIndex + 1; index < region->childNodeIds.size(); ++index) {
        skipNodeSubtree(uut, region->childNodeIds[index], frameId, reason);
    }
}

void ExecutionGraphScheduler::skipNodeSubtree(UutExecution& uut,
                                              const NodeId& rootNodeId,
                                              const FrameId& frameId,
                                              const QString& reason)
{
    const auto completedAt = QDateTime::currentDateTimeUtc();
    for (auto it = m_plan.nodes.constBegin(); it != m_plan.nodes.constEnd(); ++it) {
        const auto& skippedNode = it.value();
        if (!isNodeOrDescendantOf(skippedNode.id, rootNodeId)) {
            continue;
        }

        auto& skippedActivation = uut.ensureActivation(skippedNode.id, frameId);
        if (isTerminalActivation(skippedActivation.state)) {
            continue;
        }

        appendSyntheticAttempt(skippedActivation, NodeOutcome::Skipped, reason);
        skippedActivation.attempts.last().loopIteration =
            loopIterationForAttempt(uut, skippedNode);
        m_results.commit(uut.uutId,
                         frameId,
                         skippedNode.id,
                         skippedActivation.attempts.last().attemptIndex,
                         skippedActivation.attempts.last().result);
        skippedActivation.state = ActivationState::Skipped;
        skippedActivation.completedAt = completedAt;
        publishNodeEvent(RuntimeEventKind::NodeStateChanged,
                         uut,
                         skippedNode,
                         skippedActivation.state,
                         NodeOutcome::Skipped,
                         reason,
                         skippedActivation.attempts.last().loopIteration);
    }
}

bool ExecutionGraphScheduler::isNodeOrDescendantOf(const NodeId& nodeId,
                                                   const NodeId& rootNodeId) const
{
    std::optional<NodeId> current = nodeId;
    QSet<NodeId> visited;
    while (current && !visited.contains(*current)) {
        if (*current == rootNodeId) {
            return true;
        }
        visited.insert(*current);
        current = m_plan.structuralParentOf(*current);
    }
    return false;
}

void ExecutionGraphScheduler::resetTestItemForRetry(UutExecution& uut,
                                                     const ExecNode& testItemNode,
                                                     const FrameId& frameId)
{
    auto testItemActivation = uut.activations.find(testItemNode.id);
    if (testItemActivation != uut.activations.end()) {
        uut.variables = testItemActivation->preNodeSnapshot.values;
    }

    closeOperatorPromptsForTestItemRetry(uut, testItemNode.id);
    for (const auto& loopRegion : m_plan.loopRegions) {
        if (isNodeOrDescendantOf(loopRegion.controllerNodeId, testItemNode.id) &&
            loopRegion.controllerNodeId != testItemNode.id) {
            m_loops.reset(loopRegion, uut.uutId);
        }
    }

    for (auto it = m_plan.nodes.constBegin(); it != m_plan.nodes.constEnd(); ++it) {
        const auto& descendantNode = it.value();
        if (descendantNode.id == testItemNode.id ||
            !isNodeOrDescendantOf(descendantNode.id, testItemNode.id)) {
            continue;
        }
        auto activation = uut.activations.find(descendantNode.id);
        if (activation == uut.activations.end()) {
            continue;
        }
        activation->retryAttemptBase = activation->attempts.size();
        activation->state = ActivationState::Created;
        activation->preNodeSnapshot = {};
        activation->postNodeSnapshot = {};
        activation->completedAt = {};
        publishNodeEvent(RuntimeEventKind::NodeStateChanged,
                         uut,
                         descendantNode,
                         activation->state,
                         NodeOutcome::Unknown,
                         "reset for TestItem retry");
    }

    if (testItemActivation != uut.activations.end()) {
        testItemActivation->state = ActivationState::Created;
        testItemActivation->completedAt = {};
        publishNodeEvent(RuntimeEventKind::NodeStateChanged,
                         uut,
                         testItemNode,
                         testItemActivation->state,
                         NodeOutcome::Unknown,
                         "TestItem retry scheduled");
    }
}

void ExecutionGraphScheduler::closeOperatorPromptsForTestItemRetry(
    const UutExecution& uut,
    const NodeId& testItemNodeId)
{
    for (int index = m_activeOperatorPrompts.size() - 1; index >= 0; --index) {
        const auto& prompt = m_activeOperatorPrompts[index];
        if (prompt.uutId != uut.uutId ||
            !isNodeOrDescendantOf(prompt.sourceNodeId, testItemNodeId)) {
            continue;
        }
        publishOperatorPromptClosed(prompt.uutId,
                                    prompt.sourceNodeId,
                                    prompt.instanceId,
                                    "test-item-retry");
        m_activeOperatorPrompts.removeAt(index);
    }
}

NodeResult ExecutionGraphScheduler::executeTestItemNode(UutExecution& uut,
                                                        const ExecNode& node,
                                                        const FrameId& frameId)
{
    auto& activation = uut.ensureActivation(node.id, frameId);
    const auto region = m_plan.testItemRegionForController(node.id);
    NodeResult result;
    result.nodeId = node.id;
    result.startedAt = QDateTime::currentDateTimeUtc();

    if (!region) {
        result.outcome = NodeOutcome::Error;
        result.errorCode = "TestItemRegionMissing";
        result.errorMessage = QString("Test item region missing for node: %1").arg(node.id);
        result.finishedAt = QDateTime::currentDateTimeUtc();
    } else if (activation.state != ActivationState::WaitingForDependency) {
        activation.preNodeSnapshot.values = uut.variables;
        activation.state = ActivationState::WaitingForDependency;
        publishNodeEvent(RuntimeEventKind::TestItemStarted,
                         uut,
                         node,
                         activation.state,
                         NodeOutcome::Unknown,
                         QString("test item attempt %1 children started")
                             .arg(activation.attempts.size() -
                                  activation.retryAttemptBase + 1));
        result.outcome = NodeOutcome::Unknown;
        result.finishedAt = QDateTime::currentDateTimeUtc();
        return result;
    } else {
        result.outcome = NodeOutcome::Passed;
        QStringList failedChildren;
        for (const auto& childNodeId : region->childNodeIds) {
            const auto childOutcome = uut.outcomeOf(childNodeId);
            if (childOutcome == NodeOutcome::Passed ||
                childOutcome == NodeOutcome::Skipped) {
                continue;
            }
            failedChildren.push_back(
                QString("%1=%2").arg(childNodeId, nodeOutcomeName(childOutcome)));
            if (childOutcome == NodeOutcome::Error) {
                result.outcome = NodeOutcome::Error;
            } else if (childOutcome == NodeOutcome::Timeout &&
                       result.outcome != NodeOutcome::Error) {
                result.outcome = NodeOutcome::Timeout;
            } else if (childOutcome == NodeOutcome::Cancelled &&
                       result.outcome != NodeOutcome::Error &&
                       result.outcome != NodeOutcome::Timeout) {
                result.outcome = NodeOutcome::Cancelled;
            } else if (result.outcome == NodeOutcome::Passed) {
                result.outcome = NodeOutcome::Failed;
            }
        }
        if (!failedChildren.isEmpty()) {
            result.errorCode = "TestItemChildFailed";
            result.errorMessage = QString("Test item child result: %1")
                                      .arg(failedChildren.join(", "));
        }
        result.finishedAt = QDateTime::currentDateTimeUtc();
    }

    appendSyntheticAttempt(activation, result.outcome, result.errorMessage);
    activation.attempts.last().result = result;
    activation.attempts.last().loopIteration = loopIterationForAttempt(uut, node);
    activation.state = outcomeToActivationState(result.outcome);
    activation.completedAt = result.finishedAt;
    publishNodeEvent(RuntimeEventKind::TestItemCompleted,
                     uut,
                     node,
                     activation.state,
                     result.outcome,
                     result.errorMessage,
                     activation.attempts.last().loopIteration,
                     result.errorCode);
    publishAttemptEvent(RuntimeEventKind::AttemptCompleted,
                        uut,
                        node,
                        activation.attempts.last(),
                        result.errorMessage);

    if (result.outcome != NodeOutcome::Passed) {
        const auto completedAttempts = activation.attempts.size() -
            activation.retryAttemptBase;
        auto decision = m_errorPolicy.decide(node, result, completedAttempts);
        if (decision.action == ErrorAction::Retry) {
            m_results.commit(uut.uutId,
                             frameId,
                             node.id,
                             activation.attempts.last().attemptIndex,
                             result);
            publishAttemptEvent(RuntimeEventKind::RetryScheduled,
                                uut,
                                node,
                                activation.attempts.last(),
                                decision.reason);
            // Reset before the scheduler checks region completion. A single-item
            // region anchored to this TestItem then keeps its existing lease.
            resetTestItemForRetry(uut, node, frameId);

            NodeResult pending;
            pending.nodeId = node.id;
            pending.outcome = NodeOutcome::Unknown;
            pending.startedAt = result.startedAt;
            pending.finishedAt = QDateTime::currentDateTimeUtc();
            return pending;
        }
        if (bestEffortCleanupApplies(uut, node.id)) {
            decision.action = ErrorAction::Continue;
            decision.reason = QStringLiteral("best-effort cleanup continues after error");
        }
        if (m_plan.isInsideTestItem(node.id)) {
            handleTestItemChildFailure(uut, node, result, decision.action, frameId);
        } else if (!isWhileLoopBodyNode(node.id)) {
            if (decision.action == ErrorAction::RunCleanup) {
                activateCleanup(uut, decision.cleanupRegionId);
            }
            handleNodeFailureForBarriers(uut, node, result, frameId);
            if (decision.action == ErrorAction::StopUut ||
                decision.action == ErrorAction::RunCleanup ||
                decision.action == ErrorAction::Abort) {
                skipPendingNonAlwaysRun(uut, frameId, executionPhaseOf(node));
            }
        }
    }
    return result;
}

NodeResult ExecutionGraphScheduler::executeLoopNode(UutExecution& uut,
                                                    const ExecNode& node,
                                                    const FrameId& frameId)
{
    auto& activation = uut.ensureActivation(node.id, frameId);
    const auto previousState = activation.state;
    activation.state = ActivationState::Running;
    publishNodeEvent(RuntimeEventKind::NodeStateChanged,
                     uut,
                     node,
                     activation.state);

    NodeResult result;
    result.nodeId = node.id;
    result.startedAt = QDateTime::currentDateTimeUtc();

    const auto region = m_plan.loopRegionForController(node.id);
    if (!region) {
        result.outcome = NodeOutcome::Error;
        result.errorCode = "LoopRegionMissing";
        result.errorMessage = QString("Loop region missing for controller node: %1").arg(node.id);
        result.finishedAt = QDateTime::currentDateTimeUtc();
        appendSyntheticAttempt(activation, NodeOutcome::Error, result.errorMessage);
        activation.state = ActivationState::Error;
        activation.completedAt = result.finishedAt;
        publishNodeEvent(RuntimeEventKind::NodeStateChanged,
                         uut,
                         node,
                         activation.state,
                         result.outcome,
                         result.errorMessage);
        return result;
    }

    const auto decision = m_loops.advance(*region, uut);
    result.outcome = decision.outcome;
    result.outputs = decision.outputs;
    result.errorMessage = decision.message;
    result.errorCode = decision.errorCode;
    if (decision.outcome != NodeOutcome::Unknown &&
        decision.outcome != NodeOutcome::Passed && result.errorCode.isEmpty()) {
        result.errorCode = "LoopChildFailed";
    }
    result.finishedAt = QDateTime::currentDateTimeUtc();

    if (decision.skippedBody) {
        for (const auto& bodyNodeId : region->bodyNodes) {
            auto& bodyActivation = uut.ensureActivation(bodyNodeId, frameId);
            if (isTerminalActivation(bodyActivation.state)) {
                continue;
            }
            appendSyntheticAttempt(bodyActivation, NodeOutcome::Skipped, "loop did not run");
            bodyActivation.state = ActivationState::Skipped;
            bodyActivation.completedAt = result.finishedAt;
            if (const auto* bodyNode = m_plan.node(bodyNodeId)) {
                publishNodeEvent(RuntimeEventKind::NodeStateChanged,
                                 uut,
                                 *bodyNode,
                                 bodyActivation.state,
                                 NodeOutcome::Skipped,
                                 "loop did not run");
            }
        }
    }

    if (decision.outcome != NodeOutcome::Unknown) {
        appendSyntheticAttempt(activation, decision.outcome, decision.message);
        activation.attempts.last().result = result;
        activation.state = outcomeToActivationState(decision.outcome);
        activation.completedAt = result.finishedAt;
        publishNodeEvent(RuntimeEventKind::LoopCompleted,
                         uut,
                         node,
                         activation.state,
                         result.outcome,
                         decision.message);
        if (decision.outcome != NodeOutcome::Passed) {
            const auto errorDecision = m_errorPolicy.decide(node, result, activation.attempts.size());
            if (m_plan.isInsideTestItem(node.id)) {
                handleTestItemChildFailure(uut,
                                           node,
                                           result,
                                           errorDecision.action,
                                           frameId);
            } else {
                if (errorDecision.action == ErrorAction::RunCleanup) {
                    activateCleanup(uut, errorDecision.cleanupRegionId);
                }
                handleNodeFailureForBarriers(uut, node, result, frameId);
                if (errorDecision.action == ErrorAction::StopUut ||
                    errorDecision.action == ErrorAction::RunCleanup ||
                    errorDecision.action == ErrorAction::Abort) {
                    skipPendingNonAlwaysRun(uut, frameId, executionPhaseOf(node));
                }
            }
        }
        return result;
    }

    activation.state = previousState == ActivationState::WaitingForDependency
        ? ActivationState::Ready
        : ActivationState::WaitingForDependency;
    LoopIterationContext iteration;
    iteration.active = true;
    iteration.loopId = region->id;
    iteration.controllerNodeId = node.id;
    iteration.variableName = region->type == LoopType::While
        ? QString("iteration")
        : region->forLoop.variableName;
    iteration.iterationIndex = uut.variables.value("loop.index", -1).toInt();
    iteration.iterationNumber = uut.variables.value("loop.number", 0).toInt();
    iteration.value = uut.variables.value("loop.value", 0).toInt();
    publishNodeEvent(RuntimeEventKind::LoopIterationStarted,
                     uut,
                     node,
                     activation.state,
                     NodeOutcome::Unknown,
                      decision.message,
                      iteration);
    waitForLoopInterval(decision.delayBeforeNextMs);
    return result;
}

bool ExecutionGraphScheduler::isWhileLoopBodyNode(const NodeId& nodeId) const
{
    const auto region = m_plan.loopRegionForBodyNode(nodeId);
    return region && region->type == LoopType::While;
}

void ExecutionGraphScheduler::handleBreakRequest(UutExecution& uut,
                                                  const ExecNode& node,
                                                  const NodeResult& result,
                                                  const FrameId& frameId)
{
    if (node.kind != ExecNodeKind::Break || result.outcome != NodeOutcome::Passed ||
        !result.outputs.value("breakRequested").toBool()) {
        return;
    }

    const auto region = m_plan.loopRegionForBodyNode(node.id);
    if (!region) {
        return;
    }
    m_loops.requestBreak(*region, uut.uutId, node.id);

    const auto completedAt = QDateTime::currentDateTimeUtc();
    const auto reason = QString("skipped after Break If %1 matched").arg(node.id);
    for (const auto& bodyNodeId : region->bodyNodes) {
        if (bodyNodeId == node.id || isTerminalActivation(uut.stateOf(bodyNodeId))) {
            continue;
        }
        const auto* bodyNode = m_plan.node(bodyNodeId);
        if (!bodyNode) {
            continue;
        }

        auto& skipped = uut.ensureActivation(bodyNodeId, frameId);
        appendSyntheticAttempt(skipped, NodeOutcome::Skipped, reason);
        skipped.attempts.last().loopIteration = loopIterationForAttempt(uut, *bodyNode);
        skipped.state = ActivationState::Skipped;
        skipped.completedAt = completedAt;
        m_results.commit(uut.uutId,
                         frameId,
                         bodyNodeId,
                         skipped.attempts.last().attemptIndex,
                         skipped.attempts.last().result);
        publishNodeEvent(RuntimeEventKind::NodeStateChanged,
                         uut,
                         *bodyNode,
                         skipped.state,
                         NodeOutcome::Skipped,
                         reason,
                         skipped.attempts.last().loopIteration);
    }
}

void ExecutionGraphScheduler::waitForLoopInterval(int intervalMs) const
{
    constexpr int sliceMs = 10;
    int remaining = qMax(0, intervalMs);
    while (remaining > 0 && (!m_stopToken || !m_stopToken->isStopRequested())) {
        const int waitMs = qMin(sliceMs, remaining);
        QThread::msleep(static_cast<unsigned long>(waitMs));
        remaining -= waitMs;
    }
}

NodeResult ExecutionGraphScheduler::executeBarrierNode(UutExecution& uut,
                                                       const ExecNode& node,
                                                       const FrameId& frameId)
{
    auto& activation = uut.ensureActivation(node.id, frameId);
    const auto barrierId = barrierInstanceForNode(node, uut.uutId);

    BarrierArrival arrival;
    arrival.barrierId = barrierId;
    arrival.uutId = uut.uutId;
    arrival.frameId = frameId;
    arrival.barrierNodeId = node.id;
    arrival.arrivalOutcome = NodeOutcome::Passed;

    const auto decision = m_barriers.memberArrived(arrival);
    activation.state = ActivationState::WaitingAtBarrier;
    publishNodeEvent(RuntimeEventKind::BarrierWaiting,
                     uut,
                     node,
                     activation.state,
                     NodeOutcome::Unknown,
                     "waiting at barrier");

    NodeResult result;
    result.nodeId = node.id;
    result.startedAt = QDateTime::currentDateTimeUtc();

    if (decision.released()) {
        releaseBarrierNodes(decision);
        appendSyntheticAttempt(activation, NodeOutcome::Passed, "barrier released");
        activation.state = ActivationState::Passed;
        activation.completedAt = QDateTime::currentDateTimeUtc();
        result.outcome = NodeOutcome::Passed;
        publishNodeEvent(RuntimeEventKind::BarrierReleased,
                         uut,
                         node,
                         activation.state,
                         result.outcome,
                         "barrier released");
    } else {
        result.outcome = NodeOutcome::Unknown;
    }

    result.finishedAt = QDateTime::currentDateTimeUtc();
    return result;
}

void ExecutionGraphScheduler::handleNodeFailureForBarriers(UutExecution& uut,
                                                           const ExecNode& failedNode,
                                                           const NodeResult& result,
                                                           const FrameId& frameId)
{
    if (failedNode.kind == ExecNodeKind::Barrier) {
        return;
    }

    for (auto it = m_plan.nodes.constBegin(); it != m_plan.nodes.constEnd(); ++it) {
        const auto& barrierNode = it.value();
        if (barrierNode.kind != ExecNodeKind::Barrier) {
            continue;
        }
        if (!hasPathToNode(failedNode.id, barrierNode.id)) {
            continue;
        }

        const auto barrierId = barrierInstanceForNode(barrierNode, uut.uutId);
        auto decision = m_barriers.memberFailedBeforeArrival(
            uut.uutId, barrierId, result.outcome);
        if (decision.released()) {
            releaseBarrierNodes(decision);
        }

        auto& barrierActivation = uut.ensureActivation(barrierNode.id, frameId);
        if (!isTerminalActivation(barrierActivation.state)) {
            appendSyntheticAttempt(
                barrierActivation,
                NodeOutcome::Skipped,
                QString("skipped because %1 failed before barrier").arg(failedNode.id));
            barrierActivation.state = ActivationState::Skipped;
            barrierActivation.completedAt = QDateTime::currentDateTimeUtc();
            publishNodeEvent(RuntimeEventKind::NodeStateChanged,
                             uut,
                             barrierNode,
                             barrierActivation.state,
                             NodeOutcome::Skipped,
                             QString("skipped because %1 failed before barrier").arg(failedNode.id));
        }
    }
}

bool ExecutionGraphScheduler::hasPathToNode(const NodeId& from, const NodeId& to) const
{
    QSet<NodeId> visited;
    QVector<NodeId> stack;
    stack.push_back(from);

    while (!stack.isEmpty()) {
        const auto current = stack.takeLast();
        if (current == to) {
            return true;
        }
        if (visited.contains(current)) {
            continue;
        }
        visited.insert(current);

        for (const auto& edge : m_plan.outgoingEdges(current)) {
            stack.push_back(edge.to);
        }
    }

    return false;
}

LoopIterationContext ExecutionGraphScheduler::loopIterationForAttempt(const UutExecution& uut,
                                                                      const ExecNode& node) const
{
    LoopIterationContext context;
    const auto region = m_plan.loopRegionForBodyNode(node.id);
    if (!region) {
        return context;
    }

    const auto loopIndex = uut.variables.value("loop.index");
    const auto loopValue = uut.variables.value("loop.value");
    if (!loopIndex.isValid() || !loopValue.isValid()) {
        return context;
    }

    context.active = true;
    context.loopId = region->id;
    context.controllerNodeId = region->controllerNodeId;
    context.variableName = region->type == LoopType::While
        ? QString("iteration")
        : region->forLoop.variableName;
    context.iterationIndex = loopIndex.toInt();
    context.iterationNumber = uut.variables.value("loop.number", context.iterationIndex + 1).toInt();
    context.value = loopValue.toInt();
    return context;
}

void ExecutionGraphScheduler::activateCleanup(UutExecution& uut,
                                              const CleanupRegionId& cleanupRegionId)
{
    const auto region = m_plan.cleanupRegion(cleanupRegionId);
    if (!region) {
        return;
    }

    for (const auto& entryNode : region->entryNodes) {
        auto& activation = uut.ensureActivation(entryNode, "cleanup");
        if (!isTerminalActivation(activation.state)) {
            activation.state = ActivationState::Created;
            if (const auto* node = m_plan.node(entryNode)) {
                publishNodeEvent(RuntimeEventKind::CleanupActivated,
                                 uut,
                                 *node,
                                 activation.state,
                                 NodeOutcome::Unknown,
                                 QString("cleanup region activated: %1").arg(cleanupRegionId));
            }
        }
    }
}

bool ExecutionGraphScheduler::cleanupRegionContainsNode(
    const CleanupRegion& region,
    const NodeId& nodeId) const
{
    const auto* node = m_plan.node(nodeId);
    if (!node || executionPhaseOf(*node) != ExecutionPhase::Cleanup) {
        return false;
    }

    NodeId controlNodeId = nodeId;
    while (const auto parent = m_plan.structuralParentOf(controlNodeId)) {
        controlNodeId = *parent;
    }

    const bool reachableFromEntry = std::any_of(
        region.entryNodes.cbegin(), region.entryNodes.cend(),
        [this, &controlNodeId](const NodeId& entry) {
            return hasPathToNode(entry, controlNodeId);
        });
    if (!reachableFromEntry) {
        return false;
    }

    return region.exitNodes.isEmpty() || std::any_of(
        region.exitNodes.cbegin(), region.exitNodes.cend(),
        [this, &controlNodeId](const NodeId& exit) {
            return hasPathToNode(controlNodeId, exit);
        });
}

bool ExecutionGraphScheduler::cleanupRegionIsActive(
    const CleanupRegion& region,
    const UutExecution& uut) const
{
    return std::any_of(
        region.entryNodes.cbegin(), region.entryNodes.cend(),
        [&uut](const NodeId& entry) {
            return uut.activations.contains(entry);
        });
}

bool ExecutionGraphScheduler::bestEffortCleanupApplies(
    const UutExecution& uut,
    const NodeId& nodeId) const
{
    return std::any_of(
        m_plan.cleanupRegions.cbegin(), m_plan.cleanupRegions.cend(),
        [this, &uut, &nodeId](const CleanupRegion& region) {
            return region.bestEffort && cleanupRegionIsActive(region, uut) &&
                   cleanupRegionContainsNode(region, nodeId);
        });
}

bool ExecutionGraphScheduler::bestEffortCleanupEdgeActive(
    const UutExecution& uut,
    const NodeId& from,
    const NodeId& to) const
{
    return std::any_of(
        m_plan.cleanupRegions.cbegin(), m_plan.cleanupRegions.cend(),
        [this, &uut, &from, &to](const CleanupRegion& region) {
            return region.bestEffort && cleanupRegionIsActive(region, uut) &&
                   cleanupRegionContainsNode(region, from) &&
                   cleanupRegionContainsNode(region, to);
        });
}

bool ExecutionGraphScheduler::finalizeBlockedCleanup(UutExecution& uut,
                                                     const FrameId& frameId)
{
    bool changed = false;
    const auto completedAt = QDateTime::currentDateTimeUtc();
    for (const auto& region : m_plan.cleanupRegions) {
        if (!cleanupRegionIsActive(region, uut)) {
            continue;
        }
        for (auto it = m_plan.nodes.constBegin(); it != m_plan.nodes.constEnd(); ++it) {
            const auto& node = it.value();
            if (!cleanupRegionContainsNode(region, node.id)) {
                continue;
            }
            auto& activation = uut.ensureActivation(node.id, frameId);
            if (isTerminalActivation(activation.state) ||
                activation.state == ActivationState::Running ||
                activation.state == ActivationState::WaitingForResource ||
                activation.state == ActivationState::WaitingAtBarrier) {
                continue;
            }

            appendSyntheticAttempt(
                activation,
                NodeOutcome::Skipped,
                QStringLiteral("cleanup could not continue after a prior cleanup error"));
            activation.state = ActivationState::Skipped;
            activation.completedAt = completedAt;
            publishNodeEvent(
                RuntimeEventKind::NodeStateChanged,
                uut,
                node,
                activation.state,
                NodeOutcome::Skipped,
                QStringLiteral("cleanup could not continue after a prior cleanup error"));
            changed = true;
        }
    }
    return changed;
}

BarrierNodePayload ExecutionGraphScheduler::barrierPayloadFromNode(const ExecNode& node) const
{
    BarrierNodePayload payload;
    payload.barrierName = node.payload.value("barrierName", node.id).toString();
    payload.cohortId = node.payload.value("cohortId", "default").toString();
    payload.expectedUutCount = node.payload.value("expectedUutCount", -1).toInt();

    const auto arrivalPolicy = node.payload.value("arrivalPolicy", "WaitAll").toString();
    if (arrivalPolicy.compare("DropFailed", Qt::CaseInsensitive) == 0) {
        payload.arrivalPolicy = BarrierArrivalPolicy::DropFailed;
    } else if (arrivalPolicy.compare("Quorum", Qt::CaseInsensitive) == 0) {
        payload.arrivalPolicy = BarrierArrivalPolicy::Quorum;
    } else if (arrivalPolicy.compare("BestEffort", Qt::CaseInsensitive) == 0) {
        payload.arrivalPolicy = BarrierArrivalPolicy::BestEffort;
    } else {
        payload.arrivalPolicy = BarrierArrivalPolicy::WaitAll;
    }

    const auto releasePolicy = node.payload.value("releasePolicy", "Lockstep").toString();
    if (releasePolicy.compare("Latch", Qt::CaseInsensitive) == 0) {
        payload.releasePolicy = BarrierReleasePolicy::Latch;
    } else if (releasePolicy.compare("Cohort", Qt::CaseInsensitive) == 0) {
        payload.releasePolicy = BarrierReleasePolicy::Cohort;
    } else if (releasePolicy.compare("RollingWindow", Qt::CaseInsensitive) == 0) {
        payload.releasePolicy = BarrierReleasePolicy::RollingWindow;
    } else {
        payload.releasePolicy = BarrierReleasePolicy::Lockstep;
    }

    const auto failurePolicy = node.payload.value("failurePolicy", "FailBarrier").toString();
    if (failurePolicy.compare("RemoveFailedMember", Qt::CaseInsensitive) == 0) {
        payload.failurePolicy = BarrierFailurePolicy::RemoveFailedMember;
    } else if (failurePolicy.compare("HoldFailedMember", Qt::CaseInsensitive) == 0) {
        payload.failurePolicy = BarrierFailurePolicy::HoldFailedMember;
    } else if (failurePolicy.compare("ContinueWithWarning", Qt::CaseInsensitive) == 0) {
        payload.failurePolicy = BarrierFailurePolicy::ContinueWithWarning;
    } else if (failurePolicy.compare("AbortCohort", Qt::CaseInsensitive) == 0) {
        payload.failurePolicy = BarrierFailurePolicy::AbortCohort;
    } else {
        payload.failurePolicy = BarrierFailurePolicy::FailBarrier;
    }

    const auto timeoutPolicy = node.payload.value("timeoutPolicy", "FailArrivedAndWaiting").toString();
    if (timeoutPolicy.compare("ReleaseArrived", Qt::CaseInsensitive) == 0) {
        payload.timeoutPolicy = BarrierTimeoutPolicy::ReleaseArrived;
    } else if (timeoutPolicy.compare("ReleaseIfQuorumReached", Qt::CaseInsensitive) == 0) {
        payload.timeoutPolicy = BarrierTimeoutPolicy::ReleaseIfQuorumReached;
    } else if (timeoutPolicy.compare("AbortCohort", Qt::CaseInsensitive) == 0) {
        payload.timeoutPolicy = BarrierTimeoutPolicy::AbortCohort;
    } else if (timeoutPolicy.compare("RequestOperatorDecision", Qt::CaseInsensitive) == 0) {
        payload.timeoutPolicy = BarrierTimeoutPolicy::RequestOperatorDecision;
    } else {
        payload.timeoutPolicy = BarrierTimeoutPolicy::FailArrivedAndWaiting;
    }

    return payload;
}

BarrierInstanceId ExecutionGraphScheduler::barrierInstanceForNode(const ExecNode& node,
                                                                  const UutId& uutId)
{
    auto it = m_barrierByNode.constFind(node.id);
    if (it != m_barrierByNode.constEnd()) {
        return it.value();
    }

    auto payload = barrierPayloadFromNode(node);
    auto expected = m_cohortUuts;
    if (expected.isEmpty()) {
        expected.insert(uutId);
    }

    const auto barrierId = m_barriers.createBarrier(payload, expected);
    m_barrierByNode.insert(node.id, barrierId);
    m_nodeByBarrier.insert(barrierId, node.id);
    return barrierId;
}

void ExecutionGraphScheduler::appendSyntheticAttempt(NodeActivation& activation,
                                                     NodeOutcome outcome,
                                                     const QString& message)
{
    NodeAttempt attempt;
    attempt.id = QString("%1:synthetic-%2").arg(activation.id).arg(activation.attempts.size());
    attempt.requestId = createRequestId(QStringLiteral("synthetic"));
    attempt.activationId = activation.id;
    attempt.attemptIndex = activation.attempts.size();
    attempt.state = AttemptState::Completed;
    attempt.result.nodeId = activation.nodeId;
    attempt.result.outcome = outcome;
    attempt.result.errorMessage = message;
    attempt.result.startedAt = QDateTime::currentDateTimeUtc();
    attempt.result.finishedAt = attempt.result.startedAt;
    activation.attempts.push_back(attempt);
}

void ExecutionGraphScheduler::publishNodeEvent(RuntimeEventKind kind,
                                                const UutExecution& uut,
                                                const ExecNode& node,
                                                ActivationState state,
                                                NodeOutcome outcome,
                                                const QString& message,
                                                const LoopIterationContext& loopIteration,
                                                const QString& errorCode)
{
    if (!m_events) {
        return;
    }

    RuntimeEvent event;
    event.kind = kind;
    event.uutId = uut.uutId;
    event.nodeId = node.id;
    event.nodeLocalId = node.localId.isEmpty() ? node.id : node.localId;
    const auto parent = m_plan.structuralParentOf(node.id);
    if (parent) {
        event.parentNodeId = *parent;
    }
    event.nodeDisplayName = node.displayName;
    event.nodeKind = node.kind;
    event.nodePhase = executionPhaseOf(node);
    event.activationState = state;
    event.outcome = outcome;
    event.errorCode = errorCode;
    event.message = message;
    event.loopIteration = loopIteration;
    const auto activation = uut.activations.constFind(node.id);
    if (activation != uut.activations.constEnd()) {
        event.frameId = activation->frameId;
        if (!activation->attempts.isEmpty()) {
            event.requestId = activation->attempts.last().requestId;
        }
        if (activation->createdAt.isValid() && activation->completedAt.isValid()) {
            event.details.insert(
                "durationMs",
                qMax<qint64>(0, activation->createdAt.msecsTo(activation->completedAt)));
        }
    }
    m_events->publish(event);
}

void ExecutionGraphScheduler::publishAttemptEvent(RuntimeEventKind kind,
                                                   const UutExecution& uut,
                                                   const ExecNode& node,
                                                   const NodeAttempt& attempt,
                                                   const QString& message)
{
    if (!m_events) {
        return;
    }

    RuntimeEvent event;
    event.kind = kind;
    event.uutId = uut.uutId;
    event.nodeId = node.id;
    event.nodeLocalId = node.localId.isEmpty() ? node.id : node.localId;
    const auto parent = m_plan.structuralParentOf(node.id);
    if (parent) {
        event.parentNodeId = *parent;
    }
    event.nodeDisplayName = node.displayName;
    event.nodeKind = node.kind;
    event.nodePhase = executionPhaseOf(node);
    event.attemptId = attempt.id;
    event.requestId = attempt.requestId;
    event.attemptIndex = attempt.attemptIndex + 1;
    event.attemptState = attempt.state;
    event.outcome = attempt.result.outcome;
    event.loopIteration = attempt.loopIteration;
    event.measurements = attempt.result.measurements;
    event.errorCode = attempt.result.errorCode;
    event.message = message;
    if (node.kind == ExecNodeKind::Break) {
        const auto breakRequested = attempt.result.outputs.value("breakRequested");
        if (breakRequested.isValid()) {
            event.details.insert("breakRequested", breakRequested);
        }
    }
    if (attempt.result.startedAt.isValid() && attempt.result.finishedAt.isValid()) {
        event.details.insert(
            "durationMs",
            qMax<qint64>(0, attempt.result.startedAt.msecsTo(attempt.result.finishedAt)));
    }
    const auto activation = uut.activations.constFind(node.id);
    if (activation != uut.activations.constEnd()) {
        event.frameId = activation->frameId;
        event.activationState = activation->state;
    }
    m_events->publish(event);
}

} // namespace PicoATE::Core
