#pragma once

#include "LoginDialog.h"
#include "PicoATE/Core/RuntimeEvent.h"
#include "UiExecutionTypes.h"

#include <QElapsedTimer>
#include <QHash>
#include <QMainWindow>
#include <QSet>

#include <memory>

class QAction;
class QEvent;
class QLabel;
class QProgressBar;
class QTableView;
class QTimer;
class QTreeView;

namespace PicoATE::Ui {

class ExecutionViewModel;
class OperatorPromptPresenter;
class RuntimeTimelineModel;
class RunArtifactWriter;
class ScanDialog;
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
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    void buildUi();
    void updateCommands();
    void updateState(UiRunState state);
    void updateCompileSummary();
    void updateReport();
    void applyRuntimeEvents(const QVector<PicoATE::Core::RuntimeEvent>& events);
    void focusExecutionLogForResult(const QModelIndex& index);
    void beginRun(const QString& serialNumber);
    void beginAutoRoutedRun(const QString& serialNumber);
    void startResolvedRun();
    void showRoutingError(const QString& message);
    void updateStationSummary();
    void beginManualRun();
    void openFieldDeviceConfiguration();
    void openProductRoutingConfiguration();
    void beginRunIteration(int iteration, int totalIterations);
    void resetPreviewForUut(const QString& uutId);
    void showScanDialogWhenReady();
    void updateElapsedTime();
    void updateProgress();
    void updateYieldStatistics();

    StartupSelection m_selection;
    ExecutionViewModel* m_viewModel = nullptr;
    OperatorPromptPresenter* m_operatorPromptPresenter = nullptr;
    UutStepModel* m_resultModel = nullptr;
    RuntimeTimelineModel* m_logModel = nullptr;
    std::unique_ptr<RunArtifactWriter> m_runArtifactWriter;
    ScanDialog* m_scanDialog = nullptr;
    QAction* m_startAction = nullptr;
    QAction* m_pauseAction = nullptr;
    QAction* m_resumeAction = nullptr;
    QAction* m_stopAction = nullptr;
    QAction* m_fieldDeviceAction = nullptr;
    QAction* m_productRoutingAction = nullptr;
    QLabel* m_sequenceLabel = nullptr;
    QLabel* m_serialLabel = nullptr;
    QLabel* m_stationLabel = nullptr;
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
    QTreeView* m_resultView = nullptr;
    QTableView* m_logView = nullptr;
    QProgressBar* m_progress = nullptr;
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
    QString m_pendingSerialNumber;
    bool m_currentRunCounted = false;
};

std::unique_ptr<ProductionWindow> createProductionWindow(
    StartupSelection selection);

} // namespace PicoATE::Ui
