#include "ProductionWindow.h"

#include "ProportionalHeaderView.h"

#include "ExecutionViewModel.h"
#include "FieldDeviceDialog.h"
#include "OperatorPromptPresenter.h"
#include "PicoATE/Core/StationConfig.h"
#include "RunnerModels.h"
#include "RunArtifactWriter.h"
#include "ScanDialog.h"

#include <QAction>
#include <QAbstractItemView>
#include <QCloseEvent>
#include <QDateTime>
#include <QFileInfo>
#include <QFile>
#include <QFont>
#include <QFormLayout>
#include <QFrame>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QLabel>
#include <QJsonDocument>
#include <QMessageBox>
#include <QProgressBar>
#include <QSizePolicy>
#include <QSplitter>
#include <QStatusBar>
#include <QStyle>
#include <QTableView>
#include <QTimer>
#include <QToolBar>
#include <QTreeView>
#include <QVBoxLayout>
#include <QWidget>

#include <utility>
#include <initializer_list>
#include <algorithm>

namespace PicoATE::Ui {

namespace {

bool isTerminal(PicoATE::Core::ActivationState state)
{
    return PicoATE::Core::isTerminalActivation(state);
}

QString productionStateText(UiRunState state)
{
    switch (state) {
    case UiRunState::Compiling: return QStringLiteral("COMPILING");
    case UiRunState::Ready: return QStringLiteral("READY");
    case UiRunState::Starting:
    case UiRunState::Running: return QStringLiteral("RUNNING");
    case UiRunState::Pausing: return QStringLiteral("PAUSING");
    case UiRunState::Paused: return QStringLiteral("PAUSED");
    case UiRunState::Stopping: return QStringLiteral("STOPPING");
    case UiRunState::Completed: return QStringLiteral("PASS");
    case UiRunState::CompileFailed:
    case UiRunState::Failed: return QStringLiteral("FAIL");
    default: return QStringLiteral("WAITING");
    }
}

QString productionStateStyle(UiRunState state)
{
    switch (state) {
    case UiRunState::Starting:
    case UiRunState::Running:
    case UiRunState::Pausing:
    case UiRunState::Paused:
    case UiRunState::Stopping:
        return QStringLiteral("background:#f7d154;color:#242424;border:1px solid #d5ad2f;border-radius:4px;padding:12px;");
    case UiRunState::Completed:
        return QStringLiteral("background:#8fd14f;color:#17320b;border:1px solid #69aa31;border-radius:4px;padding:12px;");
    case UiRunState::CompileFailed:
    case UiRunState::Failed:
        return QStringLiteral("background:#e85d5d;color:white;border:1px solid #c53d3d;border-radius:4px;padding:12px;");
    default:
        return QStringLiteral("background:#e6e8eb;color:#242424;border:1px solid #c7cdd2;border-radius:4px;padding:12px;");
    }
}

QString metadataValue(const QVariantMap& metadata,
                      std::initializer_list<const char*> keys)
{
    for (const auto* key : keys) {
        const auto value = metadata.value(QString::fromLatin1(key)).toString().trimmed();
        if (!value.isEmpty()) {
            return value;
        }
    }
    return QStringLiteral("--");
}

void collectStepStates(
    const PicoATE::Core::StepReport& step,
    QHash<PicoATE::Core::NodeId, PicoATE::Core::ActivationState>& states)
{
    const auto key = step.nodePath.isEmpty() ? step.stepId : step.nodePath;
    if (!key.isEmpty()) {
        states.insert(key, step.state);
    }
    for (const auto& child : step.children) {
        collectStepStates(child, states);
    }
}

QString compactDuration(qint64 milliseconds)
{
    milliseconds = qMax<qint64>(0, milliseconds);
    return QStringLiteral("%1:%2.%3")
        .arg(milliseconds / 60000, 2, 10, QLatin1Char('0'))
        .arg(milliseconds / 1000 % 60, 2, 10, QLatin1Char('0'))
        .arg(milliseconds % 1000, 3, 10, QLatin1Char('0'));
}

} // namespace

ProductionWindow::ProductionWindow(StartupSelection selection, QWidget* parent)
    : QMainWindow(parent)
    , m_selection(std::move(selection))
{
    setObjectName(QStringLiteral("productionWindow"));
    setWindowTitle(tr("PicoATE TEST"));
    setMinimumSize(960, 620);
    m_viewModel = new ExecutionViewModel(this);
    m_operatorPromptPresenter = new OperatorPromptPresenter(m_viewModel, this, this);
    m_resultModel = new UutStepModel(this);
    m_resultModel->setSingleUutPhaseLayout(true);
    m_logModel = new RuntimeTimelineModel(this);
    m_runArtifactWriter = std::make_unique<RunArtifactWriter>();
    m_scanDialog = new ScanDialog(this);
    m_fieldDeviceDialog = new FieldDeviceDialog(m_selection.stationPath, this);
    m_scanDialog->setValidationRules(m_selection.snValidationRules);
    buildUi();

    connect(m_viewModel, &ExecutionViewModel::stateChanged,
            this, &ProductionWindow::updateState);
    connect(m_viewModel, &ExecutionViewModel::commandAvailabilityChanged,
            this, &ProductionWindow::updateCommands);
    connect(m_viewModel, &ExecutionViewModel::compileSummaryChanged,
            this, &ProductionWindow::updateCompileSummary);
    connect(m_viewModel, &ExecutionViewModel::reportChanged,
            this, &ProductionWindow::updateReport);
    connect(m_viewModel, &ExecutionViewModel::runIterationStarted,
            this, &ProductionWindow::beginRunIteration);
    connect(m_viewModel, &ExecutionViewModel::runtimeEventsReady,
            this, &ProductionWindow::applyRuntimeEvents);
    connect(m_scanDialog, &ScanDialog::barcodeAccepted,
            this, &ProductionWindow::beginRun);
    connect(m_fieldDeviceDialog, &FieldDeviceDialog::stationSaved,
            this, [this] {
                m_stationSnapshotReady = false;
                m_resolvingStation = true;
                updateCommands();
                m_fieldDeviceDialog->resolveEffectiveStation();
            });
    connect(m_fieldDeviceDialog, &FieldDeviceDialog::effectiveStationReady,
            this, &ProductionWindow::applyEffectiveStation);

    m_viewModel->setSequencePath(m_selection.sequencePath);
    m_resolvingStation = true;
    m_fieldDeviceDialog->resolveEffectiveStation();
    updateCommands();
}

ProductionWindow::~ProductionWindow()
{
    m_runArtifactWriter->abandon();
    m_scanDialog->hide();
    m_operatorPromptPresenter->closeAll();
    m_viewModel->shutdown();
}

void ProductionWindow::closeEvent(QCloseEvent* event)
{
    m_runArtifactWriter->abandon();
    m_scanDialog->hide();
    m_operatorPromptPresenter->closeAll();
    m_viewModel->shutdown();
    event->accept();
}

void ProductionWindow::buildUi()
{
    auto* central = new QWidget(this);
    central->setObjectName(QStringLiteral("productionCentral"));
    auto* layout = new QVBoxLayout(central);
    layout->setContentsMargins(16, 14, 16, 12);
    layout->setSpacing(10);

    auto* sequence = new QLabel(QFileInfo(m_selection.sequencePath).fileName(), central);
    sequence->setObjectName(QStringLiteral("productionSequenceLabel"));
    sequence->setAlignment(Qt::AlignCenter);
    sequence->setMinimumHeight(48);
    sequence->setMaximumHeight(54);
    layout->addWidget(sequence);

    auto* toolbar = new QToolBar(tr("TEST Controls"), central);
    toolbar->setObjectName(QStringLiteral("productionToolbar"));
    toolbar->setMovable(false);
    toolbar->setFloatable(false);
    toolbar->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    toolbar->setIconSize(QSize(22, 22));
    toolbar->setFixedHeight(50);
    toolbar->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    m_startAction = toolbar->addAction(
        style()->standardIcon(QStyle::SP_MediaPlay), tr("Start"));
    m_startAction->setObjectName(QStringLiteral("productionStartAction"));
    m_pauseAction = toolbar->addAction(
        style()->standardIcon(QStyle::SP_MediaPause), tr("Pause"));
    m_pauseAction->setObjectName(QStringLiteral("productionPauseAction"));
    m_resumeAction = toolbar->addAction(
        style()->standardIcon(QStyle::SP_MediaPlay), tr("Resume"));
    m_resumeAction->setObjectName(QStringLiteral("productionResumeAction"));
    m_stopAction = toolbar->addAction(
        style()->standardIcon(QStyle::SP_MediaStop), tr("Stop"));
    m_stopAction->setObjectName(QStringLiteral("productionStopAction"));
    toolbar->addSeparator();
    m_fieldDeviceAction = toolbar->addAction(
        style()->standardIcon(QStyle::SP_DriveNetIcon), tr("Devices"));
    m_fieldDeviceAction->setObjectName(QStringLiteral("productionFieldDeviceAction"));
    connect(m_startAction, &QAction::triggered,
            this, &ProductionWindow::beginManualRun);
    connect(m_pauseAction, &QAction::triggered,
            m_viewModel, &ExecutionViewModel::pause);
    connect(m_resumeAction, &QAction::triggered,
            m_viewModel, &ExecutionViewModel::resume);
    connect(m_stopAction, &QAction::triggered,
            this, [this] { m_viewModel->stop(); });
    connect(m_fieldDeviceAction, &QAction::triggered,
            this, &ProductionWindow::openFieldDeviceConfiguration);
    layout->addWidget(toolbar);

    auto* contentSplitter = new QSplitter(Qt::Horizontal, central);
    contentSplitter->setObjectName(QStringLiteral("productionContentSplitter"));
    contentSplitter->setChildrenCollapsible(false);

    auto* sidebar = new QFrame(contentSplitter);
    sidebar->setObjectName(QStringLiteral("productionSidebar"));
    sidebar->setMinimumWidth(215);
    sidebar->setMaximumWidth(270);
    auto* sidebarLayout = new QVBoxLayout(sidebar);
    sidebarLayout->setContentsMargins(18, 18, 18, 18);
    sidebarLayout->setSpacing(14);

    auto* unitTitle = new QLabel(tr("UNIT UNDER TEST"), sidebar);
    unitTitle->setObjectName(QStringLiteral("productionSectionTitle"));
    sidebarLayout->addWidget(unitTitle);

    const auto stationResult = PicoATE::Core::loadStationConfigFile(m_selection.stationPath);
    const auto stationId = stationResult.config.stationId.isEmpty()
        ? QFileInfo(m_selection.stationPath).completeBaseName()
        : stationResult.config.stationId;
    const auto& metadata = stationResult.config.metadata;

    auto* details = new QFormLayout;
    details->setContentsMargins(0, 0, 0, 0);
    details->setHorizontalSpacing(12);
    details->setVerticalSpacing(12);
    details->setLabelAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    m_serialLabel = new QLabel(tr("--"), sidebar);
    m_serialLabel->setObjectName(QStringLiteral("productionSerialLabel"));
    m_stationLabel = new QLabel(stationId, sidebar);
    m_stationLabel->setObjectName(QStringLiteral("productionStationLabel"));
    m_orderLabel = new QLabel(metadataValue(metadata, {"order", "orderNumber"}), sidebar);
    m_orderLabel->setObjectName(QStringLiteral("productionOrderLabel"));
    m_testerLabel = new QLabel(metadataValue(metadata, {"tester", "operator"}), sidebar);
    m_testerLabel->setObjectName(QStringLiteral("productionTesterLabel"));
    m_jigLabel = new QLabel(metadataValue(metadata, {"jigNo", "fixtureId", "fixture"}), sidebar);
    m_jigLabel->setObjectName(QStringLiteral("productionJigLabel"));
    for (auto* value : {m_serialLabel, m_stationLabel, m_orderLabel, m_testerLabel, m_jigLabel}) {
        value->setTextInteractionFlags(Qt::TextSelectableByMouse);
        value->setWordWrap(true);
    }
    details->addRow(tr("SN"), m_serialLabel);
    details->addRow(tr("Station"), m_stationLabel);
    details->addRow(tr("Order"), m_orderLabel);
    details->addRow(tr("Tester"), m_testerLabel);
    details->addRow(tr("Jig No."), m_jigLabel);
    sidebarLayout->addLayout(details);
    sidebarLayout->addStretch(1);

    auto* resultCaption = new QLabel(tr("OVERALL RESULT"), sidebar);
    resultCaption->setObjectName(QStringLiteral("productionMetricCaption"));
    resultCaption->setAlignment(Qt::AlignCenter);
    sidebarLayout->addWidget(resultCaption);
    m_overallResult = new QLabel(tr("WAITING"), sidebar);
    m_overallResult->setObjectName(QStringLiteral("productionOverallResult"));
    m_overallResult->setAlignment(Qt::AlignCenter);
    m_overallResult->setMinimumHeight(112);
    auto resultFont = m_overallResult->font();
    resultFont.setBold(true);
    resultFont.setPointSize(resultFont.pointSize() + 13);
    m_overallResult->setFont(resultFont);
    sidebarLayout->addWidget(m_overallResult);

    auto* elapsedCaption = new QLabel(tr("ELAPSED TIME"), sidebar);
    elapsedCaption->setObjectName(QStringLiteral("productionMetricCaption"));
    elapsedCaption->setAlignment(Qt::AlignCenter);
    sidebarLayout->addWidget(elapsedCaption);
    m_elapsedLabel = new QLabel(tr("00:00.000"), sidebar);
    m_elapsedLabel->setObjectName(QStringLiteral("productionElapsedLabel"));
    m_elapsedLabel->setAlignment(Qt::AlignCenter);
    sidebarLayout->addWidget(m_elapsedLabel);

    auto* rightSplitter = new QSplitter(Qt::Vertical, contentSplitter);
    rightSplitter->setObjectName(QStringLiteral("productionDataSplitter"));
    rightSplitter->setChildrenCollapsible(false);

    auto* resultsArea = new QWidget(rightSplitter);
    auto* resultsLayout = new QVBoxLayout(resultsArea);
    resultsLayout->setContentsMargins(0, 0, 0, 0);
    resultsLayout->setSpacing(6);
    auto* resultsTitle = new QLabel(tr("TEST RESULTS"), resultsArea);
    resultsTitle->setObjectName(QStringLiteral("productionSectionTitle"));
    resultsLayout->addWidget(resultsTitle);
    m_resultView = new QTreeView(resultsArea);
    m_resultView->setObjectName(QStringLiteral("productionResultView"));
    m_resultView->setModel(m_resultModel);
    m_resultView->setAlternatingRowColors(true);
    m_resultView->setUniformRowHeights(true);
    m_resultView->setIndentation(22);
    auto resultTreeFont = m_resultView->font();
    resultTreeFont.setFamily(QStringLiteral("Microsoft YaHei UI"));
    if (resultTreeFont.pointSizeF() > 0.0) {
        resultTreeFont.setPointSizeF(resultTreeFont.pointSizeF() + 0.5);
    }
    resultTreeFont.setWeight(QFont::Medium);
    m_resultView->setFont(resultTreeFont);
    auto* resultHeader = new ProportionalHeaderView(m_resultView);
    m_resultView->setHeader(resultHeader);
    resultHeader->setSectionWeights({2, 1, 1, 1, 1, 1, 1, 1, 1, 1});
    m_resultView->setColumnHidden(UutStepModel::AttemptsColumn, true);
    m_resultView->setColumnHidden(UutStepModel::LoopColumn, true);
    m_resultView->setColumnHidden(UutStepModel::StateColumn, true);
    m_resultView->setColumnHidden(UutStepModel::BreakpointVisualColumn, true);
    resultsLayout->addWidget(m_resultView, 1);

    auto* logsArea = new QWidget(rightSplitter);
    auto* logsLayout = new QVBoxLayout(logsArea);
    logsLayout->setContentsMargins(0, 0, 0, 0);
    logsLayout->setSpacing(6);
    auto* logsTitle = new QLabel(tr("EXECUTION LOG"), logsArea);
    logsTitle->setObjectName(QStringLiteral("productionSectionTitle"));
    logsLayout->addWidget(logsTitle);
    m_logView = new QTableView(logsArea);
    m_logView->setObjectName(QStringLiteral("productionLogView"));
    m_logView->setModel(m_logModel);
    m_logView->setAlternatingRowColors(true);
    m_logView->verticalHeader()->setVisible(false);
    m_logView->setWordWrap(false);
    m_logView->horizontalHeader()->setSectionResizeMode(
        RuntimeTimelineModel::TimeColumn, QHeaderView::Interactive);
    m_logView->horizontalHeader()->setSectionResizeMode(
        RuntimeTimelineModel::MessageColumn, QHeaderView::Stretch);
    m_logView->setColumnWidth(RuntimeTimelineModel::TimeColumn, 112);
    connect(m_resultView,
            &QTreeView::doubleClicked,
            this,
            &ProductionWindow::focusExecutionLogForResult);
    logsLayout->addWidget(m_logView, 1);
    rightSplitter->setStretchFactor(0, 4);
    rightSplitter->setStretchFactor(1, 1);
    rightSplitter->setSizes({520, 170});
    contentSplitter->setStretchFactor(0, 0);
    contentSplitter->setStretchFactor(1, 1);
    contentSplitter->setSizes({235, 900});
    layout->addWidget(contentSplitter, 1);

    auto* footer = new QWidget(central);
    footer->setObjectName(QStringLiteral("productionFooter"));
    auto* footerLayout = new QHBoxLayout(footer);
    footerLayout->setContentsMargins(12, 8, 12, 8);
    footerLayout->setSpacing(12);
    m_progress = new QProgressBar(footer);
    m_progress->setObjectName(QStringLiteral("productionProgress"));
    m_progress->setRange(0, 100);
    m_progress->setValue(0);
    m_progress->setTextVisible(true);
    footerLayout->addWidget(m_progress, 1);
    auto createCount = [footer](const QString& objectName) {
        auto* label = new QLabel(footer);
        label->setObjectName(objectName);
        label->setAlignment(Qt::AlignCenter);
        label->setMinimumWidth(84);
        return label;
    };
    m_passCountLabel = createCount(QStringLiteral("productionPassCount"));
    m_failCountLabel = createCount(QStringLiteral("productionFailCount"));
    m_totalCountLabel = createCount(QStringLiteral("productionTotalCount"));
    m_yieldLabel = createCount(QStringLiteral("productionYield"));
    m_averageTimeLabel = createCount(QStringLiteral("productionAverageTime"));
    m_yieldLabel->setMinimumWidth(112);
    m_averageTimeLabel->setMinimumWidth(132);
    footerLayout->addWidget(m_passCountLabel);
    footerLayout->addWidget(m_failCountLabel);
    footerLayout->addWidget(m_totalCountLabel);
    footerLayout->addWidget(m_yieldLabel);
    footerLayout->addWidget(m_averageTimeLabel);
    layout->addWidget(footer);
    setCentralWidget(central);

    setStyleSheet(QStringLiteral(R"css(
        QMainWindow#productionWindow, QWidget#productionCentral {
            background: #f4f6f8;
            color: #20272e;
        }
        QLabel#productionSequenceLabel {
            background: #ffffff;
            border: 1px solid #cbd2d9;
            border-radius: 4px;
            font-size: 17px;
            font-weight: 600;
            padding: 8px 12px;
        }
        QToolBar#productionToolbar {
            background: #ffffff;
            border: 1px solid #cbd2d9;
            border-radius: 4px;
            spacing: 8px;
            padding: 4px 8px;
        }
        QToolBar#productionToolbar QToolButton {
            min-width: 92px;
            min-height: 32px;
            padding: 2px 8px;
        }
        QFrame#productionSidebar {
            background: #ffffff;
            border: 1px solid #cbd2d9;
            border-radius: 4px;
        }
        QLabel#productionSectionTitle {
            color: #33414d;
            font-size: 13px;
            font-weight: 700;
            padding: 3px 0;
        }
        QLabel#productionMetricCaption {
            color: #687681;
            font-size: 11px;
            font-weight: 600;
        }
        QLabel#productionElapsedLabel {
            background: #e7f0f8;
            border: 1px solid #b9cedf;
            border-radius: 4px;
            color: #18384f;
            font-size: 22px;
            font-weight: 600;
            padding: 12px 6px;
        }
        QTreeView#productionResultView, QTableView#productionLogView {
            background: #ffffff;
            alternate-background-color: #f7f9fa;
            border: 1px solid #cbd2d9;
            gridline-color: #d8dee3;
            selection-background-color: #cfe4f3;
            selection-color: #20272e;
        }
        QTreeView#productionResultView::item {
            min-height: 29px;
            padding: 3px 6px;
        }
        QTableView#productionLogView::item {
            min-height: 25px;
            padding: 2px 4px;
        }
        QHeaderView::section {
            background: #e8edf1;
            border: 0;
            border-right: 1px solid #c5cdd4;
            border-bottom: 1px solid #b7c0c8;
            color: #2c3944;
            font-weight: 600;
            padding: 7px 6px;
        }
        QWidget#productionFooter {
            background: #ffffff;
            border: 1px solid #cbd2d9;
            border-radius: 4px;
        }
        QProgressBar#productionProgress {
            border: 1px solid #aeb9c2;
            background: #edf1f3;
            min-height: 24px;
            text-align: center;
        }
        QProgressBar#productionProgress::chunk { background: #5ca65c; }
        QLabel#productionPassCount { color: #237744; font-weight: 700; }
        QLabel#productionFailCount { color: #b12f2f; font-weight: 700; }
        QLabel#productionTotalCount { color: #33414d; font-weight: 700; }
        QLabel#productionYield { color: #175b87; font-weight: 700; }
        QLabel#productionAverageTime { color: #465561; font-weight: 700; }
    )css"));

    m_elapsedTimer = new QTimer(this);
    m_elapsedTimer->setInterval(50);
    connect(m_elapsedTimer, &QTimer::timeout,
            this, &ProductionWindow::updateElapsedTime);
    updateProgress();
    updateYieldStatistics();
    updateState(UiRunState::Empty);
}

