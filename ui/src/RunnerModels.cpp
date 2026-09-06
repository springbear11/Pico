#include "UiLanguage.h"
#include "RunnerModels.h"

#include "FunctionIconProvider.h"
#include "MeasurementDisplay.h"

#include "PicoATE/Core/MeasurementTypes.h"

#include <QBrush>
#include <QColor>
#include <QFont>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QStringList>

#include <algorithm>
#include <iterator>
#include <utility>

namespace PicoATE::Ui {

namespace {

QString diagnosticSeverityName(UiDiagnosticSeverity severity)
{
    return severity == UiDiagnosticSeverity::Error
        ? QStringLiteral("Error")
        : QStringLiteral("Warning");
}

QString activationStateName(PicoATE::Core::ActivationState state)
{
    using PicoATE::Core::ActivationState;
    switch (state) {
    case ActivationState::Created:
        return QStringLiteral("Pending");
    case ActivationState::WaitingForDependency:
        return QStringLiteral("Waiting dependency");
    case ActivationState::WaitingForResource:
        return QStringLiteral("Waiting resource");
    case ActivationState::WaitingForTimer:
        return QStringLiteral("Waiting timer");
    case ActivationState::WaitingAtBarrier:
        return QStringLiteral("Waiting barrier");
    case ActivationState::Ready:
        return QStringLiteral("Ready");
    case ActivationState::Running:
        return QStringLiteral("Running");
    case ActivationState::Passed:
        return QStringLiteral("Passed");
    case ActivationState::Failed:
        return QStringLiteral("Failed");
    case ActivationState::Error:
        return QStringLiteral("Error");
    case ActivationState::Timeout:
        return QStringLiteral("Timeout");
    case ActivationState::Cancelled:
        return QStringLiteral("Cancelled");
    case ActivationState::Skipped:
        return QStringLiteral("Skipped");
    }
    return QStringLiteral("Unknown");
}

QString attemptStateName(PicoATE::Core::AttemptState state)
{
    using PicoATE::Core::AttemptState;
    switch (state) {
    case AttemptState::Created:
        return QStringLiteral("Created");
    case AttemptState::Running:
        return QStringLiteral("Running");
    case AttemptState::Completed:
        return QStringLiteral("Completed");
    case AttemptState::Cancelled:
        return QStringLiteral("Cancelled");
    }
    return QStringLiteral("Unknown");
}

QString outcomeName(PicoATE::Core::NodeOutcome outcome)
{
    using PicoATE::Core::NodeOutcome;
    switch (outcome) {
    case NodeOutcome::Unknown:
        return QStringLiteral("Unknown");
    case NodeOutcome::Passed:
        return QStringLiteral("Passed");
    case NodeOutcome::Failed:
        return QStringLiteral("Failed");
    case NodeOutcome::Error:
        return QStringLiteral("Error");
    case NodeOutcome::Timeout:
        return QStringLiteral("Timeout");
    case NodeOutcome::Cancelled:
        return QStringLiteral("Cancelled");
    case NodeOutcome::Skipped:
        return QStringLiteral("Skipped");
    }
    return QStringLiteral("Unknown");
}

QString executionStateName(PicoATE::Core::ExecutionState state)
{
    using PicoATE::Core::ExecutionState;
    switch (state) {
    case ExecutionState::Idle: return QStringLiteral("Idle");
    case ExecutionState::Starting: return QStringLiteral("Starting");
    case ExecutionState::Running: return QStringLiteral("Running");
    case ExecutionState::Paused: return QStringLiteral("Paused");
    case ExecutionState::Stopping: return QStringLiteral("Stopping");
    case ExecutionState::CleaningUp: return QStringLiteral("Cleaning up");
    case ExecutionState::Completed: return QStringLiteral("Completed");
    case ExecutionState::CompletedWithError: return QStringLiteral("Completed with error");
    case ExecutionState::Aborted: return QStringLiteral("Aborted");
    }
    return QStringLiteral("Unknown");
}

QString execNodeKindName(PicoATE::Core::ExecNodeKind kind)
{
    using PicoATE::Core::ExecNodeKind;
    switch (kind) {
    case ExecNodeKind::Noop: return QStringLiteral("Noop");
    case ExecNodeKind::Wait: return QStringLiteral("Wait");
    case ExecNodeKind::Action: return QStringLiteral("Action");
    case ExecNodeKind::Barrier: return QStringLiteral("Barrier");
    case ExecNodeKind::Cleanup: return QStringLiteral("Cleanup");
    case ExecNodeKind::Loop: return QStringLiteral("Loop");
    case ExecNodeKind::TestItem: return QStringLiteral("TestItem");
    case ExecNodeKind::Limit: return QStringLiteral("Limit");
    case ExecNodeKind::Break: return QStringLiteral("Break If");
    case ExecNodeKind::Counter: return QStringLiteral("Counter");
    case ExecNodeKind::Aggregate: return QStringLiteral("Aggregate");
    case ExecNodeKind::OperatorPrompt: return QStringLiteral("MessageBox");
    case ExecNodeKind::Statement: return QStringLiteral("Statement");
    case ExecNodeKind::SequenceCall: return QStringLiteral("SequenceCall");
    }
    return QStringLiteral("Unknown");
}

QString debugPauseReasonName(PicoATE::Core::DebugPauseReason reason)
{
    using PicoATE::Core::DebugPauseReason;
    switch (reason) {
    case DebugPauseReason::None: return QStringLiteral("None");
    case DebugPauseReason::UserPause: return QStringLiteral("User pause");
    case DebugPauseReason::Breakpoint: return QStringLiteral("Breakpoint");
    case DebugPauseReason::StepInto: return QStringLiteral("Step into");
    case DebugPauseReason::StepOver: return QStringLiteral("Step over");
    }
    return QStringLiteral("Unknown");
}

QString resourceModeName(PicoATE::Core::ResourceMode mode)
{
    using PicoATE::Core::ResourceMode;
    switch (mode) {
    case ResourceMode::SharedRead: return QStringLiteral("SharedRead");
    case ResourceMode::SharedWrite: return QStringLiteral("SharedWrite");
    case ResourceMode::Exclusive: return QStringLiteral("Exclusive");
    case ResourceMode::Counted: return QStringLiteral("Counted");
    case ResourceMode::OrderedExclusive: return QStringLiteral("OrderedExclusive");
    }
    return QStringLiteral("Unknown");
}

QString barrierStateName(PicoATE::Core::BarrierState state)
{
    using PicoATE::Core::BarrierState;
    switch (state) {
    case BarrierState::Created: return QStringLiteral("Created");
    case BarrierState::Waiting: return QStringLiteral("Waiting");
    case BarrierState::Released: return QStringLiteral("Released");
    case BarrierState::Failed: return QStringLiteral("Failed");
    case BarrierState::TimedOut: return QStringLiteral("Timed out");
    }
    return QStringLiteral("Unknown");
}

QString variantText(const QVariant& value)
{
    if (!value.isValid()) {
        return {};
    }

    const auto json = QJsonValue::fromVariant(value);
    if (json.isObject()) {
        return QString::fromUtf8(
            QJsonDocument(json.toObject()).toJson(QJsonDocument::Compact));
    }
    if (json.isArray()) {
        return QString::fromUtf8(
            QJsonDocument(json.toArray()).toJson(QJsonDocument::Compact));
    }
    if (json.isString()) {
        return json.toString();
    }
    if (json.isBool()) {
        return json.toBool() ? QStringLiteral("true") : QStringLiteral("false");
    }
    if (json.isDouble()) {
        return QString::number(json.toDouble(), 'g', 12);
    }
    if (json.isNull()) {
        return QStringLiteral("null");
    }
    return value.toString();
}

QString uutSetText(const QSet<PicoATE::Core::UutId>& uuts)
{
    QStringList values;
    values.reserve(uuts.size());
    for (const auto& uutId : uuts) {
        values.push_back(uutId);
    }
    values.sort();
    return values.join(QStringLiteral(", "));
}

QString requirementsText(const QVector<PicoATE::Core::ResourceRequirement>& requirements)
{
    QStringList parts;
    parts.reserve(requirements.size());
    for (const auto& requirement : requirements) {
        parts.push_back(QStringLiteral("%1 %2 x%3 p%4")
                            .arg(requirement.resourceId,
                                 resourceModeName(requirement.mode))
                            .arg(requirement.count)
                            .arg(requirement.priority));
    }
    return parts.join(QStringLiteral("; "));
}

QString stringVectorText(const QVector<QString>& values)
{
    QStringList text;
    text.reserve(values.size());
    for (const auto& value : values) {
        text.push_back(value);
    }
    text.sort();
    return text.join(QStringLiteral(", "));
}

QString loopIterationDescription(const PicoATE::Core::LoopIterationContext& loop);

QString eventStateText(const PicoATE::Core::RuntimeEvent& event)
{
    using PicoATE::Core::RuntimeEventKind;
    switch (event.kind) {
    case RuntimeEventKind::SessionStateChanged:
        return executionStateName(event.executionState);
    case RuntimeEventKind::NodeStateChanged:
    case RuntimeEventKind::BarrierWaiting:
    case RuntimeEventKind::BarrierReleased:
    case RuntimeEventKind::CleanupActivated:
    case RuntimeEventKind::TestItemStarted:
    case RuntimeEventKind::TestItemCompleted:
        return activationStateName(event.activationState);
    case RuntimeEventKind::AttemptStarted:
        return attemptStateName(event.attemptState);
    case RuntimeEventKind::AttemptCompleted:
    case RuntimeEventKind::RetryScheduled:
    case RuntimeEventKind::LoopCompleted:
    case RuntimeEventKind::UutCompleted:
    case RuntimeEventKind::DebugStepCompleted:
        return outcomeName(event.outcome);
    case RuntimeEventKind::LoopIterationStarted:
        return loopIterationDescription(event.loopIteration);
    case RuntimeEventKind::DeviceStateChanged:
        return PicoATE::Core::deviceConnectionStateName(event.deviceState);
    case RuntimeEventKind::BreakpointHit:
        return QStringLiteral("Paused");
    case RuntimeEventKind::OperatorPromptRequested:
        return QStringLiteral("Waiting for operator");
    case RuntimeEventKind::OperatorPromptClosed:
        return QStringLiteral("Closed");
    case RuntimeEventKind::PeriodicTaskStateChanged:
        return PicoATE::Core::periodicTaskStateName(event.periodicTaskState);
    case RuntimeEventKind::ResourceStateChanged:
        return PicoATE::Core::resourceRuntimeStateName(event.resourceState);
    case RuntimeEventKind::UutRegistered:
    case RuntimeEventKind::ModuleLog:
        return {};
    }
    return {};
}

QString eventStepText(const PicoATE::Core::RuntimeEvent& event)
{
    if (!event.nodeDisplayName.isEmpty()) {
        return event.nodeDisplayName;
    }
    if (!event.nodeLocalId.isEmpty()) {
        return event.nodeLocalId;
    }
    return event.nodeId;
}

QString eventTypeText(const PicoATE::Core::RuntimeEvent& event)
{
    using PicoATE::Core::NodeOutcome;
    using PicoATE::Core::RuntimeEventKind;
    switch (event.kind) {
    case RuntimeEventKind::ModuleLog:
        return QStringLiteral("LOG");
    case RuntimeEventKind::AttemptStarted:
        return QStringLiteral("START");
    case RuntimeEventKind::AttemptCompleted:
        switch (event.outcome) {
        case NodeOutcome::Passed: return QStringLiteral("PASS");
        case NodeOutcome::Failed: return QStringLiteral("FAIL");
        case NodeOutcome::Error: return QStringLiteral("ERROR");
        case NodeOutcome::Timeout: return QStringLiteral("TIMEOUT");
        case NodeOutcome::Cancelled: return QStringLiteral("CANCELLED");
        case NodeOutcome::Skipped: return QStringLiteral("SKIPPED");
        case NodeOutcome::Unknown: return QStringLiteral("COMPLETE");
        }
        break;
    case RuntimeEventKind::RetryScheduled:
        return QStringLiteral("RETRY");
    case RuntimeEventKind::LoopIterationStarted:
    case RuntimeEventKind::LoopCompleted:
        return QStringLiteral("LOOP");
    case RuntimeEventKind::BreakpointHit:
    case RuntimeEventKind::DebugStepCompleted:
        return QStringLiteral("DEBUG");
    case RuntimeEventKind::DeviceStateChanged:
        return QStringLiteral("DEVICE");
    case RuntimeEventKind::OperatorPromptRequested:
    case RuntimeEventKind::OperatorPromptClosed:
        return QStringLiteral("PROMPT");
    case RuntimeEventKind::PeriodicTaskStateChanged:
        return QStringLiteral("PERIODIC");
    case RuntimeEventKind::ResourceStateChanged:
        return QStringLiteral("RESOURCE");
    default:
        return QStringLiteral("FLOW");
    }
    return QStringLiteral("FLOW");
}

QString eventDetailText(const PicoATE::Core::RuntimeEvent& event)
{
    QStringList parts;
    if (!event.message.isEmpty()) {
        parts.push_back(event.message);
    }
    if (event.kind == PicoATE::Core::RuntimeEventKind::ModuleLog) {
        const auto dropped = event.details.value(QStringLiteral("droppedBefore")).toULongLong();
        if (dropped > 0) {
            parts.push_back(QStringLiteral("%1 earlier log record(s) dropped").arg(dropped));
        }
        return parts.join(QStringLiteral(" | "));
    }
    if (event.attemptIndex > 0) {
        parts.push_back(QStringLiteral("attempt=%1").arg(event.attemptIndex));
    }
    if (event.loopIteration.active) {
        parts.push_back(loopIterationDescription(event.loopIteration));
    }
    if (!event.errorCode.isEmpty()) {
        parts.push_back(QStringLiteral("error=%1").arg(event.errorCode));
    }
    if (!event.measurements.isEmpty()) {
        parts.push_back(QStringLiteral("measurements=%1").arg(event.measurements.size()));
    }
    if (!event.details.isEmpty()) {
        parts.push_back(QStringLiteral("details=%1").arg(
            variantText(QVariant::fromValue(event.details))));
    }
    return parts.join(QStringLiteral(" | "));
}

QBrush outcomeBrush(PicoATE::Core::NodeOutcome outcome)
{
    using PicoATE::Core::NodeOutcome;
    switch (outcome) {
    case NodeOutcome::Passed:
        return QBrush(QColor(QStringLiteral("#27844b")));
    case NodeOutcome::Failed:
    case NodeOutcome::Error:
    case NodeOutcome::Timeout:
        return QBrush(QColor(QStringLiteral("#b43a3a")));
    case NodeOutcome::Skipped:
    case NodeOutcome::Cancelled:
        return QBrush(QColor(QStringLiteral("#a56600")));
    default:
        return QBrush(QColor(QStringLiteral("#62707d")));
    }
}

QString loopDescription(const PicoATE::Core::StepLoopReport& loop)
{
    if (!loop.inLoop) {
        return {};
    }
    return QStringLiteral("%1 / %2=%3..%4 step %5")
        .arg(loop.loopId, loop.variableName)
        .arg(loop.from)
        .arg(loop.to)
        .arg(loop.step);
}

QString loopIterationDescription(const PicoATE::Core::LoopIterationContext& loop)
{
    if (!loop.active) {
        return {};
    }
    return QStringLiteral("#%1 / %2=%3")
        .arg(loop.iterationNumber)
        .arg(loop.variableName)
        .arg(loop.value);
}

QString measurementLimits(const PicoATE::Core::MeasurementResult& measurement)
{
    return measurementLimitsDisplay(measurement);
}

template <typename Formatter>
QString joinedMeasurementText(
    const QVector<PicoATE::Core::MeasurementResult>& measurements,
    Formatter formatter)
{
    if (measurements.isEmpty()) {
        return QStringLiteral("-");
    }
    if (measurements.size() == 1) {
        return formatter(measurements.first());
    }
    QStringList values;
    values.reserve(measurements.size());
    for (const auto& measurement : measurements) {
        values.push_back(QStringLiteral("%1=%2").arg(
            measurement.name.isEmpty() ? QStringLiteral("value") : measurement.name,
            formatter(measurement)));
    }
    return values.join(QStringLiteral("; "));
}

QString joinedParsedText(
    const QVector<PicoATE::Core::MeasurementResult>& measurements)
{
    if (measurements.isEmpty()) {
        return QStringLiteral("-");
    }
    if (!measurements.first().attributes
             .value(QStringLiteral("parserDisplay")).toBool()) {
        return joinedMeasurementText(measurements, measurementActualDisplay);
    }

    if (measurements.size() == 1) {
        return measurementActualDisplay(measurements.first());
    }
    QStringList fields;
    fields.reserve(measurements.size());
    for (const auto& measurement : measurements) {
        fields.push_back(QStringLiteral("%1=%2").arg(
            measurement.name.isEmpty() ? QStringLiteral("value")
                                       : measurement.name,
            measurementActualDisplay(measurement)));
    }
    return fields.join(QStringLiteral("; "));
}

QVariantMap parserSelectionDisplay(
    const QVector<PicoATE::Core::MeasurementResult>& measurements)
{
    if (measurements.isEmpty()) {
        return {};
    }
    auto display = measurements.first().attributes
                       .value(QStringLiteral("parserSelectionDisplay"))
                       .toMap();
    if (!display.isEmpty()) {
        display.insert(QStringLiteral("parsedDisplay"),
                       joinedParsedText(measurements));
    }
    return display;
}

QString joinedActualText(
    const QVector<PicoATE::Core::MeasurementResult>& measurements)
{
    if (measurements.isEmpty() ||
        !measurements.first().attributes
             .value(QStringLiteral("parserDisplay")).toBool()) {
        return joinedParsedText(measurements);
    }

    const auto original = measurements.first().attributes
                              .value(QStringLiteral("parserOriginalDisplay"))
                              .toString();
    const auto parsed = joinedParsedText(measurements);
    return QStringLiteral("Raw: %1 | Parsed: %2").arg(original, parsed);
}

QString lowerLimitText(const PicoATE::Core::MeasurementResult& measurement)
{
    return measurementLowerLimitDisplay(measurement);
}

QString upperLimitText(const PicoATE::Core::MeasurementResult& measurement)
{
    return measurementUpperLimitDisplay(measurement);
}

QString stepErrorCode(const PicoATE::Core::StepReport& step)
{
    if (step.outcome == PicoATE::Core::NodeOutcome::Unknown) {
        return QStringLiteral("-");
    }
    if (!step.attempts.isEmpty() && !step.attempts.last().errorCode.isEmpty()) {
        return step.attempts.last().errorCode;
    }
    for (const auto& measurement : step.measurements) {
        if (!measurement.errorCode.isEmpty()) {
            return measurement.errorCode;
        }
    }
    return QStringLiteral("-");
}

bool runtimeEventUpdatesStep(const PicoATE::Core::RuntimeEvent& event)
{
    using PicoATE::Core::RuntimeEventKind;
    switch (event.kind) {
    case RuntimeEventKind::NodeStateChanged:
    case RuntimeEventKind::BarrierWaiting:
    case RuntimeEventKind::BarrierReleased:
    case RuntimeEventKind::CleanupActivated:
    case RuntimeEventKind::LoopIterationStarted:
    case RuntimeEventKind::LoopCompleted:
    case RuntimeEventKind::TestItemStarted:
    case RuntimeEventKind::TestItemCompleted:
    case RuntimeEventKind::AttemptStarted:
    case RuntimeEventKind::AttemptCompleted:
        return true;
    default:
        return false;
    }
}

bool isCompositeRuntimeStep(const PicoATE::Core::StepReport& step)
{
    return step.kind == PicoATE::Core::ExecNodeKind::TestItem ||
           step.kind == PicoATE::Core::ExecNodeKind::Loop;
}

void clearMeasurementRuntimeValue(PicoATE::Core::MeasurementResult& measurement)
{
    measurement.value = {};
    measurement.rawValue = {};
    measurement.status = PicoATE::Core::MeasurementStatus::Unknown;
    measurement.errorCode.clear();
    measurement.errorMessage.clear();
}

void resetCurrentStepResult(PicoATE::Core::StepReport& step,
                            bool resetDescendants)
{
    step.outcome = PicoATE::Core::NodeOutcome::Unknown;
    step.durationMs = -1;
    step.wasError = false;
    for (auto& measurement : step.measurements) {
        clearMeasurementRuntimeValue(measurement);
    }
    if (!resetDescendants) {
        return;
    }
    for (auto& child : step.children) {
        child.state = PicoATE::Core::ActivationState::Created;
        resetCurrentStepResult(child, true);
    }
}

void detachStepTree(QVector<PicoATE::Core::StepReport>& steps)
{
    steps.detach();
    for (auto& step : steps) {
        detachStepTree(step.children);
    }
}

QString durationText(qint64 durationMs)
{
    return durationMs < 0
        ? QStringLiteral("-")
        : QStringLiteral("%1 s").arg(durationMs / 1000.0, 0, 'f', 3);
}

QBrush diagnosticBrush(UiDiagnosticSeverity severity)
{
    return QBrush(QColor(severity == UiDiagnosticSeverity::Error
                             ? QStringLiteral("#b43a3a")
                             : QStringLiteral("#a56600")));
}

int totalAttemptCount(const PicoATE::Core::StepReport& step)
{
    int total = step.attempts.size();
    for (const auto& child : step.children) {
        total += totalAttemptCount(child);
    }
    return total;
}

int totalStepCount(const QVector<PicoATE::Core::StepReport>& steps)
{
    int total = 0;
    for (const auto& step : steps) {
        ++total;
        total += totalStepCount(step.children);
    }
    return total;
}

void collectTerminalStepIds(const QVector<PicoATE::Core::StepReport>& steps,
                            QSet<PicoATE::Core::NodeId>& known,
                            QSet<PicoATE::Core::NodeId>& terminal)
{
    for (const auto& step : steps) {
        const auto id = step.nodePath.isEmpty() ? step.stepId : step.nodePath;
        if (!id.isEmpty()) {
            known.insert(id);
            if (PicoATE::Core::isTerminalActivation(step.state)) {
                terminal.insert(id);
            }
        }
        collectTerminalStepIds(step.children, known, terminal);
    }
}

void appendRecentOverviewStep(
    QVector<UutOverviewRecentStep>& recentSteps,
    UutOverviewRecentStep step)
{
    if (step.nodeId.isEmpty()) {
        return;
    }
    if (step.displayName.isEmpty()) {
        step.displayName = step.nodeId;
    }
    for (int index = recentSteps.size() - 1; index >= 0; --index) {
        if (recentSteps.at(index).nodeId == step.nodeId) {
            recentSteps.removeAt(index);
        }
    }
    recentSteps.push_back(std::move(step));
    while (recentSteps.size() > 3) {
        recentSteps.removeFirst();
    }
}

void collectRecentReportSteps(
    const QVector<PicoATE::Core::StepReport>& steps,
    QVector<UutOverviewRecentStep>& recentSteps)
{
    for (const auto& step : steps) {
        collectRecentReportSteps(step.children, recentSteps);
        if (!PicoATE::Core::isTerminalActivation(step.state)) {
            continue;
        }
        appendRecentOverviewStep(
            recentSteps,
            {step.nodePath.isEmpty() ? step.stepId : step.nodePath,
             step.displayName,
             step.state,
             step.outcome});
    }
}

bool failedOutcome(PicoATE::Core::NodeOutcome outcome)
{
    return outcome == PicoATE::Core::NodeOutcome::Failed ||
           outcome == PicoATE::Core::NodeOutcome::Error ||
           outcome == PicoATE::Core::NodeOutcome::Timeout;
}

bool failedActivation(PicoATE::Core::ActivationState state)
{
    return state == PicoATE::Core::ActivationState::Failed ||
           state == PicoATE::Core::ActivationState::Error ||
           state == PicoATE::Core::ActivationState::Timeout;
}

bool terminalOverviewState(UutOverviewState state)
{
    return state == UutOverviewState::Disabled ||
           state == UutOverviewState::Passed ||
           state == UutOverviewState::Failed ||
           state == UutOverviewState::Stopped;
}

bool findFirstFailedReportStep(
    const QVector<PicoATE::Core::StepReport>& steps,
    UutOverviewEntry& entry)
{
    for (const auto& step : steps) {
        if (findFirstFailedReportStep(step.children, entry)) {
            return true;
        }
        if (!step.wasError && !failedOutcome(step.outcome) &&
            !failedActivation(step.state)) {
            continue;
        }
        entry.failedNodeId = step.nodePath.isEmpty() ? step.stepId
                                                     : step.nodePath;
        entry.failedStep = step.displayName.isEmpty() ? step.stepId
                                                      : step.displayName;
        entry.failedPhase = step.phase;
        for (const auto& attempt : step.attempts) {
            if (!failedOutcome(attempt.outcome)) {
                continue;
            }
            entry.errorCode = attempt.errorCode;
            entry.message = attempt.errorMessage;
            break;
        }
        return true;
    }
    return false;
}

bool overviewTracksNodeEvent(PicoATE::Core::RuntimeEventKind kind)
{
    using PicoATE::Core::RuntimeEventKind;
    switch (kind) {
    case RuntimeEventKind::NodeStateChanged:
    case RuntimeEventKind::AttemptStarted:
    case RuntimeEventKind::AttemptCompleted:
    case RuntimeEventKind::RetryScheduled:
    case RuntimeEventKind::LoopIterationStarted:
    case RuntimeEventKind::LoopCompleted:
    case RuntimeEventKind::TestItemStarted:
    case RuntimeEventKind::TestItemCompleted:
    case RuntimeEventKind::BarrierWaiting:
    case RuntimeEventKind::BarrierReleased:
        return true;
    default:
        return false;
    }
}

bool applyPeriodicTaskEvent(
    QVector<PeriodicTaskOverviewEntry>& tasks,
    const PicoATE::Core::RuntimeEvent& event)
{
    auto taskId = event.periodicTaskId.trimmed();
    if (taskId.isEmpty()) {
        taskId = event.nodeId;
    }
    if (taskId.isEmpty()) {
        return false;
    }

    auto task = std::find_if(
        tasks.begin(), tasks.end(), [&taskId](const auto& candidate) {
            return candidate.taskInstanceId == taskId;
        });
    if (task == tasks.end()) {
        PeriodicTaskOverviewEntry created;
        created.taskInstanceId = taskId;
        tasks.push_back(std::move(created));
        task = std::prev(tasks.end());
    }

    task->nodeId = event.nodeId;
    task->displayName = event.nodeDisplayName.trimmed().isEmpty()
        ? (event.nodeLocalId.trimmed().isEmpty() ? event.nodeId
                                                : event.nodeLocalId)
        : event.nodeDisplayName;
    task->state =
        event.periodicTaskState == PicoATE::Core::PeriodicTaskState::Passed ||
                event.periodicTaskState == PicoATE::Core::PeriodicTaskState::Failed
            ? PicoATE::Core::PeriodicTaskState::Waiting
            : event.periodicTaskState;
    task->intervalMs = event.periodicIntervalMs;
    task->invocationIndex = event.periodicInvocationIndex;
    task->counter = event.periodicCounter;
    task->nextDueAtUtc = event.periodicNextDueAtUtc;
    task->updatedAtUtc = event.timestampUtc.isValid()
        ? event.timestampUtc
        : QDateTime::currentDateTimeUtc();

    if (event.outcome == PicoATE::Core::NodeOutcome::Passed ||
        event.outcome == PicoATE::Core::NodeOutcome::Failed ||
        event.outcome == PicoATE::Core::NodeOutcome::Error ||
        event.outcome == PicoATE::Core::NodeOutcome::Timeout) {
        task->lastOutcome = event.outcome;
    }
    if (event.outcome == PicoATE::Core::NodeOutcome::Passed) {
        task->errorCode.clear();
        task->message.clear();
    } else if (!event.errorCode.trimmed().isEmpty() ||
               !event.message.trimmed().isEmpty()) {
        task->errorCode = event.errorCode;
        task->message = event.message;
    }
    return true;
}

bool removeResourceEntry(QVector<ResourceUsageOverviewEntry>& entries,
                         const PicoATE::Core::ResourceRequestId& requestId,
                         const PicoATE::Core::ResourceLeaseId& leaseId)
{
    bool changed = false;
    for (qsizetype index = entries.size() - 1; index >= 0; --index) {
        const auto& entry = entries.at(index);
        const bool requestMatches = !requestId.isEmpty() &&
                                    entry.requestId == requestId;
        const bool leaseMatches = !leaseId.isEmpty() &&
                                  entry.leaseId == leaseId;
        if (requestMatches || leaseMatches) {
            entries.removeAt(index);
            changed = true;
        }
    }
    return changed;
}

bool upsertResourceEntry(QVector<ResourceUsageOverviewEntry>& entries,
                         const PicoATE::Core::RuntimeEvent& event)
{
    auto found = std::find_if(
        entries.begin(), entries.end(), [&event](const auto& entry) {
            if (!event.resourceLeaseId.isEmpty()) {
                return entry.leaseId == event.resourceLeaseId;
            }
            return entry.requestId == event.requestId;
        });
    if (found == entries.end()) {
        entries.push_back({});
        found = std::prev(entries.end());
    }
    found->requestId = event.requestId;
    found->leaseId = event.resourceLeaseId;
    found->uutId = event.uutId;
    found->nodeId = event.nodeId;
    found->resourceIds = event.resourceIds;
    found->blockingUutIds = event.resourceBlockingUutIds;
    found->waitingSinceUtc = event.resourceWaitingSinceUtc;
    found->updatedAtUtc = event.timestampUtc.isValid()
        ? event.timestampUtc
        : QDateTime::currentDateTimeUtc();
    return true;
}

bool applyResourceEvent(QVector<ResourceUsageOverviewEntry>& held,
                        QVector<ResourceUsageOverviewEntry>& waiting,
                        const PicoATE::Core::RuntimeEvent& event)
{
    bool changed = false;
    switch (event.resourceState) {
    case PicoATE::Core::ResourceRuntimeState::Waiting:
        changed = removeResourceEntry(held, event.requestId,
                                      event.resourceLeaseId) || changed;
        changed = upsertResourceEntry(waiting, event) || changed;
        break;
    case PicoATE::Core::ResourceRuntimeState::Acquired:
        changed = removeResourceEntry(waiting, event.requestId,
                                      event.resourceLeaseId) || changed;
        changed = upsertResourceEntry(held, event) || changed;
        break;
    case PicoATE::Core::ResourceRuntimeState::Released:
        changed = removeResourceEntry(held, event.requestId,
                                      event.resourceLeaseId) || changed;
        break;
    case PicoATE::Core::ResourceRuntimeState::Cancelled:
        changed = removeResourceEntry(waiting, event.requestId,
                                      event.resourceLeaseId) || changed;
        break;
    }
    return changed;
}

} // namespace

QString uutOverviewStateName(UutOverviewState state)
{
    switch (state) {
    case UutOverviewState::Disabled: return QStringLiteral("Disabled");
    case UutOverviewState::Waiting: return QStringLiteral("Waiting");
    case UutOverviewState::Running: return QStringLiteral("Running");
    case UutOverviewState::Paused: return QStringLiteral("Paused");
    case UutOverviewState::Passed: return QStringLiteral("Pass");
    case UutOverviewState::Failed: return QStringLiteral("Fail");
    case UutOverviewState::Stopped: return QStringLiteral("Stopped");
    }
    return QStringLiteral("Waiting");
}

UutOverviewModel::UutOverviewModel(QObject* parent)
    : QAbstractTableModel(parent)
{
    enableUiModelTranslation(this);
}

int UutOverviewModel::rowCount(const QModelIndex& parent) const
{
    return parent.isValid() ? 0 : m_rows.size();
}

int UutOverviewModel::columnCount(const QModelIndex& parent) const
{
    return parent.isValid() ? 0 : ColumnCount;
}

QVariant UutOverviewModel::data(const QModelIndex& index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_rows.size()) {
        return {};
    }
    const auto& entry = m_rows[index.row()].entry;
    switch (role) {
    case UutIdRole: return entry.uutId;
    case SerialNumberRole: return entry.serialNumber;
    case StateRole: return static_cast<int>(entry.state);
    case CurrentStepRole: return entry.currentStep;
    case CurrentNodeIdRole: return entry.currentNodeId;
    case ErrorCodeRole: return entry.errorCode;
    case MessageRole: return entry.message;
    case CompletedStepsRole: return entry.completedSteps;
    case TotalStepsRole: return entry.totalSteps;
    case ProgressRole: return entry.progress;
    case DurationMsRole: return entry.durationMs;
    case RetryActiveRole: return entry.retryActive;
    case RetryAttemptRole: return entry.retryAttempt;
    case RetryMaxAttemptsRole: return entry.retryMaxAttempts;
    case EnabledRole: return entry.enabled;
    default:
        break;
    }
    if (role != Qt::DisplayRole) {
        return {};
    }
    switch (index.column()) {
    case UutColumn: return entry.uutId;
    case SerialNumberColumn:
        return entry.serialNumber.isEmpty() ? QStringLiteral("--")
                                            : entry.serialNumber;
    case StateColumn: return uiStateText(uutOverviewStateName(entry.state));
    case CurrentStepColumn:
        return entry.currentStep.isEmpty() ? uiText("Waiting to start")
                                           : entry.currentStep;
    case ProgressColumn:
        return QStringLiteral("%1 / %2 (%3%)")
            .arg(entry.completedSteps)
            .arg(entry.totalSteps)
            .arg(entry.progress);
    case DurationColumn: return durationText(entry.durationMs);
    default: return {};
    }
}

