#include "UiTextBinding.h"
#include "ProductionWindow.h"

#include "ProportionalHeaderView.h"

#include "CoreExecutionService.h"
#include "ExecutionViewModel.h"
#include "FieldDeviceDialog.h"
#include "MultiUutOverviewWidget.h"
#include "OperatorPromptPresenter.h"
#include "ParserActualDelegate.h"
#include "ProductRoutingDialog.h"
#include "ProductRoutingScanSupport.h"
#include "PicoATE/Core/ProductRouting.h"
#include "PicoATE/Core/StationConfig.h"
#include "RunnerModels.h"
#include "RunArtifactWriter.h"
#include "ScanDialog.h"
#include "UutSlotConfigurationDialog.h"
#include "YieldDonutWidget.h"

#include <QAction>
#include <QAbstractButton>
#include <QAbstractItemView>
#include <QButtonGroup>
#include <QCloseEvent>
#include <QDateTime>
#include <QDir>
#include <QEvent>
#include <QFileInfo>
#include <QFile>
#include <QFont>
#include <QFontMetrics>
#include <QFormLayout>
#include <QFrame>
#include <QGridLayout>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QJsonDocument>
#include <QMessageBox>
#include <QPainter>
#include <QPen>
#include <QPixmap>
#include <QProgressBar>
#include <QPushButton>
#include <QResizeEvent>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QSizePolicy>
#include <QSplitter>
#include <QSpinBox>
#include <QStackedWidget>
#include <QStatusBar>
#include <QStringList>
#include <QStyle>
#include <QTableView>
#include <QTimer>
#include <QToolBar>
#include <QTreeView>
#include <QVariantAnimation>
#include <QVBoxLayout>
#include <QWidget>

#include <utility>
#include <initializer_list>
#include <algorithm>

using PicoATE::Ui::uiText;
using PicoATE::Ui::uiStateText;

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
    case UiRunState::Compiling: return uiText("COMPILING");
    case UiRunState::Ready: return uiText("READY");
    case UiRunState::Starting:
    case UiRunState::Running: return uiText("RUNNING");
    case UiRunState::Pausing: return uiText("PAUSING");
    case UiRunState::Paused: return uiText("PAUSED");
    case UiRunState::Stopping: return uiText("STOPPING");
    case UiRunState::Completed: return uiText("PASS");
    case UiRunState::CompileFailed:
    case UiRunState::Failed: return uiText("FAIL");
    default: return uiText("WAITING");
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

QIcon overviewIndicatorIcon(qreal fill)
{
    QPixmap pixmap(36, 36);
    pixmap.setDevicePixelRatio(2.0);
    pixmap.fill(Qt::transparent);

    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing, true);
    const QColor color(QStringLiteral("#202328"));
    painter.setPen(QPen(color, 1.6));
    painter.setBrush(Qt::NoBrush);
    painter.drawEllipse(QRectF(3.5, 3.5, 11.0, 11.0));

    const qreal radius = 4.2 * qBound<qreal>(0.0, fill, 1.0);
    if (radius > 0.0) {
        painter.setPen(Qt::NoPen);
        painter.setBrush(color);
        painter.drawEllipse(QPointF(9.0, 9.0), radius, radius);
    }
    return QIcon(pixmap);
}

QString productionOverviewStateText(UiRunState state, bool stopRequested)
{
    switch (state) {
    case UiRunState::Starting: return uiText("STARTING");
    case UiRunState::Running: return uiText("TESTING");
    case UiRunState::Pausing: return uiText("PAUSING");
    case UiRunState::Paused: return uiText("PAUSED");
    case UiRunState::Stopping: return uiText("STOPPING");
    case UiRunState::Completed:
    case UiRunState::Failed:
        return stopRequested ? uiText("STOPPED")
                             : uiText("COMPLETED");
    case UiRunState::Ready: return uiText("READY");
    default: return uiText("WAITING");
    }
}

} // namespace

class ProductionOverviewSummaryWidget final : public QFrame
{
public:
    explicit ProductionOverviewSummaryWidget(QWidget* parent = nullptr)
        : QFrame(parent)
    {
        setObjectName(QStringLiteral("productionOverviewSummary"));
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        setMinimumHeight(94);
        setMaximumHeight(108);

        auto* root = new QHBoxLayout(this);
        root->setContentsMargins(14, 10, 14, 10);
        root->setSpacing(10);

        auto* stateArea = new QWidget(this);
        stateArea->setObjectName(
            QStringLiteral("productionOverviewSummaryStateArea"));
        stateArea->setMinimumWidth(128);
        stateArea->setMaximumWidth(168);
        auto* stateLayout = new QVBoxLayout(stateArea);
        stateLayout->setContentsMargins(0, 0, 0, 0);
        stateLayout->setSpacing(3);
        auto* stateCaption = makeUiLabel("BATCH STATUS", stateArea);
        stateCaption->setObjectName(
            QStringLiteral("productionOverviewSummaryCaption"));
        m_stateLabel = new QLabel(uiText("WAITING"), stateArea);
        m_stateLabel->setObjectName(
            QStringLiteral("productionOverviewSummaryState"));
        m_stateLabel->setAlignment(Qt::AlignCenter);
        m_stateLabel->setMinimumWidth(112);
        m_elapsedLabel = new QLabel(uiText("Elapsed 00:00.000"), stateArea);
        m_elapsedLabel->setObjectName(
            QStringLiteral("productionOverviewSummaryElapsed"));
        stateLayout->addWidget(stateCaption);
        stateLayout->addWidget(m_stateLabel);
        stateLayout->addWidget(m_elapsedLabel);
        root->addWidget(stateArea);

        root->addWidget(createDivider());

        auto* stationArea = new QWidget(this);
        stationArea->setObjectName(
            QStringLiteral("productionOverviewSummaryStationArea"));
        stationArea->setMinimumWidth(240);
        auto* stationLayout = new QGridLayout(stationArea);
        stationLayout->setContentsMargins(0, 0, 0, 0);
        stationLayout->setHorizontalSpacing(18);
        stationLayout->setVerticalSpacing(1);
        m_stationLabel = addField(
            stationLayout, 0, 0, "STATION",
            QStringLiteral("productionOverviewStationValue"));
        m_modelLabel = addField(
            stationLayout, 0, 1, "MODEL",
            QStringLiteral("productionOverviewModelValue"));
        m_customerIdLabel = addField(
            stationLayout, 0, 2, "CUSTOMER ID",
            QStringLiteral("productionOverviewCustomerIdValue"));
        m_orderLabel = addField(
            stationLayout, 2, 0, "ORDER",
            QStringLiteral("productionOverviewOrderValue"));
        m_testerLabel = addField(
            stationLayout, 2, 1, "TESTER",
            QStringLiteral("productionOverviewTesterValue"));
        m_jigLabel = addField(
            stationLayout, 2, 2, "JIG NO.",
            QStringLiteral("productionOverviewJigValue"));
        for (int column = 0; column < 3; ++column) {
            stationLayout->setColumnStretch(column, 1);
        }
        root->addWidget(stationArea, 1);

        root->addWidget(createDivider());

        auto* countArea = new QWidget(this);
        countArea->setObjectName(
            QStringLiteral("productionOverviewSummaryCountArea"));
        auto* countLayout = new QGridLayout(countArea);
        countLayout->setContentsMargins(0, 0, 0, 0);
        countLayout->setHorizontalSpacing(14);
        countLayout->setVerticalSpacing(2);
        m_runningLabel = addCount(
            countLayout, 0, "RUNNING", QStringLiteral("running"),
            QStringLiteral("productionOverviewRunningValue"));
        m_passLabel = addCount(
            countLayout, 1, "PASS", QStringLiteral("pass"),
            QStringLiteral("productionOverviewPassValue"));
        m_failLabel = addCount(
            countLayout, 2, "FAIL", QStringLiteral("fail"),
            QStringLiteral("productionOverviewFailValue"));
        m_waitingLabel = addCount(
            countLayout, 3, "WAITING", QStringLiteral("waiting"),
            QStringLiteral("productionOverviewWaitingValue"));
        for (int column = 0; column < 4; ++column) {
            countLayout->setColumnStretch(column, 1);
        }
        root->addWidget(countArea);

        root->addWidget(createDivider());

        m_yieldChart = new YieldDonutWidget(this);
        m_yieldChart->setObjectName(
            QStringLiteral("productionOverviewYieldChart"));
        m_yieldChart->setMinimumSize(105, 62);
        m_yieldChart->setMaximumSize(126, 72);
        root->addWidget(m_yieldChart, 0, Qt::AlignVCenter);

        setRunState(UiRunState::Empty, false);
        setCounts(0, 0, 0, 0);
    }

