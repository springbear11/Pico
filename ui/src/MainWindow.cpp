#include "MainWindow.h"

#include "ExecutionViewModel.h"
#include "ReportExporter.h"
#include "ReportHistoryStore.h"
#include "RunnerModels.h"

#include <QAction>
#include <QFileDialog>
#include <QFormLayout>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QItemSelectionModel>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMenuBar>
#include <QPushButton>
#include <QSpinBox>
#include <QSplitter>
#include <QSortFilterProxyModel>
#include <QStatusBar>
#include <QStyle>
#include <QTableView>
#include <QTabWidget>
#include <QToolBar>
#include <QTreeView>
#include <QVBoxLayout>
#include <QWidget>

#include <optional>

namespace PicoATE::Ui {

MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent)
{
    setWindowTitle(tr("PicoATE Runner"));
    resize(1180, 760);
    setMinimumSize(900, 600);

    m_viewModel = new ExecutionViewModel(this);
    m_diagnosticModel = new DiagnosticModel(this);
    m_deviceStatusModel = new DeviceStatusModel(this);
    m_historyModel = new HistoryModel(this);
    m_historyStore = std::make_unique<ReportHistoryStore>();
    m_uutStepModel = new UutStepModel(this);
    m_attemptModel = new AttemptModel(this);
    m_measurementModel = new MeasurementModel(this);
    buildActions();
    buildLayout();
    refreshHistory();

    connect(m_viewModel,
            &ExecutionViewModel::sequencePathChanged,
            m_sequencePath,
            &QLineEdit::setText);
    connect(m_viewModel,
            &ExecutionViewModel::stationPathChanged,
            m_stationPath,
            &QLineEdit::setText);
    connect(m_viewModel,
            &ExecutionViewModel::commandAvailabilityChanged,
            this,
            &MainWindow::updateCommandState);
    connect(m_viewModel,
            &ExecutionViewModel::diagnosticsChanged,
            this,
            &MainWindow::updateDiagnostics);
    connect(m_viewModel,
            &ExecutionViewModel::reportChanged,
            this,
            &MainWindow::updateReport);
    connect(m_viewModel,
            &ExecutionViewModel::runtimeEventsReady,
            this,
            &MainWindow::applyRuntimeEvents);
    connect(m_viewModel,
            &ExecutionViewModel::stateChanged,
            this,
            [this](UiRunState state) {
                statusBar()->showMessage(uiRunStateName(state));
            });
    connect(m_resultView->selectionModel(),
            &QItemSelectionModel::currentChanged,
            this,
            [this](const QModelIndex& current) { updateStepDetails(current); });
    connect(m_attemptView->selectionModel(),
            &QItemSelectionModel::currentChanged,
            this,
            [this](const QModelIndex& current) { updateAttemptMeasurements(current); });

    updateCommandState();
    statusBar()->showMessage(uiRunStateName(m_viewModel->state()));
}

MainWindow::~MainWindow() = default;

