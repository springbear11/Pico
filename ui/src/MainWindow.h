#pragma once

#include "PicoATE/Core/RuntimeEvent.h"
#include "ReportHistoryStore.h"

#include <QMainWindow>

#include <memory>

class QAction;
class QLineEdit;
class QSortFilterProxyModel;
class QSpinBox;
class QTableView;
class QTreeView;

namespace PicoATE::Ui {

class AttemptModel;
class DiagnosticModel;
class DeviceStatusModel;
class ExecutionViewModel;
class HistoryModel;
class MeasurementModel;
class UutStepModel;

class MainWindow final : public QMainWindow
{
public:
    explicit MainWindow(QWidget* parent = nullptr);
    ~MainWindow() override;

private:
    void buildActions();
    void buildLayout();
    void chooseSequence();
    void chooseStation();
    void updateCommandState();
    void updateDiagnostics();
    void updateReport();
    void displayReport(const PicoATE::Core::ExecutionReport& report);
    void applyRuntimeEvents(const QVector<PicoATE::Core::RuntimeEvent>& events);
    void updateStepDetails(const QModelIndex& index);
    void updateAttemptMeasurements(const QModelIndex& index);
    void selectInitialResult();
    void refreshHistory();
    void loadSelectedHistory();
    void exportSelectedHistory(bool csv);
    std::optional<ReportHistoryEntry> selectedHistoryEntry() const;

    ExecutionViewModel* m_viewModel = nullptr;
    DiagnosticModel* m_diagnosticModel = nullptr;
    DeviceStatusModel* m_deviceStatusModel = nullptr;
    HistoryModel* m_historyModel = nullptr;
    UutStepModel* m_uutStepModel = nullptr;
    AttemptModel* m_attemptModel = nullptr;
    MeasurementModel* m_measurementModel = nullptr;
    QAction* m_openSequenceAction = nullptr;
    QAction* m_openStationAction = nullptr;
    QAction* m_compileAction = nullptr;
    QAction* m_runAction = nullptr;
    QAction* m_stopAction = nullptr;
    QLineEdit* m_sequencePath = nullptr;
    QLineEdit* m_stationPath = nullptr;
    QSpinBox* m_uutCount = nullptr;
    QTreeView* m_resultView = nullptr;
    QTableView* m_attemptView = nullptr;
    QTableView* m_measurementView = nullptr;
    QTableView* m_diagnosticView = nullptr;
    QTableView* m_deviceStatusView = nullptr;
    QTableView* m_historyView = nullptr;
    QLineEdit* m_historyFilter = nullptr;
    QSortFilterProxyModel* m_historyProxy = nullptr;
    std::unique_ptr<ReportHistoryStore> m_historyStore;
    bool m_currentReportSaved = false;
};

} // namespace PicoATE::Ui
