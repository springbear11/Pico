#include "RunnerModels.h"

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
    if (measurement.hasLowerLimit && measurement.hasUpperLimit) {
        return QStringLiteral("[%1, %2]")
            .arg(measurement.lowerLimit)
            .arg(measurement.upperLimit);
    }
    if (measurement.hasLowerLimit) {
        return QStringLiteral(">= %1").arg(measurement.lowerLimit);
    }
    if (measurement.hasUpperLimit) {
        return QStringLiteral("<= %1").arg(measurement.upperLimit);
    }
    return QStringLiteral("-");
}

QString measurementValueText(const PicoATE::Core::MeasurementResult& measurement)
{
    const auto value = variantText(measurement.value);
    return measurement.unit.isEmpty() ? value : QStringLiteral("%1 %2").arg(value, measurement.unit);
}

QString inferredLimitText(const PicoATE::Core::MeasurementResult& measurement,
                          bool lower)
{
    const auto displayKey = lower ? QStringLiteral("displayLower")
                                  : QStringLiteral("displayUpper");
    const auto display = measurement.attributes.value(displayKey);
    if (display.isValid() && !display.isNull()) {
        return variantText(display);
    }
    if (lower && measurement.hasLowerLimit) {
        return QString::number(measurement.lowerLimit, 'g', 12);
    }
    if (!lower && measurement.hasUpperLimit) {
        return QString::number(measurement.upperLimit, 'g', 12);
    }

    auto comparison = measurement.attributes
                          .value(QStringLiteral("comparison")).toString()
                          .trimmed().toLower();
    comparison.remove(QLatin1Char('-'));
    comparison.remove(QLatin1Char('_'));
    comparison.remove(QLatin1Char(' '));
    const bool equality = comparison == QStringLiteral("==") ||
        comparison == QStringLiteral("eq") || comparison == QStringLiteral("equal") ||
        comparison == QStringLiteral("!=") || comparison == QStringLiteral("ne") ||
        comparison == QStringLiteral("notequal");
    const bool lowerBound = comparison == QStringLiteral(">") ||
        comparison == QStringLiteral(">=") || comparison == QStringLiteral("gt") ||
        comparison == QStringLiteral("ge") || comparison == QStringLiteral("gte") ||
        comparison == QStringLiteral("greaterthan") ||
        comparison == QStringLiteral("greaterorequal");
    const bool upperBound = comparison == QStringLiteral("<") ||
        comparison == QStringLiteral("<=") || comparison == QStringLiteral("lt") ||
        comparison == QStringLiteral("le") || comparison == QStringLiteral("lte") ||
        comparison == QStringLiteral("lessthan") ||
        comparison == QStringLiteral("lessorequal");
    if ((!equality && lower && !lowerBound) ||
        (!equality && !lower && !upperBound)) {
        return QStringLiteral("-");
    }
    const auto expected = measurement.attributes.value(QStringLiteral("expected"));
    return expected.isValid() && !expected.isNull()
        ? variantText(expected)
        : QStringLiteral("-");
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

QString lowerLimitText(const PicoATE::Core::MeasurementResult& measurement)
{
    return inferredLimitText(measurement, true);
}

QString upperLimitText(const PicoATE::Core::MeasurementResult& measurement)
{
    return inferredLimitText(measurement, false);
}

QString stepErrorCode(const PicoATE::Core::StepReport& step)
{
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

} // namespace

DiagnosticModel::DiagnosticModel(QObject* parent)
    : QAbstractTableModel(parent)
{
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
    static const QStringList headers = {
        QStringLiteral("Severity"),
        QStringLiteral("Path"),
        QStringLiteral("Message"),
        QStringLiteral("Suggestion")};
    return section >= 0 && section < headers.size() ? headers[section] : QVariant();
}

void DiagnosticModel::setDiagnostics(QVector<UiDiagnostic> diagnostics)
{
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
            return PicoATE::Core::executionPhaseName(modelItem->phase).toUpper();
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
                ? QStringLiteral("Completed")
                : (m_report.state == PicoATE::Core::ExecutionState::Running
                       ? QStringLiteral("Running")
                       : QStringLiteral("Pending"));
        case OutcomeColumn:
            if (uut.hasError) {
                return QStringLiteral("Failed");
            }
            return m_completedUuts.contains(uut.uutId)
                ? QStringLiteral("Passed")
                : QStringLiteral("Unknown");
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
    if (role == Qt::BackgroundRole) {
        using PicoATE::Core::ActivationState;
        switch (step.state) {
        case ActivationState::Running:
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
    if (role == Qt::ToolTipRole && !step.attempts.isEmpty()) {
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
        return joinedMeasurementText(step.measurements, measurementValueText);
    case OutcomeColumn:
        return step.outcome == PicoATE::Core::NodeOutcome::Unknown
            ? activationStateName(step.state)
            : outcomeName(step.outcome);
    case TimeColumn:
        return durationText(step.durationMs);
    case StateColumn:
        return activationStateName(step.state);
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
        m_singleUutPhaseLayout ? QStringLiteral("Phase / Step")
                               : QStringLiteral("UUT / Step"),
        QStringLiteral("Error Code"),
        QStringLiteral("Lower"),
        QStringLiteral("Upper"),
        QStringLiteral("Actual"),
        QStringLiteral("Result"),
        QStringLiteral("Time"),
        QStringLiteral("State"),
        QStringLiteral("Attempts"),
        QStringLiteral("Loop")};
    return section >= 0 && section < headers.size() ? headers[section] : QVariant();
}

void UutStepModel::setReport(PicoATE::Core::ExecutionReport report)
{
    beginResetModel();
    m_report = std::move(report);
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

    beginResetModel();
    for (const auto& event : events) {
        if (m_report.planId.isEmpty() && !event.planId.isEmpty()) {
            m_report.planId = event.planId;
        }
        if (event.kind == PicoATE::Core::RuntimeEventKind::SessionStateChanged) {
            m_report.state = event.executionState;
            m_report.completed = event.executionState == PicoATE::Core::ExecutionState::Completed ||
                                 event.executionState == PicoATE::Core::ExecutionState::CompletedWithError ||
                                 event.executionState == PicoATE::Core::ExecutionState::Aborted;
            continue;
        }
        if (event.uutId.isEmpty()) {
            continue;
        }

        auto& uut = ensureUut(event.uutId);
        if (event.kind == PicoATE::Core::RuntimeEventKind::UutRegistered) {
            continue;
        }
        if (event.kind == PicoATE::Core::RuntimeEventKind::UutCompleted) {
            m_completedUuts.insert(event.uutId);
            uut.hasError = event.details.value("hasError").toBool();
            m_report.hasError = m_report.hasError || uut.hasError;
            continue;
        }
        if (event.nodeId.isEmpty()) {
            continue;
        }

        const bool updatesStep = [&event] {
            switch (event.kind) {
            case PicoATE::Core::RuntimeEventKind::NodeStateChanged:
            case PicoATE::Core::RuntimeEventKind::BarrierWaiting:
            case PicoATE::Core::RuntimeEventKind::BarrierReleased:
            case PicoATE::Core::RuntimeEventKind::CleanupActivated:
            case PicoATE::Core::RuntimeEventKind::LoopIterationStarted:
            case PicoATE::Core::RuntimeEventKind::LoopCompleted:
            case PicoATE::Core::RuntimeEventKind::TestItemStarted:
            case PicoATE::Core::RuntimeEventKind::TestItemCompleted:
            case PicoATE::Core::RuntimeEventKind::AttemptStarted:
            case PicoATE::Core::RuntimeEventKind::AttemptCompleted:
                return true;
            default:
                return false;
            }
        }();
        if (!updatesStep) {
            continue;
        }

        auto& step = ensureStep(uut, event);
        if (event.details.contains("durationMs")) {
            step.durationMs = event.details.value("durationMs").toLongLong();
        }
        switch (event.kind) {
        case PicoATE::Core::RuntimeEventKind::NodeStateChanged:
        case PicoATE::Core::RuntimeEventKind::BarrierWaiting:
        case PicoATE::Core::RuntimeEventKind::BarrierReleased:
        case PicoATE::Core::RuntimeEventKind::CleanupActivated:
        case PicoATE::Core::RuntimeEventKind::LoopIterationStarted:
        case PicoATE::Core::RuntimeEventKind::LoopCompleted:
        case PicoATE::Core::RuntimeEventKind::TestItemStarted:
        case PicoATE::Core::RuntimeEventKind::TestItemCompleted:
            step.state = event.activationState;
            if (event.outcome != PicoATE::Core::NodeOutcome::Unknown) {
                step.outcome = event.outcome;
            }
            break;
        case PicoATE::Core::RuntimeEventKind::AttemptStarted:
        case PicoATE::Core::RuntimeEventKind::AttemptCompleted: {
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

        step.wasError = step.outcome == PicoATE::Core::NodeOutcome::Failed ||
                        step.outcome == PicoATE::Core::NodeOutcome::Error ||
                        step.outcome == PicoATE::Core::NodeOutcome::Timeout;
        uut.hasError = uut.hasError || step.wasError;
        m_report.hasError = m_report.hasError || uut.hasError;
    }
    rebuildIndexTree();
    endResetModel();
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
    return step ? std::optional<PicoATE::Core::StepReport>(*step) : std::nullopt;
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
    return m_report.uuts[uutIndex];
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
    PicoATE::Core::UutReport& uut,
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
    auto* existing = findStep(uut.steps, event.nodeId);
    if (!event.parentNodeId.isEmpty()) {
        auto* parent = findStep(uut.steps, event.parentNodeId);
        if (!parent) {
            PicoATE::Core::StepReport createdParent;
            createdParent.stepId = event.parentNodeId.section('.', -1);
            createdParent.nodePath = event.parentNodeId;
            createdParent.displayName = event.parentNodeId;
            createdParent.kind = PicoATE::Core::ExecNodeKind::TestItem;
            createdParent.phase = event.nodePhase;
            uut.steps.push_back(createdParent);
            parent = &uut.steps.last();
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
    uut.steps.push_back(step);
    return uut.steps.last();
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

void UutStepModel::rebuildIndexTree()
{
    m_modelItems.clear();
    m_rootItems.clear();
    if (m_singleUutPhaseLayout && m_report.uuts.size() == 1) {
        auto& uut = m_report.uuts.first();
        const QVector<PicoATE::Core::ExecutionPhase> phases = {
            PicoATE::Core::ExecutionPhase::Setup,
            PicoATE::Core::ExecutionPhase::Main,
            PicoATE::Core::ExecutionPhase::Cleanup};
        for (const auto phase : phases) {
            const bool hasSteps = std::any_of(
                uut.steps.cbegin(), uut.steps.cend(),
                [phase](const auto& step) { return step.phase == phase; });
            if (!hasSteps) {
                continue;
            }
            auto root = std::make_unique<ModelItem>();
            root->isPhase = true;
            root->phase = phase;
            root->uutIndex = 0;
            root->row = m_rootItems.size();
            auto* rootPointer = root.get();
            m_modelItems.push_back(std::move(root));
            m_rootItems.push_back(rootPointer);
            for (auto& step : uut.steps) {
                if (step.phase == phase) {
                    appendModelItem(rootPointer, 0, &step);
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
        if (m_report.uuts[item->uutIndex].uutId != uutId) {
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

DeviceStatusModel::DeviceStatusModel(QObject* parent)
    : QAbstractTableModel(parent)
{
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
        return PicoATE::Core::deviceConnectionStateName(device.state);
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
    static const QStringList headers = {
        QStringLiteral("Device"),
        QStringLiteral("Type"),
        QStringLiteral("Driver"),
        QStringLiteral("State"),
        QStringLiteral("Message")};
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
        return executionStateName(entry.state);
    case ResultColumn:
        return entry.hasError ? QStringLiteral("Failed") : QStringLiteral("Passed");
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
    static const QStringList headers = {
        QStringLiteral("Saved"),
        QStringLiteral("Sequence"),
        QStringLiteral("Version"),
        QStringLiteral("State"),
        QStringLiteral("Result"),
        QStringLiteral("UUTs")};
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
        return outcomeName(attempt.outcome);
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
    static const QStringList headers = {
        QStringLiteral("Attempt"),
        QStringLiteral("Outcome"),
        QStringLiteral("Loop iteration"),
        QStringLiteral("Measurements"),
        QStringLiteral("Error")};
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
        return PicoATE::Core::measurementStatusName(measurement.status);
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
    static const QStringList headers = {
        QStringLiteral("Measurement"),
        QStringLiteral("Value"),
        QStringLiteral("Unit"),
        QStringLiteral("Limits"),
        QStringLiteral("Status")};
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
        return tr("Time");
    case UutColumn:
        return tr("UUT");
    case StepColumn:
        return tr("Step");
    case AttemptColumn:
        return tr("Attempt");
    case MessageColumn:
        return tr("Message");
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

    beginResetModel();
    m_logs += incoming;
    if (m_logs.size() > m_maximumRows) {
        const int removeCount = m_logs.size() - m_maximumRows;
        m_logs.remove(0, removeCount);
        m_droppedRows += static_cast<quint64>(removeCount);
    }
    endResetModel();
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
        return tr("Time");
    case MessageColumn:
        return tr("Message");
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

    switch (event.kind) {
    case RuntimeEventKind::AttemptStarted:
        append(QStringLiteral("------------------------ %1_STEP_START ------------------------")
                   .arg(nameToken()),
               LineStyle::Banner);
        break;
    case RuntimeEventKind::AttemptCompleted: {
        if (event.nodeKind == PicoATE::Core::ExecNodeKind::TestItem) {
            break;
        }
        QString result = QStringLiteral("RESULT:%1").arg(resultText());
        if (event.nodeKind == PicoATE::Core::ExecNodeKind::Break &&
            event.details.contains(QStringLiteral("breakRequested"))) {
            result += event.details.value(QStringLiteral("breakRequested")).toBool()
                ? QStringLiteral(" | BREAK:CONDITION MET")
                : QStringLiteral(" | BREAK:CONDITION NOT MET");
        }
        if (!event.errorCode.isEmpty()) {
            result += QStringLiteral(" | ERROR:%1").arg(event.errorCode);
        }
        if (!event.message.trimmed().isEmpty()) {
            result += QStringLiteral(" | %1").arg(event.message.trimmed());
        }
        append(result, resultStyle());
        append(QStringLiteral("------------------------ %1_STEP_END ------------------------")
                   .arg(nameToken()),
               LineStyle::Banner);
        break;
    }
    case RuntimeEventKind::TestItemStarted:
        append(QStringLiteral("======================== %1_TESTITEM_START ========================")
                   .arg(nameToken()),
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
        append(QStringLiteral("RETRY:%1").arg(event.message), LineStyle::Warning);
        break;
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

    beginResetModel();
    const int firstNewRow = m_rows.size();
    for (const auto& event : events) {
        appendEventRows(event);
    }
    QVector<RuntimeLogLine> newLines;
    newLines.reserve(m_rows.size() - firstNewRow);
    for (int row = firstNewRow; row < m_rows.size(); ++row) {
        newLines.push_back({m_rows[row].timestampUtc, m_rows[row].message});
    }
    if (m_rows.size() > m_maximumRows) {
        const int removeCount = m_rows.size() - m_maximumRows;
        m_rows.remove(0, removeCount);
        m_droppedRows += static_cast<quint64>(removeCount);
    }
    endResetModel();
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

DebugSnapshotModel::DebugSnapshotModel(QObject* parent)
    : QAbstractTableModel(parent)
{
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
        return QStringLiteral("Section");
    case NameColumn:
        return QStringLiteral("Name");
    case ValueColumn:
        return QStringLiteral("Value");
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