    void setRunState(UiRunState state, bool stopRequested)
    {
        const int stateValue = static_cast<int>(state);
        if (m_lastState == stateValue &&
            m_lastStopRequested == stopRequested &&
            m_stateLabel->text() == productionOverviewStateText(state, stopRequested)) {
            return;
        }
        m_lastState = stateValue;
        m_lastStopRequested = stopRequested;
        m_stateLabel->setText(
            productionOverviewStateText(state, stopRequested));

        QString background = QStringLiteral("#eef2f4");
        QString foreground = QStringLiteral("#344751");
        QString border = QStringLiteral("#bcc7cd");
        switch (state) {
        case UiRunState::Starting:
        case UiRunState::Running:
        case UiRunState::Pausing:
        case UiRunState::Stopping:
            background = QStringLiteral("#fff4d7");
            foreground = QStringLiteral("#8a5d00");
            border = QStringLiteral("#d7ac45");
            break;
        case UiRunState::Paused:
            background = QStringLiteral("#e9f3f8");
            foreground = QStringLiteral("#315f78");
            border = QStringLiteral("#8fb4c6");
            break;
        case UiRunState::Completed:
            background = stopRequested ? QStringLiteral("#fbe8e8")
                                       : QStringLiteral("#e5f4e9");
            foreground = stopRequested ? QStringLiteral("#a83237")
                                       : QStringLiteral("#287848");
            border = stopRequested ? QStringLiteral("#db9295")
                                   : QStringLiteral("#87bd98");
            break;
        case UiRunState::CompileFailed:
        case UiRunState::Failed:
            background = QStringLiteral("#fbe8e8");
            foreground = QStringLiteral("#a83237");
            border = QStringLiteral("#db9295");
            break;
        default:
            break;
        }
        m_stateLabel->setStyleSheet(QStringLiteral(
            "background:%1;color:%2;border:1px solid %3;border-radius:5px;"
            "padding:4px 10px;font-size:16px;font-weight:800;")
                                         .arg(background, foreground, border));
    }

    void setElapsedText(const QString& elapsed)
    {
        const auto text = uiText("Elapsed %1").arg(elapsed);
        if (m_elapsedLabel->text() != text) {
            m_elapsedLabel->setText(text);
        }
    }

    void setStationDetails(const QString& station,
                           const QString& model,
                           const QString& customerId,
                           const QString& order,
                           const QString& tester,
                           const QString& jig)
    {
        setFieldText(m_stationLabel, station);
        setFieldText(m_modelLabel, model);
        setFieldText(m_customerIdLabel, customerId);
        setFieldText(m_orderLabel, order);
        setFieldText(m_testerLabel, tester);
        setFieldText(m_jigLabel, jig);
    }

    void setCounts(int running, int passed, int failed, int waiting)
    {
        setLabelText(m_runningLabel, QString::number(running));
        setLabelText(m_passLabel, QString::number(passed));
        setLabelText(m_failLabel, QString::number(failed));
        setLabelText(m_waitingLabel, QString::number(waiting));
    }

    void setYieldCounts(int passed, int failed)
    {
        if (m_yieldPassed == passed && m_yieldFailed == failed) {
            return;
        }
        m_yieldPassed = passed;
        m_yieldFailed = failed;
        m_yieldChart->setCounts(passed, failed);
    }

private:
    QFrame* createDivider()
    {
        auto* divider = new QFrame(this);
        divider->setObjectName(
            QStringLiteral("productionOverviewSummaryDivider"));
        divider->setFrameShape(QFrame::VLine);
        divider->setFrameShadow(QFrame::Plain);
        return divider;
    }

    QLabel* addField(QGridLayout* layout, int row, int column,
                     const char* caption, const QString& objectName)
    {
        auto* captionLabel = makeUiLabel(caption, this);
        captionLabel->setObjectName(
            QStringLiteral("productionOverviewSummaryCaption"));
        auto* valueLabel = new QLabel(uiText("--"), this);
        valueLabel->setObjectName(objectName);
        valueLabel->setProperty("productionOverviewValue", true);
        valueLabel->setMinimumWidth(52);
        valueLabel->setSizePolicy(QSizePolicy::Preferred,
                                  QSizePolicy::Preferred);
        layout->addWidget(captionLabel, row, column);
        layout->addWidget(valueLabel, row + 1, column);
        return valueLabel;
    }

    QLabel* addCount(QGridLayout* layout, int column,
                     const char* caption, const QString& tone,
                     const QString& objectName)
    {
        auto* captionLabel = makeUiLabel(caption, this);
        captionLabel->setObjectName(
            QStringLiteral("productionOverviewSummaryCaption"));
        auto* label = new QLabel(this);
        label->setObjectName(objectName);
        label->setProperty("productionOverviewValue", true);
        label->setProperty("summaryTone", tone);
        label->setAlignment(Qt::AlignCenter);
        label->setMinimumWidth(48);
        layout->addWidget(captionLabel, 0, column);
        layout->addWidget(label, 1, column);
        return label;
    }

    void setFieldText(QLabel* label, const QString& text)
    {
        const auto display = text.trimmed().isEmpty() ? uiText("--")
                                                       : text.trimmed();
        if (label->text() != display) {
            label->setText(display);
            label->setToolTip(display);
        }
    }

    void setLabelText(QLabel* label, const QString& text)
    {
        if (label->text() != text) {
            label->setText(text);
        }
    }

    QLabel* m_stateLabel = nullptr;
    QLabel* m_elapsedLabel = nullptr;
    YieldDonutWidget* m_yieldChart = nullptr;
    QLabel* m_stationLabel = nullptr;
    QLabel* m_modelLabel = nullptr;
    QLabel* m_customerIdLabel = nullptr;
    QLabel* m_orderLabel = nullptr;
    QLabel* m_testerLabel = nullptr;
    QLabel* m_jigLabel = nullptr;
    QLabel* m_runningLabel = nullptr;
    QLabel* m_passLabel = nullptr;
    QLabel* m_failLabel = nullptr;
    QLabel* m_waitingLabel = nullptr;
    int m_lastState = -1;
    bool m_lastStopRequested = false;
    int m_yieldPassed = -1;
    int m_yieldFailed = -1;
};

ProductionWindow::ProductionWindow(StartupSelection selection, QWidget* parent)
    : QMainWindow(parent)
    , m_selection(std::move(selection))
{
    setObjectName(QStringLiteral("productionWindow"));
    setWindowTitle(uiText("PicoATE TEST"));
    setMinimumSize(840, 560);
#if defined(PICOATE_UI_TEST_PROJECT_DIR)
    m_viewModel = new ExecutionViewModel(
        std::make_unique<CoreExecutionService>(
            QString::fromUtf8(PICOATE_UI_TEST_PROJECT_DIR)),
        this);
#else
    m_viewModel = new ExecutionViewModel(this);
#endif
    m_operatorPromptPresenter = new OperatorPromptPresenter(m_viewModel, this, this);
    connect(m_viewModel, &ExecutionViewModel::sequencePathChanged,
            m_operatorPromptPresenter,
            &OperatorPromptPresenter::setSequencePath);
    m_resultModel = new UutStepModel(this);
    m_resultModel->setSingleUutPhaseLayout(true);
    m_overviewModel = new UutOverviewModel(this);
    m_logModel = new RuntimeTimelineModel(this);
    m_logProxy = new UutRuntimeTimelineProxyModel(this);
    m_logProxy->setSourceModel(m_logModel);
    m_runArtifactWriter = std::make_unique<RunArtifactWriter>();
    m_scanDialog = new ScanDialog(this);
    // Auto routing must see the raw SN before any product-specific Station
    // rules are applied. The matched Station is validated in beginAutoRoutedRun.
    m_scanDialog->setValidationRules(
        m_selection.sequenceLoadMode == SequenceLoadMode::AutoBySn
            ? SnValidationRules{}
            : m_selection.snValidationRules);
    synchronizeUutSlotCount(
        StartupSupport::stationUutCount(m_selection.stationPath, 1));
    buildUi();
    installLanguageButton(this);
    connect(&UiLanguage::instance(), &UiLanguage::languageChanged,
            this, &ProductionWindow::retranslateUi, Qt::QueuedConnection);
    m_operatorPromptPresenter->setOverviewHost(m_uutOverview);
    updateUutSlotAction();
    QTimer::singleShot(0, this, [this] { applyResponsiveLayout(); });

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
    connect(m_scanDialog, &ScanDialog::barcodesAccepted,
            this, [this](const QStringList& barcodes) {
                m_uutSlotEnabled = normalizeUutSlotEnabledStates(
                    barcodes.size(), m_scanDialog->slotEnabledStates());
                updateUutSlotAction();
                beginRunBatch(barcodes);
            });
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

void ProductionWindow::resizeEvent(QResizeEvent* event)
{
    QMainWindow::resizeEvent(event);
    applyResponsiveLayout();
}

void ProductionWindow::applyResponsiveLayout(bool force)
{
    if (!centralWidget()) {
        return;
    }

    const bool compact = width() < 1200 || height() < 720;
    const int mode = compact ? 1 : 0;
    if (!force && m_responsiveLayoutMode == mode) {
        return;
    }
    m_responsiveLayoutMode = mode;

    if (auto* layout = qobject_cast<QVBoxLayout*>(centralWidget()->layout())) {
        layout->setContentsMargins(compact ? 10 : 16,
                                   compact ? 8 : 14,
                                   compact ? 10 : 16,
                                   compact ? 8 : 12);
        layout->setSpacing(compact ? 7 : 10);
    }

    if (auto* sidebar = findChild<QWidget*>(
            QStringLiteral("productionSidebar"))) {
        sidebar->setMinimumWidth(compact ? 185 : 215);
        sidebar->setMaximumWidth(compact ? 230 : 270);
        if (auto* sidebarLayout = qobject_cast<QVBoxLayout*>(sidebar->layout())) {
            const int margin = compact ? 12 : 18;
            sidebarLayout->setContentsMargins(margin, margin, margin, margin);
            sidebarLayout->setSpacing(compact ? 8 : 14);
        }
    }
    if (auto* splitter = findChild<QSplitter*>(
            QStringLiteral("productionContentSplitter"))) {
        splitter->setSizes(compact ? QList<int>{195, 805}
                                   : QList<int>{235, 900});
    }
    if (auto* splitter = findChild<QSplitter*>(
            QStringLiteral("productionDataSplitter"))) {
        splitter->setSizes(compact ? QList<int>{520, 120}
                                   : QList<int>{520, 170});
    }
    if (m_sequenceLabel) {
        m_sequenceLabel->setMinimumHeight(compact ? 34 : 40);
        m_sequenceLabel->setMaximumHeight(compact ? 38 : 44);
    }
    if (m_yieldChart) {
        m_yieldChart->setMinimumHeight(compact ? 54 : 64);
        m_yieldChart->setMaximumHeight(compact ? 74 : 100);
    }
    if (m_overallResult) {
        m_overallResult->setMinimumHeight(compact ? 76 : 112);
    }
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
            if (auto* navigationLead = findChild<QWidget*>(
                    QStringLiteral("productionUutNavigationLead"))) {
                navigationLead->setFixedWidth(sidebar->width());
            }
        }
    }
    return QMainWindow::eventFilter(watched, event);
}

