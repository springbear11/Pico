#include "MainWindow.h"

#include "ApplicationDiagnostics.h"
#include "ExecutionViewModel.h"
#include "FlowTargetSelector.h"
#include "LoadingSpinner.h"
#include "OnOffControl.h"
#include "OperatorPromptPresenter.h"
#include "PluginCatalog.h"
#include "PluginFunctionModel.h"
#include "ProportionalHeaderView.h"
#include "ReportExporter.h"
#include "ReportHistoryStore.h"
#include "RunnerModels.h"
#include "RunArtifactWriter.h"
#include "ScanDialog.h"
#include "SequenceDocument.h"
#include "SequenceEditorTreeView.h"
#include "SequenceTreeModel.h"
#include "SequenceVariablesDialog.h"
#include "StationDeviceModel.h"
#include "StationDocument.h"
#include "StationPropertyEditor.h"
#include "StationSettingsEditor.h"
#include "StepPropertyEditor.h"

#include <QAction>
#include <QCloseEvent>
#include <QComboBox>
#include <QCoreApplication>
#include <QDateTime>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QEvent>
#include <QFileDialog>
#include <QFileInfo>
#include <QFont>
#include <QFrame>
#include <QFormLayout>
#include <QGuiApplication>
#include <QGridLayout>
#include <QHeaderView>
#include <QHash>
#include <QHBoxLayout>
#include <QIcon>
#include <QItemSelectionModel>
#include <QJsonArray>
#include <QKeyEvent>
#include <QKeySequence>
#include <QLabel>
#include <QMessageBox>
#include <QLineEdit>
#include <QMenu>
#include <QMenuBar>
#include <QPainter>
#include <QMouseEvent>
#include <QPushButton>
#include <QProgressBar>
#include <QPointer>
#include <QPolygonF>
#include <QRegularExpression>
#include <QScreen>
#include <QScrollArea>
#include <QScrollBar>
#include <QSet>
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
#include <QThread>
#include <QTimer>
#include <QToolBar>
#include <QToolButton>
#include <QTreeView>
#include <QUndoStack>
#include <QVBoxLayout>
#include <QWidget>

#include "PicoATE/Core/StationConfig.h"

#include <optional>
#include <initializer_list>
#include <algorithm>
#include <functional>
#include <limits>

namespace PicoATE::Ui {

namespace {

constexpr int MaxRecentFiles = 8;
const QString StationDiagnosticPrefix = QStringLiteral("Station: ");

void collectDeviceStepReferences(const QJsonArray& steps,
                                 QHash<QString, QStringList>& references,
                                 bool enabledOnly = true)
{
    for (const auto& value : steps) {
        const auto step = value.toObject();
        if (step.isEmpty()) {
            continue;
        }
        const bool enabled = step.value(QStringLiteral("enabled")).toBool(true);
        if ((!enabledOnly || enabled) &&
            step.value(QStringLiteral("moduleId")).toString() ==
            QStringLiteral("device")) {
            const auto deviceId = step.value(QStringLiteral("inputs"))
                                      .toObject()
                                      .value(QStringLiteral("deviceId"))
                                      .toString()
                                      .trimmed();
            if (!deviceId.isEmpty()) {
                const auto stepName = step.value(QStringLiteral("name")).toString(
                    step.value(QStringLiteral("id")).toString());
                references[deviceId].push_back(stepName);
            }
        }
        if (!enabledOnly || enabled) {
            collectDeviceStepReferences(step.value(QStringLiteral("steps")).toArray(),
                                        references,
                                        enabledOnly);
        }
    }
}

QJsonArray replaceDeviceStepReferences(const QJsonArray& steps,
                                       const QString& sourceDeviceId,
                                       const QString& targetDeviceId,
                                       bool& changed)
{
    QJsonArray result;
    for (const auto& value : steps) {
        auto step = value.toObject();
        if (step.isEmpty()) {
            result.push_back(value);
            continue;
        }
        if (step.value(QStringLiteral("moduleId")).toString() ==
            QStringLiteral("device")) {
            auto inputs = step.value(QStringLiteral("inputs")).toObject();
            if (inputs.value(QStringLiteral("deviceId")).toString().trimmed() ==
                sourceDeviceId) {
                inputs.insert(QStringLiteral("deviceId"), targetDeviceId);
                step.insert(QStringLiteral("inputs"), inputs);
                changed = true;
            }
        }
        if (step.value(QStringLiteral("steps")).isArray()) {
            step.insert(
                QStringLiteral("steps"),
                replaceDeviceStepReferences(
                    step.value(QStringLiteral("steps")).toArray(),
                    sourceDeviceId,
                    targetDeviceId,
                    changed));
        }
        result.push_back(step);
    }
    return result;
}

QString stationDeviceId(const QJsonObject& device)
{
    return device.value(QStringLiteral("deviceId")).toString(
        device.value(QStringLiteral("id")).toString()).trimmed();
}

QString stationDeviceType(const QJsonObject& device)
{
    return device.value(QStringLiteral("deviceType")).toString(
        device.value(QStringLiteral("type")).toString()).trimmed().toUpper();
}

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

class RunTestBreakpointDelegate final : public QStyledItemDelegate
{
public:
    explicit RunTestBreakpointDelegate(QTreeView* view)
        : QStyledItemDelegate(view)
        , m_view(view)
    {
        setObjectName(QStringLiteral("runTestBreakpointDelegate"));
        setProperty("visualBreakpointCount", 0);
        setProperty("currentNodePath", QString{});
    }

    std::function<void(const QString&, bool)> breakpointToggled;

    void setBreakpointKeys(QSet<QString> keys)
    {
        if (m_breakpointKeys == keys) {
            return;
        }
        m_breakpointKeys = std::move(keys);
        setProperty("visualBreakpointCount", m_breakpointKeys.size());
        if (m_view && m_view->viewport()) {
            m_view->viewport()->update();
        }
    }

    void paint(QPainter* painter,
               const QStyleOptionViewItem& option,
               const QModelIndex& index) const override
    {
        const auto key = breakpointKey(index);
        if (index.column() != UutStepModel::BreakpointVisualColumn) {
            QStyledItemDelegate::paint(painter, option, index);
            return;
        }

        painter->fillRect(option.rect, option.palette.base());
        if (key.isEmpty()) {
            return;
        }

        const auto* stepModel = qobject_cast<const UutStepModel*>(index.model());
        const int lineNumber = stepModel ? stepModel->visualLineNumber(index) : 0;
        if (lineNumber > 0) {
            QFont lineNumberFont = option.font;
            lineNumberFont.setWeight(QFont::Normal);
            if (lineNumberFont.pointSizeF() > 0.0) {
                lineNumberFont.setPointSizeF(
                    qMax(7.5, lineNumberFont.pointSizeF() - 1.5));
            } else if (lineNumberFont.pixelSize() > 0) {
                lineNumberFont.setPixelSize(qMax(9, lineNumberFont.pixelSize() - 2));
            }

            painter->save();
            painter->setFont(lineNumberFont);
            painter->setPen(QColor(QStringLiteral("#7b8790")));
            const QRect lineNumberRect(
                option.rect.left() + BreakpointMarkerWidth,
                option.rect.top(),
                option.rect.width() - BreakpointMarkerWidth - 3,
                option.rect.height());
            painter->drawText(lineNumberRect,
                              Qt::AlignRight | Qt::AlignVCenter,
                              QString::number(lineNumber));
            painter->restore();
        }

        const bool current = property("currentNodePath").toString() == key;
        const bool active = m_breakpointKeys.contains(key);
        const bool hovered = option.state & QStyle::State_MouseOver;
        if (!current && !active && !hovered) {
            return;
        }

        const QPoint center(option.rect.left() + BreakpointMarkerWidth / 2,
                            option.rect.center().y());

        painter->save();
        painter->setRenderHint(QPainter::Antialiasing, true);
        if (current) {
            const qreal centerY = center.y();
            const qreal left = option.rect.left() + 2.0;
            const qreal neck = option.rect.left() + 8.0;
            const qreal tip = option.rect.left() + BreakpointMarkerWidth - 1.0;
            const QPolygonF arrow({
                QPointF(left, centerY - 3.0),
                QPointF(neck, centerY - 3.0),
                QPointF(neck, centerY - 6.0),
                QPointF(tip, centerY),
                QPointF(neck, centerY + 6.0),
                QPointF(neck, centerY + 3.0),
                QPointF(left, centerY + 3.0)});
            painter->setPen(QPen(QColor(QStringLiteral("#9a6400")), 1.0));
            painter->setBrush(QColor(QStringLiteral("#f2b63d")));
            painter->drawPolygon(arrow);
        } else if (active) {
            painter->setPen(QPen(QColor(QStringLiteral("#a51d14")), 1.0));
            painter->setBrush(QColor(QStringLiteral("#e13a2d")));
            painter->drawEllipse(center, BreakpointRadius, BreakpointRadius);
        } else {
            painter->setPen(QPen(QColor(QStringLiteral("#d96a61")), 1.0));
            painter->setBrush(QColor(QStringLiteral("#f3b2ad")));
            painter->drawEllipse(center, BreakpointRadius, BreakpointRadius);
        }
        painter->restore();
    }

    bool editorEvent(QEvent* event,
                     QAbstractItemModel* model,
                     const QStyleOptionViewItem& option,
                     const QModelIndex& index) override
    {
        Q_UNUSED(model);
        if (event->type() != QEvent::MouseButtonRelease ||
            index.column() != UutStepModel::BreakpointVisualColumn) {
            return QStyledItemDelegate::editorEvent(event, model, option, index);
        }

        const auto* mouseEvent = static_cast<QMouseEvent*>(event);
        const QRect markerRect(option.rect.left(),
                               option.rect.top(),
                               BreakpointMarkerWidth,
                               option.rect.height());
        const auto key = breakpointKey(index);
        if (mouseEvent->button() != Qt::LeftButton || key.isEmpty() ||
            !markerRect.contains(mouseEvent->position().toPoint())) {
            return QStyledItemDelegate::editorEvent(event, model, option, index);
        }

        const bool enabled = !m_breakpointKeys.contains(key);
        if (enabled) {
            m_breakpointKeys.insert(key);
        } else {
            m_breakpointKeys.remove(key);
        }
        setProperty("visualBreakpointCount", m_breakpointKeys.size());
        if (m_view && m_view->viewport()) {
            m_view->viewport()->update();
        }
        if (breakpointToggled) {
            breakpointToggled(key, enabled);
        }
        return true;
    }

private:
    QString breakpointKey(const QModelIndex& index) const
    {
        const auto* model = qobject_cast<const UutStepModel*>(index.model());
        if (!model || model->itemType(index) != UutStepModel::StepItem) {
            return {};
        }
        const auto step = model->stepAt(index);
        if (!step) {
            return {};
        }
        return step->nodePath.isEmpty() ? step->stepId : step->nodePath;
    }

    static constexpr int BreakpointRadius = 4;
    static constexpr int BreakpointMarkerWidth = 16;
    QPointer<QTreeView> m_view;
    QSet<QString> m_breakpointKeys;
};

class FlowResourceLockDelegate final : public QStyledItemDelegate
{
public:
    explicit FlowResourceLockDelegate(QTreeView* view)
        : QStyledItemDelegate(view)
        , m_view(view)
    {
        setObjectName(QStringLiteral("flowResourceLockDelegate"));
        setProperty("expectUnlock", false);
    }

    std::function<void(const QModelIndex&)> boundaryClicked;

    void paint(QPainter* painter,
               const QStyleOptionViewItem& option,
               const QModelIndex& index) const override
    {
        QStyleOptionViewItem baseOption(option);
        baseOption.text.clear();
        baseOption.icon = {};
        QStyledItemDelegate::paint(painter, baseOption, index);

        if (index.column() != SequenceTreeModel::ResourceRegionColumn ||
            !index.data(SequenceTreeModel::ResourceBoundaryEligibleRole).toBool()) {
            return;
        }
        const int marker = index.data(SequenceTreeModel::ResourceMarkerRole).toInt();
        const bool hovered = option.state & QStyle::State_MouseOver;
        if (marker == 0 && !hovered) {
            return;
        }

        const bool unlocked = marker == 2 ||
            (marker == 0 && property("expectUnlock").toBool());
        const bool singleItem = marker == 3;
        const QColor color = marker == 0
            ? QColor(QStringLiteral("#9eb4c2"))
            : (singleItem ? QColor(QStringLiteral("#476f8b"))
                          : (unlocked ? QColor(QStringLiteral("#27856f"))
                                      : QColor(QStringLiteral("#2e75a3"))));
        const QPointF center = option.rect.center();
        const QRectF body(center.x() - 6.0, center.y() - 1.0, 12.0, 9.0);

        painter->save();
        painter->setRenderHint(QPainter::Antialiasing, true);
        painter->setPen(QPen(color, 1.7, Qt::SolidLine, Qt::RoundCap));
        painter->setBrush(marker == 0 ? Qt::NoBrush : color.lighter(175));
        painter->drawRoundedRect(body, 1.8, 1.8);

        if (unlocked) {
            const QRectF shackle(center.x() - 2.5, center.y() - 8.0, 9.0, 10.0);
            painter->drawArc(shackle, 20 * 16, 145 * 16);
            painter->drawLine(QPointF(center.x() - 2.5, center.y() - 3.0),
                              QPointF(center.x() - 2.5, center.y() - 0.5));
        } else {
            const QRectF shackle(center.x() - 4.5, center.y() - 8.0, 9.0, 10.0);
            painter->drawArc(shackle, 0, 180 * 16);
            painter->drawLine(QPointF(center.x() - 4.5, center.y() - 3.0),
                              QPointF(center.x() - 4.5, center.y() - 0.5));
            painter->drawLine(QPointF(center.x() + 4.5, center.y() - 3.0),
                              QPointF(center.x() + 4.5, center.y() - 0.5));
        }
        if (singleItem) {
            painter->setBrush(Qt::NoBrush);
            painter->setPen(QPen(color, 1.25, Qt::SolidLine, Qt::RoundCap));
            const QRectF cycle(center.x() - 9.0, center.y() - 10.0, 18.0, 18.0);
            painter->drawArc(cycle, 45 * 16, 270 * 16);
            const QPointF arrowTip(center.x() + 6.4, center.y() + 6.4);
            painter->drawLine(arrowTip, QPointF(center.x() + 2.8, center.y() + 6.0));
            painter->drawLine(arrowTip, QPointF(center.x() + 6.7, center.y() + 2.8));
        }
        painter->restore();
    }

    bool editorEvent(QEvent* event,
                     QAbstractItemModel* model,
                     const QStyleOptionViewItem& option,
                     const QModelIndex& index) override
    {
        Q_UNUSED(model);
        Q_UNUSED(option);
        if (event->type() != QEvent::MouseButtonRelease ||
            index.column() != SequenceTreeModel::ResourceRegionColumn ||
            !index.data(SequenceTreeModel::ResourceBoundaryEligibleRole).toBool()) {
            return QStyledItemDelegate::editorEvent(event, model, option, index);
        }
        const auto* mouseEvent = static_cast<QMouseEvent*>(event);
        if (mouseEvent->button() != Qt::LeftButton) {
            return false;
        }
        if (boundaryClicked) {
            boundaryClicked(index);
        }
        if (m_view && m_view->viewport()) {
            m_view->viewport()->update();
        }
        return true;
    }

private:
    QPointer<QTreeView> m_view;
};

class PluginFunctionTreeView final : public QTreeView
{
public:
    using QTreeView::QTreeView;

