#pragma once

#include "PicoATE/Core/RuntimeTypes.h"

namespace PicoATE::Core {

QString errorActionName(ErrorAction action);

enum class FailureHandlingMode {
    UseNodePolicy,
    Stop,
    Continue
};

struct ErrorDecision {
    ErrorAction action = ErrorAction::Continue;
    CleanupRegionId cleanupRegionId;
    CleanupReason cleanupReason = CleanupReason::StepFailed;
    QString reason;
};

class ErrorPolicyEngine {
public:
    explicit ErrorPolicyEngine(
        FailureHandlingMode failureHandling = FailureHandlingMode::UseNodePolicy);

    ErrorDecision decide(const ExecNode& node,
                          const NodeResult& result,
                          int completedAttempts) const;

    FailureHandlingMode failureHandling() const;

private:
    FailureHandlingMode m_failureHandling = FailureHandlingMode::UseNodePolicy;
};

} // namespace PicoATE::Core