QVariant UutOverviewModel::headerData(int section,
                                      Qt::Orientation orientation,
                                      int role) const
{
    if (orientation != Qt::Horizontal || role != Qt::DisplayRole) {
        return {};
    }
    switch (section) {
    case UutColumn: return uiText("UUT");
    case SerialNumberColumn: return uiText("SN");
    case StateColumn: return uiText("State");
    case CurrentStepColumn: return uiText("Current Step");
    case ProgressColumn: return uiText("Progress");
    case DurationColumn: return uiText("Time");
    default: return {};
    }
}

void UutOverviewModel::resetForRun(
    const PicoATE::Core::ExecutionReport& preview,
    const QVector<RunRequest::UutInput>& uuts)
{
    beginResetModel();
    m_rows.clear();
    m_terminalElapsedMs.clear();
    m_sharedPeriodicTasks.clear();
    m_sharedHeldResources.clear();
    m_sharedWaitingResources.clear();
    m_sessionElapsedMs = 0;
    m_previewStepCount = preview.uuts.isEmpty()
        ? 0
        : totalStepCount(preview.uuts.first().steps);

    auto append = [this](const PicoATE::Core::UutId& uutId,
                         const QString& serialNumber,
                         int totalSteps,
                         bool enabled) {
        Row row;
        row.entry.uutId = uutId;
        row.entry.serialNumber = serialNumber;
        row.entry.enabled = enabled;
        row.entry.state = enabled ? UutOverviewState::Waiting
                                  : UutOverviewState::Disabled;
        row.entry.totalSteps = totalSteps > 0 ? totalSteps : m_previewStepCount;
        m_rows.push_back(std::move(row));
    };

    if (!uuts.isEmpty()) {
        for (const auto& uut : uuts) {
            auto serialNumber = uut.variables.value(
                QStringLiteral("serialNumber")).toString().trimmed();
            if (serialNumber.isEmpty()) {
                serialNumber = uut.variables.value(
                    QStringLiteral("sn")).toString().trimmed();
            }
            append(uut.uutId, serialNumber, m_previewStepCount, uut.enabled);
        }
    } else {
        for (const auto& uut : preview.uuts) {
            append(uut.uutId, uut.serialNumber, totalStepCount(uut.steps), true);
        }
    }
    endResetModel();
}