    std::function<void()> deviceSelectionRequired;

protected:
    void startDrag(Qt::DropActions supportedActions) override
    {
        const auto* functions = qobject_cast<PluginFunctionModel*>(model());
        if (functions && functions->requiresDeviceSelection(currentIndex())) {
            if (deviceSelectionRequired) {
                deviceSelectionRequired();
            }
            return;
        }
        QTreeView::startDrag(supportedActions);
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
    m_operatorPromptPresenter = new OperatorPromptPresenter(m_viewModel, this, this);
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
    m_runArtifactWriter = std::make_unique<RunArtifactWriter>();
    m_uutStepModel = new UutStepModel(this);
    m_uutStepModel->setSingleUutPhaseLayout(true);
    m_attemptModel = new AttemptModel(this);
    m_measurementModel = new MeasurementModel(this);
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
            &ExecutionViewModel::runIterationStarted,
            this,
            &MainWindow::beginAdminRunIteration);
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
                    m_runtimeTimelineModel->clear();
                    m_sequenceTreeModel->setCurrentDebugNodePath({});
                }
                if (state == UiRunState::Completed || state == UiRunState::Failed) {
                    m_sequenceTreeModel->setCurrentDebugNodePath({});
                }
                updateAdminRunState(state);
                if (state == UiRunState::CompileFailed) {
                    if (m_workspaceTabs) {
                        m_workspaceTabs->setCurrentIndex(0);
                    }
                    if (auto* details = findChild<QTabWidget*>(
                            QStringLiteral("runDetailsTabs"))) {
                        details->setCurrentWidget(m_diagnosticView);
                    }
                    if (m_diagnosticView && m_diagnosticModel->rowCount() > 0) {
                        const auto first = m_diagnosticModel->index(0, 0);
                        m_diagnosticView->setCurrentIndex(first);
                        m_diagnosticView->scrollTo(first);
                    }
                    const auto diagnostics = m_viewModel->diagnostics();
                    const auto summary = diagnostics.isEmpty()
                        ? tr("Compile failed. Open Diagnostics for details.")
                        : tr("Compile failed: %1 [%2]")
                              .arg(diagnostics.first().message,
                                   diagnostics.first().path.isEmpty()
                                       ? tr("root")
                                       : diagnostics.first().path);
                    statusBar()->showMessage(summary, 15000);
                } else {
                    statusBar()->showMessage(uiRunStateName(state));
                }
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
                ApplicationDiagnostics::recordAction(
                    QStringLiteral("FLOW_DOCUMENT_CHANGED"),
                    m_sequenceDocument->displayName());
                m_viewModel->invalidateSequenceDocument();
                updateSequenceEditor();
            });
    connect(m_sequenceDocument,
            &SequenceDocument::diagnosticsChanged,
            this,
            [this] {
                refreshEditorDiagnostics();
                updateWindowTitle();
                updateCommandState();
            });
    connect(m_sequenceTreeModel,
            &QAbstractItemModel::modelAboutToBeReset,
            this,
            &MainWindow::captureSequenceTreeViewState);
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
    connect(m_stepPropertyEditor,
            &StepPropertyEditor::pendingChangesChanged,
            this,
            [this] {
                updateWindowTitle();
                updateCommandState();
            });
    connect(m_sequenceTreeView->selectionModel(),
            &QItemSelectionModel::currentChanged,
            this,
            [this](const QModelIndex& current, const QModelIndex&) {
                if (m_handlingSequenceSelection) {
                    return;
                }
                auto path = m_sequenceTreeModel->pathForIndex(current);
                const auto requestedNodePath =
                    m_sequenceTreeModel->nodePathForIndex(current);
                auto selectedIndex = current;
                const auto previousPath = m_stepPropertyEditor->currentPath();
                if (path != previousPath &&
                    m_stepPropertyEditor->hasPendingChanges()) {
                    m_handlingSequenceSelection = true;
                    if (!resolvePendingStepChanges()) {
                        const auto previousIndex =
                            m_sequenceTreeModel->indexForPath(previousPath);
                        if (previousIndex.isValid()) {
                            m_sequenceTreeView->setCurrentIndex(previousIndex);
                        }
                        m_handlingSequenceSelection = false;
                        return;
                    }
                    auto refreshed = requestedNodePath.isEmpty()
                        ? QModelIndex{}
                        : m_sequenceTreeModel->indexForNodePath(
                              requestedNodePath);
                    if (!refreshed.isValid()) {
                        refreshed = m_sequenceTreeModel->indexForPath(path);
                    }
                    if (refreshed.isValid()) {
                        m_sequenceTreeView->setCurrentIndex(refreshed);
                    }
                    selectedIndex = refreshed;
                    m_handlingSequenceSelection = false;
                }
                path = m_sequenceTreeModel->pathForIndex(selectedIndex);
                if (path.isValid()) {
                    m_selectedSequencePath = path;
                    m_selectedSequenceNodePath =
                        m_sequenceTreeModel->nodePathForIndex(selectedIndex);
                }
                m_stepPropertyEditor->setCurrentItem(path);
                updateCommandState();
            });
    connect(m_pluginFunctionView->selectionModel(),
            &QItemSelectionModel::currentChanged,
            this,
            [this](const QModelIndex& current, const QModelIndex& previous) {
                if (m_handlingSequenceSelection) {
                    return;
                }
                if (m_stepPropertyEditor->hasPendingChanges()) {
                    m_handlingSequenceSelection = true;
                    if (!resolvePendingStepChanges()) {
                        m_pluginFunctionView->setCurrentIndex(previous);
                        m_handlingSequenceSelection = false;
                        return;
                    }
                    m_handlingSequenceSelection = false;
                }
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
                const auto index = m_sequenceTreeModel->indexForPath(to);
                if (index.isValid()) {
                    m_selectedSequenceNodePath =
                        m_sequenceTreeModel->nodePathForIndex(index);
                    m_sequenceTreeView->setCurrentIndex(index);
                    m_sequenceTreeView->scrollTo(
                        index, QAbstractItemView::EnsureVisible);
                    m_stepPropertyEditor->setCurrentItem(to);
                }
                updateCommandState();
            });
    connect(m_sequenceTreeModel, &SequenceTreeModel::itemsMoved,
            this,
            [this](const QVector<SequenceItemPath>&,
                   const QVector<SequenceItemPath>& destinations) {
                if (destinations.isEmpty()) {
                    return;
                }
                auto* selection = m_sequenceTreeView->selectionModel();
                selection->clearSelection();
                for (const auto& destination : destinations) {
                    const auto index = m_sequenceTreeModel->indexForPath(destination);
                    if (index.isValid()) {
                        selection->select(
                            index,
                            QItemSelectionModel::Select |
                                QItemSelectionModel::Rows);
                    }
                }
                m_selectedSequencePath = destinations.first();
                const auto current = m_sequenceTreeModel->indexForPath(
                    m_selectedSequencePath);
                if (current.isValid()) {
                    m_selectedSequenceNodePath =
                        m_sequenceTreeModel->nodePathForIndex(current);
                    m_sequenceTreeView->setCurrentIndex(current);
                    m_sequenceTreeView->scrollTo(
                        current, QAbstractItemView::EnsureVisible);
                    m_stepPropertyEditor->setCurrentItem(
                        m_selectedSequencePath);
                }
                updateCommandState();
            });
    connect(m_sequenceTreeModel, &SequenceTreeModel::itemInserted,
            this, [this](const SequenceItemPath& path) {
                m_selectedSequencePath = path;
                const auto index = m_sequenceTreeModel->indexForPath(path);
                if (index.isValid()) {
                    m_selectedSequenceNodePath =
                        m_sequenceTreeModel->nodePathForIndex(index);
                    m_sequenceTreeView->setCurrentIndex(index);
                    m_sequenceTreeView->scrollTo(
                        index, QAbstractItemView::EnsureVisible);
                    m_stepPropertyEditor->setCurrentItem(path);
                }
                updateCommandState();
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
    connect(m_stationPropertyEditor,
            &StationPropertyEditor::pendingChangesChanged,
            this,
            [this] {
                updateWindowTitle();
                updateCommandState();
            });
    connect(m_stationSettingsEditor,
            &StationSettingsEditor::pendingChangesChanged,
            this,
            [this] {
                updateWindowTitle();
                updateCommandState();
            });
    connect(m_stationDeviceView->selectionModel(),
            &QItemSelectionModel::currentChanged,
            this,
            [this](const QModelIndex& current, const QModelIndex&) {
                if (m_handlingStationSelection) {
                    return;
                }
                const int previousRow =
                    m_stationPropertyEditor->currentDeviceRow();
                const int currentRow = current.isValid()
                    ? m_stationDeviceModel->documentRow(current)
                    : -1;
                if (currentRow < 0 && previousRow >= 0 &&
                    previousRow < m_stationDocument->deviceCount()) {
                    return;
                }
                if (currentRow != previousRow &&
                    m_stationPropertyEditor->hasPendingChanges()) {
                    m_handlingStationSelection = true;
                    if (!resolvePendingStationDeviceChanges()) {
                        const auto previousIndex =
                            m_stationDeviceModel->indexForDocumentRow(previousRow);
                        if (previousIndex.isValid()) {
                            m_stationDeviceView->setCurrentIndex(previousIndex);
                        }
                        m_handlingStationSelection = false;
                        return;
                    }
                    m_handlingStationSelection = false;
                }
                m_selectedStationDeviceRow = currentRow;
                m_stationPropertyEditor->setCurrentDevices(
                    m_stationDeviceModel->documentRows(current),
                    m_stationDeviceModel->logicalBaseId(current));
                updateCommandState();
            });
    connect(m_stationDiagnosticView, &QTableView::clicked,
            this, &MainWindow::focusStationDiagnostic);
    connect(m_stationDocument->undoStack(), &QUndoStack::canUndoChanged,
            this, [this] { updateCommandState(); });
    connect(m_stationDocument->undoStack(), &QUndoStack::canRedoChanged,
            this, [this] { updateCommandState(); });
    m_previousWorkspaceTabIndex = m_workspaceTabs->currentIndex();
    connect(m_workspaceTabs, &QTabWidget::currentChanged,
            this, [this](int currentIndex) {
                if (m_handlingWorkspaceTabChange) {
                    return;
                }
                const int previousIndex = m_previousWorkspaceTabIndex;
                const int flowIndex = m_workspaceTabs->indexOf(m_flowEditorPage);
                const bool leavingFlow = previousIndex == flowIndex &&
                                         currentIndex != flowIndex;
                if (leavingFlow && m_sequenceDocument) {
                    const auto returnToFlow = [this, previousIndex] {
                        m_handlingWorkspaceTabChange = true;
                        m_workspaceTabs->setCurrentIndex(previousIndex);
                        m_handlingWorkspaceTabChange = false;
                        m_previousWorkspaceTabIndex = previousIndex;
                        updateCommandState();
                    };
                    if (!resolvePendingStepChanges()) {
                        returnToFlow();
                        return;
                    }
                    if (m_sequenceDocument->isModified()) {
                        QMessageBox prompt(
                            QMessageBox::Question,
                            tr("Unsaved Flow Draft"),
                            tr("The Flow draft has changes. Save them to %1 before leaving?")
                                .arg(m_sequenceDocument->displayName()),
                            QMessageBox::NoButton,
                            this);
                        auto* saveButton = prompt.addButton(QMessageBox::Save);
                        auto* keepDraftButton = prompt.addButton(
                            tr("Keep Draft"), QMessageBox::AcceptRole);
                        auto* cancelButton = prompt.addButton(QMessageBox::Cancel);
                        prompt.setDefaultButton(
                            qobject_cast<QPushButton*>(saveButton));
                        prompt.exec();
                        if (prompt.clickedButton() == cancelButton ||
                            (prompt.clickedButton() == saveButton && !saveSequence())) {
                            returnToFlow();
                            return;
                        }
                        Q_UNUSED(keepDraftButton);
                    }
                }
                m_previousWorkspaceTabIndex = currentIndex;
                updateCommandState();
            });
    connect(m_resultView->selectionModel(),
            &QItemSelectionModel::currentChanged,
            this,
            [this](const QModelIndex& current) { updateStepDetails(current); });
    connect(m_resultView,
            &QTreeView::doubleClicked,
            this,
            &MainWindow::focusExecutionLogForResult);
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
    ApplicationDiagnostics::recordAction(QStringLiteral("ADMIN_WINDOW_CLOSE"));
    m_runArtifactWriter->abandon();
    waitForPluginScan();
    beginShutdown();
}

bool MainWindow::openSequenceFile(const QString& filePath)
{
    m_loadingSequenceFile = true;
    m_expandSequenceTreeOnNextUpdate = true;
    const bool loaded = m_sequenceDocument->load(filePath);
    if (loaded) {
        m_sequenceDocument->ensureStandardGroups();
    }
    m_loadingSequenceFile = false;
    if (!loaded) {
        m_expandSequenceTreeOnNextUpdate = false;
        updateSequenceEditor();
        return false;
    }
    m_sequenceTreeModel->clearBreakpoints();
    m_sequenceTreeModel->setCurrentDebugNodePath({});
    m_selectedSequencePath = {};
    m_selectedSequenceNodePath.clear();
    applyStationLogicalIdMigrations();
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
        m_handlingWorkspaceTabChange = true;
        m_workspaceTabs->setCurrentIndex(0);
        m_handlingWorkspaceTabChange = false;
        m_previousWorkspaceTabIndex = 0;
        updateCommandState();
    }
}

void MainWindow::initializeAdminWorkspace()
{
    if (m_adminWorkspaceInitialized) {
        return;
    }
    m_adminWorkspaceInitialized = true;
    showStartupOverlay(tr("Loading plugins and preparing the Admin workspace..."));
    QTimer::singleShot(0, this, [this] { scanPlugins(false); });
}

void MainWindow::closeEvent(QCloseEvent* event)
{
    if (maybeSaveSequence() && maybeSaveStation()) {
        saveUiSettings();
        m_runArtifactWriter->abandon();
        beginShutdown();
        event->accept();
    } else {
        event->ignore();
    }
}

bool MainWindow::eventFilter(QObject* watched, QEvent* event)
{
    if (m_startupOverlay && watched == centralWidget() &&
        event->type() == QEvent::Resize) {
        m_startupOverlay->setGeometry(centralWidget()->rect());
        m_startupOverlay->raise();
    }
    if (watched == m_flowFieldSearch && event->type() == QEvent::KeyPress) {
        const auto* keyEvent = static_cast<QKeyEvent*>(event);
        if (keyEvent->key() == Qt::Key_Escape) {
            m_flowFieldSearch->clear();
            m_flowFieldSearch->parentWidget()->hide();
            m_sequenceTreeModel->setInspectionField({});
            m_sequenceTreeView->setFocus();
            return true;
        }
    }
    const bool sequenceTreeEvent = m_sequenceTreeView &&
        (watched == m_sequenceTreeView ||
         watched == m_sequenceTreeView->viewport());
    if (sequenceTreeEvent && event->type() == QEvent::KeyPress) {
        const auto* keyEvent = static_cast<QKeyEvent*>(event);
        if (keyEvent->matches(QKeySequence::Copy)) {
            copySequenceSteps();
            return true;
        }
        if (keyEvent->matches(QKeySequence::Paste)) {
            pasteSequenceSteps();
            return true;
        }
    }
    if (m_sequenceTreeView && watched == m_sequenceTreeView->viewport() &&
        event->type() == QEvent::MouseButtonPress) {
        const auto* mouseEvent = static_cast<QMouseEvent*>(event);
        if (mouseEvent->button() == Qt::LeftButton &&
            !m_sequenceTreeView->indexAt(
                mouseEvent->position().toPoint()).isValid()) {
            if (!resolvePendingStepChanges()) {
                return true;
            }
            m_selectedSequencePath = {};
            m_selectedSequenceNodePath.clear();
            m_sequenceTreeView->selectionModel()->clearSelection();
            m_sequenceTreeView->setCurrentIndex({});
            m_stepPropertyEditor->setCurrentItem({});
            updateCommandState();
        }
    }
    return QMainWindow::eventFilter(watched, event);
}

