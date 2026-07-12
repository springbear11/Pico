#pragma once

#include "PicoATE/Core/BarrierController.h"
#include "PicoATE/Core/ResourceManager.h"
#include "PicoATE/Core/RuntimeTypes.h"

#include <optional>

namespace PicoATE::Core {

using BreakpointId = QString;

enum class BreakpointAddressKind {
    NodePath,
    LocalPath
};

enum class DebugPauseReason {
    None,
    UserPause,
    Breakpoint,
    StepInto,
    StepOver
};

enum class DebugStepMode {
    None,
    Into,
    Over
};

struct BreakpointAddress {
    BreakpointAddressKind kind = BreakpointAddressKind::NodePath;
    QString value;
    UutId uutId;

    static BreakpointAddress nodePath(QString nodePath, UutId uutId = {});
    static BreakpointAddress localPath(QString localPath, UutId uutId = {});
    bool hasUutFilter() const;
};

struct BreakpointSpec {
    BreakpointId id;
    BreakpointAddress address;
    bool enabled = true;
    bool oneShot = false;
    int hitCount = 0;
};

struct BreakpointHit {
    BreakpointId breakpointId;
    BreakpointAddress address;
    PlanId planId;
    UutId uutId;
    NodeId nodeId;
    QString localPath;
    QString displayName;
    int hitCount = 0;
    QDateTime hitAt = QDateTime::currentDateTimeUtc();
};

struct DebugAttemptSnapshot {
    AttemptId attemptId;
    int attemptIndex = 0;
    AttemptState state = AttemptState::Created;
    LoopIterationContext loopIteration;
    NodeOutcome outcome = NodeOutcome::Unknown;
    QVariantMap outputs;
    QVector<MeasurementResult> measurements;
    QString errorCode;
    QString errorMessage;
};

struct DebugNodeSnapshot {
    NodeId nodeId;
    QString localId;
    QString localPath;
    QString displayName;
    ExecNodeKind kind = ExecNodeKind::Noop;
    FrameId frameId;
    ActivationState state = ActivationState::Created;
    NodeOutcome outcome = NodeOutcome::Unknown;
    QVector<DebugAttemptSnapshot> attempts;
};

struct DebugUutSnapshot {
    UutId uutId;
    QVariantMap variables;
    QVector<DebugNodeSnapshot> nodes;
};

struct ExecutionDebugSnapshot {
    PlanId planId;
    SequenceId sequenceId;
    QString sequenceVersion;
    ExecutionState state = ExecutionState::Idle;
    DebugPauseReason pauseReason = DebugPauseReason::None;
    std::optional<BreakpointHit> breakpoint;
    UutId currentUutId;
    NodeId currentNodeId;
    QString currentLocalPath;
    QVector<DebugUutSnapshot> uuts;
    ResourceSnapshot resources;
    BarrierSnapshot barriers;
    QDateTime capturedAt = QDateTime::currentDateTimeUtc();
};

QString debugLocalPathForNode(const ExecutionPlan& plan, const NodeId& nodeId);
std::optional<NodeId> resolveBreakpointAddress(const ExecutionPlan& plan,
                                               const BreakpointAddress& address);
bool breakpointAddressMatches(const ExecutionPlan& plan,
                              const BreakpointAddress& address,
                              const UutId& uutId,
                              const ExecNode& node);
ExecutionDebugSnapshot makeExecutionDebugSnapshot(
    const ExecutionPlan& plan,
    ExecutionState state,
    const QVector<UutExecution>& uuts,
    const ResourceSnapshot& resources,
    const BarrierSnapshot& barriers,
    DebugPauseReason pauseReason = DebugPauseReason::None,
    std::optional<BreakpointHit> breakpoint = std::nullopt,
    UutId currentUutId = {},
    NodeId currentNodeId = {});

} // namespace PicoATE::Core
