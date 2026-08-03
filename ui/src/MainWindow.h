#pragma once

#include "PicoATE/Core/RuntimeEvent.h"
#include "ReportHistoryStore.h"
#include "SequenceDocument.h"
#include "UiExecutionTypes.h"

#include <QMainWindow>
#include <QElapsedTimer>
#include <QHash>
#include <QSet>
#include <QStringList>

#include <memory>

class QAction;
class QCloseEvent;
class QEvent;
class QLineEdit;
class QLabel;
class QMenu;
class QProgressBar;
class QSortFilterProxyModel;
class QSpinBox;
class QTableView;
class QTabWidget;
class QThread;
class QTimer;
class QTreeView;

namespace PicoATE::Ui {

class AttemptModel;
class DebugSnapshotModel;
class DiagnosticModel;
class DeviceStatusModel;
class ExecutionViewModel;
class FlowTargetSelector;
class HistoryModel;
class LoadingSpinner;
class MeasurementModel;
class OperatorPromptPresenter;
class PluginFunctionModel;
class RuntimeTimelineModel;
class RunArtifactWriter;
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
    void initializeAdminWorkspace();

protected:
    void closeEvent(QCloseEvent* event) override;
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    enum class HistoryExportFormat { Text, Csv, Xlsx };

    void buildActions();
    void buildLayout();
    void chooseSequence();
    void chooseStation();
    bool maybeSaveSequence();
    bool confirmAndSaveSequence();
    bool resolvePendingStepChanges();
    bool saveSequence();
    bool saveSequenceAs();
    bool maybeSaveStation();
    bool confirmAndSaveStation();
    bool resolvePendingStationChanges();
    bool resolvePendingStationDeviceChanges();
    bool commitPendingStationChanges();
    void discardPendingStationChanges();
    void saveActiveDocument();
    bool isStationWorkspaceActive() const;
    bool saveStation();
    bool saveStationAs();
    void editSequenceVariables();
    void addSequenceStep();
    void deleteSequenceStep();
    void copySequenceSteps();
    void pasteSequenceSteps();
    void wrapSelectedStepsInTestItem();
    void placeResourceRegionBoundary();
    bool chooseResourceRegionResources(const QString& regionId,
                                       QStringList* selectedResources);
    void setSelectedSequenceStepsEnabled(bool enabled);
    QVector<SequenceItemPath> selectedSequenceStepPaths() const;
    void moveSequenceStep(int offset);
    void applyUndoRedo(bool redo);
    void compileSequence();
    void runSequence();
    void runScannedUut(const QString& serialNumber);
    void beginAdminRunIteration(int iteration, int totalIterations);
    void showScanDialog();
    void scanPlugins(bool interactive = true);
    void loadPluginRegistry();
    void buildStartupOverlay();
    void showStartupOverlay(const QString& message);
    void hideStartupOverlay();
    void waitForPluginScan();
    void updatePluginDeviceBindings();
    void addStationDevice();
    void deleteStationDevice();
    void duplicateStationDevice();
    void fillPreviousStationDeviceSlot();
    void moveStationDevice(int offset);
    void applyStationUndoRedo(bool redo);
    void testSelectedStationDevice();
    void synchronizeSequenceSnapshot();
    void synchronizeStationSnapshot();
    void captureSequenceTreeViewState();
    void restoreSequenceTreeViewState();
    void updateSequenceEditor();
    void updateStationEditor();
    void normalizeStationLogicalIds();
    void applyStationLogicalIdMigrations();
    void refreshEditorDiagnostics();
    QVector<UiDiagnostic> stationPluginDiagnostics() const;
    QVector<UiDiagnostic> stationFlowDiagnostics() const;
    QVector<UiDiagnostic> stationEditorDiagnostics() const;
    void focusSequenceDiagnostic(const QModelIndex& index);
    void focusStationDiagnostic(const QModelIndex& index);
    void focusStationDiagnosticValue(const UiDiagnostic& diagnostic);
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
    void setRunTestInstructionPointer(const QString& nodePath);
    void displayReport(const PicoATE::Core::ExecutionReport& report);
    void applyRuntimeEvents(const QVector<PicoATE::Core::RuntimeEvent>& events);
    void selectRuntimeEvent(const PicoATE::Core::RuntimeEvent& event);
    void selectTimelineSequence(quint64 sequenceNumber);
    void selectTimelineEvent(const QModelIndex& index);
    void focusExecutionLogForResult(const QModelIndex& index);
    void focusDebugNode(const PicoATE::Core::RuntimeEvent& event);
    void updateStepDetails(const QModelIndex& index);
    void updateAttemptMeasurements(const QModelIndex& index);
    void selectInitialResult();
    void refreshHistory();
    void loadSelectedHistory();
    void exportSelectedHistory(HistoryExportFormat format);
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
    OperatorPromptPresenter* m_operatorPromptPresenter = nullptr;
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
    RuntimeTimelineModel* m_runtimeTimelineModel = nullptr;
    DebugSnapshotModel* m_debugSnapshotModel = nullptr;
    QAction* m_openSequenceAction = nullptr;
    QAction* m_saveSequenceAction = nullptr;
    QAction* m_saveSequenceAsAction = nullptr;
    QAction* m_undoAction = nullptr;
    QAction* m_redoAction = nullptr;
    QAction* m_addStepAction = nullptr;
    QAction* m_deleteStepAction = nullptr;
    QAction* m_copyStepAction = nullptr;
    QAction* m_pasteStepAction = nullptr;
    QAction* m_findFlowFieldAction = nullptr;
    QAction* m_sequenceVariablesAction = nullptr;
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
    QAction* m_fillPreviousDeviceSlotAction = nullptr;
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
    QThread* m_pluginScanThread = nullptr;
    QMenu* m_recentSequenceMenu = nullptr;
    QMenu* m_recentStationMenu = nullptr;
    QLineEdit* m_sequencePath = nullptr;
    QLineEdit* m_stationPath = nullptr;
    QSpinBox* m_uutCount = nullptr;
    QSpinBox* m_connectionTimeoutMs = nullptr;
    QTabWidget* m_workspaceTabs = nullptr;
    QWidget* m_flowEditorPage = nullptr;
    QWidget* m_stationEditorPage = nullptr;
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
    QLabel* m_adminAverageTime = nullptr;
    QProgressBar* m_adminProgress = nullptr;
    QTimer* m_adminElapsedTimer = nullptr;
    QWidget* m_startupOverlay = nullptr;
    LoadingSpinner* m_startupSpinner = nullptr;
    QLabel* m_startupStatusLabel = nullptr;
    QTreeView* m_sequenceTreeView = nullptr;
    QLineEdit* m_flowFieldSearch = nullptr;
    QTreeView* m_pluginFunctionView = nullptr;
    FlowTargetSelector* m_flowTargetSelector = nullptr;
    StepPropertyEditor* m_stepPropertyEditor = nullptr;
    QTableView* m_editorDiagnosticView = nullptr;
    QTreeView* m_stationDeviceView = nullptr;
    StationSettingsEditor* m_stationSettingsEditor = nullptr;
    StationPropertyEditor* m_stationPropertyEditor = nullptr;
    QTableView* m_stationDiagnosticView = nullptr;
    QTreeView* m_resultView = nullptr;
    QTableView* m_attemptView = nullptr;
    QTableView* m_measurementView = nullptr;
    QTableView* m_runtimeTimelineView = nullptr;
    QTableView* m_debugSnapshotView = nullptr;
    QTableView* m_diagnosticView = nullptr;
    QTableView* m_deviceStatusView = nullptr;
    QTableView* m_historyView = nullptr;
    QLineEdit* m_historyFilter = nullptr;
    QSortFilterProxyModel* m_historyProxy = nullptr;
    std::unique_ptr<ReportHistoryStore> m_historyStore;
    std::unique_ptr<RunArtifactWriter> m_runArtifactWriter;
    QStringList m_recentSequences;
    QStringList m_recentStations;
    QVector<QJsonObject> m_sequenceClipboard;
    SequenceItemPath m_selectedSequencePath;
    QString m_selectedSequenceNodePath;
    QVector<SequenceItemPath> m_expandedSequencePaths;
    int m_sequenceTreeScrollValue = 0;
    int m_selectedStationDeviceRow = -1;
    QHash<QString, QString> m_pendingStationLogicalIdMigrations;
    bool m_currentReportSaved = false;
    bool m_currentAdminRunCounted = false;
    bool m_shuttingDown = false;
    bool m_handlingSequenceSelection = false;
    bool m_handlingStationSelection = false;
    bool m_handlingWorkspaceTabChange = false;
    int m_previousWorkspaceTabIndex = -1;
    bool m_sequenceTreeStatePending = false;
    bool m_expandSequenceTreeOnNextUpdate = true;
    bool m_loadingSequenceFile = false;
    bool m_pluginScanInProgress = false;
    bool m_adminWorkspaceInitialized = false;
    int m_adminTotalNodes = 0;
    int m_adminPassedUnits = 0;
    int m_adminFailedUnits = 0;
    qint64 m_adminTotalCompletedDurationMs = 0;
    QSet<PicoATE::Core::NodeId> m_adminTerminalNodes;
    QElapsedTimer m_adminElapsed;
    PicoATE::Core::ExecutionReport m_adminPreviewReport;
};

} // namespace PicoATE::Ui
