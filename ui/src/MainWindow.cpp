#include "MainWindow.h"

#include "ExecutionViewModel.h"
#include "OnOffControl.h"
#include "PluginCatalog.h"
#include "PluginFunctionModel.h"
#include "ProportionalHeaderView.h"
#include "ReportExporter.h"
#include "ReportHistoryStore.h"
#include "RunnerModels.h"
#include "ScanDialog.h"
#include "SequenceDocument.h"
#include "SequenceTreeModel.h"
#include "StationDeviceModel.h"
#include "StationDocument.h"
#include "StationPropertyEditor.h"
#include "StationSettingsEditor.h"
#include "StepPropertyEditor.h"

#include <QAction>
#include <QCloseEvent>
#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QFont>
#include <QFrame>
#include <QFormLayout>
#include <QGuiApplication>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QItemSelectionModel>
#include <QJsonArray>
#include <QKeySequence>
#include <QLabel>
#include <QMessageBox>
#include <QLineEdit>
#include <QMenu>
#include <QMenuBar>
#include <QPainter>
#include <QPushButton>
#include <QProgressBar>
#include <QRegularExpression>
#include <QScreen>
#include <QSettings>
#include <QSizePolicy>
#include <QSpinBox>
#include <QSplitter>
#include <QSortFilterProxyModel>
#include <QStatusBar>
#include <QStyledItemDelegate>
#include <QStyle>
#include <QTableView>
#include <QTabWidget>
#include <QTimer>
#include <QToolBar>
#include <QTreeView>
#include <QUndoStack>
#include <QVBoxLayout>
#include <QWidget>

#include "PicoATE/Core/StationConfig.h"

#include <optional>
#include <initializer_list>
#include <algorithm>

namespace PicoATE::Ui {

namespace {

constexpr int MaxRecentFiles = 8;

void installProportionalHeader(QTableView* view, QVector<int> weights)
{
    auto* header = new ProportionalHeaderView(view);
    view->setHorizontalHeader(header);
    header->setSectionWeights(std::move(weights));
}

void installProportionalHeader(QTreeView* view, QVector<int> weights)
{
    auto* header = new ProportionalHeaderView(view);
    view->setHeader(header);
    header->setSectionWeights(std::move(weights));
}

void polishReadableTreeView(QTreeView* view)
{
    view->setIndentation(22);

    auto font = view->font();
    font.setFamily(QStringLiteral("Microsoft YaHei UI"));
    if (font.pointSizeF() > 0.0) {
        font.setPointSizeF(font.pointSizeF() + 0.5);
    }
    font.setWeight(QFont::Medium);
    view->setFont(font);
}

class DragHandleDelegate final : public QStyledItemDelegate
{
public:
    explicit DragHandleDelegate(QObject* parent = nullptr)
        : QStyledItemDelegate(parent)
    {
        setObjectName(QStringLiteral("dragHandleDelegate"));
    }

    void paint(QPainter* painter,
               const QStyleOptionViewItem& option,
               const QModelIndex& index) const override
    {
        QStyledItemDelegate::paint(painter, option, index);
        if (index.column() != 0 ||
            !(index.flags() & Qt::ItemIsDragEnabled)) {
            return;
        }

        const QColor color = option.state & QStyle::State_Selected
            ? QColor(QStringLiteral("#6f99b4"))
            : (option.state & QStyle::State_MouseOver
                   ? QColor(QStringLiteral("#86aabd"))
                   : QColor(QStringLiteral("#b2c6d2")));
        QPen pen(color, 1.5, Qt::SolidLine, Qt::RoundCap);
        painter->save();
        painter->setPen(pen);
        const int right = option.rect.right() - 8;
        const int left = right - 11;
        const int center = option.rect.center().y();
        for (const int offset : {-4, 0, 4}) {
            painter->drawLine(left, center + offset, right, center + offset);
        }
        painter->restore();
    }
};

QString normalizedRecentPath(const QString& filePath)
{
    return QFileInfo(filePath).absoluteFilePath();
}

bool isVisibleOnAnyScreen(const QRect& geometry)
{
    for (const auto* screen : QGuiApplication::screens()) {
        if (screen && screen->availableGeometry().intersects(geometry)) {
            return true;
        }
    }
    return false;
}

void applyDefaultWindowGeometry(QWidget& window)
{
    window.resize(1180, 760);
    if (const auto* screen = QGuiApplication::primaryScreen()) {
        const auto available = screen->availableGeometry();
        window.move(available.center() - window.rect().center());
    }
}

bool adminIsTerminalActivation(PicoATE::Core::ActivationState state)
{
    using PicoATE::Core::ActivationState;
    return state == ActivationState::Passed ||
           state == ActivationState::Failed ||
           state == ActivationState::Error ||
           state == ActivationState::Timeout ||
           state == ActivationState::Skipped ||
           state == ActivationState::Cancelled;
}

QString adminRunStateText(UiRunState state)
{
    switch (state) {
    case UiRunState::Starting:
    case UiRunState::Running:
    case UiRunState::Pausing:
    case UiRunState::Paused:
    case UiRunState::Stopping:
        return QObject::tr("RUNNING");
    case UiRunState::Completed:
        return QObject::tr("PASS");
    case UiRunState::Failed:
    case UiRunState::CompileFailed:
        return QObject::tr("FAIL");
    case UiRunState::Ready:
        return QObject::tr("READY");
    default:
        return QObject::tr("WAITING");
    }
}

QString adminRunStateStyle(UiRunState state)
{
    switch (state) {
    case UiRunState::Starting:
    case UiRunState::Running:
    case UiRunState::Pausing:
    case UiRunState::Paused:
    case UiRunState::Stopping:
        return QStringLiteral("background:#ffe69a;color:#5b4500;border:1px solid #d6b84b;border-radius:4px;");
    case UiRunState::Completed:
        return QStringLiteral("background:#d9f2c7;color:#1f6b35;border:1px solid #8fc775;border-radius:4px;");
    case UiRunState::Failed:
    case UiRunState::CompileFailed:
        return QStringLiteral("background:#ffd6d2;color:#9c2727;border:1px solid #d98d86;border-radius:4px;");
    default:
        return QStringLiteral("background:#e6e8eb;color:#26323b;border:1px solid #bdc5cc;border-radius:4px;");
    }
}

QString stationMetadataValue(const QVariantMap& metadata,
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

QString firstExistingPath(const QStringList& candidates)
{
    for (const auto& candidate : candidates) {
        if (QFileInfo::exists(candidate)) {
            return QFileInfo(candidate).absoluteFilePath();
        }
    }
    return {};
}

QString firstDescribeCapableNativeHost(const QStringList& candidates)
{
    for (const auto& candidate : candidates) {
        if (PluginCatalog::nativeHostSupportsDescribe(candidate)) {
            return QFileInfo(candidate).absoluteFilePath();
        }
    }
    return {};
}

} // namespace

MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent)
{
    setWindowTitle(tr("PicoATE Runner"));
    resize(1180, 760);
    setMinimumSize(900, 600);

    m_viewModel = new ExecutionViewModel(this);
    m_sequenceDocument = new SequenceDocument(this);
    m_sequenceTreeModel = new SequenceTreeModel(m_sequenceDocument, this);
    m_pluginFunctionModel = new PluginFunctionModel(this);
    m_stationDocument = new StationDocument(this);
    m_stationDeviceModel = new StationDeviceModel(m_stationDocument, this);
    m_editorDiagnosticModel = new DiagnosticModel(this);
    m_stationDiagnosticModel = new DiagnosticModel(this);
    m_diagnosticModel = new DiagnosticModel(this);
    m_deviceStatusModel = new DeviceStatusModel(this);
    m_historyModel = new HistoryModel(this);
    m_historyStore = std::make_unique<ReportHistoryStore>();
    m_uutStepModel = new UutStepModel(this);
    m_uutStepModel->setSingleUutPhaseLayout(true);
    m_attemptModel = new AttemptModel(this);
    m_measurementModel = new MeasurementModel(this);
    m_runtimeLogModel = new RuntimeLogModel(this);
    m_runtimeTimelineModel = new RuntimeTimelineModel(this);
    m_debugSnapshotModel = new DebugSnapshotModel(this);
    m_scanDialog = new ScanDialog(this);
    buildActions();
    buildLayout();
    loadPluginRegistry();
    restoreUiSettings();
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
            &ExecutionViewModel::compileSummaryChanged,
            this,
            &MainWindow::updateCompilePreview);
    connect(m_viewModel,
            &ExecutionViewModel::reportChanged,
            this,
            &MainWindow::updateReport);
    connect(m_viewModel,
            &ExecutionViewModel::debugSnapshotChanged,
            this,
            &MainWindow::updateDebugSnapshot);
    connect(m_viewModel,
            &ExecutionViewModel::runtimeEventsReady,
            this,
            &MainWindow::applyRuntimeEvents);
    connect(m_viewModel,
            &ExecutionViewModel::stateChanged,
            this,
            [this](UiRunState state) {
                if (state == UiRunState::Starting) {
                    m_runtimeLogModel->clear();
                    m_runtimeTimelineModel->clear();
                    m_sequenceTreeModel->setCurrentDebugNodePath({});
                }
                if (state == UiRunState::Completed || state == UiRunState::Failed) {
                    m_sequenceTreeModel->setCurrentDebugNodePath({});
                }
                updateAdminRunState(state);
                statusBar()->showMessage(uiRunStateName(state));
            });
    connect(m_scanDialog,
            &ScanDialog::barcodeAccepted,
            this,
            &MainWindow::runScannedUut);
    connect(m_viewModel,
            &ExecutionViewModel::deviceConnectionTestStarted,
            this,
            [this](const QString& deviceId) {
                m_stationDeviceModel->markConnectionTesting(deviceId);
                updateStationEditor();
                statusBar()->showMessage(
                    tr("Testing connection: %1").arg(deviceId));
            });
    connect(m_viewModel,
            &ExecutionViewModel::deviceConnectionTestFinished,
            this,
            [this](const DeviceConnectionTestResult& result) {
                m_stationDeviceModel->setConnectionTestResult(result);
                updateStationEditor();
                const auto summary = result.passed()
                    ? tr("Connection passed: %1 (%2 ms)")
                          .arg(result.deviceId).arg(result.elapsedMs)
                    : tr("Connection %1: %2 - %3")
                          .arg(deviceConnectionTestOutcomeName(result.outcome),
                               result.deviceId,
                               result.errorMessage);
                statusBar()->showMessage(summary, 8000);
            });
    connect(m_sequenceDocument,
            &SequenceDocument::documentChanged,
            this,
            [this] {
                synchronizeSequenceSnapshot();
                updateSequenceEditor();
            });
    connect(m_sequenceDocument,
            &SequenceDocument::diagnosticsChanged,
            this,
            &MainWindow::updateSequenceEditor);
    connect(m_sequenceDocument,
            &SequenceDocument::filePathChanged,
            this,
            [this] {
                synchronizeSequenceSnapshot();
                updateWindowTitle();
            });
    connect(m_sequenceDocument,
            &SequenceDocument::modifiedChanged,
            this,
            [this] {
                updateWindowTitle();
                updateCommandState();
            });
    connect(m_sequenceTreeView->selectionModel(),
            &QItemSelectionModel::currentChanged,
            this,
            [this](const QModelIndex& current) {
                const auto path = m_sequenceTreeModel->pathForIndex(current);
                if (path.isValid()) {
                    m_selectedSequencePath = path;
                }
                m_stepPropertyEditor->setCurrentItem(path);
                updateCommandState();
            });
    connect(m_pluginFunctionView->selectionModel(),
            &QItemSelectionModel::currentChanged,
            this,
            [this](const QModelIndex& current) {
                const auto preview = m_pluginFunctionModel->stepTemplate(current);
                if (!preview.isEmpty()) {
                    m_stepPropertyEditor->setPreviewObject(preview);
                }
            });
    connect(m_sequenceTreeView->selectionModel(),
            &QItemSelectionModel::selectionChanged,
            this,
            [this] { updateCommandState(); });
    connect(m_sequenceTreeModel, &SequenceTreeModel::itemMoved,
            this,
            [this](const SequenceItemPath&, const SequenceItemPath& to) {
                m_selectedSequencePath = to;
                updateSequenceEditor();
            });
    connect(m_sequenceTreeModel, &SequenceTreeModel::itemInserted,
            this, [this](const SequenceItemPath& path) {
                m_selectedSequencePath = path;
                const auto index = m_sequenceTreeModel->indexForPath(path);
                if (index.isValid()) {
                    m_sequenceTreeView->setCurrentIndex(index);
                    m_sequenceTreeView->scrollTo(
                        index, QAbstractItemView::PositionAtCenter);
                    m_stepPropertyEditor->setCurrentItem(path);
                }
                updateSequenceEditor();
            });
    connect(m_editorDiagnosticView, &QTableView::clicked,
            this, &MainWindow::focusSequenceDiagnostic);
    connect(m_sequenceDocument->undoStack(), &QUndoStack::canUndoChanged,
            this, [this] { updateCommandState(); });
    connect(m_sequenceDocument->undoStack(), &QUndoStack::canRedoChanged,
            this, [this] { updateCommandState(); });
    connect(m_stationDocument,
            &StationDocument::documentChanged,
            this,
            [this] {
                synchronizeStationSnapshot();
                updateStationEditor();
                updatePluginDeviceBindings();
            });
    connect(m_stationDocument,
            &StationDocument::diagnosticsChanged,
            this,
            &MainWindow::updateStationEditor);
    connect(m_stationDocument,
            &StationDocument::filePathChanged,
            this,
            [this] {
                synchronizeStationSnapshot();
                updateWindowTitle();
            });
    connect(m_stationDocument,
            &StationDocument::modifiedChanged,
            this,
            [this] {
                updateWindowTitle();
                updateCommandState();
            });
    connect(m_stationDeviceView->selectionModel(),
            &QItemSelectionModel::currentChanged,
            this,
            [this](const QModelIndex& current) {
                m_selectedStationDeviceRow = current.isValid() ? current.row() : -1;
                m_stationPropertyEditor->setCurrentDevice(m_selectedStationDeviceRow);
                updateCommandState();
            });
    connect(m_stationDiagnosticView, &QTableView::clicked,
            this, &MainWindow::focusStationDiagnostic);
    connect(m_stationDocument->undoStack(), &QUndoStack::canUndoChanged,
            this, [this] { updateCommandState(); });
    connect(m_stationDocument->undoStack(), &QUndoStack::canRedoChanged,
            this, [this] { updateCommandState(); });
    connect(m_resultView->selectionModel(),
            &QItemSelectionModel::currentChanged,
            this,
            [this](const QModelIndex& current) { updateStepDetails(current); });
    connect(m_attemptView->selectionModel(),
            &QItemSelectionModel::currentChanged,
            this,
            [this](const QModelIndex& current) { updateAttemptMeasurements(current); });