void UutOverviewModel::setSessionElapsedMs(qint64 elapsedMs)
{
    m_sessionElapsedMs = qMax<qint64>(0, elapsedMs);
}

void UutOverviewModel::setReport(const PicoATE::Core::ExecutionReport& report)
{
    QHash<PicoATE::Core::UutId, UutOverviewEntry> previousEntries;
    QVector<UutOverviewEntry> previousOrder;
    previousOrder.reserve(m_rows.size());
    for (const auto& row : std::as_const(m_rows)) {
        previousEntries.insert(row.entry.uutId, row.entry);
        previousOrder.push_back(row.entry);
    }
    beginResetModel();
    m_rows.clear();
    m_sharedHeldResources.clear();
    m_sharedWaitingResources.clear();
    QSet<PicoATE::Core::UutId> reportedUutIds;
    for (const auto& uut : report.uuts) {
        Row row;
        row.entry.uutId = uut.uutId;
        row.entry.serialNumber = uut.serialNumber;
        row.entry.totalSteps = totalStepCount(uut.steps);
        row.entry.enabled = true;
        reportedUutIds.insert(uut.uutId);
        const auto previous = previousEntries.constFind(uut.uutId);
        if (previous != previousEntries.constEnd()) {
            row.entry.enabled = previous->enabled;
            row.entry.currentStep = previous->currentStep;
            row.entry.currentNodeId = previous->currentNodeId;
            row.entry.currentPhase = previous->currentPhase;
            row.entry.failedStep = previous->failedStep;
            row.entry.failedNodeId = previous->failedNodeId;
            row.entry.failedPhase = previous->failedPhase;
            row.entry.errorCode = previous->errorCode;
            row.entry.message = previous->message;
            row.entry.recentSteps = previous->recentSteps;
            row.entry.periodicTasks = previous->periodicTasks;
            if (!uut.completed) {
                row.entry.retryActive = previous->retryActive;
                row.entry.retryAttempt = previous->retryAttempt;
                row.entry.retryMaxAttempts = previous->retryMaxAttempts;
            }
        }
        if (row.entry.recentSteps.isEmpty()) {
            collectRecentReportSteps(uut.steps, row.entry.recentSteps);
        }
        if (row.entry.currentStep.isEmpty() && !row.entry.recentSteps.isEmpty()) {
            const auto& recent = row.entry.recentSteps.constLast();
            row.entry.currentNodeId = recent.nodeId;
            row.entry.currentStep = recent.displayName;
        }
        if ((uut.hasError || failedOutcome(uut.outcome)) &&
            row.entry.failedNodeId.isEmpty()) {
            findFirstFailedReportStep(uut.steps, row.entry);
        }
        collectTerminalStepIds(uut.steps, row.knownNodes, row.terminalNodes);
        if (uut.completed) {
            if (uut.outcome == PicoATE::Core::NodeOutcome::Cancelled ||
                report.state == PicoATE::Core::ExecutionState::Aborted) {
                row.entry.state = UutOverviewState::Stopped;
            } else if (uut.hasError || failedOutcome(uut.outcome)) {
                row.entry.state = UutOverviewState::Failed;
            } else {
                row.entry.state = UutOverviewState::Passed;
                row.entry.failedStep.clear();
                row.entry.failedNodeId.clear();
                row.entry.errorCode.clear();
                row.entry.message.clear();
            }
            auto terminalElapsed = m_terminalElapsedMs.value(uut.uutId, -1);
            if (terminalElapsed < 0) {
                terminalElapsed = m_sessionElapsedMs >= 0
                    ? m_sessionElapsedMs
                    : (report.metadata.durationMs >= 0
                           ? report.metadata.durationMs
                           : qMax<qint64>(0, uut.durationMs));
                m_terminalElapsedMs.insert(uut.uutId, terminalElapsed);
            }
            row.entry.durationMs = terminalElapsed;
        } else {
            row.entry.durationMs = qMax<qint64>(0, m_sessionElapsedMs);
        }
        updateDerivedValues(row);
        m_rows.push_back(std::move(row));
    }
    for (int previousIndex = 0; previousIndex < previousOrder.size();
         ++previousIndex) {
        const auto& previous = previousOrder.at(previousIndex);
        if (previous.enabled || reportedUutIds.contains(previous.uutId)) {
            continue;
        }
        Row disabled;
        disabled.entry = previous;
        disabled.entry.enabled = false;
        disabled.entry.state = UutOverviewState::Disabled;
        disabled.entry.currentStep.clear();
        disabled.entry.currentNodeId.clear();
        disabled.entry.failedStep.clear();
        disabled.entry.failedNodeId.clear();
        disabled.entry.errorCode.clear();
        disabled.entry.message.clear();
        disabled.entry.completedSteps = 0;
        disabled.entry.progress = 0;
        disabled.entry.retryActive = false;
        disabled.entry.retryAttempt = 0;
        disabled.entry.retryMaxAttempts = 0;
        disabled.entry.recentSteps.clear();
        disabled.entry.periodicTasks.clear();
        disabled.entry.heldResources.clear();
        disabled.entry.waitingResources.clear();
        disabled.entry.durationMs = 0;
        m_rows.insert(qMin(previousIndex, m_rows.size()),
                      std::move(disabled));
    }
    endResetModel();
}

void UutOverviewModel::applyRuntimeEvents(
    const QVector<PicoATE::Core::RuntimeEvent>& events)
{
    QSet<int> dirtyRows;
    bool sharedPeriodicChanged = false;
    bool sharedResourceStateChanged = false;
    const auto clearFailure = [](Row& row) {
        row.entry.failedStep.clear();
        row.entry.failedNodeId.clear();
        row.entry.failedPhase = PicoATE::Core::ExecutionPhase::Main;
        row.entry.errorCode.clear();
        row.entry.message.clear();
        row.failedParentNodeId.clear();
    };
    const auto applyNodeEvent = [this, &dirtyRows, &clearFailure](
                                    Row& row,
                                    int rowIndex,
                                    const PicoATE::Core::RuntimeEvent& event,
                                    bool contributesToUutProgress) {
        const auto previousState = row.entry.state;
        const bool preserveTerminalState = terminalOverviewState(previousState);
        const auto displayName = event.nodeDisplayName.isEmpty()
            ? (event.nodeLocalId.isEmpty() ? event.nodeId : event.nodeLocalId)
            : event.nodeDisplayName;
        const bool skipped =
            event.activationState == PicoATE::Core::ActivationState::Skipped;

        if (contributesToUutProgress) {
            row.knownNodes.insert(event.nodeId);
        }
        if (!skipped) {
            row.entry.currentNodeId = event.nodeId;
            row.entry.currentStep = displayName;
            row.entry.currentPhase = event.nodePhase;
        }

        const int maximumAttempts = qMax(
            1, event.details.value(QStringLiteral("maxAttempts"), 1).toInt());
        const int currentAttempt = qMax(
            1, event.details.value(QStringLiteral("retryAttemptIndex"),
                                   event.attemptIndex).toInt());
        if (event.kind == PicoATE::Core::RuntimeEventKind::RetryScheduled) {
            row.entry.retryActive = maximumAttempts > 1;
            row.entry.retryAttempt = qMin(currentAttempt + 1, maximumAttempts);
            row.entry.retryMaxAttempts = maximumAttempts;
            row.retryNodeId = row.entry.retryActive ? event.nodeId
                                                    : PicoATE::Core::NodeId{};
        } else if (event.kind == PicoATE::Core::RuntimeEventKind::AttemptStarted ||
                   event.kind == PicoATE::Core::RuntimeEventKind::TestItemStarted) {
            const bool retryStarted = maximumAttempts > 1 && currentAttempt > 1;
            if (retryStarted) {
                row.entry.retryActive = true;
                row.entry.retryAttempt = currentAttempt;
                row.entry.retryMaxAttempts = maximumAttempts;
                row.retryNodeId = event.nodeId;
            }
        } else if (event.kind == PicoATE::Core::RuntimeEventKind::AttemptCompleted ||
                   event.kind == PicoATE::Core::RuntimeEventKind::TestItemCompleted) {
            const bool anotherAttempt = failedOutcome(event.outcome) &&
                                        currentAttempt < maximumAttempts;
            const bool completesTrackedRetry = row.retryNodeId == event.nodeId;
            if (anotherAttempt) {
                row.entry.retryActive = true;
                row.entry.retryAttempt = currentAttempt + 1;
                row.entry.retryMaxAttempts = maximumAttempts;
                row.retryNodeId = event.nodeId;
            } else if (completesTrackedRetry) {
                row.entry.retryActive = false;
                row.entry.retryAttempt = 0;
                row.entry.retryMaxAttempts = 0;
                row.retryNodeId.clear();
                const bool recoveredFailure =
                    row.entry.failedNodeId == event.nodeId ||
                    row.failedParentNodeId == event.nodeId;
                if (!failedOutcome(event.outcome) && recoveredFailure) {
                    clearFailure(row);
                }
            }
        }

        const bool terminalEvent =
            (event.kind == PicoATE::Core::RuntimeEventKind::NodeStateChanged ||
             event.kind == PicoATE::Core::RuntimeEventKind::TestItemCompleted ||
             event.kind == PicoATE::Core::RuntimeEventKind::LoopCompleted) &&
            PicoATE::Core::isTerminalActivation(event.activationState);
        if (terminalEvent) {
            if (contributesToUutProgress) {
                row.terminalNodes.insert(event.nodeId);
            }
            appendRecentOverviewStep(
                row.entry.recentSteps,
                {event.nodeId, displayName, event.activationState, event.outcome});
        } else if (contributesToUutProgress &&
                   (event.kind == PicoATE::Core::RuntimeEventKind::AttemptStarted ||
                    event.kind == PicoATE::Core::RuntimeEventKind::RetryScheduled ||
                    event.kind == PicoATE::Core::RuntimeEventKind::LoopIterationStarted ||
                    !PicoATE::Core::isTerminalActivation(event.activationState))) {
            row.terminalNodes.remove(event.nodeId);
        }

        const bool eventFailed = failedOutcome(event.outcome) ||
                                 failedActivation(event.activationState);
        if (eventFailed) {
            if (row.entry.failedNodeId.isEmpty()) {
                row.entry.failedNodeId = event.nodeId;
                row.entry.failedStep = displayName;
                row.entry.failedPhase = event.nodePhase;
                row.failedParentNodeId = event.parentNodeId;
                row.entry.errorCode = event.errorCode;
                row.entry.message = event.message;
            } else if (row.entry.failedNodeId == event.nodeId) {
                if (row.entry.errorCode.trimmed().isEmpty()) {
                    row.entry.errorCode = event.errorCode;
                }
                if (row.entry.message.trimmed().isEmpty()) {
                    row.entry.message = event.message;
                }
            }
        }

        const bool waiting =
            event.kind == PicoATE::Core::RuntimeEventKind::BarrierWaiting ||
            event.activationState == PicoATE::Core::ActivationState::WaitingForDependency ||
            event.activationState == PicoATE::Core::ActivationState::WaitingForResource ||
            event.activationState == PicoATE::Core::ActivationState::WaitingForTimer ||
            event.activationState == PicoATE::Core::ActivationState::WaitingAtBarrier;
        if (preserveTerminalState) {
            row.entry.state = previousState == UutOverviewState::Passed && eventFailed
                ? UutOverviewState::Failed
                : previousState;
        } else {
            row.entry.state = waiting ? UutOverviewState::Waiting
                                      : UutOverviewState::Running;
        }
        updateDerivedValues(row);
        dirtyRows.insert(rowIndex);
    };

    for (const auto& event : events) {
        if (event.kind == PicoATE::Core::RuntimeEventKind::ResourceStateChanged) {
            if (event.resourceShared) {
                sharedResourceStateChanged =
                    applyResourceEvent(m_sharedHeldResources,
                                       m_sharedWaitingResources,
                                       event) || sharedResourceStateChanged;
            } else if (!event.uutId.isEmpty()) {
                const int rowIndex = ensureUut(event.uutId);
                auto& entry = m_rows[rowIndex].entry;
                if (!entry.enabled) {
                    continue;
                }
                if (applyResourceEvent(entry.heldResources,
                                       entry.waitingResources,
                                       event)) {
                    dirtyRows.insert(rowIndex);
                }
            }
            continue;
        }
        if (event.kind ==
            PicoATE::Core::RuntimeEventKind::PeriodicTaskStateChanged) {
            if (event.periodicTaskShared) {
                sharedPeriodicChanged =
                    applyPeriodicTaskEvent(m_sharedPeriodicTasks, event) ||
                    sharedPeriodicChanged;
            } else if (!event.uutId.isEmpty()) {
                const int rowIndex = ensureUut(event.uutId);
                if (!m_rows[rowIndex].entry.enabled) {
                    continue;
                }
                if (applyPeriodicTaskEvent(m_rows[rowIndex].entry.periodicTasks,
                                           event)) {
                    dirtyRows.insert(rowIndex);
                }
            }
            continue;
        }
        if (event.details.value(QStringLiteral("periodicTask")).toBool() ||
            event.details.value(QStringLiteral("periodicInvocation")).toBool()) {
            continue;
        }
        if (event.kind == PicoATE::Core::RuntimeEventKind::SessionStateChanged) {
            if (event.executionState == PicoATE::Core::ExecutionState::Paused) {
                for (int row = 0; row < m_rows.size(); ++row) {
                    if (m_rows[row].entry.state == UutOverviewState::Running) {
                        m_rows[row].entry.state = UutOverviewState::Paused;
                        dirtyRows.insert(row);
                    }
                }
            } else if (event.executionState == PicoATE::Core::ExecutionState::Running) {
                for (int row = 0; row < m_rows.size(); ++row) {
                    if (m_rows[row].entry.state == UutOverviewState::Paused) {
                        m_rows[row].entry.state = UutOverviewState::Running;
                        dirtyRows.insert(row);
                    }
                }
            } else if (event.executionState ==
                           PicoATE::Core::ExecutionState::Completed ||
                       event.executionState ==
                           PicoATE::Core::ExecutionState::CompletedWithError ||
                       event.executionState ==
                           PicoATE::Core::ExecutionState::Aborted) {
                if (!m_sharedHeldResources.isEmpty() ||
                    !m_sharedWaitingResources.isEmpty()) {
                    m_sharedHeldResources.clear();
                    m_sharedWaitingResources.clear();
                    sharedResourceStateChanged = true;
                }
                for (int row = 0; row < m_rows.size(); ++row) {
                    auto& entry = m_rows[row].entry;
                    if (!entry.heldResources.isEmpty() ||
                        !entry.waitingResources.isEmpty()) {
                        entry.heldResources.clear();
                        entry.waitingResources.clear();
                        dirtyRows.insert(row);
                    }
                }
            }
            continue;
        }
        if (event.uutId.isEmpty()) {
            const bool sharedSessionPhase =
                overviewTracksNodeEvent(event.kind) && !event.nodeId.isEmpty() &&
                (event.nodePhase == PicoATE::Core::ExecutionPhase::Setup ||
                 event.nodePhase == PicoATE::Core::ExecutionPhase::Cleanup);
            if (sharedSessionPhase) {
                const bool periodicInvocation = event.details.value(
                    QStringLiteral("periodicInvocation")).toBool();
                for (int rowIndex = 0; rowIndex < m_rows.size(); ++rowIndex) {
                    if (!m_rows[rowIndex].entry.enabled) {
                        continue;
                    }
                    if (periodicInvocation &&
                        terminalOverviewState(m_rows[rowIndex].entry.state)) {
                        continue;
                    }
                    applyNodeEvent(m_rows[rowIndex], rowIndex, event, false);
                }
            }
            continue;
        }

        const int rowIndex = ensureUut(event.uutId);
        auto& row = m_rows[rowIndex];
        if (!row.entry.enabled) {
            continue;
        }

        if (event.kind == PicoATE::Core::RuntimeEventKind::UutRegistered) {
            row.entry.state = UutOverviewState::Waiting;
            row.entry.currentStep.clear();
            row.entry.currentNodeId.clear();
            row.entry.currentPhase = PicoATE::Core::ExecutionPhase::Main;
            clearFailure(row);
            row.entry.recentSteps.clear();
            row.entry.retryActive = false;
            row.entry.retryAttempt = 0;
            row.entry.retryMaxAttempts = 0;
            row.entry.heldResources.clear();
            row.entry.waitingResources.clear();
            row.retryNodeId.clear();
            updateDerivedValues(row);
            dirtyRows.insert(rowIndex);
            continue;
        }
        if (event.kind == PicoATE::Core::RuntimeEventKind::UutCompleted) {
            const bool hasError = event.details.value(
                QStringLiteral("hasError")).toBool() || failedOutcome(event.outcome);
            if (event.outcome == PicoATE::Core::NodeOutcome::Cancelled) {
                row.entry.state = UutOverviewState::Stopped;
            } else {
                row.entry.state = hasError ? UutOverviewState::Failed
                                           : UutOverviewState::Passed;
            }
            if (row.entry.state == UutOverviewState::Passed) {
                clearFailure(row);
            } else {
                if (row.entry.failedNodeId.isEmpty()) {
                    row.entry.failedNodeId = row.entry.currentNodeId;
                    row.entry.failedStep = row.entry.currentStep;
                    row.entry.failedPhase = row.entry.currentPhase;
                }
                if (row.entry.errorCode.trimmed().isEmpty() &&
                    !event.errorCode.trimmed().isEmpty()) {
                    row.entry.errorCode = event.errorCode;
                }
                if (row.entry.message.trimmed().isEmpty() &&
                    !event.message.trimmed().isEmpty()) {
                    row.entry.message = event.message;
                }
            }
            row.entry.retryActive = false;
            row.entry.retryAttempt = 0;
            row.entry.retryMaxAttempts = 0;
            row.entry.heldResources.clear();
            row.entry.waitingResources.clear();
            row.retryNodeId.clear();
            updateDerivedValues(row);
            const auto terminalElapsed = qMax<qint64>(0, m_sessionElapsedMs);
            m_terminalElapsedMs.insert(event.uutId, terminalElapsed);
            row.entry.durationMs = terminalElapsed;
            row.entry.progress = 100;
            dirtyRows.insert(rowIndex);
            continue;
        }
        if (!overviewTracksNodeEvent(event.kind) || event.nodeId.isEmpty()) {
            continue;
        }
        if (terminalOverviewState(row.entry.state)) {
            continue;
        }
        applyNodeEvent(row, rowIndex, event, true);
    }

    auto orderedRows = dirtyRows.values();
    std::sort(orderedRows.begin(), orderedRows.end());
    for (const int row : orderedRows) {
        emitRowChanged(row);
    }
    if (sharedPeriodicChanged) {
        emit sharedPeriodicTasksChanged();
    }
    if (sharedResourceStateChanged) {
        emit sharedResourcesChanged();
    }
}