void ProductionWindow::retranslateUi()
{
    updateCommands();
    updateUutSlotAction();
    updateYieldStatistics();
    updateOverviewSummary();
    m_overallResult->setText(productionStateText(m_viewModel->state()));
    statusBar()->showMessage(uiStateText(uiRunStateName(m_viewModel->state())));
}

void ProductionWindow::buildUi()
{
    auto* central = new QWidget(this);
    central->setObjectName(QStringLiteral("productionCentral"));
    auto* layout = new QVBoxLayout(central);
    layout->setContentsMargins(16, 14, 16, 12);
    layout->setSpacing(10);

    auto* toolbar = new QToolBar(uiText("TEST Controls"), central);
    toolbar->setObjectName(QStringLiteral("productionToolbar"));
    toolbar->setMovable(false);
    toolbar->setFloatable(false);
    toolbar->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    toolbar->setIconSize(QSize(20, 20));
    toolbar->setFixedHeight(48);
    toolbar->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    m_startAction = addUiAction(toolbar, productionToolbarIcon("play"), "Start");
    m_startAction->setObjectName(QStringLiteral("productionStartAction"));
    m_pauseAction = addUiAction(toolbar, productionToolbarIcon("pause"), "Pause");
    m_pauseAction->setObjectName(QStringLiteral("productionPauseAction"));
    m_resumeAction = addUiAction(toolbar, productionToolbarIcon("play"), "Resume");
    m_resumeAction->setObjectName(QStringLiteral("productionResumeAction"));
    m_stopAction = addUiAction(toolbar, productionToolbarIcon("square"), "Stop");
    m_stopAction->setObjectName(QStringLiteral("productionStopAction"));
    m_uutCount = new QSpinBox(toolbar);
    m_uutCount->setObjectName(
        QStringLiteral("productionUutCountSpinBox"));
    m_uutCount->setRange(1, 64);
    m_uutCount->setValue(qMax(1, m_uutSlotEnabled.size()));
    bindUiText(m_uutCount, "prefix", "UUTs ");
    m_uutCount->setAlignment(Qt::AlignCenter);
    m_uutCount->setFixedWidth(88);
    bindUiText(m_uutCount, "toolTip", "Number of physical UUT stations");
    toolbar->addWidget(m_uutCount);
    m_uutSlotsAction = addUiAction(toolbar, productionToolbarIcon("circle-check"), "UUT Slots");
    m_uutSlotsAction->setObjectName(
        QStringLiteral("productionUutSlotsAction"));
    bindUiText(m_uutSlotsAction, "toolTip", "Enable or disable physical UUT stations");
    toolbar->addSeparator();
    m_fieldDeviceAction = addUiAction(toolbar, productionToolbarIcon("cable"), "Devices");
    m_fieldDeviceAction->setObjectName(QStringLiteral("productionFieldDeviceAction"));
    m_productRoutingAction = addUiAction(toolbar, productionToolbarIcon("list-restart"), "Routes");
    m_productRoutingAction->setObjectName(
        QStringLiteral("productionProductRoutingAction"));
    bindUiText(m_productRoutingAction, "toolTip", "Configure SN patterns and their test sequences");
    connect(m_startAction, &QAction::triggered,
            this, &ProductionWindow::beginManualRun);
    connect(m_uutCount, &QSpinBox::valueChanged, this, [this](int value) {
        synchronizeUutSlotCount(value);
        m_scanDialog->setSlotCount(value);
        m_scanDialog->setSlotEnabledStates(m_uutSlotEnabled);
        if (!m_previewReport.uuts.isEmpty() &&
            m_viewModel->canChangeSources()) {
            resetPreviewForUuts(configuredPreviewUuts(), value > 1);
        }
        updateCommands();
    });
    connect(m_uutSlotsAction, &QAction::triggered,
            this, &ProductionWindow::configureUutSlots);
    connect(m_pauseAction, &QAction::triggered,
            m_viewModel, &ExecutionViewModel::pause);
    connect(m_resumeAction, &QAction::triggered,
            m_viewModel, &ExecutionViewModel::resume);
    connect(m_stopAction, &QAction::triggered,
            this, [this] {
                if (m_uutOverview && m_runStack &&
                    m_runStack->currentWidget() == m_overviewPage) {
                    m_uutOverview->beginStopTransition();
                }
                QTimer::singleShot(0, m_viewModel,
                                   [viewModel = m_viewModel] {
                                       viewModel->stop();
                                   });
            });
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
    brandLogo->setAccessibleName(uiText("SINEXCEL"));
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
        ? uiText("Auto By SN")
        : QFileInfo(m_selection.sequencePath).fileName();
    m_sequenceLabel = new QLabel(sequenceTitle, central);
    m_sequenceLabel->setObjectName(QStringLiteral("productionSequenceLabel"));
    m_sequenceLabel->setAlignment(Qt::AlignCenter);
    m_sequenceLabel->setMinimumHeight(40);
    m_sequenceLabel->setMaximumHeight(44);
    brandHeader->addWidget(m_sequenceLabel, 1);
    layout->addLayout(brandHeader);

    m_runNavigation = new QWidget(central);
    m_runNavigation->setObjectName(QStringLiteral("productionUutNavigation"));
    auto* navigationLayout = new QHBoxLayout(m_runNavigation);
    navigationLayout->setContentsMargins(0, 0, 0, 0);
    navigationLayout->setSpacing(
        style()->pixelMetric(QStyle::PM_SplitterWidth));

    auto* navigationLead = new QWidget(m_runNavigation);
    navigationLead->setObjectName(
        QStringLiteral("productionUutNavigationLead"));
    navigationLead->setFixedWidth(ProductionSidebarWidth);
    auto* navigationLeadLayout = new QHBoxLayout(navigationLead);
    navigationLeadLayout->setContentsMargins(6, 0, 6, 0);
    navigationLeadLayout->setSpacing(8);
    m_overviewButton = makeUiButton(overviewIndicatorIcon(0.0), "Overview", navigationLead);
    m_overviewButton->setObjectName(
        QStringLiteral("productionOverviewButton"));
    m_overviewButton->setCheckable(true);
    m_overviewButton->setIconSize(QSize(18, 18));
    m_overviewButton->setProperty("overviewIndicatorFill", 0.0);
    bindUiText(m_overviewButton, "toolTip", "Return to the UUT overview");
    auto* overviewIndicatorAnimation = new QVariantAnimation(
        m_overviewButton);
    overviewIndicatorAnimation->setDuration(140);
    connect(overviewIndicatorAnimation, &QVariantAnimation::valueChanged,
            m_overviewButton,
            [button = m_overviewButton](const QVariant& value) {
                const qreal fill = value.toReal();
                button->setProperty("overviewIndicatorFill", fill);
                button->setIcon(overviewIndicatorIcon(fill));
            });
    connect(m_overviewButton, &QPushButton::toggled,
            m_overviewButton,
            [button = m_overviewButton,
             overviewIndicatorAnimation](bool checked) {
                overviewIndicatorAnimation->stop();
                overviewIndicatorAnimation->setStartValue(
                    button->property("overviewIndicatorFill").toReal());
                overviewIndicatorAnimation->setEndValue(checked ? 1.0 : 0.0);
                overviewIndicatorAnimation->start();
            });
    auto* detailTitle = makeUiLabel("UUT DETAILS", navigationLead);
    detailTitle->setObjectName(QStringLiteral("productionSectionTitle"));
    navigationLeadLayout->addWidget(m_overviewButton);
    navigationLeadLayout->addWidget(detailTitle);
    navigationLeadLayout->addStretch(1);
    navigationLayout->addWidget(navigationLead);

    m_uutNavigationGroup = new QButtonGroup(m_runNavigation);
    m_uutNavigationGroup->setObjectName(
        QStringLiteral("productionUutNavigationGroup"));
    m_uutNavigationGroup->setExclusive(true);
    auto* uutButtonScroll = new QScrollArea(m_runNavigation);
    uutButtonScroll->setObjectName(
        QStringLiteral("productionUutButtonScroll"));
    uutButtonScroll->setFrameShape(QFrame::NoFrame);
    uutButtonScroll->setWidgetResizable(true);
    uutButtonScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    uutButtonScroll->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    uutButtonScroll->setFixedHeight(44);
    auto* uutButtonsHost = new QWidget(uutButtonScroll);
    uutButtonsHost->setObjectName(
        QStringLiteral("productionUutButtonsHost"));
    m_uutNavigationLayout = new QHBoxLayout(uutButtonsHost);
    m_uutNavigationLayout->setContentsMargins(0, 1, 6, 1);
    m_uutNavigationLayout->setSpacing(6);
    m_uutNavigationLayout->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    uutButtonScroll->setWidget(uutButtonsHost);
    navigationLayout->addWidget(uutButtonScroll, 1);
    layout->addWidget(m_runNavigation);

    auto* contentSplitter = new QSplitter(Qt::Horizontal, central);
    contentSplitter->setObjectName(QStringLiteral("productionContentSplitter"));
    contentSplitter->setChildrenCollapsible(false);

    auto* sidebar = new QFrame(contentSplitter);
    sidebar->setObjectName(QStringLiteral("productionSidebar"));
    m_runSidebar = sidebar;
    sidebar->setMinimumWidth(185);
    sidebar->setMaximumWidth(270);
    sidebar->installEventFilter(this);
    auto* sidebarLayout = new QVBoxLayout(sidebar);
    sidebarLayout->setContentsMargins(18, 18, 18, 18);
    sidebarLayout->setSpacing(14);

    auto* unitTitle = makeUiLabel("UNIT UNDER TEST", sidebar);
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
    m_serialLabel = new QLabel(uiText("--"), sidebar);
    m_serialLabel->setObjectName(QStringLiteral("productionSerialLabel"));
    m_stationLabel = new QLabel(stationId, sidebar);
    m_stationLabel->setObjectName(QStringLiteral("productionStationLabel"));
    m_modelLabel = new QLabel(stationResult.config.model.trimmed().isEmpty()
                                  ? uiText("--")
                                  : stationResult.config.model.trimmed(),
                              sidebar);
    m_modelLabel->setObjectName(QStringLiteral("productionModelLabel"));
    m_customerIdLabel = new QLabel(
        stationResult.config.customerId.trimmed().isEmpty()
            ? uiText("--")
            : stationResult.config.customerId.trimmed(),
        sidebar);
    m_customerIdLabel->setObjectName(
        QStringLiteral("productionCustomerIdLabel"));
    m_orderLabel = new QLabel(metadataValue(metadata, {"order", "orderNumber"}), sidebar);
    m_orderLabel->setObjectName(QStringLiteral("productionOrderLabel"));
    m_testerLabel = new QLabel(metadataValue(metadata, {"tester", "operator"}), sidebar);
    m_testerLabel->setObjectName(QStringLiteral("productionTesterLabel"));
    m_jigLabel = new QLabel(metadataValue(metadata, {"jigNo", "fixtureId", "fixture"}), sidebar);
    m_jigLabel->setObjectName(QStringLiteral("productionJigLabel"));
    for (auto* value : {m_serialLabel, m_stationLabel, m_modelLabel,
                        m_customerIdLabel, m_orderLabel, m_testerLabel,
                        m_jigLabel}) {
        value->setTextInteractionFlags(Qt::TextSelectableByMouse);
        value->setWordWrap(true);
    }
    addUiRow(details, "SN", m_serialLabel);
    addUiRow(details, "Station ID", m_stationLabel);
    addUiRow(details, "Model", m_modelLabel);
    addUiRow(details, "Customer ID", m_customerIdLabel);
    addUiRow(details, "Order", m_orderLabel);
    addUiRow(details, "Tester", m_testerLabel);
    addUiRow(details, "Jig No.", m_jigLabel);
    sidebarLayout->addLayout(details);
    sidebarLayout->addStretch(1);

    m_yieldChart = new YieldDonutWidget(sidebar);
    m_yieldChart->setObjectName(QStringLiteral("productionYieldChart"));
    sidebarLayout->addWidget(m_yieldChart, 0, Qt::AlignHCenter);

    auto* resultCaption = makeUiLabel("OVERALL RESULT", sidebar);
    resultCaption->setObjectName(QStringLiteral("productionMetricCaption"));
    resultCaption->setAlignment(Qt::AlignCenter);
    sidebarLayout->addWidget(resultCaption);
    m_overallResult = new QLabel(uiText("WAITING"), sidebar);
    m_overallResult->setObjectName(QStringLiteral("productionOverallResult"));
    m_overallResult->setAlignment(Qt::AlignCenter);
    m_overallResult->setMinimumHeight(112);
    auto resultFont = m_overallResult->font();
    resultFont.setBold(true);
    resultFont.setPointSize(resultFont.pointSize() + 13);
    m_overallResult->setFont(resultFont);
    sidebarLayout->addWidget(m_overallResult);

    auto* elapsedCaption = makeUiLabel("ELAPSED TIME", sidebar);
    elapsedCaption->setObjectName(QStringLiteral("productionMetricCaption"));
    elapsedCaption->setAlignment(Qt::AlignCenter);
    sidebarLayout->addWidget(elapsedCaption);
    m_elapsedLabel = new QLabel(uiText("00:00.000"), sidebar);
    m_elapsedLabel->setObjectName(QStringLiteral("productionElapsedLabel"));
    m_elapsedLabel->setAlignment(Qt::AlignCenter);
    sidebarLayout->addWidget(m_elapsedLabel);

    m_runStack = new QStackedWidget(contentSplitter);
    m_runStack->setObjectName(QStringLiteral("productionRunStack"));
    m_overviewPage = new QWidget(m_runStack);
    m_overviewPage->setObjectName(
        QStringLiteral("productionRunOverviewPage"));
    auto* overviewLayout = new QVBoxLayout(m_overviewPage);
    overviewLayout->setContentsMargins(0, 0, 0, 0);
    overviewLayout->setSpacing(8);
    m_overviewSummary = new ProductionOverviewSummaryWidget(m_overviewPage);
    overviewLayout->addWidget(m_overviewSummary);
    m_uutOverview = new MultiUutOverviewWidget(m_overviewPage);
    m_uutOverview->setObjectName(
        QStringLiteral("productionUutOverview"));
    m_uutOverview->setModel(m_overviewModel);
    overviewLayout->addWidget(m_uutOverview, 1);
    m_runStack->addWidget(m_overviewPage);

    m_detailPage = new QWidget(m_runStack);
    m_detailPage->setObjectName(QStringLiteral("productionRunDetailPage"));
    auto* detailLayout = new QVBoxLayout(m_detailPage);
    detailLayout->setContentsMargins(0, 0, 0, 0);
    detailLayout->setSpacing(0);
    auto* rightSplitter = new QSplitter(Qt::Vertical, m_detailPage);
    rightSplitter->setObjectName(QStringLiteral("productionDataSplitter"));
    rightSplitter->setChildrenCollapsible(false);
    detailLayout->addWidget(rightSplitter, 1);

    auto* resultsArea = new QWidget(rightSplitter);
    auto* resultsLayout = new QVBoxLayout(resultsArea);
    resultsLayout->setContentsMargins(0, 0, 0, 0);
    resultsLayout->setSpacing(6);
    auto* resultsTitle = makeUiLabel("TEST RESULTS", resultsArea);
    resultsTitle->setObjectName(QStringLiteral("productionSectionTitle"));
    resultsLayout->addWidget(resultsTitle);
    m_resultView = new QTreeView(resultsArea);
    m_resultView->setObjectName(QStringLiteral("productionResultView"));
    m_resultView->setModel(m_resultModel);
    m_resultView->setItemDelegateForColumn(
        UutStepModel::ActualColumn,
        new ParserActualDelegate(m_resultView));
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
    auto* logsTitle = makeUiLabel("EXECUTION LOG", logsArea);
    logsTitle->setObjectName(QStringLiteral("productionSectionTitle"));
    logsLayout->addWidget(logsTitle);
    m_logView = new QTableView(logsArea);
    m_logView->setObjectName(QStringLiteral("productionLogView"));
    m_logView->setModel(m_logProxy);
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
    m_runStack->addWidget(m_detailPage);
    m_runStack->setCurrentWidget(m_detailPage);
    connect(m_uutOverview, &MultiUutOverviewWidget::uutActivated,
            this, &ProductionWindow::showUutDetails);
    connect(m_overviewButton, &QPushButton::clicked,
            this, &ProductionWindow::showUutOverview);
    connect(m_uutNavigationGroup, &QButtonGroup::idClicked,
            this, [this](int id) {
                const auto* button = m_uutNavigationGroup->button(id);
                if (button && button->isEnabled()) {
                    showUutDetails(button->property("uutId").toString());
                }
            });
    contentSplitter->setStretchFactor(0, 0);
    contentSplitter->setStretchFactor(1, 1);
    contentSplitter->setSizes({ProductionSidebarWidth, 900});
    layout->addWidget(contentSplitter, 1);

    m_progressPanel = new QWidget(central);
    m_progressPanel->setObjectName(QStringLiteral("productionProgressPanel"));
    auto* progressLayout = new QVBoxLayout(m_progressPanel);
    progressLayout->setContentsMargins(12, 8, 12, 8);
    m_progress = new QProgressBar(m_progressPanel);
    m_progress->setObjectName(QStringLiteral("productionProgress"));
    m_progress->setRange(0, 100);
    m_progress->setValue(0);
    m_progress->setTextVisible(true);
    progressLayout->addWidget(m_progress);
    layout->addWidget(m_progressPanel);

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
        QFrame#productionOverviewSummary {
            background: #ffffff;
            border: 1px solid #d2dade;
            border-radius: 6px;
        }
        QFrame#productionOverviewSummaryDivider {
            color: #dfe5e8;
            background: #dfe5e8;
            border: 0;
            min-width: 1px;
            max-width: 1px;
        }
        QLabel#productionOverviewSummaryCaption {
            color: #74838c;
            font-size: 10px;
            font-weight: 700;
        }
        QLabel#productionOverviewSummaryElapsed {
            color: #64747d;
            font-size: 11px;
            font-weight: 600;
        }
        QLabel[productionOverviewValue="true"] {
            color: #253139;
            font-size: 14px;
            font-weight: 700;
        }
        QLabel[summaryTone="running"] {
            color: #8a5d00;
        }
        QLabel[summaryTone="pass"] {
            color: #287848;
        }
        QLabel[summaryTone="fail"] {
            color: #a83237;
        }
        QLabel[summaryTone="waiting"] {
            color: #52646e;
        }
        QWidget#productionUutNavigation {
            background: #ffffff;
            border: 1px solid #d7dde1;
            border-radius: 5px;
        }
        QWidget#productionUutNavigationLead,
        QWidget#productionUutButtonsHost,
        QScrollArea#productionUutButtonScroll,
        QScrollArea#productionUutButtonScroll > QWidget > QWidget {
            background: transparent;
            border: 0;
        }
        QPushButton#productionOverviewButton {
            background: transparent;
            border: 0;
            color: #344048;
            min-height: 30px;
            padding: 2px 9px;
            font-weight: 600;
        }
        QPushButton#productionOverviewButton:hover {
            background: #e7f1f7;
            border-radius: 4px;
        }
        QPushButton#productionOverviewButton:checked {
            background: #f0f3f5;
            border-radius: 4px;
        }
        QPushButton[productionUutSwitch="true"] {
            background: #ffffff;
            color: #20262b;
            border: 1px solid #cbd3d8;
            border-radius: 5px;
            min-height: 31px;
            padding: 1px 12px;
            font-weight: 700;
        }
        QPushButton[productionUutSwitch="true"]:checked {
            background: #23272b;
            color: #ffffff;
            border-color: #23272b;
        }
        QPushButton[productionUutSwitch="true"]:disabled {
            background: #e7eaec;
            color: #8a949a;
            border-color: #cfd5d9;
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
    const bool configurationAvailable = m_viewModel->canChangeSources();
    m_startAction->setVisible(manualStart);
    m_startAction->setEnabled(manualStart && m_viewModel->canRun());
    m_pauseAction->setEnabled(m_viewModel->canPause());
    m_resumeAction->setEnabled(m_viewModel->canResume());
    m_stopAction->setEnabled(m_viewModel->canStop());
    const int slotCount = qMax(1, m_uutSlotEnabled.size());
    m_uutCount->setEnabled(configurationAvailable);
    m_uutSlotsAction->setVisible(slotCount > 1);
    m_uutSlotsAction->setEnabled(slotCount > 1 && configurationAvailable);
    m_fieldDeviceAction->setVisible(manualMode);
    m_fieldDeviceAction->setEnabled(manualMode && configurationAvailable);
    m_productRoutingAction->setVisible(!manualMode);
    m_productRoutingAction->setEnabled(!manualMode && configurationAvailable);
    if (m_runNavigation) {
        m_runNavigation->setVisible(slotCount > 1);
    }
}

