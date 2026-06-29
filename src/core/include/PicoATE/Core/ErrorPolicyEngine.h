#pragma once

#include "PicoATE/Core/RuntimeTypes.h"

namespace PicoATE::Core {

QString errorActionName(ErrorAction action);

struct ErrorDecision {
    ErrorAction action = ErrorAction::Continue;
    CleanupRegionId cleanupRegionId;
    CleanupReason cleanupReason = CleanupReason::StepFailed;
    QString reason;
};

class ErrorPolicyEngine {
public:
    ErrorDecision decide(const ExecNode& node,
                         const NodeResult& result,
                         int completedAttempts) const;
};

} // namespace PicoATE::Core