void UutOverviewModel::clear()
{
    const bool hadSharedPeriodicTasks = !m_sharedPeriodicTasks.isEmpty();
    const bool hadSharedResources = !m_sharedHeldResources.isEmpty() ||
                                    !m_sharedWaitingResources.isEmpty();
    m_sharedPeriodicTasks.clear();
    m_sharedHeldResources.clear();
    m_sharedWaitingResources.clear();
    m_terminalElapsedMs.clear();
    m_previewStepCount = 0;
    m_sessionElapsedMs = -1;
    if (m_rows.isEmpty()) {
        if (hadSharedPeriodicTasks) {
            emit sharedPeriodicTasksChanged();
        }
        if (hadSharedResources) {
            emit sharedResourcesChanged();
        }
        return;
    }
    beginResetModel();
    m_rows.clear();
    endResetModel();
}

int UutOverviewModel::rowForUut(const PicoATE::Core::UutId& uutId) const
{
    for (int row = 0; row < m_rows.size(); ++row) {
        if (m_rows[row].entry.uutId == uutId) {
            return row;
        }
    }
    return -1;
}

std::optional<UutOverviewEntry> UutOverviewModel::entryAt(int row) const
{
    if (row < 0 || row >= m_rows.size()) {
        return std::nullopt;
    }
    return m_rows[row].entry;
}

QVector<PeriodicTaskOverviewEntry> UutOverviewModel::sharedPeriodicTasks() const
{
    return m_sharedPeriodicTasks;
}

QVector<ResourceUsageOverviewEntry> UutOverviewModel::sharedHeldResources() const
{
    return m_sharedHeldResources;
}

QVector<ResourceUsageOverviewEntry> UutOverviewModel::sharedWaitingResources() const
{
    return m_sharedWaitingResources;
}

int UutOverviewModel::ensureUut(const PicoATE::Core::UutId& uutId)
{
    const int existing = rowForUut(uutId);
    if (existing >= 0) {
        return existing;
    }
    const int row = m_rows.size();
    beginInsertRows({}, row, row);
    Row created;
    created.entry.uutId = uutId;
    created.entry.totalSteps = m_previewStepCount;
    created.entry.durationMs = qMax<qint64>(0, m_sessionElapsedMs);
    m_rows.push_back(std::move(created));
    endInsertRows();
    return row;
}

void UutOverviewModel::updateDerivedValues(Row& row)
{
    if (!row.entry.enabled) {
        row.entry.state = UutOverviewState::Disabled;
        row.entry.completedSteps = 0;
        row.entry.progress = 0;
        row.entry.durationMs = 0;
        return;
    }
    row.entry.completedSteps = qMax(
        row.entry.completedSteps, static_cast<int>(row.terminalNodes.size()));
    if (row.entry.totalSteps <= 0) {
        row.entry.totalSteps = qMax(m_previewStepCount,
                                    static_cast<int>(row.knownNodes.size()));
    }
    const int currentProgress = row.entry.totalSteps > 0
        ? qBound(0, row.entry.completedSteps * 100 / row.entry.totalSteps, 100)
        : 0;
    row.entry.progress = qMax(row.entry.progress, currentProgress);
}

void UutOverviewModel::emitRowChanged(int row)
{
    if (row < 0 || row >= m_rows.size()) {
        return;
    }
    emit dataChanged(index(row, 0), index(row, ColumnCount - 1));
}

DiagnosticModel::DiagnosticModel(QObject* parent)
    : QAbstractTableModel(parent)
{
    enableUiModelTranslation(this);
}

int DiagnosticModel::rowCount(const QModelIndex& parent) const
{
    return parent.isValid() ? 0 : m_diagnostics.size();
}

int DiagnosticModel::columnCount(const QModelIndex& parent) const
{
    return parent.isValid() ? 0 : ColumnCount;
}

QVariant DiagnosticModel::data(const QModelIndex& index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_diagnostics.size()) {
        return {};
    }
    const auto& diagnostic = m_diagnostics[index.row()];
    if (role == Qt::ForegroundRole && index.column() == SeverityColumn) {
        return diagnosticBrush(diagnostic.severity);
    }
    if (role == Qt::ToolTipRole) {
        return diagnostic.suggestion.isEmpty()
            ? diagnostic.message
            : QStringLiteral("%1\n%2").arg(diagnostic.message, diagnostic.suggestion);
    }
    if (role != Qt::DisplayRole) {
        return {};
    }

    switch (index.column()) {
    case SeverityColumn:
        return diagnosticSeverityName(diagnostic.severity);
    case PathColumn:
        return diagnostic.path.isEmpty() ? QStringLiteral("<root>") : diagnostic.path;
    case MessageColumn:
        return diagnostic.message;
    case SuggestionColumn:
        return diagnostic.suggestion;
    default:
        return {};
    }
}

QVariant DiagnosticModel::headerData(int section,
                                     Qt::Orientation orientation,
                                     int role) const
{
    if (orientation != Qt::Horizontal || role != Qt::DisplayRole) {
        return {};
    }
    const QStringList headers = {
        uiText("Severity"),
        uiText("Path"),
        uiText("Message"),
        uiText("Suggestion")};
    return section >= 0 && section < headers.size() ? headers[section] : QVariant();
}

void DiagnosticModel::setDiagnostics(QVector<UiDiagnostic> diagnostics)
{
    if (m_diagnostics == diagnostics) {
        return;
    }
    beginResetModel();
    m_diagnostics = std::move(diagnostics);
    endResetModel();
}

std::optional<UiDiagnostic> DiagnosticModel::diagnosticAt(int row) const
{
    if (row < 0 || row >= m_diagnostics.size()) {
        return std::nullopt;
    }
    return m_diagnostics[row];
}

UutStepModel::UutStepModel(QObject* parent)
    : QAbstractItemModel(parent)
{
    enableUiModelTranslation(this);
}

QModelIndex UutStepModel::index(int row,
                               int column,
                               const QModelIndex& parentIndex) const
{
    if (row < 0 || column < 0 || column >= ColumnCount) {
        return {};
    }
    if (!parentIndex.isValid()) {
        return row < m_rootItems.size()
            ? createIndex(row, column, m_rootItems[row])
            : QModelIndex();
    }
    if (parentIndex.column() != 0) {
        return {};
    }
    auto* parentItem = static_cast<ModelItem*>(parentIndex.internalPointer());
    if (!parentItem || row >= parentItem->children.size()) {
        return {};
    }
    return createIndex(row, column, parentItem->children[row]);
}

QModelIndex UutStepModel::parent(const QModelIndex& child) const
{
    if (!child.isValid()) {
        return {};
    }
    auto* item = static_cast<ModelItem*>(child.internalPointer());
    if (!item || !item->parent) {
        return {};
    }
    return createIndex(item->parent->row, 0, item->parent);
}

int UutStepModel::rowCount(const QModelIndex& parentIndex) const
{
    if (!parentIndex.isValid()) {
        return m_rootItems.size();
    }
    if (parentIndex.column() != 0) {
        return 0;
    }
    const auto* item = static_cast<ModelItem*>(parentIndex.internalPointer());
    return item ? item->children.size() : 0;
}

int UutStepModel::columnCount(const QModelIndex&) const
{
    return ColumnCount;
}