bool MainWindow::openStationFile(const QString& filePath)
{
    if (!m_stationDocument->load(filePath)) {
        updateStationEditor();
        return false;
    }
    normalizeStationLogicalIds();
    applyStationLogicalIdMigrations();
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

    if (m_operatorPromptPresenter) {
        m_operatorPromptPresenter->closeAll();
    }

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
    const bool hasPendingStep = m_stepPropertyEditor &&
                                m_stepPropertyEditor->hasPendingChanges();
    if (!m_sequenceDocument ||
        (!m_sequenceDocument->isModified() && !hasPendingStep)) {
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
        if (m_stepPropertyEditor) {
            m_stepPropertyEditor->discardPendingChanges();
        }
        return true;
    }
    if (hasPendingStep && !m_stepPropertyEditor->commitPendingChanges()) {
        return false;
    }
    return saveSequence();
}

bool MainWindow::confirmAndSaveSequence()
{
    if (!m_sequenceDocument || m_sequenceDocument->isEmpty()) {
        return false;
    }
    const bool hasPendingStep = m_stepPropertyEditor &&
                                m_stepPropertyEditor->hasPendingChanges();
    if (!hasPendingStep && !m_sequenceDocument->isModified()) {
        statusBar()->showMessage(tr("No sequence changes to save"), 3000);
        return true;
    }

    if (hasPendingStep && !m_stepPropertyEditor->commitPendingChanges()) {
        return false;
    }
    return saveSequence();
}

