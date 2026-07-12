#pragma once

#include "PicoATE/Core/ExecutionDebug.h"

#include <atomic>
#include <condition_variable>
#include <mutex>
#include <optional>

namespace PicoATE::Core {

class StopToken;

enum class ExecutionControlState {
    Running,
    PauseRequested,
    Paused
};

class ExecutionControl final
{
public:
    bool requestPause();
    void resume();
    bool stepInto();
    bool stepOver();

    ExecutionControlState state() const;
    bool isPauseRequested() const;
    DebugStepMode pendingStepMode() const;

    void setBreakpoints(QVector<BreakpointSpec> breakpoints);
    void clearBreakpoints();
    QVector<BreakpointSpec> breakpoints() const;
    std::optional<BreakpointHit> matchBreakpoint(const ExecutionPlan& plan,
                                                 const UutExecution& uut,
                                                 const ExecNode& node);

    void setDebugSnapshot(ExecutionDebugSnapshot snapshot);
    std::optional<ExecutionDebugSnapshot> debugSnapshot() const;
    void clearDebugSnapshot();

    // Called only by the execution thread at a node boundary.
    bool enterPausedState();
    DebugStepMode takeStepMode();
    void waitUntilResumedOrStopped(const StopToken& stopToken);

private:
    bool requestStep(DebugStepMode mode);

    std::atomic<ExecutionControlState> m_state{ExecutionControlState::Running};
    mutable std::mutex m_mutex;
    std::condition_variable m_changed;
    QVector<BreakpointSpec> m_breakpoints;
    std::optional<ExecutionDebugSnapshot> m_debugSnapshot;
    DebugStepMode m_stepMode = DebugStepMode::None;
};

} // namespace PicoATE::Core
