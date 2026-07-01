#include "PicoATE/Core/ExecutionGraphScheduler.h"

namespace PicoATE::Core {

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
                                                 RuntimeEventEmitter* events)
    : m_plan(plan)
    , m_resources(resources)
    , m_barriers(barriers)
    , m_loops(loops)
    , m_errorPolicy(errorPolicy)
    , m_runner(runner)
    , m_results(results)
    , m_events(events)
{
}

SchedulerResult ExecutionGraphScheduler::run(UutExecution& uut, const FrameId& frameId)
{
    SchedulerResult schedulerResult;
    bool progressed = true;

    while (progressed) {
        auto step = pumpOnce(uut, frameId);
        progressed = step.progressed;
        schedulerResult.nodeResults += step.nodeResults;
        if (step.hasError) {
            schedulerResult.hasError = true;
        }
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

SchedulerStepResult ExecutionGraphScheduler::pumpOnce(UutExecution& uut, const FrameId& frameId)
{
    SchedulerStepResult step;
    const auto readyNodes = findReadyNodes(uut);
    if (readyNodes.isEmpty()) {
        step.blocked = true;
        return step;
    }

    const auto& nodeId = readyNodes.first();
    const ExecNode* node = m_plan.node(nodeId);
    if (!node) {
        step.blocked = true;
        return step;
    }

    const auto previousState = uut.stateOf(nodeId);
    auto result = executeNode(uut, *node, frameId);
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
    step.progressed = previousState != currentState || result.outcome != NodeOutcome::Unknown;
    step.blocked = !step.progressed;
    step.hasError = result.outcome == NodeOutcome::Failed ||
                    result.outcome == NodeOutcome::Error ||
                    result.outcome == NodeOutcome::Timeout;
    return step;
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

    for (auto it = m_plan.nodes.constBegin(); it != m_plan.nodes.constEnd(); ++it) {
        const auto& node = it.value();
        if (node.kind == ExecNodeKind::Cleanup && node.alwaysRun) {
            auto& activation = uut.ensureActivation(node.id, "cleanup");
            if (!isTerminalActivation(activation.state)) {
                activation.state = ActivationState::Created;
            }
        }
    }
}

void ExecutionGraphScheduler::skipPendingNonAlwaysRun(UutExecution& uut, const FrameId& frameId)
{
    for (auto it = m_plan.nodes.constBegin(); it != m_plan.nodes.constEnd(); ++it) {
        const auto& node = it.value();
        if (node.kind == ExecNodeKind::Cleanup || node.alwaysRun) {
            continue;
        }

        auto& activation = uut.ensureActivation(node.id, frameId);
        if (isTerminalActivation(activation.state)) {
            continue;
        }

        appendSyntheticAttempt(activation, NodeOutcome::Skipped, "stop requested");
        activation.state = ActivationState::Skipped;
        activation.completedAt = QDateTime::currentDateTimeUtc();
        publishNodeEvent(RuntimeEventKind::NodeStateChanged,
                         uut,
                         node,
                         activation.state,
                         NodeOutcome::Skipped,
                         "stop requested");
    }
}

QVector<NodeId> ExecutionGraphScheduler::findReadyNodes(const UutExecution& uut) const
{
    QVector<NodeId> ready;
    for (auto it = m_plan.nodes.constBegin(); it != m_plan.nodes.constEnd(); ++it) {
        const auto& node = it.value();
        const auto state = uut.stateOf(node.id);
        if (isTerminalActivation(state) ||
            state == ActivationState::Running ||
            state == ActivationState::WaitingAtBarrier) {
            continue;
        }
        if (node.kind == ExecNodeKind::Cleanup &&
            !uut.activations.contains(node.id) &&
            m_plan.incomingEdges(node.id).isEmpty()) {
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
            (!m_loops.controllerReady(*loopRegion, uut) || !dependenciesSatisfied(uut, node))) {
            continue;
        }
        const auto testItemRegion = m_plan.testItemRegionForController(node.id);
        if (testItemRegion &&
            (!testItemControllerReady(*testItemRegion, uut) ||
             !dependenciesSatisfied(uut, node))) {
            continue;
        }
        if (dependenciesSatisfied(uut, node)) {
            ready.push_back(node.id);
        }
    }
    return ready;
}

bool ExecutionGraphScheduler::dependenciesSatisfied(const UutExecution& uut,
                                                    const ExecNode& node) const
{
    auto activationIt = uut.activations.constFind(node.id);
    // V3.1 currently treats alwaysRun as a cleanup-region concern. If we add
    // non-cleanup alwaysRun nodes later, they should get their own activation
    // path instead of reusing this cleanup dependency bypass.
    if (node.kind == ExecNodeKind::Cleanup &&
        activationIt != uut.activations.constEnd() &&
        activationIt.value().frameId == "cleanup") {
        return true;
    }

    const auto incoming = m_plan.incomingEdges(node.id);
    if (incoming.isEmpty()) {
        return true;
    }

    for (const auto& edge : incoming) {
        const auto sourceOutcome = uut.outcomeOf(edge.from);
        if (triggerMatchesOutcome(edge.trigger, sourceOutcome)) {
            continue;
        }

        const bool mayContinueFailure =
            edge.trigger == EdgeTrigger::OnSuccess &&
            edge.condition != "step-result" &&
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
    if (!node.resources.isEmpty()) {
        ResourceRequest request;
        request.requestId = QString("%1:%2").arg(uut.uutId, node.id);
        request.uutId = uut.uutId;
        request.frameId = frameId;
        request.nodeId = node.id;
        request.requirements = node.resources;

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

    NodeResult result;
    ErrorDecision finalDecision;
    bool shouldRetry = true;
    const auto loopIteration = loopIterationForAttempt(uut, node);
    while (shouldRetry) {
        NodeAttempt attempt;
        attempt.id = QString("%1:attempt-%2").arg(activation.id).arg(activation.attempts.size());
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
        context.currentNodeId = node.id;
        context.attemptIndex = attempt.attemptIndex;
        context.variables = uut.variables;
        context.resultStore = &m_results;

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

        const auto decision = m_errorPolicy.decide(node, result, activation.attempts.size());
        finalDecision = decision;
        shouldRetry = decision.action == ErrorAction::Retry;
        if (shouldRetry) {
            publishAttemptEvent(RuntimeEventKind::RetryScheduled,
                                uut,
                                node,
                                activation.attempts.last(),
                                decision.reason);
        }
        if (decision.action == ErrorAction::RunCleanup && !isTestItemChild) {
            activateCleanup(uut, decision.cleanupRegionId);
        }
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

    if (hasLease) {
        m_resources.release(lease.leaseId);
    }

    if (result.outcome != NodeOutcome::Passed &&
        result.outcome != NodeOutcome::Skipped &&
        result.outcome != NodeOutcome::Unknown) {
        if (!isTestItemChild) {
            handleNodeFailureForBarriers(uut, node, result, frameId);
            if (finalDecision.action == ErrorAction::StopUut ||
                finalDecision.action == ErrorAction::RunCleanup ||
                finalDecision.action == ErrorAction::Abort) {
                skipPendingNonAlwaysRun(uut, frameId);
            }
        }
    }

    return result;
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
        activation.state = ActivationState::WaitingForDependency;
        publishNodeEvent(RuntimeEventKind::TestItemStarted,
                         uut,
                         node,
                         activation.state,
                         NodeOutcome::Unknown,
                         "test item children started");
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
                     activation.attempts.last().loopIteration);

    if (result.outcome != NodeOutcome::Passed && !m_plan.isInsideTestItem(node.id)) {
        const auto decision = m_errorPolicy.decide(node, result, activation.attempts.size());
        if (decision.action == ErrorAction::RunCleanup) {
            activateCleanup(uut, decision.cleanupRegionId);
        }
        handleNodeFailureForBarriers(uut, node, result, frameId);
        if (decision.action == ErrorAction::StopUut ||
            decision.action == ErrorAction::RunCleanup ||
            decision.action == ErrorAction::Abort) {
            skipPendingNonAlwaysRun(uut, frameId);
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
    result.errorMessage = decision.message;
    if (decision.outcome != NodeOutcome::Unknown &&
        decision.outcome != NodeOutcome::Passed) {
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
        activation.state = outcomeToActivationState(decision.outcome);
        activation.completedAt = result.finishedAt;
        publishNodeEvent(RuntimeEventKind::LoopCompleted,
                         uut,
                         node,
                         activation.state,
                         result.outcome,
                         decision.message);
        if (decision.outcome != NodeOutcome::Passed && !m_plan.isInsideTestItem(node.id)) {
            const auto errorDecision = m_errorPolicy.decide(node, result, activation.attempts.size());
            if (errorDecision.action == ErrorAction::RunCleanup) {
                activateCleanup(uut, errorDecision.cleanupRegionId);
            }
            handleNodeFailureForBarriers(uut, node, result, frameId);
            if (errorDecision.action == ErrorAction::StopUut ||
                errorDecision.action == ErrorAction::RunCleanup ||
                errorDecision.action == ErrorAction::Abort) {
                skipPendingNonAlwaysRun(uut, frameId);
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
    iteration.variableName = region->forLoop.variableName;
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
    return result;
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
    context.variableName = region->forLoop.variableName;
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
                                                const LoopIterationContext& loopIteration)
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
    event.activationState = state;
    event.outcome = outcome;
    event.message = message;
    event.loopIteration = loopIteration;
    const auto activation = uut.activations.constFind(node.id);
    if (activation != uut.activations.constEnd()) {
        event.frameId = activation->frameId;
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
    event.attemptId = attempt.id;
    event.attemptIndex = attempt.attemptIndex + 1;
    event.attemptState = attempt.state;
    event.outcome = attempt.result.outcome;
    event.loopIteration = attempt.loopIteration;
    event.measurements = attempt.result.measurements;
    event.errorCode = attempt.result.errorCode;
    event.message = message;
    const auto activation = uut.activations.constFind(node.id);
    if (activation != uut.activations.constEnd()) {
        event.frameId = activation->frameId;
        event.activationState = activation->state;
    }
    m_events->publish(event);
}

} // namespace PicoATE::Core
