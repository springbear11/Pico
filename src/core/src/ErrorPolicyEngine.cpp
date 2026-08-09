#include "PicoATE/Core/ErrorPolicyEngine.h"

namespace PicoATE::Core {

QString errorActionName(ErrorAction action)
{
    switch (action) {
    case ErrorAction::Inherit:
        return "Inherit";
    case ErrorAction::Continue:
        return "Continue";
    case ErrorAction::StopUut:
        return "StopUut";
    case ErrorAction::Retry:
        return "Retry";
    case ErrorAction::RunCleanup:
        return "RunCleanup";
    case ErrorAction::Abort:
        return "Abort";
    }
    return "StopUut";
}

ErrorPolicyEngine::ErrorPolicyEngine(FailureHandlingMode failureHandling)
    : m_failureHandling(failureHandling)
{
}

FailureHandlingMode ErrorPolicyEngine::failureHandling() const
{
    return m_failureHandling;
}

ErrorDecision ErrorPolicyEngine::decide(const ExecNode& node,
                                        const NodeResult& result,
                                        int completedAttempts,
                                        std::optional<ErrorAction> inheritedAction) const
{
    if (result.outcome == NodeOutcome::Passed || result.outcome == NodeOutcome::Skipped) {
        return {ErrorAction::Continue, {}, CleanupReason::NormalCompletion, "node passed"};
    }

    if (result.outcome == NodeOutcome::Cancelled) {
        return {ErrorAction::StopUut, {}, CleanupReason::UserStop, "node cancelled"};
    }

    if (completedAttempts < node.retry.maxAttempts) {
        return {ErrorAction::Retry, {}, CleanupReason::StepFailed, "retry allowed"};
    }

    CleanupReason cleanupReason = CleanupReason::StepFailed;
    ErrorAction configuredAction = node.errorPolicy.onFail;
    QString policyReason = QStringLiteral("configured onFail policy");
    if (result.outcome == NodeOutcome::Timeout) {
        cleanupReason = CleanupReason::Timeout;
        configuredAction = node.errorPolicy.onTimeout;
        policyReason = QStringLiteral("configured onTimeout policy");
    } else if (result.outcome == NodeOutcome::Error) {
        cleanupReason = CleanupReason::ModuleError;
        configuredAction = node.errorPolicy.onError;
        policyReason = QStringLiteral("configured onError policy");
    }

    if (configuredAction == ErrorAction::Inherit) {
        if (inheritedAction && *inheritedAction != ErrorAction::Inherit) {
            configuredAction = *inheritedAction;
            policyReason = QStringLiteral("inherited container %1 policy")
                               .arg(result.outcome == NodeOutcome::Timeout
                                        ? QStringLiteral("onTimeout")
                                        : result.outcome == NodeOutcome::Error
                                            ? QStringLiteral("onError")
                                            : QStringLiteral("onFail"));
        } else if (m_failureHandling == FailureHandlingMode::Continue) {
            configuredAction = ErrorAction::Continue;
            policyReason = QStringLiteral("inherited station continue policy");
        } else {
            configuredAction = ErrorAction::StopUut;
            policyReason = QStringLiteral("inherited station stop policy");
        }
    }

    // A runtime without Station context remains fail-safe.
    if (configuredAction == ErrorAction::Inherit) {
        configuredAction = ErrorAction::StopUut;
        policyReason = QStringLiteral("unresolved inherited policy");
    }

    if (configuredAction == ErrorAction::Retry &&
        completedAttempts < node.retry.maxAttempts) {
        return {ErrorAction::Retry, {}, cleanupReason, "retry requested by policy"};
    }

    if (configuredAction == ErrorAction::Retry) {
        return {node.errorPolicy.stopUutOnFailure
                    ? ErrorAction::StopUut
                    : ErrorAction::Continue,
                {},
                cleanupReason,
                "retry attempts exhausted"};
    }

    if (configuredAction == ErrorAction::RunCleanup) {
        return {ErrorAction::RunCleanup,
                node.errorPolicy.cleanupRegionId,
                cleanupReason,
                QStringLiteral("run cleanup by policy")};
    }

    if (configuredAction == ErrorAction::Continue) {
        return {ErrorAction::Continue, {}, cleanupReason, "continue by policy"};
    }

    return {node.errorPolicy.stopUutOnFailure ? configuredAction : ErrorAction::Continue,
            {},
            cleanupReason,
            QStringLiteral("%1: %2")
                .arg(policyReason, errorActionName(configuredAction))};
}

} // namespace PicoATE::Core