void ProductionWindow::updateState(UiRunState state)
{
    if (state == UiRunState::Starting) {
        m_stopRequested = false;
    } else if (state == UiRunState::Stopping) {
        m_stopRequested = true;
    }
    m_overallResult->setText(productionStateText(state));
    m_overallResult->setStyleSheet(productionStateStyle(state));
    if (state == UiRunState::Starting) {
        m_elapsed.restart();
        m_elapsedTimer->start();
    }
    if (state == UiRunState::Ready) {
        if (m_runPreparationPending) {
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
                details.push_back(uiText("The test could not be started."));
            }
            const auto message = details.join(QStringLiteral("\n\n"));
            statusBar()->showMessage(message.section(QLatin1Char('\n'), 0, 0),
                                     15000);
            QTimer::singleShot(0, this, [this, message] {
                m_scanDialog->hide();
                QMessageBox::critical(this, uiText("Test could not start"), message);
                showScanDialogWhenReady();
            });
        } else {
            showScanDialogWhenReady();
        }
    }
    updateCommands();
    updateOverviewSummary();
    statusBar()->showMessage(uiStateText(uiRunStateName(state)));
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
            m_runPreparationPending;
        m_pendingSerialNumbers.clear();
        m_pendingUutSlotEnabled.clear();
        m_runPreparationPending = false;
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
                ? uiText("The routed Sequence could not be compiled")
                : details.join(QStringLiteral("\n")));
        }
        return;
    }
    synchronizeUutSlotCount(configuredUutCount());
    m_previewReport = summary.previewReport;
    m_totalNodes = qMax(1, summary.nodeCount);
    resetPreviewForUuts(configuredPreviewUuts(), configuredUutCount() > 1);
    showScanDialogWhenReady();
    if (m_runPreparationPending && m_viewModel->canRun()) {
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
                uiText("Report archive failed: %1").arg(archived.errorMessage),
                10000);
        }
    }
    if (m_overviewModel) {
        m_overviewModel->setReport(report);
    }
    updateOverviewSummary();
    if (!m_selectedUutId.isEmpty()) {
        const int selectedRow = m_overviewModel
            ? m_overviewModel->rowForUut(m_selectedUutId)
            : -1;
        const auto selectedEntry = selectedRow >= 0 && m_overviewModel
            ? m_overviewModel->entryAt(selectedRow)
            : std::optional<UutOverviewEntry>{};
        if (!selectedEntry || !selectedEntry->enabled) {
            m_selectedUutId.clear();
        }
    }
    if (m_selectedUutId.isEmpty() && m_overviewModel) {
        for (int row = 0; row < m_overviewModel->rowCount(); ++row) {
            const auto entry = m_overviewModel->entryAt(row);
            if (entry && entry->enabled) {
                m_selectedUutId = entry->uutId;
                break;
            }
        }
    }
    m_resultModel->setVisibleUutId(m_selectedUutId);
    m_logProxy->setVisibleUutId(m_selectedUutId);
    m_uutOverview->setSelectedUutId(m_selectedUutId);
    m_resultModel->setReport(report);
    rebuildUutNavigation();
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

    if (!m_currentRunCounted && report.completed) {
        for (const auto& uut : report.uuts) {
            if (uut.completed && !uut.hasError &&
                uut.outcome != PicoATE::Core::NodeOutcome::Cancelled) {
                ++m_passedUnits;
            } else {
                ++m_failedUnits;
            }
            m_totalCompletedDurationMs += uut.durationMs >= 0
                ? uut.durationMs
                : 0;
        }
        m_currentRunCounted = true;
        updateYieldStatistics();
    }
}