void MainWindow::buildActions()
{
    auto* fileMenu = menuBar()->addMenu(tr("&File"));
    auto* runMenu = menuBar()->addMenu(tr("&Run"));
    auto* mainToolbar = addToolBar(tr("Runner"));
    mainToolbar->setObjectName(QStringLiteral("runnerToolbar"));
    mainToolbar->setMovable(false);

    m_openSequenceAction = new QAction(
        style()->standardIcon(QStyle::SP_DialogOpenButton),
        tr("Open Sequence"),
        this);
    m_openSequenceAction->setToolTip(tr("Open sequence JSON"));
    connect(m_openSequenceAction, &QAction::triggered, this, [this] { chooseSequence(); });

    m_openStationAction = new QAction(
        style()->standardIcon(QStyle::SP_DirOpenIcon),
        tr("Open Station"),
        this);
    m_openStationAction->setToolTip(tr("Open station JSON"));
    connect(m_openStationAction, &QAction::triggered, this, [this] { chooseStation(); });

    m_compileAction = new QAction(
        style()->standardIcon(QStyle::SP_BrowserReload),
        tr("Compile"),
        this);
    m_compileAction->setToolTip(tr("Compile selected sequence"));
    connect(m_compileAction, &QAction::triggered, m_viewModel, &ExecutionViewModel::compile);

    m_runAction = new QAction(
        style()->standardIcon(QStyle::SP_MediaPlay),
        tr("Run"),
        this);
    m_runAction->setToolTip(tr("Run compiled sequence"));
    connect(m_runAction,
            &QAction::triggered,
            this,
            [this] { m_viewModel->run(m_uutCount ? m_uutCount->value() : 1); });

    m_stopAction = new QAction(
        style()->standardIcon(QStyle::SP_MediaStop),
        tr("Stop"),
        this);
    m_stopAction->setToolTip(tr("Request graceful stop"));
    connect(m_stopAction, &QAction::triggered, this, [this] { m_viewModel->stop(); });

    auto* exitAction = new QAction(tr("E&xit"), this);
    connect(exitAction, &QAction::triggered, this, &QWidget::close);

    fileMenu->addAction(m_openSequenceAction);
    fileMenu->addAction(m_openStationAction);
    fileMenu->addSeparator();
    fileMenu->addAction(exitAction);
    runMenu->addAction(m_compileAction);
    runMenu->addAction(m_runAction);
    runMenu->addAction(m_stopAction);

    mainToolbar->addAction(m_openSequenceAction);
    mainToolbar->addAction(m_openStationAction);
    mainToolbar->addSeparator();
    auto* uutLabel = new QLabel(tr("UUTs"), mainToolbar);
    m_uutCount = new QSpinBox(mainToolbar);
    m_uutCount->setRange(1, 64);
    m_uutCount->setValue(1);
    m_uutCount->setFixedWidth(64);
    m_uutCount->setToolTip(tr("Number of UUTs in this run"));
    mainToolbar->addWidget(uutLabel);
    mainToolbar->addWidget(m_uutCount);
    mainToolbar->addSeparator();
    mainToolbar->addAction(m_compileAction);
    mainToolbar->addAction(m_runAction);
    mainToolbar->addAction(m_stopAction);
}

