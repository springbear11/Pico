#pragma once

#include "PicoATE/Core/ExecutionPlan.h"
#include "PicoATE/Core/OperatorPrompt.h"
#include "PicoATE/Core/RuntimeTypes.h"

#include <QHash>
#include <QVector>

#include <chrono>
#include <optional>

namespace PicoATE::Core {

struct OperatorPromptPendingRequest {
    RequestId requestId;
    QString instanceId;
    UutId uutId;
    FrameId frameId;
    NodeId nodeId;
    AttemptId attemptId;
    ResourceLeaseId leaseId;
    OperatorPromptMode mode = OperatorPromptMode::Confirm;
    OperatorPromptResponse acceptedResponse = OperatorPromptResponse::Confirmed;
    OperatorPromptResponse rejectedResponse = OperatorPromptResponse::None;
    QVariantMap promptDetails;
    QDateTime startedAt;
    bool timeoutEnabled = false;
    std::chrono::steady_clock::time_point deadline;
};

struct OperatorPromptResolution {
    OperatorPromptPendingRequest pending;
    OperatorPromptWaitStatus waitStatus = OperatorPromptWaitStatus::Pending;
    NodeResult result;
    QString closeReason;
    bool keepOpen = false;
};

struct OperatorPromptClosure {
    QString instanceId;
    UutId uutId;
    NodeId sourceNodeId;
    QString reason;
    NodeId closedByNodeId;
    NodeOutcome outcome = NodeOutcome::Passed;
    QString message;
};

class OperatorPromptRuntimeCoordinator {
public:
    OperatorPromptRuntimeCoordinator(const ExecutionPlan& plan,
                                     OperatorPromptController* controller);

    bool hasPendingRequests() const;
    bool hasPendingRequestForUut(const UutId& uutId) const;
    bool waitForChange(std::chrono::milliseconds maximumWait);

    void registerPending(const UutId& uutId,
                         const FrameId& frameId,
                         const ExecNode& node,
                         const NodeAttempt& attempt,
                         const NodeResult& waitingResult,
                         const ResourceLeaseId& leaseId);
    std::optional<OperatorPromptPendingRequest> cancelPending(
        const UutId& uutId,
        const NodeId& nodeId,
        const FrameId& frameId);
    QVector<OperatorPromptPendingRequest> discardObsolete(
        const UutExecution& uut);
    std::optional<OperatorPromptResolution> takeReady(
        const UutId& uutId,
        const FrameId& frameId,
        std::optional<ExecutionPhase> phase = std::nullopt);

    void trackNotice(const UutId& uutId,
                     const ExecNode& node,
                     const NodeResult& result);
    QVector<OperatorPromptClosure> takeClosuresForNode(
        const UutId& uutId,
        const ExecNode& completedNode,
        const NodeResult& result);
    QVector<OperatorPromptClosure> takeClosuresForSubtree(
        const UutId& uutId,
        const NodeId& rootNodeId,
        const QString& reason);
    QVector<OperatorPromptClosure> takeAllClosures(const QString& reason);

private:
    struct ActiveOperatorPrompt {
        QString instanceId;
        UutId uutId;
        NodeId sourceNodeId;
        NodeId closeTargetNodeId;
        QString dialogKey;
    };

    NodeId closeTargetForNode(const ExecNode& node) const;
    bool isNodeOrDescendantOf(const NodeId& nodeId,
                              const NodeId& rootNodeId) const;

    const ExecutionPlan& m_plan;
    OperatorPromptController* m_controller = nullptr;
    QVector<ActiveOperatorPrompt> m_activePrompts;
    QHash<RequestId, OperatorPromptPendingRequest> m_pendingRequests;
};

} // namespace PicoATE::Core