void ProductionWindow::applyRuntimeEvents(
    const QVector<PicoATE::Core::RuntimeEvent>& events)
{
    m_operatorPromptPresenter->applyRuntimeEvents(events);
    if (m_elapsed.isValid()) {
        m_overviewModel->setSessionElapsedMs(m_elapsed.elapsed());
    }
    m_overviewModel->applyRuntimeEvents(events);
    m_uutOverview->applyRuntimeEvents(events);
    updateOverviewSummary();
    m_resultModel->applyRuntimeEvents(events);
    const auto logLines = m_logModel->applyRuntimeEvents(events);
    const auto written = m_runArtifactWriter->appendLogLines(logLines);
    if (!written.success) {
        statusBar()->showMessage(
            uiText("TXT log write failed: %1").arg(written.errorMessage),
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
            uiText("No execution log is available for %1 yet").arg(step->displayName),
            3000);
        return;
    }
    const auto sourceIndex = m_logModel->index(
        row, RuntimeTimelineModel::MessageColumn);
    const auto logIndex = m_logProxy->mapFromSource(sourceIndex);
    if (!logIndex.isValid()) {
        statusBar()->showMessage(
            uiText("The execution log belongs to another UUT"), 3000);
        return;
    }
    m_logView->setCurrentIndex(logIndex);
    m_logView->scrollTo(logIndex, QAbstractItemView::PositionAtCenter);
}

