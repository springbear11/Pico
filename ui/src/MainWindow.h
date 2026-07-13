#pragma once

#include "PicoATE/Core/RuntimeEvent.h"
#include "ReportHistoryStore.h"
#include "SequenceDocument.h"
#include "UiExecutionTypes.h"

#include <QMainWindow>
#include <QElapsedTimer>
#include <QSet>
#include <QStringList>

#include <memory>

class QAction;
class QCloseEvent;
class QLineEdit;
class QLabel;
class QMenu;
class QProgressBar;
class QSortFilterProxyModel;
class QSpinBox;
class QTableView;
class QTabWidget;
class QTimer;
class QTreeView;

namespace PicoATE::Ui {

class AttemptModel;
class DebugSnapshotModel;
class DiagnosticModel;
class DeviceStatusModel;
class ExecutionViewModel;
class HistoryModel;
class MeasurementModel;
class PluginFunctionModel;
class RuntimeLogModel;
class RuntimeTimelineModel;
class SequenceDocument;
class SequenceTreeModel;
class ScanDialog;
class StationDeviceModel;
class StationDocument;
class StationPropertyEditor;
class StationSettingsEditor;
class StepPropertyEditor;
class UutStepModel;

class MainWindow final : public QMainWindow
{
public:
    explicit MainWindow(QWidget* parent = nullptr);
    ~MainWindow() override;

    bool openSequenceFile(const QString& filePath);
    bool openStationFile(const QString& filePath);
    void showRunPage();

protected:
    void closeEvent(QCloseEvent* event) override;

private:
    void buildActions();
    void buildLayout();
    void chooseSequence();
    void chooseStation();
    bool maybeSaveSequence();
    bool saveSequence();
    bool saveSequenceAs();
    bool maybeSaveStation();
    bool saveStation();
    bool saveStationAs();
    void addSequenceStep();
    void deleteSequenceStep();
    void duplicateSequenceStep();
    void wrapSelectedStepsInTestItem();
    void setSelectedSequenceStepsEnabled(bool enabled);
    QVector<SequenceItemPath> selectedSequenceStepPaths() const;
    void moveSequenceStep(int offset);
    void applyUndoRedo(bool redo);
    void runSequence();
    void runScannedUut(const QString& serialNumber);
    void showScanDialog();
    void scanPlugins();
    void loadPluginRegistry();
    void updatePluginDeviceBindings();
    void addStationDevice();
    void deleteStationDevice();
    void duplicateStationDevice();
    void moveStationDevice(int offset);
    void applyStationUndoRedo(bool redo);
    void testSelectedStationDevice();
    void synchronizeSequenceSnapshot();
    void synchronizeStationSnapshot();
    void updateSequenceEditor();
    void updateStationEditor();
    QVector<UiDiagnostic> stationPluginDiagnostics() const;
    void focusSequenceDiagnostic(const QModelIndex& index);
    void focusStationDiagnostic(const QModelIndex& index);
    void updateWindowTitle();
    void updateCommandState();
    void updateDiagnostics();
    void updateReport();
    void updateCompilePreview();
    void updateAdminRunState(UiRunState state);
    void updateAdminStationSummary();
    void updateAdminProgress();
    void updateAdminYield();
    void updateAdminElapsed();
    void updateDebugSnapshot();
    void displayReport(const PicoATE::Core::ExecutionReport& report);
    void applyRuntimeEvents(const QVector<PicoATE::Core::RuntimeEvent>& events);
    void selectRuntimeEvent(const PicoATE::Core::RuntimeEvent& event);
    void selectTimelineSequence(quint64 sequenceNumber);
    void selectTimelineEvent(const QModelIndex& index);
    void focusDebugNode(const PicoATE::Core::RuntimeEvent& event);
    void updateStepDetails(const QModelIndex& index);
    void updateAttemptMeasurements(const QModelIndex& index);
    void selectInitialResult();
    void refreshHistory();
    void loadSelectedHistory();
    void exportSelectedHistory(bool csv);
    void restoreUiSettings();
    void saveUiSettings() const;
    void resetUiLayout();
    void addRecentSequence(const QString& filePath);
    void addRecentStation(const QString& filePath);
    void refreshRecentFileMenus();
    void openRecentSequence(const QString& filePath);
    void openRecentStation(const QString& filePath);
    void beginShutdown();
    std::optional<ReportHistoryEntry> selectedHistoryEntry() const;