    updateWindowTitle();

    updateCommandState();
    statusBar()->showMessage(uiRunStateName(m_viewModel->state()));
}

MainWindow::~MainWindow()
{
    beginShutdown();
}

bool MainWindow::openSequenceFile(const QString& filePath)
{
    if (!m_sequenceDocument->load(filePath)) {
        updateSequenceEditor();
        return false;
    }
    m_sequenceTreeModel->clearBreakpoints();
    m_sequenceTreeModel->setCurrentDebugNodePath({});
    m_selectedSequencePath = {};
    synchronizeSequenceSnapshot();
    updateSequenceEditor();
    if (m_adminSequenceLabel) {
        m_adminSequenceLabel->setText(QFileInfo(filePath).fileName());
        m_adminSequenceLabel->setToolTip(QFileInfo(filePath).absoluteFilePath());
    }
    addRecentSequence(filePath);
    return true;
}

void MainWindow::showRunPage()
{
    if (m_workspaceTabs && m_workspaceTabs->count() > 0) {
        m_workspaceTabs->setCurrentIndex(0);
    }
}

void MainWindow::closeEvent(QCloseEvent* event)
{
    if (maybeSaveSequence() && maybeSaveStation()) {
        saveUiSettings();
        beginShutdown();
        event->accept();
    } else {
        event->ignore();
    }
}

bool MainWindow::openStationFile(const QString& filePath)
{
    if (!m_stationDocument->load(filePath)) {
        updateStationEditor();
        return false;
    }
    m_selectedStationDeviceRow = m_stationDocument->deviceCount() > 0 ? 0 : -1;
    synchronizeStationSnapshot();
    updateStationEditor();
    updateAdminStationSummary();
    addRecentStation(filePath);
    return true;
}

void MainWindow::beginShutdown()
{
    if (m_shuttingDown) {
        return;
    }
    m_shuttingDown = true;

    // Undo-stack and worker callbacks may otherwise re-enter this window while
    // its child objects are being destroyed.
    QObject::disconnect(nullptr, nullptr, this, nullptr);
    if (m_sequenceTreeView) {
        m_sequenceTreeView->setModel(nullptr);
    }
    if (m_stepPropertyEditor) {
        m_stepPropertyEditor->setCurrentItem({});
    }
    if (m_stationDeviceView) {
        m_stationDeviceView->setModel(nullptr);
    }
    if (m_stationPropertyEditor) {
        m_stationPropertyEditor->setCurrentDevice(-1);
    }
    if (m_viewModel) {
        m_viewModel->shutdown();
    }
}

bool MainWindow::maybeSaveSequence()
{
    if (!m_sequenceDocument || !m_sequenceDocument->isModified()) {
        return true;
    }

    const auto choice = QMessageBox::warning(
        this,
        tr("Unsaved Sequence"),
        tr("Save changes to %1?").arg(m_sequenceDocument->displayName()),
        QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel,
        QMessageBox::Save);
    if (choice == QMessageBox::Cancel) {
        return false;
    }
    if (choice == QMessageBox::Discard) {
        return true;
    }
    return saveSequence();
}

bool MainWindow::saveSequence()
{
    if (!m_sequenceDocument || m_sequenceDocument->isEmpty()) {
        return false;
    }
    if (m_sequenceDocument->filePath().isEmpty()) {
        return saveSequenceAs();
    }

    QString errorMessage;
    if (!m_sequenceDocument->save(&errorMessage)) {
        QMessageBox::critical(this, tr("Save Sequence"), errorMessage);
        return false;
    }
    statusBar()->showMessage(tr("Sequence saved"), 3000);
    updateWindowTitle();
    updateCommandState();
    return true;
}

bool MainWindow::saveSequenceAs()
{
    if (!m_sequenceDocument || m_sequenceDocument->isEmpty()) {
        return false;
    }

    const auto path = QFileDialog::getSaveFileName(
        this,
        tr("Save Sequence As"),
        m_sequenceDocument->filePath(),
        tr("Sequence JSON (*.json);;All Files (*.*)"));
    if (path.isEmpty()) {
        return false;
    }

    QString errorMessage;
    if (!m_sequenceDocument->saveAs(path, &errorMessage)) {
        QMessageBox::critical(this, tr("Save Sequence"), errorMessage);
        return false;
    }
    addRecentSequence(path);
    synchronizeSequenceSnapshot();
    statusBar()->showMessage(tr("Sequence saved"), 3000);
    updateWindowTitle();
    updateCommandState();
    return true;
}

bool MainWindow::maybeSaveStation()
{
    if (!m_stationDocument || !m_stationDocument->isModified()) {
        return true;
    }
    const auto choice = QMessageBox::warning(
        this,
        tr("Unsaved Station"),
        tr("Save changes to %1?").arg(m_stationDocument->displayName()),
        QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel,
        QMessageBox::Save);
    if (choice == QMessageBox::Cancel) {
        return false;
    }
    if (choice == QMessageBox::Discard) {
        return true;
    }
    return saveStation();
}

bool MainWindow::saveStation()
{
    if (!m_stationDocument || m_stationDocument->isEmpty()) {
        return false;
    }
    if (m_stationDocument->filePath().isEmpty()) {
        return saveStationAs();
    }
    QString errorMessage;
    if (!m_stationDocument->save(&errorMessage)) {
        QMessageBox::critical(this, tr("Save Station"), errorMessage);
        return false;
    }
    statusBar()->showMessage(tr("Station saved"), 3000);
    updateWindowTitle();
    updateCommandState();
    return true;
}

bool MainWindow::saveStationAs()
{
    if (!m_stationDocument || m_stationDocument->isEmpty()) {
        return false;
    }
    const auto path = QFileDialog::getSaveFileName(
        this,
        tr("Save Station As"),
        m_stationDocument->filePath(),
        tr("Station JSON (*.json);;All Files (*.*)"));
    if (path.isEmpty()) {
        return false;
    }
    QString errorMessage;
    if (!m_stationDocument->saveAs(path, &errorMessage)) {
        QMessageBox::critical(this, tr("Save Station"), errorMessage);
        return false;
    }
    addRecentStation(path);
    synchronizeStationSnapshot();
    statusBar()->showMessage(tr("Station saved"), 3000);
    updateWindowTitle();
    updateCommandState();
    return true;
}

void MainWindow::addSequenceStep()
{
    const auto current = m_sequenceTreeView->currentIndex();
    auto selectedPath = m_sequenceTreeModel->pathForIndex(current);
    if (!selectedPath.isValid()) {
        return;
    }

    SequenceItemPath parentPath = selectedPath;
    int row = -1;
    if (!selectedPath.isGroup() &&
        !m_sequenceDocument->canContainSteps(selectedPath)) {
        row = parentPath.stepIndices.takeLast() + 1;
    }

    const int currentCount = m_sequenceDocument->objectAt(parentPath)
                                 .value("steps").toArray().size();
    const int insertionRow = row < 0 ? currentCount : qBound(0, row, currentCount);
    m_selectedSequencePath = parentPath;
    m_selectedSequencePath.stepIndices.push_back(insertionRow);
    m_sequenceDocument->insertStep(parentPath, insertionRow);
}

void MainWindow::deleteSequenceStep()
{
    const auto paths = selectedSequenceStepPaths();
    if (paths.isEmpty()) {
        return;
    }

    auto parentPath = paths.first();
    const int row = parentPath.stepIndices.takeLast();
    m_selectedSequencePath = parentPath;
    if (row > 0) {
        m_selectedSequencePath.stepIndices.push_back(row - 1);
    }
    m_sequenceDocument->removeSteps(paths);
}

void MainWindow::setSelectedSequenceStepsEnabled(bool enabled)
{
    const auto paths = selectedSequenceStepPaths();
    if (!m_sequenceDocument->setStepsEnabled(paths, enabled)) {
        statusBar()->showMessage(
            enabled ? tr("Selected steps are already enabled")
                    : tr("Selected steps are already disabled"),
            3000);
    }
}

void MainWindow::duplicateSequenceStep()
{
    const auto paths = selectedSequenceStepPaths();
    if (paths.isEmpty()) {
        return;
    }

    m_selectedSequencePath = paths.first();
    ++m_selectedSequencePath.stepIndices.last();
    if (!m_sequenceDocument->duplicateSteps(paths)) {
        statusBar()->showMessage(tr("Unable to duplicate selected steps"), 4000);
    }
}

QVector<SequenceItemPath> MainWindow::selectedSequenceStepPaths() const
{
    QVector<SequenceItemPath> result;
    if (!m_sequenceTreeView || !m_sequenceTreeModel ||
        !m_sequenceTreeView->selectionModel()) {
        return result;
    }
    const auto indexes = m_sequenceTreeView->selectionModel()->selectedRows(
        SequenceTreeModel::NameColumn);
    result.reserve(indexes.size());
    for (const auto& index : indexes) {
        const auto path = m_sequenceTreeModel->pathForIndex(index);
        if (path.isValid() && !path.isGroup()) {
            result.push_back(path);
        }
    }
    return result;
}

void MainWindow::wrapSelectedStepsInTestItem()
{
    const auto paths = selectedSequenceStepPaths();
    SequenceItemPath testItemPath;
    if (!m_sequenceDocument->wrapStepsInTestItem(paths, &testItemPath)) {
        statusBar()->showMessage(
            tr("Select one or more contiguous steps under the same parent"),
            4000);
        return;
    }
    m_selectedSequencePath = testItemPath;
    const auto index = m_sequenceTreeModel->indexForPath(testItemPath);
    if (index.isValid()) {
        m_sequenceTreeView->selectionModel()->select(
            index,
            QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Rows);
        m_sequenceTreeView->setCurrentIndex(index);
        m_sequenceTreeView->scrollTo(index, QAbstractItemView::PositionAtCenter);
        m_sequenceTreeView->expand(index);
        m_stepPropertyEditor->setCurrentItem(testItemPath);
    }
}

void MainWindow::moveSequenceStep(int offset)
{
    const auto path = m_sequenceTreeModel->pathForIndex(
        m_sequenceTreeView->currentIndex());
    if (!path.isValid() || path.isGroup() || offset == 0) {
        return;
    }

    m_selectedSequencePath = path;
    m_selectedSequencePath.stepIndices.last() += offset;
    if (!m_sequenceDocument->moveStep(path, offset)) {
        m_selectedSequencePath = path;
    }
}

void MainWindow::applyUndoRedo(bool redo)
{
    auto* stack = m_sequenceDocument->undoStack();
    if ((redo && !stack->canRedo()) || (!redo && !stack->canUndo())) {
        return;
    }

    auto fallbackPath = m_sequenceTreeModel->pathForIndex(
        m_sequenceTreeView->currentIndex());
    if (!fallbackPath.isValid()) {
        fallbackPath = m_selectedSequencePath;
    }
    auto parentPath = fallbackPath;
    QString selectedKey;
    QString selectedId;
    if (fallbackPath.isValid() && !fallbackPath.isGroup()) {
        const auto object = m_sequenceDocument->objectAt(fallbackPath);
        selectedKey = object.value("key").toString();
        selectedId = object.value("id").toString();
        parentPath.stepIndices.takeLast();
    }

    if (redo) {
        stack->redo();
    } else {
        stack->undo();
    }

    auto restoredPath = fallbackPath;
    if (parentPath.isValid() && (!selectedKey.isEmpty() || !selectedId.isEmpty())) {
        const auto steps = m_sequenceDocument->objectAt(parentPath)
                               .value("steps").toArray();
        for (int index = 0; index < steps.size(); ++index) {
            const auto object = steps.at(index).toObject();
            const bool keyMatches = !selectedKey.isEmpty() &&
                                    object.value("key").toString() == selectedKey;
            const bool idMatches = selectedKey.isEmpty() && !selectedId.isEmpty() &&
                                   object.value("id").toString() == selectedId;
            if (keyMatches || idMatches) {
                restoredPath = parentPath;
                restoredPath.stepIndices.push_back(index);
                break;
            }
        }
    }
    m_selectedSequencePath = restoredPath;
    updateSequenceEditor();
}