bool MainWindow::resolvePendingStepChanges()
{
    if (!m_stepPropertyEditor || !m_stepPropertyEditor->hasPendingChanges()) {
        return true;
    }

    return m_stepPropertyEditor->commitPendingChanges();
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
    synchronizeSequenceSnapshot();
    ApplicationDiagnostics::recordAction(
        QStringLiteral("SEQUENCE_SAVED"), m_sequenceDocument->filePath());
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
    if (m_stepPropertyEditor && m_stepPropertyEditor->hasPendingChanges() &&
        !m_stepPropertyEditor->commitPendingChanges()) {
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
    ApplicationDiagnostics::recordAction(
        QStringLiteral("SEQUENCE_SAVED_AS"), path);
    statusBar()->showMessage(tr("Sequence saved"), 3000);
    updateWindowTitle();
    updateCommandState();
    return true;
}

bool MainWindow::maybeSaveStation()
{
    const bool hasPending =
        (m_stationPropertyEditor && m_stationPropertyEditor->hasPendingChanges()) ||
        (m_stationSettingsEditor && m_stationSettingsEditor->hasPendingChanges());
    if (!m_stationDocument || (!m_stationDocument->isModified() && !hasPending)) {
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
        discardPendingStationChanges();
        return true;
    }
    if (!commitPendingStationChanges()) {
        return false;
    }
    return saveStation();
}

bool MainWindow::confirmAndSaveStation()
{
    if (!m_stationDocument || m_stationDocument->isEmpty()) {
        return false;
    }
    const bool hasPending =
        (m_stationPropertyEditor && m_stationPropertyEditor->hasPendingChanges()) ||
        (m_stationSettingsEditor && m_stationSettingsEditor->hasPendingChanges());
    if (!m_stationDocument->isModified() && !hasPending) {
        statusBar()->showMessage(tr("No Station changes to save"), 3000);
        return true;
    }
    const auto choice = QMessageBox::question(
        this,
        tr("Save Station"),
        tr("Save the current Station changes to %1?")
            .arg(m_stationDocument->displayName()),
        QMessageBox::Save | QMessageBox::Cancel,
        QMessageBox::Save);
    if (choice != QMessageBox::Save || !commitPendingStationChanges()) {
        return false;
    }
    return saveStation();
}

bool MainWindow::resolvePendingStationChanges()
{
    const bool hasPending =
        (m_stationPropertyEditor && m_stationPropertyEditor->hasPendingChanges()) ||
        (m_stationSettingsEditor && m_stationSettingsEditor->hasPendingChanges());
    if (!hasPending) {
        return true;
    }
    const auto choice = QMessageBox::warning(
        this,
        tr("Unsaved Station Changes"),
        tr("Station Config has unsaved property changes."),
        QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel,
        QMessageBox::Save);
    if (choice == QMessageBox::Cancel) {
        return false;
    }
    if (choice == QMessageBox::Discard) {
        discardPendingStationChanges();
        return true;
    }
    return commitPendingStationChanges() && saveStation();
}

bool MainWindow::resolvePendingStationDeviceChanges()
{
    if (!m_stationPropertyEditor ||
        !m_stationPropertyEditor->hasPendingChanges()) {
        return true;
    }
    const auto choice = QMessageBox::warning(
        this,
        tr("Unsaved Device Changes"),
        tr("The current Station device has unsaved changes."),
        QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel,
        QMessageBox::Save);
    if (choice == QMessageBox::Cancel) {
        return false;
    }
    if (choice == QMessageBox::Discard) {
        m_stationPropertyEditor->discardPendingChanges();
        return true;
    }
    return m_stationPropertyEditor->commitPendingChanges() && saveStation();
}

bool MainWindow::commitPendingStationChanges()
{
    if (m_stationSettingsEditor &&
        !m_stationSettingsEditor->commitPendingChanges()) {
        return false;
    }
    return !m_stationPropertyEditor ||
           m_stationPropertyEditor->commitPendingChanges();
}

void MainWindow::discardPendingStationChanges()
{
    if (m_stationSettingsEditor) {
        m_stationSettingsEditor->discardPendingChanges();
    }
    if (m_stationPropertyEditor) {
        m_stationPropertyEditor->discardPendingChanges();
    }
}

void MainWindow::saveActiveDocument()
{
    if (isStationWorkspaceActive()) {
        confirmAndSaveStation();
    } else {
        confirmAndSaveSequence();
    }
}

bool MainWindow::isStationWorkspaceActive() const
{
    return m_workspaceTabs && m_stationEditorPage &&
           m_workspaceTabs->currentWidget() == m_stationEditorPage;
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
    if (!commitPendingStationChanges()) {
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

void MainWindow::editSequenceVariables()
{
    if (!m_sequenceDocument || m_sequenceDocument->isEmpty()) {
        statusBar()->showMessage(tr("Open a sequence before editing variables"), 3000);
        return;
    }
    if (!resolvePendingStepChanges()) {
        return;
    }

    SequenceVariablesDialog dialog(m_sequenceDocument->sequenceVariables(), this);
    if (dialog.exec() != QDialog::Accepted) {
        return;
    }
    if (!m_sequenceDocument->setSequenceVariables(dialog.variables())) {
        statusBar()->showMessage(tr("Failed to update sequence variables"), 4000);
        return;
    }
    statusBar()->showMessage(tr("Sequence variable draft updated; press Ctrl+S to save"),
                             4000);
}

void MainWindow::addSequenceStep()
{
    if (!resolvePendingStepChanges()) {
        return;
    }
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
    if (!resolvePendingStepChanges()) {
        return;
    }
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
    if (!resolvePendingStepChanges()) {
        return;
    }
    const auto paths = selectedSequenceStepPaths();
    if (!m_sequenceDocument->setStepsEnabled(paths, enabled)) {
        statusBar()->showMessage(
            enabled ? tr("Selected steps are already enabled")
                    : tr("Selected steps are already disabled"),
            3000);
    }
}

void MainWindow::copySequenceSteps()
{
    const auto paths = selectedSequenceStepPaths();
    if (paths.isEmpty()) {
        return;
    }
    m_sequenceClipboard = m_sequenceDocument->copiedSteps(paths);
    if (m_sequenceClipboard.isEmpty()) {
        statusBar()->showMessage(tr("Unable to copy selected steps"), 4000);
    } else {
        statusBar()->showMessage(
            tr("Copied %1 item(s)").arg(m_sequenceClipboard.size()), 2500);
    }
    updateCommandState();
}

void MainWindow::pasteSequenceSteps()
{
    if (m_sequenceClipboard.isEmpty() || !resolvePendingStepChanges()) {
        return;
    }
    const auto selectedPath = m_sequenceTreeModel->pathForIndex(
        m_sequenceTreeView->currentIndex());
    if (!selectedPath.isValid()) {
        return;
    }

    auto parentPath = selectedPath;
    int row = -1;
    if (!selectedPath.isGroup()) {
        row = parentPath.stepIndices.takeLast() + 1;
    }

    QVector<SequenceItemPath> pastedPaths;
    if (!m_sequenceDocument->pasteSteps(
            parentPath, row, m_sequenceClipboard, &pastedPaths) ||
        pastedPaths.isEmpty()) {
        statusBar()->showMessage(tr("Unable to paste copied steps here"), 4000);
        return;
    }

    m_selectedSequencePath = pastedPaths.first();
    auto* selection = m_sequenceTreeView->selectionModel();
    selection->clearSelection();
    for (const auto& path : std::as_const(pastedPaths)) {
        const auto index = m_sequenceTreeModel->indexForPath(path);
        if (index.isValid()) {
            selection->select(
                index,
                QItemSelectionModel::Select | QItemSelectionModel::Rows);
        }
    }
    const auto current = m_sequenceTreeModel->indexForPath(
        m_selectedSequencePath);
    if (current.isValid()) {
        m_sequenceTreeView->setCurrentIndex(current);
        m_sequenceTreeView->scrollTo(
            current, QAbstractItemView::PositionAtCenter);
    }
    statusBar()->showMessage(
        tr("Pasted %1 item(s)").arg(pastedPaths.size()), 2500);
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
    if (!resolvePendingStepChanges()) {
        return;
    }
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

void MainWindow::placeResourceRegionBoundary()
{
    if (!resolvePendingStepChanges()) {
        statusBar()->showMessage(
            tr("Fix the current Step parameters before placing LOCK or UNLOCK"),
            7000);
        return;
    }
    const auto path = m_sequenceTreeModel->pathForIndex(
        m_sequenceTreeView->currentIndex());
    if (!path.isValid() || path.isGroup() || path.stepIndices.isEmpty()) {
        statusBar()->showMessage(
            tr("Select a Step for LOCK or UNLOCK"), 5000);
        return;
    }

    const auto selectedObject = m_sequenceDocument->objectAt(path);
    const auto start = selectedObject.value(QStringLiteral("resourceRegionStart"))
                           .toObject();
    const auto endId = selectedObject.value(QStringLiteral("resourceRegionEnd"))
                           .toString();
    const auto pendingId = m_sequenceDocument->pendingResourceRegionId();
    const bool completesSingleItem = !start.isEmpty() && endId.isEmpty() &&
        pendingId == start.value(QStringLiteral("id")).toString();
    QString error;
    if (!start.isEmpty() && !completesSingleItem) {
        if (!m_sequenceDocument->clearResourceRegionAt(path, &error)) {
            statusBar()->showMessage(error, 7000);
            return;
        }
        statusBar()->showMessage(
            tr("LOCK and its matching UNLOCK were removed"), 5000);
        updateCommandState();
        return;
    }
    if (!endId.isEmpty()) {
        if (!m_sequenceDocument->removeResourceRegionEndAt(path, &error)) {
            statusBar()->showMessage(error, 7000);
            return;
        }
        statusBar()->showMessage(
            tr("UNLOCK removed; the LOCK is waiting for a new end point"),
            6000);
        updateCommandState();
        return;
    }
    const auto currentRegionId = m_sequenceTreeView->currentIndex()
                                     .data(SequenceTreeModel::ResourceRegionIdRole)
                                     .toString();
    if (!currentRegionId.isEmpty() && !completesSingleItem) {
        statusBar()->showMessage(
            tr("This row is already inside a LOCK/UNLOCK interval"), 6000);
        return;
    }

    bool placedEntry = pendingId.isEmpty();
    if (placedEntry) {
        if (!m_sequenceDocument->placeNextResourceRegionBoundary(
                path, {}, &placedEntry, &error)) {
            statusBar()->showMessage(error, 7000);
            return;
        }
    } else {
        QStringList resources;
        if (!chooseResourceRegionResources(pendingId, &resources)) {
            QString rollbackError;
            if (!m_sequenceDocument->clearResourceRegionAt(path, &rollbackError)) {
                statusBar()->showMessage(rollbackError, 7000);
                updateCommandState();
                return;
            }
            statusBar()->showMessage(
                tr("Hardware selection cancelled; LOCK and UNLOCK were removed"),
                6000);
            updateCommandState();
            return;
        }
        if (!m_sequenceDocument->completePendingResourceRegion(
                path, resources, &error)) {
            statusBar()->showMessage(error, 7000);
            updateCommandState();
            return;
        }
    }

    m_selectedSequencePath = path;
    const auto index = m_sequenceTreeModel->indexForPath(path);
    if (index.isValid()) {
        m_sequenceTreeView->setCurrentIndex(index);
        m_sequenceTreeView->scrollTo(index, QAbstractItemView::EnsureVisible);
    }
    statusBar()->showMessage(
        placedEntry
            ? tr("LOCK placed; click this row again for one item, or select a later sibling for a range")
            : (completesSingleItem
                   ? tr("Single-item lock placed; resources release when this item completes")
                   : tr("UNLOCK placed; the locked interval is complete")),
        6000);
    updateCommandState();
}

bool MainWindow::chooseResourceRegionResources(const QString& regionId,
                                               QStringList* selectedResources)
{
    if (!selectedResources || !m_flowTargetSelector) {
        return false;
    }
    QVector<FlowTargetDevice> devices;
    for (const auto& device : m_flowTargetSelector->devices()) {
        if (!device.configured || device.logicalId.trimmed().isEmpty()) {
            continue;
        }
        const auto duplicate = std::find_if(
            devices.cbegin(), devices.cend(), [&](const FlowTargetDevice& candidate) {
                return candidate.logicalId.compare(
                           device.logicalId, Qt::CaseInsensitive) == 0;
            });
        if (duplicate == devices.cend()) {
            devices.push_back(device);
        }
    }
    if (devices.isEmpty()) {
        QMessageBox::information(
            this,
            tr("Select Locked Hardware"),
            tr("No configured Station hardware is available. Configure and enable a device in Station Config first."));
        return false;
    }

    QDialog dialog(this);
    dialog.setObjectName(QStringLiteral("resourceRegionResourceDialog"));
    dialog.setWindowTitle(tr("Select Locked Hardware"));
    dialog.resize(620, 390);
    dialog.setMinimumSize(520, 330);
    dialog.setStyleSheet(QStringLiteral(R"(
        QDialog#resourceRegionResourceDialog { background: #f7f9fb; }
        QLabel#resourceRegionTitle { color: #1f2937; font-size: 16px; font-weight: 700; }
        QLabel#resourceRegionHint { color: #667085; }
        QLabel#resourceRegionSelectionCount { color: #475467; font-weight: 600; }
        QScrollArea#resourceRegionScroll { border: 0; background: transparent; }
        QWidget#resourceRegionCardArea { background: transparent; }
        QToolButton[resourceCard="true"] {
            min-height: 62px; padding: 7px 10px; text-align: left;
            border: 1px solid #d7dce2; border-radius: 6px;
            background: #ffffff; color: #344054; font-weight: 600;
        }
        QToolButton[resourceCard="true"]:hover {
            border-color: #9fc4e8; background: #f5faff;
        }
        QToolButton[resourceCard="true"]:checked {
            border: 2px solid #75a7e8; background: #eaf3ff; color: #175cd3;
        }
        QPushButton#resourceRegionConfirmButton {
            min-width: 86px; min-height: 30px; border: 1px solid #2f75b5;
            border-radius: 5px; background: #2f75b5; color: white; font-weight: 600;
        }
        QPushButton#resourceRegionConfirmButton:disabled {
            border-color: #cfd7df; background: #e6e9ed; color: #98a2b3;
        }
    )"));
    auto* layout = new QVBoxLayout(&dialog);
    layout->setContentsMargins(18, 16, 18, 14);
    layout->setSpacing(8);
    auto* title = new QLabel(tr("Hardware held by this interval"), &dialog);
    title->setObjectName(QStringLiteral("resourceRegionTitle"));
    layout->addWidget(title);
    auto* hint = new QLabel(
        tr("Other UUTs wait until UNLOCK. Select one or more physical devices."),
        &dialog);
    hint->setObjectName(QStringLiteral("resourceRegionHint"));
    hint->setWordWrap(true);
    layout->addWidget(hint);

    auto* scroll = new QScrollArea(&dialog);
    scroll->setObjectName(QStringLiteral("resourceRegionScroll"));
    scroll->setWidgetResizable(true);
    auto* cardArea = new QWidget(scroll);
    cardArea->setObjectName(QStringLiteral("resourceRegionCardArea"));
    auto* grid = new QGridLayout(cardArea);
    grid->setContentsMargins(0, 5, 0, 5);
    grid->setHorizontalSpacing(8);
    grid->setVerticalSpacing(8);
    const auto existing = m_sequenceDocument->resourceRegionResources(regionId);
    QVector<QToolButton*> resourceButtons;
    for (int index = 0; index < devices.size(); ++index) {
        const auto& device = devices[index];
        auto* card = new QToolButton(cardArea);
        card->setObjectName(QStringLiteral("resourceRegionResourceCard"));
        card->setProperty("resourceCard", true);
        card->setProperty("resourceId", device.logicalId);
        card->setCheckable(true);
        card->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
        card->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        const auto subtitle = device.driverName.isEmpty()
            ? device.deviceType
            : device.driverName;
        card->setText(QStringLiteral("%1\n%2").arg(device.logicalId, subtitle));
        const auto type = device.deviceType.trimmed().toUpper();
        const auto icon = type == QStringLiteral("CAN") ||
                          type == QStringLiteral("SERIAL") ||
                          type == QStringLiteral("MODBUS")
            ? QStyle::SP_DriveNetIcon
            : QStyle::SP_ComputerIcon;
        card->setIcon(style()->standardIcon(icon));
        card->setIconSize(QSize(24, 24));
        card->setToolTip(
            QStringLiteral("%1 | %2").arg(device.deviceType, device.driverName));
        bool checked = existing.contains(device.logicalId, Qt::CaseInsensitive);
        for (const auto& oldResource : existing) {
            checked = checked || oldResource.startsWith(
                device.logicalId + QLatin1Char('.'), Qt::CaseInsensitive);
        }
        card->setChecked(checked);
        grid->addWidget(card, index / 2, index % 2);
        resourceButtons.push_back(card);
    }
    grid->setRowStretch((devices.size() + 1) / 2, 1);
    scroll->setWidget(cardArea);
    layout->addWidget(scroll, 1);

    auto* selectionCount = new QLabel(&dialog);
    selectionCount->setObjectName(QStringLiteral("resourceRegionSelectionCount"));
    layout->addWidget(selectionCount);

    auto* buttons = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    buttons->setObjectName(QStringLiteral("resourceRegionResourceButtons"));
    auto* ok = buttons->button(QDialogButtonBox::Ok);
    ok->setObjectName(QStringLiteral("resourceRegionConfirmButton"));
    ok->setText(tr("Use Selected"));
    const auto updateSelection = [resourceButtons, ok, selectionCount] {
        int checkedCount = 0;
        for (const auto* card : resourceButtons) {
            checkedCount += card->isChecked() ? 1 : 0;
        }
        ok->setEnabled(checkedCount > 0);
        selectionCount->setText(
            checkedCount == 1
                ? QObject::tr("1 device selected")
                : QObject::tr("%1 devices selected").arg(checkedCount));
    };
    for (auto* card : resourceButtons) {
        connect(card, &QToolButton::toggled, &dialog,
                [updateSelection](bool) { updateSelection(); });
    }
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    layout->addWidget(buttons);
    updateSelection();
    if (dialog.exec() != QDialog::Accepted) {
        return false;
    }

    selectedResources->clear();
    for (const auto* card : resourceButtons) {
        if (card->isChecked()) {
            selectedResources->push_back(
                card->property("resourceId").toString());
        }
    }
    return !selectedResources->isEmpty();
}

void MainWindow::moveSequenceStep(int offset)
{
    if (!resolvePendingStepChanges()) {
        return;
    }
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

QString nextPendingRunTestNodePath(const UutStepModel* model,
                                   const PicoATE::Core::UutId& uutId,
                                   const PicoATE::Core::NodeId& currentNodeId)
{
    if (!model || currentNodeId.isEmpty()) {
        return {};
    }

    auto current = model->indexForStep(uutId, currentNodeId);
    if (!current.isValid()) {
        current = model->indexForStep({}, currentNodeId);
    }
    const int currentLine = model->visualLineNumber(current);
    if (currentLine <= 0) {
        return {};
    }

    int nextLine = std::numeric_limits<int>::max();
    QString nextNodePath;
    const auto visit = [&](const QModelIndex& parent, const auto& self) -> void {
        const int rowCount = model->rowCount(parent);
        for (int row = 0; row < rowCount; ++row) {
            const auto index = model->index(row, UutStepModel::NameColumn, parent);
            if (const auto step = model->stepAt(index)) {
                const int line = model->visualLineNumber(index);
                if (line > currentLine && line < nextLine &&
                    !adminIsTerminalActivation(step->state)) {
                    nextLine = line;
                    nextNodePath = step->nodePath.isEmpty()
                        ? step->stepId
                        : step->nodePath;
                }
            }
            self(index, self);
        }
    };
    visit({}, visit);
    return nextNodePath;
}

void MainWindow::applyUndoRedo(bool redo)
{
    if (!resolvePendingStepChanges()) {
        return;
    }
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

void MainWindow::compileSequence()
{
    if (!m_viewModel) {
        return;
    }
    if (m_stepPropertyEditor && m_stepPropertyEditor->hasPendingChanges() &&
        !m_stepPropertyEditor->commitPendingChanges()) {
        return;
    }
    if (m_sequenceDocument && m_sequenceDocument->isModified() &&
        !saveSequence()) {
        return;
    }
    const bool hasPendingStation =
        (m_stationPropertyEditor &&
         m_stationPropertyEditor->hasPendingChanges()) ||
        (m_stationSettingsEditor &&
         m_stationSettingsEditor->hasPendingChanges());
    if (hasPendingStation && !commitPendingStationChanges()) {
        return;
    }
    if (m_stationDocument && m_stationDocument->isModified() &&
        !saveStation()) {
        return;
    }
    synchronizeSequenceSnapshot();
    synchronizeStationSnapshot();
    ApplicationDiagnostics::recordAction(
        QStringLiteral("COMPILE_REQUESTED"),
        m_sequenceDocument ? m_sequenceDocument->displayName() : QString{});
    m_viewModel->compile();
}

void MainWindow::runSequence()
{
    if (!m_viewModel || !m_sequenceTreeModel || !resolvePendingStepChanges() ||
        !resolvePendingStationChanges()) {
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
    QVariantMap variables;
    variables.insert(QStringLiteral("sn"), sn);
    variables.insert(QStringLiteral("serialNumber"), sn);
    ApplicationDiagnostics::recordAction(
        QStringLiteral("RUN_REQUESTED"), QStringLiteral("uut=%1").arg(sn));
    m_viewModel->runUut(sn, variables);
    showRunPage();
}

void MainWindow::beginAdminRunIteration(int iteration, int totalIterations)
{
    m_currentReportSaved = false;
    m_currentAdminRunCounted = false;
    m_adminTerminalNodes.clear();
    m_runtimeTimelineModel->clear();
    const auto artifact = m_runArtifactWriter->begin(
        runArtifactSettingsFromStation(
            m_stationDocument ? m_stationDocument->rootObject() : QJsonObject{},
            m_stationDocument ? m_stationDocument->filePath() : QString()),
        m_adminSerialLabel->text().trimmed());
    if (!artifact.success) {
        statusBar()->showMessage(
            tr("Cannot create report files: %1").arg(artifact.errorMessage),
            10000);
    }
    auto preview = m_adminPreviewReport;
    preview.completed = false;
    preview.hasError = false;
    preview.state = PicoATE::Core::ExecutionState::Idle;
    if (!preview.uuts.isEmpty()) {
        preview.uuts.first().uutId = m_adminSerialLabel->text().trimmed();
        preview.uuts.first().hasError = false;
    }
    displayReport(preview);
    updateAdminProgress();
    m_adminElapsed.restart();
    m_adminElapsedTimer->start();
    statusBar()->showMessage(
        tr("Loop run %1 of %2").arg(iteration).arg(totalIterations));
}

void MainWindow::toggleScanDialog()
{
    if (m_scanDialog && m_scanDialog->isVisible()) {
        m_scanDialog->cancelCurrentScan();
        statusBar()->showMessage(tr("Barcode scan cancelled"), 2500);
        return;
    }
    if (!resolvePendingStepChanges()) {
        return;
    }
    if (!m_viewModel || !m_viewModel->canRun()) {
        statusBar()->showMessage(tr("Compile the sequence before scanning"), 4000);
        return;
    }
    const auto station = m_stationDocument
        ? m_stationDocument->rootObject()
        : QJsonObject{};
    SnValidationRules rules;
    rules.exactLength = qBound(
        0, station.value(QStringLiteral("snLength")).toInt(0), 256);
    rules.wildcardPattern = station.value(QStringLiteral("snPattern"))
                                .toString().trimmed();
    rules.allowedRegex = station.value(QStringLiteral("snAllowedRegex"))
                             .toString().trimmed();
    m_scanDialog->setValidationRules(std::move(rules));
    m_scanDialog->showForNextScan();
}

void MainWindow::scanPlugins(bool interactive)
{
    if (m_pluginScanInProgress) {
        statusBar()->showMessage(tr("Plugin scan is already running"), 3000);
        return;
    }

    const auto applicationDirectory = QCoreApplication::applicationDirPath();
    const auto pluginDirectory = QDir(applicationDirectory).absoluteFilePath(
        QStringLiteral("plugins"));
    if (!QDir().mkpath(pluginDirectory)) {
        const auto message = tr(
            "Could not create the 'plugins' directory next to PicoATE.UI.exe.");
        if (interactive) {
            QMessageBox::warning(this, tr("Scan Plugins"), message);
        }
        hideStartupOverlay();
        return;
    }

    if (PluginCatalog::discoverPluginFiles(pluginDirectory).isEmpty()) {
        loadPluginRegistry();
        if (interactive) {
            statusBar()->showMessage(
                tr("No PicoATE plugin DLL was found in the plugins directory"),
                7000);
        }
        hideStartupOverlay();
        return;
    }

    const QStringList nativeHostCandidates = {
        QDir(applicationDirectory).absoluteFilePath(
            QStringLiteral("PicoATE.NativeHost.exe")),
        QDir(applicationDirectory).absoluteFilePath(
            QStringLiteral("../../../src/nativehost/Debug/PicoATE.NativeHost.exe")),
    };
    if (firstExistingPath(nativeHostCandidates).isEmpty()) {
        const auto message = tr("No compatible PicoATE.NativeHost.exe was found. Rebuild or redeploy the UI so the Host supports --describe. Plugin DLLs are never loaded directly in the UI process.");
        if (interactive) {
            QMessageBox::critical(this, tr("Scan Plugins"), message);
        } else {
            statusBar()->showMessage(message, 7000);
        }
        hideStartupOverlay();
        return;
    }

    const auto registryPath = QDir(pluginDirectory).absoluteFilePath(
        QStringLiteral("PluginRegistry.json"));
    m_pluginScanInProgress = true;
    if (m_scanPluginsAction) {
        m_scanPluginsAction->setEnabled(false);
    }
    showStartupOverlay(tr("Scanning plugin functions..."));
    statusBar()->showMessage(tr("Scanning plugins..."));
    auto result = std::make_shared<PluginScanResult>();
    auto* worker = QThread::create(
        [result, pluginDirectory, nativeHostCandidates, registryPath] {
            const auto nativeHost = firstDescribeCapableNativeHost(
                nativeHostCandidates);
            if (nativeHost.isEmpty()) {
                result->discoveredDllCount =
                    PluginCatalog::discoverPluginFiles(pluginDirectory).size();
                result->errors.push_back({
                    nativeHostCandidates.join(QStringLiteral("; ")),
                    QStringLiteral("No compatible NativeHost with --describe support was found")});
                return;
            }
            *result = PluginCatalog::scanPlugins(
                pluginDirectory, nativeHost, registryPath, 5000);
        });
    m_pluginScanThread = worker;
    connect(worker, &QThread::finished, this,
            [this, worker, result, registryPath, interactive] {
                if (m_pluginScanThread == worker) {
                    m_pluginScanThread = nullptr;
                }
                m_pluginScanInProgress = false;
                if (m_scanPluginsAction) {
                    m_scanPluginsAction->setEnabled(true);
                }
                hideStartupOverlay();

                if (result->ok()) {
                    loadPluginRegistry();
                    if (interactive) {
                        QMessageBox::information(
                            this,
                            tr("Scan Plugins"),
                            tr("Found %1 plugin DLL(s), loaded %2 plugin(s), and updated:\n%3")
                                .arg(result->discoveredDllCount)
                                .arg(result->plugins.size())
                                .arg(registryPath));
                    }
                    statusBar()->showMessage(tr("Plugin registry updated"), 5000);
                    worker->deleteLater();
                    return;
                }

                if (result->registrySaved) {
                    loadPluginRegistry();
                }
                QStringList details;
                const int maximumDetails = qMin(8, result->errors.size());
                for (int index = 0; index < maximumDetails; ++index) {
                    const auto& error = result->errors[index];
                    details.push_back(tr("%1: %2").arg(error.path, error.message));
                }
                if (interactive) {
                    QMessageBox::warning(
                        this,
                        tr("Scan Plugins"),
                        tr("Found %1 DLL(s) and loaded %2 plugin(s).\n%3")
                            .arg(result->discoveredDllCount)
                            .arg(result->plugins.size())
                            .arg(details.join(QLatin1Char('\n'))));
                }
                statusBar()->showMessage(
                    tr("Plugin scan completed with %1 error(s)")
                        .arg(result->errors.size()),
                    7000);
                worker->deleteLater();
            });
    worker->start();
}

void MainWindow::buildStartupOverlay()
{
    auto* central = centralWidget();
    if (!central || m_startupOverlay) {
        return;
    }

    m_startupOverlay = new QWidget(central);
    m_startupOverlay->setObjectName(QStringLiteral("adminStartupOverlay"));
    m_startupOverlay->setAttribute(Qt::WA_StyledBackground, true);
    auto* overlayLayout = new QVBoxLayout(m_startupOverlay);
    overlayLayout->setContentsMargins(20, 20, 20, 20);
    overlayLayout->addStretch();

    auto* row = new QHBoxLayout;
    row->addStretch();
    auto* card = new QFrame(m_startupOverlay);
    card->setObjectName(QStringLiteral("adminStartupCard"));
    card->setFixedSize(390, 154);
    auto* cardLayout = new QVBoxLayout(card);
    cardLayout->setContentsMargins(28, 24, 28, 24);
    cardLayout->setSpacing(12);
    m_startupSpinner = new LoadingSpinner(card);
    m_startupSpinner->setObjectName(QStringLiteral("adminStartupSpinner"));
    m_startupSpinner->setFixedSize(34, 34);
    cardLayout->addWidget(m_startupSpinner, 0, Qt::AlignHCenter);
    m_startupStatusLabel = new QLabel(card);
    m_startupStatusLabel->setObjectName(QStringLiteral("adminStartupStatus"));
    m_startupStatusLabel->setAlignment(Qt::AlignCenter);
    m_startupStatusLabel->setWordWrap(true);
    cardLayout->addWidget(m_startupStatusLabel);
    row->addWidget(card);
    row->addStretch();
    overlayLayout->addLayout(row);
    overlayLayout->addStretch();

    m_startupOverlay->setStyleSheet(QStringLiteral(R"css(
        QWidget#adminStartupOverlay { background: rgba(244, 247, 250, 235); }
        QFrame#adminStartupCard {
            background: #ffffff;
            border: 1px solid #d6dfe8;
            border-radius: 8px;
        }
        QLabel#adminStartupStatus {
            color: #405160;
            font-size: 10pt;
            font-weight: 600;
        }
    )css"));
    m_startupOverlay->setGeometry(central->rect());
    central->installEventFilter(this);
    m_startupSpinner->setRunning(false);
    m_startupOverlay->hide();
}

void MainWindow::showStartupOverlay(const QString& message)
{
    if (!m_startupOverlay) {
        buildStartupOverlay();
    }
    if (!m_startupOverlay) {
        return;
    }
    m_startupStatusLabel->setText(message);
    m_startupSpinner->setRunning(true);
    m_startupOverlay->setGeometry(centralWidget()->rect());
    m_startupOverlay->show();
    m_startupOverlay->raise();
}

void MainWindow::hideStartupOverlay()
{
    if (!m_startupOverlay) {
        return;
    }
    m_startupSpinner->setRunning(false);
    m_startupOverlay->hide();
}

void MainWindow::waitForPluginScan()
{
    auto* worker = m_pluginScanThread;
    if (!worker) {
        return;
    }
    m_pluginScanThread = nullptr;
    disconnect(worker, nullptr, this, nullptr);
    if (worker->isRunning()) {
        worker->requestInterruption();
        worker->wait();
    }
    delete worker;
    m_pluginScanInProgress = false;
}

void MainWindow::loadPluginRegistry()
{
    if (!m_pluginFunctionModel || !m_stepPropertyEditor) {
        return;
    }
    const auto registryPath = QDir(QCoreApplication::applicationDirPath())
        .absoluteFilePath(QStringLiteral("plugins/PluginRegistry.json"));
    if (!QFileInfo::exists(registryPath)) {
        m_pluginFunctionModel->setPlugins({});
        m_stepPropertyEditor->setPluginRegistry({});
        if (m_stationPropertyEditor) {
            m_stationPropertyEditor->setPluginRegistry({});
        }
        if (m_stationDeviceModel) {
            m_stationDeviceModel->setPluginRegistry({});
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
    if (m_stationDeviceModel) {
        m_stationDeviceModel->setPluginRegistry(registry.plugins);
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
    QHash<QString, QJsonObject> deviceConfigurations;
    QVector<FlowTargetDevice> flowDevices;
    QHash<QString, int> flowDeviceById;
    const auto plugins = m_pluginFunctionModel->plugins();
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
        if (deviceId.isEmpty()) {
            continue;
        }

        const auto deviceType = device.value(QStringLiteral("deviceType"))
                                    .toString(device.value(QStringLiteral("type"))
                                                  .toString(QStringLiteral("PLUGIN")))
                                    .trimmed().toUpper();
        QString baseId = deviceId;
        QString channelName;
        if (deviceType == QStringLiteral("CAN")) {
            static const QRegularExpression channelPattern(
                QStringLiteral(R"(^(.+)\.CH(\d+)$)"),
                QRegularExpression::CaseInsensitiveOption);
            const auto match = channelPattern.match(deviceId);
            if (match.hasMatch()) {
                baseId = match.captured(1);
                channelName = QStringLiteral("CH%1").arg(match.captured(2));
            }
        }

        int flowIndex = flowDeviceById.value(baseId, -1);
        if (flowIndex < 0) {
            FlowTargetDevice flowDevice;
            flowDevice.logicalId = baseId;
            flowDevice.deviceType = deviceType;
            flowDevice.moduleId = moduleId;
            const auto plugin = std::find_if(
                plugins.cbegin(), plugins.cend(),
                [&](const PluginManifest& manifest) {
                    return manifest.moduleId == moduleId;
                });
            if (plugin != plugins.cend()) {
                flowDevice.driverName = plugin->name;
                flowDevice.configured = true;
            }
            flowIndex = flowDevices.size();
            flowDeviceById.insert(baseId, flowIndex);
            flowDevices.push_back(std::move(flowDevice));
        }
        auto& flowDevice = flowDevices[flowIndex];
        if (!flowDevice.targetIds.contains(deviceId)) {
            const int channelIndex = device.value(QStringLiteral("options"))
                                         .toObject()
                                         .value(QStringLiteral("channelIndex"))
                                         .toInt(flowDevice.targetIds.size());
            int insertAt = flowDevice.targetIds.size();
            for (int index = 0; index < flowDevice.channelNames.size(); ++index) {
                const auto existing = flowDevice.channelNames[index]
                                          .mid(2).toInt();
                if (!channelName.isEmpty() && channelIndex + 1 < existing) {
                    insertAt = index;
                    break;
                }
            }
            flowDevice.targetIds.insert(insertAt, deviceId);
            flowDevice.channelNames.insert(
                insertAt, channelName.isEmpty() ? deviceId : channelName);
        }

        if (!moduleId.isEmpty()) {
            devicesByModuleId[moduleId].push_back(deviceId);
            pluginByDeviceId.insert(deviceId, moduleId);
        }
        auto effectiveInputs = device.value(QStringLiteral("options")).toObject();
        effectiveInputs.insert(QStringLiteral("deviceId"), deviceId);
        effectiveInputs.insert(QStringLiteral("deviceType"),
                               device.value(QStringLiteral("deviceType")));
        effectiveInputs.insert(QStringLiteral("address"),
                               device.value(QStringLiteral("address")));
        deviceConfigurations.insert(deviceId, effectiveInputs);
    }
    m_pluginFunctionModel->setDeviceBindings(std::move(devicesByModuleId));
    if (m_flowTargetSelector) {
        m_flowTargetSelector->setDevices(std::move(flowDevices));
        m_pluginFunctionModel->setSelectedDeviceId(
            m_flowTargetSelector->currentTargetId());
    }
    m_stepPropertyEditor->setDevicePluginBindings(std::move(pluginByDeviceId));
    m_stepPropertyEditor->setDeviceConfigurations(std::move(deviceConfigurations));
}

void MainWindow::addStationDevice()
{
    if (!resolvePendingStationChanges()) {
        return;
    }
    const int row = m_stationDocument->deviceCount();
    m_selectedStationDeviceRow = row;
    m_stationDocument->insertDevice(row);
}

void MainWindow::deleteStationDevice()
{
    if (!m_stationDeviceView || !m_stationDeviceView->currentIndex().isValid() ||
        !resolvePendingStationChanges()) {
        return;
    }
    const auto selected = m_stationDeviceView->currentIndex()
                              .siblingAtColumn(0);
    const auto rows = m_stationDeviceModel->documentRows(selected);
    if (rows.isEmpty()) {
        return;
    }
    const auto device = m_stationDocument->deviceAt(rows.front());
    const auto deviceType = stationDeviceType(device);
    const auto logicalId = m_stationDeviceModel->logicalId(selected);
    for (int rootRow = selected.row() + 1;
         rootRow < m_stationDeviceModel->rowCount(); ++rootRow) {
        const auto later = m_stationDeviceModel->index(rootRow, 0);
        const auto laterDevice = m_stationDeviceModel->deviceAt(later);
        if (stationDeviceType(laterDevice) == deviceType) {
            QMessageBox::information(
                this,
                tr("Cannot Delete Device"),
                tr("%1 is not the last %2 device. Device IDs must remain continuous.")
                    .arg(logicalId, deviceType));
            return;
        }
    }

    QHash<QString, QStringList> references;
    if (m_sequenceDocument && !m_sequenceDocument->isEmpty()) {
        const auto groups = m_sequenceDocument->rootObject()
                                .value(QStringLiteral("groups")).toArray();
        for (const auto& groupValue : groups) {
            collectDeviceStepReferences(
                groupValue.toObject().value(QStringLiteral("steps")).toArray(),
                references,
                false);
        }
    }
    QStringList stepNames;
    QStringList referencedIds;
    for (const int row : rows) {
        const auto id = stationDeviceId(m_stationDocument->deviceAt(row));
        const auto names = references.value(id);
        if (!names.isEmpty()) {
            referencedIds.push_back(id);
            stepNames.append(names);
        }
    }
    stepNames.removeDuplicates();
    if (!stepNames.isEmpty()) {
        QMessageBox::warning(
            this,
            tr("Device Is Still Used"),
            tr("%1 cannot be deleted because the Flow still references it:\n\n%2\n\n"
               "Change or remove these device references first.")
                .arg(referencedIds.join(QStringLiteral(", ")),
                     stepNames.join(QStringLiteral("\n"))));
        return;
    }

    auto root = m_stationDocument->rootObject();
    auto devices = root.value(QStringLiteral("devices")).toArray();
    auto sortedRows = rows;
    std::sort(sortedRows.begin(), sortedRows.end(), std::greater<int>());
    for (const int row : sortedRows) {
        devices.removeAt(row);
    }
    root.insert(QStringLiteral("devices"), devices);
    m_selectedStationDeviceRow = devices.isEmpty()
        ? -1
        : qMin(rows.front(), devices.size() - 1);
    m_stationDocument->replaceRootObject(std::move(root));
}

void MainWindow::duplicateStationDevice()
{
    if (!m_stationDeviceView || !m_stationDeviceView->currentIndex().isValid() ||
        !resolvePendingStationChanges()) {
        return;
    }
    const auto selected = m_stationDeviceView->currentIndex()
                              .siblingAtColumn(0);
    const auto rows = m_stationDeviceModel->documentRows(selected);
    if (rows.isEmpty()) {
        return;
    }
    auto root = m_stationDocument->rootObject();
    auto devices = root.value(QStringLiteral("devices")).toArray();
    const int insertionRow = *std::max_element(rows.cbegin(), rows.cend()) + 1;
    const auto type = stationDeviceType(m_stationDocument->deviceAt(rows.front()));
    int typeCount = 0;
    for (int modelRow = 0; modelRow < m_stationDeviceModel->rowCount();
         ++modelRow) {
        const auto candidate = m_stationDeviceModel->index(modelRow, 0);
        if (stationDeviceType(m_stationDeviceModel->deviceAt(candidate)) == type) {
            ++typeCount;
        }
    }
    const auto baseId = QStringLiteral("%1%2").arg(type).arg(typeCount + 1);
    int offset = 0;
    for (const int sourceRow : rows) {
        auto copy = m_stationDocument->deviceAt(sourceRow);
        const int channel = copy.value(QStringLiteral("options"))
                                .toObject()
                                .value(QStringLiteral("channelIndex"))
                                .toInt();
        copy.insert(QStringLiteral("deviceId"),
                    type == QStringLiteral("CAN")
                        ? QStringLiteral("%1.CH%2").arg(baseId).arg(channel + 1)
                        : baseId);
        copy.remove(QStringLiteral("id"));
        devices.insert(insertionRow + offset, copy);
        ++offset;
    }
    root.insert(QStringLiteral("devices"), devices);
    m_selectedStationDeviceRow = insertionRow;
    m_stationDocument->replaceRootObject(std::move(root));
}

void MainWindow::fillPreviousStationDeviceSlot()
{
    const auto selected = m_stationDeviceView
        ? m_stationDeviceView->currentIndex().siblingAtColumn(0)
        : QModelIndex{};
    if (!selected.isValid() ||
        m_selectedStationDeviceRow < 0 ||
        !resolvePendingStationChanges()) {
        return;
    }
    const int sourceRow = m_selectedStationDeviceRow;
    const int targetRow = m_stationDocument->previousEmptyDeviceRow(sourceRow);
    if (targetRow < 0) {
        QMessageBox::information(
            this,
            tr("No Earlier Empty Slot"),
            tr("The selected device has no earlier empty logical ID of the same type."));
        return;
    }

    const auto sourceId = stationDeviceId(m_stationDocument->deviceAt(sourceRow));
    const auto targetId = stationDeviceId(m_stationDocument->deviceAt(targetRow));
    QHash<QString, QStringList> references;
    if (m_sequenceDocument && !m_sequenceDocument->isEmpty()) {
        const auto groups = m_sequenceDocument->rootObject()
                                .value(QStringLiteral("groups")).toArray();
        for (const auto& groupValue : groups) {
            collectDeviceStepReferences(
                groupValue.toObject().value(QStringLiteral("steps")).toArray(),
                references,
                false);
        }
    }

    const bool hasReferences = !references.value(sourceId).isEmpty();
    if (hasReferences) {
        const auto choice = QMessageBox::question(
            this,
            tr("Update Flow References"),
            tr("Move the configuration from %1 to %2 and update every Flow "
               "reference from %1 to %2?")
                .arg(sourceId, targetId),
            QMessageBox::Yes | QMessageBox::Cancel,
            QMessageBox::Yes);
        if (choice != QMessageBox::Yes) {
            return;
        }
    }

    if (!m_stationDocument->moveDeviceConfiguration(sourceRow, targetRow)) {
        QMessageBox::warning(
            this,
            tr("Move Failed"),
            tr("Unable to move the selected configuration into %1.").arg(targetId));
        return;
    }

    if (hasReferences && m_sequenceDocument && !m_sequenceDocument->isEmpty()) {
        auto root = m_sequenceDocument->rootObject();
        auto groups = root.value(QStringLiteral("groups")).toArray();
        bool changed = false;
        for (int groupIndex = 0; groupIndex < groups.size(); ++groupIndex) {
            auto group = groups[groupIndex].toObject();
            group.insert(
                QStringLiteral("steps"),
                replaceDeviceStepReferences(
                    group.value(QStringLiteral("steps")).toArray(),
                    sourceId,
                    targetId,
                    changed));
            groups[groupIndex] = group;
        }
        if (changed) {
            root.insert(QStringLiteral("groups"), groups);
            m_sequenceDocument->replaceRootObject(std::move(root));
        }
    }

    statusBar()->showMessage(
        tr("Moved device configuration from %1 to %2; %1 is now an empty slot")
            .arg(sourceId, targetId),
        8000);
    updateStationEditor();
    updateSequenceEditor();
}

void MainWindow::moveStationDevice(int offset)
{
    const auto selected = m_stationDeviceView
        ? m_stationDeviceView->currentIndex().siblingAtColumn(0)
        : QModelIndex{};
    if (!selected.isValid() ||
        m_selectedStationDeviceRow < 0 || offset == 0 ||
        !resolvePendingStationChanges()) {
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
    if (!resolvePendingStationChanges()) {
        return;
    }
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
    if (!m_stationDocument || !m_viewModel || !m_connectionTimeoutMs ||
        !resolvePendingStationChanges()) {
        return;
    }
    const auto selected = m_stationDeviceView
        ? m_stationDeviceView->currentIndex().siblingAtColumn(0)
        : QModelIndex{};
    const auto rows = m_stationDeviceModel->documentRows(selected);
    int row = -1;
    for (const int candidate : rows) {
        if (m_stationDocument->deviceAt(candidate)
                .value(QStringLiteral("enabled")).toBool(true)) {
            row = candidate;
            break;
        }
    }
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
    ScopedOperationTimer timer(
        QStringLiteral("MainWindow.synchronizeSequenceSnapshot"), 15);
    if (!m_sequenceDocument || m_sequenceDocument->isEmpty() ||
        !m_viewModel->canChangeSources()) {
        return;
    }
    const auto snapshot = m_sequenceDocument->snapshot();
    m_viewModel->setSequenceDocument(snapshot.filePath, snapshot.json);
}

void MainWindow::synchronizeStationSnapshot()
{
    ScopedOperationTimer timer(
        QStringLiteral("MainWindow.synchronizeStationSnapshot"), 15);
    if (!m_stationDocument || m_stationDocument->isEmpty() ||
        !m_viewModel->canChangeSources()) {
        return;
    }
    const auto snapshot = m_stationDocument->snapshot();
    m_viewModel->setStationDocument(snapshot.filePath, snapshot.json);
}

void MainWindow::captureSequenceTreeViewState()
{
    m_sequenceTreeStatePending = false;
    m_expandedSequencePaths.clear();
    if (!m_sequenceTreeView || !m_sequenceTreeModel ||
        m_loadingSequenceFile) {
        return;
    }

    m_sequenceTreeScrollValue =
        m_sequenceTreeView->verticalScrollBar()->value();
    m_selectedSequenceNodePath = m_sequenceTreeModel->nodePathForIndex(
        m_sequenceTreeView->currentIndex());
    const std::function<void(const QModelIndex&)> collectExpanded =
        [this, &collectExpanded](const QModelIndex& parent) {
            const int rows = m_sequenceTreeModel->rowCount(parent);
            for (int row = 0; row < rows; ++row) {
                const auto index = m_sequenceTreeModel->index(
                    row, SequenceTreeModel::NameColumn, parent);
                if (m_sequenceTreeView->isExpanded(index)) {
                    m_expandedSequencePaths.push_back(
                        m_sequenceTreeModel->pathForIndex(index));
                }
                collectExpanded(index);
            }
        };
    collectExpanded({});
    m_sequenceTreeStatePending = true;
}

void MainWindow::restoreSequenceTreeViewState()
{
    if (!m_sequenceTreeStatePending || !m_sequenceTreeView ||
        !m_sequenceTreeModel) {
        return;
    }

    m_sequenceTreeView->collapseAll();
    for (const auto& path : std::as_const(m_expandedSequencePaths)) {
        const auto index = m_sequenceTreeModel->indexForPath(path);
        if (index.isValid()) {
            m_sequenceTreeView->expand(index);
        }
    }
    auto* scrollBar = m_sequenceTreeView->verticalScrollBar();
    scrollBar->setValue(qBound(scrollBar->minimum(),
                               m_sequenceTreeScrollValue,
                               scrollBar->maximum()));
    m_sequenceTreeStatePending = false;
}

void MainWindow::updateSequenceEditor()
{
    if (!m_sequenceDocument || !m_editorDiagnosticModel) {
        return;
    }

    if (m_sequenceTreeView) {
        if (m_expandSequenceTreeOnNextUpdate) {
            m_sequenceTreeView->expandAll();
            m_expandSequenceTreeOnNextUpdate = false;
            m_sequenceTreeStatePending = false;
        }
        auto selected = m_selectedSequenceNodePath.isEmpty()
            ? QModelIndex{}
            : m_sequenceTreeModel->indexForNodePath(m_selectedSequenceNodePath);
        if (!selected.isValid()) {
            selected = m_sequenceTreeModel->indexForPath(m_selectedSequencePath);
        }
        if (!selected.isValid() && m_sequenceTreeModel->rowCount() > 0) {
            selected = m_sequenceTreeModel->index(0, 0);
            m_selectedSequencePath = m_sequenceTreeModel->pathForIndex(selected);
        }
        if (selected.isValid()) {
            m_selectedSequencePath = m_sequenceTreeModel->pathForIndex(selected);
            m_selectedSequenceNodePath =
                m_sequenceTreeModel->nodePathForIndex(selected);
            m_sequenceTreeView->setCurrentIndex(selected);
            m_stepPropertyEditor->setCurrentItem(
                m_sequenceTreeModel->pathForIndex(selected));
        } else {
            m_stepPropertyEditor->setCurrentItem({});
        }
        restoreSequenceTreeViewState();
    }
    updateWindowTitle();
    updateCommandState();
}

void MainWindow::updateStationEditor()
{
    if (!m_stationDocument || !m_stationDiagnosticModel) {
        return;
    }
    refreshEditorDiagnostics();
    const int count = m_stationDocument->deviceCount();
    if (count == 0) {
        m_selectedStationDeviceRow = -1;
    } else {
        m_selectedStationDeviceRow = qBound(0, m_selectedStationDeviceRow, count - 1);
    }
    if (m_stationDeviceView && m_selectedStationDeviceRow >= 0) {
        const auto index =
            m_stationDeviceModel->indexForDocumentRow(m_selectedStationDeviceRow);
        if (index.isValid()) {
            m_stationDeviceView->setCurrentIndex(index);
            m_stationDeviceView->scrollTo(index);
        }
    }
    if (m_stationPropertyEditor) {
        const auto index = m_stationDeviceModel->indexForDocumentRow(
            m_selectedStationDeviceRow);
        m_stationPropertyEditor->setCurrentDevices(
            m_stationDeviceModel->documentRows(index),
            m_stationDeviceModel->logicalBaseId(index));
    }
    updateWindowTitle();
    updateCommandState();
}

void MainWindow::normalizeStationLogicalIds()
{
    if (!m_stationDocument || m_stationDocument->isEmpty() ||
        !m_stationDeviceModel) {
        return;
    }
    auto root = m_stationDocument->rootObject();
    auto devices = root.value(QStringLiteral("devices")).toArray();
    QHash<QString, QString> migrations;
    bool changed = false;
    for (int row = 0; row < m_stationDeviceModel->rowCount(); ++row) {
        const auto index = m_stationDeviceModel->index(row, 0);
        for (const int documentRow :
             m_stationDeviceModel->documentRows(index)) {
            if (documentRow < 0 || documentRow >= devices.size()) {
                continue;
            }
            auto device = devices[documentRow].toObject();
            const auto oldId = stationDeviceId(device);
            const auto newId = m_stationDeviceModel->generatedLogicalId(
                index, documentRow);
            if (newId.isEmpty() || oldId == newId) {
                continue;
            }
            device.insert(QStringLiteral("deviceId"), newId);
            device.remove(QStringLiteral("id"));
            devices[documentRow] = device;
            if (!oldId.isEmpty()) {
                migrations.insert(oldId, newId);
            }
            changed = true;
        }
    }
    if (!changed) {
        return;
    }
    root.insert(QStringLiteral("devices"), devices);
    for (auto iterator = migrations.cbegin(); iterator != migrations.cend();
         ++iterator) {
        m_pendingStationLogicalIdMigrations.insert(
            iterator.key(), iterator.value());
    }
    m_stationDocument->replaceRootObject(std::move(root));
    statusBar()->showMessage(
        tr("Logical device IDs were generated automatically. Save Station Config to keep them."),
        8000);
}

void MainWindow::applyStationLogicalIdMigrations()
{
    if (m_pendingStationLogicalIdMigrations.isEmpty() ||
        !m_sequenceDocument || m_sequenceDocument->isEmpty()) {
        return;
    }
    auto root = m_sequenceDocument->rootObject();
    auto groups = root.value(QStringLiteral("groups")).toArray();
    bool changed = false;
    for (int groupIndex = 0; groupIndex < groups.size(); ++groupIndex) {
        auto group = groups[groupIndex].toObject();
        auto steps = group.value(QStringLiteral("steps")).toArray();
        for (auto iterator = m_pendingStationLogicalIdMigrations.cbegin();
             iterator != m_pendingStationLogicalIdMigrations.cend(); ++iterator) {
            steps = replaceDeviceStepReferences(
                steps, iterator.key(), iterator.value(), changed);
        }
        group.insert(QStringLiteral("steps"), steps);
        groups[groupIndex] = group;
    }
    if (changed) {
        root.insert(QStringLiteral("groups"), groups);
        m_sequenceDocument->replaceRootObject(std::move(root));
        statusBar()->showMessage(
            tr("Flow device references were updated to the generated Logical IDs."),
            8000);
    }
    m_pendingStationLogicalIdMigrations.clear();
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

QVector<UiDiagnostic> MainWindow::stationFlowDiagnostics() const
{
    QVector<UiDiagnostic> result;
    if (!m_stationDocument || m_stationDocument->isEmpty() ||
        !m_sequenceDocument || m_sequenceDocument->isEmpty()) {
        return result;
    }

    QHash<QString, QStringList> references;
    for (const auto& groupValue :
         m_sequenceDocument->rootObject().value(QStringLiteral("groups")).toArray()) {
        const auto group = groupValue.toObject();
        if (!group.value(QStringLiteral("enabled")).toBool(true)) {
            continue;
        }
        collectDeviceStepReferences(group.value(QStringLiteral("steps")).toArray(),
                                    references);
    }

    QHash<QString, int> rowByDeviceId;
    QSet<QString> enabledDeviceIds;
    const auto devices = m_stationDocument->rootObject()
                             .value(QStringLiteral("devices")).toArray();
    for (int row = 0; row < devices.size(); ++row) {
        const auto device = devices[row].toObject();
        const auto deviceId = device.value(QStringLiteral("deviceId")).toString(
            device.value(QStringLiteral("id")).toString()).trimmed();
        if (deviceId.isEmpty()) {
            continue;
        }
        rowByDeviceId.insert(deviceId, row);
        if (device.value(QStringLiteral("enabled")).toBool(true)) {
            enabledDeviceIds.insert(deviceId);
        }
    }

    for (auto iterator = references.cbegin(); iterator != references.cend(); ++iterator) {
        if (enabledDeviceIds.contains(iterator.key())) {
            continue;
        }
        auto stepNames = iterator.value();
        stepNames.removeDuplicates();
        const bool configured = rowByDeviceId.contains(iterator.key());
        const auto path = configured
            ? QStringLiteral("devices[%1].enabled").arg(rowByDeviceId.value(iterator.key()))
            : QStringLiteral("devices");
        result.push_back({
            UiDiagnosticSeverity::Error,
            path,
            configured
                ? tr("Device '%1' is disabled but is used by Flow: %2")
                      .arg(iterator.key(), stepNames.join(QStringLiteral(", ")))
                : tr("Device '%1' is used by Flow but is not configured: %2")
                      .arg(iterator.key(), stepNames.join(QStringLiteral(", "))),
            configured
                ? tr("Enable the Station device, or disable/remove the listed Flow steps")
                : tr("Add and enable this logical device in Station Config")});
    }
    return result;
}

QVector<UiDiagnostic> MainWindow::stationEditorDiagnostics() const
{
    auto diagnostics = m_stationDocument
        ? m_stationDocument->diagnostics()
        : QVector<UiDiagnostic>{};
    diagnostics += stationPluginDiagnostics();
    diagnostics += stationFlowDiagnostics();
    return diagnostics;
}

void MainWindow::refreshEditorDiagnostics()
{
    if (!m_editorDiagnosticModel || !m_stationDiagnosticModel) {
        return;
    }
    const auto stationDiagnostics = stationEditorDiagnostics();
    m_stationDiagnosticModel->setDiagnostics(stationDiagnostics);

    auto flowDiagnostics = m_sequenceDocument
        ? m_sequenceDocument->diagnostics()
        : QVector<UiDiagnostic>{};
    for (auto diagnostic : stationDiagnostics) {
        diagnostic.path = StationDiagnosticPrefix +
                          (diagnostic.path.isEmpty()
                               ? QStringLiteral("root")
                               : diagnostic.path);
        flowDiagnostics.push_back(std::move(diagnostic));
    }
    m_editorDiagnosticModel->setDiagnostics(std::move(flowDiagnostics));
}

void MainWindow::focusSequenceDiagnostic(const QModelIndex& index)
{
    const auto diagnostic = m_editorDiagnosticModel->diagnosticAt(index.row());
    if (!diagnostic) {
        return;
    }
    if (diagnostic->path.startsWith(StationDiagnosticPrefix)) {
        auto stationDiagnostic = *diagnostic;
        stationDiagnostic.path.remove(0, StationDiagnosticPrefix.size());
        if (stationDiagnostic.path == QStringLiteral("root")) {
            stationDiagnostic.path.clear();
        }
        focusStationDiagnosticValue(stationDiagnostic);
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

    m_workspaceTabs->setCurrentWidget(m_flowEditorPage);
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
    focusStationDiagnosticValue(*diagnostic);
}

void MainWindow::focusStationDiagnosticValue(const UiDiagnostic& diagnostic)
{
    static const QRegularExpression devicePath(
        QStringLiteral("^devices\\[(\\d+)\\](?:\\.(.*))?$"));
    const auto match = devicePath.match(diagnostic.path);
    if (match.hasMatch()) {
        const int row = match.captured(1).toInt();
        if (row >= 0 && row < m_stationDocument->deviceCount()) {
            m_selectedStationDeviceRow = row;
            const auto deviceIndex =
                m_stationDeviceModel->indexForDocumentRow(row);
            m_stationDeviceView->setCurrentIndex(deviceIndex);
            m_stationDeviceView->scrollTo(deviceIndex,
                                          QAbstractItemView::PositionAtCenter);
            m_stationPropertyEditor->setCurrentDevices(
                m_stationDeviceModel->documentRows(deviceIndex),
                m_stationDeviceModel->logicalBaseId(deviceIndex));
        }
    }
    m_handlingWorkspaceTabChange = true;
    m_workspaceTabs->setCurrentWidget(m_stationEditorPage);
    m_handlingWorkspaceTabChange = false;
    m_previousWorkspaceTabIndex =
        m_workspaceTabs->indexOf(m_stationEditorPage);
    const bool focused = diagnostic.path.startsWith(QStringLiteral("devices["))
        ? m_stationPropertyEditor->focusField(diagnostic.path)
        : m_stationSettingsEditor->focusField(diagnostic.path);
    statusBar()->showMessage(
        focused ? tr("Located station field: %1").arg(diagnostic.path)
                : tr("Located station diagnostic: %1").arg(diagnostic.path),
        4000);
}

void MainWindow::updateWindowTitle()
{
    QString title = tr("PicoATE");
    if (m_sequenceDocument && !m_sequenceDocument->isEmpty()) {
        title += QStringLiteral(" - ") + m_sequenceDocument->displayName();
        if (m_sequenceDocument->isModified() ||
            (m_stepPropertyEditor && m_stepPropertyEditor->hasPendingChanges())) {
            title += QLatin1Char('*');
        }
    }
    if (m_stationDocument && !m_stationDocument->isEmpty()) {
        title += QStringLiteral(" | ") + m_stationDocument->displayName();
        if (m_stationDocument->isModified() ||
            (m_stationPropertyEditor &&
             m_stationPropertyEditor->hasPendingChanges()) ||
            (m_stationSettingsEditor &&
             m_stationSettingsEditor->hasPendingChanges())) {
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
    m_saveSequenceAction->setObjectName(QStringLiteral("saveSequenceAction"));
    m_saveSequenceAction->setShortcut(QKeySequence::Save);
    m_saveSequenceAction->setToolTip(tr("Save sequence JSON"));
    connect(m_saveSequenceAction, &QAction::triggered,
            this, &MainWindow::saveActiveDocument);

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

    m_copyStepAction = new QAction(
        QIcon::fromTheme(QStringLiteral("edit-copy"),
                         style()->standardIcon(QStyle::SP_FileIcon)),
        tr("Copy Selected"), this);
    m_copyStepAction->setObjectName(QStringLiteral("copyStepAction"));
    m_copyStepAction->setToolTip(
        tr("Copy selected Steps and TestItems (Ctrl+C)"));
    connect(m_copyStepAction, &QAction::triggered,
            this, &MainWindow::copySequenceSteps);

    m_pasteStepAction = new QAction(
        QIcon::fromTheme(QStringLiteral("edit-paste"),
                         style()->standardIcon(QStyle::SP_FileLinkIcon)),
        tr("Paste"), this);
    m_pasteStepAction->setObjectName(QStringLiteral("pasteStepAction"));
    m_pasteStepAction->setToolTip(
        tr("Paste copied items after the current item (Ctrl+V)"));
    connect(m_pasteStepAction, &QAction::triggered,
            this, &MainWindow::pasteSequenceSteps);

    m_findFlowFieldAction = new QAction(tr("Inspect Field"), this);
    m_findFlowFieldAction->setObjectName(QStringLiteral("findFlowFieldAction"));
    m_findFlowFieldAction->setShortcut(QKeySequence::Find);
    m_findFlowFieldAction->setShortcutContext(Qt::WidgetWithChildrenShortcut);
    m_findFlowFieldAction->setToolTip(
        tr("Show one JSON field beside every flow item (Ctrl+F)"));
    connect(m_findFlowFieldAction, &QAction::triggered, this, [this] {
        if (!m_flowFieldSearch) return;
        m_flowFieldSearch->parentWidget()->show();
        m_flowFieldSearch->show();
        m_flowFieldSearch->setFocus();
        m_flowFieldSearch->selectAll();
    });

    m_sequenceVariablesAction = new QAction(
        style()->standardIcon(QStyle::SP_FileDialogDetailedView),
        tr("Variables"),
        this);
    m_sequenceVariablesAction->setObjectName(
        QStringLiteral("sequenceVariablesAction"));
    m_sequenceVariablesAction->setToolTip(
        tr("Edit shared and per-UUT sequence variables"));
    connect(m_sequenceVariablesAction, &QAction::triggered,
            this, &MainWindow::editSequenceVariables);

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
            this, [this] { confirmAndSaveStation(); });
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
    m_deleteDeviceAction->setObjectName(QStringLiteral("deleteDeviceAction"));
    connect(m_deleteDeviceAction, &QAction::triggered,
            this, [this] { deleteStationDevice(); });
    m_fillPreviousDeviceSlotAction = new QAction(
        style()->standardIcon(QStyle::SP_ArrowBack),
        tr("Fill Previous Empty ID"),
        this);
    m_fillPreviousDeviceSlotAction->setObjectName(
        QStringLiteral("fillPreviousDeviceSlotAction"));
    m_fillPreviousDeviceSlotAction->setToolTip(
        tr("Move this configuration into the earliest empty logical ID of the same type"));
    connect(m_fillPreviousDeviceSlotAction, &QAction::triggered,
            this, [this] { fillPreviousStationDeviceSlot(); });
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
    connect(m_compileAction, &QAction::triggered,
            this, &MainWindow::compileSequence);

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
    m_stepIntoAction->setObjectName(QStringLiteral("stepIntoAction"));
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
    m_scanAction->setToolTip(tr("Open or cancel the barcode dialog"));
    connect(m_scanAction, &QAction::triggered, this, &MainWindow::toggleScanDialog);

    m_scanPluginsAction = new QAction(
        style()->standardIcon(QStyle::SP_BrowserReload),
        tr("Scan Plugins"),
        this);
    m_scanPluginsAction->setObjectName(QStringLiteral("scanPluginsAction"));
    m_scanPluginsAction->setToolTip(
        tr("Scan the plugin directory and rebuild PluginRegistry.json"));
    connect(m_scanPluginsAction, &QAction::triggered,
            this, [this] { scanPlugins(true); });

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
    editMenu->addAction(m_sequenceVariablesAction);
    editMenu->addAction(m_copyStepAction);
    editMenu->addAction(m_pasteStepAction);
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

    m_flowEditorPage = new QWidget(m_workspaceTabs);
    auto* sequenceEditorPage = m_flowEditorPage;
    sequenceEditorPage->addAction(m_findFlowFieldAction);
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
    sequenceToolbar->addAction(m_sequenceVariablesAction);
    sequenceToolbar->addAction(m_copyStepAction);
    sequenceToolbar->addAction(m_pasteStepAction);
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
    auto* functionPanel = new QWidget(sequenceWorkArea);
    functionPanel->setObjectName(QStringLiteral("flowFunctionPanel"));
    auto* functionPanelLayout = new QVBoxLayout(functionPanel);
    functionPanelLayout->setContentsMargins(0, 0, 0, 0);
    functionPanelLayout->setSpacing(0);
    m_flowTargetSelector = new FlowTargetSelector(functionPanel);
    functionPanelLayout->addWidget(m_flowTargetSelector);
    auto* pluginFunctionView = new PluginFunctionTreeView;
    m_pluginFunctionView = pluginFunctionView;
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
    polishReadableTreeView(m_pluginFunctionView);
    m_pluginFunctionView->header()->setStretchLastSection(true);
    pluginFunctionView->deviceSelectionRequired = [this] {
        m_flowTargetSelector->showSelectionRequired();
        statusBar()->showMessage(
            tr("Select a target device above before dragging a plugin function"),
            5000);
    };
    functionPanelLayout->addWidget(m_pluginFunctionView, 1);
    functionPanel->setMinimumWidth(230);
    functionPanel->setMaximumWidth(390);
    connect(m_flowTargetSelector, &FlowTargetSelector::targetChanged,
            this, [this](const QString& targetId) {
                m_pluginFunctionModel->setSelectedDeviceId(targetId);
                if (m_pluginFunctionView) {
                    m_pluginFunctionView->expandAll();
                    m_pluginFunctionView->resizeColumnToContents(0);
                }
                if (!targetId.isEmpty()) {
                    statusBar()->showMessage(
                        tr("Flow target selected: %1").arg(targetId), 3000);
                }
            });
    m_sequenceTreeView = new SequenceEditorTreeView;
    m_sequenceTreeView->setObjectName(QStringLiteral("sequenceTreeView"));
    m_sequenceTreeView->setModel(m_sequenceTreeModel);
    m_sequenceTreeView->setRootIsDecorated(true);
    m_sequenceTreeView->setUniformRowHeights(true);
    m_sequenceTreeView->setAlternatingRowColors(true);
    m_sequenceTreeView->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_sequenceTreeView->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_sequenceTreeView->setDragEnabled(true);
    m_sequenceTreeView->setAcceptDrops(true);
    m_sequenceTreeView->setDropIndicatorShown(false);
    m_sequenceTreeView->setDragDropMode(QAbstractItemView::DragDrop);
    m_sequenceTreeView->setDefaultDropAction(Qt::MoveAction);
    m_sequenceTreeView->setMouseTracking(true);
    m_sequenceTreeView->addAction(m_copyStepAction);
    m_sequenceTreeView->addAction(m_pasteStepAction);
    m_sequenceTreeView->installEventFilter(this);
    m_sequenceTreeView->viewport()->installEventFilter(this);
    m_sequenceTreeView->setItemDelegateForColumn(
        SequenceTreeModel::NameColumn,
        new DragHandleDelegate(m_sequenceTreeView));
    auto* resourceLockDelegate = new FlowResourceLockDelegate(m_sequenceTreeView);
    resourceLockDelegate->boundaryClicked = [this](const QModelIndex& index) {
        const auto rowIndex = index.siblingAtColumn(SequenceTreeModel::NameColumn);
        m_sequenceTreeView->selectionModel()->select(
            rowIndex,
            QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Rows);
        m_sequenceTreeView->setCurrentIndex(rowIndex);
        placeResourceRegionBoundary();
    };
    m_sequenceTreeView->setItemDelegateForColumn(
        SequenceTreeModel::ResourceRegionColumn,
        resourceLockDelegate);
    auto treeSizePolicy = m_sequenceTreeView->sizePolicy();
    treeSizePolicy.setHorizontalPolicy(QSizePolicy::Ignored);
    m_sequenceTreeView->setSizePolicy(treeSizePolicy);
    m_sequenceTreeView->setMinimumWidth(320);
    polishReadableTreeView(m_sequenceTreeView);
    installProportionalHeader(m_sequenceTreeView, {5, 2, 2, 1, 1, 1, 2});
    m_sequenceTreeView->setColumnHidden(SequenceTreeModel::BreakpointColumn, true);
    auto* flowHeader = m_sequenceTreeView->header();
    flowHeader->setMinimumSectionSize(28);
    flowHeader->setSectionResizeMode(SequenceTreeModel::ResourceRegionColumn,
                                     QHeaderView::Fixed);
    flowHeader->resizeSection(SequenceTreeModel::ResourceRegionColumn, 34);
    flowHeader->moveSection(
        flowHeader->visualIndex(SequenceTreeModel::ResourceRegionColumn), 0);

    auto* sequenceTreePanel = new QWidget(sequenceWorkArea);
    auto* sequenceTreeLayout = new QVBoxLayout(sequenceTreePanel);
    sequenceTreeLayout->setContentsMargins(0, 0, 0, 0);
    sequenceTreeLayout->setSpacing(0);
    auto* flowFieldSearchRow = new QWidget(sequenceTreePanel);
    flowFieldSearchRow->setObjectName(QStringLiteral("flowFieldSearchRow"));
    auto* flowFieldSearchLayout = new QHBoxLayout(flowFieldSearchRow);
    flowFieldSearchLayout->setContentsMargins(0, 0, 0, 4);
    flowFieldSearchLayout->setSpacing(0);
    flowFieldSearchLayout->addStretch();
    m_flowFieldSearch = new QLineEdit(sequenceTreePanel);
    m_flowFieldSearch->setObjectName(QStringLiteral("flowFieldSearch"));
    m_flowFieldSearch->setPlaceholderText(
        tr("Inspect key, e.g. deviceId"));
    m_flowFieldSearch->setClearButtonEnabled(true);
    m_flowFieldSearch->setFixedHeight(32);
    m_flowFieldSearch->setMaximumWidth(360);
    m_flowFieldSearch->setMinimumWidth(240);
    m_flowFieldSearch->setStyleSheet(QStringLiteral(
        "QLineEdit#flowFieldSearch {"
        " background: #eef7fd;"
        " border: 1px solid #a9cce3;"
        " border-top: 0;"
        " border-bottom-left-radius: 8px;"
        " border-bottom-right-radius: 8px;"
        " padding: 4px 28px 5px 12px;"
        " color: #253746;"
        " selection-background-color: #b9dcf2;"
        "}"
        "QLineEdit#flowFieldSearch:focus {"
        " border-color: #6faed3;"
        " background: #f7fbfe;"
        "}"));
    m_flowFieldSearch->hide();
    m_flowFieldSearch->installEventFilter(this);
    flowFieldSearchLayout->addWidget(m_flowFieldSearch, 1);
    flowFieldSearchLayout->addStretch();
    flowFieldSearchRow->hide();
    connect(m_flowFieldSearch, &QLineEdit::returnPressed, this, [this] {
        const auto field = m_flowFieldSearch->text().trimmed();
        const int matches = m_sequenceTreeModel->setInspectionField(field);
        if (auto* header = dynamic_cast<ProportionalHeaderView*>(
                m_sequenceTreeView->header())) {
            header->redistributeSections();
        }
        m_sequenceTreeView->doItemsLayout();
        m_sequenceTreeView->viewport()->update();
        if (!field.isEmpty() && matches > 0) {
            statusBar()->showMessage(
                tr("Found '%1' on %2 flow item(s)").arg(field).arg(matches),
                5000);
        } else if (!field.isEmpty()) {
            statusBar()->showMessage(
                tr("Field '%1' was not found in any flow item").arg(field),
                7000);
        }
    });
    connect(m_flowFieldSearch, &QLineEdit::textChanged, this,
            [this](const QString& text) {
                if (!text.trimmed().isEmpty()) {
                    return;
                }
                m_sequenceTreeModel->setInspectionField({});
            });
    sequenceTreeLayout->addWidget(flowFieldSearchRow);
    sequenceTreeLayout->addWidget(m_sequenceTreeView, 1);

    m_stepPropertyEditor = new StepPropertyEditor(m_sequenceDocument);
    sequenceWorkArea->addWidget(functionPanel);
    sequenceWorkArea->addWidget(sequenceTreePanel);
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

    m_stationEditorPage = new QWidget(m_workspaceTabs);
    auto* stationEditorPage = m_stationEditorPage;
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
    stationToolbar->addAction(m_fillPreviousDeviceSlotAction);
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

    m_stationDeviceView = new QTreeView(devicePane);
    m_stationDeviceView->setObjectName(QStringLiteral("stationDeviceView"));
    m_stationDeviceView->setModel(m_stationDeviceModel);
    m_stationDeviceView->setRootIsDecorated(false);
    m_stationDeviceView->setItemsExpandable(false);
    m_stationDeviceView->setIndentation(0);
    m_stationDeviceView->setExpandsOnDoubleClick(false);
    m_stationDeviceView->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_stationDeviceView->setUniformRowHeights(true);
    m_stationDeviceView->setAlternatingRowColors(true);
    m_stationDeviceView->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_stationDeviceView->setSelectionMode(QAbstractItemView::SingleSelection);
    installProportionalHeader(m_stationDeviceView, {3, 2, 4, 4, 3, 2, 2});
    m_stationDeviceView->setItemDelegateForColumn(
        StationDeviceModel::EnabledColumn,
        new OnOffItemDelegate(m_stationDeviceView));
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
    m_resultView->setMouseTracking(true);
    auto* runTestBreakpointDelegate = new RunTestBreakpointDelegate(m_resultView);
    runTestBreakpointDelegate->setBreakpointKeys(
        m_sequenceTreeModel->breakpointNodePaths());
    runTestBreakpointDelegate->breakpointToggled =
        [this](const QString& nodePath, bool enabled) {
            auto nodePaths = m_sequenceTreeModel->breakpointNodePaths();
            if (enabled) {
                nodePaths.insert(nodePath);
            } else {
                nodePaths.remove(nodePath);
            }
            m_sequenceTreeModel->setBreakpointNodePaths(std::move(nodePaths));
        };
    connect(m_sequenceTreeModel,
            &SequenceTreeModel::breakpointsChanged,
            runTestBreakpointDelegate,
            [this, runTestBreakpointDelegate] {
                runTestBreakpointDelegate->setBreakpointKeys(
                    m_sequenceTreeModel->breakpointNodePaths());
                if (m_viewModel) {
                    m_viewModel->setBreakpoints(
                        m_sequenceTreeModel->breakpointSpecs());
                }
            });
    m_resultView->setItemDelegateForColumn(
        UutStepModel::BreakpointVisualColumn,
        runTestBreakpointDelegate);
    installProportionalHeader(m_resultView, {2, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1});
    auto* resultHeader = m_resultView->header();
    resultHeader->setMinimumSectionSize(28);
    resultHeader->setSectionResizeMode(UutStepModel::BreakpointVisualColumn,
                                       QHeaderView::Fixed);
    resultHeader->resizeSection(UutStepModel::BreakpointVisualColumn, 40);
    resultHeader->moveSection(
        resultHeader->visualIndex(UutStepModel::BreakpointVisualColumn), 0);
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
    m_runtimeTimelineView->setWordWrap(false);
    m_runtimeTimelineView->horizontalHeader()->setSectionResizeMode(
        RuntimeTimelineModel::TimeColumn, QHeaderView::Interactive);
    m_runtimeTimelineView->horizontalHeader()->setSectionResizeMode(
        RuntimeTimelineModel::MessageColumn, QHeaderView::Stretch);
    m_runtimeTimelineView->setColumnWidth(RuntimeTimelineModel::TimeColumn, 112);
    details->addTab(m_runtimeTimelineView, tr("Execution Log"));
    connect(m_runtimeTimelineView,
            &QTableView::clicked,
            this,
            &MainWindow::selectTimelineEvent);

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
    auto* exportText = new QPushButton(
        style()->standardIcon(QStyle::SP_DialogSaveButton), tr("TXT"), historyPage);
    auto* exportCsv = new QPushButton(
        style()->standardIcon(QStyle::SP_DialogSaveButton), tr("CSV"), historyPage);
    auto* exportXlsx = new QPushButton(
        style()->standardIcon(QStyle::SP_DialogSaveButton), tr("XLSX"), historyPage);
    openHistory->setToolTip(tr("Open selected report"));
    exportText->setToolTip(tr("Export selected report as TXT"));
    exportCsv->setToolTip(tr("Export selected report as CSV"));
    exportXlsx->setToolTip(tr("Export selected report as XLSX"));
    historyCommands->addWidget(openHistory);
    historyCommands->addWidget(exportText);
    historyCommands->addWidget(exportCsv);
    historyCommands->addWidget(exportXlsx);
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
    connect(exportText, &QPushButton::clicked, this, [this] {
        exportSelectedHistory(HistoryExportFormat::Text);
    });
    connect(exportCsv, &QPushButton::clicked, this, [this] {
        exportSelectedHistory(HistoryExportFormat::Csv);
    });
    connect(exportXlsx, &QPushButton::clicked, this, [this] {
        exportSelectedHistory(HistoryExportFormat::Xlsx);
    });
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
    m_adminAverageTime = createCounter(QStringLiteral("adminAverageTime"));
    m_adminYield->setMinimumWidth(110);
    m_adminAverageTime->setMinimumWidth(130);
    footerLayout->addWidget(m_adminPassCount);
    footerLayout->addWidget(m_adminFailCount);
    footerLayout->addWidget(m_adminTotalCount);
    footerLayout->addWidget(m_adminYield);
    footerLayout->addWidget(m_adminAverageTime);
    runPageLayout->addWidget(footer);

    m_workspaceTabs->insertTab(0, runPage, tr("Run Test"));
    m_workspaceTabs->addTab(historyPage, tr("Reports"));
    rootLayout->addWidget(m_workspaceTabs, 1);

    setCentralWidget(central);
    buildStartupOverlay();

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
        QLabel#adminAverageTime { color: #465561; font-weight: 700; }
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
    m_runAction->setToolTip(
        m_viewModel->state() == UiRunState::CompileFailed
            ? tr("Run is unavailable because compilation failed. Fix the highlighted diagnostic and compile again.")
            : tr("Run compiled sequence"));
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
    const bool resourceExitPending = m_sequenceDocument &&
        !m_sequenceDocument->pendingResourceRegionId().isEmpty();
    if (m_sequenceTreeView) {
        if (auto* delegate = m_sequenceTreeView->itemDelegateForColumn(
                SequenceTreeModel::ResourceRegionColumn)) {
            if (delegate->property("expectUnlock").toBool() !=
                resourceExitPending) {
                delegate->setProperty("expectUnlock", resourceExitPending);
                m_sequenceTreeView->viewport()->update();
            }
        }
    }

    const bool sequenceHasChanges = hasDocument &&
        (m_sequenceDocument->isModified() ||
         (m_stepPropertyEditor && m_stepPropertyEditor->hasPendingChanges()));
    const bool stationHasPending =
        (m_stationPropertyEditor && m_stationPropertyEditor->hasPendingChanges()) ||
        (m_stationSettingsEditor && m_stationSettingsEditor->hasPendingChanges());
    const bool stationHasChanges = m_stationDocument &&
        !m_stationDocument->isEmpty() &&
        (m_stationDocument->isModified() || stationHasPending);
    const bool stationActive = isStationWorkspaceActive();
    m_saveSequenceAction->setText(
        stationActive ? tr("Save Station") : tr("Save Sequence"));
    m_saveSequenceAction->setToolTip(
        stationActive ? tr("Save Station configuration")
                      : tr("Save sequence JSON"));
    m_saveSequenceAction->setEnabled(
        canChangeSources &&
        (stationActive ? stationHasChanges : sequenceHasChanges));
    m_saveSequenceAsAction->setEnabled(canChangeSources && hasDocument);
    m_undoAction->setEnabled(
        canChangeSources && m_sequenceDocument->undoStack()->canUndo());
    m_redoAction->setEnabled(
        canChangeSources && m_sequenceDocument->undoStack()->canRedo());
    m_addStepAction->setEnabled(canChangeSources && hasDocument && hasSelection);
    m_sequenceVariablesAction->setEnabled(canChangeSources && hasDocument);
    const bool hasSelectedSteps = !selectedStepPaths.isEmpty();
    m_deleteStepAction->setEnabled(canChangeSources && hasSelectedSteps);
    m_copyStepAction->setEnabled(canChangeSources && hasSelectedSteps);
    m_pasteStepAction->setEnabled(
        canChangeSources && !m_sequenceClipboard.isEmpty() && hasSelection);
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
    const auto stationIndex = m_stationDeviceView
        ? m_stationDeviceView->currentIndex().siblingAtColumn(0)
        : QModelIndex{};
    const int stationRow = stationIndex.isValid()
        ? m_stationDeviceModel->documentRow(stationIndex)
        : m_selectedStationDeviceRow;
    const bool hasDevice = hasStation && stationRow >= 0 &&
                           stationRow < m_stationDocument->deviceCount();
    const bool groupedCan = stationIndex.isValid() &&
                            m_stationDeviceModel->isDeviceGroup(stationIndex);
    m_saveStationAction->setEnabled(
        canChangeSources && hasStation && stationHasChanges);
    m_saveStationAsAction->setEnabled(canChangeSources && hasStation);
    m_stationUndoAction->setEnabled(
        canChangeSources && m_stationDocument->undoStack()->canUndo());
    m_stationRedoAction->setEnabled(
        canChangeSources && m_stationDocument->undoStack()->canRedo());
    m_addDeviceAction->setEnabled(canChangeSources && hasStation);
    m_deleteDeviceAction->setEnabled(
        canChangeSources && hasDevice);
    m_duplicateDeviceAction->setEnabled(
        canChangeSources && hasDevice);
    m_fillPreviousDeviceSlotAction->setEnabled(
        canChangeSources && hasDevice && !groupedCan &&
        m_stationDocument->previousEmptyDeviceRow(stationRow) >= 0 &&
        !m_stationDocument->isDeviceSlotEmpty(stationRow));
    m_moveDeviceUpAction->setEnabled(
        canChangeSources && hasDevice && !groupedCan && stationRow > 0);
    m_moveDeviceDownAction->setEnabled(
        canChangeSources && hasDevice && !groupedCan &&
        stationRow + 1 < m_stationDocument->deviceCount());
    bool enabledDevice = false;
    for (const int row : m_stationDeviceModel->documentRows(stationIndex)) {
        enabledDevice = enabledDevice ||
            m_stationDocument->deviceAt(row)
                .value(QStringLiteral("enabled")).toBool(true);
    }
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
    const auto diagnostics = m_viewModel->diagnostics();
    m_diagnosticModel->setDiagnostics(diagnostics);
    if (auto* details = findChild<QTabWidget*>(QStringLiteral("runDetailsTabs"))) {
        const int index = details->indexOf(m_diagnosticView);
        if (index >= 0) {
            details->setTabText(
                index,
                diagnostics.isEmpty()
                    ? tr("Diagnostics")
                    : tr("Diagnostics (%1)").arg(diagnostics.size()));
        }
    }
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
    const qint64 average = total > 0
        ? m_adminTotalCompletedDurationMs / total
        : 0;
    m_adminAverageTime->setText(tr("AVG %1:%2.%3")
        .arg(average / 60000, 2, 10, QLatin1Char('0'))
        .arg(average / 1000 % 60, 2, 10, QLatin1Char('0'))
        .arg(average % 1000, 3, 10, QLatin1Char('0')));
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
    if (report.completed && m_runArtifactWriter->active()) {
        const auto archived = m_runArtifactWriter->finalize(report);
        if (!archived.success) {
            statusBar()->showMessage(
                tr("Report archive failed: %1").arg(archived.errorMessage),
                10000);
        }
    }
    if (report.completed && !m_currentAdminRunCounted) {
        if (report.state == PicoATE::Core::ExecutionState::Completed &&
            !report.hasError) {
            ++m_adminPassedUnits;
        } else {
            ++m_adminFailedUnits;
        }
        m_adminTotalCompletedDurationMs += m_adminElapsed.isValid()
            ? m_adminElapsed.elapsed()
            : 0;
        m_currentAdminRunCounted = true;
        updateAdminYield();
    }
    displayReport(report);
}

void MainWindow::updateDebugSnapshot()
{
    const auto snapshot = m_viewModel->debugSnapshot();
    m_debugSnapshotModel->setSnapshot(snapshot);
    setRunTestInstructionPointer(snapshot ? snapshot->currentNodeId : QString{});
}

void MainWindow::setRunTestInstructionPointer(const QString& nodePath)
{
    if (auto* delegate = findChild<QObject*>(
            QStringLiteral("runTestBreakpointDelegate"))) {
        delegate->setProperty("currentNodePath", nodePath);
    }
    if (m_resultView && m_resultView->viewport()) {
        m_resultView->viewport()->update();
    }
}

void MainWindow::displayReport(const PicoATE::Core::ExecutionReport& report)
{
    PicoATE::Core::UutId selectedUutId;
    PicoATE::Core::NodeId selectedStepId;
    const auto current = m_resultView->currentIndex();
    if (const auto selectedUut = m_uutStepModel->uutAt(current)) {
        selectedUutId = selectedUut->uutId;
    }
    if (const auto selectedStep = m_uutStepModel->stepAt(current)) {
        selectedStepId = selectedStep->nodePath.isEmpty()
            ? selectedStep->stepId
            : selectedStep->nodePath;
    }

    m_uutStepModel->setReport(report);
    if (report.planId.isEmpty() && report.uuts.isEmpty()) {
        m_deviceStatusModel->clear();
    }
    m_attemptModel->setStep(std::nullopt);
    m_measurementModel->setMeasurements({});
    m_resultView->expandAll();
    const auto restored = m_uutStepModel->indexForStep(selectedUutId,
                                                        selectedStepId);
    if (restored.isValid()) {
        m_resultView->setCurrentIndex(restored);
        m_resultView->scrollTo(restored,
                               QAbstractItemView::PositionAtCenter);
        updateStepDetails(restored);
    } else {
        selectInitialResult();
    }
}

void MainWindow::applyRuntimeEvents(
    const QVector<PicoATE::Core::RuntimeEvent>& events)
{
    m_operatorPromptPresenter->applyRuntimeEvents(events);
    PicoATE::Core::UutId selectedUutId;
    PicoATE::Core::NodeId selectedStepId;
    const auto current = m_resultView->currentIndex();
    const auto selectedUut = m_uutStepModel->uutAt(current);
    const auto selectedStep = m_uutStepModel->stepAt(current);
    if (selectedUut) {
        selectedUutId = selectedUut->uutId;
    }
    if (selectedStep) {
        selectedStepId = selectedStep->nodePath.isEmpty()
            ? selectedStep->stepId
            : selectedStep->nodePath;
    }

    m_uutStepModel->applyRuntimeEvents(events);
    m_deviceStatusModel->applyRuntimeEvents(events);
    const auto logLines = m_runtimeTimelineModel->applyRuntimeEvents(events);
    const auto written = m_runArtifactWriter->appendLogLines(logLines);
    if (!written.success) {
        statusBar()->showMessage(
            tr("TXT log write failed: %1").arg(written.errorMessage),
            10000);
    }
    if (m_runtimeTimelineView->model()->rowCount() > 0) {
        m_runtimeTimelineView->scrollToBottom();
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
        if (!event.nodeId.isEmpty() &&
            event.activationState == PicoATE::Core::ActivationState::Running) {
            const auto index = m_uutStepModel->indexForStep(event.uutId,
                                                             event.nodeId);
            if (index.isValid()) {
                m_resultView->setCurrentIndex(index);
                m_resultView->scrollTo(
                    index, QAbstractItemView::PositionAtBottom);
                updateStepDetails(index);
            }
        }
        if (event.kind == PicoATE::Core::RuntimeEventKind::BreakpointHit ||
            event.kind == PicoATE::Core::RuntimeEventKind::DebugStepCompleted) {
            const auto instructionNode =
                event.kind == PicoATE::Core::RuntimeEventKind::DebugStepCompleted
                ? nextPendingRunTestNodePath(m_uutStepModel,
                                             event.uutId,
                                             event.nodeId)
                : event.nodeId;
            setRunTestInstructionPointer(
                instructionNode.isEmpty() ? event.nodeId : instructionNode);
            focusDebugNode(event);
        }
    }
    updateAdminProgress();
}

void MainWindow::selectRuntimeEvent(const PicoATE::Core::RuntimeEvent& event)
{
    if (!event.nodeId.isEmpty() && m_resultView) {
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
    const auto index = m_runtimeTimelineModel->index(
        row, RuntimeTimelineModel::MessageColumn);
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

void MainWindow::focusExecutionLogForResult(const QModelIndex& index)
{
    const auto step = m_uutStepModel->stepAt(index);
    if (!step || !m_runtimeTimelineModel || !m_runtimeTimelineView) {
        return;
    }
    const auto uut = m_uutStepModel->uutAt(index);
    const auto nodeId = step->nodePath.isEmpty() ? step->stepId : step->nodePath;
    int row = m_runtimeTimelineModel->rowForNode(
        uut ? uut->uutId : PicoATE::Core::UutId{}, nodeId);
    if (row < 0 && nodeId != step->stepId) {
        row = m_runtimeTimelineModel->rowForNode(
            uut ? uut->uutId : PicoATE::Core::UutId{}, step->stepId);
    }
    if (row < 0) {
        statusBar()->showMessage(
            tr("No execution log is available for %1 yet").arg(step->displayName),
            3000);
        return;
    }
    if (auto* details = findChild<QTabWidget*>(QStringLiteral("runDetailsTabs"))) {
        details->setCurrentWidget(m_runtimeTimelineView);
    }
    const auto logIndex = m_runtimeTimelineModel->index(
        row, RuntimeTimelineModel::MessageColumn);
    m_runtimeTimelineView->setCurrentIndex(logIndex);
    m_runtimeTimelineView->scrollTo(
        logIndex, QAbstractItemView::PositionAtCenter);
}

void MainWindow::focusDebugNode(const PicoATE::Core::RuntimeEvent& event)
{
    if (event.nodeId.isEmpty() || !m_sequenceTreeModel || !m_sequenceTreeView) {
        return;
    }

    selectTimelineSequence(event.sequenceNumber);
    selectRuntimeEvent(event);

    if (m_resultView) {
        const auto resultIndex =
            m_uutStepModel->indexForStep(event.uutId, event.nodeId);
        if (resultIndex.isValid()) {
            m_resultView->scrollTo(
                resultIndex, QAbstractItemView::PositionAtBottom);
        }
    }

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

void MainWindow::exportSelectedHistory(HistoryExportFormat format)
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
    const bool csv = format == HistoryExportFormat::Csv;
    const bool xlsx = format == HistoryExportFormat::Xlsx;
    const auto suffix = xlsx
        ? QStringLiteral("xlsx")
        : (csv ? QStringLiteral("csv") : QStringLiteral("txt"));
    const auto path = QFileDialog::getSaveFileName(
        this,
        xlsx ? tr("Export XLSX Report")
             : (csv ? tr("Export CSV Report") : tr("Export TXT Report")),
        entry->id + '.' + suffix,
        xlsx ? tr("Excel Workbook (*.xlsx)")
             : (csv ? tr("CSV Report (*.csv)") : tr("TXT Report (*.txt)")));
    if (path.isEmpty()) {
        return;
    }
    const auto result = xlsx
        ? ReportExporter::saveXlsx(path, loaded.report)
        : (csv ? ReportExporter::saveCsv(path, loaded.report)
               : ReportExporter::saveText(path, loaded.report));
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
