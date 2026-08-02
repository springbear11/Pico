#pragma once

#include "PicoATE/Core/RuntimeTypes.h"

namespace PicoATE::Core {

struct StepLoopReport {
    bool inLoop = false;
    LoopId loopId;
    NodeId controllerStepId;
    QString variableName;
    int from = 0;
    int to = 0;
    int step = 1;
};

struct AttemptReport {
    int index = 0;
    RequestId requestId;
    NodeOutcome outcome = NodeOutcome::Unknown;
    qint64 durationMs = -1;
    QString errorCode;
    QString errorMessage;
    LoopIterationContext loopIteration;
    QVector<MeasurementResult> measurements;
};

struct StepReport {
    NodeId stepId;
    NodeId nodePath;
    QString displayName;
    ExecNodeKind kind = ExecNodeKind::Noop;
    ActivationState state = ActivationState::Created;
    NodeOutcome outcome = NodeOutcome::Unknown;
    qint64 durationMs = -1;
    bool wasError = false;
    bool resultRecording = true;
    StepLoopReport loop;
    QVector<MeasurementResult> measurements;
    QVector<AttemptReport> attempts;
    QVector<StepReport> children;
    ExecutionPhase phase = ExecutionPhase::Main;
};

struct UutReport {
    UutId uutId;
    bool completed = false;
    bool hasError = false;
    NodeOutcome outcome = NodeOutcome::Unknown;
    QVector<StepReport> steps;
};

struct ExecutionReport {
    PlanId planId;
    SequenceId sequenceId;
    QString sequenceVersion;
    ExecutionState state = ExecutionState::Idle;
    bool completed = false;
    bool hasError = false;
    bool sessionHasError = false;
    QVector<StepReport> sessionSteps;
    QVector<UutReport> uuts;
};

} // namespace PicoATE::Core