void ProductionWindow::beginRun(const QString& serialNumber)
{
    beginRunBatch(serialNumber.trimmed().isEmpty()
                      ? QStringList{}
                      : QStringList{serialNumber});
}

void ProductionWindow::beginRunBatch(const QStringList& serialNumbers)
{
    if (m_selection.sequenceLoadMode == SequenceLoadMode::AutoBySn) {
        beginAutoRoutedRunBatch(serialNumbers);
        return;
    }
    if (!m_viewModel->canRun()) {
        showScanDialogWhenReady();
        return;
    }
    m_pendingSerialNumbers.clear();
    m_pendingSerialNumbers.reserve(serialNumbers.size());
    for (const auto& serialNumber : serialNumbers) {
        m_pendingSerialNumbers.push_back(serialNumber.trimmed());
    }
    const int slotCount = m_pendingSerialNumbers.isEmpty()
        ? configuredUutCount()
        : m_pendingSerialNumbers.size();
    synchronizeUutSlotCount(slotCount);
    m_pendingUutSlotEnabled = m_uutSlotEnabled;
    for (int slot = 0; slot < m_pendingSerialNumbers.size(); ++slot) {
        if (!m_pendingUutSlotEnabled[slot]) {
            m_pendingSerialNumbers[slot].clear();
        }
    }
    m_runPreparationPending = true;
    statusBar()->showMessage(uiText("Preparing station devices..."));
    startResolvedRun();
}

void ProductionWindow::beginAutoRoutedRun(const QString& serialNumber)
{
    beginAutoRoutedRunBatch({serialNumber});
}

void ProductionWindow::beginAutoRoutedRunBatch(
    const QStringList& serialNumbers)
{
    QStringList sns;
    sns.reserve(serialNumbers.size());
    for (const auto& serialNumber : serialNumbers) {
        sns.push_back(serialNumber.trimmed());
    }
    m_uutSlotEnabled = normalizeUutSlotEnabledStates(
        sns.size(), m_uutSlotEnabled);
    bool hasActiveSerialNumber = false;
    for (int slot = 0; slot < sns.size(); ++slot) {
        if (!m_uutSlotEnabled[slot]) {
            sns[slot].clear();
            continue;
        }
        if (m_uutSlotEnabled[slot] && !sns[slot].isEmpty()) {
            hasActiveSerialNumber = true;
        }
    }
    if (!hasActiveSerialNumber) {
        return;
    }
    if (!m_viewModel->canChangeSources()) {
        showRoutingError(uiText("A test is already running"));
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
    const auto batch = PicoATE::Core::resolveProductBatchRoute(
        routing.config, sns);
    if (!batch.ok()) {
        QStringList details;
        for (const auto& error : batch.errors) {
            details.push_back(error.message);
        }
        showRoutingError(details.join(QStringLiteral("\n")));
        return;
    }
    if (sns.size() != batch.uutCount) {
        showRoutingError(
            uiText("Project %1 provides %2 UUT slot(s), but the scanner has %3")
                .arg(batch.route.projectName)
                .arg(batch.uutCount)
                .arg(sns.size()));
        return;
    }
    const auto& route = batch.route;

    synchronizeUutSlotCount(batch.uutCount);
    m_pendingSerialNumbers = sns;
    m_pendingUutSlotEnabled = m_uutSlotEnabled;
    m_runPreparationPending = true;
    m_selection.projectName = route.projectName;
    m_selection.projectPath = route.projectPath;
    m_selection.sequencePath = route.sequencePath;
    m_selection.stationPath = route.stationPath;
    updateStationSummary();
    m_sequenceLabel->setText(QFileInfo(route.sequencePath).fileName());
    m_sequenceLabel->setToolTip(
        uiText("Project: %1\nSequence: %2\nStation: %3\nMatched route: %4")
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
        uiText("%1 active UUT SN(s) matched %2. Loading %3...")
            .arg(enabledUutSlotCount(m_uutSlotEnabled))
            .arg(route.routeName, QFileInfo(route.sequencePath).fileName()));
}

void ProductionWindow::showRoutingError(const QString& message)
{
    const auto text = message.trimmed().isEmpty()
        ? uiText("Product routing failed")
        : message.trimmed();
    statusBar()->showMessage(text.section(QLatin1Char('\n'), 0, 0), 10000);
    QTimer::singleShot(0, this, [this, text] {
        m_scanDialog->hide();
        QMessageBox::warning(this, uiText("Product routing"), text);
        showScanDialogWhenReady();
    });
}

void ProductionWindow::startResolvedRun()
{
    if (!m_viewModel->canRun() || !m_runPreparationPending) {
        return;
    }
    auto serialNumbers = std::exchange(m_pendingSerialNumbers, QStringList{});
    auto enabledStates = std::exchange(
        m_pendingUutSlotEnabled, QVector<bool>{});
    m_runPreparationPending = false;
    int uutCount = serialNumbers.size();
    if (uutCount == 0) {
        uutCount = configuredUutCount();
        serialNumbers = QStringList(uutCount, QString{});
    }
    enabledStates = normalizeUutSlotEnabledStates(
        uutCount,
        enabledStates.isEmpty() ? m_uutSlotEnabled : enabledStates);
    if (enabledUutSlotCount(enabledStates) == 0) {
        statusBar()->showMessage(uiText("Enable at least one UUT station"), 4000);
        return;
    }

    QVector<RunRequest::UutInput> inputs;
    inputs.reserve(uutCount);
    for (int index = 0; index < uutCount; ++index) {
        const auto sn = serialNumbers.value(index).trimmed();
        RunRequest::UutInput input;
        input.uutId = uutCount == 1 && !sn.isEmpty()
            ? sn
            : QStringLiteral("UUT-%1").arg(index + 1);
        input.slotIndex = index;
        input.enabled = enabledStates[index];
        input.variables.insert(QStringLiteral("sn"), sn);
        input.variables.insert(QStringLiteral("serialNumber"), sn);
        inputs.push_back(std::move(input));
    }

    const auto active = std::find_if(
        inputs.cbegin(), inputs.cend(),
        [](const RunRequest::UutInput& input) { return input.enabled; });
    m_activeSerialNumber = active->variables.value(
        QStringLiteral("serialNumber")).toString().trimmed();
    m_activeUutId = active->uutId;
    const int activeCount = enabledUutSlotCount(enabledStates);
    m_serialLabel->setText(m_activeSerialNumber.isEmpty()
                               ? uiText("--")
                               : activeCount > 1
                               ? uiText("%1  (+%2)").arg(m_activeSerialNumber)
                                                  .arg(activeCount - 1)
                               : m_activeSerialNumber);
    m_runUutInputs = inputs;
    m_selectedUutId = m_activeUutId;
    resetPreviewForUuts(m_runUutInputs, uutCount > 1);
    m_viewModel->runUuts(inputs);
}

void ProductionWindow::configureUutSlots()
{
    const int slotCount = configuredUutCount();
    synchronizeUutSlotCount(slotCount);
    const bool restoreScanner = m_scanDialog->isVisible();
    m_uutSlotDialogOpen = true;
    m_scanDialog->hide();
    const auto selected = showUutSlotConfigurationDialog(
        this, slotCount, m_uutSlotEnabled);
    if (selected) {
        m_uutSlotEnabled = *selected;
        m_scanDialog->setSlotCount(slotCount);
        m_scanDialog->setSlotEnabledStates(m_uutSlotEnabled);
        updateUutSlotAction();
        if (!m_previewReport.uuts.isEmpty() &&
            m_viewModel->canChangeSources()) {
            resetPreviewForUuts(configuredPreviewUuts(), slotCount > 1);
        }
        updateCommands();
    }
    m_uutSlotDialogOpen = false;
    if (restoreScanner) {
        m_scanDialog->show();
        m_scanDialog->raise();
        m_scanDialog->activateWindow();
        m_scanDialog->setFocus(Qt::OtherFocusReason);
    }
}

int ProductionWindow::configuredUutCount() const
{
    return qBound(
        1,
        m_uutCount ? m_uutCount->value() : m_uutSlotEnabled.size(),
        64);
}

void ProductionWindow::synchronizeUutSlotCount(int slotCount)
{
    slotCount = qBound(1, slotCount, 64);
    if (m_uutCount && m_uutCount->value() != slotCount) {
        const QSignalBlocker blocker(m_uutCount);
        m_uutCount->setValue(slotCount);
    }
    m_uutSlotEnabled = normalizeUutSlotEnabledStates(
        slotCount, m_uutSlotEnabled);
    if (enabledUutSlotCount(m_uutSlotEnabled) == 0) {
        m_uutSlotEnabled[0] = true;
    }
    updateUutSlotAction();
}

void ProductionWindow::updateUutSlotAction()
{
    if (!m_uutSlotsAction) {
        return;
    }
    const int slotCount = qMax(1, m_uutSlotEnabled.size());
    const int activeCount = enabledUutSlotCount(m_uutSlotEnabled);
    m_uutSlotsAction->setText(
        uiText("UUT Slots %1/%2").arg(activeCount).arg(slotCount));
    m_uutSlotsAction->setToolTip(
        uiText("%1 active UUT station(s); physical slot numbers are preserved")
            .arg(activeCount));
}

void ProductionWindow::openFieldDeviceConfiguration()
{
    if (m_viewModel->canPause() || m_viewModel->canStop()) {
        return;
    }
    if (m_selection.stationPath.trimmed().isEmpty() ||
        !QFileInfo(m_selection.stationPath).isFile()) {
        statusBar()->showMessage(
            uiText("Select a product route before configuring its devices"), 5000);
        return;
    }
    const bool restoreScanner = m_scanDialog->isVisible();
    m_scanDialog->hide();
    m_fieldDeviceDialogOpen = true;
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
    m_fieldDeviceDialogOpen = false;
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
    resetPreviewForUuts(
        m_runUutInputs.isEmpty() ? configuredPreviewUuts() : m_runUutInputs,
        configuredUutCount() > 1);
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
        m_activeSerialNumber);
    QVector<RunArtifactUutContext> artifactUuts;
    for (const auto& input : m_viewModel->activeRunUuts()) {
        auto serialNumber = input.variables.value(
            QStringLiteral("serialNumber")).toString().trimmed();
        if (serialNumber.isEmpty()) {
            serialNumber = input.variables.value(
                QStringLiteral("sn")).toString().trimmed();
        }
        artifactUuts.push_back({input.uutId, serialNumber});
    }
    const auto artifact = m_runArtifactWriter->beginForUuts(
        runArtifactSettingsFromStation(stationObject, m_selection.stationPath),
        artifactContext,
        artifactUuts);
    if (!artifact.success) {
        statusBar()->showMessage(
            uiText("Cannot create report files: %1").arg(artifact.errorMessage),
            10000);
    }
    m_terminalNodes.clear();
    m_nodeStates.clear();
    m_progress->setValue(0);
    updateProgress();
    m_elapsed.restart();
    m_elapsedTimer->start();
    statusBar()->showMessage(
        uiText("Loop run %1 of %2").arg(iteration).arg(totalIterations));
}

