#include "RunnerModels.h"

#include "PicoATE/Core/MeasurementTypes.h"

#include <QBrush>
#include <QColor>

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
        return QStringLiteral("Created");
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

QBrush diagnosticBrush(UiDiagnosticSeverity severity)
{
    return QBrush(QColor(severity == UiDiagnosticSeverity::Error
                             ? QStringLiteral("#b43a3a")
                             : QStringLiteral("#a56600")));
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
        return row < m_report.uuts.size() ? createIndex(row, column, quintptr(0)) : QModelIndex();
    }
    if (isStepIndex(parentIndex) || parentIndex.column() != 0) {
        return {};
    }
    const int uutIndex = parentIndex.row();
    if (uutIndex < 0 || uutIndex >= m_report.uuts.size() ||
        row >= m_report.uuts[uutIndex].steps.size()) {
        return {};
    }
    return createIndex(row, column, quintptr(uutIndex + 1));
}

QModelIndex UutStepModel::parent(const QModelIndex& child) const
{
    if (!child.isValid() || !isStepIndex(child)) {
        return {};
    }
    const int uutIndex = uutIndexFor(child);
    return uutIndex >= 0 && uutIndex < m_report.uuts.size()
        ? createIndex(uutIndex, 0, quintptr(0))
        : QModelIndex();
}

int UutStepModel::rowCount(const QModelIndex& parentIndex) const
{
    if (!parentIndex.isValid()) {
        return m_report.uuts.size();
    }
    if (isStepIndex(parentIndex) || parentIndex.column() != 0) {
        return 0;
    }
    const int uutIndex = parentIndex.row();
    return uutIndex >= 0 && uutIndex < m_report.uuts.size()
        ? m_report.uuts[uutIndex].steps.size()
        : 0;
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

    if (!isStepIndex(index)) {
        if (role == Qt::ForegroundRole && index.column() == OutcomeColumn) {
            return outcomeBrush(uut.hasError
                                    ? PicoATE::Core::NodeOutcome::Failed
                                    : PicoATE::Core::NodeOutcome::Passed);
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
                attempts += step.attempts.size();
            }
            return attempts;
        }
        default:
            return {};
        }
    }

    if (index.row() < 0 || index.row() >= uut.steps.size()) {
        return {};
    }
    const auto& step = uut.steps[index.row()];
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
    case StateColumn:
        return activationStateName(step.state);
    case OutcomeColumn:
        return outcomeName(step.outcome);
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
    static const QStringList headers = {
        QStringLiteral("UUT / Step"),
        QStringLiteral("State"),
        QStringLiteral("Outcome"),
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

        auto& step = ensureStep(uut, event);
        switch (event.kind) {
        case PicoATE::Core::RuntimeEventKind::NodeStateChanged:
        case PicoATE::Core::RuntimeEventKind::BarrierWaiting:
        case PicoATE::Core::RuntimeEventKind::BarrierReleased:
        case PicoATE::Core::RuntimeEventKind::CleanupActivated:
        case PicoATE::Core::RuntimeEventKind::LoopIterationStarted:
        case PicoATE::Core::RuntimeEventKind::LoopCompleted:
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
    endResetModel();
}

void UutStepModel::clear()
{
    setReport({});
}

UutStepModel::ItemType UutStepModel::itemType(const QModelIndex& index) const
{
    return isStepIndex(index) ? StepItem : UutItem;
}

std::optional<PicoATE::Core::StepReport> UutStepModel::stepAt(const QModelIndex& index) const
{
    if (!index.isValid() || !isStepIndex(index)) {
        return std::nullopt;
    }
    const int uutIndex = uutIndexFor(index);
    if (uutIndex < 0 || uutIndex >= m_report.uuts.size() ||
        index.row() < 0 || index.row() >= m_report.uuts[uutIndex].steps.size()) {
        return std::nullopt;
    }
    return m_report.uuts[uutIndex].steps[index.row()];
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
    for (int uutIndex = 0; uutIndex < m_report.uuts.size(); ++uutIndex) {
        const auto& uut = m_report.uuts[uutIndex];
        if (uut.uutId != uutId) {
            continue;
        }
        for (int stepIndex = 0; stepIndex < uut.steps.size(); ++stepIndex) {
            if (uut.steps[stepIndex].stepId == stepId) {
                return index(stepIndex, 0, index(uutIndex, 0));
            }
        }
    }
    return {};
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
    auto it = std::find_if(uut.steps.begin(), uut.steps.end(), [&event](const auto& item) {
        return item.stepId == event.nodeId;
    });
    if (it != uut.steps.end()) {
        return *it;
    }
    PicoATE::Core::StepReport step;
    step.stepId = event.nodeId;
    step.displayName = event.nodeDisplayName;
    step.kind = event.nodeKind;
    uut.steps.push_back(step);
    return uut.steps.last();
}

bool UutStepModel::isStepIndex(const QModelIndex& index) const
{
    return index.isValid() && index.internalId() > 0;
}

int UutStepModel::uutIndexFor(const QModelIndex& index) const
{
    if (!index.isValid()) {
        return -1;
    }
    return isStepIndex(index)
        ? static_cast<int>(index.internalId()) - 1
        : index.row();
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

} // namespace PicoATE::Ui