void MainWindow::buildLayout()
{
    auto* central = new QWidget(this);
    auto* rootLayout = new QVBoxLayout(central);
    rootLayout->setContentsMargins(12, 12, 12, 12);
    rootLayout->setSpacing(10);

    auto* sourceLayout = new QFormLayout;
    sourceLayout->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
    sourceLayout->setHorizontalSpacing(12);
    sourceLayout->setVerticalSpacing(8);

    m_sequencePath = new QLineEdit(central);
    m_sequencePath->setReadOnly(true);
    m_sequencePath->setPlaceholderText(tr("No sequence selected"));
    sourceLayout->addRow(tr("Sequence"), m_sequencePath);

    m_stationPath = new QLineEdit(central);
    m_stationPath->setReadOnly(true);
    m_stationPath->setPlaceholderText(tr("No station selected"));
    sourceLayout->addRow(tr("Station"), m_stationPath);
    rootLayout->addLayout(sourceLayout);

    auto* splitter = new QSplitter(Qt::Horizontal, central);
    splitter->setChildrenCollapsible(false);

    m_resultView = new QTreeView(splitter);
    m_resultView->setModel(m_uutStepModel);
    m_resultView->setRootIsDecorated(true);
    m_resultView->setUniformRowHeights(true);
    m_resultView->setAlternatingRowColors(true);
    m_resultView->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_resultView->setSelectionMode(QAbstractItemView::SingleSelection);
    m_resultView->header()->setSectionResizeMode(UutStepModel::NameColumn, QHeaderView::Stretch);
    m_resultView->header()->setSectionResizeMode(UutStepModel::StateColumn, QHeaderView::ResizeToContents);
    m_resultView->header()->setSectionResizeMode(UutStepModel::OutcomeColumn, QHeaderView::ResizeToContents);
    m_resultView->header()->setSectionResizeMode(UutStepModel::AttemptsColumn, QHeaderView::ResizeToContents);
    m_resultView->header()->setSectionResizeMode(UutStepModel::LoopColumn, QHeaderView::Stretch);

    auto* details = new QTabWidget(splitter);
    m_attemptView = new QTableView(details);
    m_attemptView->setModel(m_attemptModel);
    m_attemptView->setAlternatingRowColors(true);
    m_attemptView->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_attemptView->setSelectionMode(QAbstractItemView::SingleSelection);
    m_attemptView->verticalHeader()->setVisible(false);
    m_attemptView->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    m_attemptView->horizontalHeader()->setStretchLastSection(true);
    details->addTab(m_attemptView, tr("Attempts"));

    m_measurementView = new QTableView(details);
    m_measurementView->setModel(m_measurementModel);
    m_measurementView->setAlternatingRowColors(true);
    m_measurementView->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_measurementView->setSelectionMode(QAbstractItemView::SingleSelection);
    m_measurementView->verticalHeader()->setVisible(false);
    m_measurementView->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    m_measurementView->horizontalHeader()->setSectionResizeMode(
        MeasurementModel::NameColumn,
        QHeaderView::Stretch);
    details->addTab(m_measurementView, tr("Measurements"));

    m_deviceStatusView = new QTableView(details);
    m_deviceStatusView->setModel(m_deviceStatusModel);
    m_deviceStatusView->setAlternatingRowColors(true);
    m_deviceStatusView->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_deviceStatusView->setSelectionMode(QAbstractItemView::SingleSelection);
    m_deviceStatusView->verticalHeader()->setVisible(false);
    m_deviceStatusView->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    m_deviceStatusView->horizontalHeader()->setSectionResizeMode(
        DeviceStatusModel::MessageColumn,
        QHeaderView::Stretch);
    details->addTab(m_deviceStatusView, tr("Devices"));

    auto* historyPage = new QWidget(details);
    auto* historyLayout = new QVBoxLayout(historyPage);
    historyLayout->setContentsMargins(0, 0, 0, 0);
    historyLayout->setSpacing(6);
    auto* historyCommands = new QHBoxLayout;
    m_historyFilter = new QLineEdit(historyPage);
    m_historyFilter->setPlaceholderText(tr("Filter by sequence, UUT or result"));
    historyCommands->addWidget(m_historyFilter, 1);
    auto* openHistory = new QPushButton(
        style()->standardIcon(QStyle::SP_DialogOpenButton), tr("Open"), historyPage);
    auto* exportJson = new QPushButton(
        style()->standardIcon(QStyle::SP_DialogSaveButton), tr("JSON"), historyPage);
    auto* exportCsv = new QPushButton(
        style()->standardIcon(QStyle::SP_DialogSaveButton), tr("CSV"), historyPage);
    openHistory->setToolTip(tr("Open selected report"));
    exportJson->setToolTip(tr("Export selected report as JSON"));
    exportCsv->setToolTip(tr("Export selected report as CSV"));
    historyCommands->addWidget(openHistory);
    historyCommands->addWidget(exportJson);
    historyCommands->addWidget(exportCsv);
    historyLayout->addLayout(historyCommands);

    m_historyProxy = new QSortFilterProxyModel(this);
    m_historyProxy->setSourceModel(m_historyModel);
    m_historyProxy->setFilterCaseSensitivity(Qt::CaseInsensitive);
    m_historyProxy->setFilterKeyColumn(-1);
    m_historyProxy->setDynamicSortFilter(true);
    m_historyView = new QTableView(historyPage);
    m_historyView->setModel(m_historyProxy);
    m_historyView->setAlternatingRowColors(true);
    m_historyView->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_historyView->setSelectionMode(QAbstractItemView::SingleSelection);
    m_historyView->setSortingEnabled(true);
    m_historyView->verticalHeader()->setVisible(false);
    m_historyView->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    m_historyView->horizontalHeader()->setSectionResizeMode(
        HistoryModel::UutsColumn, QHeaderView::Stretch);
    historyLayout->addWidget(m_historyView, 1);
    details->addTab(historyPage, tr("History"));

    connect(m_historyFilter,
            &QLineEdit::textChanged,
            m_historyProxy,
            &QSortFilterProxyModel::setFilterFixedString);
    connect(openHistory, &QPushButton::clicked, this, [this] { loadSelectedHistory(); });
    connect(exportJson, &QPushButton::clicked, this, [this] { exportSelectedHistory(false); });
    connect(exportCsv, &QPushButton::clicked, this, [this] { exportSelectedHistory(true); });
    connect(m_historyView, &QTableView::doubleClicked, this, [this] { loadSelectedHistory(); });

    m_diagnosticView = new QTableView(details);
    m_diagnosticView->setModel(m_diagnosticModel);
    m_diagnosticView->setAlternatingRowColors(true);
    m_diagnosticView->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_diagnosticView->setSelectionMode(QAbstractItemView::SingleSelection);
    m_diagnosticView->verticalHeader()->setVisible(false);
    m_diagnosticView->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    m_diagnosticView->horizontalHeader()->setSectionResizeMode(
        DiagnosticModel::MessageColumn,
        QHeaderView::Stretch);
    details->addTab(m_diagnosticView, tr("Diagnostics"));

    splitter->addWidget(m_resultView);
    splitter->addWidget(details);
    splitter->setStretchFactor(0, 3);
    splitter->setStretchFactor(1, 2);
    rootLayout->addWidget(splitter, 1);

    setCentralWidget(central);
}