QVariant UutStepModel::data(const QModelIndex& index, int role) const
{
    if (!index.isValid()) {
        return {};
    }
    const int uutIndex = uutIndexFor(index);
    if (uutIndex < 0 || uutIndex >= m_report.uuts.size()) {
        return {};
    }
    const auto& uut = m_report.uuts[uutIndex];
    const auto* modelItem = static_cast<ModelItem*>(index.internalPointer());

    if (modelItem && modelItem->isPhase) {
        if (role == Qt::BackgroundRole) {
            return QBrush(QColor(QStringLiteral("#e8edf1")));
        }
        if (role == Qt::FontRole) {
            QFont font;
            font.setBold(true);
            return font;
        }
        if (role == Qt::DisplayRole && index.column() == NameColumn) {
            return uiStateText(PicoATE::Core::executionPhaseName(modelItem->phase).toUpper());
        }
        return {};
    }

    if (!isStepIndex(index)) {
        if (role == Qt::ForegroundRole && index.column() == OutcomeColumn) {
            const auto completed = m_completedUuts.contains(uut.uutId);
            return outcomeBrush(!completed
                                    ? PicoATE::Core::NodeOutcome::Unknown
                                    : (uut.hasError
                                           ? PicoATE::Core::NodeOutcome::Failed
                                           : PicoATE::Core::NodeOutcome::Passed));
        }
        if (role != Qt::DisplayRole) {
            return {};
        }
        switch (index.column()) {
        case NameColumn:
            return uut.uutId;
        case StateColumn:
            return m_completedUuts.contains(uut.uutId)
                ? uiText("Completed")
                : (m_report.state == PicoATE::Core::ExecutionState::Running
                       ? uiText("Running")
                       : uiText("Pending"));
        case OutcomeColumn:
            if (!m_completedUuts.contains(uut.uutId)) {
                return uiText("Unknown");
            }
            return uut.hasError ? uiText("Failed")
                                : uiText("Passed");
        case AttemptsColumn: {
            int attempts = 0;
            for (const auto& step : uut.steps) {
                attempts += totalAttemptCount(step);
            }
            return attempts;
        }
        default:
            return {};
        }
    }

    const auto* stepPointer = stepForIndex(index);
    if (!stepPointer) {
        return {};
    }
    const auto& step = *stepPointer;
    if (role == ParserSelectionDisplayRole && index.column() == ActualColumn) {
        return parserSelectionDisplay(step.measurements);
    }
    if (role == Qt::ToolTipRole && index.column() == ActualColumn) {
        const auto display = parserSelectionDisplay(step.measurements);
        const auto description = display
                                     .value(QStringLiteral("selectionDescription"))
                                     .toString();
        if (!description.isEmpty()) {
            return description;
        }
    }
    if (role == Qt::DecorationRole && index.column() == NameColumn) {
        return functionIconForStep(step);
    }
    if (role == Qt::BackgroundRole) {
        using PicoATE::Core::ActivationState;
        switch (step.state) {
        case ActivationState::Running:
        case ActivationState::WaitingForTimer:
            return QBrush(QColor(QStringLiteral("#fff0a6")));
        case ActivationState::Passed:
            return QBrush(QColor(QStringLiteral("#d9f2c7")));
        case ActivationState::Failed:
        case ActivationState::Error:
        case ActivationState::Timeout:
            return QBrush(QColor(QStringLiteral("#ffd6d2")));
        case ActivationState::Cancelled:
        case ActivationState::Skipped:
            return QBrush(QColor(QStringLiteral("#e6e8eb")));
        default:
            return {};
        }
    }
    if (role == Qt::ForegroundRole && index.column() == OutcomeColumn) {
        return outcomeBrush(step.outcome);
    }
    if (role == Qt::ToolTipRole &&
        step.outcome != PicoATE::Core::NodeOutcome::Unknown &&
        !step.attempts.isEmpty()) {
        const auto& last = step.attempts.last();
        if (!last.errorMessage.isEmpty()) {
            return last.errorCode.isEmpty()
                ? last.errorMessage
                : QStringLiteral("%1: %2").arg(last.errorCode, last.errorMessage);
        }
    }
    if (role != Qt::DisplayRole) {
        return {};
    }

    switch (index.column()) {
    case NameColumn:
        return step.displayName.isEmpty() ? step.stepId : step.displayName;
    case ErrorCodeColumn:
        return stepErrorCode(step);
    case LowerLimitColumn:
        return joinedMeasurementText(step.measurements, lowerLimitText);
    case UpperLimitColumn:
        return joinedMeasurementText(step.measurements, upperLimitText);
    case ActualColumn:
        return joinedActualText(step.measurements);
    case OutcomeColumn:
        return step.outcome == PicoATE::Core::NodeOutcome::Unknown
            ? uiStateText(activationStateName(step.state))
            : uiStateText(outcomeName(step.outcome));
    case TimeColumn:
        return durationText(step.durationMs);
    case StateColumn:
        return uiStateText(activationStateName(step.state));
    case AttemptsColumn:
        return step.attempts.size();
    case LoopColumn:
        return loopDescription(step.loop);
    default:
        return {};
    }
}

QVariant UutStepModel::headerData(int section,
                                  Qt::Orientation orientation,
                                  int role) const
{
    if (orientation != Qt::Horizontal || role != Qt::DisplayRole) {
        return {};
    }
    const QStringList headers = {
        m_singleUutPhaseLayout ? uiText("Phase / Step")
                               : uiText("UUT / Step"),
        uiText("Error Code"),
        uiText("Lower"),
        uiText("Upper"),
        uiText("Actual"),
        uiText("Result"),
        uiText("Time"),
        uiText("State"),
        uiText("Attempts"),
        uiText("Loop"),
        QString()};
    return section >= 0 && section < headers.size() ? headers[section] : QVariant();
}

void UutStepModel::setReport(PicoATE::Core::ExecutionReport report)
{
    beginResetModel();
    m_report = std::move(report);
    m_report.uuts.detach();
    for (auto& uut : m_report.uuts) {
        detachStepTree(uut.steps);
    }
    detachStepTree(m_report.sessionSteps);
    m_completedUuts.clear();
    if (m_report.completed) {
        for (const auto& uut : m_report.uuts) {
            m_completedUuts.insert(uut.uutId);
        }
    }
    rebuildIndexTree();
    endResetModel();
}

void UutStepModel::setSingleUutPhaseLayout(bool enabled)
{
    if (m_singleUutPhaseLayout == enabled) {
        return;
    }
    beginResetModel();
    m_singleUutPhaseLayout = enabled;
    rebuildIndexTree();
    endResetModel();
}

void UutStepModel::setVisibleUutId(const PicoATE::Core::UutId& uutId)
{
    if (m_visibleUutId == uutId) {
        return;
    }
    beginResetModel();
    m_visibleUutId = uutId;
    rebuildIndexTree();
    endResetModel();
}

PicoATE::Core::UutId UutStepModel::visibleUutId() const
{
    return m_visibleUutId;
}

void UutStepModel::applyRuntimeEvents(
    const QVector<PicoATE::Core::RuntimeEvent>& events)
{
    const bool hasExecutionEvent = std::any_of(
        events.cbegin(),
        events.cend(),
        [](const auto& event) {
            return event.kind != PicoATE::Core::RuntimeEventKind::DeviceStateChanged;
        });
    if (!hasExecutionEvent) {
        return;
    }

    const bool rebuildTree = runtimeEventsRequireTreeRebuild(events);
    QSet<ModelItem*> dirtyItems;
    if (rebuildTree) {
        beginResetModel();
    }
    for (const auto& event : events) {
        if (m_report.planId.isEmpty() && !event.planId.isEmpty()) {
            m_report.planId = event.planId;
        }
        if (event.kind == PicoATE::Core::RuntimeEventKind::SessionStateChanged) {
            m_report.state = event.executionState;
            m_report.completed = event.executionState == PicoATE::Core::ExecutionState::Completed ||
                                 event.executionState == PicoATE::Core::ExecutionState::CompletedWithError ||
                                 event.executionState == PicoATE::Core::ExecutionState::Aborted;
            if (!rebuildTree) {
                for (auto* root : m_rootItems) {
                    if (root && root->isUut) {
                        dirtyItems.insert(root);
                    }
                }
            }
            continue;
        }
        auto* uut = event.uutId.isEmpty() ? nullptr : &ensureUut(event.uutId);
        if (event.kind == PicoATE::Core::RuntimeEventKind::UutRegistered) {
            continue;
        }
        if (event.kind == PicoATE::Core::RuntimeEventKind::UutCompleted) {
            if (!uut) {
                continue;
            }
            m_completedUuts.insert(event.uutId);
            uut->hasError = event.details.value("hasError").toBool();
            m_report.hasError = m_report.hasError || uut->hasError;
            if (!rebuildTree) {
                for (auto* root : m_rootItems) {
                    if (root && root->isUut && root->uutIndex >= 0 &&
                        root->uutIndex < m_report.uuts.size() &&
                        m_report.uuts[root->uutIndex].uutId == event.uutId) {
                        dirtyItems.insert(root);
                        break;
                    }
                }
            }
            continue;
        }
        if (event.nodeId.isEmpty()) {
            continue;
        }

        if (!runtimeEventUpdatesStep(event)) {
            continue;
        }

        auto& step = ensureStep(uut ? uut->steps : m_report.sessionSteps, event);
        auto* changedItem = rebuildTree
            ? nullptr
            : findModelItem(event.uutId, event.nodeId);
        switch (event.kind) {
        case PicoATE::Core::RuntimeEventKind::NodeStateChanged:
        case PicoATE::Core::RuntimeEventKind::BarrierWaiting:
        case PicoATE::Core::RuntimeEventKind::BarrierReleased:
        case PicoATE::Core::RuntimeEventKind::CleanupActivated: {
            step.state = event.activationState;
            const bool resetResult =
                event.outcome == PicoATE::Core::NodeOutcome::Unknown &&
                !PicoATE::Core::isTerminalActivation(event.activationState);
            if (resetResult) {
                resetCurrentStepResult(step, false);
            } else if (event.outcome != PicoATE::Core::NodeOutcome::Unknown) {
                step.outcome = event.outcome;
            }
            if (!isCompositeRuntimeStep(step) && step.durationMs < 0 &&
                event.details.contains("durationMs")) {
                step.durationMs = event.details.value("durationMs").toLongLong();
            }
            break;
        }
        case PicoATE::Core::RuntimeEventKind::LoopIterationStarted:
            resetCurrentStepResult(step, true);
            step.state = PicoATE::Core::ActivationState::Running;
            collectItemAndDescendants(changedItem, dirtyItems);
            break;
        case PicoATE::Core::RuntimeEventKind::LoopCompleted:
            step.state = event.activationState;
            step.outcome = event.outcome;
            if (event.details.contains("durationMs")) {
                step.durationMs = event.details.value("durationMs").toLongLong();
            }
            break;
        case PicoATE::Core::RuntimeEventKind::TestItemStarted:
            resetCurrentStepResult(step, false);
            step.state = PicoATE::Core::ActivationState::Running;
            break;
        case PicoATE::Core::RuntimeEventKind::TestItemCompleted:
            step.state = event.activationState;
            step.outcome = event.outcome;
            if (event.details.contains("durationMs")) {
                step.durationMs = event.details.value("durationMs").toLongLong();
            }
            break;
        case PicoATE::Core::RuntimeEventKind::AttemptStarted:
        case PicoATE::Core::RuntimeEventKind::AttemptCompleted: {
            if (event.kind == PicoATE::Core::RuntimeEventKind::AttemptStarted) {
                resetCurrentStepResult(step, false);
                step.state = PicoATE::Core::ActivationState::Running;
            }
            auto attempt = std::find_if(
                step.attempts.begin(),
                step.attempts.end(),
                [&event](const auto& item) { return item.index == event.attemptIndex; });
            if (attempt == step.attempts.end()) {
                PicoATE::Core::AttemptReport created;
                created.index = event.attemptIndex;
                step.attempts.push_back(created);
                attempt = std::prev(step.attempts.end());
            }
            attempt->outcome = event.outcome;
            attempt->errorCode = event.errorCode;
            attempt->errorMessage = event.message;
            attempt->loopIteration = event.loopIteration;
            attempt->measurements = event.measurements;
            if (event.details.contains("durationMs")) {
                attempt->durationMs = event.details.value("durationMs").toLongLong();
            }
            if (event.kind == PicoATE::Core::RuntimeEventKind::AttemptCompleted) {
                step.outcome = event.outcome;
                step.measurements = event.measurements;
                if (!isCompositeRuntimeStep(step) &&
                    event.details.contains("durationMs")) {
                    step.durationMs = event.details.value("durationMs").toLongLong();
                }
            }
            if (event.loopIteration.active) {
                step.loop.inLoop = true;
                step.loop.loopId = event.loopIteration.loopId;
                step.loop.controllerStepId = event.loopIteration.controllerNodeId;
                step.loop.variableName = event.loopIteration.variableName;
            }
            break;
        }
        default:
            break;
        }

        if (changedItem) {
            dirtyItems.insert(changedItem);
        }

        step.wasError = step.outcome == PicoATE::Core::NodeOutcome::Failed ||
                        step.outcome == PicoATE::Core::NodeOutcome::Error ||
                        step.outcome == PicoATE::Core::NodeOutcome::Timeout;
        if (uut) {
            uut->hasError = uut->hasError || step.wasError;
            m_report.hasError = m_report.hasError || uut->hasError;
        } else {
            m_report.sessionHasError = m_report.sessionHasError || step.wasError;
            m_report.hasError = m_report.hasError || m_report.sessionHasError;
        }
    }
    if (rebuildTree) {
        rebuildIndexTree();
        endResetModel();
    } else {
        emitItemsDataChanged(dirtyItems);
    }
}

void UutStepModel::clear()
{
    setReport({});
}

UutStepModel::ItemType UutStepModel::itemType(const QModelIndex& index) const
{
    if (!index.isValid()) {
        return UutItem;
    }
    const auto* item = static_cast<ModelItem*>(index.internalPointer());
    if (item && item->isPhase) {
        return PhaseItem;
    }
    return isStepIndex(index) ? StepItem : UutItem;
}

std::optional<PicoATE::Core::StepReport> UutStepModel::stepAt(const QModelIndex& index) const
{
    const auto* step = stepForIndex(index);
    if (!step) {
        return std::nullopt;
    }
    PicoATE::Core::StepReport snapshot = *step;
    detachStepTree(snapshot.children);
    return snapshot;
}

std::optional<PicoATE::Core::UutReport> UutStepModel::uutAt(const QModelIndex& index) const
{
    if (!index.isValid()) {
        return std::nullopt;
    }
    const int uutIndex = uutIndexFor(index);
    if (uutIndex < 0 || uutIndex >= m_report.uuts.size()) {
        return std::nullopt;
    }
    PicoATE::Core::UutReport snapshot = m_report.uuts[uutIndex];
    detachStepTree(snapshot.steps);
    return snapshot;
}

QModelIndex UutStepModel::indexForStep(const PicoATE::Core::UutId& uutId,
                                       const PicoATE::Core::NodeId& stepId) const
{
    const auto* item = findModelItem(uutId, stepId);
    return item ? createIndex(item->row, 0, const_cast<ModelItem*>(item)) : QModelIndex();
}

PicoATE::Core::UutReport& UutStepModel::ensureUut(const PicoATE::Core::UutId& uutId)
{
    auto it = std::find_if(m_report.uuts.begin(), m_report.uuts.end(), [&uutId](const auto& item) {
        return item.uutId == uutId;
    });
    if (it != m_report.uuts.end()) {
        return *it;
    }
    PicoATE::Core::UutReport uut;
    uut.uutId = uutId;
    m_report.uuts.push_back(uut);
    return m_report.uuts.last();
}

PicoATE::Core::StepReport& UutStepModel::ensureStep(
    QVector<PicoATE::Core::StepReport>& steps,
    const PicoATE::Core::RuntimeEvent& event)
{
    const auto findStepByPath = [](QVector<PicoATE::Core::StepReport>& steps,
                                   const PicoATE::Core::NodeId& id,
                                   const auto& self) -> PicoATE::Core::StepReport* {
        for (auto& step : steps) {
            if (step.nodePath == id) {
                return &step;
            }
            if (auto* child = self(step.children, id, self)) {
                return child;
            }
        }
        return nullptr;
    };
    const auto findLegacyStep = [](QVector<PicoATE::Core::StepReport>& steps,
                                   const PicoATE::Core::NodeId& id,
                                   const auto& self) -> PicoATE::Core::StepReport* {
        for (auto& step : steps) {
            if (step.nodePath.isEmpty() && step.stepId == id) {
                return &step;
            }
            if (auto* child = self(step.children, id, self)) {
                return child;
            }
        }
        return nullptr;
    };
    const auto findStep = [&](QVector<PicoATE::Core::StepReport>& steps,
                              const PicoATE::Core::NodeId& id) {
        if (auto* exact = findStepByPath(steps, id, findStepByPath)) {
            return exact;
        }
        return findLegacyStep(steps, id, findLegacyStep);
    };
    auto* existing = findStep(steps, event.nodeId);
    if (!event.parentNodeId.isEmpty()) {
        auto* parent = findStep(steps, event.parentNodeId);
        if (!parent) {
            PicoATE::Core::StepReport createdParent;
            createdParent.stepId = event.parentNodeId.section('.', -1);
            createdParent.nodePath = event.parentNodeId;
            createdParent.displayName = event.parentNodeId;
            createdParent.kind = PicoATE::Core::ExecNodeKind::TestItem;
            createdParent.phase = event.nodePhase;
            steps.push_back(createdParent);
            parent = &steps.last();
        }
        parent->phase = event.nodePhase;
        auto* child = findStep(parent->children, event.nodeId);
        if (!child) {
            PicoATE::Core::StepReport created;
            created.stepId = event.nodeLocalId.isEmpty() ? event.nodeId : event.nodeLocalId;
            created.nodePath = event.nodeId;
            created.displayName = event.nodeDisplayName;
            created.kind = event.nodeKind;
            created.phase = event.nodePhase;
            parent->children.push_back(created);
            child = &parent->children.last();
        }
        child->phase = event.nodePhase;
        return *child;
    }
    if (existing) {
        existing->stepId = event.nodeLocalId.isEmpty() ? existing->stepId : event.nodeLocalId;
        existing->nodePath = event.nodeId;
        if (!event.nodeDisplayName.isEmpty()) {
            existing->displayName = event.nodeDisplayName;
        }
        existing->kind = event.nodeKind;
        existing->phase = event.nodePhase;
        return *existing;
    }
    PicoATE::Core::StepReport step;
    step.stepId = event.nodeLocalId.isEmpty() ? event.nodeId : event.nodeLocalId;
    step.nodePath = event.nodeId;
    step.displayName = event.nodeDisplayName;
    step.kind = event.nodeKind;
    step.phase = event.nodePhase;
    steps.push_back(step);
    return steps.last();
}

