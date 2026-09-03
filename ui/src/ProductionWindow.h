#pragma once

#include "LoginDialog.h"
#include "PicoATE/Core/RuntimeEvent.h"
#include "UiExecutionTypes.h"

#include <QElapsedTimer>
#include <QHash>
#include <QMainWindow>
#include <QSet>
#include <QStringList>

#include <memory>

class QAction;
class QButtonGroup;
class QEvent;
class QHBoxLayout;
class QLabel;
class QProgressBar;
class QPushButton;
class QResizeEvent;
class QSpinBox;
class QStackedWidget;
class QTableView;
class QTimer;
class QTreeView;
class QWidget;

namespace PicoATE::Ui {

class ExecutionViewModel;
class MultiUutOverviewWidget;
class OperatorPromptPresenter;
class ProductionOverviewSummaryWidget;
class RuntimeTimelineModel;
class RunArtifactWriter;
class ScanDialog;
class UutOverviewModel;
class UutRuntimeTimelineProxyModel;
class UutStepModel;
class YieldDonutWidget;

class ProductionWindow final : public QMainWindow
{
    Q_OBJECT

public:
    explicit ProductionWindow(StartupSelection selection,
                              QWidget* parent = nullptr);
    ~ProductionWindow() override;

protected:
    void closeEvent(QCloseEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    void buildUi();
    void applyResponsiveLayout(bool force = false);
    void updateCommands();
    void updateState(UiRunState state);
    void updateCompileSummary();
    void updateReport();
    void applyRuntimeEvents(const QVector<PicoATE::Core::RuntimeEvent>& events);
    void focusExecutionLogForResult(const QModelIndex& index);
    void beginRun(const QString& serialNumber);
    void beginRunBatch(const QStringList& serialNumbers);
    void beginAutoRoutedRun(const QString& serialNumber);
    void beginAutoRoutedRunBatch(const QStringList& serialNumbers);
    void startResolvedRun();
    void configureUutSlots();
    int configuredUutCount() const;
    void synchronizeUutSlotCount(int slotCount);
    void updateUutSlotAction();
    void showRoutingError(const QString& message);
    void updateStationSummary();
    void beginManualRun();
    void openFieldDeviceConfiguration();
    void openProductRoutingConfiguration();
    void beginRunIteration(int iteration, int totalIterations);
    void resetPreviewForUuts(const QVector<RunRequest::UutInput>& inputs,
                             bool preferOverview);
    QVector<RunRequest::UutInput> configuredPreviewUuts() const;
    void rebuildUutNavigation();
    void showUutOverview();
    void showUutDetails(const PicoATE::Core::UutId& uutId);
    void showScanDialogWhenReady();
    void updateElapsedTime();
    void updateProgress();
    void updateYieldStatistics();
    void updateOverviewSummary();

    StartupSelection m_selection;
    ExecutionViewModel* m_viewModel = nullptr;
    OperatorPromptPresenter* m_operatorPromptPresenter = nullptr;
    UutStepModel* m_resultModel = nullptr;
    UutOverviewModel* m_overviewModel = nullptr;
    RuntimeTimelineModel* m_logModel = nullptr;
    UutRuntimeTimelineProxyModel* m_logProxy = nullptr;
    std::unique_ptr<RunArtifactWriter> m_runArtifactWriter;
    ScanDialog* m_scanDialog = nullptr;
    QAction* m_startAction = nullptr;
    QAction* m_pauseAction = nullptr;
    QAction* m_resumeAction = nullptr;
    QAction* m_stopAction = nullptr;
    QSpinBox* m_uutCount = nullptr;
    QAction* m_uutSlotsAction = nullptr;
    QAction* m_fieldDeviceAction = nullptr;
    QAction* m_productRoutingAction = nullptr;
    QLabel* m_sequenceLabel = nullptr;
    QLabel* m_serialLabel = nullptr;
    QLabel* m_stationLabel = nullptr;
    QLabel* m_modelLabel = nullptr;
    QLabel* m_customerIdLabel = nullptr;
    QLabel* m_orderLabel = nullptr;
    QLabel* m_testerLabel = nullptr;
    QLabel* m_jigLabel = nullptr;
    QLabel* m_overallResult = nullptr;
    QLabel* m_elapsedLabel = nullptr;
    QLabel* m_passCountLabel = nullptr;
    QLabel* m_failCountLabel = nullptr;
    QLabel* m_totalCountLabel = nullptr;
    QLabel* m_averageTimeLabel = nullptr;
    YieldDonutWidget* m_yieldChart = nullptr;
    QWidget* m_runSidebar = nullptr;
    QWidget* m_runNavigation = nullptr;
    QPushButton* m_overviewButton = nullptr;
    QButtonGroup* m_uutNavigationGroup = nullptr;
    QHBoxLayout* m_uutNavigationLayout = nullptr;
    QStackedWidget* m_runStack = nullptr;
    QWidget* m_overviewPage = nullptr;
    QWidget* m_detailPage = nullptr;
    ProductionOverviewSummaryWidget* m_overviewSummary = nullptr;
    MultiUutOverviewWidget* m_uutOverview = nullptr;
    QTreeView* m_resultView = nullptr;
    QTableView* m_logView = nullptr;
    QProgressBar* m_progress = nullptr;
    QWidget* m_progressPanel = nullptr;
    QTimer* m_elapsedTimer = nullptr;
    PicoATE::Core::ExecutionReport m_previewReport;
    QSet<PicoATE::Core::NodeId> m_terminalNodes;
    QHash<PicoATE::Core::NodeId, PicoATE::Core::ActivationState> m_nodeStates;
    QElapsedTimer m_elapsed;
    int m_totalNodes = 0;
    int m_passedUnits = 0;
    int m_failedUnits = 0;
    int m_lastAutoFollowLine = 0;
    PicoATE::Core::UutId m_lastAutoFollowUutId;
    PicoATE::Core::NodeId m_lastAutoFollowNodeId;
    qint64 m_totalCompletedDurationMs = 0;
    QString m_activeUutId;
    QString m_selectedUutId;
    QString m_activeSerialNumber;
    QVector<RunRequest::UutInput> m_runUutInputs;
    QStringList m_pendingSerialNumbers;
    QVector<bool> m_uutSlotEnabled;
    QVector<bool> m_pendingUutSlotEnabled;
    bool m_runPreparationPending = false;
    bool m_currentRunCounted = false;
    bool m_stopRequested = false;
    bool m_fieldDeviceDialogOpen = false;
    int m_responsiveLayoutMode = -1;
};

std::unique_ptr<ProductionWindow> createProductionWindow(
    StartupSelection selection);

} // namespace PicoATE::Ui