void ProductionWindow::updateCommands()
{
    const bool manualStart = !m_selection.scanDialogEnabled;
    m_startAction->setVisible(manualStart);
    m_startAction->setEnabled(manualStart && m_viewModel->canRun());
    m_pauseAction->setEnabled(m_viewModel->canPause());
    m_resumeAction->setEnabled(m_viewModel->canResume());
    m_stopAction->setEnabled(m_viewModel->canStop());
    m_fieldDeviceAction->setEnabled(!m_resolvingStation &&
                                    !m_viewModel->canPause() &&
                                    !m_viewModel->canStop());
}

void ProductionWindow::updateState(UiRunState state)
{
    m_overallResult->setText(productionStateText(state));
    m_overallResult->setStyleSheet(productionStateStyle(state));
    if (state == UiRunState::Starting) {
        m_elapsed.restart();
        m_elapsedTimer->start();
    }
    if (state == UiRunState::Ready) {
        if (!m_pendingSerialNumber.isEmpty() && m_stationSnapshotReady) {
            startResolvedRun();
        } else {
            showScanDialogWhenReady();
        }
    }
    if (state == UiRunState::Completed || state == UiRunState::Failed) {
        m_elapsedTimer->stop();
        updateElapsedTime();
        m_progress->setValue(100);
        showScanDialogWhenReady();
    }
    updateCommands();
    statusBar()->showMessage(uiRunStateName(state));
}

