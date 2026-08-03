#include "PicoATE/Core/TimerService.h"

#include <algorithm>

namespace PicoATE::Core {

bool TimerService::schedule(TimerRequest request)
{
    if (request.requestId.trimmed().isEmpty()) {
        return false;
    }

    const auto duration = std::chrono::milliseconds(std::max(0, request.durationMs));
    PendingTimer pending;
    pending.request = std::move(request);
    pending.deadline = Clock::now() + duration;

    {
        std::lock_guard lock(m_mutex);
        if (m_pending.contains(pending.request.requestId)) {
            return false;
        }
        m_pending.insert(pending.request.requestId, std::move(pending));
    }
    m_changed.notify_all();
    return true;
}

bool TimerService::cancel(const RequestId& requestId)
{
    bool removed = false;
    {
        std::lock_guard lock(m_mutex);
        removed = m_pending.remove(requestId);
    }
    if (removed) {
        m_changed.notify_all();
    }
    return removed;
}

std::optional<TimerCompletion> TimerService::takeReadyForContext(const UutId& uutId,
                                                                 const FrameId& frameId)
{
    std::lock_guard lock(m_mutex);
    const auto now = Clock::now();
    auto selected = m_pending.end();
    for (auto it = m_pending.begin(); it != m_pending.end(); ++it) {
        if (it->request.uutId != uutId || it->request.frameId != frameId ||
            it->deadline > now) {
            continue;
        }
        if (selected == m_pending.end() || it->deadline < selected->deadline) {
            selected = it;
        }
    }
    if (selected == m_pending.end()) {
        return std::nullopt;
    }

    TimerCompletion completion;
    completion.requestId = selected->request.requestId;
    completion.uutId = selected->request.uutId;
    completion.frameId = selected->request.frameId;
    completion.nodeId = selected->request.nodeId;
    completion.activationId = selected->request.activationId;
    completion.attemptId = selected->request.attemptId;
    completion.finishedAt = QDateTime::currentDateTimeUtc();
    m_pending.erase(selected);
    return completion;
}

std::optional<TimerCompletion> TimerService::takeAnyReady()
{
    std::lock_guard lock(m_mutex);
    const auto now = Clock::now();
    auto selected = m_pending.end();
    for (auto it = m_pending.begin(); it != m_pending.end(); ++it) {
        if (it->deadline > now) {
            continue;
        }
        if (selected == m_pending.end() || it->deadline < selected->deadline) {
            selected = it;
        }
    }
    if (selected == m_pending.end()) {
        return std::nullopt;
    }

    TimerCompletion completion;
    completion.requestId = selected->request.requestId;
    completion.uutId = selected->request.uutId;
    completion.frameId = selected->request.frameId;
    completion.nodeId = selected->request.nodeId;
    completion.activationId = selected->request.activationId;
    completion.attemptId = selected->request.attemptId;
    completion.finishedAt = QDateTime::currentDateTimeUtc();
    m_pending.erase(selected);
    return completion;
}

bool TimerService::hasPendingRequests() const
{
    std::lock_guard lock(m_mutex);
    return !m_pending.isEmpty();
}

bool TimerService::hasPendingRequestForUut(const UutId& uutId) const
{
    std::lock_guard lock(m_mutex);
    return std::any_of(m_pending.cbegin(), m_pending.cend(), [&uutId](const PendingTimer& timer) {
        return timer.request.uutId == uutId;
    });
}

int TimerService::pendingRequestCount() const
{
    std::lock_guard lock(m_mutex);
    return m_pending.size();
}

bool TimerService::waitForNextDeadline(std::chrono::milliseconds maximumWait)
{
    if (maximumWait <= std::chrono::milliseconds::zero()) {
        return hasPendingRequests();
    }

    std::unique_lock lock(m_mutex);
    if (m_pending.isEmpty()) {
        return false;
    }

    const auto next = std::min_element(
        m_pending.cbegin(), m_pending.cend(), [](const PendingTimer& left, const PendingTimer& right) {
            return left.deadline < right.deadline;
        });
    const auto now = Clock::now();
    if (next->deadline <= now) {
        return true;
    }

    const auto untilDeadline = std::chrono::duration_cast<std::chrono::milliseconds>(
        next->deadline - now);
    m_changed.wait_for(lock, std::min(maximumWait, untilDeadline));
    return !m_pending.isEmpty();
}

} // namespace PicoATE::Core
