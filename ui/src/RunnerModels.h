#pragma once

#include "UiExecutionTypes.h"
#include "PicoATE/Core/ExecutionDebug.h"
#include "PicoATE/Core/RuntimeEvent.h"
#include "ReportHistoryStore.h"
#include "RunArtifactWriter.h"

#include <QAbstractItemModel>
#include <QAbstractTableModel>
#include <QHash>

#include <optional>
#include <memory>
#include <vector>

namespace PicoATE::Ui {

class DiagnosticModel final : public QAbstractTableModel
{
    Q_OBJECT

public:
    enum Column {
        SeverityColumn,
        PathColumn,
        MessageColumn,
        SuggestionColumn,
        ColumnCount
    };

    explicit DiagnosticModel(QObject* parent = nullptr);

    int rowCount(const QModelIndex& parent = {}) const override;
    int columnCount(const QModelIndex& parent = {}) const override;
    QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;
    QVariant headerData(int section,
                        Qt::Orientation orientation,
                        int role = Qt::DisplayRole) const override;

    void setDiagnostics(QVector<UiDiagnostic> diagnostics);
    std::optional<UiDiagnostic> diagnosticAt(int row) const;

private:
    QVector<UiDiagnostic> m_diagnostics;
};

class UutStepModel final : public QAbstractItemModel
{
    Q_OBJECT

public:
    enum Column {
        NameColumn,
        ErrorCodeColumn,
        LowerLimitColumn,
        UpperLimitColumn,
        ActualColumn,
        OutcomeColumn,
        TimeColumn,
        StateColumn,
        AttemptsColumn,
        LoopColumn,
        BreakpointVisualColumn,
        ColumnCount
    };

    enum ItemType {
        UutItem,
        PhaseItem,
        StepItem
    };

    explicit UutStepModel(QObject* parent = nullptr);

    QModelIndex index(int row,
                      int column,
                      const QModelIndex& parent = {}) const override;
    QModelIndex parent(const QModelIndex& child) const override;
    int rowCount(const QModelIndex& parent = {}) const override;
    int columnCount(const QModelIndex& parent = {}) const override;
    QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;
    QVariant headerData(int section,
                        Qt::Orientation orientation,
                        int role = Qt::DisplayRole) const override;

    void setReport(PicoATE::Core::ExecutionReport report);
    void setSingleUutPhaseLayout(bool enabled);
    void applyRuntimeEvents(const QVector<PicoATE::Core::RuntimeEvent>& events);
    void clear();
    ItemType itemType(const QModelIndex& index) const;
    std::optional<PicoATE::Core::StepReport> stepAt(const QModelIndex& index) const;
    std::optional<PicoATE::Core::UutReport> uutAt(const QModelIndex& index) const;
    QModelIndex indexForStep(const PicoATE::Core::UutId& uutId,
                             const PicoATE::Core::NodeId& stepId) const;
    int visualLineNumber(const QModelIndex& index) const;

private:
    struct ModelItem {
        bool isUut = false;
        bool isPhase = false;
        int uutIndex = -1;
        int row = -1;
        PicoATE::Core::ExecutionPhase phase = PicoATE::Core::ExecutionPhase::Main;
        PicoATE::Core::StepReport* step = nullptr;
        int visualLineNumber = 0;
        ModelItem* parent = nullptr;
        QVector<ModelItem*> children;
    };

    bool isStepIndex(const QModelIndex& index) const;
    int uutIndexFor(const QModelIndex& index) const;
    const PicoATE::Core::StepReport* stepForIndex(const QModelIndex& index) const;
    void rebuildIndexTree();
    ModelItem* appendModelItem(ModelItem* parent,
                               int uutIndex,
                               PicoATE::Core::StepReport* step);
    ModelItem* findModelItem(const PicoATE::Core::UutId& uutId,
                             const PicoATE::Core::NodeId& stepId) const;
    PicoATE::Core::UutReport& ensureUut(const PicoATE::Core::UutId& uutId);
    PicoATE::Core::StepReport& ensureStep(PicoATE::Core::UutReport& uut,
                                          const PicoATE::Core::RuntimeEvent& event);