void MainWindow::runSequence()
{
    if (!m_viewModel || !m_sequenceTreeModel) {
        return;
    }
    const auto uutId = QStringLiteral("UUT-%1")
        .arg(QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd-HHmmss-zzz")));
    runScannedUut(uutId);
}

void MainWindow::runScannedUut(const QString& serialNumber)
{
    if (!m_viewModel || !m_viewModel->canRun()) {
        statusBar()->showMessage(tr("Compile the sequence before starting a test"), 4000);
        return;
    }
    const auto sn = serialNumber.trimmed();
    if (sn.isEmpty()) {
        return;
    }
    m_viewModel->setBreakpoints(m_sequenceTreeModel->breakpointSpecs());
    m_sequenceTreeModel->setCurrentDebugNodePath({});
    m_adminSerialLabel->setText(sn);
    m_currentAdminRunCounted = false;
    m_adminTerminalNodes.clear();
    m_runtimeLogModel->clear();
    m_runtimeTimelineModel->clear();
    auto preview = m_adminPreviewReport;
    preview.completed = false;
    preview.hasError = false;
    preview.state = PicoATE::Core::ExecutionState::Idle;
    if (!preview.uuts.isEmpty()) {
        preview.uuts.first().uutId = sn;
        preview.uuts.first().hasError = false;
    }
    displayReport(preview);
    updateAdminProgress();
    QVariantMap variables;
    variables.insert(QStringLiteral("sn"), sn);
    m_viewModel->runUut(sn, variables);
}

void MainWindow::showScanDialog()
{
    if (!m_viewModel || !m_viewModel->canRun()) {
        statusBar()->showMessage(tr("Compile the sequence before scanning"), 4000);
        return;
    }
    const auto station = m_stationDocument
        ? m_stationDocument->rootObject()
        : QJsonObject{};
    m_scanDialog->setExpectedLength(
        qBound(0, station.value(QStringLiteral("snLength")).toInt(0), 256));
    m_scanDialog->showForNextScan();
}

void MainWindow::scanPlugins()
{
    const auto applicationDirectory = QCoreApplication::applicationDirPath();
    const auto pluginDirectory = firstExistingPath({
        QDir(applicationDirectory).absoluteFilePath(QStringLiteral("plugin")),
        QDir(applicationDirectory).absoluteFilePath(QStringLiteral("plugins")),
#if defined(PICOATE_PROJECT_DIR)
        QDir(QStringLiteral(PICOATE_PROJECT_DIR))
            .absoluteFilePath(QStringLiteral("templates/bin/Debug")),
#endif
    });
    if (pluginDirectory.isEmpty()) {
        QMessageBox::warning(
            this,
            tr("Scan Plugins"),
            tr("No plugin directory was found. Create a 'plugin' directory next to PicoATE.UI.exe."));
        return;
    }

    const auto nativeHost = firstDescribeCapableNativeHost({
        QDir(applicationDirectory).absoluteFilePath(
            QStringLiteral("PicoATE.NativeHost.exe")),
        QDir(applicationDirectory).absoluteFilePath(
            QStringLiteral("../../../src/nativehost/Debug/PicoATE.NativeHost.exe")),
    });
    if (nativeHost.isEmpty()) {
        QMessageBox::critical(
            this,
            tr("Scan Plugins"),
            tr("No compatible PicoATE.NativeHost.exe was found. Rebuild or redeploy the UI so the Host supports --describe. Plugin DLLs are never loaded directly in the UI process."));
        return;
    }

    const auto registryPath = QDir(applicationDirectory).absoluteFilePath(
        QStringLiteral("PluginRegistry.json"));
    statusBar()->showMessage(tr("Scanning plugins..."));
    const auto result = PluginCatalog::scanPlugins(
        pluginDirectory, nativeHost, registryPath, 5000);

    if (result.ok()) {
        loadPluginRegistry();
        QMessageBox::information(
            this,
            tr("Scan Plugins"),
            tr("Found %1 plugin DLL(s), loaded %2 plugin(s), and updated:\n%3")
                .arg(result.discoveredDllCount)
                .arg(result.plugins.size())
                .arg(registryPath));
        statusBar()->showMessage(tr("Plugin registry updated"), 5000);
        return;
    }

    if (result.registrySaved) {
        loadPluginRegistry();
    }

    QStringList details;
    const int maximumDetails = qMin(8, result.errors.size());
    for (int index = 0; index < maximumDetails; ++index) {
        const auto& error = result.errors[index];
        details.push_back(tr("%1: %2").arg(error.path, error.message));
    }
    QMessageBox::warning(
        this,
        tr("Scan Plugins"),
        tr("Found %1 DLL(s) and loaded %2 plugin(s).\n%3")
            .arg(result.discoveredDllCount)
            .arg(result.plugins.size())
            .arg(details.join(QLatin1Char('\n'))));
    statusBar()->showMessage(tr("Plugin scan completed with errors"), 7000);
}

void MainWindow::loadPluginRegistry()
{
    if (!m_pluginFunctionModel || !m_stepPropertyEditor) {
        return;
    }
    const auto registryPath = QDir(QCoreApplication::applicationDirPath())
        .absoluteFilePath(QStringLiteral("PluginRegistry.json"));
    if (!QFileInfo::exists(registryPath)) {
        m_pluginFunctionModel->setPlugins({});
        m_stepPropertyEditor->setPluginRegistry({});
        if (m_stationPropertyEditor) {
            m_stationPropertyEditor->setPluginRegistry({});
        }
        updatePluginDeviceBindings();
        updateStationEditor();
        return;
    }
    const auto registry = PluginCatalog::loadRegistry(registryPath);
    m_pluginFunctionModel->setPlugins(registry.plugins);
    m_stepPropertyEditor->setPluginRegistry(registry.plugins);
    if (m_stationPropertyEditor) {
        m_stationPropertyEditor->setPluginRegistry(registry.plugins);
    }
    updatePluginDeviceBindings();
    updateStationEditor();
    if (!registry.ok()) {
        statusBar()->showMessage(
            tr("Plugin registry contains %1 error(s)").arg(registry.errors.size()),
            7000);
    }
    if (m_pluginFunctionView) {
        m_pluginFunctionView->expandAll();
        m_pluginFunctionView->resizeColumnToContents(0);
    }
}

void MainWindow::updatePluginDeviceBindings()
{
    if (!m_stationDocument || !m_pluginFunctionModel || !m_stepPropertyEditor) {
        return;
    }
    QHash<QString, QStringList> devicesByModuleId;
    QHash<QString, QString> pluginByDeviceId;
    const auto devices = m_stationDocument->rootObject()
                             .value(QStringLiteral("devices")).toArray();
    for (const auto& value : devices) {
        const auto device = value.toObject();
        if (device.isEmpty() || !device.value(QStringLiteral("enabled")).toBool(true)) {
            continue;
        }
        const auto deviceId = device.value(QStringLiteral("deviceId")).toString(
            device.value(QStringLiteral("id")).toString());
        const auto moduleId = device.value(QStringLiteral("driverId")).toString(
            device.value(QStringLiteral("driver")).toString());
        if (deviceId.isEmpty() || moduleId.isEmpty()) {
            continue;
        }
        devicesByModuleId[moduleId].push_back(deviceId);
        pluginByDeviceId.insert(deviceId, moduleId);
    }
    m_pluginFunctionModel->setDeviceBindings(std::move(devicesByModuleId));
    m_stepPropertyEditor->setDevicePluginBindings(std::move(pluginByDeviceId));
}

void MainWindow::addStationDevice()
{
    const int row = m_selectedStationDeviceRow < 0
        ? m_stationDocument->deviceCount()
        : m_selectedStationDeviceRow + 1;
    m_selectedStationDeviceRow = row;
    m_stationDocument->insertDevice(row);
}

void MainWindow::deleteStationDevice()
{
    if (m_selectedStationDeviceRow < 0) {
        return;
    }
    const int deletedRow = m_selectedStationDeviceRow;
    m_selectedStationDeviceRow = qMin(
        deletedRow, m_stationDocument->deviceCount() - 2);
    m_stationDocument->removeDevice(deletedRow);
}

void MainWindow::duplicateStationDevice()
{
    if (m_selectedStationDeviceRow < 0) {
        return;
    }
    const int sourceRow = m_selectedStationDeviceRow;
    m_selectedStationDeviceRow = sourceRow + 1;
    if (!m_stationDocument->duplicateDevice(sourceRow)) {
        m_selectedStationDeviceRow = sourceRow;
    }
}

void MainWindow::moveStationDevice(int offset)
{
    if (m_selectedStationDeviceRow < 0 || offset == 0) {
        return;
    }
    const int sourceRow = m_selectedStationDeviceRow;
    m_selectedStationDeviceRow += offset;
    if (!m_stationDocument->moveDevice(sourceRow, offset)) {
        m_selectedStationDeviceRow = sourceRow;
    }
}

void MainWindow::applyStationUndoRedo(bool redo)
{
    auto* stack = m_stationDocument->undoStack();
    if ((redo && !stack->canRedo()) || (!redo && !stack->canUndo())) {
        return;
    }
    if (redo) {
        stack->redo();
    } else {
        stack->undo();
    }
    updateStationEditor();
}

void MainWindow::testSelectedStationDevice()
{
    if (!m_stationDocument || !m_viewModel || !m_connectionTimeoutMs) {
        return;
    }
    const int row = m_stationDeviceView && m_stationDeviceView->currentIndex().isValid()
        ? m_stationDeviceView->currentIndex().row()
        : m_selectedStationDeviceRow;
    const auto device = m_stationDocument->deviceAt(row);
    if (device.isEmpty() || !device.value("enabled").toBool(true)) {
        return;
    }
    const auto deviceId = device.value("deviceId").toString(
        device.value("id").toString()).trimmed();
    m_viewModel->testDeviceConnection(deviceId, m_connectionTimeoutMs->value());
}

void MainWindow::synchronizeSequenceSnapshot()
{
    if (!m_sequenceDocument || m_sequenceDocument->isEmpty() ||
        !m_viewModel->canChangeSources()) {
        return;
    }
    const auto snapshot = m_sequenceDocument->snapshot();
    m_viewModel->setSequenceDocument(snapshot.filePath, snapshot.json);
}

void MainWindow::synchronizeStationSnapshot()
{
    if (!m_stationDocument || m_stationDocument->isEmpty() ||
        !m_viewModel->canChangeSources()) {
        return;
    }
    const auto snapshot = m_stationDocument->snapshot();
    m_viewModel->setStationDocument(snapshot.filePath, snapshot.json);
}

void MainWindow::updateSequenceEditor()
{
    if (!m_sequenceDocument || !m_editorDiagnosticModel) {
        return;
    }

    m_editorDiagnosticModel->setDiagnostics(m_sequenceDocument->diagnostics());
    if (m_sequenceTreeView) {
        m_sequenceTreeView->expandAll();
        auto selected = m_sequenceTreeModel->indexForPath(m_selectedSequencePath);
        if (!selected.isValid() && m_sequenceTreeModel->rowCount() > 0) {
            selected = m_sequenceTreeModel->index(0, 0);
            m_selectedSequencePath = m_sequenceTreeModel->pathForIndex(selected);
        }
        if (selected.isValid()) {
            m_sequenceTreeView->setCurrentIndex(selected);
            m_stepPropertyEditor->setCurrentItem(
                m_sequenceTreeModel->pathForIndex(selected));
        } else {
            m_stepPropertyEditor->setCurrentItem({});
        }
    }
    updateWindowTitle();
    updateCommandState();
}

void MainWindow::updateStationEditor()
{
    if (!m_stationDocument || !m_stationDiagnosticModel) {
        return;
    }
    auto diagnostics = m_stationDocument->diagnostics();
    diagnostics += stationPluginDiagnostics();
    m_stationDiagnosticModel->setDiagnostics(std::move(diagnostics));
    const int count = m_stationDocument->deviceCount();
    if (count == 0) {
        m_selectedStationDeviceRow = -1;
    } else {
        m_selectedStationDeviceRow = qBound(0, m_selectedStationDeviceRow, count - 1);
    }
    if (m_stationDeviceView && m_selectedStationDeviceRow >= 0) {
        const auto index = m_stationDeviceModel->index(m_selectedStationDeviceRow, 0);
        m_stationDeviceView->setCurrentIndex(index);
        m_stationDeviceView->scrollTo(index);
    }
    if (m_stationPropertyEditor) {
        m_stationPropertyEditor->setCurrentDevice(m_selectedStationDeviceRow);
    }
    updateWindowTitle();
    updateCommandState();
}

QVector<UiDiagnostic> MainWindow::stationPluginDiagnostics() const
{
    QVector<UiDiagnostic> result;
    if (!m_stationDocument || m_stationDocument->isEmpty() ||
        !m_pluginFunctionModel) {
        return result;
    }
    QString projectDir;
#ifdef PICOATE_PROJECT_DIR
    projectDir = QString::fromUtf8(PICOATE_PROJECT_DIR);
#elif defined(PICOATE_UI_TEST_PROJECT_DIR)
    projectDir = QString::fromUtf8(PICOATE_UI_TEST_PROJECT_DIR);
#else
    projectDir = QCoreApplication::applicationDirPath();
#endif
    const auto diagnostics = PluginCatalog::validateStationBindings(
        m_stationDocument->rootObject(),
        m_pluginFunctionModel->plugins(),
        m_stationDocument->filePath(),
        projectDir);
    result.reserve(diagnostics.size());
    for (const auto& diagnostic : diagnostics) {
        result.push_back({diagnostic.warning ? UiDiagnosticSeverity::Warning
                                             : UiDiagnosticSeverity::Error,
                          diagnostic.path,
                          diagnostic.message,
                          diagnostic.suggestion});
    }
    return result;
}

void MainWindow::focusSequenceDiagnostic(const QModelIndex& index)
{
    const auto diagnostic = m_editorDiagnosticModel->diagnosticAt(index.row());
    if (!diagnostic) {
        return;
    }
    const auto target = parseSequenceDiagnosticTarget(diagnostic->path);
    if (!target.isValid()) {
        statusBar()->showMessage(
            tr("This diagnostic belongs to the sequence root: %1")
                .arg(diagnostic->path.isEmpty() ? tr("root") : diagnostic->path),
            5000);
        return;
    }

    auto resolvedPath = target.itemPath;
    auto treeIndex = m_sequenceTreeModel->indexForPath(resolvedPath);
    while (!treeIndex.isValid() && !resolvedPath.stepIndices.isEmpty()) {
        resolvedPath.stepIndices.removeLast();
        treeIndex = m_sequenceTreeModel->indexForPath(resolvedPath);
    }
    if (!treeIndex.isValid()) {
        statusBar()->showMessage(
            tr("The diagnostic target no longer exists: %1").arg(diagnostic->path),
            5000);
        return;
    }

    m_workspaceTabs->setCurrentIndex(0);
    m_selectedSequencePath = resolvedPath;
    m_sequenceTreeView->setCurrentIndex(treeIndex);
    m_sequenceTreeView->scrollTo(treeIndex, QAbstractItemView::PositionAtCenter);
    m_stepPropertyEditor->setCurrentItem(resolvedPath);
    const bool exactItem = resolvedPath == target.itemPath;
    const bool fieldFocused = exactItem &&
                              m_stepPropertyEditor->focusField(target.fieldPath);
    statusBar()->showMessage(
        fieldFocused
            ? tr("Located diagnostic field: %1").arg(diagnostic->path)
            : tr("Located diagnostic node: %1").arg(diagnostic->path),
        4000);
}

void MainWindow::focusStationDiagnostic(const QModelIndex& index)
{
    const auto diagnostic = m_stationDiagnosticModel->diagnosticAt(index.row());
    if (!diagnostic) {
        return;
    }
    static const QRegularExpression devicePath(
        QStringLiteral("^devices\\[(\\d+)\\](?:\\.(.*))?$"));
    const auto match = devicePath.match(diagnostic->path);
    if (match.hasMatch()) {
        const int row = match.captured(1).toInt();
        if (row >= 0 && row < m_stationDeviceModel->rowCount()) {
            m_selectedStationDeviceRow = row;
            const auto deviceIndex = m_stationDeviceModel->index(row, 0);
            m_stationDeviceView->setCurrentIndex(deviceIndex);
            m_stationDeviceView->scrollTo(deviceIndex,
                                          QAbstractItemView::PositionAtCenter);
            m_stationPropertyEditor->setCurrentDevice(row);
        }
    }
    m_workspaceTabs->setCurrentIndex(1);
    const bool focused = diagnostic->path.startsWith(QStringLiteral("devices["))
        ? m_stationPropertyEditor->focusField(diagnostic->path)
        : m_stationSettingsEditor->focusField(diagnostic->path);
    statusBar()->showMessage(
        focused ? tr("Located station field: %1").arg(diagnostic->path)
                : tr("Located station diagnostic: %1").arg(diagnostic->path),
        4000);
}

void MainWindow::updateWindowTitle()
{
    QString title = tr("PicoATE");
    if (m_sequenceDocument && !m_sequenceDocument->isEmpty()) {
        title += QStringLiteral(" - ") + m_sequenceDocument->displayName();
        if (m_sequenceDocument->isModified()) {
            title += QLatin1Char('*');
        }
    }
    if (m_stationDocument && !m_stationDocument->isEmpty()) {
        title += QStringLiteral(" | ") + m_stationDocument->displayName();
        if (m_stationDocument->isModified()) {
            title += QLatin1Char('*');
        }
    }
    setWindowTitle(title);
}


void MainWindow::buildActions()
{
    auto* fileMenu = menuBar()->addMenu(tr("&File"));
    auto* editMenu = menuBar()->addMenu(tr("&Edit"));
    auto* runMenu = menuBar()->addMenu(tr("&Run"));
    auto* viewMenu = menuBar()->addMenu(tr("&View"));
    auto* toolsMenu = menuBar()->addMenu(tr("&Tools"));
    auto* mainToolbar = addToolBar(tr("Runner"));
    mainToolbar->setObjectName(QStringLiteral("runnerToolbar"));
    mainToolbar->setMovable(false);
    mainToolbar->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);

    m_openSequenceAction = new QAction(
        style()->standardIcon(QStyle::SP_DialogOpenButton),
        tr("Open Sequence"),
        this);
    m_openSequenceAction->setToolTip(tr("Open sequence JSON"));
    connect(m_openSequenceAction, &QAction::triggered, this, [this] { chooseSequence(); });
    m_recentSequenceMenu = new QMenu(tr("Open Recent Sequence"), fileMenu);
    m_recentSequenceMenu->setObjectName(QStringLiteral("recentSequenceMenu"));

    m_saveSequenceAction = new QAction(
        style()->standardIcon(QStyle::SP_DialogSaveButton), tr("Save Sequence"), this);
    m_saveSequenceAction->setShortcut(QKeySequence::Save);
    m_saveSequenceAction->setToolTip(tr("Save sequence JSON"));
    connect(m_saveSequenceAction, &QAction::triggered, this, [this] { saveSequence(); });

    m_saveSequenceAsAction = new QAction(tr("Save Sequence As..."), this);
    m_saveSequenceAsAction->setShortcut(QKeySequence::SaveAs);
    connect(m_saveSequenceAsAction, &QAction::triggered, this, [this] { saveSequenceAs(); });

    m_undoAction = new QAction(
        style()->standardIcon(QStyle::SP_ArrowBack), tr("Undo"), this);
    m_undoAction->setShortcut(QKeySequence::Undo);
    m_undoAction->setToolTip(tr("Undo last sequence edit"));
    connect(m_undoAction, &QAction::triggered,
            this, [this] { applyUndoRedo(false); });
    m_redoAction = new QAction(
        style()->standardIcon(QStyle::SP_ArrowForward), tr("Redo"), this);
    m_redoAction->setShortcut(QKeySequence::Redo);
    m_redoAction->setToolTip(tr("Redo last sequence edit"));
    connect(m_redoAction, &QAction::triggered,
            this, [this] { applyUndoRedo(true); });
    connect(m_sequenceDocument->undoStack(), &QUndoStack::undoTextChanged,
            this, [this](const QString& text) {
                m_undoAction->setText(text.isEmpty()
                    ? tr("Undo") : tr("Undo %1").arg(text));
            });
    connect(m_sequenceDocument->undoStack(), &QUndoStack::redoTextChanged,
            this, [this](const QString& text) {
                m_redoAction->setText(text.isEmpty()
                    ? tr("Redo") : tr("Redo %1").arg(text));
            });

    m_addStepAction = new QAction(
        style()->standardIcon(QStyle::SP_FileIcon), tr("Add Step"), this);
    m_addStepAction->setToolTip(tr("Add step after selection or inside a container"));
    connect(m_addStepAction, &QAction::triggered, this, [this] { addSequenceStep(); });

    m_deleteStepAction = new QAction(
        style()->standardIcon(QStyle::SP_TrashIcon), tr("Delete Step"), this);
    m_deleteStepAction->setShortcut(QKeySequence::Delete);
    connect(m_deleteStepAction, &QAction::triggered, this, [this] { deleteSequenceStep(); });

    m_duplicateStepAction = new QAction(
        style()->standardIcon(QStyle::SP_FileLinkIcon), tr("Duplicate Selected"), this);
    m_duplicateStepAction->setObjectName(QStringLiteral("duplicateStepAction"));
    m_duplicateStepAction->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_D));
    m_duplicateStepAction->setToolTip(
        tr("Duplicate all selected Steps and TestItems"));
    connect(m_duplicateStepAction, &QAction::triggered, this, [this] { duplicateSequenceStep(); });

    m_wrapTestItemAction = new QAction(
        style()->standardIcon(QStyle::SP_DirIcon), tr("Wrap in TestItem"), this);
    m_wrapTestItemAction->setObjectName(QStringLiteral("wrapTestItemAction"));
    m_wrapTestItemAction->setToolTip(
        tr("Wrap selected contiguous steps in a TestItem"));
    connect(m_wrapTestItemAction, &QAction::triggered,
            this, [this] { wrapSelectedStepsInTestItem(); });

    m_enableStepsAction = new QAction(
        style()->standardIcon(QStyle::SP_DialogApplyButton), tr("Enable Selected"), this);
    m_enableStepsAction->setObjectName(QStringLiteral("enableStepsAction"));
    m_enableStepsAction->setToolTip(tr("Enable all selected steps"));
    connect(m_enableStepsAction, &QAction::triggered,
            this, [this] { setSelectedSequenceStepsEnabled(true); });

    m_disableStepsAction = new QAction(
        style()->standardIcon(QStyle::SP_DialogCancelButton), tr("Disable Selected"), this);
    m_disableStepsAction->setObjectName(QStringLiteral("disableStepsAction"));
    m_disableStepsAction->setToolTip(tr("Disable all selected steps"));
    connect(m_disableStepsAction, &QAction::triggered,
            this, [this] { setSelectedSequenceStepsEnabled(false); });

    m_moveStepUpAction = new QAction(
        style()->standardIcon(QStyle::SP_ArrowUp), tr("Move Up"), this);
    connect(m_moveStepUpAction, &QAction::triggered, this, [this] { moveSequenceStep(-1); });

    m_moveStepDownAction = new QAction(
        style()->standardIcon(QStyle::SP_ArrowDown), tr("Move Down"), this);
    connect(m_moveStepDownAction, &QAction::triggered, this, [this] { moveSequenceStep(1); });

    m_openStationAction = new QAction(
        style()->standardIcon(QStyle::SP_DirOpenIcon),
        tr("Open Station"),
        this);
    m_openStationAction->setToolTip(tr("Open station JSON"));
    connect(m_openStationAction, &QAction::triggered, this, [this] { chooseStation(); });
    m_recentStationMenu = new QMenu(tr("Open Recent Station"), fileMenu);
    m_recentStationMenu->setObjectName(QStringLiteral("recentStationMenu"));

    m_saveStationAction = new QAction(
        style()->standardIcon(QStyle::SP_DialogSaveButton), tr("Save Station"), this);
    m_saveStationAction->setToolTip(tr("Save station JSON"));
    connect(m_saveStationAction, &QAction::triggered,
            this, [this] { saveStation(); });
    m_saveStationAsAction = new QAction(tr("Save Station As..."), this);
    connect(m_saveStationAsAction, &QAction::triggered,
            this, [this] { saveStationAs(); });

    m_stationUndoAction = new QAction(
        style()->standardIcon(QStyle::SP_ArrowBack), tr("Undo"), this);
    m_stationUndoAction->setToolTip(tr("Undo last station edit"));
    connect(m_stationUndoAction, &QAction::triggered,
            this, [this] { applyStationUndoRedo(false); });
    m_stationRedoAction = new QAction(
        style()->standardIcon(QStyle::SP_ArrowForward), tr("Redo"), this);
    m_stationRedoAction->setToolTip(tr("Redo last station edit"));
    connect(m_stationRedoAction, &QAction::triggered,
            this, [this] { applyStationUndoRedo(true); });
    connect(m_stationDocument->undoStack(), &QUndoStack::undoTextChanged,
            this, [this](const QString& text) {
                m_stationUndoAction->setText(text.isEmpty()
                    ? tr("Undo") : tr("Undo %1").arg(text));
            });
    connect(m_stationDocument->undoStack(), &QUndoStack::redoTextChanged,
            this, [this](const QString& text) {
                m_stationRedoAction->setText(text.isEmpty()
                    ? tr("Redo") : tr("Redo %1").arg(text));
            });

    m_addDeviceAction = new QAction(
        style()->standardIcon(QStyle::SP_FileIcon), tr("Add Device"), this);
    connect(m_addDeviceAction, &QAction::triggered,
            this, [this] { addStationDevice(); });
    m_duplicateDeviceAction = new QAction(
        style()->standardIcon(QStyle::SP_FileLinkIcon), tr("Duplicate Device"), this);
    connect(m_duplicateDeviceAction, &QAction::triggered,
            this, [this] { duplicateStationDevice(); });
    m_deleteDeviceAction = new QAction(
        style()->standardIcon(QStyle::SP_TrashIcon), tr("Delete Device"), this);
    connect(m_deleteDeviceAction, &QAction::triggered,
            this, [this] { deleteStationDevice(); });
    m_moveDeviceUpAction = new QAction(
        style()->standardIcon(QStyle::SP_ArrowUp), tr("Move Device Up"), this);
    connect(m_moveDeviceUpAction, &QAction::triggered,
            this, [this] { moveStationDevice(-1); });
    m_moveDeviceDownAction = new QAction(
        style()->standardIcon(QStyle::SP_ArrowDown), tr("Move Device Down"), this);
    connect(m_moveDeviceDownAction, &QAction::triggered,
            this, [this] { moveStationDevice(1); });
    m_testDeviceConnectionAction = new QAction(
        style()->standardIcon(QStyle::SP_DialogApplyButton),
        tr("Test Connection"),
        this);
    m_testDeviceConnectionAction->setObjectName(
        QStringLiteral("testDeviceConnectionAction"));
    m_testDeviceConnectionAction->setToolTip(tr("Open, health-check, and close selected device"));
    connect(m_testDeviceConnectionAction, &QAction::triggered,
            this, [this] { testSelectedStationDevice(); });

    m_compileAction = new QAction(
        style()->standardIcon(QStyle::SP_BrowserReload),
        tr("Compile"),
        this);
    m_compileAction->setObjectName(QStringLiteral("compileAction"));
    m_compileAction->setToolTip(tr("Compile selected sequence"));
    connect(m_compileAction, &QAction::triggered, m_viewModel, &ExecutionViewModel::compile);

    m_runAction = new QAction(
        style()->standardIcon(QStyle::SP_MediaPlay),
        tr("Run"),
        this);
    m_runAction->setObjectName(QStringLiteral("runAction"));
    m_runAction->setToolTip(tr("Run compiled sequence"));
    connect(m_runAction,
            &QAction::triggered,
            this,
            &MainWindow::runSequence);

    m_pauseAction = new QAction(
        style()->standardIcon(QStyle::SP_MediaPause),
        tr("Pause"),
        this);
    m_pauseAction->setToolTip(tr("Pause after the running step completes"));
    connect(m_pauseAction, &QAction::triggered, m_viewModel, &ExecutionViewModel::pause);

    m_resumeAction = new QAction(
        style()->standardIcon(QStyle::SP_MediaPlay),
        tr("Resume"),
        this);
    m_resumeAction->setToolTip(tr("Resume the paused execution"));
    connect(m_resumeAction, &QAction::triggered, m_viewModel, &ExecutionViewModel::resume);

    m_stepIntoAction = new QAction(
        style()->standardIcon(QStyle::SP_ArrowDown),
        tr("Step Into"),
        this);
    m_stepIntoAction->setToolTip(tr("Run one scheduler step and pause again"));
    connect(m_stepIntoAction, &QAction::triggered, m_viewModel, &ExecutionViewModel::stepInto);

    m_stepOverAction = new QAction(
        style()->standardIcon(QStyle::SP_ArrowForward),
        tr("Step Over"),
        this);
    m_stepOverAction->setToolTip(tr("Run current step or structural block and pause again"));
    connect(m_stepOverAction, &QAction::triggered, m_viewModel, &ExecutionViewModel::stepOver);

    m_stopAction = new QAction(
        style()->standardIcon(QStyle::SP_MediaStop),
        tr("Stop"),
        this);
    m_stopAction->setToolTip(tr("Request graceful stop"));
    connect(m_stopAction, &QAction::triggered, this, [this] { m_viewModel->stop(); });

    m_scanAction = new QAction(
        style()->standardIcon(QStyle::SP_DialogYesButton),
        tr("Scan SN"),
        this);
    m_scanAction->setObjectName(QStringLiteral("adminScanAction"));
    m_scanAction->setToolTip(tr("Open the barcode dialog for one test run"));
    connect(m_scanAction, &QAction::triggered, this, &MainWindow::showScanDialog);

    m_scanPluginsAction = new QAction(
        style()->standardIcon(QStyle::SP_BrowserReload),
        tr("Scan Plugins"),
        this);
    m_scanPluginsAction->setObjectName(QStringLiteral("scanPluginsAction"));
    m_scanPluginsAction->setToolTip(
        tr("Scan the plugin directory and rebuild PluginRegistry.json"));
    connect(m_scanPluginsAction, &QAction::triggered,
            this, &MainWindow::scanPlugins);

    m_resetLayoutAction = new QAction(tr("Reset Layout"), this);
    m_resetLayoutAction->setObjectName(QStringLiteral("resetLayoutAction"));
    m_resetLayoutAction->setToolTip(tr("Restore the default window and panel layout"));
    connect(m_resetLayoutAction, &QAction::triggered,
            this, &MainWindow::resetUiLayout);

    auto* exitAction = new QAction(tr("E&xit"), this);
    connect(exitAction, &QAction::triggered, this, &QWidget::close);

    fileMenu->addAction(m_openSequenceAction);
    fileMenu->addMenu(m_recentSequenceMenu);
    fileMenu->addAction(m_saveSequenceAction);
    fileMenu->addAction(m_saveSequenceAsAction);
    fileMenu->addSeparator();
    fileMenu->addAction(m_openStationAction);
    fileMenu->addMenu(m_recentStationMenu);
    fileMenu->addAction(m_saveStationAction);
    fileMenu->addAction(m_saveStationAsAction);
    fileMenu->addSeparator();
    fileMenu->addAction(exitAction);
    editMenu->addAction(m_undoAction);
    editMenu->addAction(m_redoAction);
    editMenu->addSeparator();
    editMenu->addAction(m_addStepAction);
    editMenu->addAction(m_duplicateStepAction);
    editMenu->addAction(m_deleteStepAction);
    editMenu->addAction(m_enableStepsAction);
    editMenu->addAction(m_disableStepsAction);
    editMenu->addSeparator();
    editMenu->addAction(m_moveStepUpAction);
    editMenu->addAction(m_moveStepDownAction);
    runMenu->addAction(m_compileAction);
    runMenu->addAction(m_runAction);
    runMenu->addAction(m_pauseAction);
    runMenu->addAction(m_resumeAction);
    runMenu->addAction(m_stepIntoAction);
    runMenu->addAction(m_stepOverAction);
    runMenu->addAction(m_stopAction);
    runMenu->addSeparator();
    runMenu->addAction(m_scanAction);
    toolsMenu->addAction(m_scanPluginsAction);
    viewMenu->addAction(m_resetLayoutAction);

    mainToolbar->addAction(m_openSequenceAction);
    mainToolbar->addAction(m_openStationAction);
    mainToolbar->addSeparator();
    m_uutCount = new QSpinBox(mainToolbar);
    m_uutCount->setObjectName(QStringLiteral("uutCountSpinBox"));
    m_uutCount->setRange(1, 64);
    m_uutCount->setValue(1);
    m_uutCount->setFixedWidth(64);
    m_uutCount->setToolTip(tr("Number of UUTs in this run"));
    m_uutCount->hide();
    mainToolbar->addAction(m_compileAction);
    mainToolbar->addAction(m_runAction);
    mainToolbar->addAction(m_pauseAction);
    mainToolbar->addAction(m_resumeAction);
    mainToolbar->addAction(m_stepIntoAction);
    mainToolbar->addAction(m_stepOverAction);
    mainToolbar->addAction(m_stopAction);
    mainToolbar->addSeparator();
    mainToolbar->addAction(m_scanAction);
}