bool UutStepModel::isStepIndex(const QModelIndex& index) const
{
    if (!index.isValid()) {
        return false;
    }
    const auto* item = static_cast<ModelItem*>(index.internalPointer());
    return item && !item->isUut && !item->isPhase;
}

int UutStepModel::uutIndexFor(const QModelIndex& index) const
{
    if (!index.isValid()) {
        return -1;
    }
    const auto* item = static_cast<ModelItem*>(index.internalPointer());
    return item ? item->uutIndex : -1;
}

const PicoATE::Core::StepReport* UutStepModel::stepForIndex(const QModelIndex& index) const
{
    if (!index.isValid() || !isStepIndex(index)) {
        return nullptr;
    }
    const auto* item = static_cast<ModelItem*>(index.internalPointer());
    return item ? item->step : nullptr;
}

int UutStepModel::visualLineNumber(const QModelIndex& index) const
{
    if (!index.isValid()) {
        return 0;
    }
    const auto* item = static_cast<ModelItem*>(index.internalPointer());
    return item && item->step ? item->visualLineNumber : 0;
}

void UutStepModel::rebuildIndexTree()
{
    m_modelItems.clear();
    m_rootItems.clear();
    m_nextVisualLineNumber = 1;
    int phaseLayoutUutIndex = -1;
    if (!m_visibleUutId.isEmpty()) {
        for (int index = 0; index < m_report.uuts.size(); ++index) {
            if (m_report.uuts[index].uutId == m_visibleUutId) {
                phaseLayoutUutIndex = index;
                break;
            }
        }
    } else if (m_singleUutPhaseLayout && m_report.uuts.size() == 1) {
        phaseLayoutUutIndex = 0;
    }
    if (phaseLayoutUutIndex >= 0) {
        auto& uut = m_report.uuts[phaseLayoutUutIndex];
        const QVector<PicoATE::Core::ExecutionPhase> phases = {
            PicoATE::Core::ExecutionPhase::Setup,
            PicoATE::Core::ExecutionPhase::Main,
            PicoATE::Core::ExecutionPhase::Cleanup};
        for (const auto phase : phases) {
            auto& phaseSteps = phase == PicoATE::Core::ExecutionPhase::Main
                ? uut.steps
                : m_report.sessionSteps;
            const bool hasSteps = std::any_of(
                phaseSteps.cbegin(), phaseSteps.cend(),
                [phase](const auto& step) { return step.phase == phase; });
            if (!hasSteps) {
                continue;
            }
            auto root = std::make_unique<ModelItem>();
            root->isPhase = true;
            root->phase = phase;
            root->uutIndex = phaseLayoutUutIndex;
            root->row = m_rootItems.size();
            auto* rootPointer = root.get();
            m_modelItems.push_back(std::move(root));
            m_rootItems.push_back(rootPointer);
            for (auto& step : phaseSteps) {
                if (step.phase == phase) {
                    appendModelItem(rootPointer, phaseLayoutUutIndex, &step);
                }
            }
        }
        return;
    }
    for (int uutIndex = 0; uutIndex < m_report.uuts.size(); ++uutIndex) {
        auto root = std::make_unique<ModelItem>();
        root->isUut = true;
        root->uutIndex = uutIndex;
        root->row = uutIndex;
        auto* rootPointer = root.get();
        m_modelItems.push_back(std::move(root));
        m_rootItems.push_back(rootPointer);
        for (auto& step : m_report.uuts[uutIndex].steps) {
            appendModelItem(rootPointer, uutIndex, &step);
        }
    }
}

UutStepModel::ModelItem* UutStepModel::appendModelItem(
    ModelItem* parent,
    int uutIndex,
    PicoATE::Core::StepReport* step)
{
    auto item = std::make_unique<ModelItem>();
    item->uutIndex = uutIndex;
    item->row = parent->children.size();
    item->step = step;
    item->visualLineNumber = m_nextVisualLineNumber++;
    item->parent = parent;
    auto* pointer = item.get();
    m_modelItems.push_back(std::move(item));
    parent->children.push_back(pointer);
    for (auto& child : step->children) {
        appendModelItem(pointer, uutIndex, &child);
    }
    return pointer;
}

UutStepModel::ModelItem* UutStepModel::findModelItem(
    const PicoATE::Core::UutId& uutId,
    const PicoATE::Core::NodeId& stepId) const
{
    ModelItem* legacyMatch = nullptr;
    for (const auto& item : m_modelItems) {
        if (!item->step || item->uutIndex < 0 || item->uutIndex >= m_report.uuts.size()) {
            continue;
        }
        if (!uutId.isEmpty() && m_report.uuts[item->uutIndex].uutId != uutId) {
            continue;
        }
        if (uutId.isEmpty() && item->step->phase == PicoATE::Core::ExecutionPhase::Main) {
            continue;
        }
        if (item->step->nodePath == stepId) {
            return item.get();
        }
        if (!legacyMatch && item->step->nodePath.isEmpty() &&
            item->step->stepId == stepId) {
            legacyMatch = item.get();
        }
    }
    return legacyMatch;
}

bool UutStepModel::runtimeEventsRequireTreeRebuild(
    const QVector<PicoATE::Core::RuntimeEvent>& events) const
{
    for (const auto& event : events) {
        const bool hiddenUutEvent = !m_visibleUutId.isEmpty() &&
                                    !event.uutId.isEmpty() &&
                                    event.uutId != m_visibleUutId;
        if (event.kind == PicoATE::Core::RuntimeEventKind::UutRegistered &&
            !event.uutId.isEmpty()) {
            const bool uutExists = std::any_of(
                m_report.uuts.cbegin(),
                m_report.uuts.cend(),
                [&event](const auto& uut) { return uut.uutId == event.uutId; });
            if (!uutExists) {
                return true;
            }
        }
        if (hiddenUutEvent) {
            continue;
        }
        if (!runtimeEventUpdatesStep(event) || event.nodeId.isEmpty()) {
            continue;
        }
        const auto* item = findModelItem(event.uutId, event.nodeId);
        if (!item) {
            return true;
        }
        if (!event.parentNodeId.isEmpty()) {
            const auto* parentItem = findModelItem(event.uutId, event.parentNodeId);
            if (!parentItem || item->parent != parentItem) {
                return true;
            }
        }
    }
    return false;
}

void UutStepModel::collectItemAndDescendants(
    ModelItem* item,
    QSet<ModelItem*>& items) const
{
    if (!item || items.contains(item)) {
        return;
    }
    items.insert(item);
    for (auto* child : item->children) {
        collectItemAndDescendants(child, items);
    }
}

void UutStepModel::emitItemsDataChanged(const QSet<ModelItem*>& items)
{
    const QList<int> roles = {
        Qt::DisplayRole,
        Qt::BackgroundRole,
        Qt::ForegroundRole,
        Qt::ToolTipRole,
    };
    for (auto* item : items) {
        if (!item || item->row < 0) {
            continue;
        }
        emit dataChanged(createIndex(item->row, 0, item),
                         createIndex(item->row, ColumnCount - 1, item),
                         roles);
    }
}

DeviceStatusModel::DeviceStatusModel(QObject* parent)
    : QAbstractTableModel(parent)
{
    enableUiModelTranslation(this);
}

int DeviceStatusModel::rowCount(const QModelIndex& parent) const
{
    return parent.isValid() ? 0 : m_devices.size();
}

int DeviceStatusModel::columnCount(const QModelIndex& parent) const
{
    return parent.isValid() ? 0 : ColumnCount;
}

QVariant DeviceStatusModel::data(const QModelIndex& index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_devices.size()) {
        return {};
    }
    const auto& device = m_devices[index.row()];
    if (role == Qt::ForegroundRole && index.column() == StateColumn) {
        using PicoATE::Core::DeviceConnectionState;
        if (device.state == DeviceConnectionState::Connected) {
            return QBrush(QColor(QStringLiteral("#27844b")));
        }
        if (device.state == DeviceConnectionState::Error) {
            return QBrush(QColor(QStringLiteral("#b43a3a")));
        }
        return QBrush(QColor(QStringLiteral("#62707d")));
    }
    if (role == Qt::ToolTipRole && !device.errorCode.isEmpty()) {
        return QStringLiteral("%1: %2").arg(device.errorCode, device.message);
    }
    if (role != Qt::DisplayRole) {
        return {};
    }
    switch (index.column()) {
    case DeviceColumn:
        return device.id;
    case TypeColumn:
        return device.type;
    case DriverColumn:
        return device.driver;
    case StateColumn:
        return uiStateText(PicoATE::Core::deviceConnectionStateName(device.state));
    case MessageColumn:
        return device.message;
    default:
        return {};
    }
}

QVariant DeviceStatusModel::headerData(int section,
                                       Qt::Orientation orientation,
                                       int role) const
{
    if (orientation != Qt::Horizontal || role != Qt::DisplayRole) {
        return {};
    }
    const QStringList headers = {
        uiText("Device"),
        uiText("Type"),
        uiText("Driver"),
        uiText("State"),
        uiText("Message")};
    return section >= 0 && section < headers.size() ? headers[section] : QVariant();
}

void DeviceStatusModel::applyRuntimeEvents(
    const QVector<PicoATE::Core::RuntimeEvent>& events)
{
    const bool hasDeviceEvent = std::any_of(
        events.cbegin(),
        events.cend(),
        [](const auto& event) {
            return event.kind == PicoATE::Core::RuntimeEventKind::DeviceStateChanged &&
                   !event.deviceId.isEmpty();
        });
    if (!hasDeviceEvent) {
        return;
    }

    beginResetModel();
    for (const auto& event : events) {
        if (event.kind != PicoATE::Core::RuntimeEventKind::DeviceStateChanged ||
            event.deviceId.isEmpty()) {
            continue;
        }
        auto it = std::find_if(m_devices.begin(), m_devices.end(), [&event](const auto& item) {
            return item.id == event.deviceId;
        });
        if (it == m_devices.end()) {
            DeviceRow row;
            row.id = event.deviceId;
            m_devices.push_back(row);
            it = std::prev(m_devices.end());
        }
        it->type = event.details.value("deviceType").toString();
        it->driver = event.details.value("driverId").toString();
        it->state = event.deviceState;
        it->message = event.message;
        it->errorCode = event.errorCode;
    }
    endResetModel();
}

void DeviceStatusModel::clear()
{
    beginResetModel();
    m_devices.clear();
    endResetModel();
}

HistoryModel::HistoryModel(QObject* parent)
    : QAbstractTableModel(parent)
{
    enableUiModelTranslation(this);
}

int HistoryModel::rowCount(const QModelIndex& parent) const
{
    return parent.isValid() ? 0 : m_entries.size();
}

int HistoryModel::columnCount(const QModelIndex& parent) const
{
    return parent.isValid() ? 0 : ColumnCount;
}

QVariant HistoryModel::data(const QModelIndex& index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_entries.size()) {
        return {};
    }
    const auto& entry = m_entries[index.row()];
    if (role == Qt::ForegroundRole && index.column() == ResultColumn) {
        return QBrush(QColor(entry.hasError
                                 ? QStringLiteral("#b43a3a")
                                 : QStringLiteral("#27844b")));
    }
    if (role == Qt::ToolTipRole) {
        return QStringLiteral("%1\n%2")
            .arg(entry.planId, entry.uutIds.join(QStringLiteral(", ")));
    }
    if (role != Qt::DisplayRole) {
        return {};
    }
    switch (index.column()) {
    case SavedAtColumn:
        return entry.savedAtUtc.toLocalTime().toString(QStringLiteral("yyyy-MM-dd HH:mm:ss"));
    case SequenceColumn:
        return entry.sequenceId;
    case VersionColumn:
        return entry.sequenceVersion;
    case StateColumn:
        return uiStateText(executionStateName(entry.state));
    case ResultColumn:
        return entry.hasError ? uiText("Failed") : uiText("Passed");
    case UutsColumn:
        return entry.uutIds.join(QStringLiteral(", "));
    default:
        return {};
    }
}

QVariant HistoryModel::headerData(int section,
                                  Qt::Orientation orientation,
                                  int role) const
{
    if (orientation != Qt::Horizontal || role != Qt::DisplayRole) {
        return {};
    }
    const QStringList headers = {
        uiText("Saved"),
        uiText("Sequence"),
        uiText("Version"),
        uiText("State"),
        uiText("Result"),
        uiText("UUTs")};
    return section >= 0 && section < headers.size() ? headers[section] : QVariant();
}

void HistoryModel::setEntries(QVector<ReportHistoryEntry> entries)
{
    beginResetModel();
    m_entries = std::move(entries);
    endResetModel();
}

std::optional<ReportHistoryEntry> HistoryModel::entryAt(int row) const
{
    if (row < 0 || row >= m_entries.size()) {
        return std::nullopt;
    }
    return m_entries[row];
}

AttemptModel::AttemptModel(QObject* parent)
    : QAbstractTableModel(parent)
{
    enableUiModelTranslation(this);
}

int AttemptModel::rowCount(const QModelIndex& parent) const
{
    return parent.isValid() || !m_step ? 0 : m_step->attempts.size();
}

int AttemptModel::columnCount(const QModelIndex& parent) const
{
    return parent.isValid() ? 0 : ColumnCount;
}

QVariant AttemptModel::data(const QModelIndex& index, int role) const
{
    if (!m_step || !index.isValid() || index.row() < 0 || index.row() >= m_step->attempts.size()) {
        return {};
    }
    const auto& attempt = m_step->attempts[index.row()];
    if (role == Qt::ForegroundRole && index.column() == OutcomeColumn) {
        return outcomeBrush(attempt.outcome);
    }
    if (role == Qt::ToolTipRole && !attempt.errorMessage.isEmpty()) {
        return attempt.errorCode.isEmpty()
            ? attempt.errorMessage
            : QStringLiteral("%1: %2").arg(attempt.errorCode, attempt.errorMessage);
    }
    if (role != Qt::DisplayRole) {
        return {};
    }

    switch (index.column()) {
    case IndexColumn:
        return attempt.index;
    case OutcomeColumn:
        return uiStateText(outcomeName(attempt.outcome));
    case LoopColumn:
        return loopIterationDescription(attempt.loopIteration);
    case MeasurementCountColumn:
        return attempt.measurements.size();
    case ErrorColumn:
        return attempt.errorMessage;
    default:
        return {};
    }
}

QVariant AttemptModel::headerData(int section,
                                  Qt::Orientation orientation,
                                  int role) const
{
    if (orientation != Qt::Horizontal || role != Qt::DisplayRole) {
        return {};
    }
    const QStringList headers = {
        uiText("Attempt"),
        uiText("Outcome"),
        uiText("Loop iteration"),
        uiText("Measurements"),
        uiText("Error")};
    return section >= 0 && section < headers.size() ? headers[section] : QVariant();
}

void AttemptModel::setStep(std::optional<PicoATE::Core::StepReport> step)
{
    beginResetModel();
    m_step = std::move(step);
    endResetModel();
}

std::optional<PicoATE::Core::AttemptReport> AttemptModel::attemptAt(int row) const
{
    if (!m_step || row < 0 || row >= m_step->attempts.size()) {
        return std::nullopt;
    }
    return m_step->attempts[row];
}

MeasurementModel::MeasurementModel(QObject* parent)
    : QAbstractTableModel(parent)
{
    enableUiModelTranslation(this);
}