    PicoATE::Core::ExecutionReport m_report;
    QSet<PicoATE::Core::UutId> m_completedUuts;
    bool m_singleUutPhaseLayout = false;
    std::vector<std::unique_ptr<ModelItem>> m_modelItems;
    QVector<ModelItem*> m_rootItems;
    int m_nextVisualLineNumber = 1;
};

class DeviceStatusModel final : public QAbstractTableModel
{
    Q_OBJECT

public:
    enum Column {
        DeviceColumn,
        TypeColumn,
        DriverColumn,
        StateColumn,
        MessageColumn,
        ColumnCount
    };

    explicit DeviceStatusModel(QObject* parent = nullptr);

    int rowCount(const QModelIndex& parent = {}) const override;
    int columnCount(const QModelIndex& parent = {}) const override;
    QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;
    QVariant headerData(int section,
                        Qt::Orientation orientation,
                        int role = Qt::DisplayRole) const override;

    void applyRuntimeEvents(const QVector<PicoATE::Core::RuntimeEvent>& events);
    void clear();

private:
    struct DeviceRow {
        PicoATE::Core::DeviceId id;
        QString type;
        QString driver;
        PicoATE::Core::DeviceConnectionState state =
            PicoATE::Core::DeviceConnectionState::Disconnected;
        QString message;
        QString errorCode;
    };

    QVector<DeviceRow> m_devices;
};

class HistoryModel final : public QAbstractTableModel
{
    Q_OBJECT

public:
    enum Column {
        SavedAtColumn,
        SequenceColumn,
        VersionColumn,
        StateColumn,
        ResultColumn,
        UutsColumn,
        ColumnCount
    };

    explicit HistoryModel(QObject* parent = nullptr);

    int rowCount(const QModelIndex& parent = {}) const override;
    int columnCount(const QModelIndex& parent = {}) const override;
    QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;
    QVariant headerData(int section,
                        Qt::Orientation orientation,
                        int role = Qt::DisplayRole) const override;

    void setEntries(QVector<ReportHistoryEntry> entries);
    std::optional<ReportHistoryEntry> entryAt(int row) const;

private:
    QVector<ReportHistoryEntry> m_entries;
};

class AttemptModel final : public QAbstractTableModel
{
    Q_OBJECT

public:
    enum Column {
        IndexColumn,
        OutcomeColumn,
        LoopColumn,
        MeasurementCountColumn,
        ErrorColumn,
        ColumnCount
    };

    explicit AttemptModel(QObject* parent = nullptr);

    int rowCount(const QModelIndex& parent = {}) const override;
    int columnCount(const QModelIndex& parent = {}) const override;
    QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;
    QVariant headerData(int section,
                        Qt::Orientation orientation,
                        int role = Qt::DisplayRole) const override;

    void setStep(std::optional<PicoATE::Core::StepReport> step);
    std::optional<PicoATE::Core::AttemptReport> attemptAt(int row) const;

private:
    std::optional<PicoATE::Core::StepReport> m_step;
};

class MeasurementModel final : public QAbstractTableModel
{
    Q_OBJECT

public:
    enum Column {
        NameColumn,
        ValueColumn,
        UnitColumn,
        LimitsColumn,
        StatusColumn,
        ColumnCount
    };

    explicit MeasurementModel(QObject* parent = nullptr);

    int rowCount(const QModelIndex& parent = {}) const override;
    int columnCount(const QModelIndex& parent = {}) const override;
    QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;
    QVariant headerData(int section,
                        Qt::Orientation orientation,
                        int role = Qt::DisplayRole) const override;

