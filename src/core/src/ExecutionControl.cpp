#include "PicoATE/Core/ExecutionControl.h"

#include "PicoATE/Core/StopToken.h"

#include <chrono>
#include <utility>

namespace PicoATE::Core {

bool ExecutionControl::requestPause()
{
    auto expected = ExecutionControlState::Running;
    return m_state.compare_exchange_strong(expected,
                                           ExecutionControlState::PauseRequested,
                                           std::memory_order_acq_rel);
}

void ExecutionControl::resume()
{
    {
        std::lock_guard lock(m_mutex);
        m_stepMode = DebugStepMode::None;
    }
    m_state.store(ExecutionControlState::Running, std::memory_order_release);
    m_changed.notify_all();
}

bool ExecutionControl::stepInto()
{
    return requestStep(DebugStepMode::Into);
}

bool ExecutionControl::stepOver()
{
    return requestStep(DebugStepMode::Over);
}

ExecutionControlState ExecutionControl::state() const
{
    return m_state.load(std::memory_order_acquire);
}

bool ExecutionControl::isPauseRequested() const
{
    return state() != ExecutionControlState::Running;
}

DebugStepMode ExecutionControl::pendingStepMode() const
{
    std::lock_guard lock(m_mutex);
    return m_stepMode;
}

void ExecutionControl::setBreakpoints(QVector<BreakpointSpec> breakpoints)
{
    std::lock_guard lock(m_mutex);
    for (int index = 0; index < breakpoints.size(); ++index) {
        if (breakpoints[index].id.trimmed().isEmpty()) {
            breakpoints[index].id = QString("breakpoint-%1").arg(index + 1);
        }
    }
    m_breakpoints = std::move(breakpoints);
}

void ExecutionControl::clearBreakpoints()
{
    std::lock_guard lock(m_mutex);
    m_breakpoints.clear();
}

QVector<BreakpointSpec> ExecutionControl::breakpoints() const
{
    std::lock_guard lock(m_mutex);
    return m_breakpoints;
}

std::optional<BreakpointHit> ExecutionControl::matchBreakpoint(
    const ExecutionPlan& plan,
    const UutExecution& uut,
    const ExecNode& node)
{
    std::lock_guard lock(m_mutex);
    for (auto& breakpoint : m_breakpoints) {
        if (!breakpoint.enabled ||
            !breakpointAddressMatches(plan, breakpoint.address, uut.uutId, node)) {
            continue;
        }

        breakpoint.hitCount += 1;
        if (breakpoint.oneShot) {
            breakpoint.enabled = false;
        }

        BreakpointHit hit;
        hit.breakpointId = breakpoint.id;
        hit.address = breakpoint.address;
        hit.planId = plan.id;
        hit.uutId = uut.uutId;
        hit.nodeId = node.id;
        hit.localPath = debugLocalPathForNode(plan, node.id);
        hit.displayName = node.displayName;
        hit.hitCount = breakpoint.hitCount;
        hit.hitAt = QDateTime::currentDateTimeUtc();
        return hit;
    }
    return std::nullopt;
}

void ExecutionControl::setDebugSnapshot(ExecutionDebugSnapshot snapshot)
{
    std::lock_guard lock(m_mutex);
    m_debugSnapshot = std::move(snapshot);
}

std::optional<ExecutionDebugSnapshot> ExecutionControl::debugSnapshot() const
{
    std::lock_guard lock(m_mutex);
    return m_debugSnapshot;
}

void ExecutionControl::clearDebugSnapshot()
{
    std::lock_guard lock(m_mutex);
    m_debugSnapshot.reset();
}

bool ExecutionControl::enterPausedState()
{
    auto expected = ExecutionControlState::PauseRequested;
    return m_state.compare_exchange_strong(expected,
                                           ExecutionControlState::Paused,
                                           std::memory_order_acq_rel);
}

DebugStepMode ExecutionControl::takeStepMode()
{
    std::lock_guard lock(m_mutex);
    const auto mode = m_stepMode;
    m_stepMode = DebugStepMode::None;
    return mode;
}

void ExecutionControl::waitUntilResumedOrStopped(const StopToken& stopToken)
{
    std::unique_lock lock(m_mutex);
    while (state() == ExecutionControlState::Paused && !stopToken.isStopRequested()) {
        // StopToken intentionally remains independent. The bounded wait also wakes
        // sessions stopped through a token held by an external caller.
        m_changed.wait_for(lock, std::chrono::milliseconds(20));
    }
    if (stopToken.isStopRequested()) {
        m_state.store(ExecutionControlState::Running, std::memory_order_release);
    }
}

bool ExecutionControl::requestStep(DebugStepMode mode)
{
    if (mode == DebugStepMode::None) {
        return false;
    }

    {
        std::lock_guard lock(m_mutex);
        if (state() != ExecutionControlState::Paused) {
            return false;
        }
        m_stepMode = mode;
    }
    m_state.store(ExecutionControlState::Running, std::memory_order_release);
    m_changed.notify_all();
    return true;
}

} // namespace PicoATE::Core