int MeasurementModel::rowCount(const QModelIndex& parent) const
{
    return parent.isValid() ? 0 : m_measurements.size();
}

int MeasurementModel::columnCount(const QModelIndex& parent) const
{
    return parent.isValid() ? 0 : ColumnCount;
}

QVariant MeasurementModel::data(const QModelIndex& index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_measurements.size()) {
        return {};
    }
    const auto& measurement = m_measurements[index.row()];
    if (role == Qt::ForegroundRole && index.column() == StatusColumn) {
        using PicoATE::Core::MeasurementStatus;
        switch (measurement.status) {
        case MeasurementStatus::Passed:
            return QBrush(QColor(QStringLiteral("#27844b")));
        case MeasurementStatus::Failed:
        case MeasurementStatus::Error:
            return QBrush(QColor(QStringLiteral("#b43a3a")));
        case MeasurementStatus::Skipped:
            return QBrush(QColor(QStringLiteral("#a56600")));
        default:
            return QBrush(QColor(QStringLiteral("#62707d")));
        }
    }
    if (role == Qt::ToolTipRole && !measurement.errorMessage.isEmpty()) {
        return measurement.errorCode.isEmpty()
            ? measurement.errorMessage
            : QStringLiteral("%1: %2").arg(measurement.errorCode, measurement.errorMessage);
    }
    if (role != Qt::DisplayRole) {
        return {};
    }

    switch (index.column()) {
    case NameColumn:
        return measurement.name;
    case ValueColumn:
        return measurement.value;
    case UnitColumn:
        return measurement.unit;
    case LimitsColumn:
        return measurementLimits(measurement);
    case StatusColumn:
        return uiStateText(PicoATE::Core::measurementStatusName(measurement.status));
    default:
        return {};
    }
}

QVariant MeasurementModel::headerData(int section,
                                      Qt::Orientation orientation,
                                      int role) const
{
    if (orientation != Qt::Horizontal || role != Qt::DisplayRole) {
        return {};
    }
    const QStringList headers = {
        uiText("Measurement"),
        uiText("Value"),
        uiText("Unit"),
        uiText("Limits"),
        uiText("Status")};
    return section >= 0 && section < headers.size() ? headers[section] : QVariant();
}

void MeasurementModel::setMeasurements(
    QVector<PicoATE::Core::MeasurementResult> measurements)
{
    beginResetModel();
    m_measurements = std::move(measurements);
    endResetModel();
}

std::optional<PicoATE::Core::MeasurementResult> MeasurementModel::measurementAt(int row) const
{
    if (row < 0 || row >= m_measurements.size()) {
        return std::nullopt;
    }
    return m_measurements[row];
}

RuntimeLogModel::RuntimeLogModel(QObject* parent, int maximumRows)
    : QAbstractTableModel(parent)
    , m_maximumRows(qMax(1, maximumRows))
{
    enableUiModelTranslation(this, false);
}

int RuntimeLogModel::rowCount(const QModelIndex& parent) const
{
    return parent.isValid() ? 0 : m_logs.size();
}

int RuntimeLogModel::columnCount(const QModelIndex&) const
{
    return ColumnCount;
}

QVariant RuntimeLogModel::data(const QModelIndex& index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_logs.size()) {
        return {};
    }
    const auto& event = m_logs[index.row()];
    if (role == Qt::ToolTipRole) {
        return event.message;
    }
    if (role != Qt::DisplayRole) {
        return {};
    }
    switch (index.column()) {
    case TimeColumn:
        return event.timestampUtc.toLocalTime().toString("HH:mm:ss.zzz");
    case UutColumn:
        return event.uutId;
    case StepColumn:
        return event.nodeDisplayName.isEmpty() ? event.nodeId : event.nodeDisplayName;
    case AttemptColumn:
        return event.attemptIndex > 0 ? QVariant(event.attemptIndex) : QVariant{};
    case MessageColumn:
        return event.message;
    default:
        return {};
    }
}

QVariant RuntimeLogModel::headerData(int section,
                                     Qt::Orientation orientation,
                                     int role) const
{
    if (orientation != Qt::Horizontal || role != Qt::DisplayRole) {
        return {};
    }
    switch (section) {
    case TimeColumn:
        return uiText("Time");
    case UutColumn:
        return uiText("UUT");
    case StepColumn:
        return uiText("Step");
    case AttemptColumn:
        return uiText("Attempt");
    case MessageColumn:
        return uiText("Message");
    default:
        return {};
    }
}

void RuntimeLogModel::applyRuntimeEvents(
    const QVector<PicoATE::Core::RuntimeEvent>& events)
{
    QVector<PicoATE::Core::RuntimeEvent> incoming;
    for (const auto& event : events) {
        if (event.kind == PicoATE::Core::RuntimeEventKind::ModuleLog) {
            incoming.push_back(event);
        }
    }
    if (incoming.isEmpty()) {
        return;
    }

    const int overflow = qMax(0, m_logs.size() + incoming.size() - m_maximumRows);
    const int existingRemoveCount = qMin(overflow, m_logs.size());
    if (existingRemoveCount > 0) {
        beginRemoveRows({}, 0, existingRemoveCount - 1);
        m_logs.remove(0, existingRemoveCount);
        endRemoveRows();
    }
    const int incomingRemoveCount = overflow - existingRemoveCount;
    if (incomingRemoveCount > 0) {
        incoming.remove(0, incomingRemoveCount);
    }
    m_droppedRows += static_cast<quint64>(overflow);
    if (incoming.isEmpty()) {
        return;
    }

    const int firstRow = m_logs.size();
    beginInsertRows({}, firstRow, firstRow + incoming.size() - 1);
    m_logs += incoming;
    endInsertRows();
}

void RuntimeLogModel::clear()
{
    beginResetModel();
    m_logs.clear();
    m_droppedRows = 0;
    endResetModel();
}

quint64 RuntimeLogModel::droppedRowCount() const
{
    return m_droppedRows;
}

RuntimeTimelineModel::RuntimeTimelineModel(QObject* parent, int maximumRows)
    : QAbstractTableModel(parent)
    , m_maximumRows(qMax(1, maximumRows))
{
    enableUiModelTranslation(this, false);
}

int RuntimeTimelineModel::rowCount(const QModelIndex& parent) const
{
    return parent.isValid() ? 0 : m_rows.size();
}

int RuntimeTimelineModel::columnCount(const QModelIndex&) const
{
    return ColumnCount;
}

QVariant RuntimeTimelineModel::data(const QModelIndex& index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_rows.size()) {
        return {};
    }

    const auto& row = m_rows[index.row()];
    if (role == Qt::ToolTipRole) {
        return row.message;
    }
    if (role == Qt::TextAlignmentRole) {
        return index.column() == TimeColumn
            ? QVariant::fromValue(Qt::AlignCenter)
            : QVariant::fromValue(Qt::AlignLeft | Qt::AlignVCenter);
    }
    if (role == Qt::FontRole) {
        QFont font;
        font.setBold(true);
        return font;
    }
    if (role == Qt::ForegroundRole && index.column() == MessageColumn) {
        switch (row.style) {
        case LineStyle::Banner:
            return QBrush(QColor(QStringLiteral("#355f78")));
        case LineStyle::Log:
            return QBrush(QColor(QStringLiteral("#334a5a")));
        case LineStyle::Warning:
            return QBrush(QColor(QStringLiteral("#a56600")));
        case LineStyle::Passed:
            return QBrush(QColor(QStringLiteral("#27844b")));
        case LineStyle::Failed:
            return QBrush(QColor(QStringLiteral("#b43a3a")));
        case LineStyle::Flow:
            break;
        }
    }
    if (role != Qt::DisplayRole) {
        return {};
    }

    switch (index.column()) {
    case TimeColumn:
        return row.timestampUtc.isValid()
            ? row.timestampUtc.toLocalTime().toString("HH:mm:ss.zzz")
            : QVariant{};
    case MessageColumn:
        return row.message;
    default:
        return {};
    }
}

QVariant RuntimeTimelineModel::headerData(int section,
                                          Qt::Orientation orientation,
                                          int role) const
{
    if (orientation != Qt::Horizontal || role != Qt::DisplayRole) {
        return {};
    }

    switch (section) {
    case TimeColumn:
        return uiText("Time");
    case MessageColumn:
        return uiText("Message");
    default:
        return {};
    }
}

int RuntimeTimelineModel::indentationFor(
    const PicoATE::Core::RuntimeEvent& event) const
{
    int depth = 0;
    auto parent = event.parentNodeId;
    QSet<PicoATE::Core::NodeId> visited;
    while (!parent.isEmpty() && !visited.contains(parent)) {
        visited.insert(parent);
        ++depth;
        parent = m_parentNodes.value(parent);
    }
    return depth;
}

void RuntimeTimelineModel::appendEventRows(
    const PicoATE::Core::RuntimeEvent& event)
{
    using PicoATE::Core::ActivationState;
    using PicoATE::Core::NodeOutcome;
    using PicoATE::Core::RuntimeEventKind;

    if (!event.nodeId.isEmpty() && !event.parentNodeId.isEmpty()) {
        m_parentNodes.insert(event.nodeId, event.parentNodeId);
    }

    if (event.kind == RuntimeEventKind::AttemptStarted && !event.nodeId.isEmpty()) {
        m_activeAttempts.push_back(qMakePair(event.uutId, event.nodeId));
    }

    auto displayedEvent = event;
    if (event.kind == RuntimeEventKind::DeviceStateChanged &&
        event.nodeId.isEmpty() && !m_activeAttempts.isEmpty()) {
        const auto& active = m_activeAttempts.constLast();
        displayedEvent.uutId = active.first;
        displayedEvent.nodeId = active.second;
        displayedEvent.parentNodeId = m_parentNodes.value(active.second);
    }

    QDateTime timestamp = event.timestampUtc;
    if (event.kind == RuntimeEventKind::ModuleLog) {
        const auto sourceTimestamp =
            event.details.value(QStringLiteral("sourceTimestampUtc")).toDateTime();
        if (sourceTimestamp.isValid()) {
            timestamp = sourceTimestamp;
        }
    }

    const auto indentation = QString(indentationFor(displayedEvent) * 4,
                                     QLatin1Char(' '));
    const auto append = [this, &displayedEvent, &timestamp, &indentation](
                            QString message, LineStyle style) {
        message = message.trimmed();
        if (!message.isEmpty()) {
            m_rows.push_back(Row{displayedEvent,
                                 timestamp,
                                 indentation + message,
                                 style});
        }
    };
    const auto nameToken = [&event] {
        auto name = eventStepText(event).trimmed();
        if (name.isEmpty()) {
            name = QStringLiteral("UNNAMED");
        }
        name.replace(QLatin1Char(' '), QLatin1Char('_'));
        return name.toUpper();
    };
    const auto resultText = [&event] {
        switch (event.outcome) {
        case NodeOutcome::Passed: return QStringLiteral("PASS");
        case NodeOutcome::Failed: return QStringLiteral("FAIL");
        case NodeOutcome::Error: return QStringLiteral("ERROR");
        case NodeOutcome::Timeout: return QStringLiteral("TIMEOUT");
        case NodeOutcome::Cancelled: return QStringLiteral("CANCELLED");
        case NodeOutcome::Skipped: return QStringLiteral("SKIPPED");
        case NodeOutcome::Unknown: return QStringLiteral("UNKNOWN");
        }
        return QStringLiteral("UNKNOWN");
    };
    const auto resultStyle = [&event] {
        return event.outcome == NodeOutcome::Passed
            ? LineStyle::Passed
            : LineStyle::Failed;
    };
    const auto attemptSuffix = [&event] {
        const int maximum = event.details.value(
            QStringLiteral("maxAttempts"), 1).toInt();
        const int current = event.details.value(
            QStringLiteral("retryAttemptIndex"), event.attemptIndex).toInt();
        if (maximum <= 1 || current <= 0) {
            return QString{};
        }
        return QStringLiteral(" | ATTEMPT %1/%2").arg(current).arg(maximum);
    };

    switch (event.kind) {
    case RuntimeEventKind::AttemptStarted:
        append(QStringLiteral("------------------------ %1_STEP_START%2 ------------------------")
                   .arg(nameToken(), attemptSuffix()),
               LineStyle::Banner);
        break;
    case RuntimeEventKind::AttemptCompleted: {
        if (event.nodeKind == PicoATE::Core::ExecNodeKind::TestItem) {
            break;
        }
        if (event.nodeKind == PicoATE::Core::ExecNodeKind::Break &&
            event.details.contains(QStringLiteral("breakRequested"))) {
            const bool requested = event.details.value(
                QStringLiteral("breakRequested")).toBool();
            append(requested
                       ? QStringLiteral("BREAK_RESULT:TRIGGERED")
                       : QStringLiteral("BREAK_RESULT:CONTINUE | CONDITION NOT MET"),
                   requested ? LineStyle::Passed : LineStyle::Flow);
        } else {
            QString result = QStringLiteral("RESULT:%1").arg(resultText());
            if (!event.errorCode.isEmpty()) {
                result += QStringLiteral(" | ERROR:%1").arg(event.errorCode);
            }
            if (!event.message.trimmed().isEmpty()) {
                result += QStringLiteral(" | %1").arg(event.message.trimmed());
            }
            append(result, resultStyle());
        }
        append(QStringLiteral("------------------------ %1_STEP_END ------------------------")
                   .arg(nameToken()),
               LineStyle::Banner);
        break;
    }
    case RuntimeEventKind::TestItemStarted:
        append(QStringLiteral("======================== %1_TESTITEM_START%2 ========================")
                   .arg(nameToken(), attemptSuffix()),
               LineStyle::Banner);
        break;
    case RuntimeEventKind::TestItemCompleted:
    {
        QString result = QStringLiteral("RESULT:%1").arg(resultText());
        if (!event.errorCode.isEmpty()) {
            result += QStringLiteral(" | ERROR:%1").arg(event.errorCode);
        }
        if (!event.message.trimmed().isEmpty()) {
            result += QStringLiteral(" | %1").arg(event.message.trimmed());
        }
        append(result, resultStyle());
        append(QStringLiteral("======================== %1_TESTITEM_END ========================")
                   .arg(nameToken()),
               LineStyle::Banner);
        break;
    }
    case RuntimeEventKind::ModuleLog: {
        auto message = event.message.trimmed();
        for (const auto& prefix : {QStringLiteral("DEBUG:"),
                                   QStringLiteral("INFO:")}) {
            if (message.startsWith(prefix, Qt::CaseInsensitive)) {
                message = QStringLiteral("LOG:") + message.mid(prefix.size()).trimmed();
                break;
            }
        }
        const QStringList recognizedPrefixes = {
            QStringLiteral("LOG:"), QStringLiteral("WARN:"),
            QStringLiteral("WARNING:"), QStringLiteral("ERROR:"),
            QStringLiteral("RESULT:")};
        const bool hasPrefix = std::any_of(
            recognizedPrefixes.cbegin(), recognizedPrefixes.cend(),
            [&message](const QString& prefix) {
                return message.startsWith(prefix, Qt::CaseInsensitive);
            });
        if (!hasPrefix) {
            message.prepend(QStringLiteral("LOG:"));
        }
        const auto style = message.startsWith(QStringLiteral("ERROR:"), Qt::CaseInsensitive)
            ? LineStyle::Failed
            : (message.startsWith(QStringLiteral("WARN"), Qt::CaseInsensitive)
                   ? LineStyle::Warning
                   : LineStyle::Log);
        append(message, style);
        const auto dropped =
            event.details.value(QStringLiteral("droppedBefore")).toULongLong();
        if (dropped > 0) {
            append(QStringLiteral("WARN:%1 earlier log record(s) dropped").arg(dropped),
                   LineStyle::Warning);
        }
        break;
    }
    case RuntimeEventKind::RetryScheduled:
    {
        const int maximum = event.details.value(
            QStringLiteral("maxAttempts"), 1).toInt();
        const int current = event.details.value(
            QStringLiteral("retryAttemptIndex"), event.attemptIndex).toInt();
        QString retry = QStringLiteral("RETRY:SCHEDULED");
        if (maximum > 1 && current > 0) {
            retry += QStringLiteral(" | ATTEMPT %1/%2 FAILED | NEXT %3/%2")
                         .arg(current)
                         .arg(maximum)
                         .arg(qMin(current + 1, maximum));
        }
        if (!event.message.trimmed().isEmpty()) {
            retry += QStringLiteral(" | %1").arg(event.message.trimmed());
        }
        append(retry, LineStyle::Warning);
        break;
    }
    case RuntimeEventKind::LoopIterationStarted:
        if (event.loopIteration.iterationNumber <= 1) {
            append(QStringLiteral("======================== %1_LOOP_START ========================")
                       .arg(nameToken()),
                   LineStyle::Banner);
        }
        append(QStringLiteral("LOOP:%1").arg(
                   loopIterationDescription(event.loopIteration)),
               LineStyle::Flow);
        break;
    case RuntimeEventKind::LoopCompleted:
        append(QStringLiteral("LOOP_RESULT:%1").arg(resultText()), resultStyle());
        append(QStringLiteral("======================== %1_LOOP_END ========================")
                   .arg(nameToken()),
               LineStyle::Banner);
        break;
    case RuntimeEventKind::BarrierWaiting:
        append(QStringLiteral("BARRIER:WAITING"), LineStyle::Flow);
        break;
    case RuntimeEventKind::BarrierReleased:
        append(QStringLiteral("BARRIER:RELEASED"), LineStyle::Flow);
        break;
    case RuntimeEventKind::CleanupActivated:
        append(QStringLiteral("CLEANUP:%1").arg(nameToken()), LineStyle::Flow);
        break;
    case RuntimeEventKind::DeviceStateChanged:
        append(QStringLiteral("DEVICE:%1 | %2 | %3")
                   .arg(event.deviceId,
                        PicoATE::Core::deviceConnectionStateName(event.deviceState),
                        event.message),
               event.deviceState == PicoATE::Core::DeviceConnectionState::Error
                   ? LineStyle::Failed
                   : LineStyle::Flow);
        break;
    case RuntimeEventKind::BreakpointHit:
        append(QStringLiteral("LOG:BREAKPOINT | %1").arg(eventStepText(event)),
               LineStyle::Log);
        break;
    case RuntimeEventKind::DebugStepCompleted:
        append(QStringLiteral("LOG:PAUSED AFTER | %1").arg(eventStepText(event)),
               LineStyle::Log);
        break;
    case RuntimeEventKind::OperatorPromptRequested:
        append(QStringLiteral("PROMPT:OPEN | %1").arg(event.message),
               LineStyle::Warning);
        break;
    case RuntimeEventKind::OperatorPromptClosed:
        append(QStringLiteral("PROMPT:CLOSED | %1").arg(event.message),
               LineStyle::Flow);
        break;
    case RuntimeEventKind::NodeStateChanged:
        if (event.activationState == ActivationState::Skipped ||
            event.activationState == ActivationState::Cancelled) {
            append(QStringLiteral("RESULT:%1 | %2")
                       .arg(activationStateName(event.activationState).toUpper(),
                            eventStepText(event)),
                   LineStyle::Warning);
        }
        break;
    case RuntimeEventKind::SessionStateChanged:
        append(QStringLiteral("SESSION:%1").arg(
                   executionStateName(event.executionState).toUpper()),
               LineStyle::Flow);
        break;
    case RuntimeEventKind::UutCompleted:
        append(QStringLiteral("RUN_RESULT:%1").arg(resultText()), resultStyle());
        break;
    case RuntimeEventKind::UutRegistered:
    case RuntimeEventKind::PeriodicTaskStateChanged:
    case RuntimeEventKind::ResourceStateChanged:
        break;
    }

    if (event.kind == RuntimeEventKind::AttemptCompleted && !event.nodeId.isEmpty()) {
        for (int index = m_activeAttempts.size() - 1; index >= 0; --index) {
            if (m_activeAttempts[index].first == event.uutId &&
                m_activeAttempts[index].second == event.nodeId) {
                m_activeAttempts.removeAt(index);
                break;
            }
        }
    }
}

