#include "PicoATE/Core/ExecutionSession.h"

#include <algorithm>

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
    if (!activation.attempts.isEmpty()) {
        report.outcome = activation.attempts.last().result.outcome;
        report.measurements = activation.attempts.last().result.measurements;
    }

    report.attempts.reserve(activation.attempts.size());
    for (const auto& attempt : activation.attempts) {
        AttemptReport attemptReport;
        attemptReport.index = attempt.attemptIndex + 1;
        attemptReport.outcome = attempt.result.outcome;
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
                                   IRuntimeEventSink* eventSink)
    : m_plan(std::move(plan))
    , m_results(m_plan)
    , m_events(m_plan.id, eventSink)
    , m_stopToken(stopToken ? std::move(stopToken) : std::make_shared<StopToken>())
    , m_runtimeServices(m_devices)
{
    m_devices.setRuntimeEventEmitter(&m_events);
    m_runner.setRuntimeServices(&m_runtimeServices);
    m_scheduler = std::make_unique<ExecutionGraphScheduler>(
        m_plan, m_resources, m_barriers, m_loops, m_errorPolicy, m_runner, m_results, &m_events);
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
}

std::shared_ptr<StopToken> ExecutionSession::stopToken() const
{
    return m_stopToken;
}

ExecutionState ExecutionSession::state() const
{
    return m_state;
}

ExecutionSessionResult ExecutionSession::run()
{
    ExecutionSessionResult result;
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
        progressed = false;

        for (auto& uut : m_uuts) {
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

void ExecutionSession::publishSessionState(const QString& message)
{
    RuntimeEvent event;
    event.kind = RuntimeEventKind::SessionStateChanged;
    event.executionState = m_state;
    event.message = message;
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
