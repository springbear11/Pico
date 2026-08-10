#include "ProductionWindow.h"

#include "ProportionalHeaderView.h"

#include "CoreExecutionService.h"
#include "ExecutionViewModel.h"
#include "FieldDeviceDialog.h"
#include "OperatorPromptPresenter.h"
#include "ProductRoutingDialog.h"
#include "PicoATE/Core/ProductRouting.h"
#include "PicoATE/Core/StationConfig.h"
#include "RunnerModels.h"
#include "RunArtifactWriter.h"
#include "ScanDialog.h"
#include "YieldDonutWidget.h"

#include <QAction>
#include <QAbstractItemView>
#include <QCloseEvent>
#include <QDateTime>
#include <QDir>
#include <QEvent>
#include <QFileInfo>
#include <QFile>
#include <QFont>
#include <QFormLayout>
#include <QFrame>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QJsonDocument>
#include <QMessageBox>
#include <QPixmap>
#include <QProgressBar>
#include <QSizePolicy>
#include <QSplitter>
#include <QStatusBar>
#include <QStringList>
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

QIcon productionToolbarIcon(const char* name)
{
    return QIcon(QStringLiteral(":/icons/%1.svg")
                     .arg(QString::fromLatin1(name)));
}

bool isTerminal(PicoATE::Core::ActivationState state)
{
    return PicoATE::Core::isTerminalActivation(state);
}

bool runtimeEventCarriesActivationState(
    PicoATE::Core::RuntimeEventKind kind)
{
    using PicoATE::Core::RuntimeEventKind;
    switch (kind) {
    case RuntimeEventKind::NodeStateChanged:
    case RuntimeEventKind::AttemptStarted:
    case RuntimeEventKind::AttemptCompleted:
    case RuntimeEventKind::LoopIterationStarted:
    case RuntimeEventKind::LoopCompleted:
    case RuntimeEventKind::TestItemStarted:
    case RuntimeEventKind::TestItemCompleted:
    case RuntimeEventKind::BarrierWaiting:
    case RuntimeEventKind::BarrierReleased:
    case RuntimeEventKind::CleanupActivated:
        return true;
    default:
        return false;
    }
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
        return QStringLiteral("background:#f4d768;color:#493a00;border:1px solid #cbaa39;border-radius:6px;padding:12px;");
    case UiRunState::Completed:
        return QStringLiteral("background:#cfe8d5;color:#1f5d35;border:1px solid #86b794;border-radius:6px;padding:12px;");
    case UiRunState::CompileFailed:
    case UiRunState::Failed:
        return QStringLiteral("background:#efc9c9;color:#862a2a;border:1px solid #c98282;border-radius:6px;padding:12px;");
    default:
        return QStringLiteral("background:#e7eaec;color:#303940;border:1px solid #c8cfd4;border-radius:6px;padding:12px;");
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
#if defined(PICOATE_UI_TEST_PROJECT_DIR)
    m_viewModel = new ExecutionViewModel(
        std::make_unique<CoreExecutionService>(
            QString::fromUtf8(PICOATE_UI_TEST_PROJECT_DIR)),
        this);
#else
    m_viewModel = new ExecutionViewModel(this);
#endif
    m_operatorPromptPresenter = new OperatorPromptPresenter(m_viewModel, this, this);
    m_resultModel = new UutStepModel(this);
    m_resultModel->setSingleUutPhaseLayout(true);
    m_logModel = new RuntimeTimelineModel(this);
    m_runArtifactWriter = std::make_unique<RunArtifactWriter>();
    m_scanDialog = new ScanDialog(this);
    // Auto routing must see the raw SN before any product-specific Station
    // rules are applied. The matched Station is validated in beginAutoRoutedRun.
    m_scanDialog->setValidationRules(
        m_selection.sequenceLoadMode == SequenceLoadMode::AutoBySn
            ? SnValidationRules{}
            : m_selection.snValidationRules);
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
    if (m_selection.sequenceLoadMode == SequenceLoadMode::Manual) {
        m_viewModel->setStationPath(m_selection.stationPath);
        m_viewModel->setSequencePath(m_selection.sequencePath);
        m_viewModel->compile();
    } else {
        QTimer::singleShot(0, this, &ProductionWindow::showScanDialogWhenReady);
    }
    updateCommands();
}

