#include "PicoATE/Core/LoopController.h"

namespace PicoATE::Core {

bool LoopController::controllerReady(const LoopRegion& region, const UutExecution& uut) const
{
    const auto it = m_states.constFind(stateKey(uut.uutId, region.id));
    if (it == m_states.constEnd()) {
        return true;
    }
    return !it->completed && bodyComplete(region, uut);
}

bool LoopController::bodyNodeMayRun(const LoopRegion& region,
                                    const UutExecution& uut,
                                    const NodeId& nodeId) const
{
    if (!region.bodyNodes.contains(nodeId)) {
        return true;
    }

    const auto it = m_states.constFind(stateKey(uut.uutId, region.id));
    return it != m_states.constEnd() && it->started && !it->completed;
}

LoopControllerResult LoopController::advance(const LoopRegion& region, UutExecution& uut)
{
    auto& state = m_states[stateKey(uut.uutId, region.id)];
    return region.type == LoopType::While
        ? advanceWhile(region, uut, state)
        : advanceFor(region, uut, state);
}

void LoopController::requestBreak(const LoopRegion& region,
                                  const UutId& uutId,
                                  const NodeId& breakNodeId)
{
    auto& state = m_states[stateKey(uutId, region.id)];
    if (!state.completed) {
        state.breakRequested = true;
        state.breakNodeId = breakNodeId;
    }
}

LoopControllerResult LoopController::advanceFor(const LoopRegion& region,
                                                UutExecution& uut,
                                                LoopRuntimeState& state)
{
    if (!state.started) {
        state.values = iterationValues(region.forLoop);
        state.started = true;
        state.currentIndex = -1;
    } else {
        if (!bodyComplete(region, uut)) {
            return {};
        }
        if (state.breakRequested) {
            state.completed = true;
            LoopControllerResult result;
            result.progressed = true;
            result.outcome = NodeOutcome::Passed;
            result.message = QString("loop exited by %1 after %2 iteration(s)")
                                 .arg(state.breakNodeId)
                                 .arg(state.currentIndex + 1);
            result.outputs.insert("iterations", state.currentIndex + 1);
            result.outputs.insert("exitReason", "break");
            result.outputs.insert("breakNodeId", state.breakNodeId);
            publishLoopVariables(uut, result.outputs);
            return result;
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
        LoopControllerResult result;
        result.progressed = true;
        result.outcome = state.aggregateOutcome;
        result.skippedBody = state.values.isEmpty();
        result.message = message;
        result.outputs.insert("iterations", state.values.size());
        result.outputs.insert("exitReason", "range-complete");
        publishLoopVariables(uut, result.outputs);
        return result;
    }

    state.breakRequested = false;
    state.breakNodeId.clear();
    uut.variables.insert(region.forLoop.variableName, state.values[state.currentIndex]);
    uut.variables.insert("loop.index", state.currentIndex);
    uut.variables.insert("loop.number", state.currentIndex + 1);
    uut.variables.insert("loop.value", state.values[state.currentIndex]);
    uut.variables.insert("loop.variable", region.forLoop.variableName);
    resetBody(region, uut);

    LoopControllerResult result;
    result.progressed = true;
    result.message = QString("loop iteration %1, %2=%3")
                         .arg(state.currentIndex + 1)
                         .arg(region.forLoop.variableName)
                         .arg(state.values[state.currentIndex]);
    return result;
}

LoopControllerResult LoopController::advanceWhile(const LoopRegion& region,
                                                  UutExecution& uut,
                                                  LoopRuntimeState& state)
{
    const auto& spec = region.whileLoop;
    if (!state.started) {
        state.started = true;
        state.startedAt = QDateTime::currentDateTimeUtc();
        state.currentIndex = -1;
    } else {
        if (!bodyComplete(region, uut)) {
            return {};
        }

        const auto now = QDateTime::currentDateTimeUtc();
        const qint64 elapsedMs = qMax<qint64>(0, state.startedAt.msecsTo(now));
        if (state.breakRequested) {
            state.completed = true;
            auto outputs = whileOutputs(state, elapsedMs);
            outputs.insert("exitReason", "break");
            outputs.insert("breakNodeId", state.breakNodeId);
            publishLoopVariables(uut, outputs);

            LoopControllerResult result;
            result.progressed = true;
            result.outcome = NodeOutcome::Passed;
            result.message = QString("while loop exited by %1 after %2 iteration(s)")
                                 .arg(state.breakNodeId)
                                 .arg(state.currentIndex + 1);
            result.outputs = outputs;
            return result;
        }

        const auto summary = summarizeBody(region, uut);
        const bool failedIteration = summary.outcome != NodeOutcome::Passed;
        const bool mustAbort = summary.outcome == NodeOutcome::Cancelled ||
            (failedIteration &&
             spec.iterationErrorPolicy == WhileIterationErrorPolicy::AbortLoop);
        if (mustAbort) {
            state.completed = true;
            auto outputs = whileOutputs(state, elapsedMs);
            outputs.insert("exitReason", "iteration-failed");
            publishLoopVariables(uut, outputs);

            LoopControllerResult result;
            result.progressed = true;
            result.outcome = summary.outcome;
            result.errorCode = summary.outcome == NodeOutcome::Timeout
                ? "WhileLoopIterationTimeout"
                : summary.outcome == NodeOutcome::Cancelled
                    ? "WhileLoopCancelled"
                    : summary.outcome == NodeOutcome::Failed
                        ? "WhileLoopIterationFailed"
                        : "WhileLoopIterationError";
            result.message = summary.failedChildren.isEmpty()
                ? QString("while loop iteration failed")
                : QString("while loop iteration failed: %1")
                      .arg(summary.failedChildren.join(", "));
            result.outputs = outputs;
            return result;
        }

        if (spec.timeoutMs > 0 && elapsedMs >= spec.timeoutMs) {
            state.completed = true;
            auto outputs = whileOutputs(state, elapsedMs);
            outputs.insert("exitReason", "timeout");
            publishLoopVariables(uut, outputs);

            LoopControllerResult result;
            result.progressed = true;
            result.outcome = NodeOutcome::Timeout;
            result.errorCode = "WhileLoopTimeout";
            result.message = QString("while loop timed out after %1 ms").arg(elapsedMs);
            result.outputs = outputs;
            return result;
        }
        if (spec.maxIterations > 0 && state.currentIndex + 1 >= spec.maxIterations) {
            state.completed = true;
            auto outputs = whileOutputs(state, elapsedMs);
            outputs.insert("exitReason", "maximum-iterations");
            publishLoopVariables(uut, outputs);

            LoopControllerResult result;
            result.progressed = true;
            result.outcome = NodeOutcome::Failed;
            result.errorCode = "WhileLoopMaxIterations";
            result.message = QString("while loop reached %1 iteration(s) without Break If")
                                 .arg(spec.maxIterations);
            result.outputs = outputs;
            return result;
        }
    }

    ++state.currentIndex;
    state.breakRequested = false;
    state.breakNodeId.clear();
    uut.variables.insert("loop.index", state.currentIndex);
    uut.variables.insert("loop.number", state.currentIndex + 1);
    uut.variables.insert("loop.value", state.currentIndex + 1);
    uut.variables.insert("loop.variable", "iteration");
    resetBody(region, uut);

    LoopControllerResult result;
    result.progressed = true;
    result.message = QString("while loop iteration %1 started").arg(state.currentIndex + 1);
    result.delayBeforeNextMs = state.currentIndex > 0 ? spec.intervalMs : 0;
    result.outputs = whileOutputs(
        state,
        qMax<qint64>(0, state.startedAt.msecsTo(QDateTime::currentDateTimeUtc())));
    return result;
}

void LoopController::reset(const LoopRegion& region, const UutId& uutId)
{
    m_states.remove(stateKey(uutId, region.id));
}

LoopController::LoopBodySummary LoopController::summarizeBody(
    const LoopRegion& region,
    const UutExecution& uut) const
{
    LoopBodySummary summary;
    for (const auto& childNodeId : region.childNodeIds) {
        const auto outcome = uut.outcomeOf(childNodeId);
        if (outcome == NodeOutcome::Passed) {
            continue;
        }
        summary.failedChildren.push_back(
            QString("%1=%2").arg(childNodeId, nodeOutcomeName(outcome)));
        if (outcome == NodeOutcome::Error) {
            summary.outcome = NodeOutcome::Error;
        } else if (outcome == NodeOutcome::Timeout &&
                   summary.outcome != NodeOutcome::Error) {
            summary.outcome = NodeOutcome::Timeout;
        } else if (outcome == NodeOutcome::Cancelled &&
                   summary.outcome != NodeOutcome::Error &&
                   summary.outcome != NodeOutcome::Timeout) {
            summary.outcome = NodeOutcome::Cancelled;
        } else if (summary.outcome == NodeOutcome::Passed) {
            summary.outcome = NodeOutcome::Failed;
        }
    }
    return summary;
}

void LoopController::aggregateBodyResult(const LoopRegion& region,
                                         const UutExecution& uut,
                                         LoopRuntimeState& state) const
{
    const auto summary = summarizeBody(region, uut);
    for (const auto& failedChild : summary.failedChildren) {
        state.failedChildren.push_back(
            QString("iteration %1 %2").arg(state.currentIndex + 1).arg(failedChild));
    }
    if (summary.outcome == NodeOutcome::Error) {
        state.aggregateOutcome = NodeOutcome::Error;
    } else if (summary.outcome == NodeOutcome::Timeout &&
               state.aggregateOutcome != NodeOutcome::Error) {
        state.aggregateOutcome = NodeOutcome::Timeout;
    } else if (summary.outcome == NodeOutcome::Cancelled &&
               state.aggregateOutcome != NodeOutcome::Error &&
               state.aggregateOutcome != NodeOutcome::Timeout) {
        state.aggregateOutcome = NodeOutcome::Cancelled;
    } else if (summary.outcome != NodeOutcome::Passed &&
               state.aggregateOutcome == NodeOutcome::Passed) {
        state.aggregateOutcome = NodeOutcome::Failed;
    }
}

QVariantMap LoopController::whileOutputs(const LoopRuntimeState& state,
                                         qint64 elapsedMs) const
{
    QVariantMap outputs;
    outputs.insert("iterations", state.currentIndex + 1);
    outputs.insert("elapsedMs", elapsedMs);
    return outputs;
}

void LoopController::publishLoopVariables(UutExecution& uut,
                                          const QVariantMap& outputs) const
{
    for (auto it = outputs.constBegin(); it != outputs.constEnd(); ++it) {
        uut.variables.insert(QString("loop.%1").arg(it.key()), it.value());
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
        if (it == uut.activations.end() || !isTerminalActivation(it->state)) {
            continue;
        }
        it->retryAttemptBase = it->attempts.size();
        it->state = ActivationState::Created;
        it->preNodeSnapshot = {};
        it->postNodeSnapshot = {};
        it->completedAt = {};
    }
}

} // namespace PicoATE::Core
