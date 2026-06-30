#include "PicoATE/Core/ExecutionReportJson.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonParseError>

#include <optional>

namespace PicoATE::Core {

namespace {

QString executionStateName(ExecutionState state)
{
    switch (state) {
    case ExecutionState::Idle: return "Idle";
    case ExecutionState::Starting: return "Starting";
    case ExecutionState::Running: return "Running";
    case ExecutionState::Paused: return "Paused";
    case ExecutionState::Stopping: return "Stopping";
    case ExecutionState::CleaningUp: return "CleaningUp";
    case ExecutionState::Completed: return "Completed";
    case ExecutionState::CompletedWithError: return "CompletedWithError";
    case ExecutionState::Aborted: return "Aborted";
    }
    return "Idle";
}

std::optional<ExecutionState> executionStateFromString(const QString& value)
{
    if (value == "Idle") return ExecutionState::Idle;
    if (value == "Starting") return ExecutionState::Starting;
    if (value == "Running") return ExecutionState::Running;
    if (value == "Paused") return ExecutionState::Paused;
    if (value == "Stopping") return ExecutionState::Stopping;
    if (value == "CleaningUp") return ExecutionState::CleaningUp;
    if (value == "Completed") return ExecutionState::Completed;
    if (value == "CompletedWithError") return ExecutionState::CompletedWithError;
    if (value == "Aborted") return ExecutionState::Aborted;
    return std::nullopt;
}

QString activationStateName(ActivationState state)
{
    switch (state) {
    case ActivationState::Created: return "Created";
    case ActivationState::WaitingForDependency: return "WaitingForDependency";
    case ActivationState::WaitingForResource: return "WaitingForResource";
    case ActivationState::WaitingAtBarrier: return "WaitingAtBarrier";
    case ActivationState::Ready: return "Ready";
    case ActivationState::Running: return "Running";
    case ActivationState::Passed: return "Passed";
    case ActivationState::Failed: return "Failed";
    case ActivationState::Error: return "Error";
    case ActivationState::Timeout: return "Timeout";
    case ActivationState::Cancelled: return "Cancelled";
    case ActivationState::Skipped: return "Skipped";
    }
    return "Created";
}

std::optional<ActivationState> activationStateFromString(const QString& value)
{
    if (value == "Created") return ActivationState::Created;
    if (value == "WaitingForDependency") return ActivationState::WaitingForDependency;
    if (value == "WaitingForResource") return ActivationState::WaitingForResource;
    if (value == "WaitingAtBarrier") return ActivationState::WaitingAtBarrier;
    if (value == "Ready") return ActivationState::Ready;
    if (value == "Running") return ActivationState::Running;
    if (value == "Passed") return ActivationState::Passed;
    if (value == "Failed") return ActivationState::Failed;
    if (value == "Error") return ActivationState::Error;
    if (value == "Timeout") return ActivationState::Timeout;
    if (value == "Cancelled") return ActivationState::Cancelled;
    if (value == "Skipped") return ActivationState::Skipped;
    return std::nullopt;
}

QString nodeKindName(ExecNodeKind kind)
{
    switch (kind) {
    case ExecNodeKind::Noop: return "Noop";
    case ExecNodeKind::Wait: return "Wait";
    case ExecNodeKind::Action: return "Action";
    case ExecNodeKind::Barrier: return "Barrier";
    case ExecNodeKind::Cleanup: return "Cleanup";
    case ExecNodeKind::Loop: return "Loop";
    case ExecNodeKind::TestItem: return "TestItem";
    }
    return "Noop";
}

std::optional<ExecNodeKind> nodeKindFromString(const QString& value)
{
    if (value == "Noop") return ExecNodeKind::Noop;
    if (value == "Wait") return ExecNodeKind::Wait;
    if (value == "Action") return ExecNodeKind::Action;
    if (value == "Barrier") return ExecNodeKind::Barrier;
    if (value == "Cleanup") return ExecNodeKind::Cleanup;
    if (value == "Loop") return ExecNodeKind::Loop;
    if (value == "TestItem") return ExecNodeKind::TestItem;
    return std::nullopt;
}

std::optional<NodeOutcome> outcomeFromString(const QString& value)
{
    if (value == "Unknown") return NodeOutcome::Unknown;
    if (value == "Passed") return NodeOutcome::Passed;
    if (value == "Failed") return NodeOutcome::Failed;
    if (value == "Error") return NodeOutcome::Error;
    if (value == "Timeout") return NodeOutcome::Timeout;
    if (value == "Cancelled") return NodeOutcome::Cancelled;
    if (value == "Skipped") return NodeOutcome::Skipped;
    return std::nullopt;
}

void addError(QVector<ExecutionReportJsonError>& errors,
              QString path,
              QString message)
{
    errors.push_back({std::move(path), std::move(message)});
}

QJsonObject loopIterationToJson(const LoopIterationContext& loop)
{
    return {
        {"active", loop.active},
        {"loopId", loop.loopId},
        {"controllerNodeId", loop.controllerNodeId},
        {"variableName", loop.variableName},
        {"iterationIndex", loop.iterationIndex},
        {"iterationNumber", loop.iterationNumber},
        {"value", loop.value},
    };
}

LoopIterationContext loopIterationFromJson(const QJsonObject& object)
{
    LoopIterationContext loop;
    loop.active = object.value("active").toBool(false);
    loop.loopId = object.value("loopId").toString();
    loop.controllerNodeId = object.value("controllerNodeId").toString();
    loop.variableName = object.value("variableName").toString();
    loop.iterationIndex = object.value("iterationIndex").toInt(-1);
    loop.iterationNumber = object.value("iterationNumber").toInt(0);
    loop.value = object.value("value").toInt(0);
    return loop;
}

QJsonObject measurementToJson(const MeasurementResult& measurement)
{
    QJsonObject object;
    object.insert("name", measurement.name);
    object.insert("value", QJsonValue::fromVariant(measurement.value));
    object.insert("unit", measurement.unit);
    object.insert("rawValue", QJsonValue::fromVariant(measurement.rawValue));
    object.insert("hasLowerLimit", measurement.hasLowerLimit);
    if (measurement.hasLowerLimit) object.insert("lowerLimit", measurement.lowerLimit);
    object.insert("hasUpperLimit", measurement.hasUpperLimit);
    if (measurement.hasUpperLimit) object.insert("upperLimit", measurement.upperLimit);
    object.insert("status", measurementStatusName(measurement.status));
    object.insert("errorCode", measurement.errorCode);
    object.insert("errorMessage", measurement.errorMessage);
    object.insert("attributes", QJsonObject::fromVariantMap(measurement.attributes));
    return object;
}

MeasurementResult measurementFromJson(const QJsonObject& object)
{
    MeasurementResult measurement;
    measurement.name = object.value("name").toString();
    measurement.value = object.value("value").toVariant();
    measurement.unit = object.value("unit").toString();
    measurement.rawValue = object.value("rawValue").toVariant();
    measurement.hasLowerLimit = object.value("hasLowerLimit").toBool(
        object.contains("lowerLimit"));
    measurement.lowerLimit = object.value("lowerLimit").toDouble();
    measurement.hasUpperLimit = object.value("hasUpperLimit").toBool(
        object.contains("upperLimit"));
    measurement.upperLimit = object.value("upperLimit").toDouble();
    measurement.status = measurementStatusFromString(object.value("status").toString());
    measurement.errorCode = object.value("errorCode").toString();
    measurement.errorMessage = object.value("errorMessage").toString();
    measurement.attributes = object.value("attributes").toObject().toVariantMap();
    return measurement;
}

QJsonArray measurementsToJson(const QVector<MeasurementResult>& measurements)
{
    QJsonArray array;
    for (const auto& measurement : measurements) array.push_back(measurementToJson(measurement));
    return array;
}

QVector<MeasurementResult> measurementsFromJson(const QJsonValue& value,
                                                const QString& path,
                                                QVector<ExecutionReportJsonError>& errors)
{
    QVector<MeasurementResult> measurements;
    if (value.isUndefined()) return measurements;
    if (!value.isArray()) {
        addError(errors, path, "Expected array");
        return measurements;
    }
    const auto array = value.toArray();
    measurements.reserve(array.size());
    for (int index = 0; index < array.size(); ++index) {
        if (!array[index].isObject()) {
            addError(errors, QString("%1[%2]").arg(path).arg(index), "Expected object");
            continue;
        }
        measurements.push_back(measurementFromJson(array[index].toObject()));
    }
    return measurements;
}

QJsonObject attemptToJson(const AttemptReport& attempt)
{
    return {
        {"index", attempt.index},
        {"outcome", nodeOutcomeName(attempt.outcome)},
        {"errorCode", attempt.errorCode},
        {"errorMessage", attempt.errorMessage},
        {"loopIteration", loopIterationToJson(attempt.loopIteration)},
        {"measurements", measurementsToJson(attempt.measurements)},
    };
}

AttemptReport attemptFromJson(const QJsonObject& object,
                              const QString& path,
                              QVector<ExecutionReportJsonError>& errors)
{
    AttemptReport attempt;
    attempt.index = object.value("index").toInt();
    const auto outcomeText = object.value("outcome").toString("Unknown");
    const auto outcome = outcomeFromString(outcomeText);
    if (outcome) attempt.outcome = *outcome;
    else addError(errors, path + ".outcome", "Unsupported node outcome: " + outcomeText);
    attempt.errorCode = object.value("errorCode").toString();
    attempt.errorMessage = object.value("errorMessage").toString();
    attempt.loopIteration = loopIterationFromJson(object.value("loopIteration").toObject());
    attempt.measurements = measurementsFromJson(
        object.value("measurements"), path + ".measurements", errors);
    return attempt;
}

QJsonObject stepLoopToJson(const StepLoopReport& loop)
{
    return {
        {"inLoop", loop.inLoop},
        {"loopId", loop.loopId},
        {"controllerStepId", loop.controllerStepId},
        {"variableName", loop.variableName},
        {"from", loop.from},
        {"to", loop.to},
        {"step", loop.step},
    };
}

StepLoopReport stepLoopFromJson(const QJsonObject& object)
{
    StepLoopReport loop;
    loop.inLoop = object.value("inLoop").toBool(false);
    loop.loopId = object.value("loopId").toString();
    loop.controllerStepId = object.value("controllerStepId").toString();
    loop.variableName = object.value("variableName").toString();
    loop.from = object.value("from").toInt();
    loop.to = object.value("to").toInt();
    loop.step = object.value("step").toInt(1);
    return loop;
}

QJsonObject stepToJson(const StepReport& step)
{
    QJsonArray attempts;
    for (const auto& attempt : step.attempts) attempts.push_back(attemptToJson(attempt));
    QJsonArray children;
    for (const auto& child : step.children) children.push_back(stepToJson(child));
    return {
        {"stepId", step.stepId},
        {"displayName", step.displayName},
        {"kind", nodeKindName(step.kind)},
        {"state", activationStateName(step.state)},
        {"outcome", nodeOutcomeName(step.outcome)},
        {"wasError", step.wasError},
        {"loop", stepLoopToJson(step.loop)},
        {"measurements", measurementsToJson(step.measurements)},
        {"attempts", attempts},
        {"children", children},
    };
}

StepReport stepFromJson(const QJsonObject& object,
                        const QString& path,
                        QVector<ExecutionReportJsonError>& errors)
{
    StepReport step;
    step.stepId = object.value("stepId").toString();
    step.displayName = object.value("displayName").toString();
    const auto kindText = object.value("kind").toString("Noop");
    const auto kind = nodeKindFromString(kindText);
    if (kind) step.kind = *kind;
    else addError(errors, path + ".kind", "Unsupported node kind: " + kindText);
    const auto stateText = object.value("state").toString("Created");
    const auto state = activationStateFromString(stateText);
    if (state) step.state = *state;
    else addError(errors, path + ".state", "Unsupported activation state: " + stateText);
    const auto outcomeText = object.value("outcome").toString("Unknown");
    const auto outcome = outcomeFromString(outcomeText);
    if (outcome) step.outcome = *outcome;
    else addError(errors, path + ".outcome", "Unsupported node outcome: " + outcomeText);
    step.wasError = object.value("wasError").toBool(false);
    step.loop = stepLoopFromJson(object.value("loop").toObject());
    step.measurements = measurementsFromJson(
        object.value("measurements"), path + ".measurements", errors);

    const auto attemptsValue = object.value("attempts");
    if (!attemptsValue.isUndefined() && !attemptsValue.isArray()) {
        addError(errors, path + ".attempts", "Expected array");
    } else {
        const auto attempts = attemptsValue.toArray();
        step.attempts.reserve(attempts.size());
        for (int index = 0; index < attempts.size(); ++index) {
            if (!attempts[index].isObject()) {
                addError(errors,
                         QString("%1.attempts[%2]").arg(path).arg(index),
                         "Expected object");
                continue;
            }
            step.attempts.push_back(attemptFromJson(
                attempts[index].toObject(),
                QString("%1.attempts[%2]").arg(path).arg(index),
                errors));
        }
    }
    const auto childrenValue = object.value("children");
    if (!childrenValue.isUndefined() && !childrenValue.isArray()) {
        addError(errors, path + ".children", "Expected array");
    } else {
        const auto children = childrenValue.toArray();
        step.children.reserve(children.size());
        for (int index = 0; index < children.size(); ++index) {
            if (!children[index].isObject()) {
                addError(errors,
                         QString("%1.children[%2]").arg(path).arg(index),
                         "Expected object");
                continue;
            }
            step.children.push_back(stepFromJson(
                children[index].toObject(),
                QString("%1.children[%2]").arg(path).arg(index),
                errors));
        }
    }
    return step;
}

QJsonObject reportBodyToJson(const ExecutionReport& report)
{
    QJsonArray uuts;
    for (const auto& uut : report.uuts) {
        QJsonArray steps;
        for (const auto& step : uut.steps) steps.push_back(stepToJson(step));
        uuts.push_back(QJsonObject{
            {"uutId", uut.uutId},
            {"hasError", uut.hasError},
            {"steps", steps},
        });
    }
    return {
        {"planId", report.planId},
        {"sequenceId", report.sequenceId},
        {"sequenceVersion", report.sequenceVersion},
        {"state", executionStateName(report.state)},
        {"completed", report.completed},
        {"hasError", report.hasError},
        {"uuts", uuts},
    };
}

} // namespace

QJsonObject executionReportToJson(const ExecutionReport& report)
{
    return {
        {"schema", "picoate.execution-report"},
        {"schemaVersion", ExecutionReportSchemaVersion},
        {"report", reportBodyToJson(report)},
    };
}

QByteArray serializeExecutionReport(const ExecutionReport& report)
{
    return QJsonDocument(executionReportToJson(report)).toJson(QJsonDocument::Indented);
}

ExecutionReportJsonResult executionReportFromJson(const QJsonObject& object)
{
    ExecutionReportJsonResult result;
    if (object.value("schema").toString() != "picoate.execution-report") {
        addError(result.errors, "schema", "Expected picoate.execution-report");
    }
    const int version = object.value("schemaVersion").toInt(-1);
    if (version < 1 || version > ExecutionReportSchemaVersion) {
        addError(result.errors,
                 "schemaVersion",
                 QString("Unsupported execution report schema version: %1").arg(version));
    }
    if (!object.value("report").isObject()) {
        addError(result.errors, "report", "Expected object");
        return result;
    }

    const auto report = object.value("report").toObject();
    result.report.planId = report.value("planId").toString();
    result.report.sequenceId = report.value("sequenceId").toString();
    result.report.sequenceVersion = report.value("sequenceVersion").toString();
    const auto stateText = report.value("state").toString("Idle");
    const auto state = executionStateFromString(stateText);
    if (state) result.report.state = *state;
    else addError(result.errors, "report.state", "Unsupported execution state: " + stateText);
    result.report.completed = report.value("completed").toBool(false);
    result.report.hasError = report.value("hasError").toBool(false);

    const auto uutsValue = report.value("uuts");
    if (!uutsValue.isUndefined() && !uutsValue.isArray()) {
        addError(result.errors, "report.uuts", "Expected array");
        return result;
    }
    const auto uuts = uutsValue.toArray();
    result.report.uuts.reserve(uuts.size());
    for (int uutIndex = 0; uutIndex < uuts.size(); ++uutIndex) {
        const auto uutPath = QString("report.uuts[%1]").arg(uutIndex);
        if (!uuts[uutIndex].isObject()) {
            addError(result.errors, uutPath, "Expected object");
            continue;
        }
        const auto uutObject = uuts[uutIndex].toObject();
        UutReport uut;
        uut.uutId = uutObject.value("uutId").toString();
        uut.hasError = uutObject.value("hasError").toBool(false);
        const auto stepsValue = uutObject.value("steps");
        if (!stepsValue.isUndefined() && !stepsValue.isArray()) {
            addError(result.errors, uutPath + ".steps", "Expected array");
        } else {
            const auto steps = stepsValue.toArray();
            uut.steps.reserve(steps.size());
            for (int stepIndex = 0; stepIndex < steps.size(); ++stepIndex) {
                const auto stepPath = QString("%1.steps[%2]").arg(uutPath).arg(stepIndex);
                if (!steps[stepIndex].isObject()) {
                    addError(result.errors, stepPath, "Expected object");
                    continue;
                }
                uut.steps.push_back(stepFromJson(
                    steps[stepIndex].toObject(), stepPath, result.errors));
            }
        }
        result.report.uuts.push_back(uut);
    }
    return result;
}

ExecutionReportJsonResult parseExecutionReport(const QByteArray& json)
{
    ExecutionReportJsonResult result;
    QJsonParseError parseError;
    const auto document = QJsonDocument::fromJson(json, &parseError);
    if (parseError.error != QJsonParseError::NoError) {
        addError(result.errors,
                 QString("offset %1").arg(parseError.offset),
                 parseError.errorString());
        return result;
    }
    if (!document.isObject()) {
        addError(result.errors, {}, "Execution report root must be an object");
        return result;
    }
    return executionReportFromJson(document.object());
}

} // namespace PicoATE::Core
