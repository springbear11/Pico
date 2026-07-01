#pragma once

#include "PicoATE/Core/DeviceSessionManager.h"
#include "PicoATE/Core/RuntimeTypes.h"

#include <QDateTime>
#include <QVariantMap>

#include <atomic>

namespace PicoATE::Core {

enum class RuntimeEventKind {
    SessionStateChanged,
    UutRegistered,
    UutCompleted,
    NodeStateChanged,
    AttemptStarted,
    AttemptCompleted,
    RetryScheduled,
    LoopIterationStarted,
    LoopCompleted,
    TestItemStarted,
    TestItemCompleted,
    BarrierWaiting,
    BarrierReleased,
    CleanupActivated,
    DeviceStateChanged
};

struct RuntimeEvent {
    quint64 sequenceNumber = 0;
    QDateTime timestampUtc;
    PlanId planId;
    RuntimeEventKind kind = RuntimeEventKind::SessionStateChanged;
    ExecutionState executionState = ExecutionState::Idle;
    ActivationState activationState = ActivationState::Created;
    AttemptState attemptState = AttemptState::Created;
    NodeOutcome outcome = NodeOutcome::Unknown;
    DeviceConnectionState deviceState = DeviceConnectionState::Disconnected;
    UutId uutId;
    NodeId nodeId;
    NodeId nodeLocalId;
    NodeId parentNodeId;
    QString nodeDisplayName;
    ExecNodeKind nodeKind = ExecNodeKind::Action;
    AttemptId attemptId;
    DeviceId deviceId;
    FrameId frameId;
    int attemptIndex = 0;
    LoopIterationContext loopIteration;
    QVector<MeasurementResult> measurements;
    QString errorCode;
    QString message;
    QVariantMap details;
};

class IRuntimeEventSink {
public:
    virtual ~IRuntimeEventSink() = default;
    virtual void publish(const RuntimeEvent& event) = 0;
};

class RuntimeEventEmitter {
public:
    explicit RuntimeEventEmitter(PlanId planId = {}, IRuntimeEventSink* sink = nullptr);

    void setSink(IRuntimeEventSink* sink);
    bool hasSink() const;
    void publish(RuntimeEvent event);

private:
    PlanId m_planId;
    IRuntimeEventSink* m_sink = nullptr;
    std::atomic<quint64> m_nextSequence{1};
};

QString runtimeEventKindName(RuntimeEventKind kind);

} // namespace PicoATE::Core
