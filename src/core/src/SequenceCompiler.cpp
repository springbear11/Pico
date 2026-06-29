#include "PicoATE/Core/SequenceCompiler.h"

#include <QJsonArray>
#include <QJsonValue>
#include <QSet>

namespace PicoATE::Core {
namespace {

QString normalized(QString value)
{
    value = value.trimmed().toLower();
    value.remove('-');
    value.remove('_');
    value.remove(' ');
    return value;
}

QString stringValue(const QJsonObject& object,
                    const QString& key,
                    const QString& fallback = {})
{
    const auto value = object.value(key);
    if (value.isString()) {
        return value.toString();
    }
    return fallback;
}

QString childPath(const QString& path, const QString& key)
{
    return path.isEmpty() ? key : QString("%1.%2").arg(path, key);
}

QString jsonTypeName(const QJsonValue& value)
{
    if (value.isNull()) {
        return "null";
    }
    if (value.isBool()) {
        return "bool";
    }
    if (value.isDouble()) {
        return "number";
    }
    if (value.isString()) {
        return "string";
    }
    if (value.isArray()) {
        return "array";
    }
    if (value.isObject()) {
        return "object";
    }
    return "undefined";
}

void addTypeError(QVector<CompileError>& errors,
                  const QString& path,
                  const QString& expected)
{
    errors.push_back({path,
                      QString("Expected %1").arg(expected),
                      "Fix the JSON field type"});
}

QString readString(const QJsonObject& object,
                   const QString& key,
                   const QString& path,
                   QVector<CompileError>& errors,
                   const QString& fallback = {})
{
    if (!object.contains(key)) {
        return fallback;
    }
    const auto value = object.value(key);
    if (!value.isString()) {
        addTypeError(errors, childPath(path, key), QString("string, got %1").arg(jsonTypeName(value)));
        return fallback;
    }
    return value.toString();
}

bool readBool(const QJsonObject& object,
              const QString& key,
              const QString& path,
              QVector<CompileError>& errors,
              bool fallback)
{
    if (!object.contains(key)) {
        return fallback;
    }
    const auto value = object.value(key);
    if (!value.isBool()) {
        addTypeError(errors, childPath(path, key), QString("bool, got %1").arg(jsonTypeName(value)));
        return fallback;
    }
    return value.toBool();
}

int readInt(const QJsonObject& object,
            const QString& key,
            const QString& path,
            QVector<CompileError>& errors,
            int fallback)
{
    if (!object.contains(key)) {
        return fallback;
    }
    const auto value = object.value(key);
    if (!value.isDouble()) {
        addTypeError(errors, childPath(path, key), QString("number, got %1").arg(jsonTypeName(value)));
        return fallback;
    }
    return value.toInt();
}

double readDouble(const QJsonObject& object,
                  const QString& key,
                  const QString& path,
                  QVector<CompileError>& errors,
                  double fallback)
{
    if (!object.contains(key)) {
        return fallback;
    }
    const auto value = object.value(key);
    if (!value.isDouble()) {
        addTypeError(errors, childPath(path, key), QString("number, got %1").arg(jsonTypeName(value)));
        return fallback;
    }
    return value.toDouble();
}

QVariantMap toVariantMap(const QJsonObject& object)
{
    return object.toVariantMap();
}

bool isExtensionField(const QString& key)
{
    return key.startsWith("x-", Qt::CaseInsensitive) || key == "vendor";
}

void addUnknownFieldWarning(QVector<CompileWarning>& warnings,
                            const QString& path)
{
    warnings.push_back({path,
                        "Unknown field",
                        "Remove it, fix the spelling, or move extension data under x-* or vendor"});
}

void warnUnknownFields(const QJsonObject& object,
                       const QString& path,
                       const QSet<QString>& knownFields,
                       QVector<CompileWarning>& warnings)
{
    for (auto it = object.constBegin(); it != object.constEnd(); ++it) {
        const auto key = it.key();
        if (knownFields.contains(key) || isExtensionField(key)) {
            continue;
        }
        addUnknownFieldWarning(warnings, childPath(path, key));
    }
}

bool stepKindLooksLikeBarrier(const QJsonObject& object)
{
    const auto kind = object.contains("kind") ? stringValue(object, "kind") : stringValue(object, "type");
    return normalized(kind) == "barrier";
}

QSet<QString> barrierFieldNames()
{
    return {
        "barrierName",
        "cohortId",
        "expectedUutCount",
        "quorumCount",
        "quorumRatio",
        "arrivalTimeoutMs",
        "releaseTimeoutMs",
        "arrivalPolicy",
        "releasePolicy",
        "failurePolicy",
        "timeoutPolicy",
        "releaseHeldResourcesOnWait",
    };
}

void collectBarrierWarnings(const QJsonObject& object,
                            const QString& path,
                            QVector<CompileWarning>& warnings)
{
    warnUnknownFields(object, path, barrierFieldNames(), warnings);
}

void collectErrorPolicyWarnings(const QJsonObject& object,
                                const QString& path,
                                QVector<CompileWarning>& warnings)
{
    warnUnknownFields(object,
                      path,
                      {"onFail", "onError", "onTimeout", "cleanupRegionId", "stopUutOnFailure"},
                      warnings);
}

void collectTimeoutWarnings(const QJsonObject& object,
                            const QString& path,
                            QVector<CompileWarning>& warnings)
{
    warnUnknownFields(object, path, {"timeoutMs"}, warnings);
}

void collectRetryWarnings(const QJsonObject& object,
                          const QString& path,
                          QVector<CompileWarning>& warnings)
{
    warnUnknownFields(object, path, {"maxAttempts", "delayMs", "retryWhen"}, warnings);
}

void collectLoopWarnings(const QJsonObject& object,
                         const QString& path,
                         QVector<CompileWarning>& warnings)
{
    warnUnknownFields(object, path, {"type", "variable", "from", "to", "step"}, warnings);
}

void collectResourceWarnings(const QJsonObject& object,
                             const QString& path,
                             QVector<CompileWarning>& warnings)
{
    warnUnknownFields(
        object,
        path,
        {"resourceId", "name", "mode", "count", "priority", "acquireTimeoutMs"},
        warnings);
}

void collectModuleBindingWarnings(const QJsonObject& object,
                                  const QString& path,
                                  QVector<CompileWarning>& warnings)
{
    warnUnknownFields(object,
                      path,
                      {"moduleId", "transport", "program", "arguments", "timeoutMs", "enabled"},
                      warnings);
}

void collectStepWarnings(const QJsonObject& object,
                         const QString& path,
                         QVector<CompileWarning>& warnings)
{
    auto knownFields = QSet<QString>{
        "id",
        "name",
        "kind",
        "type",
        "enabled",
        "alwaysRun",
        "resultRecording",
        "checkpointBefore",
        "checkpointAfter",
        "parameters",
        "moduleId",
        "function",
        "inputs",
        "ms",
        "resources",
        "retry",
        "timeout",
        "timeoutMs",
        "errorPolicy",
        "barrier",
        "tags",
    };

    const auto kind = object.contains("kind") ? stringValue(object, "kind") : stringValue(object, "type");
    if (normalized(kind) == "loop" || normalized(kind) == "forloop") {
        knownFields.insert("loop");
        knownFields.insert("steps");
    }

    if (!object.contains("barrier") && stepKindLooksLikeBarrier(object)) {
        knownFields.unite(barrierFieldNames());
    }

    warnUnknownFields(object, path, knownFields, warnings);

    if (object.value("resources").isArray()) {
        const auto resources = object.value("resources").toArray();
        for (int i = 0; i < resources.size(); ++i) {
            if (resources[i].isObject()) {
                collectResourceWarnings(resources[i].toObject(), QString("%1.resources[%2]").arg(path).arg(i), warnings);
            }
        }
    }

    if (object.value("retry").isObject()) {
        collectRetryWarnings(object.value("retry").toObject(), path + ".retry", warnings);
    }

    if (object.value("timeout").isObject()) {
        collectTimeoutWarnings(object.value("timeout").toObject(), path + ".timeout", warnings);
    }

    if (object.value("errorPolicy").isObject()) {
        collectErrorPolicyWarnings(object.value("errorPolicy").toObject(), path + ".errorPolicy", warnings);
    }

    if (object.value("barrier").isObject()) {
        collectBarrierWarnings(object.value("barrier").toObject(), path + ".barrier", warnings);
    }

    if (object.value("loop").isObject()) {
        collectLoopWarnings(object.value("loop").toObject(), path + ".loop", warnings);
    }

    if (object.value("steps").isArray()) {
        const auto steps = object.value("steps").toArray();
        for (int i = 0; i < steps.size(); ++i) {
            if (steps[i].isObject()) {
                collectStepWarnings(steps[i].toObject(), QString("%1.steps[%2]").arg(path).arg(i), warnings);
            }
        }
    }
}

void collectGroupWarnings(const QJsonObject& object,
                          const QString& path,
                          QVector<CompileWarning>& warnings)
{
    warnUnknownFields(object, path, {"id", "name", "kind", "type", "enabled", "steps"}, warnings);

    if (!object.value("steps").isArray()) {
        return;
    }

    const auto steps = object.value("steps").toArray();
    for (int i = 0; i < steps.size(); ++i) {
        if (steps[i].isObject()) {
            collectStepWarnings(steps[i].toObject(), QString("%1.steps[%2]").arg(path).arg(i), warnings);
        }
    }
}

void collectSequenceWarnings(const QJsonObject& object,
                             QVector<CompileWarning>& warnings)
{
    warnUnknownFields(object, {}, {"id", "name", "version", "metadata", "moduleBindings", "groups"}, warnings);

    if (object.value("moduleBindings").isArray()) {
        const auto bindings = object.value("moduleBindings").toArray();
        for (int i = 0; i < bindings.size(); ++i) {
            if (bindings[i].isObject()) {
                collectModuleBindingWarnings(bindings[i].toObject(),
                                             QString("moduleBindings[%1]").arg(i),
                                             warnings);
            }
        }
    }

    if (!object.value("groups").isArray()) {
        return;
    }

    const auto groups = object.value("groups").toArray();
    for (int i = 0; i < groups.size(); ++i) {
        if (groups[i].isObject()) {
            collectGroupWarnings(groups[i].toObject(), QString("groups[%1]").arg(i), warnings);
        }
    }
}

StepKind parseStepKindString(const QString& text, bool& ok)
{
    const auto value = normalized(text);
    ok = true;
    if (value == "noop") {
        return StepKind::Noop;
    }
    if (value == "wait") {
        return StepKind::Wait;
    }
    if (value == "action" || value == "mockaction") {
        return StepKind::Action;
    }
    if (value == "barrier") {
        return StepKind::Barrier;
    }
    if (value == "cleanup") {
        return StepKind::Cleanup;
    }
    if (value == "loop" || value == "forloop") {
        return StepKind::Loop;
    }
    if (value == "statement") {
        return StepKind::Statement;
    }
    if (value == "sequencecall") {
        return StepKind::SequenceCall;
    }
    ok = false;
    return StepKind::Noop;
}

StepGroupKind parseGroupKindString(const QString& text, bool& ok)
{
    const auto value = normalized(text);
    ok = true;
    if (value == "setup") {
        return StepGroupKind::Setup;
    }
    if (value == "main") {
        return StepGroupKind::Main;
    }
    if (value == "cleanup") {
        return StepGroupKind::Cleanup;
    }
    if (value == "custom") {
        return StepGroupKind::Custom;
    }
    ok = false;
    return StepGroupKind::Custom;
}

ResourceMode parseResourceModeString(const QString& text, bool& ok)
{
    const auto value = normalized(text);
    ok = true;
    if (value == "sharedread") {
        return ResourceMode::SharedRead;
    }
    if (value == "sharedwrite") {
        return ResourceMode::SharedWrite;
    }
    if (value == "counted") {
        return ResourceMode::Counted;
    }
    if (value == "orderedexclusive") {
        return ResourceMode::OrderedExclusive;
    }
    if (value == "exclusive") {
        return ResourceMode::Exclusive;
    }
    ok = false;
    return ResourceMode::Exclusive;
}

OnFailureAction parseFailureActionString(const QString& text, bool& ok)
{
    const auto value = normalized(text);
    ok = true;
    if (value == "continue") {
        return OnFailureAction::Continue;
    }
    if (value == "retry") {
        return OnFailureAction::Retry;
    }
    if (value == "runcleanup" || value == "cleanup") {
        return OnFailureAction::RunCleanup;
    }
    if (value == "abort") {
        return OnFailureAction::Abort;
    }
    if (value == "stopuut" || value == "stop") {
        return OnFailureAction::StopUut;
    }
    ok = false;
    return OnFailureAction::StopUut;
}

BarrierArrivalPolicy parseArrivalPolicyString(const QString& text, bool& ok)
{
    const auto value = normalized(text);
    ok = true;
    if (value == "dropfailed") {
        return BarrierArrivalPolicy::DropFailed;
    }
    if (value == "countfailed") {
        return BarrierArrivalPolicy::CountFailed;
    }
    if (value == "quorum") {
        return BarrierArrivalPolicy::Quorum;
    }
    if (value == "besteffort") {
        return BarrierArrivalPolicy::BestEffort;
    }
    if (value == "manualdecision") {
        return BarrierArrivalPolicy::ManualDecision;
    }
    if (value == "waitall") {
        return BarrierArrivalPolicy::WaitAll;
    }
    ok = false;
    return BarrierArrivalPolicy::WaitAll;
}

BarrierReleasePolicy parseReleasePolicyString(const QString& text, bool& ok)
{
    const auto value = normalized(text);
    ok = true;
    if (value == "latch") {
        return BarrierReleasePolicy::Latch;
    }
    if (value == "cohort") {
        return BarrierReleasePolicy::Cohort;
    }
    if (value == "rollingwindow") {
        return BarrierReleasePolicy::RollingWindow;
    }
    if (value == "lockstep") {
        return BarrierReleasePolicy::Lockstep;
    }
    ok = false;
    return BarrierReleasePolicy::Lockstep;
}

BarrierFailurePolicy parseFailurePolicyString(const QString& text, bool& ok)
{
    const auto value = normalized(text);
    ok = true;
    if (value == "removefailedmember" || value == "removefailed") {
        return BarrierFailurePolicy::RemoveFailedMember;
    }
    if (value == "holdfailedmember" || value == "holdfailed") {
        return BarrierFailurePolicy::HoldFailedMember;
    }
    if (value == "continuewithwarning") {
        return BarrierFailurePolicy::ContinueWithWarning;
    }
    if (value == "abortcohort") {
        return BarrierFailurePolicy::AbortCohort;
    }
    if (value == "failbarrier") {
        return BarrierFailurePolicy::FailBarrier;
    }
    ok = false;
    return BarrierFailurePolicy::FailBarrier;
}

BarrierTimeoutPolicy parseTimeoutPolicyString(const QString& text, bool& ok)
{
    const auto value = normalized(text);
    ok = true;
    if (value == "releasearrived") {
        return BarrierTimeoutPolicy::ReleaseArrived;
    }
    if (value == "releaseifquorumreached") {
        return BarrierTimeoutPolicy::ReleaseIfQuorumReached;
    }
    if (value == "abortcohort") {
        return BarrierTimeoutPolicy::AbortCohort;
    }
    if (value == "requestoperatordecision") {
        return BarrierTimeoutPolicy::RequestOperatorDecision;
    }
    if (value == "failarrivedandwaiting") {
        return BarrierTimeoutPolicy::FailArrivedAndWaiting;
    }
    ok = false;
    return BarrierTimeoutPolicy::FailArrivedAndWaiting;
}

} // namespace

CompileResult SequenceCompiler::compileJson(const QJsonObject& object) const
{
    CompileResult result;
    collectSequenceWarnings(object, result.warnings);
    result.sequence = parseSequence(object, result.errors);
    if (!result.errors.isEmpty()) {
        return result;
    }

    PlanBuilder builder;
    const auto build = builder.build(result.sequence);
    result.plan = build.plan;
    for (const auto& error : build.errors) {
        result.errors.push_back({"plan", error.message, error.suggestion});
    }
    return result;
}

SequenceDef SequenceCompiler::parseSequence(const QJsonObject& object,
                                            QVector<CompileError>& errors) const
{
    SequenceDef sequence;
    sequence.id = readString(object, "id", {}, errors);
    sequence.name = readString(object, "name", {}, errors);
    sequence.version = readString(object, "version", {}, errors, "0.1.0");

    if (object.contains("metadata")) {
        if (object.value("metadata").isObject()) {
            sequence.metadata = toVariantMap(object.value("metadata").toObject());
        } else {
            addTypeError(errors, "metadata", QString("object, got %1").arg(jsonTypeName(object.value("metadata"))));
        }
    }

    if (object.contains("moduleBindings") && !object.value("moduleBindings").isArray()) {
        addTypeError(errors,
                     "moduleBindings",
                     QString("array, got %1").arg(jsonTypeName(object.value("moduleBindings"))));
    } else if (object.value("moduleBindings").isArray()) {
        sequence.moduleBindings = parseModuleBindings(object.value("moduleBindings").toArray(),
                                                      "moduleBindings",
                                                      errors);
    }

    const auto groupsValue = object.value("groups");
    if (!groupsValue.isArray()) {
        addError(errors, "groups", "Sequence must contain a groups array", "Add a groups array with at least a Main group");
        return sequence;
    }

    const auto groups = groupsValue.toArray();
    for (int i = 0; i < groups.size(); ++i) {
        const auto path = QString("groups[%1]").arg(i);
        if (!groups[i].isObject()) {
            addError(errors, path, "Group must be an object");
            continue;
        }
        sequence.addGroup(parseGroup(groups[i].toObject(), path, errors));
    }

    return sequence;
}

StepGroupDef SequenceCompiler::parseGroup(const QJsonObject& object,
                                          const QString& path,
                                          QVector<CompileError>& errors) const
{
    StepGroupDef group;
    group.id = readString(object, "id", path, errors, path);
    group.name = readString(object, "name", path, errors, group.id);
    group.enabled = readBool(object, "enabled", path, errors, true);

    bool kindOk = false;
    const auto kindKey = object.contains("kind") ? QString("kind") : QString("type");
    group.kind = parseGroupKindString(readString(object, kindKey, path, errors, "custom"), kindOk);
    if (!kindOk) {
        addError(errors, childPath(path, kindKey), "Unsupported group kind", "Use setup, main, cleanup, or custom");
    }

    const auto stepsValue = object.value("steps");
    if (!stepsValue.isArray()) {
        addError(errors, path + ".steps", "Group must contain a steps array");
        return group;
    }

    const auto steps = stepsValue.toArray();
    for (int i = 0; i < steps.size(); ++i) {
        const auto stepPath = QString("%1.steps[%2]").arg(path).arg(i);
        if (!steps[i].isObject()) {
            addError(errors, stepPath, "Step must be an object");
            continue;
        }
        group.addStep(parseStep(steps[i].toObject(), stepPath, errors));
    }

    return group;
}

StepDef SequenceCompiler::parseStep(const QJsonObject& object,
                                    const QString& path,
                                    QVector<CompileError>& errors) const
{
    StepDef step;
    step.id = readString(object, "id", path, errors);
    step.name = readString(object, "name", path, errors, step.id);
    step.enabled = readBool(object, "enabled", path, errors, true);
    step.alwaysRun = readBool(object, "alwaysRun", path, errors, false);
    step.resultRecording = readBool(object, "resultRecording", path, errors, true);
    step.checkpointBefore = readBool(object, "checkpointBefore", path, errors, false);
    step.checkpointAfter = readBool(object, "checkpointAfter", path, errors, false);

    bool kindOk = false;
    const auto kindKey = object.contains("kind") ? QString("kind") : QString("type");
    step.kind = parseStepKindString(readString(object, kindKey, path, errors, "noop"), kindOk);
    if (!kindOk) {
        addError(errors, childPath(path, kindKey), "Unsupported step kind", "Use noop, wait, action, barrier, cleanup, or loop");
    }

    if (object.contains("parameters")) {
        if (object.value("parameters").isObject()) {
            step.parameters = toVariantMap(object.value("parameters").toObject());
        } else {
            addTypeError(errors,
                         childPath(path, "parameters"),
                         QString("object, got %1").arg(jsonTypeName(object.value("parameters"))));
        }
    }

    step.moduleId = readString(object, "moduleId", path, errors);
    step.functionName = readString(object, "function", path, errors);
    if (object.contains("inputs")) {
        if (object.value("inputs").isObject()) {
            step.inputs = toVariantMap(object.value("inputs").toObject());
        } else {
            addTypeError(errors,
                         childPath(path, "inputs"),
                         QString("object, got %1").arg(jsonTypeName(object.value("inputs"))));
        }
    }

    if (step.kind == StepKind::Wait && object.contains("ms") && !step.parameters.contains("ms")) {
        step.parameters.insert("ms", readInt(object, "ms", path, errors, 0));
    }

    if (object.contains("resources") && !object.value("resources").isArray()) {
        addTypeError(errors,
                     childPath(path, "resources"),
                     QString("array, got %1").arg(jsonTypeName(object.value("resources"))));
    } else if (object.value("resources").isArray()) {
        step.resources = parseResources(object.value("resources").toArray(), path + ".resources", errors);
    }

    if (object.contains("retry") && !object.value("retry").isObject()) {
        addTypeError(errors,
                     childPath(path, "retry"),
                     QString("object, got %1").arg(jsonTypeName(object.value("retry"))));
    } else if (object.value("retry").isObject()) {
        step.retry = parseRetry(object.value("retry").toObject(), path + ".retry", errors);
    }

    if (object.contains("timeout") && !object.value("timeout").isObject()) {
        addTypeError(errors,
                     childPath(path, "timeout"),
                     QString("object, got %1").arg(jsonTypeName(object.value("timeout"))));
    } else if (object.value("timeout").isObject()) {
        step.timeout = parseTimeout(object.value("timeout").toObject(), path + ".timeout", errors);
    } else if (object.contains("timeoutMs")) {
        step.timeout.timeoutMs = readInt(object, "timeoutMs", path, errors, 0);
    }

    if (object.contains("errorPolicy") && !object.value("errorPolicy").isObject()) {
        addTypeError(errors,
                     childPath(path, "errorPolicy"),
                     QString("object, got %1").arg(jsonTypeName(object.value("errorPolicy"))));
    } else if (object.value("errorPolicy").isObject()) {
        step.errorPolicy = parseErrorPolicy(object.value("errorPolicy").toObject(), path + ".errorPolicy", errors);
    }

    if (object.contains("barrier") && !object.value("barrier").isObject()) {
        addTypeError(errors,
                     childPath(path, "barrier"),
                     QString("object, got %1").arg(jsonTypeName(object.value("barrier"))));
    } else if (object.value("barrier").isObject()) {
        step.barrier = parseBarrierPolicy(object.value("barrier").toObject(), path + ".barrier", step, errors);
    } else if (step.kind == StepKind::Barrier) {
        step.barrier = parseBarrierPolicy(object, path, step, errors);
    }

    if (object.contains("loop") && !object.value("loop").isObject()) {
        addTypeError(errors,
                     childPath(path, "loop"),
                     QString("object, got %1").arg(jsonTypeName(object.value("loop"))));
    } else if (object.value("loop").isObject()) {
        step.loop = parseLoopPolicy(object.value("loop").toObject(), path + ".loop", errors);
    }

    if (step.kind == StepKind::Loop) {
        if (object.contains("steps") && !object.value("steps").isArray()) {
            addTypeError(errors,
                         childPath(path, "steps"),
                         QString("array, got %1").arg(jsonTypeName(object.value("steps"))));
        } else if (object.value("steps").isArray()) {
            const auto steps = object.value("steps").toArray();
            for (int i = 0; i < steps.size(); ++i) {
                const auto childPathValue = QString("%1.steps[%2]").arg(path).arg(i);
                if (!steps[i].isObject()) {
                    addError(errors, childPathValue, "Step must be an object");
                    continue;
                }
                step.steps.push_back(parseStep(steps[i].toObject(), childPathValue, errors));
            }
        } else {
            addError(errors,
                     childPath(path, "steps"),
                     "Loop step must contain a steps array",
                     "Add one or more child steps to the loop");
        }
    }

    if (object.contains("tags") && !object.value("tags").isArray()) {
        addTypeError(errors,
                     childPath(path, "tags"),
                     QString("array, got %1").arg(jsonTypeName(object.value("tags"))));
    } else if (object.value("tags").isArray()) {
        int index = 0;
        for (const auto& tag : object.value("tags").toArray()) {
            if (tag.isString()) {
                step.tags.push_back(tag.toString());
            } else {
                addTypeError(errors,
                             QString("%1.tags[%2]").arg(path).arg(index),
                             QString("string, got %1").arg(jsonTypeName(tag)));
            }
            ++index;
        }
    }

    return step;
}

QVector<ModuleBindingDef> SequenceCompiler::parseModuleBindings(const QJsonArray& array,
                                                                const QString& path,
                                                                QVector<CompileError>& errors) const
{
    QVector<ModuleBindingDef> bindings;
    for (int i = 0; i < array.size(); ++i) {
        const auto bindingPath = QString("%1[%2]").arg(path).arg(i);
        if (!array[i].isObject()) {
            addError(errors, bindingPath, "Module binding must be an object");
            continue;
        }

        const auto object = array[i].toObject();
        ModuleBindingDef binding;
        binding.moduleId = readString(object, "moduleId", bindingPath, errors);
        binding.transport = readString(object, "transport", bindingPath, errors, "qprocess");
        binding.program = readString(object, "program", bindingPath, errors);
        binding.timeoutMs = readInt(object, "timeoutMs", bindingPath, errors, 30000);
        binding.enabled = readBool(object, "enabled", bindingPath, errors, true);

        const auto transportKind = normalized(binding.transport);
        if (transportKind != "qprocess" && transportKind != "persistentqprocess") {
            addError(errors,
                     childPath(bindingPath, "transport"),
                     "Unsupported module transport",
                     "Use qprocess or persistent-qprocess");
        }

        if (binding.moduleId.trimmed().isEmpty()) {
            addError(errors,
                     childPath(bindingPath, "moduleId"),
                     "Module binding moduleId is required",
                     "Set moduleId to the id used by action steps");
        }

        if (binding.enabled && binding.program.trimmed().isEmpty()) {
            addError(errors,
                     childPath(bindingPath, "program"),
                     "Module binding program is required",
                     "Set the executable path for the module transport");
        }

        if (object.contains("arguments") && !object.value("arguments").isArray()) {
            addTypeError(errors,
                         childPath(bindingPath, "arguments"),
                         QString("array, got %1").arg(jsonTypeName(object.value("arguments"))));
        } else if (object.value("arguments").isArray()) {
            const auto arguments = object.value("arguments").toArray();
            for (int argIndex = 0; argIndex < arguments.size(); ++argIndex) {
                const auto argument = arguments[argIndex];
                if (!argument.isString()) {
                    addTypeError(errors,
                                 QString("%1.arguments[%2]").arg(bindingPath).arg(argIndex),
                                 QString("string, got %1").arg(jsonTypeName(argument)));
                    continue;
                }
                binding.arguments.push_back(argument.toString());
            }
        }

        bindings.push_back(binding);
    }
    return bindings;
}

QVector<ResourceRequirementDef> SequenceCompiler::parseResources(const QJsonArray& array,
                                                                 const QString& path,
                                                                 QVector<CompileError>& errors) const
{
    QVector<ResourceRequirementDef> resources;
    for (int i = 0; i < array.size(); ++i) {
        const auto resourcePath = QString("%1[%2]").arg(path).arg(i);
        if (!array[i].isObject()) {
            addError(errors, resourcePath, "Resource requirement must be an object");
            continue;
        }

        const auto object = array[i].toObject();
        ResourceRequirementDef resource;
        if (object.contains("resourceId")) {
            resource.resourceId = readString(object, "resourceId", resourcePath, errors);
        } else {
            resource.resourceId = readString(object, "name", resourcePath, errors);
        }

        bool modeOk = false;
        resource.mode = parseResourceModeString(readString(object, "mode", resourcePath, errors, "exclusive"), modeOk);
        if (!modeOk) {
            addError(errors,
                     resourcePath + ".mode",
                     "Unsupported resource mode",
                     "Use exclusive, sharedRead, sharedWrite, counted, or orderedExclusive");
        }

        resource.count = readInt(object, "count", resourcePath, errors, 1);
        resource.priority = readInt(object, "priority", resourcePath, errors, 0);
        resource.acquireTimeoutMs = readInt(object, "acquireTimeoutMs", resourcePath, errors, 30000);

        if (resource.resourceId.isEmpty()) {
            addError(errors, resourcePath + ".resourceId", "Resource id is required", "Use resourceId or name");
        }

        resources.push_back(resource);
    }
    return resources;
}

RetryPolicyDef SequenceCompiler::parseRetry(const QJsonObject& object,
                                            const QString& path,
                                            QVector<CompileError>& errors) const
{
    RetryPolicyDef retry;
    retry.maxAttempts = readInt(object, "maxAttempts", path, errors, 1);
    retry.delayMs = readInt(object, "delayMs", path, errors, 0);
    retry.retryWhenExpression = readString(object, "retryWhen", path, errors);
    return retry;
}

TimeoutPolicyDef SequenceCompiler::parseTimeout(const QJsonObject& object,
                                                const QString& path,
                                                QVector<CompileError>& errors) const
{
    TimeoutPolicyDef timeout;
    timeout.timeoutMs = readInt(object, "timeoutMs", path, errors, 0);
    return timeout;
}

LoopPolicyDef SequenceCompiler::parseLoopPolicy(const QJsonObject& object,
                                                const QString& path,
                                                QVector<CompileError>& errors) const
{
    LoopPolicyDef loop;
    const auto type = readString(object, "type", path, errors, "for");
    if (normalized(type) != "for") {
        addError(errors, childPath(path, "type"), "Unsupported loop type", "Use for");
    }

    loop.variableName = readString(object, "variable", path, errors, "i").trimmed();
    if (loop.variableName.trimmed().isEmpty()) {
        addError(errors, childPath(path, "variable"), "Loop variable is required", "Set a non-empty variable name");
    }

    loop.from = readInt(object, "from", path, errors, 0);
    loop.to = readInt(object, "to", path, errors, 0);
    loop.step = readInt(object, "step", path, errors, 1);
    if (loop.step == 0) {
        addError(errors, childPath(path, "step"), "Loop step must not be zero", "Use a positive or negative integer");
    }
    return loop;
}

ErrorPolicyDef SequenceCompiler::parseErrorPolicy(const QJsonObject& object,
                                                  const QString& path,
                                                  QVector<CompileError>& errors) const
{
    ErrorPolicyDef policy;
    bool actionOk = false;
    policy.onFail = parseFailureActionString(readString(object, "onFail", path, errors, "StopUut"), actionOk);
    if (!actionOk) {
        addError(errors, path + ".onFail", "Unsupported error action", "Use Continue, StopUut, Retry, RunCleanup, or Abort");
    }

    policy.onError = parseFailureActionString(readString(object, "onError", path, errors, "StopUut"), actionOk);
    if (!actionOk) {
        addError(errors, path + ".onError", "Unsupported error action", "Use Continue, StopUut, Retry, RunCleanup, or Abort");
    }

    policy.onTimeout = parseFailureActionString(readString(object, "onTimeout", path, errors, "StopUut"), actionOk);
    if (!actionOk) {
        addError(errors, path + ".onTimeout", "Unsupported error action", "Use Continue, StopUut, Retry, RunCleanup, or Abort");
    }

    policy.cleanupRegionId = readString(object, "cleanupRegionId", path, errors);
    policy.stopUutOnFailure = readBool(object, "stopUutOnFailure", path, errors, true);
    return policy;
}

BarrierPolicyDef SequenceCompiler::parseBarrierPolicy(const QJsonObject& object,
                                                      const QString& path,
                                                      const StepDef& step,
                                                      QVector<CompileError>& errors) const
{
    BarrierPolicyDef barrier;
    barrier.barrierName = readString(object, "barrierName", path, errors, step.id);
    barrier.cohortId = readString(object, "cohortId", path, errors, "default");
    barrier.expectedUutCount = readInt(object, "expectedUutCount", path, errors, -1);
    barrier.quorumCount = readInt(object, "quorumCount", path, errors, -1);
    barrier.quorumRatio = readDouble(object, "quorumRatio", path, errors, 1.0);
    barrier.arrivalTimeoutMs = readInt(object, "arrivalTimeoutMs", path, errors, 60000);
    barrier.releaseTimeoutMs = readInt(object, "releaseTimeoutMs", path, errors, 5000);

    bool policyOk = false;
    barrier.arrivalPolicy = parseArrivalPolicyString(readString(object, "arrivalPolicy", path, errors, "WaitAll"), policyOk);
    if (!policyOk) {
        addError(errors,
                 path + ".arrivalPolicy",
                 "Unsupported barrier arrival policy",
                 "Use WaitAll, DropFailed, CountFailed, Quorum, BestEffort, or ManualDecision");
    }

    barrier.releasePolicy = parseReleasePolicyString(readString(object, "releasePolicy", path, errors, "Lockstep"), policyOk);
    if (!policyOk) {
        addError(errors,
                 path + ".releasePolicy",
                 "Unsupported barrier release policy",
                 "Use Lockstep, Latch, Cohort, or RollingWindow");
    }

    barrier.failurePolicy = parseFailurePolicyString(readString(object, "failurePolicy", path, errors, "FailBarrier"), policyOk);
    if (!policyOk) {
        addError(errors,
                 path + ".failurePolicy",
                 "Unsupported barrier failure policy",
                 "Use FailBarrier, RemoveFailedMember, HoldFailedMember, ContinueWithWarning, or AbortCohort");
    }

    barrier.timeoutPolicy = parseTimeoutPolicyString(readString(object, "timeoutPolicy", path, errors, "FailArrivedAndWaiting"), policyOk);
    if (!policyOk) {
        addError(errors,
                 path + ".timeoutPolicy",
                 "Unsupported barrier timeout policy",
                 "Use FailArrivedAndWaiting, ReleaseArrived, ReleaseIfQuorumReached, AbortCohort, or RequestOperatorDecision");
    }

    barrier.releaseHeldResourcesOnWait = readBool(object, "releaseHeldResourcesOnWait", path, errors, true);
    return barrier;
}

void SequenceCompiler::addError(QVector<CompileError>& errors,
                                const QString& path,
                                const QString& message,
                                const QString& suggestion) const
{
    errors.push_back({path, message, suggestion});
}

} // namespace PicoATE::Core
