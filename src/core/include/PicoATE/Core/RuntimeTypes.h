#pragma once

#include "PicoATE/Core/ExecutionPlan.h"
#include "PicoATE/Core/MeasurementTypes.h"

#include <QDateTime>
#include <QHash>
#include <QVector>

namespace PicoATE::Core {

enum class ExecutionState {
    Idle,
    Starting,
    Running,
    Paused,
    Stopping,
    CleaningUp,
    Completed,
    CompletedWithError,
    Aborted
};

enum class StopMode {
    Graceful,
    Abort
};

enum class FrameKind {
    RootSequence,
    SequenceCall,
    LoopIteration,
    RetryScope,
    BatchCohort,
    Cleanup
};

enum class FrameState {
    Created,
    Active,
    Completed,
    Cancelled
};

enum class ActivationState {
    Created,
    WaitingForDependency,
    WaitingForResource,
    WaitingAtBarrier,
    Ready,
    Running,
    Passed,
    Failed,
    Error,
    Timeout,
    Cancelled,
    Skipped
};

enum class AttemptState {
    Created,
    Running,
    Completed,
    Cancelled
};

struct VariableSnapshot {
    QVariantMap values;
};

struct LoopIterationContext {
    bool active = false;
    LoopId loopId;
    NodeId controllerNodeId;
    QString variableName;
    int iterationIndex = -1;
    int iterationNumber = 0;
    int value = 0;
};

struct NodeResult {
    NodeId nodeId;
    NodeOutcome outcome = NodeOutcome::Unknown;
    QVariantMap outputs;
    QVector<MeasurementResult> measurements;
    QString errorCode;
    QString errorMessage;
    QDateTime startedAt;
    QDateTime finishedAt;
};

struct NodeAttempt {
    AttemptId id;
    ActivationId activationId;
    int attemptIndex = 0;
    LoopIterationContext loopIteration;
    AttemptState state = AttemptState::Created;
    VariableSnapshot beforeAttempt;
    ResourceLeaseId leaseId;
    NodeResult result;
};

struct NodeActivation {
    ActivationId id;
    FrameId frameId;
    NodeId nodeId;
    ActivationState state = ActivationState::Created;
    QVector<NodeAttempt> attempts;
    VariableSnapshot preNodeSnapshot;
    VariableSnapshot postNodeSnapshot;
    QDateTime createdAt = QDateTime::currentDateTimeUtc();
    QDateTime completedAt;
};

struct ExecutionFrame {
    FrameId id;
    FrameId parentFrameId;
    PlanId planId;
    FrameKind kind = FrameKind::RootSequence;
    UutId uutId;
    NodeId callerNodeId;
    QVariantMap inputBindings;
    QVariantMap outputBindings;
    int loopIndex = -1;
    int retryAttempt = 0;
    FrameState state = FrameState::Created;
};

struct UutExecution {
    UutId uutId;
    QHash<NodeId, NodeActivation> activations;
    QVariantMap variables;

    ActivationState stateOf(const NodeId& nodeId) const;
    NodeOutcome outcomeOf(const NodeId& nodeId) const;
    NodeActivation& ensureActivation(const NodeId& nodeId, const FrameId& frameId);
};

ActivationState outcomeToActivationState(NodeOutcome outcome);
bool isTerminalActivation(ActivationState state);

} // namespace PicoATE::Core