void MainWindow::buildLayout()
{
    auto* central = new QWidget(this);
    auto* rootLayout = new QVBoxLayout(central);
    rootLayout->setContentsMargins(12, 12, 12, 12);
    rootLayout->setSpacing(10);

    m_sequencePath = new QLineEdit(central);
    m_sequencePath->setReadOnly(true);
    m_sequencePath->setPlaceholderText(tr("No sequence selected"));
    m_sequencePath->hide();

    m_stationPath = new QLineEdit(central);
    m_stationPath->setReadOnly(true);
    m_stationPath->setPlaceholderText(tr("No station selected"));
    m_stationPath->hide();

    m_adminSequenceLabel = new QLabel(tr("No sequence selected"), central);
    m_adminSequenceLabel->setObjectName(QStringLiteral("adminSequenceLabel"));
    m_adminSequenceLabel->setAlignment(Qt::AlignCenter);
    rootLayout->addWidget(m_adminSequenceLabel);

    m_workspaceTabs = new QTabWidget(central);
    m_workspaceTabs->setObjectName(QStringLiteral("workspaceTabs"));

    auto* sequenceEditorPage = new QWidget(m_workspaceTabs);
    sequenceEditorPage->setObjectName(QStringLiteral("sequenceEditorPage"));
    auto* sequenceEditorLayout = new QVBoxLayout(sequenceEditorPage);
    sequenceEditorLayout->setContentsMargins(0, 0, 0, 0);
    sequenceEditorLayout->setSpacing(6);
    auto* sequenceToolbar = new QToolBar(sequenceEditorPage);
    sequenceToolbar->setObjectName(QStringLiteral("sequenceToolbar"));
    sequenceToolbar->setMovable(false);
    sequenceToolbar->setToolButtonStyle(Qt::ToolButtonIconOnly);
    sequenceToolbar->addAction(m_saveSequenceAction);
    sequenceToolbar->addSeparator();
    sequenceToolbar->addAction(m_undoAction);
    sequenceToolbar->addAction(m_redoAction);
    sequenceToolbar->addSeparator();
    sequenceToolbar->addAction(m_addStepAction);
    sequenceToolbar->addAction(m_duplicateStepAction);
    sequenceToolbar->addAction(m_wrapTestItemAction);
    sequenceToolbar->addAction(m_deleteStepAction);
    sequenceToolbar->addAction(m_enableStepsAction);
    sequenceToolbar->addAction(m_disableStepsAction);
    sequenceToolbar->addSeparator();
    sequenceToolbar->addAction(m_moveStepUpAction);
    sequenceToolbar->addAction(m_moveStepDownAction);
    sequenceToolbar->addSeparator();
    sequenceToolbar->addAction(m_scanPluginsAction);
    sequenceEditorLayout->addWidget(sequenceToolbar);

    auto* sequenceSplitter = new QSplitter(Qt::Vertical, sequenceEditorPage);
    sequenceSplitter->setObjectName(QStringLiteral("sequenceVerticalSplitter"));
    sequenceSplitter->setChildrenCollapsible(false);
    auto* sequenceWorkArea = new QSplitter(Qt::Horizontal);
    sequenceWorkArea->setObjectName(QStringLiteral("sequenceWorkSplitter"));
    sequenceWorkArea->setChildrenCollapsible(false);
    m_pluginFunctionView = new QTreeView;
    m_pluginFunctionView->setObjectName(QStringLiteral("pluginFunctionView"));
    m_pluginFunctionView->setModel(m_pluginFunctionModel);
    m_pluginFunctionView->setRootIsDecorated(true);
    m_pluginFunctionView->setUniformRowHeights(true);
    m_pluginFunctionView->setAlternatingRowColors(true);
    m_pluginFunctionView->setSelectionMode(QAbstractItemView::SingleSelection);
    m_pluginFunctionView->setDragEnabled(true);
    m_pluginFunctionView->setDragDropMode(QAbstractItemView::DragOnly);
    m_pluginFunctionView->setDefaultDropAction(Qt::CopyAction);
    m_pluginFunctionView->setItemDelegateForColumn(
        0, new DragHandleDelegate(m_pluginFunctionView));
    m_pluginFunctionView->setMinimumWidth(190);
    m_pluginFunctionView->setMaximumWidth(360);
    polishReadableTreeView(m_pluginFunctionView);
    m_pluginFunctionView->header()->setStretchLastSection(true);
    m_sequenceTreeView = new QTreeView;
    m_sequenceTreeView->setObjectName(QStringLiteral("sequenceTreeView"));
    m_sequenceTreeView->setModel(m_sequenceTreeModel);
    m_sequenceTreeView->setRootIsDecorated(true);
    m_sequenceTreeView->setUniformRowHeights(true);
    m_sequenceTreeView->setAlternatingRowColors(true);
    m_sequenceTreeView->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_sequenceTreeView->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_sequenceTreeView->setDragEnabled(true);
    m_sequenceTreeView->setAcceptDrops(true);
    m_sequenceTreeView->setDropIndicatorShown(true);
    m_sequenceTreeView->setDragDropMode(QAbstractItemView::DragDrop);
    m_sequenceTreeView->setDefaultDropAction(Qt::MoveAction);
    m_sequenceTreeView->setItemDelegateForColumn(
        SequenceTreeModel::NameColumn,
        new DragHandleDelegate(m_sequenceTreeView));
    auto treeSizePolicy = m_sequenceTreeView->sizePolicy();
    treeSizePolicy.setHorizontalPolicy(QSizePolicy::Ignored);
    m_sequenceTreeView->setSizePolicy(treeSizePolicy);
    m_sequenceTreeView->setMinimumWidth(320);
    m_sequenceTreeView->setMaximumWidth(720);
    polishReadableTreeView(m_sequenceTreeView);
    installProportionalHeader(m_sequenceTreeView, {4, 2, 2, 1, 1});

    m_stepPropertyEditor = new StepPropertyEditor(m_sequenceDocument);
    sequenceWorkArea->addWidget(m_pluginFunctionView);
    sequenceWorkArea->addWidget(m_sequenceTreeView);
    sequenceWorkArea->addWidget(m_stepPropertyEditor);
    sequenceWorkArea->setStretchFactor(0, 1);
    sequenceWorkArea->setStretchFactor(1, 3);
    sequenceWorkArea->setStretchFactor(2, 2);
    sequenceWorkArea->setSizes({220, 560, 380});

    m_editorDiagnosticView = new QTableView;
    m_editorDiagnosticView->setObjectName(QStringLiteral("sequenceDiagnosticView"));
    m_editorDiagnosticView->setModel(m_editorDiagnosticModel);
    m_editorDiagnosticView->setAlternatingRowColors(true);
    m_editorDiagnosticView->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_editorDiagnosticView->setSelectionMode(QAbstractItemView::SingleSelection);
    m_editorDiagnosticView->verticalHeader()->setVisible(false);
    installProportionalHeader(m_editorDiagnosticView, {1, 2, 5, 3});
    sequenceSplitter->addWidget(sequenceWorkArea);
    sequenceSplitter->addWidget(m_editorDiagnosticView);
    sequenceSplitter->setStretchFactor(0, 4);
    sequenceSplitter->setStretchFactor(1, 1);
    sequenceSplitter->setSizes({520, 140});
    sequenceEditorLayout->addWidget(sequenceSplitter, 1);
    m_workspaceTabs->addTab(sequenceEditorPage, tr("Flow Editor"));

    auto* stationEditorPage = new QWidget(m_workspaceTabs);
    stationEditorPage->setObjectName(QStringLiteral("stationEditorPage"));
    auto* stationEditorLayout = new QVBoxLayout(stationEditorPage);
    stationEditorLayout->setContentsMargins(0, 0, 0, 0);
    stationEditorLayout->setSpacing(6);
    auto* stationToolbar = new QToolBar(stationEditorPage);
    stationToolbar->setObjectName(QStringLiteral("stationToolbar"));
    stationToolbar->setMovable(false);
    stationToolbar->setToolButtonStyle(Qt::ToolButtonIconOnly);
    stationToolbar->addAction(m_saveStationAction);
    stationToolbar->addSeparator();
    stationToolbar->addAction(m_stationUndoAction);
    stationToolbar->addAction(m_stationRedoAction);
    stationToolbar->addSeparator();
    stationToolbar->addAction(m_addDeviceAction);
    stationToolbar->addAction(m_duplicateDeviceAction);
    stationToolbar->addAction(m_deleteDeviceAction);
    stationToolbar->addSeparator();
    stationToolbar->addAction(m_moveDeviceUpAction);
    stationToolbar->addAction(m_moveDeviceDownAction);
    stationToolbar->addSeparator();
    auto* connectionTimeoutLabel = new QLabel(tr("Timeout"), stationToolbar);
    m_connectionTimeoutMs = new QSpinBox(stationToolbar);
    m_connectionTimeoutMs->setObjectName(QStringLiteral("connectionTimeoutSpinBox"));
    m_connectionTimeoutMs->setRange(100, 60000);
    m_connectionTimeoutMs->setValue(5000);
    m_connectionTimeoutMs->setSingleStep(500);
    m_connectionTimeoutMs->setSuffix(tr(" ms"));
    m_connectionTimeoutMs->setFixedWidth(110);
    stationToolbar->addWidget(connectionTimeoutLabel);
    stationToolbar->addWidget(m_connectionTimeoutMs);
    stationToolbar->addAction(m_testDeviceConnectionAction);
    stationEditorLayout->addWidget(stationToolbar);

    auto* stationSplitter = new QSplitter(Qt::Vertical, stationEditorPage);
    stationSplitter->setObjectName(QStringLiteral("stationVerticalSplitter"));
    stationSplitter->setChildrenCollapsible(false);
    auto* stationWorkArea = new QSplitter(Qt::Horizontal);
    stationWorkArea->setObjectName(QStringLiteral("stationWorkSplitter"));
    stationWorkArea->setChildrenCollapsible(false);
    m_stationSettingsEditor = new StationSettingsEditor(m_stationDocument);

    auto* devicePane = new QWidget(stationWorkArea);
    devicePane->setObjectName(QStringLiteral("stationDevicePane"));
    auto* deviceLayout = new QVBoxLayout(devicePane);
    deviceLayout->setContentsMargins(8, 8, 8, 8);
    deviceLayout->setSpacing(8);
    auto* deviceTitle = new QLabel(tr("Devices"), devicePane);
    auto deviceTitleFont = deviceTitle->font();
    deviceTitleFont.setBold(true);
    deviceTitleFont.setPointSize(deviceTitleFont.pointSize() + 1);
    deviceTitle->setFont(deviceTitleFont);
    deviceLayout->addWidget(deviceTitle);

    m_stationDeviceView = new QTableView(devicePane);
    m_stationDeviceView->setObjectName(QStringLiteral("stationDeviceView"));
    m_stationDeviceView->setModel(m_stationDeviceModel);
    m_stationDeviceView->setAlternatingRowColors(true);
    m_stationDeviceView->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_stationDeviceView->setSelectionMode(QAbstractItemView::SingleSelection);
    m_stationDeviceView->verticalHeader()->setVisible(false);
    installProportionalHeader(m_stationDeviceView, {2, 2, 3, 4, 2, 2, 2});
    m_stationDeviceView->setItemDelegateForColumn(
        StationDeviceModel::EnabledColumn,
        new OnOffItemDelegate(m_stationDeviceView));
    m_stationDeviceView->verticalHeader()->setDefaultSectionSize(34);
    m_stationDeviceView->setMinimumWidth(360);
    deviceLayout->addWidget(m_stationDeviceView, 1);

    auto* propertyPane = new QWidget(stationWorkArea);
    propertyPane->setObjectName(QStringLiteral("stationDevicePropertyPane"));
    auto* propertyLayout = new QVBoxLayout(propertyPane);
    propertyLayout->setContentsMargins(8, 8, 8, 8);
    propertyLayout->setSpacing(8);
    auto* propertyTitle = new QLabel(tr("Device Parameters"), propertyPane);
    auto propertyTitleFont = propertyTitle->font();
    propertyTitleFont.setBold(true);
    propertyTitleFont.setPointSize(propertyTitleFont.pointSize() + 1);
    propertyTitle->setFont(propertyTitleFont);
    propertyLayout->addWidget(propertyTitle);
    m_stationPropertyEditor = new StationPropertyEditor(m_stationDocument);
    m_stationPropertyEditor->setStationPageVisible(false);
    propertyLayout->addWidget(m_stationPropertyEditor, 1);
    stationWorkArea->addWidget(m_stationSettingsEditor);
    stationWorkArea->addWidget(devicePane);
    stationWorkArea->addWidget(propertyPane);
    stationWorkArea->setStretchFactor(0, 1);
    stationWorkArea->setStretchFactor(1, 3);
    stationWorkArea->setStretchFactor(2, 2);
    stationWorkArea->setSizes({260, 610, 390});

    m_stationDiagnosticView = new QTableView;
    m_stationDiagnosticView->setObjectName(QStringLiteral("stationDiagnosticView"));
    m_stationDiagnosticView->setModel(m_stationDiagnosticModel);
    m_stationDiagnosticView->setAlternatingRowColors(true);
    m_stationDiagnosticView->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_stationDiagnosticView->setSelectionMode(QAbstractItemView::SingleSelection);
    m_stationDiagnosticView->verticalHeader()->setVisible(false);
    installProportionalHeader(m_stationDiagnosticView, {1, 2, 5, 3});
    stationSplitter->addWidget(stationWorkArea);
    stationSplitter->addWidget(m_stationDiagnosticView);
    stationSplitter->setStretchFactor(0, 4);
    stationSplitter->setStretchFactor(1, 1);
    stationSplitter->setSizes({520, 140});
    stationEditorLayout->addWidget(stationSplitter, 1);
    m_workspaceTabs->addTab(stationEditorPage, tr("Station Config"));

    auto* runPage = new QWidget(m_workspaceTabs);
    runPage->setObjectName(QStringLiteral("adminRunPage"));
    auto* runPageLayout = new QVBoxLayout(runPage);
    runPageLayout->setContentsMargins(0, 0, 0, 0);
    runPageLayout->setSpacing(8);
    auto* splitter = new QSplitter(Qt::Horizontal, runPage);
    splitter->setObjectName(QStringLiteral("runSplitter"));
    splitter->setChildrenCollapsible(false);

    auto* sidebar = new QFrame(splitter);
    sidebar->setObjectName(QStringLiteral("adminRunSidebar"));
    sidebar->setMinimumWidth(205);
    sidebar->setMaximumWidth(265);
    auto* sidebarLayout = new QVBoxLayout(sidebar);
    sidebarLayout->setContentsMargins(18, 18, 18, 18);
    sidebarLayout->setSpacing(12);
    auto* unitTitle = new QLabel(tr("UNIT UNDER TEST"), sidebar);
    unitTitle->setObjectName(QStringLiteral("adminSectionTitle"));
    sidebarLayout->addWidget(unitTitle);
    auto* unitDetails = new QFormLayout;
    unitDetails->setHorizontalSpacing(12);
    unitDetails->setVerticalSpacing(10);
    m_adminSerialLabel = new QLabel(tr("--"), sidebar);
    m_adminSerialLabel->setObjectName(QStringLiteral("adminSerialLabel"));
    m_adminStationLabel = new QLabel(tr("--"), sidebar);
    m_adminStationLabel->setObjectName(QStringLiteral("adminStationLabel"));
    m_adminOrderLabel = new QLabel(tr("--"), sidebar);
    m_adminTesterLabel = new QLabel(tr("--"), sidebar);
    m_adminJigLabel = new QLabel(tr("--"), sidebar);
    unitDetails->addRow(tr("SN"), m_adminSerialLabel);
    unitDetails->addRow(tr("Station"), m_adminStationLabel);
    unitDetails->addRow(tr("Order"), m_adminOrderLabel);
    unitDetails->addRow(tr("Tester"), m_adminTesterLabel);
    unitDetails->addRow(tr("Jig No."), m_adminJigLabel);
    sidebarLayout->addLayout(unitDetails);
    sidebarLayout->addStretch(1);
    auto* resultCaption = new QLabel(tr("OVERALL RESULT"), sidebar);
    resultCaption->setObjectName(QStringLiteral("adminMetricCaption"));
    resultCaption->setAlignment(Qt::AlignCenter);
    sidebarLayout->addWidget(resultCaption);
    m_adminOverallResult = new QLabel(tr("WAITING"), sidebar);
    m_adminOverallResult->setObjectName(QStringLiteral("adminOverallResult"));
    m_adminOverallResult->setAlignment(Qt::AlignCenter);
    m_adminOverallResult->setMinimumHeight(104);
    auto overallFont = m_adminOverallResult->font();
    overallFont.setBold(true);
    overallFont.setPointSize(overallFont.pointSize() + 12);
    m_adminOverallResult->setFont(overallFont);
    sidebarLayout->addWidget(m_adminOverallResult);
    auto* elapsedCaption = new QLabel(tr("ELAPSED TIME"), sidebar);
    elapsedCaption->setObjectName(QStringLiteral("adminMetricCaption"));
    elapsedCaption->setAlignment(Qt::AlignCenter);
    sidebarLayout->addWidget(elapsedCaption);
    m_adminElapsedLabel = new QLabel(tr("00:00.000"), sidebar);
    m_adminElapsedLabel->setObjectName(QStringLiteral("adminElapsedLabel"));
    m_adminElapsedLabel->setAlignment(Qt::AlignCenter);
    sidebarLayout->addWidget(m_adminElapsedLabel);

    auto* runDataSplitter = new QSplitter(Qt::Vertical, splitter);
    runDataSplitter->setObjectName(QStringLiteral("adminRunDataSplitter"));
    runDataSplitter->setChildrenCollapsible(false);

    m_resultView = new QTreeView(runDataSplitter);
    m_resultView->setModel(m_uutStepModel);
    m_resultView->setObjectName(QStringLiteral("resultView"));
    m_resultView->setRootIsDecorated(true);
    m_resultView->setUniformRowHeights(true);
    m_resultView->setAlternatingRowColors(true);
    m_resultView->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_resultView->setSelectionMode(QAbstractItemView::SingleSelection);
    polishReadableTreeView(m_resultView);
    installProportionalHeader(m_resultView, {2, 1, 1, 1, 1, 1, 1, 1, 1, 1});
    m_resultView->setColumnHidden(UutStepModel::StateColumn, true);
    m_resultView->setColumnHidden(UutStepModel::AttemptsColumn, true);
    m_resultView->setColumnHidden(UutStepModel::LoopColumn, true);

    auto* details = new QTabWidget(runDataSplitter);
    details->setObjectName(QStringLiteral("runDetailsTabs"));
    m_attemptView = new QTableView(details);
    m_attemptView->setModel(m_attemptModel);
    m_attemptView->setAlternatingRowColors(true);
    m_attemptView->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_attemptView->setSelectionMode(QAbstractItemView::SingleSelection);
    m_attemptView->verticalHeader()->setVisible(false);
    installProportionalHeader(m_attemptView, {1, 2, 1, 1, 4});
    details->addTab(m_attemptView, tr("Attempts"));

    m_measurementView = new QTableView(details);
    m_measurementView->setModel(m_measurementModel);
    m_measurementView->setAlternatingRowColors(true);
    m_measurementView->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_measurementView->setSelectionMode(QAbstractItemView::SingleSelection);
    m_measurementView->verticalHeader()->setVisible(false);
    installProportionalHeader(m_measurementView, {2, 2, 1, 3, 1});
    details->addTab(m_measurementView, tr("Measurements"));

    m_runtimeTimelineView = new QTableView(details);
    m_runtimeTimelineView->setModel(m_runtimeTimelineModel);
    m_runtimeTimelineView->setObjectName(QStringLiteral("runtimeTimelineView"));
    m_runtimeTimelineView->setAlternatingRowColors(true);
    m_runtimeTimelineView->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_runtimeTimelineView->setSelectionMode(QAbstractItemView::SingleSelection);
    m_runtimeTimelineView->verticalHeader()->setVisible(false);
    installProportionalHeader(m_runtimeTimelineView, {1, 2, 2, 1, 2, 1, 5});
    details->addTab(m_runtimeTimelineView, tr("Timeline"));
    connect(m_runtimeTimelineView,
            &QTableView::clicked,
            this,
            &MainWindow::selectTimelineEvent);

    m_runtimeLogView = new QTableView(details);
    m_runtimeLogView->setModel(m_runtimeLogModel);
    m_runtimeLogView->setAlternatingRowColors(true);
    m_runtimeLogView->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_runtimeLogView->setSelectionMode(QAbstractItemView::SingleSelection);
    m_runtimeLogView->verticalHeader()->setVisible(false);
    installProportionalHeader(m_runtimeLogView, {2, 1, 2, 1, 6});
    details->addTab(m_runtimeLogView, tr("Logs"));

    m_debugSnapshotView = new QTableView(details);
    m_debugSnapshotView->setModel(m_debugSnapshotModel);
    m_debugSnapshotView->setObjectName(QStringLiteral("debugSnapshotView"));
    m_debugSnapshotView->setAlternatingRowColors(true);
    m_debugSnapshotView->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_debugSnapshotView->setSelectionMode(QAbstractItemView::SingleSelection);
    m_debugSnapshotView->verticalHeader()->setVisible(false);
    installProportionalHeader(m_debugSnapshotView, {2, 2, 5});
    details->addTab(m_debugSnapshotView, tr("Debug"));

    m_deviceStatusView = new QTableView(details);
    m_deviceStatusView->setModel(m_deviceStatusModel);
    m_deviceStatusView->setAlternatingRowColors(true);
    m_deviceStatusView->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_deviceStatusView->setSelectionMode(QAbstractItemView::SingleSelection);
    m_deviceStatusView->verticalHeader()->setVisible(false);
    installProportionalHeader(m_deviceStatusView, {2, 2, 3, 2, 5});
    details->addTab(m_deviceStatusView, tr("Devices"));

    auto* historyPage = new QWidget(m_workspaceTabs);
    historyPage->setObjectName(QStringLiteral("reportHistoryPage"));
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
    installProportionalHeader(m_historyView, {2, 3, 1, 1, 1, 1});
    historyLayout->addWidget(m_historyView, 1);

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
    installProportionalHeader(m_diagnosticView, {1, 2, 5, 3});
    details->addTab(m_diagnosticView, tr("Diagnostics"));

    runDataSplitter->setStretchFactor(0, 3);
    runDataSplitter->setStretchFactor(1, 2);
    runDataSplitter->setSizes({430, 250});
    splitter->setStretchFactor(0, 0);
    splitter->setStretchFactor(1, 1);
    splitter->setSizes({230, 930});
    runPageLayout->addWidget(splitter, 1);

    auto* footer = new QWidget(runPage);
    footer->setObjectName(QStringLiteral("adminRunFooter"));
    auto* footerLayout = new QHBoxLayout(footer);
    footerLayout->setContentsMargins(12, 7, 12, 7);
    footerLayout->setSpacing(12);
    m_adminProgress = new QProgressBar(footer);
    m_adminProgress->setObjectName(QStringLiteral("adminRunProgress"));
    m_adminProgress->setRange(0, 100);
    m_adminProgress->setValue(0);
    footerLayout->addWidget(m_adminProgress, 1);
    const auto createCounter = [footer](const QString& objectName) {
        auto* label = new QLabel(footer);
        label->setObjectName(objectName);
        label->setAlignment(Qt::AlignCenter);
        label->setMinimumWidth(82);
        return label;
    };
    m_adminPassCount = createCounter(QStringLiteral("adminPassCount"));
    m_adminFailCount = createCounter(QStringLiteral("adminFailCount"));
    m_adminTotalCount = createCounter(QStringLiteral("adminTotalCount"));
    m_adminYield = createCounter(QStringLiteral("adminYield"));
    m_adminYield->setMinimumWidth(110);
    footerLayout->addWidget(m_adminPassCount);
    footerLayout->addWidget(m_adminFailCount);
    footerLayout->addWidget(m_adminTotalCount);
    footerLayout->addWidget(m_adminYield);
    runPageLayout->addWidget(footer);

    m_workspaceTabs->insertTab(0, runPage, tr("Run Test"));
    m_workspaceTabs->addTab(historyPage, tr("Reports"));
    rootLayout->addWidget(m_workspaceTabs, 1);

    setCentralWidget(central);

    setStyleSheet(QStringLiteral(R"css(
        QWidget#adminRunPage { background: #f4f6f8; color: #20272e; }
        QLabel#adminSequenceLabel {
            background: #ffffff; border: 1px solid #cbd2d9; border-radius: 4px;
            font-size: 16px; font-weight: 600; padding: 8px 12px;
        }
        QFrame#adminRunSidebar, QWidget#adminRunFooter {
            background: #ffffff; border: 1px solid #cbd2d9; border-radius: 4px;
        }
        QLabel#adminSectionTitle { color: #33414d; font-size: 13px; font-weight: 700; }
        QLabel#adminMetricCaption { color: #687681; font-size: 11px; font-weight: 600; }
        QLabel#adminElapsedLabel {
            background: #e7f0f8; border: 1px solid #b9cedf; border-radius: 4px;
            color: #18384f; font-size: 20px; font-weight: 600; padding: 10px 6px;
        }
        QLabel#adminPassCount { color: #237744; font-weight: 700; }
        QLabel#adminFailCount { color: #b12f2f; font-weight: 700; }
        QLabel#adminTotalCount { color: #33414d; font-weight: 700; }
        QLabel#adminYield { color: #175b87; font-weight: 700; }
        QProgressBar#adminRunProgress {
            border: 1px solid #aeb9c2; background: #edf1f3;
            min-height: 23px; text-align: center;
        }
        QProgressBar#adminRunProgress::chunk { background: #5ca65c; }
        QTreeView, QTableView, QListView {
            selection-background-color: #cfe4f3;
            selection-color: #20272e;
        }
        QTreeView::item:hover, QTreeView::item:selected,
        QTableView::item:hover, QTableView::item:selected,
        QListView::item:hover, QListView::item:selected {
            background: #cfe4f3; color: #20272e;
        }
        QTreeView#pluginFunctionView::item,
        QTreeView#sequenceTreeView::item {
            min-height: 28px;
            padding: 3px 6px;
        }
        QTreeView#resultView::item {
            min-height: 29px;
            padding: 3px 6px;
        }
        QTabBar::tab:selected {
            background: #cfe4f3; color: #20272e;
        }
    )css"));

    m_adminElapsedTimer = new QTimer(this);
    m_adminElapsedTimer->setInterval(50);
    connect(m_adminElapsedTimer, &QTimer::timeout,
            this, &MainWindow::updateAdminElapsed);
    updateAdminRunState(UiRunState::Empty);
    updateAdminYield();
}

