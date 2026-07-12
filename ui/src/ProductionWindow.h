#pragma once

#include "LoginDialog.h"
#include "PicoATE/Core/RuntimeEvent.h"
#include "UiExecutionTypes.h"

#include <QElapsedTimer>
#include <QHash>
#include <QMainWindow>
#include <QSet>

class QAction;
class QLabel;
class QProgressBar;
class QTableView;
class QTimer;
class QTreeView;

namespace PicoATE::Ui {

class ExecutionViewModel;
class RuntimeLogModel;
class ScanDialog;
class UutStepModel;

class ProductionWindow final : public QMainWindow
{
    Q_OBJECT

public:
    explicit ProductionWindow(StartupSelection selection,
                              QWidget* parent = nullptr);
    ~ProductionWindow() override;

protected:
    void closeEvent(QCloseEvent* event) override;

private:
    void buildUi();
    void updateCommands();
    void updateState(UiRunState state);
    void updateCompileSummary();
    void updateReport();
    void applyRuntimeEvents(const QVector<PicoATE::Core::RuntimeEvent>& events);
    void beginRun(const QString& serialNumber);
    void beginManualRun();
    void resetPreviewForUut(const QString& uutId);
    void showScanDialogWhenReady();
    void updateElapsedTime();
    void updateProgress();
    void updateYieldStatistics();

    StartupSelection m_selection;
    ExecutionViewModel* m_viewModel = nullptr;
    UutStepModel* m_resultModel = nullptr;
    RuntimeLogModel* m_logModel = nullptr;
    ScanDialog* m_scanDialog = nullptr;
    QAction* m_startAction = nullptr;
    QAction* m_pauseAction = nullptr;
    QAction* m_resumeAction = nullptr;
    QAction* m_stopAction = nullptr;
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
    QLabel* m_yieldLabel = nullptr;
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
    QString m_activeUutId;
    bool m_currentRunCounted = false;
};

} // namespace PicoATE::Ui
