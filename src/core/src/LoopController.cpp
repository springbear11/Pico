#include "PicoATE/Core/LoopController.h"

namespace PicoATE::Core {

bool LoopController::controllerReady(const LoopRegion& region, const UutExecution& uut) const
{
    const auto it = m_states.constFind(stateKey(uut.uutId, region.id));
    if (it == m_states.constEnd()) {
        return true;
    }
    if (it.value().completed) {
        return false;
    }
    return bodyComplete(region, uut);
}

bool LoopController::bodyNodeMayRun(const LoopRegion& region,
                                    const UutExecution& uut,
                                    const NodeId& nodeId) const
{
    if (!region.bodyNodes.contains(nodeId)) {
        return true;
    }

    const auto it = m_states.constFind(stateKey(uut.uutId, region.id));
    return it != m_states.constEnd() && it.value().started && !it.value().completed;
}

LoopControllerResult LoopController::advance(const LoopRegion& region, UutExecution& uut)
{
    auto& state = m_states[stateKey(uut.uutId, region.id)];
    if (!state.started) {
        state.values = iterationValues(region.forLoop);
        state.started = true;
        state.currentIndex = -1;
    } else {
        if (!bodyComplete(region, uut)) {
            return {};
        }
        if (state.currentIndex >= 0) {
            aggregateBodyResult(region, uut, state);
        }
    }

    ++state.currentIndex;
    if (state.currentIndex >= state.values.size()) {
        state.completed = true;
        const auto message = state.failedChildren.isEmpty()
            ? (state.values.isEmpty() ? QString("loop skipped") : QString("loop complete"))
            : QString("loop child result: %1").arg(state.failedChildren.join(", "));
        return {true,
                state.aggregateOutcome,
                state.values.isEmpty(),
                message};
    }

    uut.variables.insert(region.forLoop.variableName, state.values[state.currentIndex]);
    uut.variables.insert("loop.index", state.currentIndex);
    uut.variables.insert("loop.number", state.currentIndex + 1);
    uut.variables.insert("loop.value", state.values[state.currentIndex]);
    uut.variables.insert("loop.variable", region.forLoop.variableName);
    resetBody(region, uut);
    return {
        true,
        NodeOutcome::Unknown,
        false,
        QString("loop iteration %1, %2=%3")
            .arg(state.currentIndex + 1)
            .arg(region.forLoop.variableName)
            .arg(state.values[state.currentIndex]),
    };
}

void LoopController::aggregateBodyResult(const LoopRegion& region,
                                         const UutExecution& uut,
                                         LoopRuntimeState& state) const
{
    for (const auto& childNodeId : region.childNodeIds) {
        const auto outcome = uut.outcomeOf(childNodeId);
        if (outcome == NodeOutcome::Passed) {
            continue;
        }
        state.failedChildren.push_back(
            QString("iteration %1 %2=%3")
                .arg(state.currentIndex + 1)
                .arg(childNodeId, nodeOutcomeName(outcome)));
        if (outcome == NodeOutcome::Error) {
            state.aggregateOutcome = NodeOutcome::Error;
        } else if (outcome == NodeOutcome::Timeout &&
                   state.aggregateOutcome != NodeOutcome::Error) {
            state.aggregateOutcome = NodeOutcome::Timeout;
        } else if (outcome == NodeOutcome::Cancelled &&
                   state.aggregateOutcome != NodeOutcome::Error &&
                   state.aggregateOutcome != NodeOutcome::Timeout) {
            state.aggregateOutcome = NodeOutcome::Cancelled;
        } else if (state.aggregateOutcome == NodeOutcome::Passed) {
            state.aggregateOutcome = NodeOutcome::Failed;
        }
    }
}

QString LoopController::stateKey(const UutId& uutId, const LoopId& loopId) const
{
    return QString("%1:%2").arg(uutId, loopId);
}

QVector<int> LoopController::iterationValues(const ForLoopSpec& spec) const
{
    QVector<int> values;
    if (spec.step == 0) {
        return values;
    }

    if (spec.step > 0) {
        for (int value = spec.from; value <= spec.to; value += spec.step) {
            values.push_back(value);
        }
        return values;
    }

    for (int value = spec.from; value >= spec.to; value += spec.step) {
        values.push_back(value);
    }
    return values;
}

bool LoopController::bodyComplete(const LoopRegion& region, const UutExecution& uut) const
{
    for (const auto& nodeId : region.bodyNodes) {
        if (!isTerminalActivation(uut.stateOf(nodeId))) {
            return false;
        }
    }
    return true;
}

void LoopController::resetBody(const LoopRegion& region, UutExecution& uut) const
{
    for (const auto& nodeId : region.bodyNodes) {
        auto it = uut.activations.find(nodeId);
        if (it == uut.activations.end()) {
            continue;
        }
        if (!isTerminalActivation(it.value().state)) {
            continue;
        }
        it.value().state = ActivationState::Created;
        it.value().completedAt = {};
    }
}

} // namespace PicoATE::Core