void MainWindow::chooseSequence()
{
    if (!maybeSaveSequence()) {
        return;
    }

    const QString path = QFileDialog::getOpenFileName(
        this,
        tr("Open Sequence"),
        m_sequenceDocument ? m_sequenceDocument->filePath() : QString(),
        tr("Sequence JSON (*.json);;All Files (*.*)"));
    if (path.isEmpty()) {
        return;
    }

    if (!openSequenceFile(path)) {
        const auto diagnostics = m_sequenceDocument->diagnostics();
        statusBar()->showMessage(
            diagnostics.isEmpty() ? tr("Failed to open sequence")
                                  : diagnostics.first().message);
        return;
    }
    statusBar()->showMessage(tr("Sequence loaded"), 3000);
}
void MainWindow::chooseStation()
{
    if (!maybeSaveStation()) {
        return;
    }
    const QString path = QFileDialog::getOpenFileName(
        this,
        tr("Open Station"),
        m_stationDocument ? m_stationDocument->filePath() : QString(),
        tr("Station JSON (*.json);;All Files (*.*)"));
    if (path.isEmpty()) {
        return;
    }
    if (!openStationFile(path)) {
        const auto diagnostics = m_stationDocument->diagnostics();
        statusBar()->showMessage(
            diagnostics.isEmpty() ? tr("Failed to open station")
                                  : diagnostics.first().message);
        return;
    }
    statusBar()->showMessage(tr("Station loaded"), 3000);
}