void ProductionWindow::updateCompileSummary()
{
    const auto summary = m_viewModel->compileSummary();
    if (!summary.success) {
        m_stationSnapshotReady = false;
        m_pendingSerialNumber.clear();
        return;
    }
    m_stationSnapshotReady = true;
    m_previewReport = summary.previewReport;
    m_totalNodes = qMax(1, summary.nodeCount);
    resetPreviewForUut({});
    showScanDialogWhenReady();
    if (!m_pendingSerialNumber.isEmpty() && m_viewModel->canRun()) {
        startResolvedRun();
    }
}

void ProductionWindow::updateReport()
{
    const auto report = m_viewModel->report();
    if (report.uuts.isEmpty()) {
        return;
    }
    if (report.completed && m_runArtifactWriter->active()) {
        const auto archived = m_runArtifactWriter->finalize(report);
        if (!archived.success) {
            statusBar()->showMessage(
                tr("Report archive failed: %1").arg(archived.errorMessage),
                10000);
        }
    }
    m_resultModel->setReport(report);
    m_resultView->expandAll();
    m_nodeStates.clear();
    m_terminalNodes.clear();
    for (const auto& uut : report.uuts) {
        for (const auto& step : uut.steps) {
            collectStepStates(step, m_nodeStates);
        }
    }
    for (auto it = m_nodeStates.cbegin(); it != m_nodeStates.cend(); ++it) {
        if (isTerminal(it.value())) {
            m_terminalNodes.insert(it.key());
        }
    }
    updateProgress();

    const bool matchingUut = m_activeUutId.isEmpty() || std::any_of(
        report.uuts.cbegin(),
        report.uuts.cend(),
        [this](const auto& uut) { return uut.uutId == m_activeUutId; });
    if (!m_currentRunCounted && matchingUut && report.completed) {
        if (report.state == PicoATE::Core::ExecutionState::Completed &&
            !report.hasError) {
            ++m_passedUnits;
        } else {
            ++m_failedUnits;
        }
        m_totalCompletedDurationMs += m_elapsed.isValid() ? m_elapsed.elapsed() : 0;
        m_currentRunCounted = true;
        updateYieldStatistics();
    }
}

