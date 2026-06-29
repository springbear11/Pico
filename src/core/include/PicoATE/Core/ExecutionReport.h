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
    NodeOutcome outcome = NodeOutcome::Unknown;
    QString errorCode;
    QString errorMessage;
    LoopIterationContext loopIteration;
    QVector<MeasurementResult> measurements;
};

struct StepReport {
    NodeId stepId;
    QString displayName;
    ExecNodeKind kind = ExecNodeKind::Noop;
    ActivationState state = ActivationState::Created;
    NodeOutcome outcome = NodeOutcome::Unknown;
    bool wasError = false;
    StepLoopReport loop;
    QVector<MeasurementResult> measurements;
    QVector<AttemptReport> attempts;
};

struct UutReport {
    UutId uutId;
    bool hasError = false;
    QVector<StepReport> steps;
};

struct ExecutionReport {
    PlanId planId;
    SequenceId sequenceId;
    QString sequenceVersion;
    ExecutionState state = ExecutionState::Idle;
    bool completed = false;
    bool hasError = false;
    QVector<UutReport> uuts;
};

} // namespace PicoATE::Core