void MainWindow::updateCommandState()
{
    if (m_shuttingDown) {
        return;
    }
    const bool canChangeSources = m_viewModel->canChangeSources();
    m_openSequenceAction->setEnabled(canChangeSources);
    m_openStationAction->setEnabled(canChangeSources);
    m_compileAction->setEnabled(m_viewModel->canCompile());
    m_runAction->setEnabled(m_viewModel->canRun());
    m_pauseAction->setEnabled(m_viewModel->canPause());
    m_resumeAction->setEnabled(m_viewModel->canResume());
    m_stepIntoAction->setEnabled(m_viewModel->canStepInto());
    m_stepOverAction->setEnabled(m_viewModel->canStepOver());
    m_stopAction->setEnabled(m_viewModel->canStop());
    m_scanAction->setEnabled(m_viewModel->canRun());
    m_uutCount->setEnabled(canChangeSources);

    const bool hasDocument = m_sequenceDocument && !m_sequenceDocument->isEmpty();
    const auto selectedPath = m_sequenceTreeView
        ? m_sequenceTreeModel->pathForIndex(m_sequenceTreeView->currentIndex())
        : SequenceItemPath{};
    const bool hasSelection = selectedPath.isValid();
    const auto selectedStepPaths = selectedSequenceStepPaths();
    const bool stepSelected = hasSelection && !selectedPath.isGroup() &&
                              selectedStepPaths.size() == 1;

    m_saveSequenceAction->setEnabled(
        canChangeSources && hasDocument && m_sequenceDocument->isModified());
    m_saveSequenceAsAction->setEnabled(canChangeSources && hasDocument);
    m_undoAction->setEnabled(
        canChangeSources && m_sequenceDocument->undoStack()->canUndo());
    m_redoAction->setEnabled(
        canChangeSources && m_sequenceDocument->undoStack()->canRedo());
    m_addStepAction->setEnabled(canChangeSources && hasDocument && hasSelection);
    const bool hasSelectedSteps = !selectedStepPaths.isEmpty();
    m_deleteStepAction->setEnabled(canChangeSources && hasSelectedSteps);
    m_duplicateStepAction->setEnabled(canChangeSources && hasSelectedSteps);
    m_enableStepsAction->setEnabled(canChangeSources && hasSelectedSteps);
    m_disableStepsAction->setEnabled(canChangeSources && hasSelectedSteps);
    m_wrapTestItemAction->setEnabled(
        canChangeSources &&
        m_sequenceDocument->canWrapStepsInTestItem(selectedStepPaths));

    bool canMoveUp = false;
    bool canMoveDown = false;
    if (stepSelected) {
        auto parentPath = selectedPath;
        const int row = parentPath.stepIndices.takeLast();
        const int count = m_sequenceDocument->objectAt(parentPath)
                              .value("steps").toArray().size();
        canMoveUp = row > 0;
        canMoveDown = row >= 0 && row + 1 < count;
    }
    m_moveStepUpAction->setEnabled(canChangeSources && canMoveUp);
    m_moveStepDownAction->setEnabled(canChangeSources && canMoveDown);
    if (m_sequenceTreeView) {
        m_sequenceTreeView->setEnabled(canChangeSources);
    }
    if (m_pluginFunctionView) {
        m_pluginFunctionView->setEnabled(canChangeSources);
    }
    if (m_stepPropertyEditor) {
        m_stepPropertyEditor->setEditable(canChangeSources);
    }

    const bool hasStation = m_stationDocument && !m_stationDocument->isEmpty();
    const int stationRow = m_stationDeviceView && m_stationDeviceView->currentIndex().isValid()
        ? m_stationDeviceView->currentIndex().row()
        : m_selectedStationDeviceRow;
    const bool hasDevice = hasStation && stationRow >= 0 &&
                           stationRow < m_stationDocument->deviceCount();
    m_saveStationAction->setEnabled(
        canChangeSources && hasStation && m_stationDocument->isModified());
    m_saveStationAsAction->setEnabled(canChangeSources && hasStation);
    m_stationUndoAction->setEnabled(
        canChangeSources && m_stationDocument->undoStack()->canUndo());
    m_stationRedoAction->setEnabled(
        canChangeSources && m_stationDocument->undoStack()->canRedo());
    m_addDeviceAction->setEnabled(canChangeSources && hasStation);
    m_deleteDeviceAction->setEnabled(canChangeSources && hasDevice);
    m_duplicateDeviceAction->setEnabled(canChangeSources && hasDevice);
    m_moveDeviceUpAction->setEnabled(canChangeSources && hasDevice && stationRow > 0);
    m_moveDeviceDownAction->setEnabled(
        canChangeSources && hasDevice && stationRow + 1 < m_stationDocument->deviceCount());
    const auto selectedDevice = hasDevice
        ? m_stationDocument->deviceAt(stationRow)
        : QJsonObject{};
    const bool enabledDevice = !selectedDevice.isEmpty() &&
                               selectedDevice.value("enabled").toBool(true);
    m_testDeviceConnectionAction->setEnabled(
        m_viewModel->canTestDeviceConnection() && enabledDevice);
    if (m_connectionTimeoutMs) {
        m_connectionTimeoutMs->setEnabled(
            m_viewModel->canTestDeviceConnection() && enabledDevice);
    }
    if (m_stationDeviceView) {
        m_stationDeviceView->setEnabled(canChangeSources);
    }
    if (m_stationPropertyEditor) {
        m_stationPropertyEditor->setEditable(canChangeSources);
    }
    if (m_stationSettingsEditor) {
        m_stationSettingsEditor->setEditable(canChangeSources);
    }
}
void MainWindow::updateDiagnostics()
{
    m_diagnosticModel->setDiagnostics(m_viewModel->diagnostics());
}