void ProductionWindow::applyRuntimeEvents(
    const QVector<PicoATE::Core::RuntimeEvent>& events)
{
    m_operatorPromptPresenter->applyRuntimeEvents(events);
    m_resultModel->applyRuntimeEvents(events);
    const auto logLines = m_logModel->applyRuntimeEvents(events);
    const auto written = m_runArtifactWriter->appendLogLines(logLines);
    if (!written.success) {
        statusBar()->showMessage(
            tr("TXT log write failed: %1").arg(written.errorMessage),
            10000);
    }
    m_resultView->expandAll();
    if (m_logModel->rowCount() > 0) {
        m_logView->scrollToBottom();
    }

    for (const auto& event : events) {
        if (!event.nodeId.isEmpty() && isTerminal(event.activationState)) {
            m_terminalNodes.insert(event.nodeId);
            m_nodeStates.insert(event.nodeId, event.activationState);
        } else if (!event.nodeId.isEmpty() &&
                   event.activationState == PicoATE::Core::ActivationState::Running &&
                   !isTerminal(m_nodeStates.value(event.nodeId))) {
            m_nodeStates.insert(event.nodeId, event.activationState);
        }
        if (!event.uutId.isEmpty() && !event.nodeId.isEmpty()) {
            const auto index = m_resultModel->indexForStep(event.uutId, event.nodeId);
            if (index.isValid() && event.activationState == PicoATE::Core::ActivationState::Running) {
                m_resultView->setCurrentIndex(index);
                m_resultView->scrollTo(index, QAbstractItemView::PositionAtBottom);
            }
        }
    }
    updateProgress();
}

