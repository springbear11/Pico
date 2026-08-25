#include "PicoATE/Core/SharedExecutionController.h"

namespace PicoATE::Core {

SharedExecutionController::SharedExecutionController(const ExecutionPlan& plan)
{
    Q_UNUSED(plan);
}

void SharedExecutionController::setCohortUuts(const QSet<UutId>& uutIds)
{
    m_cohortUuts = uutIds;
    m_instances.clear();
}

SharedExecutionArrivalDecision SharedExecutionController::arrive(
    const ExecNode& node,
    UutExecution& uut,
    const FrameId& frameId)
{
    SharedExecutionArrivalDecision decision;
    if (node.executionScope != NodeExecutionScope::OncePerBatch) {
        return decision;
    }

    decision.applies = true;
    auto& instance = m_instances[instanceKey(node.id, frameId)];
    if (instance.rootNodeId.isEmpty()) {
        instance.rootNodeId = node.id;
        instance.frameId = frameId;
        instance.expectedUuts = m_cohortUuts;
        if (instance.expectedUuts.isEmpty()) {
            instance.expectedUuts.insert(uut.uutId);
        }
    }
    instance.executions.insert(uut.uutId, &uut);

    if (!instance.arrivalOrder.contains(uut.uutId)) {
        instance.arrivalOrder.push_back(uut.uutId);
    }

    auto& activation = uut.ensureActivation(node.id, frameId);
    if (!instance.ready) {
        activation.state = ActivationState::WaitingAtBarrier;
    }

    decision.leaderUutId = instance.leaderUutId;
    decision.execute = instance.ready && !instance.completed &&
                       instance.leaderUutId == uut.uutId;
    decision.waiting = !decision.execute && !instance.completed;
    return decision;
}

SharedExecutionUpdate SharedExecutionController::synchronize(
    const QVector<UutExecution*>& uuts)
{
    QHash<UutId, UutExecution*> known;
    for (auto* uut : uuts) {
        if (uut) {
            known.insert(uut->uutId, uut);
        }
    }

    SharedExecutionUpdate update;
    for (auto it = m_instances.begin(); it != m_instances.end(); ++it) {
        auto& instance = it.value();
        if (instance.completed) {
            continue;
        }
        for (auto knownIt = known.cbegin(); knownIt != known.cend(); ++knownIt) {
            instance.executions.insert(knownIt.key(), knownIt.value());
        }

        if (!instance.ready) {
            for (const auto& uutId : instance.expectedUuts) {
                const auto execution = instance.executions.value(uutId, nullptr);
                if (execution &&
                    isTerminalActivation(execution->stateOf(instance.rootNodeId))) {
                    instance.droppedUuts.insert(uutId);
                }
            }
            releaseIfReady(instance, update.readyLeaders);
        }

        if (auto completion = completionIfReady(instance)) {
            update.completions.push_back(std::move(*completion));
        }
    }
    return update;
}

QString SharedExecutionController::instanceKey(const NodeId& nodeId,
                                               const FrameId& frameId) const
{
    return QStringLiteral("%1\x1f%2").arg(frameId, nodeId);
}

bool SharedExecutionController::releaseIfReady(
    Instance& instance,
    QVector<SharedExecutionLeaderReady>& readyLeaders)
{
    if (instance.ready || instance.completed) {
        return false;
    }

    QSet<UutId> activeExpected = instance.expectedUuts;
    activeExpected.subtract(instance.droppedUuts);
    if (activeExpected.isEmpty()) {
        instance.completed = true;
        return false;
    }
    for (const auto& uutId : activeExpected) {
        if (!instance.arrivalOrder.contains(uutId)) {
            return false;
        }
    }

    for (const auto& uutId : instance.arrivalOrder) {
        if (activeExpected.contains(uutId)) {
            instance.leaderUutId = uutId;
            break;
        }
    }
    auto* leader = instance.executions.value(instance.leaderUutId, nullptr);
    if (!leader) {
        return false;
    }

    instance.ready = true;
    auto& activation = leader->ensureActivation(instance.rootNodeId,
                                                instance.frameId);
    activation.state = ActivationState::Ready;
    readyLeaders.push_back(
        {instance.rootNodeId, instance.frameId, leader});
    return true;
}

std::optional<SharedExecutionCompletion>
SharedExecutionController::completionIfReady(Instance& instance)
{
    if (!instance.ready || instance.completed) {
        return std::nullopt;
    }
    auto* leader = instance.executions.value(instance.leaderUutId, nullptr);
    if (!leader ||
        !isTerminalActivation(leader->stateOf(instance.rootNodeId))) {
        return std::nullopt;
    }

    SharedExecutionCompletion completion;
    completion.rootNodeId = instance.rootNodeId;
    completion.frameId = instance.frameId;
    completion.leader = leader;
    for (const auto& uutId : instance.arrivalOrder) {
        if (uutId == instance.leaderUutId ||
            instance.droppedUuts.contains(uutId)) {
            continue;
        }
        auto* execution = instance.executions.value(uutId, nullptr);
        if (!execution ||
            isTerminalActivation(execution->stateOf(instance.rootNodeId))) {
            continue;
        }
        completion.recipients.push_back(execution);
    }
    instance.completed = true;
    return completion;
}

} // namespace PicoATE::Core