void MainWindow::updateCompilePreview()
{
    const auto summary = m_viewModel->compileSummary();
    if (!summary.success) {
        return;
    }
    m_adminPreviewReport = summary.previewReport;
    m_adminTotalNodes = qMax(1, summary.nodeCount);
    m_adminTerminalNodes.clear();
    auto preview = m_adminPreviewReport;
    if (!preview.uuts.isEmpty()) {
        preview.uuts.first().uutId.clear();
    }
    displayReport(preview);
    m_resultView->clearSelection();
    m_resultView->setCurrentIndex({});
    m_attemptModel->setStep(std::nullopt);
    m_measurementModel->setMeasurements({});
    updateAdminProgress();
}

void MainWindow::updateAdminRunState(UiRunState state)
{
    if (!m_adminOverallResult) {
        return;
    }
    m_adminOverallResult->setText(adminRunStateText(state));
    m_adminOverallResult->setStyleSheet(adminRunStateStyle(state));
    if (state == UiRunState::Starting) {
        m_adminElapsed.restart();
        m_adminElapsedTimer->start();
    }
    if (state == UiRunState::Completed || state == UiRunState::Failed) {
        m_adminElapsedTimer->stop();
        updateAdminElapsed();
        m_adminProgress->setValue(100);
    }
}

void MainWindow::updateAdminStationSummary()
{
    if (!m_adminStationLabel || !m_stationDocument ||
        m_stationDocument->filePath().isEmpty()) {
        return;
    }
    const auto result = PicoATE::Core::loadStationConfigFile(
        m_stationDocument->filePath());
    const auto stationId = result.config.stationId.isEmpty()
        ? QFileInfo(m_stationDocument->filePath()).completeBaseName()
        : result.config.stationId;
    m_adminStationLabel->setText(stationId);
    m_adminOrderLabel->setText(stationMetadataValue(
        result.config.metadata, {"order", "orderNumber"}));
    m_adminTesterLabel->setText(stationMetadataValue(
        result.config.metadata, {"tester", "operator"}));
    m_adminJigLabel->setText(stationMetadataValue(
        result.config.metadata, {"jigNo", "fixtureId", "fixture"}));
}

void MainWindow::updateAdminProgress()
{
    if (!m_adminProgress) {
        return;
    }
    m_adminProgress->setValue(m_adminTotalNodes > 0
        ? qMin(100, m_adminTerminalNodes.size() * 100 / m_adminTotalNodes)
        : 0);
}