void ProductionWindow::focusExecutionLogForResult(const QModelIndex& index)
{
    const auto step = m_resultModel->stepAt(index);
    if (!step || !m_logModel || !m_logView) {
        return;
    }
    const auto uut = m_resultModel->uutAt(index);
    const auto nodeId = step->nodePath.isEmpty() ? step->stepId : step->nodePath;
    int row = m_logModel->rowForNode(
        uut ? uut->uutId : m_activeUutId, nodeId);
    if (row < 0 && nodeId != step->stepId) {
        row = m_logModel->rowForNode(
            uut ? uut->uutId : m_activeUutId, step->stepId);
    }
    if (row < 0) {
        statusBar()->showMessage(
            tr("No execution log is available for %1 yet").arg(step->displayName),
            3000);
        return;
    }
    const auto logIndex = m_logModel->index(
        row, RuntimeTimelineModel::MessageColumn);
    m_logView->setCurrentIndex(logIndex);
    m_logView->scrollTo(logIndex, QAbstractItemView::PositionAtCenter);
}

void ProductionWindow::beginRun(const QString& serialNumber)
{
    if (!m_viewModel->canRun() || m_resolvingStation) {
        showScanDialogWhenReady();
        return;
    }
    m_pendingSerialNumber = serialNumber.trimmed();
    m_stationSnapshotReady = false;
    m_resolvingStation = true;
    updateCommands();
    statusBar()->showMessage(tr("Resolving field devices..."));
    m_fieldDeviceDialog->resolveEffectiveStation(true);
}

