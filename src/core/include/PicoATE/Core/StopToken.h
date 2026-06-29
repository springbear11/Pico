#pragma once

#include "PicoATE/Core/RuntimeTypes.h"

#include <atomic>

namespace PicoATE::Core {

class StopToken final
{
public:
    void requestStop(StopMode mode = StopMode::Graceful);

    bool isStopRequested() const;
    StopMode requestedMode() const;

private:
    static int priorityFor(StopMode mode);

    std::atomic<int> m_requestPriority{0};
};

} // namespace PicoATE::Core
