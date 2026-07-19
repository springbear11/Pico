#include "PicoATE/Core/ErrorPolicyEngine.h"

namespace PicoATE::Core {

QString errorActionName(ErrorAction action)
{
    switch (action) {
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
                                        int completedAttempts) const
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
    if (result.outcome == NodeOutcome::Timeout) {
        cleanupReason = CleanupReason::Timeout;
        configuredAction = node.errorPolicy.onTimeout;
    } else if (result.outcome == NodeOutcome::Error) {
        cleanupReason = CleanupReason::ModuleError;
        configuredAction = node.errorPolicy.onError;
    }

    if (m_failureHandling == FailureHandlingMode::Stop) {
        configuredAction = ErrorAction::StopUut;
    } else if (m_failureHandling == FailureHandlingMode::Continue) {
        configuredAction = ErrorAction::Continue;
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

    if (configuredAction == ErrorAction::RunCleanup ||
        (!node.errorPolicy.cleanupRegionId.isEmpty() &&
         configuredAction != ErrorAction::Continue)) {
        return {ErrorAction::RunCleanup,
                node.errorPolicy.cleanupRegionId,
                cleanupReason,
                "activate cleanup"};
    }

    if (configuredAction == ErrorAction::Continue) {
        return {ErrorAction::Continue, {}, cleanupReason, "continue by policy"};
    }

    if (m_failureHandling == FailureHandlingMode::Stop) {
        return {ErrorAction::StopUut, {}, cleanupReason, "stop by station policy"};
    }

    return {node.errorPolicy.stopUutOnFailure ? configuredAction : ErrorAction::Continue,
            {},
            cleanupReason,
            QString("configured policy: %1").arg(errorActionName(configuredAction))};
}

} // namespace PicoATE::Core