void ProductionWindow::startResolvedRun()
{
    if (!m_stationSnapshotReady || !m_viewModel->canRun() ||
        m_pendingSerialNumber.isEmpty()) {
        return;
    }
    const auto sn = std::exchange(m_pendingSerialNumber, {});
    m_activeUutId = sn;
    m_serialLabel->setText(sn);
    QVariantMap variables;
    variables.insert(QStringLiteral("sn"), sn);
    variables.insert(QStringLiteral("serialNumber"), sn);
    m_viewModel->runUut(sn, variables);
}

void ProductionWindow::openFieldDeviceConfiguration()
{
    if (m_viewModel->canPause() || m_viewModel->canStop() || m_resolvingStation) {
        return;
    }
    const bool restoreScanner = m_scanDialog->isVisible();
    m_scanDialog->hide();
    m_fieldDeviceDialog->exec();
    if (restoreScanner) {
        showScanDialogWhenReady();
    }
}

void ProductionWindow::applyEffectiveStation(const QByteArray& stationJson,
                                             const QString& errorMessage)
{
    m_resolvingStation = false;
    if (!errorMessage.isEmpty() || stationJson.isEmpty()) {
        m_stationSnapshotReady = false;
        m_pendingSerialNumber.clear();
        const auto message = errorMessage.isEmpty()
            ? tr("Cannot create the effective Station snapshot") : errorMessage;
        statusBar()->showMessage(message, 12000);
        QMessageBox::warning(this, tr("Field Device Configuration"), message);
        updateCommands();
        showScanDialogWhenReady();
        return;
    }
    m_viewModel->setStationDocument(m_selection.stationPath, stationJson);
    m_viewModel->compile();
    updateCommands();
}