void MainWindow::chooseSequence()
{
    const QString path = QFileDialog::getOpenFileName(
        this,
        tr("Open Sequence"),
        QString(),
        tr("Sequence JSON (*.json);;All Files (*.*)"));
    if (!path.isEmpty()) {
        m_viewModel->setSequencePath(path);
    }
}

void MainWindow::chooseStation()
{
    const QString path = QFileDialog::getOpenFileName(
        this,
        tr("Open Station"),
        QString(),
        tr("Station JSON (*.json);;All Files (*.*)"));
    if (!path.isEmpty()) {
        m_viewModel->setStationPath(path);
    }
}

void MainWindow::updateCommandState()
{
    const bool canChangeSources = m_viewModel->canChangeSources();
    m_openSequenceAction->setEnabled(canChangeSources);
    m_openStationAction->setEnabled(canChangeSources);
    m_compileAction->setEnabled(m_viewModel->canCompile());
    m_runAction->setEnabled(m_viewModel->canRun());
    m_stopAction->setEnabled(m_viewModel->canStop());
    m_uutCount->setEnabled(canChangeSources);
}

void MainWindow::updateDiagnostics()
{
    m_diagnosticModel->setDiagnostics(m_viewModel->diagnostics());
    m_diagnosticView->resizeColumnsToContents();
}

void MainWindow::updateReport()
{
    const auto report = m_viewModel->report();
    if (report.planId.isEmpty() && report.uuts.isEmpty()) {
        m_currentReportSaved = false;
    } else if (report.completed && !m_currentReportSaved) {
        const auto saved = m_historyStore->save(report);
        m_currentReportSaved = true;
        if (saved.success) {
            refreshHistory();
        } else {
            statusBar()->showMessage(tr("Failed to save report: %1").arg(saved.errorMessage));
        }
    }
    displayReport(report);
}

void MainWindow::displayReport(const PicoATE::Core::ExecutionReport& report)
{
    m_uutStepModel->setReport(report);
    if (report.planId.isEmpty() && report.uuts.isEmpty()) {
        m_deviceStatusModel->clear();
    }
    m_attemptModel->setStep(std::nullopt);
    m_measurementModel->setMeasurements({});
    m_resultView->expandAll();
    selectInitialResult();
}

void MainWindow::applyRuntimeEvents(
    const QVector<PicoATE::Core::RuntimeEvent>& events)
{
    PicoATE::Core::UutId selectedUutId;
    PicoATE::Core::NodeId selectedStepId;
    const auto current = m_resultView->currentIndex();
    const auto selectedUut = m_uutStepModel->uutAt(current);
    const auto selectedStep = m_uutStepModel->stepAt(current);
    if (selectedUut) {
        selectedUutId = selectedUut->uutId;
    }
    if (selectedStep) {
        selectedStepId = selectedStep->stepId;
    }

    m_uutStepModel->applyRuntimeEvents(events);
    m_deviceStatusModel->applyRuntimeEvents(events);
    m_resultView->expandAll();

    const auto restored = m_uutStepModel->indexForStep(selectedUutId, selectedStepId);
    if (restored.isValid()) {
        m_resultView->setCurrentIndex(restored);
        updateStepDetails(restored);
    } else if (!m_resultView->currentIndex().isValid()) {
        selectInitialResult();
    } else {
        updateStepDetails(m_resultView->currentIndex());
    }
}

