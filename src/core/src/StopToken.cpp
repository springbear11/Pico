#include "PicoATE/Core/StopToken.h"

namespace PicoATE::Core {

void StopToken::requestStop(StopMode mode)
{
    const int requestedPriority = priorityFor(mode);
    int current = m_requestPriority.load(std::memory_order_relaxed);
    while (current < requestedPriority &&
           !m_requestPriority.compare_exchange_weak(
               current,
               requestedPriority,
               std::memory_order_release,
               std::memory_order_relaxed)) {
    }
}

bool StopToken::isStopRequested() const
{
    return m_requestPriority.load(std::memory_order_acquire) > 0;
}

StopMode StopToken::requestedMode() const
{
    return m_requestPriority.load(std::memory_order_acquire) >= priorityFor(StopMode::Abort)
        ? StopMode::Abort
        : StopMode::Graceful;
}

int StopToken::priorityFor(StopMode mode)
{
    return mode == StopMode::Abort ? 2 : 1;
}

} // namespace PicoATE::Core