    ExecutionViewModel* m_viewModel = nullptr;
    SequenceDocument* m_sequenceDocument = nullptr;
    SequenceTreeModel* m_sequenceTreeModel = nullptr;
    StationDocument* m_stationDocument = nullptr;
    StationDeviceModel* m_stationDeviceModel = nullptr;
    DiagnosticModel* m_editorDiagnosticModel = nullptr;
    DiagnosticModel* m_stationDiagnosticModel = nullptr;
    DiagnosticModel* m_diagnosticModel = nullptr;
    DeviceStatusModel* m_deviceStatusModel = nullptr;
    HistoryModel* m_historyModel = nullptr;
    UutStepModel* m_uutStepModel = nullptr;
    AttemptModel* m_attemptModel = nullptr;
    MeasurementModel* m_measurementModel = nullptr;
    PluginFunctionModel* m_pluginFunctionModel = nullptr;
    RuntimeLogModel* m_runtimeLogModel = nullptr;
    RuntimeTimelineModel* m_runtimeTimelineModel = nullptr;
    DebugSnapshotModel* m_debugSnapshotModel = nullptr;
    QAction* m_openSequenceAction = nullptr;
    QAction* m_saveSequenceAction = nullptr;
    QAction* m_saveSequenceAsAction = nullptr;
    QAction* m_undoAction = nullptr;
    QAction* m_redoAction = nullptr;
    QAction* m_addStepAction = nullptr;
    QAction* m_deleteStepAction = nullptr;
    QAction* m_duplicateStepAction = nullptr;
    QAction* m_wrapTestItemAction = nullptr;
    QAction* m_enableStepsAction = nullptr;
    QAction* m_disableStepsAction = nullptr;
    QAction* m_moveStepUpAction = nullptr;
    QAction* m_moveStepDownAction = nullptr;
    QAction* m_openStationAction = nullptr;
    QAction* m_saveStationAction = nullptr;
    QAction* m_saveStationAsAction = nullptr;
    QAction* m_stationUndoAction = nullptr;
    QAction* m_stationRedoAction = nullptr;
    QAction* m_addDeviceAction = nullptr;
    QAction* m_deleteDeviceAction = nullptr;
    QAction* m_duplicateDeviceAction = nullptr;
    QAction* m_moveDeviceUpAction = nullptr;
    QAction* m_moveDeviceDownAction = nullptr;
    QAction* m_testDeviceConnectionAction = nullptr;
    QAction* m_compileAction = nullptr;
    QAction* m_runAction = nullptr;
    QAction* m_pauseAction = nullptr;
    QAction* m_resumeAction = nullptr;
    QAction* m_stepIntoAction = nullptr;
    QAction* m_stepOverAction = nullptr;
    QAction* m_stopAction = nullptr;
    QAction* m_scanAction = nullptr;
    QAction* m_scanPluginsAction = nullptr;
    QAction* m_resetLayoutAction = nullptr;
    QMenu* m_recentSequenceMenu = nullptr;
    QMenu* m_recentStationMenu = nullptr;
    QLineEdit* m_sequencePath = nullptr;
    QLineEdit* m_stationPath = nullptr;
    QSpinBox* m_uutCount = nullptr;
    QSpinBox* m_connectionTimeoutMs = nullptr;
    QTabWidget* m_workspaceTabs = nullptr;
    ScanDialog* m_scanDialog = nullptr;
    QLabel* m_adminSequenceLabel = nullptr;
    QLabel* m_adminSerialLabel = nullptr;
    QLabel* m_adminStationLabel = nullptr;
    QLabel* m_adminOrderLabel = nullptr;
    QLabel* m_adminTesterLabel = nullptr;
    QLabel* m_adminJigLabel = nullptr;
    QLabel* m_adminOverallResult = nullptr;
    QLabel* m_adminElapsedLabel = nullptr;
    QLabel* m_adminPassCount = nullptr;
    QLabel* m_adminFailCount = nullptr;
    QLabel* m_adminTotalCount = nullptr;
    QLabel* m_adminYield = nullptr;
    QProgressBar* m_adminProgress = nullptr;
    QTimer* m_adminElapsedTimer = nullptr;
    QTreeView* m_sequenceTreeView = nullptr;
    QTreeView* m_pluginFunctionView = nullptr;
    StepPropertyEditor* m_stepPropertyEditor = nullptr;
    QTableView* m_editorDiagnosticView = nullptr;
    QTableView* m_stationDeviceView = nullptr;
    StationSettingsEditor* m_stationSettingsEditor = nullptr;
    StationPropertyEditor* m_stationPropertyEditor = nullptr;
    QTableView* m_stationDiagnosticView = nullptr;
    QTreeView* m_resultView = nullptr;
    QTableView* m_attemptView = nullptr;
    QTableView* m_measurementView = nullptr;
    QTableView* m_runtimeTimelineView = nullptr;
    QTableView* m_runtimeLogView = nullptr;
    QTableView* m_debugSnapshotView = nullptr;
    QTableView* m_diagnosticView = nullptr;
    QTableView* m_deviceStatusView = nullptr;
    QTableView* m_historyView = nullptr;
    QLineEdit* m_historyFilter = nullptr;
    QSortFilterProxyModel* m_historyProxy = nullptr;
    std::unique_ptr<ReportHistoryStore> m_historyStore;
    QStringList m_recentSequences;
    QStringList m_recentStations;
    SequenceItemPath m_selectedSequencePath;
    int m_selectedStationDeviceRow = -1;
    bool m_currentReportSaved = false;
    bool m_currentAdminRunCounted = false;
    bool m_shuttingDown = false;
    int m_adminTotalNodes = 0;
    int m_adminPassedUnits = 0;
    int m_adminFailedUnits = 0;
    QSet<PicoATE::Core::NodeId> m_adminTerminalNodes;
    QElapsedTimer m_adminElapsed;
    PicoATE::Core::ExecutionReport m_adminPreviewReport;
};

} // namespace PicoATE::Ui
