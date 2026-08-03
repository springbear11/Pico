#pragma once

#include "PicoATE/Core/ExecutionRequest.h"

#include <QDateTime>
#include <QHash>

#include <chrono>
#include <condition_variable>
#include <mutex>
#include <optional>

namespace PicoATE::Core {

struct TimerRequest {
    RequestId requestId;
    UutId uutId;
    FrameId frameId;
    NodeId nodeId;
    ActivationId activationId;
    AttemptId attemptId;
    int durationMs = 0;
    QDateTime startedAt;
};

struct TimerCompletion {
    RequestId requestId;
    UutId uutId;
    FrameId frameId;
    NodeId nodeId;
    ActivationId activationId;
    AttemptId attemptId;
    QDateTime finishedAt;
};

class TimerService {
public:
    bool schedule(TimerRequest request);
    bool cancel(const RequestId& requestId);
    std::optional<TimerCompletion> takeReadyForContext(const UutId& uutId,
                                                       const FrameId& frameId);
    std::optional<TimerCompletion> takeAnyReady();

    bool hasPendingRequests() const;
    bool hasPendingRequestForUut(const UutId& uutId) const;
    int pendingRequestCount() const;

    // The execution thread sleeps only when no UUT has runnable work. The
    // bounded wait keeps externally-owned StopToken requests responsive.
    bool waitForNextDeadline(std::chrono::milliseconds maximumWait);

private:
    using Clock = std::chrono::steady_clock;

    struct PendingTimer {
        TimerRequest request;
        Clock::time_point deadline;
    };

    mutable std::mutex m_mutex;
    std::condition_variable m_changed;
    QHash<RequestId, PendingTimer> m_pending;
};

} // namespace PicoATE::Core
