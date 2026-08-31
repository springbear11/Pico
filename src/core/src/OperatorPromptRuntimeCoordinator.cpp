#include "PicoATE/Core/OperatorPromptRuntimeCoordinator.h"

#include <algorithm>
#include <utility>

namespace PicoATE::Core {

OperatorPromptRuntimeCoordinator::OperatorPromptRuntimeCoordinator(
    const ExecutionPlan& plan,
    OperatorPromptController* controller)
    : m_plan(plan)
    , m_controller(controller)
{
}

bool OperatorPromptRuntimeCoordinator::hasPendingRequests() const
{
    return !m_pendingRequests.isEmpty();
}

bool OperatorPromptRuntimeCoordinator::hasPendingRequestForUut(
    const UutId& uutId) const
{
    return std::any_of(
        m_pendingRequests.cbegin(),
        m_pendingRequests.cend(),
        [&uutId](const OperatorPromptPendingRequest& pending) {
            return pending.uutId == uutId;
        });
}

bool OperatorPromptRuntimeCoordinator::waitForChange(
    std::chrono::milliseconds maximumWait)
{
    if (!m_controller || m_pendingRequests.isEmpty()) {
        return false;
    }
    m_controller->waitForChange(maximumWait);
    return true;
}

void OperatorPromptRuntimeCoordinator::registerPending(
    const UutId& uutId,
    const FrameId& frameId,
    const ExecNode& node,
    const NodeAttempt& attempt,
    const NodeResult& waitingResult,
    const ResourceLeaseId& leaseId)
{
    OperatorPromptPendingRequest pending;
    pending.requestId = attempt.requestId;
    pending.instanceId = waitingResult.outputs.value(
        QStringLiteral("promptInstanceId")).toString();
    pending.uutId = uutId;
    pending.frameId = frameId;
    pending.nodeId = node.id;
    pending.attemptId = attempt.id;
    pending.leaseId = leaseId;
    pending.mode = operatorPromptModeFromName(
        waitingResult.outputs.value(
            QStringLiteral("mode"), QStringLiteral("confirm")).toString());
    pending.acceptedResponse = pending.mode == OperatorPromptMode::Notice
        ? OperatorPromptResponse::Shown
        : (pending.mode == OperatorPromptMode::Judgment
               ? OperatorPromptResponse::Passed
               : (pending.mode == OperatorPromptMode::Input
                      ? OperatorPromptResponse::Submitted
                      : OperatorPromptResponse::Confirmed));
    pending.rejectedResponse = pending.mode == OperatorPromptMode::Judgment
        ? OperatorPromptResponse::Failed
        : OperatorPromptResponse::None;
    pending.promptDetails = waitingResult.outputs;
    pending.startedAt = waitingResult.startedAt;

    int timeoutMs = pending.promptDetails.value(
        QStringLiteral("timeoutMs"), 60000).toInt();
    if (pending.mode == OperatorPromptMode::Notice) {
        timeoutMs = timeoutMs > 0 ? qMin(timeoutMs, 5000) : 5000;
    }
    pending.timeoutEnabled = timeoutMs > 0;
    pending.deadline = std::chrono::steady_clock::now() +
        std::chrono::milliseconds(qMax(0, timeoutMs));
    m_pendingRequests.insert(pending.requestId, std::move(pending));
}

std::optional<OperatorPromptPendingRequest>
OperatorPromptRuntimeCoordinator::cancelPending(
    const UutId& uutId,
    const NodeId& nodeId,
    const FrameId& frameId)
{
    const auto pendingIt = std::find_if(
        m_pendingRequests.begin(),
        m_pendingRequests.end(),
        [&uutId, &nodeId, &frameId](const OperatorPromptPendingRequest& pending) {
            return pending.uutId == uutId && pending.nodeId == nodeId &&
                   pending.frameId == frameId;
        });
    if (pendingIt == m_pendingRequests.end()) {
        return std::nullopt;
    }

    const auto pending = pendingIt.value();
    m_pendingRequests.erase(pendingIt);
    if (m_controller) {
        m_controller->cancelPrompt(pending.instanceId);
    }
    return pending;
}

QVector<OperatorPromptPendingRequest>
OperatorPromptRuntimeCoordinator::discardObsolete(const UutExecution& uut)
{
    QVector<RequestId> obsoleteIds;
    for (auto it = m_pendingRequests.cbegin();
         it != m_pendingRequests.cend(); ++it) {
        if (it->uutId != uut.uutId) {
            continue;
        }
        const auto activation = uut.activations.constFind(it->nodeId);
        const bool attemptStillRunning = activation != uut.activations.constEnd() &&
            std::any_of(
                activation->attempts.cbegin(),
                activation->attempts.cend(),
                [&pending = *it](const NodeAttempt& attempt) {
                    return attempt.requestId == pending.requestId &&
                           attempt.id == pending.attemptId &&
                           attempt.state == AttemptState::Running;
                });
        if (activation == uut.activations.constEnd() ||
            activation->state != ActivationState::Running ||
            !attemptStillRunning) {
            obsoleteIds.push_back(it.key());
        }
    }

    QVector<OperatorPromptPendingRequest> obsolete;
    obsolete.reserve(obsoleteIds.size());
    for (const auto& requestId : obsoleteIds) {
        const auto pending = m_pendingRequests.take(requestId);
        if (m_controller) {
            m_controller->cancelPrompt(pending.instanceId);
        }
        obsolete.push_back(pending);
    }
    return obsolete;
}

std::optional<OperatorPromptResolution>
OperatorPromptRuntimeCoordinator::takeReady(
    const UutId& uutId,
    const FrameId& frameId,
    std::optional<ExecutionPhase> phase)
{
    if (!m_controller) {
        return std::nullopt;
    }

    const auto now = std::chrono::steady_clock::now();
    for (auto pendingIt = m_pendingRequests.begin();
         pendingIt != m_pendingRequests.end(); ++pendingIt) {
        if (pendingIt->uutId != uutId || pendingIt->frameId != frameId) {
            continue;
        }
        const auto* node = m_plan.node(pendingIt->nodeId);
        if (!node || (phase && executionPhaseOf(*node) != *phase)) {
            continue;
        }

        QVariantMap responseValues;
        auto waitStatus = m_controller->takeResponse(
            pendingIt->instanceId,
            pendingIt->acceptedResponse,
            pendingIt->rejectedResponse,
            &responseValues);
        if (waitStatus == OperatorPromptWaitStatus::Pending &&
            pendingIt->timeoutEnabled && now >= pendingIt->deadline) {
            m_controller->cancelPrompt(pendingIt->instanceId);
            waitStatus = OperatorPromptWaitStatus::Timeout;
        }
        if (waitStatus == OperatorPromptWaitStatus::Pending) {
            continue;
        }

        OperatorPromptResolution resolution;
        resolution.pending = pendingIt.value();
        resolution.waitStatus = waitStatus;
        m_pendingRequests.erase(pendingIt);

        auto& result = resolution.result;
        result.nodeId = node->id;
        result.startedAt = resolution.pending.startedAt;
        switch (waitStatus) {
        case OperatorPromptWaitStatus::Accepted:
            result.outcome = NodeOutcome::Passed;
            result.outputs = resolution.pending.promptDetails;
            if (resolution.pending.mode == OperatorPromptMode::Input) {
                if (!normalizeOperatorPromptInput(node->payload,
                                                  responseValues,
                                                  result.outputs,
                                                  result.errorMessage)) {
                    result.outcome = NodeOutcome::Error;
                    result.errorCode = QStringLiteral("OperatorInputInvalid");
                    resolution.closeReason = QStringLiteral("invalid-input");
                } else {
                    resolution.closeReason = QStringLiteral("submitted");
                }
            } else {
                const auto response =
                    resolution.pending.mode == OperatorPromptMode::Judgment
                    ? QStringLiteral("pass")
                    : (resolution.pending.mode == OperatorPromptMode::Notice
                           ? QStringLiteral("shown")
                           : QStringLiteral("confirmed"));
                result.outputs.insert(QStringLiteral("response"), response);
                resolution.closeReason = response;
            }
            break;
        case OperatorPromptWaitStatus::Rejected:
            result.outcome = NodeOutcome::Failed;
            result.errorCode = node->payload.value(
                QStringLiteral("failureCode"),
                QStringLiteral("OperatorCheckFailed")).toString();
            result.errorMessage =
                QStringLiteral("Operator marked the check as failed");
            result.outputs = resolution.pending.promptDetails;
            result.outputs.insert(
                QStringLiteral("response"), QStringLiteral("fail"));
            resolution.closeReason = QStringLiteral("fail");
            break;
        case OperatorPromptWaitStatus::Timeout:
            result.outcome = NodeOutcome::Timeout;
            result.errorCode = QStringLiteral("OperatorPromptTimeout");
            result.errorMessage = QStringLiteral("Operator prompt timed out");
            resolution.closeReason = QStringLiteral("timeout");
            break;
        case OperatorPromptWaitStatus::Unavailable:
            result.outcome = NodeOutcome::Error;
            result.errorCode =
                QStringLiteral("OperatorPromptResponderUnavailable");
            result.errorMessage =
                QStringLiteral("Operator prompt responder became unavailable");
            resolution.closeReason = QStringLiteral("unavailable");
            break;
        case OperatorPromptWaitStatus::Cancelled:
            result.outcome = NodeOutcome::Cancelled;
            result.errorCode = QStringLiteral("OperatorPromptCancelled");
            result.errorMessage = QStringLiteral("Operator prompt was cancelled");
            resolution.closeReason = QStringLiteral("cancelled");
            break;
        case OperatorPromptWaitStatus::Pending:
            return std::nullopt;
        }
        result.finishedAt = QDateTime::currentDateTimeUtc();
        resolution.keepOpen =
            resolution.pending.mode == OperatorPromptMode::Notice &&
            waitStatus == OperatorPromptWaitStatus::Accepted;
        return resolution;
    }
    return std::nullopt;
}

void OperatorPromptRuntimeCoordinator::trackNotice(
    const UutId& uutId,
    const ExecNode& node,
    const NodeResult& result)
{
    const auto instanceId = result.outputs.value(
        QStringLiteral("promptInstanceId")).toString();
    if (instanceId.isEmpty()) {
        return;
    }

    ActiveOperatorPrompt prompt;
    prompt.instanceId = instanceId;
    prompt.uutId = uutId;
    prompt.sourceNodeId = node.id;
    prompt.closeTargetNodeId = closeTargetForNode(node);
    prompt.dialogKey = result.outputs.value(
        QStringLiteral("dialogKey")).toString().trimmed();
    m_activePrompts.push_back(std::move(prompt));
}

QVector<OperatorPromptClosure>
OperatorPromptRuntimeCoordinator::takeClosuresForNode(
    const UutId& uutId,
    const ExecNode& completedNode,
    const NodeResult& result)
{
    QVector<OperatorPromptClosure> closures;
    if (!isTerminalOutcome(result.outcome)) {
        return closures;
    }

    const bool completedJudgment =
        completedNode.kind == ExecNodeKind::OperatorPrompt &&
        completedNode.payload.value(QStringLiteral("mode")).toString().compare(
            QStringLiteral("judgment"), Qt::CaseInsensitive) == 0;
    const auto completedDialogKey = completedNode.payload.value(
        QStringLiteral("dialogKey")).toString().trimmed();
    for (int index = m_activePrompts.size() - 1; index >= 0; --index) {
        const auto& prompt = m_activePrompts[index];
        const bool targetCompleted =
            prompt.closeTargetNodeId == completedNode.id;
        const bool judgmentCompleted = completedJudgment &&
            !completedDialogKey.isEmpty() &&
            prompt.dialogKey == completedDialogKey;
        if (prompt.uutId != uutId ||
            prompt.sourceNodeId == completedNode.id ||
            (!targetCompleted && !judgmentCompleted)) {
            continue;
        }

        OperatorPromptClosure closure;
        closure.instanceId = prompt.instanceId;
        closure.uutId = prompt.uutId;
        closure.sourceNodeId = prompt.sourceNodeId;
        closure.reason = judgmentCompleted
            ? QStringLiteral("judgment-completed")
            : QStringLiteral("target-completed");
        closure.closedByNodeId = completedNode.id;
        closures.push_back(std::move(closure));
        m_activePrompts.removeAt(index);
    }
    return closures;
}

QVector<OperatorPromptClosure>
OperatorPromptRuntimeCoordinator::takeClosuresForSubtree(
    const UutId& uutId,
    const NodeId& rootNodeId,
    const QString& reason)
{
    QVector<OperatorPromptClosure> closures;
    for (int index = m_activePrompts.size() - 1; index >= 0; --index) {
        const auto& prompt = m_activePrompts[index];
        if (prompt.uutId != uutId ||
            !isNodeOrDescendantOf(prompt.sourceNodeId, rootNodeId)) {
            continue;
        }
        closures.push_back({prompt.instanceId,
                            prompt.uutId,
                            prompt.sourceNodeId,
                            reason,
                            {},
                            NodeOutcome::Passed,
                            {}});
        m_activePrompts.removeAt(index);
    }
    return closures;
}

QVector<OperatorPromptClosure>
OperatorPromptRuntimeCoordinator::takeAllClosures(const QString& reason)
{
    QVector<OperatorPromptClosure> closures;
    closures.reserve(m_activePrompts.size());
    for (const auto& prompt : std::as_const(m_activePrompts)) {
        closures.push_back({prompt.instanceId,
                            prompt.uutId,
                            prompt.sourceNodeId,
                            reason,
                            {},
                            NodeOutcome::Passed,
                            {}});
    }
    m_activePrompts.clear();
    return closures;
}

NodeId OperatorPromptRuntimeCoordinator::closeTargetForNode(
    const ExecNode& node) const
{
    QString requested = node.payload.value(
        QStringLiteral("closeOnStep")).toString().trimmed();
    if (requested.startsWith(QStringLiteral("step:"), Qt::CaseInsensitive)) {
        requested = requested.mid(5).trimmed();
    }
    if (!requested.isEmpty()) {
        if (m_plan.node(requested)) {
            return requested;
        }

        QVector<NodeId> matches;
        const auto parent = m_plan.structuralParentOf(node.id);
        for (auto it = m_plan.nodes.constBegin();
             it != m_plan.nodes.constEnd(); ++it) {
            const auto& candidate = it.value();
            if (candidate.localId != requested && candidate.key != requested) {
                continue;
            }
            if (parent == m_plan.structuralParentOf(candidate.id)) {
                matches.push_back(candidate.id);
            }
        }
        return matches.size() == 1 ? matches.first() : NodeId{};
    }

    if (!node.payload.value(QStringLiteral("dialogKey"))
             .toString().trimmed().isEmpty()) {
        return {};
    }

    auto edges = m_plan.outgoingEdges(node.id);
    std::sort(edges.begin(), edges.end(),
              [](const ExecEdge& left, const ExecEdge& right) {
                  return left.priority > right.priority;
              });
    for (const auto& edge : edges) {
        if (edge.kind == EdgeKind::Control &&
            (edge.trigger == EdgeTrigger::OnSuccess ||
             edge.trigger == EdgeTrigger::Always ||
             edge.trigger == EdgeTrigger::Finally)) {
            return edge.to;
        }
    }
    return {};
}

bool OperatorPromptRuntimeCoordinator::isNodeOrDescendantOf(
    const NodeId& nodeId,
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

} // namespace PicoATE::Core
