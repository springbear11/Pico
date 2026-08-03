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
        report.phase = executionPhaseOf(*node);
        report.resultRecording = node->resultRecording;
        if (node->kind == ExecNodeKind::Limit) {
            report.measurements.push_back(
                configuredMeasurementPreview(node->payload, node->displayName));
        }
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
        if (!activation.attempts.last().result.measurements.isEmpty()) {
            report.measurements = activation.attempts.last().result.measurements;
        }
    }

    report.attempts.reserve(activation.attempts.size());
    for (const auto& attempt : activation.attempts) {
        AttemptReport attemptReport;
        attemptReport.index = attempt.attemptIndex + 1;
        attemptReport.requestId = attempt.requestId;
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
    if (node && node->periodic.enabled) {
        for (const auto& attempt : activation.attempts) {
            report.wasError = report.wasError || outcomeWasError(attempt.result.outcome) ||
                              measurementsHaveError(attempt.result.measurements);
        }
    }
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

bool ExecutionSession::hasModule(const ModuleId& moduleId) const
{
    return m_runner.modules().contains(moduleId);
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
    m_scheduler->stopAllPeriodicTasks();
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
        ExecutionSessionResult::UutResult uutResult;
        uutResult.uutId = uut.uutId;
        result.uutResults.push_back(std::move(uutResult));
    }
    m_scheduler->setCohortUuts(uutIds);

    auto appendSessionStep = [&result](const SchedulerStepResult& step) {
        result.sessionNodeResults += step.nodeResults;
        result.nodeResults += step.nodeResults;
        result.hasError = result.hasError || step.hasError;
    };
    auto appendUutStep = [&result](const UutId& uutId, const SchedulerStepResult& step) {
        result.nodeResults += step.nodeResults;
        result.hasError = result.hasError || step.hasError;
        auto it = std::find_if(
            result.uutResults.begin(), result.uutResults.end(),
            [&uutId](const ExecutionSessionResult::UutResult& candidate) {
                return candidate.uutId == uutId;
            });
        if (it != result.uutResults.end()) {
            it->nodeResults += step.nodeResults;
            it->hasError = it->hasError || step.hasError;
        }
    };

    if (!m_stopToken->isStopRequested()) {
        m_state = ExecutionState::Running;
        publishSessionState("session running");
    }

    while (!phaseComplete(m_sessionExecution, ExecutionPhase::Setup)) {
        prepareStopIfRequested();
        if (!m_stopToken->isStopRequested()) {
            const auto periodicStep = m_scheduler->pumpPeriodicTaskOnce();
            appendSessionStep(periodicStep);
            if (periodicStep.progressed) {
                continue;
            }
        }
        if (m_executionControl->state() == ExecutionControlState::PauseRequested &&
            m_scheduler->hasPendingRequestForUut(m_sessionExecution.uutId)) {
            const auto step = m_scheduler->pumpPendingRequestOnce(
                m_sessionExecution,
                QStringLiteral("session-setup"),
                ExecutionPhase::Setup);
            appendSessionStep(step);
            if (!step.progressed) {
                m_scheduler->waitForPendingRequest();
            }
            continue;
        }
        pauseAtSafePointIfRequested();
        prepareStopIfRequested();
        pauseAtBreakpointIfNeeded(
            m_sessionExecution,
            QStringLiteral("session-setup"),
            ExecutionPhase::Setup);
        prepareStopIfRequested();
        const auto step = m_scheduler->pumpOnce(
            m_sessionExecution, QStringLiteral("session-setup"), ExecutionPhase::Setup);
        appendSessionStep(step);
        pauseAfterDebugStepIfNeeded(
            m_sessionExecution, step, QStringLiteral("session-setup"));
        if (!step.progressed) {
            if (m_scheduler->hasPendingRequestForUut(m_sessionExecution.uutId)) {
                m_scheduler->waitForPendingRequest();
                continue;
            }
            break;
        }
    }

    // A stop can be requested before run() or while an empty Setup phase is crossed.
    prepareStopIfRequested();

    const bool setupComplete = phaseComplete(m_sessionExecution, ExecutionPhase::Setup);
    const bool setupHasError = phaseHasError(m_sessionExecution, ExecutionPhase::Setup);
    result.hasError = result.hasError || setupHasError;
    if (!setupComplete || setupHasError) {
        const auto reason = setupHasError
            ? QStringLiteral("skipped because session setup failed")
            : QStringLiteral("skipped because session setup did not complete");
        for (auto& uut : m_uuts) {
            m_scheduler->skipPendingNonAlwaysRun(
                uut, QStringLiteral("root"), ExecutionPhase::Main, reason, true);
        }
    }

    const bool runMain = setupComplete && !setupHasError && !m_stopToken->isStopRequested();
    while (runMain) {
        prepareStopIfRequested();
        const auto periodicStep = m_stopToken->isStopRequested()
            ? SchedulerStepResult{}
            : m_scheduler->pumpPeriodicTaskOnce();
        appendSessionStep(periodicStep);
        if (m_executionControl->state() == ExecutionControlState::PauseRequested &&
            m_scheduler->hasPendingRequests()) {
            bool completedPendingRequest = false;
            for (auto& uut : m_uuts) {
                if (!m_scheduler->hasPendingRequestForUut(uut.uutId)) {
                    continue;
                }
                const auto step = m_scheduler->pumpPendingRequestOnce(
                    uut, QStringLiteral("root"), ExecutionPhase::Main);
                appendUutStep(uut.uutId, step);
                completedPendingRequest = completedPendingRequest || step.progressed;
            }
            m_scheduler->applyBarrierReleases(uutPointers());
            publishCompletedUuts();
            if (m_scheduler->hasPendingRequests()) {
                if (!completedPendingRequest) {
                    m_scheduler->waitForPendingRequest();
                }
                continue;
            }
        }
        pauseAtSafePointIfRequested();
        prepareStopIfRequested();
        bool progressed = periodicStep.progressed;
        for (auto& uut : m_uuts) {
            if (uutComplete(uut)) {
                continue;
            }
            if (m_executionControl->state() == ExecutionControlState::PauseRequested) {
                break;
            }
            pauseAtSafePointIfRequested();
            prepareStopIfRequested();
            if (!m_stopToken->isStopRequested()) {
                const auto periodicUutStep = m_scheduler->pumpPeriodicTaskOnce();
                appendSessionStep(periodicUutStep);
                progressed = progressed || periodicUutStep.progressed;
            }
            pauseAtBreakpointIfNeeded(uut);
            prepareStopIfRequested();
            auto step = m_scheduler->pumpOnce(
                uut, QStringLiteral("root"), ExecutionPhase::Main);
            appendUutStep(uut.uutId, step);
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
        if (!progressed) {
            if (m_scheduler->hasPendingRequests()) {
                m_scheduler->waitForPendingRequest();
                continue;
            }
            break;
        }
    }

    publishCompletedUuts();
    result.hasError = result.hasError || m_scheduler->stopAllPeriodicTasks();
    for (const auto& uut : m_uuts) {
        m_scheduler->releaseAllResourceRegions(uut.uutId, QStringLiteral("root"));
    }
    if (allUutsComplete()) {
        m_state = ExecutionState::CleaningUp;
        publishSessionState("session cleanup running");
        m_scheduler->activateAllCleanup(m_sessionExecution);

        while (!phaseComplete(m_sessionExecution, ExecutionPhase::Cleanup)) {
            prepareStopIfRequested();
            if (m_executionControl->state() == ExecutionControlState::PauseRequested &&
                m_scheduler->hasPendingRequestForUut(m_sessionExecution.uutId)) {
                const auto step = m_scheduler->pumpPendingRequestOnce(
                    m_sessionExecution,
                    QStringLiteral("session-cleanup"),
                    ExecutionPhase::Cleanup);
                appendSessionStep(step);
                if (!step.progressed) {
                    m_scheduler->waitForPendingRequest();
                }
                continue;
            }
            pauseAtBreakpointIfNeeded(
                m_sessionExecution,
                QStringLiteral("session-cleanup"),
                ExecutionPhase::Cleanup);
            prepareStopIfRequested();
            const auto step = m_scheduler->pumpOnce(
                m_sessionExecution,
                QStringLiteral("session-cleanup"),
                ExecutionPhase::Cleanup);
            appendSessionStep(step);
            pauseAfterDebugStepIfNeeded(
                m_sessionExecution, step, QStringLiteral("session-cleanup"));
            if (!step.progressed) {
                if (m_scheduler->hasPendingRequestForUut(m_sessionExecution.uutId)) {
                    m_scheduler->waitForPendingRequest();
                    continue;
                }
                break;
            }
        }
    }

    const bool cleanupComplete = phaseComplete(m_sessionExecution, ExecutionPhase::Cleanup);
    const bool cleanupHasError = phaseHasError(m_sessionExecution, ExecutionPhase::Cleanup);
    result.hasError = result.hasError || cleanupHasError;

    for (auto& uutResult : result.uutResults) {
        const auto uut = std::find_if(
            m_uuts.cbegin(), m_uuts.cend(),
            [&uutResult](const UutExecution& candidate) {
                return candidate.uutId == uutResult.uutId;
            });
        if (uut == m_uuts.cend()) {
            continue;
        }
        uutResult.completed = uutComplete(*uut);
        uutResult.hasError = uutResult.hasError || phaseHasError(*uut, ExecutionPhase::Main);
    }

    result.completed = setupComplete && allUutsComplete() && cleanupComplete;
    if (m_stopToken->isStopRequested() &&
        m_stopToken->requestedMode() == StopMode::Abort) {
        m_state = ExecutionState::Aborted;
    } else if (result.completed && result.hasError) {
        m_state = ExecutionState::CompletedWithError;
    } else if (result.completed) {
        m_state = ExecutionState::Completed;
    } else {
        // run() is synchronous; transitional states must not escape as final results.
        result.hasError = true;
        m_state = ExecutionState::CompletedWithError;
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
    report.completed = sessionStateIsTerminal(m_state) ||
        (phaseComplete(m_sessionExecution, ExecutionPhase::Setup) &&
         allUutsComplete() &&
         phaseComplete(m_sessionExecution, ExecutionPhase::Cleanup));

    const auto nodeIds = orderedNodeIds(m_plan);
    for (const auto& nodeId : nodeIds) {
        const auto* node = m_plan.node(nodeId);
        if (!node || executionPhaseOf(*node) == ExecutionPhase::Main ||
            m_plan.structuralParentOf(nodeId)) {
            continue;
        }
        auto stepReport = makeStepReportTree(m_plan, m_sessionExecution, nodeId);
        report.sessionHasError = report.sessionHasError || stepReportHasError(stepReport);
        report.sessionSteps.push_back(std::move(stepReport));
    }
    report.hasError = report.sessionHasError;

    report.uuts.reserve(m_uuts.size());
    for (const auto& uut : m_uuts) {
        UutReport uutReport;
        uutReport.uutId = uut.uutId;
        uutReport.completed = uutComplete(uut);
        uutReport.steps.reserve(nodeIds.size());

        for (const auto& nodeId : nodeIds) {
            const auto* node = m_plan.node(nodeId);
            if (!node || executionPhaseOf(*node) != ExecutionPhase::Main ||
                m_plan.structuralParentOf(nodeId)) {
                continue;
            }
            auto stepReport = makeStepReportTree(m_plan, uut, nodeId);
            uutReport.hasError = uutReport.hasError || stepReportHasError(stepReport);
            uutReport.steps.push_back(stepReport);
        }

        uutReport.outcome = uutReport.completed
            ? (uutReport.hasError ? NodeOutcome::Failed : NodeOutcome::Passed)
            : NodeOutcome::Unknown;

        report.hasError = report.hasError || uutReport.hasError;
        report.uuts.push_back(uutReport);
    }

    if (m_state == ExecutionState::CompletedWithError) {
        report.sessionHasError = true;
        report.hasError = true;
    }

    return report;
}

ExecutionSessionSnapshot ExecutionSession::snapshot() const
{
    ExecutionSessionSnapshot snapshot;
    snapshot.rootPlanId = m_plan.id;
    snapshot.state = m_state;
    snapshot.sessionExecution = m_sessionExecution;
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

bool ExecutionSession::phaseComplete(const UutExecution& execution,
                                     ExecutionPhase phase) const
{
    for (auto it = m_plan.nodes.constBegin(); it != m_plan.nodes.constEnd(); ++it) {
        if (executionPhaseOf(it.value()) != phase) {
            continue;
        }
        if (!isTerminalActivation(execution.stateOf(it.key()))) {
            return false;
        }
    }
    return true;
}

bool ExecutionSession::phaseHasError(const UutExecution& execution,
                                     ExecutionPhase phase) const
{
    for (auto it = m_plan.nodes.constBegin(); it != m_plan.nodes.constEnd(); ++it) {
        if (executionPhaseOf(it.value()) == phase &&
            outcomeWasError(execution.outcomeOf(it.key()))) {
            return true;
        }
    }
    return false;
}

bool ExecutionSession::uutComplete(const UutExecution& uut) const
{
    return phaseComplete(uut, ExecutionPhase::Main);
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
    m_scheduler->skipPendingNonAlwaysRun(
        m_sessionExecution,
        QStringLiteral("session-setup"),
        ExecutionPhase::Setup,
        QStringLiteral("skipped after session stop"),
        true);
    for (auto& uut : m_uuts) {
        m_scheduler->skipPendingNonAlwaysRun(
            uut,
            QStringLiteral("root"),
            ExecutionPhase::Main,
            QStringLiteral("skipped after session stop"),
            true);
    }
    m_scheduler->activateAllCleanup(m_sessionExecution);
    m_stopPrepared = true;
}

void ExecutionSession::pauseAtSafePointIfRequested()
{
    if (m_stopToken->isStopRequested() ||
        (m_scheduler && m_scheduler->hasPendingRequests()) ||
        !m_executionControl->enterPausedState()) {
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

void ExecutionSession::pauseAtBreakpointIfNeeded(
    UutExecution& uut,
    const FrameId& frameId,
    ExecutionPhase phase)
{
    if (m_stopToken->isStopRequested() ||
        m_executionControl->state() != ExecutionControlState::Running) {
        return;
    }

    const auto nextNodeId = m_scheduler->nextReadyNodeId(uut, phase);
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
        return isTerminalActivation(uut.stateOf(step.nodeId));
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
        return isTerminalActivation(uut.stateOf(m_debugStepRootNodeId));
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