void ProductionWindow::beginManualRun()
{
    beginRun({});
}

QVector<RunRequest::UutInput> ProductionWindow::configuredPreviewUuts() const
{
    const int count = configuredUutCount();
    QVector<RunRequest::UutInput> inputs;
    inputs.reserve(count);
    for (int index = 0; index < count; ++index) {
        RunRequest::UutInput input;
        input.uutId = QStringLiteral("UUT-%1").arg(index + 1);
        input.slotIndex = index;
        input.enabled = m_uutSlotEnabled.value(index, true);
        inputs.push_back(std::move(input));
    }
    return inputs;
}

void ProductionWindow::resetPreviewForUuts(
    const QVector<RunRequest::UutInput>& inputs,
    bool preferOverview)
{
    if (inputs.isEmpty()) {
        return;
    }

    m_uutOverview->resetRuntimeState();
    m_overviewModel->resetForRun(m_previewReport, inputs);

    const auto selected = std::find_if(
        inputs.cbegin(), inputs.cend(), [this](const auto& input) {
            return input.enabled && input.uutId == m_selectedUutId;
        });
    const auto firstEnabled = std::find_if(
        inputs.cbegin(), inputs.cend(), [](const auto& input) {
            return input.enabled;
        });
    if (selected == inputs.cend()) {
        m_selectedUutId = firstEnabled == inputs.cend()
            ? PicoATE::Core::UutId{}
            : firstEnabled->uutId;
    }

    auto preview = m_previewReport;
    preview.state = PicoATE::Core::ExecutionState::Idle;
    preview.completed = false;
    preview.hasError = false;
    if (!preview.uuts.isEmpty()) {
        const auto templateUut = preview.uuts.first();
        preview.uuts.clear();
        for (const auto& input : inputs) {
            if (!input.enabled) {
                continue;
            }
            auto uut = templateUut;
            uut.uutId = input.uutId;
            uut.serialNumber = input.variables.value(
                QStringLiteral("serialNumber")).toString().trimmed();
            if (uut.serialNumber.isEmpty()) {
                uut.serialNumber = input.variables.value(
                    QStringLiteral("sn")).toString().trimmed();
            }
            uut.completed = false;
            uut.hasError = false;
            uut.durationMs = 0;
            preview.uuts.push_back(std::move(uut));
        }
    }

    m_resultModel->setVisibleUutId(m_selectedUutId);
    m_logProxy->setVisibleUutId(m_selectedUutId);
    m_uutOverview->setSelectedUutId(m_selectedUutId);
    m_resultModel->setReport(std::move(preview));
    m_resultView->expandAll();
    m_resultView->clearSelection();
    m_resultView->setCurrentIndex({});
    m_terminalNodes.clear();
    m_nodeStates.clear();
    rebuildUutNavigation();
    if (preferOverview && inputs.size() > 1) {
        showUutOverview();
    } else {
        showUutDetails(m_selectedUutId);
    }
    updateProgress();
}

void ProductionWindow::rebuildUutNavigation()
{
    if (!m_uutNavigationGroup || !m_uutNavigationLayout ||
        !m_overviewModel) {
        return;
    }
    const auto existingButtons = m_uutNavigationGroup->buttons();
    for (auto* button : existingButtons) {
        m_uutNavigationGroup->removeButton(button);
        m_uutNavigationLayout->removeWidget(button);
        delete button;
    }

    struct ButtonDefinition {
        PicoATE::Core::UutId uutId;
        QString serialNumber;
        QString text;
        bool enabled = true;
    };
    QVector<ButtonDefinition> definitions;
    definitions.reserve(m_overviewModel->rowCount());
    auto buttonFont = m_overviewButton->font();
    buttonFont.setBold(true);
    int commonButtonWidth = 92;
    for (int row = 0; row < m_overviewModel->rowCount(); ++row) {
        const auto entry = m_overviewModel->entryAt(row);
        if (!entry) {
            continue;
        }
        const auto prefix = QStringLiteral("UUT%1").arg(row + 1);
        const auto serialNumber = entry->serialNumber.trimmed();
        const auto text = serialNumber.isEmpty()
            ? prefix
            : QStringLiteral("%1-%2").arg(prefix, serialNumber);
        commonButtonWidth = qMax(
            commonButtonWidth,
            QFontMetrics(buttonFont).horizontalAdvance(text) + 30);
        definitions.push_back(
            {entry->uutId, serialNumber, text, entry->enabled});
    }
    commonButtonWidth = qMin(commonButtonWidth, 240);

    for (int row = 0; row < definitions.size(); ++row) {
        const auto& definition = definitions.at(row);
        auto* button = new QPushButton(
            definition.text, m_uutNavigationLayout->parentWidget());
        button->setObjectName(
            QStringLiteral("productionUutButton_%1").arg(row + 1));
        button->setProperty("productionUutSwitch", true);
        button->setProperty("uutId", definition.uutId);
        button->setProperty("slotEnabled", definition.enabled);
        button->setCheckable(true);
        button->setFont(buttonFont);
        button->setFixedWidth(commonButtonWidth);
        button->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Preferred);
        button->setEnabled(definition.enabled);
        button->setToolTip(
            definition.enabled
                ? (definition.serialNumber.isEmpty()
                       ? definition.uutId
                       : uiText("%1 | SN: %2")
                             .arg(definition.uutId,
                                  definition.serialNumber))
                : uiText("%1 | Disabled for this run").arg(definition.uutId));
        m_uutNavigationGroup->addButton(button, row + 1);
        m_uutNavigationLayout->addWidget(button);
        button->show();
    }
    if (auto* host = m_uutNavigationLayout->parentWidget()) {
        host->setMinimumWidth(
            definitions.size() * commonButtonWidth +
            qMax(0, definitions.size() - 1) * m_uutNavigationLayout->spacing() +
            m_uutNavigationLayout->contentsMargins().left() +
            m_uutNavigationLayout->contentsMargins().right());
    }

    const bool multipleSlots = m_overviewModel->rowCount() > 1;
    m_runNavigation->setVisible(multipleSlots);
    m_overviewButton->setEnabled(multipleSlots);
    const bool showingOverview = m_runStack &&
        m_runStack->currentWidget() == m_overviewPage;
    m_overviewButton->setChecked(showingOverview);
    m_uutNavigationGroup->setExclusive(false);
    for (auto* button : m_uutNavigationGroup->buttons()) {
        button->setChecked(!showingOverview && button->isEnabled() &&
                           button->property("uutId").toString() ==
                               m_selectedUutId);
    }
    m_uutNavigationGroup->setExclusive(true);
}

void ProductionWindow::showUutOverview()
{
    if (!m_runStack || !m_overviewPage ||
        !m_overviewModel || m_overviewModel->rowCount() <= 1) {
        showUutDetails(m_selectedUutId);
        return;
    }
    m_runStack->setCurrentWidget(m_overviewPage);
    m_overviewButton->setChecked(true);
    m_progressPanel->hide();
    if (m_runSidebar) {
        m_runSidebar->hide();
    }
    m_uutNavigationGroup->setExclusive(false);
    for (auto* button : m_uutNavigationGroup->buttons()) {
        button->setChecked(false);
    }
    m_uutNavigationGroup->setExclusive(true);
    m_operatorPromptPresenter->rehostActivePromptsInOverview();
    updateOverviewSummary();
}