void MainWindow::updateStepDetails(const QModelIndex& index)
{
    const auto step = m_uutStepModel->stepAt(index);
    m_attemptModel->setStep(step);
    m_measurementModel->setMeasurements(step ? step->measurements
                                             : QVector<PicoATE::Core::MeasurementResult>{});
    if (m_attemptModel->rowCount() > 0) {
        const auto lastAttempt = m_attemptModel->index(m_attemptModel->rowCount() - 1, 0);
        m_attemptView->setCurrentIndex(lastAttempt);
        updateAttemptMeasurements(lastAttempt);
    }
}

void MainWindow::updateAttemptMeasurements(const QModelIndex& index)
{
    const auto attempt = m_attemptModel->attemptAt(index.row());
    if (attempt) {
        m_measurementModel->setMeasurements(attempt->measurements);
    }
}

void MainWindow::selectInitialResult()
{
    if (m_uutStepModel->rowCount() == 0) {
        return;
    }
    const auto uut = m_uutStepModel->index(0, 0);
    if (m_uutStepModel->rowCount(uut) == 0) {
        m_resultView->setCurrentIndex(uut);
        return;
    }
    m_resultView->setCurrentIndex(m_uutStepModel->index(0, 0, uut));
}

void MainWindow::refreshHistory()
{
    QString errorMessage;
    m_historyModel->setEntries(m_historyStore->entries(&errorMessage));
    if (!errorMessage.isEmpty()) {
        statusBar()->showMessage(tr("Failed to load report history: %1").arg(errorMessage));
    }
}

std::optional<ReportHistoryEntry> MainWindow::selectedHistoryEntry() const
{
    const auto proxyIndex = m_historyView->currentIndex();
    if (!proxyIndex.isValid()) {
        return std::nullopt;
    }
    return m_historyModel->entryAt(m_historyProxy->mapToSource(proxyIndex).row());
}

void MainWindow::loadSelectedHistory()
{
    const auto entry = selectedHistoryEntry();
    if (!entry) {
        statusBar()->showMessage(tr("Select a report first"));
        return;
    }
    const auto loaded = m_historyStore->load(entry->id);
    if (!loaded.ok()) {
        const auto detail = !loaded.errorMessage.isEmpty()
            ? loaded.errorMessage
            : loaded.parseErrors.first().message;
        statusBar()->showMessage(tr("Failed to load report: %1").arg(detail));
        return;
    }
    displayReport(loaded.report);
    statusBar()->showMessage(tr("Loaded report %1").arg(entry->id));
}

void MainWindow::exportSelectedHistory(bool csv)
{
    const auto entry = selectedHistoryEntry();
    if (!entry) {
        statusBar()->showMessage(tr("Select a report first"));
        return;
    }
    const auto loaded = m_historyStore->load(entry->id);
    if (!loaded.ok()) {
        statusBar()->showMessage(tr("Failed to load selected report"));
        return;
    }
    const auto suffix = csv ? QStringLiteral("csv") : QStringLiteral("json");
    const auto path = QFileDialog::getSaveFileName(
        this,
        csv ? tr("Export CSV Report") : tr("Export JSON Report"),
        entry->id + '.' + suffix,
        csv ? tr("CSV Report (*.csv)") : tr("JSON Report (*.json)"));
    if (path.isEmpty()) {
        return;
    }
    const auto result = csv
        ? ReportExporter::saveCsv(path, loaded.report)
        : ReportExporter::saveJson(path, loaded.report);
    statusBar()->showMessage(result.success
                                 ? tr("Report exported")
                                 : tr("Export failed: %1").arg(result.errorMessage));
}

} // namespace PicoATE::Ui