void ProductionWindow::beginRunIteration(int iteration, int totalIterations)
{
    m_currentRunCounted = false;
    resetPreviewForUut(m_activeUutId);
    m_logModel->clear();
    QFile stationFile(m_selection.stationPath);
    QJsonObject stationObject;
    if (stationFile.open(QIODevice::ReadOnly)) {
        stationObject = QJsonDocument::fromJson(stationFile.readAll()).object();
    }
    const auto artifact = m_runArtifactWriter->begin(
        runArtifactSettingsFromStation(stationObject, m_selection.stationPath),
        m_activeUutId);
    if (!artifact.success) {
        statusBar()->showMessage(
            tr("Cannot create report files: %1").arg(artifact.errorMessage),
            10000);
    }
    m_terminalNodes.clear();
    m_nodeStates.clear();
    m_progress->setValue(0);
    updateProgress();
    m_elapsed.restart();
    m_elapsedTimer->start();
    statusBar()->showMessage(
        tr("Loop run %1 of %2").arg(iteration).arg(totalIterations));
}

void ProductionWindow::beginManualRun()
{
    const auto uutId = QStringLiteral("UUT-%1")
        .arg(QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd-HHmmss-zzz")));
    beginRun(uutId);
}

void ProductionWindow::resetPreviewForUut(const QString& uutId)
{
    auto preview = m_previewReport;
    preview.state = PicoATE::Core::ExecutionState::Idle;
    preview.completed = false;
    preview.hasError = false;
    if (!preview.uuts.isEmpty()) {
        preview.uuts.first().uutId = uutId;
        preview.uuts.first().hasError = false;
    }
    m_resultModel->setReport(std::move(preview));
    m_resultView->expandAll();
    m_terminalNodes.clear();
    m_nodeStates.clear();
    updateProgress();
}

void ProductionWindow::showScanDialogWhenReady()
{
    if (!m_selection.scanDialogEnabled || !m_viewModel->canRun()) {
        return;
    }
    QTimer::singleShot(0, m_scanDialog, [dialog = m_scanDialog] {
        dialog->showForNextScan();
    });
}

void ProductionWindow::updateElapsedTime()
{
    const qint64 elapsed = m_elapsed.isValid() ? m_elapsed.elapsed() : 0;
    const qint64 minutes = elapsed / 60000;
    const qint64 seconds = elapsed / 1000 % 60;
    const qint64 milliseconds = elapsed % 1000;
    m_elapsedLabel->setText(QStringLiteral("%1:%2.%3")
                                .arg(minutes, 2, 10, QLatin1Char('0'))
                                .arg(seconds, 2, 10, QLatin1Char('0'))
                                .arg(milliseconds, 3, 10, QLatin1Char('0')));
}

void ProductionWindow::updateProgress()
{
    m_progress->setValue(m_totalNodes > 0
        ? qMin(100, m_terminalNodes.size() * 100 / m_totalNodes)
        : 0);
}

void ProductionWindow::updateYieldStatistics()
{
    const int total = m_passedUnits + m_failedUnits;
    const double yield = total > 0
        ? static_cast<double>(m_passedUnits) * 100.0 / total
        : 0.0;
    m_passCountLabel->setText(tr("PASS %1").arg(m_passedUnits));
    m_failCountLabel->setText(tr("FAIL %1").arg(m_failedUnits));
    m_totalCountLabel->setText(tr("TOTAL %1").arg(total));
    m_yieldLabel->setText(tr("YIELD %1%").arg(yield, 0, 'f', 2));
    const qint64 average = total > 0 ? m_totalCompletedDurationMs / total : 0;
    m_averageTimeLabel->setText(tr("AVG %1").arg(compactDuration(average)));
}

} // namespace PicoATE::Ui
