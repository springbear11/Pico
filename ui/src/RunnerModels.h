#pragma once

#include "UiExecutionTypes.h"
#include "PicoATE/Core/RuntimeEvent.h"
#include "ReportHistoryStore.h"

#include <QAbstractItemModel>
#include <QAbstractTableModel>

#include <optional>

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
        StateColumn,
        OutcomeColumn,
        AttemptsColumn,
        LoopColumn,
        ColumnCount
    };

    enum ItemType {
        UutItem,
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
    void applyRuntimeEvents(const QVector<PicoATE::Core::RuntimeEvent>& events);
    void clear();
    ItemType itemType(const QModelIndex& index) const;
    std::optional<PicoATE::Core::StepReport> stepAt(const QModelIndex& index) const;
    std::optional<PicoATE::Core::UutReport> uutAt(const QModelIndex& index) const;
    QModelIndex indexForStep(const PicoATE::Core::UutId& uutId,
                             const PicoATE::Core::NodeId& stepId) const;

private:
    bool isStepIndex(const QModelIndex& index) const;
    int uutIndexFor(const QModelIndex& index) const;
    PicoATE::Core::UutReport& ensureUut(const PicoATE::Core::UutId& uutId);
    PicoATE::Core::StepReport& ensureStep(PicoATE::Core::UutReport& uut,
                                          const PicoATE::Core::RuntimeEvent& event);

    PicoATE::Core::ExecutionReport m_report;
    QSet<PicoATE::Core::UutId> m_completedUuts;
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

} // namespace PicoATE::Ui