void MainWindow::updateAdminYield()
{
    if (!m_adminPassCount) {
        return;
    }
    const int total = m_adminPassedUnits + m_adminFailedUnits;
    const double yield = total > 0
        ? static_cast<double>(m_adminPassedUnits) * 100.0 / total
        : 0.0;
    m_adminPassCount->setText(tr("PASS %1").arg(m_adminPassedUnits));
    m_adminFailCount->setText(tr("FAIL %1").arg(m_adminFailedUnits));
    m_adminTotalCount->setText(tr("TOTAL %1").arg(total));
    m_adminYield->setText(tr("YIELD %1%").arg(yield, 0, 'f', 2));
}

void MainWindow::updateAdminElapsed()
{
    if (!m_adminElapsedLabel) {
        return;
    }
    const qint64 elapsed = m_adminElapsed.isValid() ? m_adminElapsed.elapsed() : 0;
    m_adminElapsedLabel->setText(QStringLiteral("%1:%2.%3")
        .arg(elapsed / 60000, 2, 10, QLatin1Char('0'))
        .arg(elapsed / 1000 % 60, 2, 10, QLatin1Char('0'))
        .arg(elapsed % 1000, 3, 10, QLatin1Char('0')));
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
    if (report.completed && !m_currentAdminRunCounted) {
        if (report.state == PicoATE::Core::ExecutionState::Completed) {
            ++m_adminPassedUnits;
            m_currentAdminRunCounted = true;
        } else if (report.state == PicoATE::Core::ExecutionState::CompletedWithError) {
            ++m_adminFailedUnits;
            m_currentAdminRunCounted = true;
        }
        updateAdminYield();
    }
    displayReport(report);
}

void MainWindow::updateDebugSnapshot()
{
    m_debugSnapshotModel->setSnapshot(m_viewModel->debugSnapshot());
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
    m_runtimeTimelineModel->applyRuntimeEvents(events);
    m_runtimeLogModel->applyRuntimeEvents(events);
    if (m_runtimeTimelineView->model()->rowCount() > 0) {
        m_runtimeTimelineView->scrollToBottom();
    }
    if (m_runtimeLogView->model()->rowCount() > 0) {
        m_runtimeLogView->scrollToBottom();
    }
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

    for (const auto& event : events) {
        if (!event.nodeId.isEmpty() && adminIsTerminalActivation(event.activationState)) {
            m_adminTerminalNodes.insert(event.nodeId);
        }
        if (event.kind == PicoATE::Core::RuntimeEventKind::BreakpointHit ||
            event.kind == PicoATE::Core::RuntimeEventKind::DebugStepCompleted) {
            focusDebugNode(event);
        }
    }
    updateAdminProgress();
}

void MainWindow::selectRuntimeEvent(const PicoATE::Core::RuntimeEvent& event)
{
    if (!event.uutId.isEmpty() && !event.nodeId.isEmpty() && m_resultView) {
        const auto resultIndex = m_uutStepModel->indexForStep(event.uutId, event.nodeId);
        if (resultIndex.isValid()) {
            m_resultView->setCurrentIndex(resultIndex);
            m_resultView->scrollTo(resultIndex, QAbstractItemView::PositionAtCenter);
            updateStepDetails(resultIndex);
        }
    }

    if (!event.nodeId.isEmpty() && m_sequenceTreeModel && m_sequenceTreeView) {
        m_sequenceTreeModel->setCurrentDebugNodePath(event.nodeId);
        const auto index = m_sequenceTreeModel->indexForNodePath(event.nodeId);
        if (index.isValid()) {
            m_selectedSequencePath = m_sequenceTreeModel->pathForIndex(index);
            m_sequenceTreeView->setCurrentIndex(index);
            m_sequenceTreeView->scrollTo(index, QAbstractItemView::PositionAtCenter);
            if (m_stepPropertyEditor) {
                m_stepPropertyEditor->setCurrentItem(m_selectedSequencePath);
            }
        }
    }

    const auto kindName = PicoATE::Core::runtimeEventKindName(event.kind);
    const auto stepName = event.nodeDisplayName.isEmpty() ? event.nodeId
                                                         : event.nodeDisplayName;
    QString message = stepName.isEmpty()
        ? kindName
        : tr("%1: %2").arg(kindName, stepName);
    if (!event.message.isEmpty()) {
        message = tr("%1 - %2").arg(message, event.message);
    }
    statusBar()->showMessage(message, 5000);
}

void MainWindow::selectTimelineSequence(quint64 sequenceNumber)
{
    if (!m_runtimeTimelineModel || !m_runtimeTimelineView) {
        return;
    }

    const int row = m_runtimeTimelineModel->rowForSequenceNumber(sequenceNumber);
    if (row < 0) {
        return;
    }
    const auto index = m_runtimeTimelineModel->index(row, RuntimeTimelineModel::EventColumn);
    m_runtimeTimelineView->setCurrentIndex(index);
    m_runtimeTimelineView->scrollTo(index, QAbstractItemView::PositionAtCenter);
}

void MainWindow::selectTimelineEvent(const QModelIndex& index)
{
    if (!index.isValid() || !m_runtimeTimelineModel) {
        return;
    }

    const auto event = m_runtimeTimelineModel->eventAt(index.row());
    if (!event) {
        return;
    }
    selectRuntimeEvent(*event);
}

void MainWindow::focusDebugNode(const PicoATE::Core::RuntimeEvent& event)
{
    if (event.nodeId.isEmpty() || !m_sequenceTreeModel || !m_sequenceTreeView) {
        return;
    }

    selectTimelineSequence(event.sequenceNumber);
    selectRuntimeEvent(event);

    const auto index = m_sequenceTreeModel->indexForNodePath(event.nodeId);
    if (!index.isValid()) {
        statusBar()->showMessage(event.message, 8000);
        return;
    }
    const auto localPath = m_sequenceTreeModel->localPathForIndex(index);
    if (event.kind == PicoATE::Core::RuntimeEventKind::BreakpointHit) {
        statusBar()->showMessage(
            tr("Breakpoint hit: %1").arg(localPath.isEmpty() ? event.nodeId : localPath),
            8000);
    } else {
        statusBar()->showMessage(
            tr("Paused after step: %1").arg(localPath.isEmpty() ? event.nodeId : localPath),
            5000);
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

void MainWindow::restoreUiSettings()
{
    QSettings settings;
    m_recentSequences = settings.value(
        QStringLiteral("Recent/Sequences")).toStringList();
    m_recentStations = settings.value(
        QStringLiteral("Recent/Stations")).toStringList();
    refreshRecentFileMenus();

    settings.beginGroup(QStringLiteral("MainWindow"));
    const auto geometry = settings.value(QStringLiteral("Geometry")).toByteArray();
    const bool restoredGeometry = !geometry.isEmpty() && restoreGeometry(geometry);
    if (!restoredGeometry || !isVisibleOnAnyScreen(frameGeometry())) {
        applyDefaultWindowGeometry(*this);
    }

    const auto windowState = settings.value(QStringLiteral("State")).toByteArray();
    if (!windowState.isEmpty()) {
        restoreState(windowState, 1);
    }

    const auto restoreSplitter = [&settings, this](const char* objectName,
                                                    const char* key) {
        auto* splitter = findChild<QSplitter*>(QString::fromLatin1(objectName));
        const auto state = settings.value(QString::fromLatin1(key)).toByteArray();
        if (splitter && !state.isEmpty()) {
            splitter->restoreState(state);
        }
    };
    restoreSplitter("sequenceVerticalSplitter", "SequenceVerticalSplitter");
    restoreSplitter("sequenceWorkSplitter", "SequenceWorkSplitter");
    restoreSplitter("stationVerticalSplitter", "StationVerticalSplitter");
    restoreSplitter("stationWorkSplitter", "StationWorkSplitterV2");
    restoreSplitter("runSplitter", "RunSplitter");

    const int workspaceTab = settings.value(QStringLiteral("WorkspaceTab"), 0).toInt();
    if (workspaceTab >= 0 && workspaceTab < m_workspaceTabs->count()) {
        m_workspaceTabs->setCurrentIndex(workspaceTab);
    }
    if (auto* details = findChild<QTabWidget*>(QStringLiteral("runDetailsTabs"))) {
        const int detailsTab = settings.value(QStringLiteral("RunDetailsTab"), 0).toInt();
        if (detailsTab >= 0 && detailsTab < details->count()) {
            details->setCurrentIndex(detailsTab);
        }
    }
    m_uutCount->setValue(settings.value(QStringLiteral("UutCount"), 1).toInt());
    m_connectionTimeoutMs->setValue(
        settings.value(QStringLiteral("ConnectionTimeoutMs"), 5000).toInt());
    settings.endGroup();
}

void MainWindow::saveUiSettings() const
{
    QSettings settings;
    settings.setValue(QStringLiteral("Recent/Sequences"), m_recentSequences);
    settings.setValue(QStringLiteral("Recent/Stations"), m_recentStations);

    settings.beginGroup(QStringLiteral("MainWindow"));
    settings.setValue(QStringLiteral("Geometry"), saveGeometry());
    settings.setValue(QStringLiteral("State"), saveState(1));
    const auto saveSplitter = [&settings, this](const char* objectName,
                                                const char* key) {
        if (const auto* splitter = findChild<QSplitter*>(QString::fromLatin1(objectName))) {
            settings.setValue(QString::fromLatin1(key), splitter->saveState());
        }
    };
    saveSplitter("sequenceVerticalSplitter", "SequenceVerticalSplitter");
    saveSplitter("sequenceWorkSplitter", "SequenceWorkSplitter");
    saveSplitter("stationVerticalSplitter", "StationVerticalSplitter");
    saveSplitter("stationWorkSplitter", "StationWorkSplitterV2");
    saveSplitter("runSplitter", "RunSplitter");
    settings.setValue(QStringLiteral("WorkspaceTab"), m_workspaceTabs->currentIndex());
    if (const auto* details = findChild<QTabWidget*>(QStringLiteral("runDetailsTabs"))) {
        settings.setValue(QStringLiteral("RunDetailsTab"), details->currentIndex());
    }
    settings.setValue(QStringLiteral("UutCount"), m_uutCount->value());
    settings.setValue(QStringLiteral("ConnectionTimeoutMs"),
                      m_connectionTimeoutMs->value());
    settings.endGroup();
    settings.sync();
}

void MainWindow::resetUiLayout()
{
    QSettings settings;
    settings.remove(QStringLiteral("MainWindow"));

    showNormal();
    applyDefaultWindowGeometry(*this);
    m_workspaceTabs->setCurrentIndex(0);
    if (auto* details = findChild<QTabWidget*>(QStringLiteral("runDetailsTabs"))) {
        details->setCurrentIndex(0);
    }
    if (auto* splitter = findChild<QSplitter*>(QStringLiteral("sequenceVerticalSplitter"))) {
        splitter->setSizes({520, 140});
    }
    if (auto* splitter = findChild<QSplitter*>(QStringLiteral("sequenceWorkSplitter"))) {
        splitter->setSizes({220, 560, 380});
    }
    if (auto* splitter = findChild<QSplitter*>(QStringLiteral("stationVerticalSplitter"))) {
        splitter->setSizes({520, 140});
    }
    if (auto* splitter = findChild<QSplitter*>(QStringLiteral("stationWorkSplitter"))) {
        splitter->setSizes({260, 610, 390});
    }
    if (auto* splitter = findChild<QSplitter*>(QStringLiteral("runSplitter"))) {
        splitter->setSizes({700, 460});
    }
    statusBar()->showMessage(tr("Default layout restored"), 3000);
}

void MainWindow::addRecentSequence(const QString& filePath)
{
    const auto path = normalizedRecentPath(filePath);
    m_recentSequences.removeAll(path);
    m_recentSequences.prepend(path);
    m_recentSequences = m_recentSequences.mid(0, MaxRecentFiles);
    refreshRecentFileMenus();
    QSettings().setValue(QStringLiteral("Recent/Sequences"), m_recentSequences);
}

void MainWindow::addRecentStation(const QString& filePath)
{
    const auto path = normalizedRecentPath(filePath);
    m_recentStations.removeAll(path);
    m_recentStations.prepend(path);
    m_recentStations = m_recentStations.mid(0, MaxRecentFiles);
    refreshRecentFileMenus();
    QSettings().setValue(QStringLiteral("Recent/Stations"), m_recentStations);
}

void MainWindow::refreshRecentFileMenus()
{
    const auto refreshMenu = [this](QMenu* menu,
                                    QStringList& paths,
                                    bool sequence) {
        if (!menu) {
            return;
        }
        menu->clear();
        paths.erase(std::remove_if(paths.begin(), paths.end(), [](const QString& path) {
            return !QFileInfo::exists(path);
        }), paths.end());
        for (int index = 0; index < paths.size(); ++index) {
            const auto path = paths.at(index);
            auto* action = menu->addAction(
                tr("%1  %2").arg(index + 1).arg(QFileInfo(path).fileName()));
            action->setToolTip(path);
            connect(action, &QAction::triggered, this, [this, path, sequence] {
                sequence ? openRecentSequence(path) : openRecentStation(path);
            });
        }
        menu->setEnabled(!paths.isEmpty());
    };
    refreshMenu(m_recentSequenceMenu, m_recentSequences, true);
    refreshMenu(m_recentStationMenu, m_recentStations, false);
}

void MainWindow::openRecentSequence(const QString& filePath)
{
    if (!maybeSaveSequence()) {
        return;
    }
    if (!openSequenceFile(filePath)) {
        m_recentSequences.removeAll(filePath);
        refreshRecentFileMenus();
        statusBar()->showMessage(tr("Recent sequence could not be opened"), 5000);
    }
}

void MainWindow::openRecentStation(const QString& filePath)
{
    if (!maybeSaveStation()) {
        return;
    }
    if (!openStationFile(filePath)) {
        m_recentStations.removeAll(filePath);
        refreshRecentFileMenus();
        statusBar()->showMessage(tr("Recent station could not be opened"), 5000);
    }
}

} // namespace PicoATE::Ui