QVector<RuntimeLogLine> RuntimeTimelineModel::applyRuntimeEvents(
    const QVector<PicoATE::Core::RuntimeEvent>& events)
{
    if (events.isEmpty()) {
        return {};
    }

    auto existingRows = std::move(m_rows);
    m_rows.clear();
    for (const auto& event : events) {
        appendEventRows(event);
    }
    auto appendedRows = std::move(m_rows);
    m_rows = std::move(existingRows);

    QVector<RuntimeLogLine> newLines;
    newLines.reserve(appendedRows.size());
    for (const auto& row : appendedRows) {
        newLines.push_back({row.timestampUtc,
                            row.message,
                            row.event.uutId});
    }

    const int overflow = qMax(0, m_rows.size() + appendedRows.size() - m_maximumRows);
    const int existingRemoveCount = qMin(overflow, m_rows.size());
    if (existingRemoveCount > 0) {
        beginRemoveRows({}, 0, existingRemoveCount - 1);
        m_rows.remove(0, existingRemoveCount);
        endRemoveRows();
    }
    const int appendedRemoveCount = overflow - existingRemoveCount;
    if (appendedRemoveCount > 0) {
        appendedRows.remove(0, appendedRemoveCount);
    }
    m_droppedRows += static_cast<quint64>(overflow);

    if (!appendedRows.isEmpty()) {
        const int firstRow = m_rows.size();
        beginInsertRows({}, firstRow, firstRow + appendedRows.size() - 1);
        m_rows += appendedRows;
        endInsertRows();
    }
    return newLines;
}

void RuntimeTimelineModel::clear()
{
    beginResetModel();
    m_rows.clear();
    m_parentNodes.clear();
    m_activeAttempts.clear();
    m_droppedRows = 0;
    endResetModel();
}

std::optional<PicoATE::Core::RuntimeEvent> RuntimeTimelineModel::eventAt(int row) const
{
    if (row < 0 || row >= m_rows.size()) {
        return std::nullopt;
    }
    return m_rows[row].event;
}

int RuntimeTimelineModel::rowForSequenceNumber(quint64 sequenceNumber) const
{
    if (sequenceNumber == 0) {
        return -1;
    }
    for (int row = 0; row < m_rows.size(); ++row) {
        if (m_rows[row].event.sequenceNumber == sequenceNumber) {
            return row;
        }
    }
    return -1;
}

int RuntimeTimelineModel::rowForNode(
    const PicoATE::Core::UutId& uutId,
    const PicoATE::Core::NodeId& nodeId) const
{
    if (nodeId.isEmpty()) {
        return -1;
    }
    const auto uutMatches = [&uutId](const PicoATE::Core::RuntimeEvent& event) {
        return uutId.isEmpty() || event.uutId.isEmpty() || event.uutId == uutId;
    };
    for (int row = 0; row < m_rows.size(); ++row) {
        const auto& event = m_rows[row].event;
        if (uutMatches(event) && event.nodeId == nodeId) {
            return row;
        }
    }
    for (int row = 0; row < m_rows.size(); ++row) {
        const auto& event = m_rows[row].event;
        if (uutMatches(event) && event.nodeLocalId == nodeId) {
            return row;
        }
    }
    return -1;
}

quint64 RuntimeTimelineModel::droppedRowCount() const
{
    return m_droppedRows;
}

UutRuntimeTimelineProxyModel::UutRuntimeTimelineProxyModel(QObject* parent)
    : QSortFilterProxyModel(parent)
{
    setDynamicSortFilter(true);
}

void UutRuntimeTimelineProxyModel::setVisibleUutId(
    const PicoATE::Core::UutId& uutId)
{
    if (m_visibleUutId == uutId) {
        return;
    }
    m_visibleUutId = uutId;
    invalidateFilter();
}

PicoATE::Core::UutId UutRuntimeTimelineProxyModel::visibleUutId() const
{
    return m_visibleUutId;
}

bool UutRuntimeTimelineProxyModel::filterAcceptsRow(
    int sourceRow,
    const QModelIndex& sourceParent) const
{
    if (m_visibleUutId.isEmpty()) {
        return true;
    }
    const auto* timeline = qobject_cast<const RuntimeTimelineModel*>(sourceModel());
    if (!timeline || sourceParent.isValid()) {
        return true;
    }
    const auto event = timeline->eventAt(sourceRow);
    return !event || event->uutId.isEmpty() || event->uutId == m_visibleUutId;
}

DebugSnapshotModel::DebugSnapshotModel(QObject* parent)
    : QAbstractTableModel(parent)
{
    enableUiModelTranslation(this, false);
}

int DebugSnapshotModel::rowCount(const QModelIndex& parent) const
{
    return parent.isValid() ? 0 : m_rows.size();
}

int DebugSnapshotModel::columnCount(const QModelIndex& parent) const
{
    return parent.isValid() ? 0 : ColumnCount;
}

QVariant DebugSnapshotModel::data(const QModelIndex& index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_rows.size()) {
        return {};
    }

    const auto& row = m_rows[index.row()];
    if (role == Qt::ToolTipRole) {
        return row.value;
    }
    if (role != Qt::DisplayRole) {
        return {};
    }

    switch (index.column()) {
    case SectionColumn:
        return row.section;
    case NameColumn:
        return row.name;
    case ValueColumn:
        return row.value;
    default:
        return {};
    }
}

QVariant DebugSnapshotModel::headerData(int section,
                                        Qt::Orientation orientation,
                                        int role) const
{
    if (orientation != Qt::Horizontal || role != Qt::DisplayRole) {
        return {};
    }

    switch (section) {
    case SectionColumn:
        return uiText("Section");
    case NameColumn:
        return uiText("Name");
    case ValueColumn:
        return uiText("Value");
    default:
        return {};
    }
}

void DebugSnapshotModel::setSnapshot(
    std::optional<PicoATE::Core::ExecutionDebugSnapshot> snapshot)
{
    beginResetModel();
    m_snapshot = std::move(snapshot);
    rebuildRows();
    endResetModel();
}

void DebugSnapshotModel::clear()
{
    setSnapshot(std::nullopt);
}

void DebugSnapshotModel::rebuildRows()
{
    m_rows.clear();
    if (!m_snapshot) {
        return;
    }

    const auto& snapshot = *m_snapshot;
    auto addRow = [this](QString section, QString name, QString value) {
        m_rows.push_back({std::move(section), std::move(name), std::move(value)});
    };
    auto addTime = [&addRow](const QString& section,
                             const QString& name,
                             const QDateTime& time) {
        if (time.isValid()) {
            addRow(section, name, time.toLocalTime().toString(Qt::ISODateWithMs));
        }
    };

    addRow(QStringLiteral("Summary"),
           QStringLiteral("State"),
           executionStateName(snapshot.state));
    addRow(QStringLiteral("Summary"),
           QStringLiteral("Pause reason"),
           debugPauseReasonName(snapshot.pauseReason));
    addRow(QStringLiteral("Summary"), QStringLiteral("Plan"), snapshot.planId);
    addRow(QStringLiteral("Summary"), QStringLiteral("Sequence"), snapshot.sequenceId);
    addRow(QStringLiteral("Summary"), QStringLiteral("Version"), snapshot.sequenceVersion);
    addRow(QStringLiteral("Summary"), QStringLiteral("Current UUT"), snapshot.currentUutId);
    addRow(QStringLiteral("Summary"), QStringLiteral("Current node"), snapshot.currentNodeId);
    addRow(QStringLiteral("Summary"),
           QStringLiteral("Current local path"),
           snapshot.currentLocalPath);
    addTime(QStringLiteral("Summary"), QStringLiteral("Captured at"), snapshot.capturedAt);

    if (snapshot.breakpoint) {
        const auto& breakpoint = *snapshot.breakpoint;
        addRow(QStringLiteral("Breakpoint"),
               QStringLiteral("Id"),
               breakpoint.breakpointId);
        addRow(QStringLiteral("Breakpoint"),
               QStringLiteral("Display name"),
               breakpoint.displayName);
        addRow(QStringLiteral("Breakpoint"),
               QStringLiteral("Hit count"),
               QString::number(breakpoint.hitCount));
        addTime(QStringLiteral("Breakpoint"), QStringLiteral("Hit at"), breakpoint.hitAt);
    }

    for (const auto& uut : snapshot.uuts) {
        const auto variableSection = QStringLiteral("Variables / %1").arg(uut.uutId);
        if (uut.variables.isEmpty()) {
            addRow(variableSection, QStringLiteral("<none>"), {});
        } else {
            for (auto it = uut.variables.cbegin(); it != uut.variables.cend(); ++it) {
                addRow(variableSection, it.key(), variantText(it.value()));
            }
        }

        const auto nodeSection = QStringLiteral("Nodes / %1").arg(uut.uutId);
        for (const auto& node : uut.nodes) {
            const bool current = uut.uutId == snapshot.currentUutId &&
                                 node.nodeId == snapshot.currentNodeId;
            const bool interesting = current ||
                                     node.state != PicoATE::Core::ActivationState::Created ||
                                     !node.attempts.isEmpty();
            if (!interesting) {
                continue;
            }

            const auto name = !node.localPath.isEmpty()
                ? node.localPath
                : (!node.localId.isEmpty() ? node.localId : node.nodeId);
            QStringList values;
            values << node.displayName
                   << execNodeKindName(node.kind)
                   << activationStateName(node.state)
                   << outcomeName(node.outcome)
                   << QStringLiteral("attempts=%1").arg(node.attempts.size());
            if (current) {
                values << QStringLiteral("current");
            }
            addRow(nodeSection, name, values.join(QStringLiteral(" | ")));

            for (const auto& attempt : node.attempts) {
                const auto attemptName = QStringLiteral("%1 / attempt %2")
                                             .arg(name)
                                             .arg(attempt.attemptIndex);
                QStringList attemptValues;
                attemptValues << attempt.attemptId
                              << attemptStateName(attempt.state)
                              << outcomeName(attempt.outcome);
                if (attempt.loopIteration.active) {
                    attemptValues << loopIterationDescription(attempt.loopIteration);
                }
                if (!attempt.errorCode.isEmpty()) {
                    attemptValues << attempt.errorCode;
                }
                if (!attempt.errorMessage.isEmpty()) {
                    attemptValues << attempt.errorMessage;
                }
                addRow(QStringLiteral("Attempts / %1").arg(uut.uutId),
                       attemptName,
                       attemptValues.join(QStringLiteral(" | ")));

                for (auto outputIt = attempt.outputs.cbegin();
                     outputIt != attempt.outputs.cend();
                     ++outputIt) {
                    addRow(QStringLiteral("Outputs / %1").arg(uut.uutId),
                           QStringLiteral("%1.%2").arg(name, outputIt.key()),
                           variantText(outputIt.value()));
                }
            }
        }
    }

    for (const auto& resource : snapshot.resources.resources) {
        addRow(QStringLiteral("Resources / state"),
               resource.resourceId,
               QStringLiteral("leases=%1").arg(stringVectorText(resource.activeLeases)));
    }
    for (const auto& lease : snapshot.resources.activeLeases) {
        addRow(QStringLiteral("Resources / active"),
               lease.leaseId,
               QStringLiteral("%1 %2 %3")
                   .arg(lease.uutId, lease.nodeId, requirementsText(lease.requirements)));
    }
    for (const auto& waiter : snapshot.resources.waiters) {
        addRow(QStringLiteral("Resources / waiters"),
               waiter.requestId,
               QStringLiteral("%1 %2 p%3 %4")
                   .arg(waiter.uutId, waiter.nodeId)
                   .arg(waiter.priority)
                   .arg(requirementsText(waiter.requirements)));
    }

    for (const auto& barrier : snapshot.barriers) {
        addRow(QStringLiteral("Barriers"),
               barrier.id,
               QStringLiteral("%1 %2 expected=[%3] arrived=[%4] released=[%5]")
                   .arg(barrier.barrierName,
                        barrierStateName(barrier.state),
                        uutSetText(barrier.expected),
                        uutSetText(barrier.arrived),
                        uutSetText(barrier.released)));
    }
}

} // namespace PicoATE::Ui
