#pragma once

#include "PicoATE/Core/ExecutionControl.h"
#include "PicoATE/Core/ExecutionGraphScheduler.h"
#include "PicoATE/Core/ExecutionReport.h"
#include "PicoATE/Core/SessionSnapshot.h"
#include "PicoATE/Core/StopToken.h"
#include "PicoATE/Core/RuntimeEvent.h"

#include <memory>
#include <optional>
#include <QSet>

namespace PicoATE::Core {

struct ExecutionSessionResult {
    bool completed = false;
    bool hasError = false;
    ExecutionState state = ExecutionState::Idle;
    QVector<NodeResult> nodeResults;
};

class ExecutionSession {
public:
    explicit ExecutionSession(ExecutionPlan plan,
                              std::shared_ptr<StopToken> stopToken = {},
                              IRuntimeEventSink* eventSink = nullptr,
                              std::shared_ptr<ExecutionControl> executionControl = {});

    UutExecution& addUut(const UutId& uutId);
    QVector<UutExecution>& uuts();
    const QVector<UutExecution>& uuts() const;
    ExecutionResultStore& results();
    const ExecutionResultStore& results() const;
    DeviceSessionManager& devices();
    const DeviceSessionManager& devices() const;
    bool registerModule(std::shared_ptr<IModule> module);

    void requestStop(StopMode mode = StopMode::Graceful);
    bool requestPause();
    void resume();
    std::shared_ptr<StopToken> stopToken() const;
    std::shared_ptr<ExecutionControl> executionControl() const;
    ExecutionState state() const;

    ExecutionSessionResult run();
    ExecutionReport report() const;
    ExecutionSessionSnapshot snapshot() const;
    ExecutionDebugSnapshot debugSnapshot(DebugPauseReason reason = DebugPauseReason::None,
                                         std::optional<BreakpointHit> breakpoint = std::nullopt) const;

private:
    bool allUutsComplete() const;
    bool uutComplete(const UutExecution& uut) const;
    QVector<UutExecution*> uutPointers();
    void prepareStopIfRequested();
    void pauseAtSafePointIfRequested();
    void pauseAtBreakpointIfNeeded(UutExecution& uut, const FrameId& frameId = "root");
    void consumeDebugStepCommand();
    void beginDebugStepIfNeeded(const UutExecution& uut,
                                const NodeId& nodeId,
                                const FrameId& frameId);
    void pauseAfterDebugStepIfNeeded(const UutExecution& uut,
                                     const SchedulerStepResult& step,
                                     const FrameId& frameId);
    bool shouldPauseForActiveDebugStep(const UutExecution& uut,
                                       const SchedulerStepResult& step) const;
    bool debugStepShouldSuppressBreakpoint(const NodeId& nodeId) const;
    bool nodeIsDescendantOf(const NodeId& nodeId, const NodeId& rootNodeId) const;
    void publishSessionState(const QString& message = {});
    void publishBreakpointHit(const BreakpointHit& hit);
    void publishDebugStepCompleted(DebugStepMode mode,
                                   const UutExecution& uut,
                                   const NodeId& nodeId);
    void publishCompletedUuts();

    ExecutionPlan m_plan;
    ExecutionResultStore m_results;
    RuntimeEventEmitter m_events;
    QVector<UutExecution> m_uuts;
    ExecutionState m_state = ExecutionState::Idle;
    std::shared_ptr<StopToken> m_stopToken;
    std::shared_ptr<ExecutionControl> m_executionControl;
    bool m_stopPrepared = false;
    QSet<UutId> m_publishedCompletedUuts;
    QSet<QString> m_breakpointResumeGuards;
    DebugStepMode m_activeDebugStepMode = DebugStepMode::None;
    UutId m_debugStepUutId;
    FrameId m_debugStepFrameId;
    NodeId m_debugStepRootNodeId;
    bool m_debugStepStarted = false;

    DeviceSessionManager m_devices;
    ModuleRuntimeServices m_runtimeServices;
    ResourceManager m_resources;
    BarrierController m_barriers;
    LoopController m_loops;
    ErrorPolicyEngine m_errorPolicy;
    NodeRunner m_runner;
    std::unique_ptr<ExecutionGraphScheduler> m_scheduler;
};

} // namespace PicoATE::Core
