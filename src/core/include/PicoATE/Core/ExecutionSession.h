#pragma once

#include "PicoATE/Core/ExecutionGraphScheduler.h"
#include "PicoATE/Core/ExecutionReport.h"
#include "PicoATE/Core/SessionSnapshot.h"
#include "PicoATE/Core/StopToken.h"
#include "PicoATE/Core/RuntimeEvent.h"

#include <memory>

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
                              IRuntimeEventSink* eventSink = nullptr);

    UutExecution& addUut(const UutId& uutId);
    QVector<UutExecution>& uuts();
    const QVector<UutExecution>& uuts() const;
    ExecutionResultStore& results();
    const ExecutionResultStore& results() const;
    DeviceSessionManager& devices();
    const DeviceSessionManager& devices() const;
    bool registerModule(std::shared_ptr<IModule> module);

    void requestStop(StopMode mode = StopMode::Graceful);
    std::shared_ptr<StopToken> stopToken() const;
    ExecutionState state() const;

    ExecutionSessionResult run();
    ExecutionReport report() const;
    ExecutionSessionSnapshot snapshot() const;

private:
    bool allUutsComplete() const;
    bool uutComplete(const UutExecution& uut) const;
    QVector<UutExecution*> uutPointers();
    void prepareStopIfRequested();
    void publishSessionState(const QString& message = {});
    void publishCompletedUuts();

    ExecutionPlan m_plan;
    ExecutionResultStore m_results;
    RuntimeEventEmitter m_events;
    QVector<UutExecution> m_uuts;
    ExecutionState m_state = ExecutionState::Idle;
    std::shared_ptr<StopToken> m_stopToken;
    bool m_stopPrepared = false;
    QSet<UutId> m_publishedCompletedUuts;

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