void ProductionWindow::showUutDetails(
    const PicoATE::Core::UutId& uutId)
{
    if (!m_runStack || !m_detailPage || !m_overviewModel) {
        return;
    }
    int row = m_overviewModel->rowForUut(uutId);
    auto entry = m_overviewModel->entryAt(row);
    if (!entry || !entry->enabled) {
        for (int candidate = 0; candidate < m_overviewModel->rowCount();
             ++candidate) {
            const auto available = m_overviewModel->entryAt(candidate);
            if (available && available->enabled) {
                row = candidate;
                entry = available;
                break;
            }
        }
    }
    if (!entry || !entry->enabled) {
        return;
    }

    m_selectedUutId = entry->uutId;
    m_resultModel->setVisibleUutId(m_selectedUutId);
    m_logProxy->setVisibleUutId(m_selectedUutId);
    m_uutOverview->setSelectedUutId(m_selectedUutId);
    m_serialLabel->setText(entry->serialNumber.isEmpty()
                               ? uiText("--")
                               : entry->serialNumber);
    m_runStack->setCurrentWidget(m_detailPage);
    m_overviewButton->setChecked(false);
    m_progressPanel->show();
    if (m_runSidebar) {
        m_runSidebar->show();
    }
    for (auto* button : m_uutNavigationGroup->buttons()) {
        button->setChecked(button->isEnabled() &&
                           button->property("uutId").toString() ==
                               m_selectedUutId);
    }
    m_lastAutoFollowLine = 0;
    m_lastAutoFollowUutId = m_selectedUutId;
    m_lastAutoFollowNodeId.clear();
    m_resultView->expandAll();
    m_resultView->clearSelection();
    m_resultView->setCurrentIndex({});
    if (m_logProxy->rowCount() > 0) {
        m_logView->scrollToBottom();
    }
}

void ProductionWindow::showScanDialogWhenReady()
{
    if (m_fieldDeviceDialogOpen || m_uutSlotDialogOpen || m_scanDialog->isVisible()) {
        return;
    }
    const bool autoRouting =
        m_selection.sequenceLoadMode == SequenceLoadMode::AutoBySn;
    const bool ready = autoRouting
        ? m_viewModel->canChangeSources() && !m_runPreparationPending
        : m_viewModel->canRun();
    if (!m_selection.scanDialogEnabled || !ready) {
        return;
    }
    m_scanDialog->setValidationRules(
        autoRouting ? SnValidationRules{} : m_selection.snValidationRules);
    if (autoRouting) {
        const auto routing = PicoATE::Core::loadProductRoutingFile(
            m_selection.productRoutingPath);
        if (routing.ok()) {
            m_scanDialog->setSubmissionValidator(
                [config = routing.config](const QStringList& proposed,
                                          int currentSlot) {
                    return validateAutoRoutedScan(config, proposed,
                                                  currentSlot);
                });
        } else {
            QStringList details;
            for (const auto& error : routing.errors) {
                details.push_back(error.path.isEmpty()
                    ? error.message
                    : QStringLiteral("%1: %2").arg(error.path, error.message));
            }
            const auto message = details.join(QStringLiteral("\n"));
            m_scanDialog->setSubmissionValidator(
                [message](const QStringList&, int) {
                    return ScanSubmissionDecision{false, message, 0, {}};
                });
        }
        const int initialSlotCount = configuredUutCount();
        synchronizeUutSlotCount(initialSlotCount);
        m_scanDialog->setSlotCount(initialSlotCount);
        m_scanDialog->setSlotEnabledStates(m_uutSlotEnabled);
    } else {
        m_scanDialog->setSubmissionValidator({});
        const int slotCount = configuredUutCount();
        synchronizeUutSlotCount(slotCount);
        m_scanDialog->setSlotCount(slotCount);
        m_scanDialog->setSlotEnabledStates(m_uutSlotEnabled);
    }
    QTimer::singleShot(0, this, [this] {
        const bool ready = m_selection.sequenceLoadMode == SequenceLoadMode::AutoBySn
            ? m_viewModel->canChangeSources() && !m_runPreparationPending
            : m_viewModel->canRun();
        if (!m_fieldDeviceDialogOpen && !m_uutSlotDialogOpen &&
            m_selection.scanDialogEnabled && ready && !m_scanDialog->isVisible()) {
            m_scanDialog->showForNextScan();
        }
    });
}

void ProductionWindow::updateElapsedTime()
{
    const qint64 elapsed = m_elapsed.isValid() ? m_elapsed.elapsed() : 0;
    if (m_overviewModel) {
        m_overviewModel->setSessionElapsedMs(elapsed);
    }
    const qint64 minutes = elapsed / 60000;
    const qint64 seconds = elapsed / 1000 % 60;
    const qint64 milliseconds = elapsed % 1000;
    m_elapsedLabel->setText(QStringLiteral("%1:%2.%3")
                                .arg(minutes, 2, 10, QLatin1Char('0'))
                                .arg(seconds, 2, 10, QLatin1Char('0'))
                                .arg(milliseconds, 3, 10, QLatin1Char('0')));
    if (m_overviewSummary) {
        m_overviewSummary->setElapsedText(m_elapsedLabel->text());
    }
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
    m_passCountLabel->setText(uiText("PASS %1").arg(m_passedUnits));
    m_failCountLabel->setText(uiText("FAIL %1").arg(m_failedUnits));
    m_totalCountLabel->setText(uiText("TOTAL %1").arg(total));
    m_yieldChart->setCounts(m_passedUnits, m_failedUnits);
    const qint64 average = total > 0 ? m_totalCompletedDurationMs / total : 0;
    m_averageTimeLabel->setText(
        uiText("AVERAGE TIME %1").arg(compactDuration(average)));
    updateOverviewSummary();
}

void ProductionWindow::updateOverviewSummary()
{
    if (!m_overviewSummary) {
        return;
    }

    const auto labelText = [](const QLabel* label) {
        return label ? label->text() : QStringLiteral("--");
    };
    m_overviewSummary->setRunState(
        m_viewModel ? m_viewModel->state() : UiRunState::Empty,
        m_stopRequested);
    m_overviewSummary->setElapsedText(labelText(m_elapsedLabel));
    m_overviewSummary->setStationDetails(
        labelText(m_stationLabel), labelText(m_modelLabel),
        labelText(m_customerIdLabel), labelText(m_orderLabel),
        labelText(m_testerLabel), labelText(m_jigLabel));
    m_overviewSummary->setYieldCounts(m_passedUnits, m_failedUnits);

    int running = 0;
    int passed = 0;
    int failed = 0;
    int waiting = 0;
    if (m_overviewModel) {
        for (int row = 0; row < m_overviewModel->rowCount(); ++row) {
            const auto entry = m_overviewModel->entryAt(row);
            if (!entry) {
                continue;
            }
            switch (entry->state) {
            case UutOverviewState::Disabled:
                break;
            case UutOverviewState::Running:
            case UutOverviewState::Paused:
                ++running;
                break;
            case UutOverviewState::Passed:
                ++passed;
                break;
            case UutOverviewState::Failed:
            case UutOverviewState::Stopped:
                ++failed;
                break;
            case UutOverviewState::Waiting:
                ++waiting;
                break;
            }
        }
    }
    m_overviewSummary->setCounts(running, passed, failed, waiting);
}

void ProductionWindow::updateStationSummary()
{
    if (!m_stationLabel || !m_modelLabel || !m_customerIdLabel ||
        !m_orderLabel || !m_testerLabel || !m_jigLabel) {
        return;
    }
    const auto stationResult = PicoATE::Core::loadStationConfigFile(
        m_selection.stationPath);
    const auto stationId = stationResult.config.stationId.isEmpty()
        ? QFileInfo(m_selection.stationPath).completeBaseName()
        : stationResult.config.stationId;
    const auto& metadata = stationResult.config.metadata;
    m_stationLabel->setText(stationId.isEmpty() ? uiText("--") : stationId);
    m_modelLabel->setText(stationResult.config.model.trimmed().isEmpty()
                              ? uiText("--")
                              : stationResult.config.model.trimmed());
    m_customerIdLabel->setText(
        stationResult.config.customerId.trimmed().isEmpty()
            ? uiText("--")
            : stationResult.config.customerId.trimmed());
    m_orderLabel->setText(metadataValue(metadata, {"order", "orderNumber"}));
    m_testerLabel->setText(metadataValue(metadata, {"tester", "operator"}));
    m_jigLabel->setText(metadataValue(
        metadata, {"jigNo", "fixtureId", "fixture"}));
    updateOverviewSummary();
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
            uiText("Product routing saved. Changes apply to the next scan."), 5000);
    });
    dialog.exec();
    if (restoreScanner) {
        showScanDialogWhenReady();
    }
}

} // namespace PicoATE::Ui