    void setMeasurements(QVector<PicoATE::Core::MeasurementResult> measurements);
    std::optional<PicoATE::Core::MeasurementResult> measurementAt(int row) const;

private:
    QVector<PicoATE::Core::MeasurementResult> m_measurements;
};

class RuntimeLogModel final : public QAbstractTableModel
{
    Q_OBJECT

public:
    enum Column {
        TimeColumn,
        UutColumn,
        StepColumn,
        AttemptColumn,
        MessageColumn,
        ColumnCount
    };

    explicit RuntimeLogModel(QObject* parent = nullptr, int maximumRows = 10000);

    int rowCount(const QModelIndex& parent = {}) const override;
    int columnCount(const QModelIndex& parent = {}) const override;
    QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;
    QVariant headerData(int section,
                        Qt::Orientation orientation,
                        int role = Qt::DisplayRole) const override;

    void applyRuntimeEvents(const QVector<PicoATE::Core::RuntimeEvent>& events);
    void clear();
    quint64 droppedRowCount() const;

private:
    const int m_maximumRows;
    QVector<PicoATE::Core::RuntimeEvent> m_logs;
    quint64 m_droppedRows = 0;
};

class RuntimeTimelineModel final : public QAbstractTableModel
{
    Q_OBJECT

public:
    enum Column {
        TimeColumn,
        MessageColumn,
        ColumnCount
    };

    explicit RuntimeTimelineModel(QObject* parent = nullptr,
                                  int maximumRows = 20000);

    int rowCount(const QModelIndex& parent = {}) const override;
    int columnCount(const QModelIndex& parent = {}) const override;
    QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;
    QVariant headerData(int section,
                        Qt::Orientation orientation,
                        int role = Qt::DisplayRole) const override;

    QVector<RuntimeLogLine> applyRuntimeEvents(
        const QVector<PicoATE::Core::RuntimeEvent>& events);
    void clear();
    std::optional<PicoATE::Core::RuntimeEvent> eventAt(int row) const;
    int rowForSequenceNumber(quint64 sequenceNumber) const;
    int rowForNode(const PicoATE::Core::UutId& uutId,
                   const PicoATE::Core::NodeId& nodeId) const;
    quint64 droppedRowCount() const;

private:
    enum class LineStyle {
        Flow,
        Banner,
        Log,
        Warning,
        Passed,
        Failed
    };

    struct Row {
        PicoATE::Core::RuntimeEvent event;
        QDateTime timestampUtc;
        QString message;
        LineStyle style = LineStyle::Flow;
    };

    void appendEventRows(const PicoATE::Core::RuntimeEvent& event);
    int indentationFor(const PicoATE::Core::RuntimeEvent& event) const;

    const int m_maximumRows;
    QVector<Row> m_rows;
    QHash<PicoATE::Core::NodeId, PicoATE::Core::NodeId> m_parentNodes;
    QVector<QPair<PicoATE::Core::UutId, PicoATE::Core::NodeId>> m_activeAttempts;
    quint64 m_droppedRows = 0;
};

class DebugSnapshotModel final : public QAbstractTableModel
{
    Q_OBJECT

public:
    enum Column {
        SectionColumn,
        NameColumn,
        ValueColumn,
        ColumnCount
    };

    explicit DebugSnapshotModel(QObject* parent = nullptr);

    int rowCount(const QModelIndex& parent = {}) const override;
    int columnCount(const QModelIndex& parent = {}) const override;
    QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;
    QVariant headerData(int section,
                        Qt::Orientation orientation,
                        int role = Qt::DisplayRole) const override;

    void setSnapshot(std::optional<PicoATE::Core::ExecutionDebugSnapshot> snapshot);
    void clear();

private:
    struct Row {
        QString section;
        QString name;
        QString value;
    };

    void rebuildRows();

    std::optional<PicoATE::Core::ExecutionDebugSnapshot> m_snapshot;
    QVector<Row> m_rows;
};

} // namespace PicoATE::Ui
