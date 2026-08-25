#pragma once

#include "PicoATE/Core/RuntimeTypes.h"

#include <QDateTime>

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
    QString moduleId;
    QString functionName;
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
    NodeExecutionScope executionScope = NodeExecutionScope::PerUut;
};

struct UutReport {
    UutId uutId;
    QString serialNumber;
    bool completed = false;
    bool hasError = false;
    NodeOutcome outcome = NodeOutcome::Unknown;
    QDateTime startedAt;
    QDateTime finishedAt;
    qint64 durationMs = -1;
    QVector<StepReport> steps;
};

struct ExecutionReportMetadata {
    QString model;
    QString customerId;
    QString sequenceName;
    QString serialNumber;
    QString stationId;
    QString jigNo;
    QString order;
    QString tester;
    QDateTime startedAt;
    QDateTime finishedAt;
    qint64 durationMs = -1;
};

struct ExecutionReport {
    PlanId planId;
    SequenceId sequenceId;
    QString sequenceVersion;
    ExecutionState state = ExecutionState::Idle;
    bool completed = false;
    bool hasError = false;
    bool sessionHasError = false;
    ExecutionReportMetadata metadata;
    QVector<StepReport> sessionSteps;
    QVector<UutReport> uuts;
};

} // namespace PicoATE::Core
