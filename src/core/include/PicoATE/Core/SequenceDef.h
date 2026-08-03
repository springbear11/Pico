#pragma once

#include "PicoATE/Core/ExecutionPlan.h"
#include "PicoATE/Core/BarrierController.h"
#include "PicoATE/Core/ModuleRuntime.h"

#include <QSet>

namespace PicoATE::Core {

enum class StepKind {
    Noop,
    Wait,
    Action,
    Barrier,
    Cleanup,
    Loop,
    TestItem,
    Limit,
    Break,
    Counter,
    Aggregate,
    OperatorPrompt,
    Statement,
    SequenceCall
};

enum class StepGroupKind {
    Setup,
    Main,
    Cleanup,
    Custom
};

enum class OnFailureAction {
    Continue,
    StopUut,
    Retry,
    RunCleanup,
    Abort
};

struct ResourceRequirementDef {
    ResourceId resourceId;
    ResourceMode mode = ResourceMode::Exclusive;
    int count = 1;
    int priority = 0;
    int acquireTimeoutMs = 30000;

    ResourceRequirement toRuntimeRequirement() const;
};

struct ResourceRegionStartDef {
    ResourceRegionId id;
    QVector<ResourceRequirementDef> resources;
};

struct RetryPolicyDef {
    int maxAttempts = 1;
    int delayMs = 0;
    QString retryWhenExpression;

    RetryPolicy toRuntimePolicy() const;
};

struct TimeoutPolicyDef {
    int timeoutMs = 0;

    TimeoutPolicy toRuntimePolicy() const;
};

struct PeriodicTaskPolicyDef {
    bool enabled = false;
    int intervalMs = 5000;
    bool runImmediately = true;

    PeriodicTaskPolicy toRuntimePolicy() const;
};

struct ErrorPolicyDef {
    OnFailureAction onFail = OnFailureAction::StopUut;
    OnFailureAction onError = OnFailureAction::StopUut;
    OnFailureAction onTimeout = OnFailureAction::StopUut;
    CleanupRegionId cleanupRegionId;
    bool stopUutOnFailure = true;

    NodeErrorPolicy toRuntimePolicy() const;
};

struct BarrierPolicyDef {
    QString barrierName;
    QString cohortId = "default";
    int expectedUutCount = -1;
    int quorumCount = -1;
    double quorumRatio = 1.0;
    int arrivalTimeoutMs = 60000;
    int releaseTimeoutMs = 5000;
    BarrierArrivalPolicy arrivalPolicy = BarrierArrivalPolicy::WaitAll;
    BarrierReleasePolicy releasePolicy = BarrierReleasePolicy::Lockstep;
    BarrierFailurePolicy failurePolicy = BarrierFailurePolicy::FailBarrier;
    BarrierTimeoutPolicy timeoutPolicy = BarrierTimeoutPolicy::FailArrivedAndWaiting;
    bool releaseHeldResourcesOnWait = true;

    QVariantMap toPayload() const;
};

struct LoopPolicyDef {
    LoopType type = LoopType::For;
    QString variableName = "i";
    int from = 0;
    int to = 0;
    int step = 1;
    WhileIterationErrorPolicy iterationErrorPolicy =
        WhileIterationErrorPolicy::AbortLoop;
    int intervalMs = 0;
    int maxIterations = 100;
    int timeoutMs = 60000;

    ForLoopSpec toRuntimeSpec() const;
    WhileLoopSpec toRuntimeWhileSpec() const;
    QVariantMap toPayload() const;
};

struct OperatorPromptDef {
    QString mode = "confirm";
    QString title;
    QString message;
    QString image;
    QString confirmText = "OK";
    QString closeOnStep;
    int timeoutMs = 60000;

    QVariantMap toPayload() const;
};

struct StepDef {
    QString id;
    QString name;
    StepKind kind = StepKind::Noop;
    QString key;
    QVariantMap parameters;
    ModuleId moduleId;
    ModuleFunction functionName;
    QVariantMap inputs;
    QVector<ResourceRequirementDef> resources;
    std::optional<ResourceRegionStartDef> resourceRegionStart;
    ResourceRegionId resourceRegionEnd;
    RetryPolicyDef retry;
    TimeoutPolicyDef timeout;
    PeriodicTaskPolicyDef periodic;
    ErrorPolicyDef errorPolicy;
    BarrierPolicyDef barrier;
    LoopPolicyDef loop;
    OperatorPromptDef prompt;
    QVector<StepDef> steps;
    bool enabled = true;
    bool alwaysRun = false;
    bool resultRecording = true;
    bool checkpointBefore = false;
    bool checkpointAfter = false;
    QStringList tags;

    bool isCleanup() const;
};

struct StepGroupDef {
    QString id;
    QString name;
    StepGroupKind kind = StepGroupKind::Custom;
    QVector<StepDef> steps;
    bool enabled = true;

    void addStep(const StepDef& step);
};

struct ModuleBindingDef {
    ModuleId moduleId;
    QString transport = "qprocess";
    QString program;
    QStringList arguments;
    int timeoutMs = 30000;
    bool enabled = true;
};

struct SequenceDef {
    QString id;
    QString name;
    QString version = "0.1.0";
    QVector<StepGroupDef> groups;
    QVector<ModuleBindingDef> moduleBindings;
    QVector<SequenceVariableDefinition> variables;
    QVariantMap metadata;

    void addGroup(const StepGroupDef& group);
    QVector<const StepDef*> allSteps() const;
    std::optional<StepDef> stepById(const QString& stepId) const;
    QVector<QString> duplicateStepIds() const;
    bool hasDuplicateStepIds() const;
};

ExecNodeKind toExecNodeKind(StepKind kind);
ExecutionPhase toExecutionPhase(StepGroupKind kind);
ErrorAction toErrorAction(OnFailureAction action);
QString stepKindName(StepKind kind);
QString stepGroupKindName(StepGroupKind kind);

} // namespace PicoATE::Core
