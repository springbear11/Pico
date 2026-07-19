#include "PicoATE/Core/ExecutionSession.h"

#include <algorithm>
#include <utility>

namespace PicoATE::Core {

namespace {

bool outcomeWasError(NodeOutcome outcome)
{
    return outcome == NodeOutcome::Failed ||
           outcome == NodeOutcome::Error ||
           outcome == NodeOutcome::Timeout;
}

bool measurementsHaveError(const QVector<MeasurementResult>& measurements)
{
    for (const auto& measurement : measurements) {
        if (measurementStatusIsError(measurement.status)) {
            return true;
        }
    }
    return false;
}

bool sessionStateIsTerminal(ExecutionState state)
{
    return state == ExecutionState::Completed ||
           state == ExecutionState::CompletedWithError ||
           state == ExecutionState::Aborted;
}

QVector<NodeId> topologicallyOrderedSubset(const ExecutionPlan& plan,
                                           const QVector<NodeId>& candidates)
{
    QSet<NodeId> candidateSet;
    QHash<NodeId, int> sourceOrder;
    for (int index = 0; index < candidates.size(); ++index) {
        candidateSet.insert(candidates[index]);
        sourceOrder.insert(candidates[index], index);
    }

    QHash<NodeId, int> indegree;
    for (const auto& id : candidates) {
        indegree.insert(id, 0);
    }
    for (const auto& edge : plan.edges) {
        if (candidateSet.contains(edge.from) && candidateSet.contains(edge.to)) {
            indegree[edge.to] += 1;
        }
    }

    QVector<NodeId> ready;
    for (const auto& id : candidates) {
        if (indegree.value(id) == 0) {
            ready.push_back(id);
        }
    }

    QVector<NodeId> ordered;
    ordered.reserve(candidates.size());
    while (!ready.isEmpty()) {
        std::sort(ready.begin(), ready.end(), [&sourceOrder](const NodeId& left, const NodeId& right) {
            return sourceOrder.value(left) < sourceOrder.value(right);
        });
        const auto current = ready.takeFirst();
        ordered.push_back(current);

        auto outgoing = plan.outgoingEdges(current);
        std::sort(outgoing.begin(), outgoing.end(), [](const ExecEdge& left, const ExecEdge& right) {
            if (left.priority != right.priority) {
                return left.priority > right.priority;
            }
            return left.to < right.to;
        });

        for (const auto& edge : outgoing) {
            if (!candidateSet.contains(edge.to)) {
                continue;
            }
            indegree[edge.to] -= 1;
            if (indegree.value(edge.to) == 0) {
                ready.push_back(edge.to);
            }
        }
    }

    for (const auto& id : candidates) {
        if (!ordered.contains(id)) {
            ordered.push_back(id);
        }
    }
    return ordered;
}

QVector<NodeId> directStructuralChildren(const ExecutionPlan& plan, const NodeId& nodeId)
{
    if (const auto testItem = plan.testItemRegionForController(nodeId)) {
        return topologicallyOrderedSubset(plan, testItem->childNodeIds);
    }
    if (const auto loop = plan.loopRegionForController(nodeId)) {
        return topologicallyOrderedSubset(plan, loop->childNodeIds);
    }
    return {};
}

void appendStructuralOrder(const ExecutionPlan& plan,
                           const NodeId& nodeId,
                           QVector<NodeId>& ordered,
                           QSet<NodeId>& placed)
{
    if (placed.contains(nodeId)) {
        return;
    }
    placed.insert(nodeId);
    ordered.push_back(nodeId);
    for (const auto& childNodeId : directStructuralChildren(plan, nodeId)) {
        appendStructuralOrder(plan, childNodeId, ordered, placed);
    }
}

QVector<NodeId> orderedNodeIds(const ExecutionPlan& plan)
{
    QVector<NodeId> allNodeIds;
    allNodeIds.reserve(plan.nodes.size());
    for (auto it = plan.nodes.constBegin(); it != plan.nodes.constEnd(); ++it) {
        allNodeIds.push_back(it.key());
    }
    std::sort(allNodeIds.begin(), allNodeIds.end());

    QVector<NodeId> topLevelNodeIds;
    for (const auto& nodeId : allNodeIds) {
        if (!plan.structuralParentOf(nodeId)) {
            topLevelNodeIds.push_back(nodeId);
        }
    }

    QVector<NodeId> ordered;
    QSet<NodeId> placed;
    ordered.reserve(allNodeIds.size());
    for (const auto& nodeId : topologicallyOrderedSubset(plan, topLevelNodeIds)) {
        appendStructuralOrder(plan, nodeId, ordered, placed);
    }

    for (const auto& nodeId : allNodeIds) {
        appendStructuralOrder(plan, nodeId, ordered, placed);
    }
    return ordered;
}

StepReport makeStepReport(const ExecutionPlan& plan, const UutExecution& uut, const NodeId& nodeId)
{
    StepReport report;
    report.stepId = nodeId;
    report.nodePath = nodeId;

    const auto* node = plan.node(nodeId);
    if (node) {
        report.stepId = node->localId.isEmpty() ? nodeId : node->localId;
        report.displayName = node->displayName;
        report.kind = node->kind;
        report.phase = node->phase;
    }

    const auto loopRegion = plan.loopRegionForBodyNode(nodeId);
    if (loopRegion) {
        report.loop.inLoop = true;
        report.loop.loopId = loopRegion->id;
        report.loop.controllerStepId = loopRegion->controllerNodeId;
        report.loop.variableName = loopRegion->forLoop.variableName;
        report.loop.from = loopRegion->forLoop.from;
        report.loop.to = loopRegion->forLoop.to;
        report.loop.step = loopRegion->forLoop.step;
    }

    const auto activationIt = uut.activations.constFind(nodeId);
    if (activationIt == uut.activations.constEnd()) {
        return report;
    }

    const auto& activation = activationIt.value();
    report.state = activation.state;
    if (activation.createdAt.isValid() && activation.completedAt.isValid()) {
        report.durationMs = qMax<qint64>(
            0, activation.createdAt.msecsTo(activation.completedAt));
    }
    if (!activation.attempts.isEmpty()) {
        report.outcome = activation.attempts.last().result.outcome;
        report.measurements = activation.attempts.last().result.measurements;
    }

    report.attempts.reserve(activation.attempts.size());
    for (const auto& attempt : activation.attempts) {
        AttemptReport attemptReport;
        attemptReport.index = attempt.attemptIndex + 1;
        attemptReport.outcome = attempt.result.outcome;
        if (attempt.result.startedAt.isValid() && attempt.result.finishedAt.isValid()) {
            attemptReport.durationMs = qMax<qint64>(
                0, attempt.result.startedAt.msecsTo(attempt.result.finishedAt));
        }
        attemptReport.errorCode = attempt.result.errorCode;
        attemptReport.errorMessage = attempt.result.errorMessage;
        attemptReport.loopIteration = attempt.loopIteration;
        attemptReport.measurements = attempt.result.measurements;
        report.attempts.push_back(attemptReport);
    }

    report.wasError = outcomeWasError(report.outcome) || measurementsHaveError(report.measurements);
    return report;
}

QVector<NodeId> structuralChildren(const ExecutionPlan& plan, const NodeId& nodeId)
{
    return directStructuralChildren(plan, nodeId);
}

StepReport makeStepReportTree(const ExecutionPlan& plan,
                              const UutExecution& uut,
                              const NodeId& nodeId)
{
    auto report = makeStepReport(plan, uut, nodeId);
    const auto children = structuralChildren(plan, nodeId);
    report.children.reserve(children.size());
    for (const auto& childNodeId : children) {
        report.children.push_back(makeStepReportTree(plan, uut, childNodeId));
    }
    return report;
}

bool stepReportHasError(const StepReport& step)
{
    if (step.wasError) {
        return true;
    }
    for (const auto& child : step.children) {
        if (stepReportHasError(child)) {
            return true;
        }
    }
    return false;
}

} // namespace

ExecutionSession::ExecutionSession(ExecutionPlan plan,
                                   std::shared_ptr<StopToken> stopToken,
                                   IRuntimeEventSink* eventSink,
                                   std::shared_ptr<ExecutionControl> executionControl,
                                   FailureHandlingMode failureHandling)
    : m_plan(std::move(plan))
    , m_results(m_plan)
    , m_events(m_plan.id, eventSink)
    , m_stopToken(stopToken ? std::move(stopToken) : std::make_shared<StopToken>())
    , m_executionControl(executionControl
                             ? std::move(executionControl)
                             : std::make_shared<ExecutionControl>())
    , m_runtimeServices(m_devices)
    , m_errorPolicy(failureHandling)
{
    m_devices.setRuntimeEventEmitter(&m_events);
    m_runner.setRuntimeServices(&m_runtimeServices);
    m_scheduler = std::make_unique<ExecutionGraphScheduler>(
        m_plan,
        m_resources,
        m_barriers,
        m_loops,
        m_errorPolicy,
        m_runner,
        m_results,
        &m_events,
        m_executionControl.get(),
        m_stopToken.get());
}

UutExecution& ExecutionSession::addUut(const UutId& uutId)
{
    UutExecution uut;
    uut.uutId = uutId;
    m_uuts.push_back(uut);
    RuntimeEvent event;
    event.kind = RuntimeEventKind::UutRegistered;
    event.uutId = uutId;
    m_events.publish(event);
    return m_uuts.last();
}

QVector<UutExecution>& ExecutionSession::uuts()
{
    return m_uuts;
}

const QVector<UutExecution>& ExecutionSession::uuts() const
{
    return m_uuts;
}

ExecutionResultStore& ExecutionSession::results()
{
    return m_results;
}

const ExecutionResultStore& ExecutionSession::results() const
{
    return m_results;
}

DeviceSessionManager& ExecutionSession::devices()
{
    return m_devices;
}

const DeviceSessionManager& ExecutionSession::devices() const
{
    return m_devices;
}

bool ExecutionSession::registerModule(std::shared_ptr<IModule> module)
{
    return m_runner.registerModule(std::move(module));
}

void ExecutionSession::requestStop(StopMode mode)
{
    m_stopToken->requestStop(mode);
    m_executionControl->operatorPrompts().cancelAll();
    m_executionControl->resume();
}

bool ExecutionSession::requestPause()
{
    if (m_stopToken->isStopRequested()) {
        return false;
    }
    return m_executionControl->requestPause();
}

void ExecutionSession::resume()
{
    m_executionControl->resume();
}

std::shared_ptr<StopToken> ExecutionSession::stopToken() const
{
    return m_stopToken;
}

std::shared_ptr<ExecutionControl> ExecutionSession::executionControl() const
{
    return m_executionControl;
}

ExecutionState ExecutionSession::state() const
{
    return m_state;
}

ExecutionSessionResult ExecutionSession::run()
{
    ExecutionSessionResult result;
    m_executionControl->clearDebugSnapshot();
    m_breakpointResumeGuards.clear();
    m_activeDebugStepMode = DebugStepMode::None;
    m_debugStepUutId.clear();
    m_debugStepFrameId.clear();
    m_debugStepRootNodeId.clear();
    m_debugStepStarted = false;
    if (m_uuts.isEmpty()) {
        m_state = ExecutionState::Completed;
        publishSessionState("session completed without UUTs");
        result.completed = true;
        result.state = m_state;
        return result;
    }

    QSet<UutId> uutIds;
    for (const auto& uut : m_uuts) {
        uutIds.insert(uut.uutId);
    }
    m_scheduler->setCohortUuts(uutIds);

    if (!m_stopToken->isStopRequested()) {
        m_state = ExecutionState::Running;
        publishSessionState("session running");
    }

    bool progressed = true;
    while (progressed) {
        prepareStopIfRequested();
        pauseAtSafePointIfRequested();
        prepareStopIfRequested();
        progressed = false;

        for (auto& uut : m_uuts) {
            pauseAtSafePointIfRequested();
            prepareStopIfRequested();
            pauseAtBreakpointIfNeeded(uut);
            prepareStopIfRequested();
            auto step = m_scheduler->pumpOnce(uut);
            if (!step.nodeResults.isEmpty()) {
                result.nodeResults += step.nodeResults;
            }
            if (step.hasError) {
                result.hasError = true;
            }
            if (step.progressed) {
                progressed = true;
            }

            m_scheduler->applyBarrierReleases(uutPointers());
            publishCompletedUuts();
            pauseAfterDebugStepIfNeeded(uut, step, "root");
        }

        if (allUutsComplete()) {
            break;
        }
    }

    result.completed = allUutsComplete();
    if (m_stopToken->isStopRequested() &&
        m_stopToken->requestedMode() == StopMode::Abort) {
        m_state = ExecutionState::Aborted;
    } else if (result.completed && result.hasError) {
        m_state = ExecutionState::CompletedWithError;
    } else if (result.completed) {
        m_state = ExecutionState::Completed;
    } else if (m_stopToken->isStopRequested()) {
        m_state = ExecutionState::Stopping;
    }

    result.state = m_state;
    m_scheduler->closeAllOperatorPrompts(
        m_stopToken->isStopRequested() ? QStringLiteral("session-stopped")
                                       : QStringLiteral("session-finished"));
    publishCompletedUuts();
    publishSessionState("session finished");
    return result;
}

ExecutionReport ExecutionSession::report() const
{
    ExecutionReport report;
    report.planId = m_plan.id;
    report.sequenceId = m_plan.sequenceId;
    report.sequenceVersion = m_plan.sequenceVersion;
    report.state = m_state;
    report.completed = sessionStateIsTerminal(m_state) || allUutsComplete();

    const auto nodeIds = orderedNodeIds(m_plan);
    report.uuts.reserve(m_uuts.size());
    for (const auto& uut : m_uuts) {
        UutReport uutReport;
        uutReport.uutId = uut.uutId;
        uutReport.steps.reserve(nodeIds.size());

        for (const auto& nodeId : nodeIds) {
            if (m_plan.structuralParentOf(nodeId)) {
                continue;
            }
            auto stepReport = makeStepReportTree(m_plan, uut, nodeId);
            uutReport.hasError = uutReport.hasError || stepReportHasError(stepReport);
            uutReport.steps.push_back(stepReport);
        }

        report.hasError = report.hasError || uutReport.hasError;
        report.uuts.push_back(uutReport);
    }

    return report;
}

ExecutionSessionSnapshot ExecutionSession::snapshot() const
{
    ExecutionSessionSnapshot snapshot;
    snapshot.rootPlanId = m_plan.id;
    snapshot.state = m_state;
    snapshot.uuts = m_uuts;
    snapshot.resources = m_resources.snapshot();
    snapshot.barriers = m_barriers.snapshot();
    snapshot.runtimeVersion = "0.1.0";
    return snapshot;
}

ExecutionDebugSnapshot ExecutionSession::debugSnapshot(
    DebugPauseReason reason,
    std::optional<BreakpointHit> breakpoint) const
{
    return makeExecutionDebugSnapshot(m_plan,
                                      m_state,
                                      m_uuts,
                                      m_resources.snapshot(),
                                      m_barriers.snapshot(),
                                      reason,
                                      std::move(breakpoint));
}

bool ExecutionSession::allUutsComplete() const
{
    for (const auto& uut : m_uuts) {
        if (!uutComplete(uut)) {
            return false;
        }
    }
    return true;
}

bool ExecutionSession::uutComplete(const UutExecution& uut) const
{
    for (auto it = m_plan.nodes.constBegin(); it != m_plan.nodes.constEnd(); ++it) {
        const auto& node = it.value();
        if (node.kind == ExecNodeKind::Cleanup &&
            !uut.activations.contains(node.id) &&
            m_plan.incomingEdges(node.id).isEmpty()) {
            continue;
        }

        if (!isTerminalActivation(uut.stateOf(node.id))) {
            return false;
        }
    }
    return true;
}

QVector<UutExecution*> ExecutionSession::uutPointers()
{
    QVector<UutExecution*> pointers;
    pointers.reserve(m_uuts.size());
    for (auto& uut : m_uuts) {
        pointers.push_back(&uut);
    }
    return pointers;
}

void ExecutionSession::prepareStopIfRequested()
{
    if (!m_stopToken->isStopRequested() || m_stopPrepared) {
        return;
    }

    m_state = m_stopToken->requestedMode() == StopMode::Abort
        ? ExecutionState::Aborted
        : ExecutionState::Stopping;
    publishSessionState(m_state == ExecutionState::Aborted
                            ? "abort requested"
                            : "graceful stop requested");
    for (auto& uut : m_uuts) {
        m_scheduler->skipPendingNonAlwaysRun(uut);
        m_scheduler->activateAllCleanup(uut);
    }
    m_stopPrepared = true;
}

void ExecutionSession::pauseAtSafePointIfRequested()
{
    if (m_stopToken->isStopRequested() || !m_executionControl->enterPausedState()) {
        return;
    }

    m_state = ExecutionState::Paused;
    m_executionControl->setDebugSnapshot(debugSnapshot(DebugPauseReason::UserPause));
    publishSessionState("session paused at node boundary");
    m_executionControl->waitUntilResumedOrStopped(*m_stopToken);

    if (!m_stopToken->isStopRequested()) {
        consumeDebugStepCommand();
        m_state = ExecutionState::Running;
        publishSessionState("session resumed");
    }
}

void ExecutionSession::pauseAtBreakpointIfNeeded(UutExecution& uut, const FrameId& frameId)
{
    if (m_stopToken->isStopRequested() ||
        m_executionControl->state() != ExecutionControlState::Running) {
        return;
    }

    const auto nextNodeId = m_scheduler->nextReadyNodeId(uut);
    if (!nextNodeId) {
        return;
    }
    const auto* node = m_plan.node(*nextNodeId);
    if (!node) {
        return;
    }
    if (debugStepShouldSuppressBreakpoint(node->id)) {
        return;
    }

    const auto guardKey = QString("%1|%2|%3").arg(uut.uutId, frameId, node->id);
    if (m_breakpointResumeGuards.remove(guardKey)) {
        return;
    }

    auto hit = m_executionControl->matchBreakpoint(m_plan, uut, *node);
    if (!hit || !m_executionControl->requestPause()) {
        return;
    }

    m_breakpointResumeGuards.insert(guardKey);
    if (!m_executionControl->enterPausedState()) {
        return;
    }

    m_state = ExecutionState::Paused;
    m_executionControl->setDebugSnapshot(
        makeExecutionDebugSnapshot(m_plan,
                                   m_state,
                                   m_uuts,
                                   m_resources.snapshot(),
                                   m_barriers.snapshot(),
                                   DebugPauseReason::Breakpoint,
                                   hit));
    publishBreakpointHit(*hit);
    publishSessionState(QString("breakpoint hit: %1").arg(hit->localPath));
    m_executionControl->waitUntilResumedOrStopped(*m_stopToken);

    if (!m_stopToken->isStopRequested()) {
        consumeDebugStepCommand();
        m_state = ExecutionState::Running;
        publishSessionState("session resumed");
    }
}

void ExecutionSession::consumeDebugStepCommand()
{
    const auto mode = m_executionControl->takeStepMode();
    if (mode == DebugStepMode::None) {
        return;
    }
    m_activeDebugStepMode = mode;
    m_debugStepUutId.clear();
    m_debugStepFrameId.clear();
    m_debugStepRootNodeId.clear();
    m_debugStepStarted = false;
}

void ExecutionSession::beginDebugStepIfNeeded(const UutExecution& uut,
                                              const NodeId& nodeId,
                                              const FrameId& frameId)
{
    if (m_activeDebugStepMode == DebugStepMode::None || m_debugStepStarted) {
        return;
    }
    m_debugStepStarted = true;
    m_debugStepUutId = uut.uutId;
    m_debugStepFrameId = frameId;
    m_debugStepRootNodeId = nodeId;
}

void ExecutionSession::pauseAfterDebugStepIfNeeded(const UutExecution& uut,
                                                   const SchedulerStepResult& step,
                                                   const FrameId& frameId)
{
    if (m_stopToken->isStopRequested() ||
        m_activeDebugStepMode == DebugStepMode::None ||
        !step.progressed ||
        step.nodeId.isEmpty()) {
        return;
    }

    beginDebugStepIfNeeded(uut, step.nodeId, frameId);
    if (uut.uutId != m_debugStepUutId || frameId != m_debugStepFrameId) {
        return;
    }
    if (!shouldPauseForActiveDebugStep(uut, step) ||
        !m_executionControl->requestPause() ||
        !m_executionControl->enterPausedState()) {
        return;
    }

    const auto finishedMode = m_activeDebugStepMode;
    m_activeDebugStepMode = DebugStepMode::None;
    m_debugStepStarted = false;

    const auto reason = finishedMode == DebugStepMode::Over
        ? DebugPauseReason::StepOver
        : DebugPauseReason::StepInto;
    m_state = ExecutionState::Paused;
    m_executionControl->setDebugSnapshot(
        makeExecutionDebugSnapshot(m_plan,
                                   m_state,
                                   m_uuts,
                                   m_resources.snapshot(),
                                   m_barriers.snapshot(),
                                   reason,
                                   std::nullopt,
                                   uut.uutId,
                                   step.nodeId));
    publishDebugStepCompleted(finishedMode, uut, step.nodeId);
    publishSessionState(finishedMode == DebugStepMode::Over
                            ? "step over completed"
                            : "step into completed");
    m_executionControl->waitUntilResumedOrStopped(*m_stopToken);

    if (!m_stopToken->isStopRequested()) {
        consumeDebugStepCommand();
        m_state = ExecutionState::Running;
        publishSessionState("session resumed");
    }
}

bool ExecutionSession::shouldPauseForActiveDebugStep(const UutExecution& uut,
                                                     const SchedulerStepResult& step) const
{
    if (m_activeDebugStepMode == DebugStepMode::Into) {
        return true;
    }
    if (m_activeDebugStepMode != DebugStepMode::Over) {
        return false;
    }
    if (m_debugStepRootNodeId.isEmpty()) {
        return true;
    }

    const bool structuralRoot =
        m_plan.loopRegionForController(m_debugStepRootNodeId).has_value() ||
        m_plan.testItemRegionForController(m_debugStepRootNodeId).has_value();
    if (!structuralRoot) {
        return true;
    }
    return isTerminalActivation(uut.stateOf(m_debugStepRootNodeId));
}

bool ExecutionSession::debugStepShouldSuppressBreakpoint(const NodeId& nodeId) const
{
    if (m_activeDebugStepMode != DebugStepMode::Over ||
        !m_debugStepStarted ||
        m_debugStepRootNodeId.isEmpty()) {
        return false;
    }
    return nodeId == m_debugStepRootNodeId ||
           nodeIsDescendantOf(nodeId, m_debugStepRootNodeId);
}

bool ExecutionSession::nodeIsDescendantOf(const NodeId& nodeId,
                                          const NodeId& rootNodeId) const
{
    auto parent = m_plan.structuralParentOf(nodeId);
    while (parent) {
        if (*parent == rootNodeId) {
            return true;
        }
        parent = m_plan.structuralParentOf(*parent);
    }
    return false;
}

void ExecutionSession::publishSessionState(const QString& message)
{
    RuntimeEvent event;
    event.kind = RuntimeEventKind::SessionStateChanged;
    event.executionState = m_state;
    event.message = message;
    m_events.publish(event);
}

void ExecutionSession::publishBreakpointHit(const BreakpointHit& hit)
{
    RuntimeEvent event;
    event.kind = RuntimeEventKind::BreakpointHit;
    event.executionState = ExecutionState::Paused;
    event.uutId = hit.uutId;
    event.nodeId = hit.nodeId;
    event.nodeLocalId = hit.localPath;
    event.nodeDisplayName = hit.displayName;
    event.message = QString("breakpoint hit: %1").arg(hit.localPath);
    event.details.insert("breakpointId", hit.breakpointId);
    event.details.insert("localPath", hit.localPath);
    event.details.insert("hitCount", hit.hitCount);
    m_events.publish(event);
}

void ExecutionSession::publishDebugStepCompleted(DebugStepMode mode,
                                                 const UutExecution& uut,
                                                 const NodeId& nodeId)
{
    const auto* node = m_plan.node(nodeId);
    RuntimeEvent event;
    event.kind = RuntimeEventKind::DebugStepCompleted;
    event.executionState = ExecutionState::Paused;
    event.uutId = uut.uutId;
    event.nodeId = nodeId;
    event.nodeLocalId = debugLocalPathForNode(m_plan, nodeId);
    if (node) {
        event.nodeDisplayName = node->displayName;
        event.nodeKind = node->kind;
    }
    event.message = mode == DebugStepMode::Over
        ? QString("step over completed: %1").arg(event.nodeLocalId)
        : QString("step into completed: %1").arg(event.nodeLocalId);
    event.details.insert("stepMode", mode == DebugStepMode::Over ? "over" : "into");
    m_events.publish(event);
}

void ExecutionSession::publishCompletedUuts()
{
    for (const auto& uut : m_uuts) {
        if (m_publishedCompletedUuts.contains(uut.uutId) || !uutComplete(uut)) {
            continue;
        }

        bool hasError = false;
        for (auto it = uut.activations.constBegin(); it != uut.activations.constEnd(); ++it) {
            const auto outcome = uut.outcomeOf(it.key());
            hasError = hasError || outcomeWasError(outcome);
        }

        RuntimeEvent event;
        event.kind = RuntimeEventKind::UutCompleted;
        event.uutId = uut.uutId;
        event.outcome = hasError ? NodeOutcome::Failed : NodeOutcome::Passed;
        event.details.insert("hasError", hasError);
        m_events.publish(event);
        m_publishedCompletedUuts.insert(uut.uutId);
    }
}

} // namespace PicoATE::Core