ProductionWindow::~ProductionWindow()
{
    m_runArtifactWriter->abandon();
    m_scanDialog->hide();
    m_operatorPromptPresenter->closeAll();
    m_viewModel->shutdown();
}

std::unique_ptr<ProductionWindow> createProductionWindow(
    StartupSelection selection)
{
    return std::make_unique<ProductionWindow>(std::move(selection));
}

void ProductionWindow::closeEvent(QCloseEvent* event)
{
    m_runArtifactWriter->abandon();
    m_scanDialog->hide();
    m_operatorPromptPresenter->closeAll();
    m_viewModel->shutdown();
    event->accept();
}

bool ProductionWindow::eventFilter(QObject* watched, QEvent* event)
{
    if (watched &&
        watched->objectName() == QStringLiteral("productionSidebar") &&
        event->type() == QEvent::Resize) {
        if (auto* sidebar = qobject_cast<QWidget*>(watched);
            sidebar) {
            auto* brandSlot = findChild<QWidget*>(
                QStringLiteral("productionBrandSlot"));
            if (!brandSlot) {
                return QMainWindow::eventFilter(watched, event);
            }
            brandSlot->setFixedWidth(
                sidebar->width());
        }
    }
    return QMainWindow::eventFilter(watched, event);
}

void ProductionWindow::buildUi()
{
    auto* central = new QWidget(this);
    central->setObjectName(QStringLiteral("productionCentral"));
    auto* layout = new QVBoxLayout(central);
    layout->setContentsMargins(16, 14, 16, 12);
    layout->setSpacing(10);

    auto* toolbar = new QToolBar(tr("TEST Controls"), central);
    toolbar->setObjectName(QStringLiteral("productionToolbar"));
    toolbar->setMovable(false);
    toolbar->setFloatable(false);
    toolbar->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    toolbar->setIconSize(QSize(20, 20));
    toolbar->setFixedHeight(48);
    toolbar->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    m_startAction = toolbar->addAction(
        productionToolbarIcon("play"), tr("Start"));
    m_startAction->setObjectName(QStringLiteral("productionStartAction"));
    m_pauseAction = toolbar->addAction(
        productionToolbarIcon("pause"), tr("Pause"));
    m_pauseAction->setObjectName(QStringLiteral("productionPauseAction"));
    m_resumeAction = toolbar->addAction(
        productionToolbarIcon("play"), tr("Resume"));
    m_resumeAction->setObjectName(QStringLiteral("productionResumeAction"));
    m_stopAction = toolbar->addAction(
        productionToolbarIcon("square"), tr("Stop"));
    m_stopAction->setObjectName(QStringLiteral("productionStopAction"));
    toolbar->addSeparator();
    m_fieldDeviceAction = toolbar->addAction(
        productionToolbarIcon("cable"), tr("Devices"));
    m_fieldDeviceAction->setObjectName(QStringLiteral("productionFieldDeviceAction"));
    m_productRoutingAction = toolbar->addAction(
        productionToolbarIcon("list-restart"), tr("Routes"));
    m_productRoutingAction->setObjectName(
        QStringLiteral("productionProductRoutingAction"));
    m_productRoutingAction->setToolTip(
        tr("Configure SN patterns and their test sequences"));
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
    connect(m_productRoutingAction, &QAction::triggered,
            this, &ProductionWindow::openProductRoutingConfiguration);
    layout->addWidget(toolbar);

    constexpr int ProductionSidebarWidth = 235;
    auto* brandHeader = new QHBoxLayout;
    brandHeader->setContentsMargins(0, 0, 0, 0);
    brandHeader->setSpacing(style()->pixelMetric(QStyle::PM_SplitterWidth));

    auto* brandSlot = new QWidget(central);
    brandSlot->setObjectName(QStringLiteral("productionBrandSlot"));
    brandSlot->setFixedWidth(ProductionSidebarWidth);
    auto* brandSlotLayout = new QHBoxLayout(brandSlot);
    brandSlotLayout->setContentsMargins(0, 0, 0, 0);
    brandSlotLayout->setSpacing(0);

    auto* brandLogo = new QLabel(brandSlot);
    brandLogo->setObjectName(QStringLiteral("productionBrandLogo"));
    brandLogo->setAccessibleName(tr("SINEXCEL"));
    brandLogo->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    brandLogo->setFixedSize(170, 32);
    const QPixmap brandSource(QStringLiteral(":/branding/Sinexcel.png"));
    const qreal brandPixelRatio = devicePixelRatioF();
    auto scaledBrand = brandSource.scaled(
        QSize(qRound(brandLogo->width() * brandPixelRatio),
              qRound(brandLogo->height() * brandPixelRatio)),
        Qt::KeepAspectRatio,
        Qt::SmoothTransformation);
    scaledBrand.setDevicePixelRatio(brandPixelRatio);
    brandLogo->setPixmap(scaledBrand);
    brandSlotLayout->addWidget(brandLogo, 0, Qt::AlignLeft | Qt::AlignVCenter);
    brandSlotLayout->addStretch(1);
    brandHeader->addWidget(brandSlot);

    const auto sequenceTitle = m_selection.sequenceLoadMode == SequenceLoadMode::AutoBySn
        ? tr("Auto By SN")
        : QFileInfo(m_selection.sequencePath).fileName();
    m_sequenceLabel = new QLabel(sequenceTitle, central);
    m_sequenceLabel->setObjectName(QStringLiteral("productionSequenceLabel"));
    m_sequenceLabel->setAlignment(Qt::AlignCenter);
    m_sequenceLabel->setMinimumHeight(40);
    m_sequenceLabel->setMaximumHeight(44);
    brandHeader->addWidget(m_sequenceLabel, 1);
    layout->addLayout(brandHeader);

    auto* contentSplitter = new QSplitter(Qt::Horizontal, central);
    contentSplitter->setObjectName(QStringLiteral("productionContentSplitter"));
    contentSplitter->setChildrenCollapsible(false);

    auto* sidebar = new QFrame(contentSplitter);
    sidebar->setObjectName(QStringLiteral("productionSidebar"));
    sidebar->setMinimumWidth(215);
    sidebar->setMaximumWidth(270);
    sidebar->installEventFilter(this);
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

    m_yieldChart = new YieldDonutWidget(sidebar);
    m_yieldChart->setObjectName(QStringLiteral("productionYieldChart"));
    sidebarLayout->addWidget(m_yieldChart, 0, Qt::AlignHCenter);

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
    contentSplitter->setSizes({ProductionSidebarWidth, 900});
    layout->addWidget(contentSplitter, 1);

    auto* progressPanel = new QWidget(central);
    progressPanel->setObjectName(QStringLiteral("productionProgressPanel"));
    auto* progressLayout = new QVBoxLayout(progressPanel);
    progressLayout->setContentsMargins(12, 8, 12, 8);
    m_progress = new QProgressBar(progressPanel);
    m_progress->setObjectName(QStringLiteral("productionProgress"));
    m_progress->setRange(0, 100);
    m_progress->setValue(0);
    m_progress->setTextVisible(true);
    progressLayout->addWidget(m_progress);
    layout->addWidget(progressPanel);

    statusBar()->setObjectName(QStringLiteral("productionStatusBar"));
    auto* statsBar = new QWidget(statusBar());
    statsBar->setObjectName(QStringLiteral("productionStatsBar"));
    auto* statsLayout = new QHBoxLayout(statsBar);
    statsLayout->setContentsMargins(10, 2, 10, 2);
    statsLayout->setSpacing(18);
    auto createCount = [statsBar](const QString& objectName) {
        auto* label = new QLabel(statsBar);
        label->setObjectName(objectName);
        label->setAlignment(Qt::AlignCenter);
        label->setMinimumWidth(92);
        return label;
    };
    m_passCountLabel = createCount(QStringLiteral("productionPassCount"));
    m_failCountLabel = createCount(QStringLiteral("productionFailCount"));
    m_totalCountLabel = createCount(QStringLiteral("productionTotalCount"));
    m_averageTimeLabel = createCount(QStringLiteral("productionAverageTime"));
    statsLayout->addWidget(m_passCountLabel);
    statsLayout->addWidget(m_failCountLabel);
    statsLayout->addWidget(m_totalCountLabel);
    statsLayout->addStretch(1);
    m_averageTimeLabel->setMinimumWidth(190);
    statsLayout->addWidget(m_averageTimeLabel);
    statusBar()->addPermanentWidget(statsBar, 1);
    setCentralWidget(central);

    setStyleSheet(QStringLiteral(R"css(
        QMainWindow#productionWindow, QWidget#productionCentral {
            background: #f4f6f7;
            color: #20262b;
        }
        QLabel#productionSequenceLabel {
            background: #ffffff;
            border: 1px solid #d7dde1;
            border-radius: 6px;
            font-size: 16px;
            font-weight: 600;
            padding: 8px 12px;
        }
        QToolBar#productionToolbar {
            background: #ffffff;
            border: 1px solid #d7dde1;
            border-radius: 6px;
            spacing: 4px;
            padding: 5px 8px;
        }
        QToolBar#productionToolbar QToolButton {
            min-width: 84px;
            min-height: 32px;
            padding: 2px 7px;
            font-weight: 600;
        }
        QFrame#productionSidebar {
            background: #ffffff;
            border: 1px solid #d7dde1;
            border-radius: 6px;
        }
        QLabel#productionSectionTitle {
            color: #344048;
            font-size: 13px;
            font-weight: 700;
            padding: 3px 0;
        }
        QLabel#productionMetricCaption {
            color: #707b83;
            font-size: 11px;
            font-weight: 600;
        }
        QLabel#productionElapsedLabel {
            background: #eef2f4;
            border: 1px solid #d4dce1;
            border-radius: 6px;
            color: #263139;
            font-size: 22px;
            font-weight: 600;
            padding: 12px 6px;
        }
        QTreeView#productionResultView, QTableView#productionLogView {
            background: #ffffff;
            alternate-background-color: #f7f8f9;
            border: 1px solid #d8dde1;
            border-radius: 4px;
            gridline-color: #e2e6e9;
            selection-background-color: #dcecf6;
            selection-color: #20262b;
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
            background: #eef1f3;
            border: 0;
            border-right: 1px solid #d8dde1;
            border-bottom: 1px solid #cbd2d7;
            color: #364149;
            font-weight: 600;
            padding: 7px 6px;
        }
        QWidget#productionProgressPanel {
            background: #ffffff;
            border: 1px solid #d7dde1;
            border-radius: 6px;
        }
        QStatusBar#productionStatusBar {
            background: #f8f9fa;
            border-top: 1px solid #dce1e4;
        }
        QWidget#productionStatsBar { background: transparent; border: 0; }
        QProgressBar#productionProgress {
            border: 1px solid #c6ced3;
            border-radius: 5px;
            background: #e9edef;
            min-height: 24px;
            text-align: center;
        }
        QProgressBar#productionProgress::chunk { background: #4f7d5d; border-radius: 4px; }
        QLabel#productionPassCount { color: #2f7548; font-weight: 700; }
        QLabel#productionFailCount { color: #a43838; font-weight: 700; }
        QLabel#productionTotalCount { color: #344048; font-weight: 700; }
        QLabel#productionAverageTime { color: #56636c; font-weight: 700; }
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
    const bool manualMode =
        m_selection.sequenceLoadMode == SequenceLoadMode::Manual;
    const bool manualStart = manualMode && !m_selection.scanDialogEnabled;
    const bool configurationAvailable =
        !m_viewModel->canPause() && !m_viewModel->canStop();
    m_startAction->setVisible(manualStart);
    m_startAction->setEnabled(manualStart && m_viewModel->canRun());
    m_pauseAction->setEnabled(m_viewModel->canPause());
    m_resumeAction->setEnabled(m_viewModel->canResume());
    m_stopAction->setEnabled(m_viewModel->canStop());
    m_fieldDeviceAction->setVisible(manualMode);
    m_fieldDeviceAction->setEnabled(manualMode && configurationAvailable);
    m_productRoutingAction->setVisible(!manualMode);
    m_productRoutingAction->setEnabled(!manualMode && configurationAvailable);
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
        if (!m_pendingSerialNumber.isEmpty()) {
            startResolvedRun();
        } else {
            showScanDialogWhenReady();
        }
    }
    if (state == UiRunState::Completed || state == UiRunState::Failed) {
        m_elapsedTimer->stop();
        updateElapsedTime();
        m_progress->setValue(100);
        const auto diagnostics = m_viewModel->diagnostics();
        const bool runCouldNotStart = state == UiRunState::Failed &&
                                      m_viewModel->report().uuts.isEmpty() &&
                                      !diagnostics.isEmpty();
        if (runCouldNotStart) {
            QStringList details;
            for (const auto& diagnostic : diagnostics) {
                if (diagnostic.severity != UiDiagnosticSeverity::Error) continue;
                auto line = diagnostic.path.isEmpty()
                    ? diagnostic.message
                    : QStringLiteral("%1: %2").arg(diagnostic.path,
                                                     diagnostic.message);
                if (!diagnostic.suggestion.isEmpty()) {
                    line += QStringLiteral("\n%1").arg(diagnostic.suggestion);
                }
                details.push_back(line);
                if (details.size() == 5) break;
            }
            if (details.isEmpty()) {
                details.push_back(tr("The test could not be started."));
            }
            const auto message = details.join(QStringLiteral("\n\n"));
            statusBar()->showMessage(message.section(QLatin1Char('\n'), 0, 0),
                                     15000);
            QTimer::singleShot(0, this, [this, message] {
                m_scanDialog->hide();
                QMessageBox::critical(this, tr("Test could not start"), message);
                showScanDialogWhenReady();
            });
        } else {
            showScanDialogWhenReady();
        }
    }
    updateCommands();
    statusBar()->showMessage(uiRunStateName(state));
}

