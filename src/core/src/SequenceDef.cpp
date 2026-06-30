#include "PicoATE/Core/SequenceDef.h"

#include <QHash>

namespace PicoATE::Core {

ResourceRequirement ResourceRequirementDef::toRuntimeRequirement() const
{
    ResourceRequirement requirement;
    requirement.resourceId = resourceId;
    requirement.mode = mode;
    requirement.count = count;
    requirement.priority = priority;
    requirement.acquireTimeoutMs = acquireTimeoutMs;
    return requirement;
}

RetryPolicy RetryPolicyDef::toRuntimePolicy() const
{
    RetryPolicy policy;
    policy.maxAttempts = maxAttempts;
    policy.delayMs = delayMs;
    policy.retryWhenExpression = retryWhenExpression;
    return policy;
}

TimeoutPolicy TimeoutPolicyDef::toRuntimePolicy() const
{
    TimeoutPolicy policy;
    policy.timeoutMs = timeoutMs;
    return policy;
}

NodeErrorPolicy ErrorPolicyDef::toRuntimePolicy() const
{
    NodeErrorPolicy policy;
    policy.onFail = toErrorAction(onFail);
    policy.onError = toErrorAction(onError);
    policy.onTimeout = toErrorAction(onTimeout);
    policy.cleanupRegionId = cleanupRegionId;
    policy.stopUutOnFailure = stopUutOnFailure;
    return policy;
}

QVariantMap BarrierPolicyDef::toPayload() const
{
    QVariantMap payload;
    payload.insert("barrierName", barrierName);
    payload.insert("cohortId", cohortId);
    payload.insert("expectedUutCount", expectedUutCount);
    payload.insert("quorumCount", quorumCount);
    payload.insert("quorumRatio", quorumRatio);
    payload.insert("arrivalTimeoutMs", arrivalTimeoutMs);
    payload.insert("releaseTimeoutMs", releaseTimeoutMs);

    auto arrival = QString("WaitAll");
    if (arrivalPolicy == BarrierArrivalPolicy::DropFailed) {
        arrival = "DropFailed";
    } else if (arrivalPolicy == BarrierArrivalPolicy::CountFailed) {
        arrival = "CountFailed";
    } else if (arrivalPolicy == BarrierArrivalPolicy::Quorum) {
        arrival = "Quorum";
    } else if (arrivalPolicy == BarrierArrivalPolicy::BestEffort) {
        arrival = "BestEffort";
    } else if (arrivalPolicy == BarrierArrivalPolicy::ManualDecision) {
        arrival = "ManualDecision";
    }
    payload.insert("arrivalPolicy", arrival);

    auto release = QString("Lockstep");
    if (releasePolicy == BarrierReleasePolicy::Latch) {
        release = "Latch";
    } else if (releasePolicy == BarrierReleasePolicy::Cohort) {
        release = "Cohort";
    } else if (releasePolicy == BarrierReleasePolicy::RollingWindow) {
        release = "RollingWindow";
    }
    payload.insert("releasePolicy", release);

    auto failure = QString("FailBarrier");
    if (failurePolicy == BarrierFailurePolicy::RemoveFailedMember) {
        failure = "RemoveFailedMember";
    } else if (failurePolicy == BarrierFailurePolicy::HoldFailedMember) {
        failure = "HoldFailedMember";
    } else if (failurePolicy == BarrierFailurePolicy::ContinueWithWarning) {
        failure = "ContinueWithWarning";
    } else if (failurePolicy == BarrierFailurePolicy::AbortCohort) {
        failure = "AbortCohort";
    }
    payload.insert("failurePolicy", failure);

    auto timeout = QString("FailArrivedAndWaiting");
    if (timeoutPolicy == BarrierTimeoutPolicy::ReleaseArrived) {
        timeout = "ReleaseArrived";
    } else if (timeoutPolicy == BarrierTimeoutPolicy::ReleaseIfQuorumReached) {
        timeout = "ReleaseIfQuorumReached";
    } else if (timeoutPolicy == BarrierTimeoutPolicy::AbortCohort) {
        timeout = "AbortCohort";
    } else if (timeoutPolicy == BarrierTimeoutPolicy::RequestOperatorDecision) {
        timeout = "RequestOperatorDecision";
    }
    payload.insert("timeoutPolicy", timeout);

    payload.insert("releaseHeldResourcesOnWait", releaseHeldResourcesOnWait);
    return payload;
}

ForLoopSpec LoopPolicyDef::toRuntimeSpec() const
{
    ForLoopSpec spec;
    spec.variableName = variableName;
    spec.from = from;
    spec.to = to;
    spec.step = step;
    return spec;
}

QVariantMap LoopPolicyDef::toPayload() const
{
    QVariantMap payload;
    payload.insert("type", "for");
    payload.insert("variable", variableName);
    payload.insert("from", from);
    payload.insert("to", to);
    payload.insert("step", step);
    return payload;
}

bool StepDef::isCleanup() const
{
    return kind == StepKind::Cleanup || alwaysRun;
}

void StepGroupDef::addStep(const StepDef& step)
{
    steps.push_back(step);
}

void SequenceDef::addGroup(const StepGroupDef& group)
{
    groups.push_back(group);
}

QVector<const StepDef*> SequenceDef::allSteps() const
{
    QVector<const StepDef*> result;
    const auto collect = [&](const StepDef& step, const auto& collectRef) -> void {
        result.push_back(&step);
        for (const auto& child : step.steps) {
            collectRef(child, collectRef);
        }
    };

    for (const auto& group : groups) {
        for (const auto& step : group.steps) {
            collect(step, collect);
        }
    }
    return result;
}

std::optional<StepDef> SequenceDef::stepById(const QString& stepId) const
{
    for (const auto* step : allSteps()) {
        if (step->id == stepId) {
            return *step;
        }
    }
    return std::nullopt;
}

QVector<QString> SequenceDef::duplicateStepIds() const
{
    QHash<QString, int> counts;
    for (const auto* step : allSteps()) {
        if (!step->id.isEmpty()) {
            counts[step->id] += 1;
        }
    }

    QVector<QString> duplicates;
    for (auto it = counts.constBegin(); it != counts.constEnd(); ++it) {
        if (it.value() > 1) {
            duplicates.push_back(it.key());
        }
    }
    return duplicates;
}

bool SequenceDef::hasDuplicateStepIds() const
{
    return !duplicateStepIds().isEmpty();
}

ExecNodeKind toExecNodeKind(StepKind kind)
{
    switch (kind) {
    case StepKind::Noop:
        return ExecNodeKind::Noop;
    case StepKind::Wait:
        return ExecNodeKind::Wait;
    case StepKind::Action:
        return ExecNodeKind::Action;
    case StepKind::Barrier:
        return ExecNodeKind::Barrier;
    case StepKind::Cleanup:
        return ExecNodeKind::Cleanup;
    case StepKind::Loop:
        return ExecNodeKind::Loop;
    case StepKind::TestItem:
        return ExecNodeKind::TestItem;
    case StepKind::Statement:
    case StepKind::SequenceCall:
        return ExecNodeKind::Action;
    }
    return ExecNodeKind::Noop;
}

ErrorAction toErrorAction(OnFailureAction action)
{
    switch (action) {
    case OnFailureAction::Continue:
        return ErrorAction::Continue;
    case OnFailureAction::StopUut:
        return ErrorAction::StopUut;
    case OnFailureAction::Retry:
        return ErrorAction::Retry;
    case OnFailureAction::RunCleanup:
        return ErrorAction::RunCleanup;
    case OnFailureAction::Abort:
        return ErrorAction::Abort;
    }
    return ErrorAction::StopUut;
}

QString stepKindName(StepKind kind)
{
    switch (kind) {
    case StepKind::Noop:
        return "Noop";
    case StepKind::Wait:
        return "Wait";
    case StepKind::Action:
        return "Action";
    case StepKind::Barrier:
        return "Barrier";
    case StepKind::Cleanup:
        return "Cleanup";
    case StepKind::Loop:
        return "Loop";
    case StepKind::TestItem:
        return "TestItem";
    case StepKind::Statement:
        return "Statement";
    case StepKind::SequenceCall:
        return "SequenceCall";
    }
    return "Unknown";
}

QString stepGroupKindName(StepGroupKind kind)
{
    switch (kind) {
    case StepGroupKind::Setup:
        return "Setup";
    case StepGroupKind::Main:
        return "Main";
    case StepGroupKind::Cleanup:
        return "Cleanup";
    case StepGroupKind::Custom:
        return "Custom";
    }
    return "Unknown";
}

} // namespace PicoATE::Core