void ProductionWindow::updateCompileSummary()
{
    const auto summary = m_viewModel->compileSummary();
    if (!summary.success) {
        // Changing the routed Sequence invalidates the previous artifact and
        // emits an empty summary before the new asynchronous compile starts.
        if (m_viewModel->state() != UiRunState::Compiling) {
            return;
        }
        const bool routedCompile =
            m_selection.sequenceLoadMode == SequenceLoadMode::AutoBySn &&
            !m_pendingSerialNumber.isEmpty();
        m_pendingSerialNumber.clear();
        if (routedCompile) {
            QStringList details;
            for (const auto& diagnostic : m_viewModel->diagnostics()) {
                if (diagnostic.severity != UiDiagnosticSeverity::Error) {
                    continue;
                }
                details.push_back(diagnostic.path.isEmpty()
                    ? diagnostic.message
                    : QStringLiteral("%1: %2").arg(diagnostic.path,
                                                     diagnostic.message));
                if (details.size() == 5) {
                    break;
                }
            }
            showRoutingError(details.isEmpty()
                ? tr("The routed Sequence could not be compiled")
                : details.join(QStringLiteral("\n")));
        }
        return;
    }
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
    m_resultView->clearSelection();
    m_resultView->setCurrentIndex({});
    if (!m_lastAutoFollowNodeId.isEmpty()) {
        const auto followed = m_resultModel->indexForStep(
            m_lastAutoFollowUutId, m_lastAutoFollowNodeId);
        if (followed.isValid()) {
            m_resultView->scrollTo(followed,
                                   QAbstractItemView::PositionAtBottom);
        }
    }
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
    if (m_logModel->rowCount() > 0) {
        m_logView->scrollToBottom();
    }

    for (const auto& event : events) {
        if (!event.nodeId.isEmpty() &&
            runtimeEventCarriesActivationState(event.kind)) {
            m_nodeStates.insert(event.nodeId, event.activationState);
            if (isTerminal(event.activationState)) {
                m_terminalNodes.insert(event.nodeId);
            } else {
                m_terminalNodes.remove(event.nodeId);
            }
        }
        if (!event.nodeId.isEmpty()) {
            const auto index = m_resultModel->indexForStep(event.uutId, event.nodeId);
            if (index.isValid() && event.activationState == PicoATE::Core::ActivationState::Running) {
                const int line = m_resultModel->visualLineNumber(index);
                if (line > m_lastAutoFollowLine) {
                    m_lastAutoFollowLine = line;
                    m_lastAutoFollowUutId = event.uutId;
                    m_lastAutoFollowNodeId = event.nodeId;
                    m_resultView->scrollTo(index, QAbstractItemView::PositionAtBottom);
                }
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
    if (m_selection.sequenceLoadMode == SequenceLoadMode::AutoBySn) {
        beginAutoRoutedRun(serialNumber);
        return;
    }
    if (!m_viewModel->canRun()) {
        showScanDialogWhenReady();
        return;
    }
    m_pendingSerialNumber = serialNumber.trimmed();
    statusBar()->showMessage(tr("Preparing station devices..."));
    startResolvedRun();
}

void ProductionWindow::beginAutoRoutedRun(const QString& serialNumber)
{
    const auto sn = serialNumber.trimmed();
    if (sn.isEmpty()) {
        return;
    }
    if (!m_viewModel->canChangeSources()) {
        showRoutingError(tr("A test is already running"));
        return;
    }

    const auto routing = PicoATE::Core::loadProductRoutingFile(
        m_selection.productRoutingPath);
    if (!routing.ok()) {
        QStringList details;
        for (const auto& error : routing.errors) {
            details.push_back(error.path.isEmpty()
                ? error.message
                : QStringLiteral("%1: %2").arg(error.path, error.message));
        }
        showRoutingError(details.join(QStringLiteral("\n")));
        return;
    }
    const auto route = PicoATE::Core::resolveProductRoute(routing.config, sn);
    if (!route.ok()) {
        QStringList details;
        for (const auto& error : route.errors) {
            details.push_back(error.message);
        }
        showRoutingError(details.join(QStringLiteral("\n")));
        return;
    }

    m_pendingSerialNumber = sn;
    m_selection.projectName = route.projectName;
    m_selection.projectPath = route.projectPath;
    m_selection.sequencePath = route.sequencePath;
    m_selection.stationPath = route.stationPath;
    updateStationSummary();
    m_sequenceLabel->setText(QFileInfo(route.sequencePath).fileName());
    m_sequenceLabel->setToolTip(
        tr("Project: %1\nSequence: %2\nStation: %3\nMatched route: %4")
            .arg(route.projectName, route.sequencePath,
                 route.stationPath, route.routeName));
    if (m_viewModel->sequencePath() == route.sequencePath &&
        m_viewModel->stationPath() == route.stationPath &&
        m_viewModel->canRun()) {
        startResolvedRun();
        return;
    }
    m_viewModel->setStationPath(route.stationPath);
    m_viewModel->setSequencePath(route.sequencePath);
    m_viewModel->compile();
    statusBar()->showMessage(
        tr("SN matched %1. Loading %2...")
            .arg(route.routeName, QFileInfo(route.sequencePath).fileName()));
}

void ProductionWindow::showRoutingError(const QString& message)
{
    const auto text = message.trimmed().isEmpty()
        ? tr("Product routing failed")
        : message.trimmed();
    statusBar()->showMessage(text.section(QLatin1Char('\n'), 0, 0), 10000);
    QTimer::singleShot(0, this, [this, text] {
        m_scanDialog->hide();
        QMessageBox::warning(this, tr("Product routing"), text);
        showScanDialogWhenReady();
    });
}

void ProductionWindow::startResolvedRun()
{
    if (!m_viewModel->canRun() || m_pendingSerialNumber.isEmpty()) {
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
    if (m_viewModel->canPause() || m_viewModel->canStop()) {
        return;
    }
    if (m_selection.stationPath.trimmed().isEmpty() ||
        !QFileInfo(m_selection.stationPath).isFile()) {
        statusBar()->showMessage(
            tr("Select a product route before configuring its devices"), 5000);
        return;
    }
    const bool restoreScanner = m_scanDialog->isVisible();
    m_scanDialog->hide();
    FieldDeviceDialog dialog(m_selection.stationPath, this);
    connect(&dialog, &FieldDeviceDialog::stationSaved, this, [this] {
        QFile file(m_selection.stationPath);
        if (file.open(QIODevice::ReadOnly)) {
            m_viewModel->setStationDocument(
                m_selection.stationPath, file.readAll());
            m_viewModel->compile();
            updateStationSummary();
        }
    });
    dialog.exec();
    if (restoreScanner) {
        showScanDialogWhenReady();
    }
}

void ProductionWindow::beginRunIteration(int iteration, int totalIterations)
{
    m_currentRunCounted = false;
    m_lastAutoFollowLine = 0;
    m_lastAutoFollowUutId.clear();
    m_lastAutoFollowNodeId.clear();
    resetPreviewForUut(m_activeUutId);
    m_logModel->clear();
    QFile stationFile(m_selection.stationPath);
    QJsonObject stationObject;
    if (stationFile.open(QIODevice::ReadOnly)) {
        stationObject = QJsonDocument::fromJson(stationFile.readAll()).object();
    }
    QFile sequenceFile(m_selection.sequencePath);
    QJsonObject sequenceObject;
    if (sequenceFile.open(QIODevice::ReadOnly)) {
        sequenceObject = QJsonDocument::fromJson(sequenceFile.readAll()).object();
    }
    const auto artifactContext = runArtifactContextFromDocuments(
        sequenceObject,
        m_selection.sequencePath,
        stationObject,
        m_selection.stationPath,
        m_activeUutId);
    const auto artifact = m_runArtifactWriter->begin(
        runArtifactSettingsFromStation(stationObject, m_selection.stationPath),
        artifactContext);
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
    m_resultView->clearSelection();
    m_resultView->setCurrentIndex({});
    m_terminalNodes.clear();
    m_nodeStates.clear();
    updateProgress();
}

void ProductionWindow::showScanDialogWhenReady()
{
    const bool autoRouting =
        m_selection.sequenceLoadMode == SequenceLoadMode::AutoBySn;
    const bool ready = autoRouting
        ? m_viewModel->canChangeSources() && m_pendingSerialNumber.isEmpty()
        : m_viewModel->canRun();
    if (!m_selection.scanDialogEnabled || !ready) {
        return;
    }
    m_scanDialog->setValidationRules(
        autoRouting ? SnValidationRules{} : m_selection.snValidationRules);
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
    m_passCountLabel->setText(tr("PASS %1").arg(m_passedUnits));
    m_failCountLabel->setText(tr("FAIL %1").arg(m_failedUnits));
    m_totalCountLabel->setText(tr("TOTAL %1").arg(total));
    m_yieldChart->setCounts(m_passedUnits, m_failedUnits);
    const qint64 average = total > 0 ? m_totalCompletedDurationMs / total : 0;
    m_averageTimeLabel->setText(
        tr("AVERAGE TIME %1").arg(compactDuration(average)));
}

void ProductionWindow::updateStationSummary()
{
    if (!m_stationLabel || !m_orderLabel || !m_testerLabel || !m_jigLabel) {
        return;
    }
    const auto stationResult = PicoATE::Core::loadStationConfigFile(
        m_selection.stationPath);
    const auto stationId = stationResult.config.stationId.isEmpty()
        ? QFileInfo(m_selection.stationPath).completeBaseName()
        : stationResult.config.stationId;
    const auto& metadata = stationResult.config.metadata;
    m_stationLabel->setText(stationId.isEmpty() ? tr("--") : stationId);
    m_orderLabel->setText(metadataValue(metadata, {"order", "orderNumber"}));
    m_testerLabel->setText(metadataValue(metadata, {"tester", "operator"}));
    m_jigLabel->setText(metadataValue(
        metadata, {"jigNo", "fixtureId", "fixture"}));
}

void ProductionWindow::openProductRoutingConfiguration()
{
    if (m_viewModel->canPause() || m_viewModel->canStop()) {
        return;
    }
    const bool restoreScanner = m_scanDialog->isVisible();
    m_scanDialog->hide();
    auto routingPath = m_selection.productRoutingPath.trimmed();
    if (routingPath.isEmpty()) {
        routingPath = QFileInfo(m_selection.stationPath).absoluteDir().filePath(
            QStringLiteral("ProductRouting.json"));
    }
    ProductRoutingDialog dialog(routingPath, this);
    connect(&dialog, &ProductRoutingDialog::routingSaved, this, [this] {
        statusBar()->showMessage(
            tr("Product routing saved. Changes apply to the next scan."), 5000);
    });
    dialog.exec();
    if (restoreScanner) {
        showScanDialogWhenReady();
    }
}

} // namespace PicoATE::Ui
