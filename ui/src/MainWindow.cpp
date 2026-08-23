#include "MainWindow.h"

#include "ApplicationDiagnostics.h"
#include "CoreExecutionService.h"
#include "ExecutionViewModel.h"
#include "FlowTargetSelector.h"
#include "LoadingSpinner.h"
#include "MultiUutOverviewWidget.h"
#include "OnOffControl.h"
#include "OperatorPromptPresenter.h"
#include "ParserActualDelegate.h"
#include "PluginCatalog.h"
#include "PluginFunctionModel.h"
#include "ProductRoutingDialog.h"
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
#include "StartupSupport.h"
#include "StepPropertyEditor.h"
#include "YieldDonutWidget.h"

#include <QAction>
#include <QButtonGroup>
#include <QCloseEvent>
#include <QComboBox>
#include <QCoreApplication>
#include <QDateTime>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QEvent>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFont>
#include <QFontMetrics>
#include <QFrame>
#include <QFormLayout>
#include <QGraphicsDropShadowEffect>
#include <QGuiApplication>
#include <QGridLayout>
#include <QHeaderView>
#include <QHash>
#include <QHBoxLayout>
#include <QIcon>
#include <QInputDialog>
#include <QItemSelectionModel>
#include <QJsonArray>
#include <QJsonDocument>
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
#include <QProcess>
#include <QPointer>
#include <QPolygonF>
#include <QPixmap>
#include <QRegularExpression>
#include <QResizeEvent>
#include <QScreen>
#include <QScrollArea>
#include <QScrollBar>
#include <QSaveFile>
#include <QSet>
#include <QSettings>
#include <QSignalBlocker>
#include <QSizePolicy>
#include <QSpinBox>
#include <QStackedWidget>
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
#include <QVariantAnimation>
#include <QVBoxLayout>
#include <QWidget>

#include "PicoATE/Core/StationConfig.h"
#include "PicoATE/Core/ProductRouting.h"

#include <optional>
#include <initializer_list>
#include <algorithm>
#include <functional>
#include <limits>
#include <utility>

namespace PicoATE::Ui {

namespace {

constexpr int MaxRecentFiles = 8;
// Temporary presentation gates for the staged multi-UUT UI rollout.
// Restore them in order: count control, detail switcher, then overview.
constexpr bool ShowAdminUutCountControl = false;
constexpr bool ShowAdminUutSwitcher = false;
constexpr bool ShowAdminUutOverview = false;
const QString StationDiagnosticPrefix = QStringLiteral("Station: ");
const QString RegisterDirectoryName = QStringLiteral("register");

bool containsSequencePath(const SequenceItemPath& parent,
                          const SequenceItemPath& candidate)
{
    if (!parent.isValid() || !candidate.isValid() ||
        parent.groupIndex != candidate.groupIndex ||
        parent.stepIndices.size() > candidate.stepIndices.size()) {
        return false;
    }
    return std::equal(parent.stepIndices.cbegin(), parent.stepIndices.cend(),
                      candidate.stepIndices.cbegin());
}

QIcon toolbarIcon(const char* name)
{
    return QIcon(QStringLiteral(":/icons/%1.svg")
                     .arg(QString::fromLatin1(name)));
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

QString registerImporterExecutablePath()
{
    const QFileInfo importer(
        QDir(QCoreApplication::applicationDirPath())
            .filePath(QStringLiteral("PicoATE.RegisterImporter.exe")));
    return importer.exists() && importer.isFile()
        ? importer.absoluteFilePath()
        : QString{};
}

class RegisterWorkbookDialog final : public QDialog
{
public:
    explicit RegisterWorkbookDialog(const QFileInfoList& workbooks,
                                    const QString& preferredWorkbookPath,
                                    QWidget* parent = nullptr)
        : QDialog(parent)
    {
        setObjectName(QStringLiteral("registerWorkbookDialog"));
        setWindowTitle(tr("Import Register Workbook"));
        setModal(true);
        setWindowFlags(Qt::Dialog | Qt::FramelessWindowHint);
        setAttribute(Qt::WA_TranslucentBackground);
        setFixedSize(420, 238);

        auto* outerLayout = new QVBoxLayout(this);
        outerLayout->setContentsMargins(16, 16, 16, 16);
        outerLayout->setSpacing(0);

        auto* card = new QFrame(this);
        card->setObjectName(QStringLiteral("registerWorkbookCard"));
        auto* shadow = new QGraphicsDropShadowEffect(card);
        shadow->setBlurRadius(30.0);
        shadow->setOffset(0.0, 8.0);
        shadow->setColor(QColor(32, 38, 45, 60));
        card->setGraphicsEffect(shadow);
        outerLayout->addWidget(card);

        auto* cardLayout = new QVBoxLayout(card);
        cardLayout->setContentsMargins(24, 22, 24, 24);
        cardLayout->setSpacing(12);

        auto* title = new QLabel(tr("Import Register Workbook"), card);
        title->setObjectName(QStringLiteral("registerWorkbookTitle"));
        title->setAlignment(Qt::AlignCenter);
        cardLayout->addWidget(title);

        auto* hint = new QLabel(
            tr("Select a workbook from the project register folder"), card);
        hint->setObjectName(QStringLiteral("registerWorkbookHint"));
        hint->setAlignment(Qt::AlignCenter);
        cardLayout->addWidget(hint);

        m_workbookCombo = new QComboBox(card);
        m_workbookCombo->setObjectName(
            QStringLiteral("registerWorkbookCombo"));
        m_workbookCombo->setMinimumHeight(40);
        m_workbookCombo->setSizeAdjustPolicy(
            QComboBox::AdjustToMinimumContentsLengthWithIcon);
        int preferredIndex = -1;
        const auto preferredAbsolutePath = preferredWorkbookPath.trimmed().isEmpty()
            ? QString{}
            : QFileInfo(preferredWorkbookPath).absoluteFilePath();
        const auto preferredFileName =
            QFileInfo(preferredWorkbookPath).fileName();
        for (const auto& workbook : workbooks) {
            m_workbookCombo->addItem(workbook.fileName(),
                                     workbook.absoluteFilePath());
            const int comboIndex = m_workbookCombo->count() - 1;
            if (preferredIndex < 0 &&
                (!preferredAbsolutePath.isEmpty() &&
                 workbook.absoluteFilePath().compare(
                     preferredAbsolutePath, Qt::CaseInsensitive) == 0)) {
                preferredIndex = comboIndex;
            } else if (preferredIndex < 0 &&
                       !preferredFileName.isEmpty() &&
                       workbook.fileName().compare(
                           preferredFileName, Qt::CaseInsensitive) == 0) {
                preferredIndex = comboIndex;
            }
            const auto modelIndex = m_workbookCombo->model()->index(
                comboIndex, 0);
            m_workbookCombo->model()->setData(
                modelIndex, Qt::AlignCenter, Qt::TextAlignmentRole);
            m_workbookCombo->setItemData(
                comboIndex,
                QDir::toNativeSeparators(workbook.absoluteFilePath()),
                Qt::ToolTipRole);
        }
        m_workbookCombo->setCurrentIndex(
            preferredIndex >= 0 ? preferredIndex : 0);
        cardLayout->addWidget(m_workbookCombo);
        cardLayout->addStretch();

        auto* buttonRow = new QHBoxLayout;
        buttonRow->setContentsMargins(0, 0, 0, 0);
        buttonRow->setSpacing(10);
        buttonRow->addStretch();
        auto* importButton = new QPushButton(tr("Import"), card);
        importButton->setObjectName(
            QStringLiteral("registerWorkbookImportButton"));
        importButton->setDefault(true);
        importButton->setFixedSize(112, 40);
        buttonRow->addWidget(importButton);
        auto* cancelButton = new QPushButton(tr("Cancel"), card);
        cancelButton->setObjectName(
            QStringLiteral("registerWorkbookCancelButton"));
        cancelButton->setFixedSize(112, 40);
        buttonRow->addWidget(cancelButton);
        cardLayout->addLayout(buttonRow);

        setStyleSheet(QStringLiteral(R"css(
            QFrame#registerWorkbookCard {
                background: #ffffff;
                border: 1px solid #d7dbe0;
                border-radius: 7px;
            }
            QLabel#registerWorkbookTitle {
                color: #30343a;
                background: transparent;
                font-size: 16px;
                font-weight: 700;
            }
            QLabel#registerWorkbookHint {
                color: #7b828a;
                background: transparent;
                font-size: 12px;
            }
            QComboBox#registerWorkbookCombo {
                color: #30343a;
                background: #f5f6f8;
                border: 1px solid #c9cdd3;
                border-radius: 5px;
                padding: 0 34px 0 14px;
                selection-background-color: #cfd5dc;
            }
            QComboBox#registerWorkbookCombo:hover {
                background: #ffffff;
                border-color: #9fa6ae;
            }
            QComboBox#registerWorkbookCombo:focus {
                background: #ffffff;
                border-color: #686e76;
            }
            QComboBox#registerWorkbookCombo::drop-down {
                subcontrol-origin: padding;
                subcontrol-position: top right;
                width: 32px;
                border: none;
                background: transparent;
            }
            QComboBox#registerWorkbookCombo::down-arrow {
                image: url(:/icons/arrow-down.svg);
                width: 13px;
                height: 13px;
            }
            QComboBox#registerWorkbookCombo QAbstractItemView {
                color: #30343a;
                background: #ffffff;
                border: 1px solid #c9cdd3;
                selection-color: #202328;
                selection-background-color: #e2e5e9;
                outline: none;
            }
            QPushButton#registerWorkbookCancelButton,
            QPushButton#registerWorkbookImportButton {
                border-radius: 5px;
                font-weight: 700;
            }
            QPushButton#registerWorkbookCancelButton {
                color: #3d4248;
                background: #ffffff;
                border: 1px solid #c9cdd3;
            }
            QPushButton#registerWorkbookCancelButton:hover {
                background: #f2f3f5;
                border-color: #9fa6ae;
            }
            QPushButton#registerWorkbookImportButton {
                color: #ffffff;
                background: #34383e;
                border: 1px solid #34383e;
            }
            QPushButton#registerWorkbookImportButton:hover {
                background: #24272b;
                border-color: #24272b;
            }
            QPushButton#registerWorkbookImportButton:pressed {
                background: #17191c;
                border-color: #17191c;
            }
        )css"));

        connect(cancelButton, &QPushButton::clicked,
                this, &QDialog::reject);
        connect(importButton, &QPushButton::clicked,
                this, &QDialog::accept);
    }

    QString selectedWorkbookPath() const
    {
        return m_workbookCombo
            ? m_workbookCombo->currentData().toString()
            : QString{};
    }

private:
    QComboBox* m_workbookCombo = nullptr;
};

bool writeJsonObjectFile(const QString& filePath,
                         const QJsonObject& object,
                         QString* errorMessage)
{
    QSaveFile file(filePath);
    if (!file.open(QIODevice::WriteOnly)) {
        if (errorMessage) {
            *errorMessage = file.errorString();
        }
        return false;
    }
    const auto bytes = QJsonDocument(object).toJson(QJsonDocument::Indented);
    if (file.write(bytes) != bytes.size() || !file.commit()) {
        if (errorMessage) {
            *errorMessage = file.errorString();
        }
        return false;
    }
    return true;
}

QString projectIdentifier(QString projectName)
{
    projectName = projectName.trimmed().toLower();
    projectName.replace(QRegularExpression(QStringLiteral("[^a-z0-9]+")),
                        QStringLiteral("-"));
    projectName.remove(QRegularExpression(QStringLiteral("^-+|-+$")));
    return projectName.isEmpty() ? QStringLiteral("project") : projectName;
}

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
        return QStringLiteral("background:#f4d768;color:#493a00;border:1px solid #cbaa39;border-radius:6px;");
    case UiRunState::Completed:
        return QStringLiteral("background:#cfe8d5;color:#1f5d35;border:1px solid #86b794;border-radius:6px;");
    case UiRunState::Failed:
    case UiRunState::CompileFailed:
        return QStringLiteral("background:#efc9c9;color:#862a2a;border:1px solid #c98282;border-radius:6px;");
    default:
        return QStringLiteral("background:#e7eaec;color:#303940;border:1px solid #c8cfd4;border-radius:6px;");
    }
}

QString adminOverviewStateText(UiRunState state, bool stopRequested)
{
    switch (state) {
    case UiRunState::Starting: return QObject::tr("STARTING");
    case UiRunState::Running: return QObject::tr("TESTING");
    case UiRunState::Pausing: return QObject::tr("PAUSING");
    case UiRunState::Paused: return QObject::tr("PAUSED");
    case UiRunState::Stopping: return QObject::tr("STOPPING");
    case UiRunState::Completed:
    case UiRunState::Failed:
        return stopRequested ? QObject::tr("STOPPED")
                             : QObject::tr("COMPLETED");
    case UiRunState::Ready: return QObject::tr("READY");
    default: return QObject::tr("WAITING");
    }
}

void setAdminOverallResultTypography(QLabel* label,
                                     bool overview,
                                     bool compact)
{
    if (!label) {
        return;
    }
    auto font = label->font();
    font.setBold(true);
    font.setPointSize(overview ? (compact ? 12 : 14)
                               : (compact ? 18 : 21));
    label->setFont(font);
    label->setWordWrap(overview);
}

QString adminOverviewStateStyle(UiRunState state, bool stopRequested)
{
    switch (state) {
    case UiRunState::Starting:
        return QStringLiteral("background:#e7eaec;color:#303940;"
                              "border:1px solid #c8cfd4;border-radius:6px;");
    case UiRunState::Running:
    case UiRunState::Pausing:
        return QStringLiteral("background:#f4d768;color:#493a00;"
                              "border:1px solid #cbaa39;border-radius:6px;");
    case UiRunState::Paused:
        return QStringLiteral("background:#dceaf2;color:#315f78;"
                              "border:1px solid #9abed1;border-radius:6px;");
    case UiRunState::Stopping:
        return QStringLiteral("background:#f1dddd;color:#7f3939;"
                              "border:1px solid #cfa0a0;border-radius:6px;");
    case UiRunState::Completed:
    case UiRunState::Failed:
        return stopRequested
            ? QStringLiteral("background:#f1dddd;color:#7f3939;"
                             "border:1px solid #cfa0a0;border-radius:6px;")
            : QStringLiteral("background:#dfe7eb;color:#324751;"
                             "border:1px solid #aab9c1;border-radius:6px;");
    default:
        return QStringLiteral("background:#e7eaec;color:#303940;"
                              "border:1px solid #c8cfd4;border-radius:6px;");
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
    m_uutOverviewModel = new UutOverviewModel(this);
    m_attemptModel = new AttemptModel(this);
    m_measurementModel = new MeasurementModel(this);
    m_runtimeTimelineModel = new RuntimeTimelineModel(this);
    m_runtimeTimelineProxy = new UutRuntimeTimelineProxyModel(this);
    m_runtimeTimelineProxy->setSourceModel(m_runtimeTimelineModel);
    m_debugSnapshotModel = new DebugSnapshotModel(this);
    m_scanDialog = new ScanDialog(this);
    serviceAdminStartupAnimation();
    buildActions();
    serviceAdminStartupAnimation();
    buildLayout();
    m_operatorPromptPresenter->setOverviewHost(
        ShowAdminUutOverview ? m_adminUutOverview : nullptr);
    serviceAdminStartupAnimation();
    restoreUiSettings();
    serviceAdminStartupAnimation();
    QTimer::singleShot(0, this, [this] { applyResponsiveLayout(); });

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
                    m_adminLastAutoFollowLine = 0;
                    m_adminLastAutoFollowUutId.clear();
                    m_adminLastAutoFollowNodeId.clear();
                }
                if (state == UiRunState::Completed || state == UiRunState::Failed) {
                    m_sequenceTreeModel->setCurrentDebugNodePath({});
                }
                if (state == UiRunState::Ready && m_autoRouteBySn &&
                    !m_pendingRoutedSerialNumbers.isEmpty()) {
                    const auto serialNumbers = std::exchange(
                        m_pendingRoutedSerialNumbers, QStringList{});
                    QTimer::singleShot(0, this, [this, serialNumbers] {
                        startAdminRunWithSerials(serialNumbers);
                    });
                }
                updateAdminRunState(state);
                if (state == UiRunState::CompileFailed) {
                    const bool routedCompile = m_autoRouteBySn &&
                        !m_pendingRoutedSerialNumbers.isEmpty();
                    m_pendingRoutedSerialNumbers.clear();
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
                    if (routedCompile) {
                        QTimer::singleShot(0, this, [this, summary] {
                            showProductRoutingError(summary);
                        });
                    }
                } else {
                    statusBar()->showMessage(uiRunStateName(state));
                }
            });
    connect(m_scanDialog,
            &ScanDialog::barcodesAccepted,
            this,
            &MainWindow::runScannedUuts);
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
    connect(m_stepPropertyEditor,
            &StepPropertyEditor::inspectionFieldRequested,
            this,
            [this](const QString& fieldPath, const QString& displayName) {
                const int matches = m_sequenceTreeModel->setInspectionField(
                    fieldPath, displayName);
                m_stepPropertyEditor->setInspectionField(fieldPath);
                if (auto* header = dynamic_cast<ProportionalHeaderView*>(
                        m_sequenceTreeView->header())) {
                    header->redistributeSections();
                }
                m_sequenceTreeView->doItemsLayout();
                m_sequenceTreeView->viewport()->update();
                if (fieldPath.isEmpty()) {
                    statusBar()->showMessage(tr("Field inspection cleared"),
                                             3000);
                } else if (matches > 0) {
                    statusBar()->showMessage(
                        tr("Showing '%1' on %2 Flow item(s)")
                            .arg(displayName).arg(matches),
                        5000);
                } else {
                    statusBar()->showMessage(
                        tr("'%1' is not stored on any Flow item")
                            .arg(displayName),
                        7000);
                }
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
                if (!m_historyLoaded && m_historyPage &&
                    m_workspaceTabs->widget(currentIndex) == m_historyPage) {
                    m_historyLoaded = true;
                    QTimer::singleShot(0, this, [this] { refreshHistory(); });
                }
                if (m_runTestPage &&
                    m_workspaceTabs->widget(currentIndex) == m_runTestPage) {
                    QTimer::singleShot(
                        0, this, [this] { refreshVisibleRuntimeViews(); });
                }
                updateCommandState();
            });
    connect(m_resultView->selectionModel(),
            &QItemSelectionModel::currentChanged,
            this,
            [this](const QModelIndex& current) {
                updateStepDetails(current);
                selectFlowNodeForResult(current);
            });
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
    serviceAdminStartupAnimation();
}

MainWindow::~MainWindow()
{
    ApplicationDiagnostics::recordAction(QStringLiteral("ADMIN_WINDOW_CLOSE"));
    m_runArtifactWriter->abandon();
    waitForPluginScan();
    beginShutdown();
}

std::unique_ptr<MainWindow> createMainWindow()
{
    return std::make_unique<MainWindow>();
}

QString MainWindow::newProjectRootPath() const
{
    if (!m_newProjectRootPath.trimmed().isEmpty()) {
        return QFileInfo(m_newProjectRootPath).absoluteFilePath();
    }
    if (!m_productRoutingPath.trimmed().isEmpty()) {
        const auto routing = PicoATE::Core::loadProductRoutingFile(
            m_productRoutingPath);
        if (routing.ok() && !routing.config.projectRootPath.isEmpty()) {
            return QFileInfo(routing.config.projectRootPath).absoluteFilePath();
        }
    }
    return StartupSupport::productProjectRootPathForRoot(
        QCoreApplication::applicationDirPath());
}

void MainWindow::initializeNewProjectTemplate(const QString& projectRootPath)
{
    m_autoRouteBySn = false;
    if (m_scanDialog) {
        m_scanDialog->hide();
    }
    m_newProjectRootPath = projectRootPath.trimmed().isEmpty()
        ? newProjectRootPath()
        : QFileInfo(projectRootPath).absoluteFilePath();
    m_newProjectTemplate = true;

    m_loadingSequenceFile = true;
    m_expandSequenceTreeOnNextUpdate = true;
    const bool sequenceReady = m_sequenceDocument->initializeNew(
        StartupSupport::newProjectSequenceTemplate());
    m_loadingSequenceFile = false;
    const bool stationReady = m_stationDocument->initializeNew(
        StartupSupport::newProjectStationTemplate());
    if (!sequenceReady || !stationReady) {
        m_newProjectTemplate = false;
        statusBar()->showMessage(tr("Failed to initialize the new project template"),
                                 5000);
        return;
    }

    m_sequenceTreeModel->clearBreakpoints();
    m_sequenceTreeModel->setCurrentDebugNodePath({});
    m_selectedSequencePath = {};
    m_selectedSequenceNodePath.clear();
    m_selectedStationDeviceRow = -1;
    m_pendingStationLogicalIdMigrations.clear();
    synchronizeSequenceSnapshot();
    synchronizeStationSnapshot();
    updateSequenceEditor();
    updateStationEditor();
    updateAdminStationSummary();
    if (m_adminSequenceLabel) {
        m_adminSequenceLabel->setText(tr("New Project Template"));
        m_adminSequenceLabel->setToolTip(
            tr("The first save creates sequence.json and StationSystem.json together"));
    }
    updateWindowTitle();
    updateCommandState();
}

void MainWindow::createNewProject()
{
    if (!m_viewModel || !m_viewModel->canChangeSources()) {
        statusBar()->showMessage(
            tr("Wait for the current operation to finish before creating a project"),
            4000);
        return;
    }
    if (!maybeSaveSequence() || !maybeSaveStation()) {
        return;
    }
    initializeNewProjectTemplate(newProjectRootPath());
    if (m_workspaceTabs && m_flowEditorPage) {
        m_handlingWorkspaceTabChange = true;
        m_workspaceTabs->setCurrentWidget(m_flowEditorPage);
        m_handlingWorkspaceTabChange = false;
        m_previousWorkspaceTabIndex =
            m_workspaceTabs->indexOf(m_flowEditorPage);
    }
    statusBar()->showMessage(
        tr("New project template created. The first save will ask for a project name."),
        5000);
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
    m_newProjectTemplate = false;
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

void MainWindow::configureAutoRouting(const QString& productRoutingPath)
{
    m_autoRouteBySn = true;
    setProductRoutingPath(productRoutingPath);
    m_pendingRoutedSerialNumbers.clear();
    updateCommandState();
}

void MainWindow::setProductRoutingPath(const QString& productRoutingPath)
{
    m_productRoutingPath = productRoutingPath.trimmed().isEmpty()
        ? QString{}
        : QFileInfo(productRoutingPath).absoluteFilePath();
}

QString MainWindow::effectiveProductRoutingPath() const
{
    if (!m_productRoutingPath.trimmed().isEmpty()) {
        return QFileInfo(m_productRoutingPath).absoluteFilePath();
    }
    if (m_stationDocument && !m_stationDocument->filePath().isEmpty()) {
        return QFileInfo(m_stationDocument->filePath()).absoluteDir().filePath(
            QStringLiteral("ProductRouting.json"));
    }
    if (m_sequenceDocument && !m_sequenceDocument->filePath().isEmpty()) {
        return QFileInfo(m_sequenceDocument->filePath()).absoluteDir().filePath(
            QStringLiteral("ProductRouting.json"));
    }
    return QDir(QCoreApplication::applicationDirPath()).filePath(
        QStringLiteral("ProductRouting.json"));
}

void MainWindow::openProductRoutingConfiguration()
{
    if (!m_viewModel || !m_viewModel->canChangeSources()) {
        statusBar()->showMessage(
            tr("Wait for the current operation to finish before editing routes"),
            4000);
        return;
    }

    const bool restoreScanner = m_scanDialog && m_scanDialog->isVisible();
    if (m_scanDialog) {
        m_scanDialog->hide();
    }
    ProductRoutingDialog dialog(effectiveProductRoutingPath(), this);
    connect(&dialog, &ProductRoutingDialog::routingSaved, this, [this] {
        statusBar()->showMessage(
            tr("Product routing saved. Changes apply to the next scan."), 5000);
    });
    dialog.exec();
    if (restoreScanner) {
        showStartupScanDialog();
    }
}

void MainWindow::showStartupScanDialog()
{
    if (!m_autoRouteBySn || (m_scanDialog && m_scanDialog->isVisible())) {
        return;
    }
    toggleScanDialog();
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
    m_adminWorkspaceInitializing = true;
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

void MainWindow::resizeEvent(QResizeEvent* event)
{
    QMainWindow::resizeEvent(event);
    applyResponsiveLayout();
}

void MainWindow::applyResponsiveLayout(bool force)
{
    if (!m_workspaceTabs) {
        return;
    }

    const bool compact = width() < 1400 || height() < 760;
    const int mode = compact ? 1 : 0;
    const int previousMode = m_responsiveLayoutMode;
    if (!force && previousMode == mode) {
        return;
    }
    m_responsiveLayoutMode = mode;

    // On a first launch at normal size, the build-time defaults are already
    // correct. Restored splitter positions are kept until the screen class
    // actually changes.
    if (!force && !compact && previousMode < 0) {
        return;
    }

    if (auto* layout = qobject_cast<QVBoxLayout*>(centralWidget()->layout())) {
        const int margin = compact ? 8 : 12;
        layout->setContentsMargins(margin, margin, margin, margin);
        layout->setSpacing(compact ? 7 : 10);
    }

    const auto setSplitterSizes = [this](const char* objectName,
                                         const QList<int>& normalSizes,
                                         const QList<int>& compactSizes) {
        if (auto* splitter = findChild<QSplitter*>(
                QString::fromLatin1(objectName))) {
            splitter->setSizes(m_responsiveLayoutMode == 1
                                   ? compactSizes
                                   : normalSizes);
        }
    };
    setSplitterSizes("sequenceWorkSplitter",
                     {220, 560, 380}, {180, 520, 280});
    setSplitterSizes("sequenceVerticalSplitter",
                     {520, 140}, {560, 100});
    setSplitterSizes("stationWorkSplitter",
                     {260, 610, 390}, {180, 540, 260});
    setSplitterSizes("stationVerticalSplitter",
                     {520, 140}, {560, 100});
    setSplitterSizes("runSplitter",
                     {230, 900}, {190, 900});

    if (m_adminSequenceLabel) {
        m_adminSequenceLabel->setMinimumHeight(compact ? 32 : 36);
    }
    if (auto* sidebar = findChild<QWidget*>(
            QStringLiteral("adminRunSidebar"))) {
        sidebar->setMinimumWidth(compact ? 185 : 205);
        sidebar->setMaximumWidth(compact ? 245 : 265);
        if (auto* sidebarLayout = qobject_cast<QVBoxLayout*>(sidebar->layout())) {
            const int margin = compact ? 12 : 18;
            sidebarLayout->setContentsMargins(margin, margin, margin, margin);
            sidebarLayout->setSpacing(compact ? 8 : 12);
        }
    }
    if (m_adminOverallResult) {
        m_adminOverallResult->setMinimumHeight(compact ? 78 : 104);
        const bool showingOverview = m_adminRunStack && m_adminRunOverviewPage &&
            m_adminRunStack->currentWidget() == m_adminRunOverviewPage;
        setAdminOverallResultTypography(
            m_adminOverallResult, showingOverview, compact);
    }
    if (m_adminYieldChart) {
        m_adminYieldChart->setMinimumHeight(compact ? 56 : 64);
        m_adminYieldChart->setMaximumHeight(compact ? 76 : 100);
    }
}

bool MainWindow::eventFilter(QObject* watched, QEvent* event)
{
    if (watched &&
        watched->objectName() == QStringLiteral("adminRunSidebar") &&
        event->type() == QEvent::Resize) {
        if (auto* sidebar = qobject_cast<QWidget*>(watched);
            sidebar) {
            auto* brandSlot = findChild<QWidget*>(
                QStringLiteral("adminBrandSlot"));
            if (!brandSlot) {
                return QMainWindow::eventFilter(watched, event);
            }
            brandSlot->setFixedWidth(
                sidebar->width());
            if (m_adminUutNavigationLead) {
                m_adminUutNavigationLead->setFixedWidth(sidebar->width());
            }
        }
    }
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
    m_newProjectTemplate = false;
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
    if (m_registerImportProcess) {
        m_registerImportProcess->disconnect(this);
        m_registerImportProcess->kill();
        m_registerImportProcess->waitForFinished(1000);
        m_registerImportProcess = nullptr;
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

bool MainWindow::saveNewProjectAs()
{
    if (!m_newProjectTemplate || !m_sequenceDocument ||
        !m_stationDocument || m_sequenceDocument->isEmpty() ||
        m_stationDocument->isEmpty()) {
        return false;
    }
    if (m_stepPropertyEditor && m_stepPropertyEditor->hasPendingChanges() &&
        !m_stepPropertyEditor->commitPendingChanges()) {
        return false;
    }
    if (!commitPendingStationChanges()) {
        return false;
    }

    const auto rootPath = newProjectRootPath();
    if (!QDir(rootPath).exists() && !QDir().mkpath(rootPath)) {
        QMessageBox::critical(
            this, tr("Save New Project As"),
            tr("Cannot create the projects directory: %1").arg(rootPath));
        return false;
    }

    QString suggestedName = QStringLiteral("NewProject");
    for (int suffix = 2;
         QFileInfo::exists(QDir(rootPath).filePath(suggestedName));
         ++suffix) {
        suggestedName = QStringLiteral("NewProject%1").arg(suffix);
    }

    QString projectName;
    QString projectPath;
    while (projectName.isEmpty()) {
        bool accepted = false;
        const auto candidate = QInputDialog::getText(
            this,
            tr("Save New Project As"),
            tr("Project name (saved under %1):").arg(rootPath),
            QLineEdit::Normal,
            suggestedName,
            &accepted).trimmed();
        if (!accepted) {
            return false;
        }

        static const QRegularExpression invalidCharacters(
            QStringLiteral(R"([<>:"/\\|?*\x00-\x1F])"));
        static const QSet<QString> reservedNames = {
            QStringLiteral("CON"), QStringLiteral("PRN"),
            QStringLiteral("AUX"), QStringLiteral("NUL"),
            QStringLiteral("COM1"), QStringLiteral("COM2"),
            QStringLiteral("COM3"), QStringLiteral("COM4"),
            QStringLiteral("COM5"), QStringLiteral("COM6"),
            QStringLiteral("COM7"), QStringLiteral("COM8"),
            QStringLiteral("COM9"), QStringLiteral("LPT1"),
            QStringLiteral("LPT2"), QStringLiteral("LPT3"),
            QStringLiteral("LPT4"), QStringLiteral("LPT5"),
            QStringLiteral("LPT6"), QStringLiteral("LPT7"),
            QStringLiteral("LPT8"), QStringLiteral("LPT9")};
        const auto reservedToken = candidate.section(QLatin1Char('.'), 0, 0)
                                       .toUpper();
        const bool invalid = candidate.isEmpty() ||
            candidate == QStringLiteral(".") ||
            candidate == QStringLiteral("..") ||
            candidate.endsWith(QLatin1Char('.')) ||
            candidate.endsWith(QLatin1Char(' ')) ||
            invalidCharacters.match(candidate).hasMatch() ||
            reservedNames.contains(reservedToken);
        if (invalid) {
            QMessageBox::warning(
                this, tr("Invalid Project Name"),
                tr("Use a normal folder name without reserved names or these characters: < > : \" / \\ | ? *"));
            suggestedName = candidate;
            continue;
        }

        const auto candidatePath = QDir(rootPath).filePath(candidate);
        QDir candidateDirectory(candidatePath);
        if (candidateDirectory.exists() &&
            !candidateDirectory.entryList(
                QDir::AllEntries | QDir::Hidden | QDir::System |
                    QDir::NoDotAndDotDot).isEmpty()) {
            QMessageBox::warning(
                this, tr("Project Already Exists"),
                tr("The project folder is not empty: %1\nChoose another project name.")
                    .arg(candidatePath));
            suggestedName = candidate;
            continue;
        }
        if (!candidateDirectory.exists() && !QDir().mkpath(candidatePath)) {
            QMessageBox::critical(
                this, tr("Save New Project As"),
                tr("Cannot create the project folder: %1").arg(candidatePath));
            return false;
        }
        projectName = candidate;
        projectPath = QFileInfo(candidatePath).absoluteFilePath();
    }

    auto sequenceRoot = m_sequenceDocument->rootObject();
    auto stationRoot = m_stationDocument->rootObject();
    const auto identifier = projectIdentifier(projectName);
    const auto replaceNa = [](QJsonObject& object,
                              const QString& key,
                              const QString& replacement) {
        const auto current = object.value(key).toString().trimmed();
        if (current.isEmpty() ||
            current.compare(QStringLiteral("NA"), Qt::CaseInsensitive) == 0) {
            object.insert(key, replacement);
        }
    };
    replaceNa(sequenceRoot, QStringLiteral("id"),
              identifier + QStringLiteral("-sequence"));
    replaceNa(sequenceRoot, QStringLiteral("name"), projectName);
    replaceNa(stationRoot, QStringLiteral("stationId"),
              identifier + QStringLiteral("-station"));
    replaceNa(stationRoot, QStringLiteral("name"), projectName);

    const auto sequencePath = QDir(projectPath).filePath(
        QStringLiteral("sequence.json"));
    const auto stationPath = QDir(projectPath).filePath(
        QStringLiteral("StationSystem.json"));
    QString errorMessage;
    if (!writeJsonObjectFile(sequencePath, sequenceRoot, &errorMessage) ||
        !writeJsonObjectFile(stationPath, stationRoot, &errorMessage)) {
        QFile::remove(sequencePath);
        QFile::remove(stationPath);
        QMessageBox::critical(
            this, tr("Save New Project As"),
            tr("Failed to create the project files: %1").arg(errorMessage));
        return false;
    }
    const auto imagesPath = QDir(projectPath).filePath(QStringLiteral("images"));
    if (!QDir().mkpath(imagesPath)) {
        QFile::remove(sequencePath);
        QFile::remove(stationPath);
        QMessageBox::critical(
            this, tr("Save New Project As"),
            tr("Cannot create the project images folder: %1").arg(imagesPath));
        return false;
    }
    const auto registerPath = QDir(projectPath).filePath(RegisterDirectoryName);
    if (!QDir().mkpath(registerPath)) {
        QFile::remove(sequencePath);
        QFile::remove(stationPath);
        QMessageBox::critical(
            this, tr("Save New Project As"),
            tr("Cannot create the project register folder: %1")
                .arg(registerPath));
        return false;
    }
    if (!m_sequenceDocument->load(sequencePath) ||
        !m_stationDocument->load(stationPath)) {
        QMessageBox::critical(
            this, tr("Save New Project As"),
            tr("The project files were written but could not be reloaded."));
        return false;
    }

    m_newProjectTemplate = false;
    m_newProjectRootPath = QFileInfo(rootPath).absoluteFilePath();
    m_selectedSequencePath = {};
    m_selectedSequenceNodePath.clear();
    m_selectedStationDeviceRow = -1;
    addRecentSequence(sequencePath);
    addRecentStation(stationPath);
    synchronizeSequenceSnapshot();
    synchronizeStationSnapshot();
    updateSequenceEditor();
    updateStationEditor();
    updateAdminStationSummary();
    if (m_adminSequenceLabel) {
        m_adminSequenceLabel->setText(QFileInfo(sequencePath).fileName());
        m_adminSequenceLabel->setToolTip(sequencePath);
    }
    ApplicationDiagnostics::recordAction(
        QStringLiteral("NEW_PROJECT_SAVED"), projectPath);
    statusBar()->showMessage(
        tr("Project '%1' saved").arg(projectName), 5000);
    updateWindowTitle();
    updateCommandState();
    return true;
}

bool MainWindow::saveSequenceAs()
{
    if (!m_sequenceDocument || m_sequenceDocument->isEmpty()) {
        return false;
    }
    if (m_newProjectTemplate) {
        return saveNewProjectAs();
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
    if (m_newProjectTemplate) {
        const bool hasChanges =
            (m_sequenceDocument && m_sequenceDocument->isModified()) ||
            (m_stationDocument && m_stationDocument->isModified()) ||
            (m_stepPropertyEditor &&
             m_stepPropertyEditor->hasPendingChanges()) ||
            (m_stationPropertyEditor &&
             m_stationPropertyEditor->hasPendingChanges()) ||
            (m_stationSettingsEditor &&
             m_stationSettingsEditor->hasPendingChanges());
        if (!hasChanges) {
            statusBar()->showMessage(tr("No project changes to save"), 3000);
            return;
        }
        saveNewProjectAs();
        return;
    }
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

bool MainWindow::isRunWorkspaceActive() const
{
    return m_workspaceTabs && m_runTestPage &&
           m_workspaceTabs->currentWidget() == m_runTestPage;
}

bool MainWindow::isRunDetailVisible() const
{
    return isRunWorkspaceActive() && m_adminRunStack && m_adminRunDetailPage &&
           m_adminRunStack->currentWidget() == m_adminRunDetailPage;
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
    if (m_newProjectTemplate) {
        return saveNewProjectAs();
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

void MainWindow::importRegisterConfiguration()
{
    if (!m_sequenceDocument || m_sequenceDocument->isEmpty() ||
        m_registerImportProcess) {
        return;
    }
    if (!resolvePendingStepChanges()) {
        return;
    }

    const auto sequencePath = m_sequenceDocument->filePath().trimmed();
    if (sequencePath.isEmpty()) {
        QMessageBox::information(
            this,
            tr("Save Project First"),
            tr("Save the new project before importing its Register workbook."));
        return;
    }

    QDir projectDirectory = QFileInfo(sequencePath).absoluteDir();
    QString registerDirectory;
    for (int depth = 0; depth < 3 && registerDirectory.isEmpty(); ++depth) {
        const auto children = projectDirectory.entryInfoList(
            QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
        for (const auto& child : children) {
            if (child.fileName().compare(RegisterDirectoryName,
                                         Qt::CaseInsensitive) == 0) {
                registerDirectory = child.absoluteFilePath();
                break;
            }
        }
        if (!projectDirectory.cdUp()) {
            break;
        }
    }
    if (registerDirectory.isEmpty()) {
        QMessageBox::warning(
            this,
            tr("Register Workbook Not Found"),
            tr("Create a lowercase register folder beside the project files and place an .xlsx or .xlsm workbook in it."));
        return;
    }

    QDir workbookDirectory(registerDirectory);
    auto workbooks = workbookDirectory.entryInfoList(
        {QStringLiteral("*.xlsx"), QStringLiteral("*.xlsm")},
        QDir::Files | QDir::Readable,
        QDir::Name | QDir::IgnoreCase);
    workbooks.erase(
        std::remove_if(workbooks.begin(), workbooks.end(),
                       [](const QFileInfo& workbook) {
                           return workbook.fileName().startsWith(
                               QStringLiteral("~$"));
                       }),
        workbooks.end());
    if (workbooks.isEmpty()) {
        QMessageBox::warning(
            this,
            tr("Register Workbook Not Found"),
            tr("No .xlsx or .xlsm workbook was found in:\n%1")
                .arg(QDir::toNativeSeparators(registerDirectory)));
        return;
    }

    QSettings registerImportSettings;
    const auto rememberedWorkbookPath = registerImportSettings.value(
        QStringLiteral("RegisterImport/LastWorkbookPath")).toString();
    RegisterWorkbookDialog workbookDialog(
        workbooks, rememberedWorkbookPath, this);
    if (workbookDialog.exec() != QDialog::Accepted) {
        return;
    }
    const auto workbookPath = workbookDialog.selectedWorkbookPath();
    if (workbookPath.isEmpty()) {
        return;
    }
    registerImportSettings.setValue(
        QStringLiteral("RegisterImport/LastWorkbookPath"), workbookPath);

    QStringList modbusDeviceIds;
    if (m_stationDocument && !m_stationDocument->isEmpty()) {
        const auto devices = m_stationDocument->rootObject()
                                 .value(QStringLiteral("devices")).toArray();
        for (const auto& value : devices) {
            const auto device = value.toObject();
            if (!device.value(QStringLiteral("enabled")).toBool(true) ||
                device.value(QStringLiteral("type")).toString()
                        .compare(QStringLiteral("MODBUS"), Qt::CaseInsensitive) != 0) {
                continue;
            }
            const auto id = stationDeviceId(device);
            if (!id.isEmpty() && !modbusDeviceIds.contains(id)) {
                modbusDeviceIds.push_back(id);
            }
        }
    }
    QString deviceId = QStringLiteral("MODBUS1");
    if (modbusDeviceIds.size() == 1) {
        deviceId = modbusDeviceIds.first();
    } else if (modbusDeviceIds.size() > 1) {
        bool accepted = false;
        deviceId = QInputDialog::getItem(
            this,
            tr("Register Target Device"),
            tr("Modbus device"),
            modbusDeviceIds,
            0,
            false,
            &accepted);
        if (!accepted || deviceId.isEmpty()) {
            return;
        }
    }

    const auto importerPath = registerImporterExecutablePath();
    if (importerPath.isEmpty()) {
        QMessageBox::critical(
            this,
            tr("Register Importer Missing"),
            tr("PicoATE.RegisterImporter.exe is not available beside PicoATE.UI.exe."));
        return;
    }

    auto* process = new QProcess(this);
    m_registerImportProcess = process;
    process->setProgram(importerPath);
    process->setArguments({QStringLiteral("--workbook"),
                           workbookPath,
                           QStringLiteral("--device-id"),
                           deviceId});
    process->setProcessChannelMode(QProcess::SeparateChannels);
    process->setProperty("sequencePath", sequencePath);
    process->setProperty("sequenceRevision",
                         QVariant::fromValue(m_sequenceDocument->revision()));

    const auto finish = [this, process](bool failedToStart) {
        if (m_registerImportProcess != process) {
            return;
        }
        m_registerImportProcess = nullptr;

        const auto standardOutput = process->readAllStandardOutput();
        const auto standardError = QString::fromUtf8(
            process->readAllStandardError()).trimmed();
        const auto processError = process->errorString();
        const auto capturedPath = process->property("sequencePath").toString();
        const auto capturedRevision =
            process->property("sequenceRevision").toULongLong();
        process->deleteLater();

        if (failedToStart) {
            QMessageBox::critical(
                this,
                tr("Register Import Failed"),
                tr("Unable to start the register importer: %1").arg(processError));
            updateCommandState();
            return;
        }

        QJsonParseError parseError;
        const auto responseDocument =
            QJsonDocument::fromJson(standardOutput, &parseError);
        if (parseError.error != QJsonParseError::NoError ||
            !responseDocument.isObject()) {
            QMessageBox::critical(
                this,
                tr("Register Import Failed"),
                tr("The register importer returned an invalid response.%1")
                    .arg(standardError.isEmpty()
                             ? QString{}
                             : QStringLiteral("\n\n") + standardError));
            updateCommandState();
            return;
        }

        const auto response = responseDocument.object();
        if (!response.value(QStringLiteral("ok")).toBool()) {
            QStringList messages;
            for (const auto& value :
                 response.value(QStringLiteral("errors")).toArray()) {
                const auto diagnostic = value.toObject();
                const auto path = diagnostic.value(QStringLiteral("path")).toString();
                const auto message =
                    diagnostic.value(QStringLiteral("message")).toString();
                messages.push_back(path.isEmpty()
                                       ? message
                                       : QStringLiteral("%1: %2").arg(path, message));
            }
            if (!standardError.isEmpty()) {
                messages.push_back(standardError);
            }
            QMessageBox::warning(
                this,
                tr("Register Import Failed"),
                messages.isEmpty()
                    ? tr("The register table could not be converted.")
                    : messages.join(QLatin1Char('\n')));
            updateCommandState();
            return;
        }

        if (!m_sequenceDocument ||
            QFileInfo(m_sequenceDocument->filePath()).absoluteFilePath() !=
                QFileInfo(capturedPath).absoluteFilePath() ||
            m_sequenceDocument->revision() != capturedRevision) {
            QMessageBox::information(
                this,
                tr("Flow Changed"),
                tr("The Flow changed while the workbook was being converted. Import again to avoid overwriting newer edits."));
            updateCommandState();
            return;
        }

        SequenceItemPath insertedPath;
        QString errorMessage;
        if (!m_sequenceDocument->replaceMainTopLevelStepById(
                QStringLiteral("register_config"),
                response.value(QStringLiteral("testItem")).toObject(),
                &insertedPath,
                &errorMessage)) {
            QMessageBox::warning(
                this,
                tr("Register Import Failed"),
                errorMessage.isEmpty()
                    ? tr("The generated TestItem could not be inserted into Main.")
                    : errorMessage);
            updateCommandState();
            return;
        }

        m_selectedSequencePath = insertedPath;
        m_selectedSequenceNodePath.clear();
        QTimer::singleShot(0, this, [this, insertedPath] {
            if (!m_sequenceTreeView || !m_sequenceTreeModel) {
                return;
            }
            const auto index = m_sequenceTreeModel->indexForPath(insertedPath);
            if (!index.isValid()) {
                return;
            }
            m_sequenceTreeView->setCurrentIndex(index);
            m_sequenceTreeView->expand(index);
            m_sequenceTreeView->scrollTo(index,
                                         QAbstractItemView::PositionAtTop);
        });

        QStringList warningMessages;
        for (const auto& value :
             response.value(QStringLiteral("warnings")).toArray()) {
            warningMessages.push_back(
                value.toObject().value(QStringLiteral("message")).toString());
        }
        const auto summary =
            tr("Imported %1 register parameter(s) from %2. Press Ctrl+S to save.")
                .arg(response.value(QStringLiteral("parameterCount")).toInt())
                .arg(QFileInfo(response.value(QStringLiteral("workbook")).toString())
                         .fileName());
        statusBar()->showMessage(summary, 12000);
        if (!warningMessages.isEmpty()) {
            QMessageBox::information(
                this,
                tr("Register Import Warnings"),
                summary + QStringLiteral("\n\n") +
                    warningMessages.join(QLatin1Char('\n')));
        }
        updateCommandState();
    };

    connect(process, &QProcess::finished, this,
            [finish](int, QProcess::ExitStatus) { finish(false); });
    connect(process, &QProcess::errorOccurred, this,
            [finish](QProcess::ProcessError error) {
                if (error == QProcess::FailedToStart) {
                    finish(true);
                }
            });
    if (m_importRegisterConfigAction) {
        m_importRegisterConfigAction->setEnabled(false);
    }
    statusBar()->showMessage(tr("Importing register workbook..."));
    process->start();
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
    const auto paths = selectedSequenceStepPaths();
    if (paths.isEmpty()) {
        return;
    }

    if (m_stepPropertyEditor && m_stepPropertyEditor->hasPendingChanges()) {
        const auto draftPath = m_stepPropertyEditor->currentPath();
        const bool removesDraft = std::any_of(
            paths.cbegin(), paths.cend(), [&draftPath](const auto& path) {
                return containsSequencePath(path, draftPath);
            });
        if (removesDraft) {
            // A discarded item must not require a valid draft. Reloading here
            // also prevents selection-change callbacks from trying to commit it.
            m_stepPropertyEditor->discardPendingChanges();
        } else if (!resolvePendingStepChanges()) {
            return;
        }
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

void MainWindow::expandSequencePhases()
{
    if (!m_sequenceTreeView || !m_sequenceTreeModel) {
        return;
    }

    auto* scrollBar = m_sequenceTreeView->verticalScrollBar();
    const int scrollValue = scrollBar->value();
    const std::function<void(const QModelIndex&)> expandSubtree =
        [this, &expandSubtree](const QModelIndex& parent) {
            m_sequenceTreeView->setExpanded(parent, true);
            const int rows = m_sequenceTreeModel->rowCount(parent);
            for (int row = 0; row < rows; ++row) {
                expandSubtree(m_sequenceTreeModel->index(
                    row, SequenceTreeModel::NameColumn, parent));
            }
        };

    for (int row = 0; row < m_sequenceTreeModel->rowCount(); ++row) {
        const auto phase = m_sequenceTreeModel->index(
            row, SequenceTreeModel::NameColumn);
        const auto kind = phase.siblingAtColumn(SequenceTreeModel::KindColumn)
                              .data().toString().trimmed().toLower();
        if (kind == QStringLiteral("setup") ||
            kind == QStringLiteral("main") ||
            kind == QStringLiteral("cleanup")) {
            expandSubtree(phase);
        }
    }

    scrollBar->setValue(qBound(scrollBar->minimum(), scrollValue,
                               scrollBar->maximum()));
}

void MainWindow::collapseSequencePhasesToFirstLevel()
{
    if (!m_sequenceTreeView || !m_sequenceTreeModel) {
        return;
    }

    auto* scrollBar = m_sequenceTreeView->verticalScrollBar();
    const int scrollValue = scrollBar->value();
    const std::function<void(const QModelIndex&)> collapseSubtree =
        [this, &collapseSubtree](const QModelIndex& parent) {
            const int rows = m_sequenceTreeModel->rowCount(parent);
            for (int row = 0; row < rows; ++row) {
                collapseSubtree(m_sequenceTreeModel->index(
                    row, SequenceTreeModel::NameColumn, parent));
            }
            m_sequenceTreeView->setExpanded(parent, false);
        };

    for (int row = 0; row < m_sequenceTreeModel->rowCount(); ++row) {
        const auto phase = m_sequenceTreeModel->index(
            row, SequenceTreeModel::NameColumn);
        const auto kind = phase.siblingAtColumn(SequenceTreeModel::KindColumn)
                              .data().toString().trimmed().toLower();
        if (kind != QStringLiteral("setup") &&
            kind != QStringLiteral("main") &&
            kind != QStringLiteral("cleanup")) {
            continue;
        }

        const int children = m_sequenceTreeModel->rowCount(phase);
        for (int childRow = 0; childRow < children; ++childRow) {
            collapseSubtree(m_sequenceTreeModel->index(
                childRow, SequenceTreeModel::NameColumn, phase));
        }
        m_sequenceTreeView->setExpanded(phase, true);
    }

    scrollBar->setValue(qBound(scrollBar->minimum(), scrollValue,
                               scrollBar->maximum()));
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
    startAdminRunWithSerial({});
}

void MainWindow::runScannedUut(const QString& serialNumber)
{
    runScannedUuts({serialNumber});
}

void MainWindow::runScannedUuts(const QStringList& serialNumbers)
{
    QStringList sns;
    sns.reserve(serialNumbers.size());
    for (const auto& serialNumber : serialNumbers) {
        const auto sn = serialNumber.trimmed();
        if (!sn.isEmpty()) {
            sns.push_back(sn);
        }
    }
    if (sns.isEmpty()) {
        return;
    }
    if (m_autoRouteBySn) {
        if (!m_viewModel || !m_viewModel->canChangeSources()) {
            showProductRoutingError(tr("A test is already running"));
            return;
        }

        const auto routing = PicoATE::Core::loadProductRoutingFile(
            m_productRoutingPath);
        if (!routing.ok()) {
            QStringList details;
            for (const auto& error : routing.errors) {
                details.push_back(error.path.isEmpty()
                    ? error.message
                    : QStringLiteral("%1: %2").arg(error.path, error.message));
            }
            showProductRoutingError(details.join(QStringLiteral("\n")));
            return;
        }
        const auto route = PicoATE::Core::resolveProductRoute(
            routing.config, sns.first());
        if (!route.ok()) {
            QStringList details;
            for (const auto& error : route.errors) {
                details.push_back(error.message);
            }
            showProductRoutingError(details.join(QStringLiteral("\n")));
            return;
        }
        for (int index = 1; index < sns.size(); ++index) {
            const auto candidate = PicoATE::Core::resolveProductRoute(
                routing.config, sns[index]);
            if (!candidate.ok()) {
                QStringList details;
                for (const auto& error : candidate.errors) {
                    details.push_back(error.message);
                }
                showProductRoutingError(
                    tr("UUT %1 (%2): %3")
                        .arg(index + 1)
                        .arg(sns[index], details.join(QStringLiteral("; "))));
                return;
            }
            if (candidate.sequencePath != route.sequencePath ||
                candidate.stationPath != route.stationPath) {
                showProductRoutingError(
                    tr("All UUTs in one batch must resolve to the same project. "
                       "UUT 1 and UUT %1 matched different projects.")
                        .arg(index + 1));
                return;
            }
        }

        const auto currentPath = m_sequenceDocument &&
                                 !m_sequenceDocument->filePath().isEmpty()
            ? QFileInfo(m_sequenceDocument->filePath()).absoluteFilePath()
            : QString{};
        const auto currentStationPath = m_stationDocument &&
                                        !m_stationDocument->filePath().isEmpty()
            ? QFileInfo(m_stationDocument->filePath()).absoluteFilePath()
            : QString{};
        if (currentPath == route.sequencePath &&
            currentStationPath == route.stationPath &&
            m_viewModel->canRun()) {
            startAdminRunWithSerials(sns);
            return;
        }
        if (currentPath != route.sequencePath && !maybeSaveSequence()) {
            showStartupScanDialog();
            return;
        }
        if (currentStationPath != route.stationPath && !maybeSaveStation()) {
            showStartupScanDialog();
            return;
        }
        if (currentStationPath != route.stationPath &&
            !openStationFile(route.stationPath)) {
            showProductRoutingError(
                tr("Cannot open routed Station: %1").arg(route.stationPath));
            return;
        }
        if (currentPath != route.sequencePath) {
            if (!openSequenceFile(route.sequencePath)) {
                showProductRoutingError(
                    tr("Cannot open routed Sequence: %1").arg(route.sequencePath));
                return;
            }
        }
        m_pendingRoutedSerialNumbers = sns;
        statusBar()->showMessage(
            tr("%1 UUT SN(s) matched %2. Loading project %3...")
                .arg(sns.size())
                .arg(route.routeName, route.projectName));
        compileSequence();
        return;
    }

    startAdminRunWithSerials(sns);
}

void MainWindow::startAdminRunWithSerial(const QString& serialNumber)
{
    const auto sn = serialNumber.trimmed();
    if (!sn.isEmpty()) {
        startAdminRunWithSerials({sn});
        return;
    }
    if (!m_viewModel || !m_viewModel->canRun()) {
        statusBar()->showMessage(
            tr("Compile the sequence before starting a test"), 4000);
        return;
    }
    const int uutCount = m_uutCount
        ? qMax(1, m_uutCount->value())
        : 1;
    m_activeAdminSerialNumber.clear();
    m_activeAdminUutId = uutCount > 1
        ? QStringLiteral("UUT-1")
        : QStringLiteral("UUT-%1").arg(
              QDateTime::currentDateTime().toString(
                  QStringLiteral("yyyyMMdd-HHmmss-zzz")));
    m_viewModel->setBreakpoints(m_sequenceTreeModel->breakpointSpecs());
    m_sequenceTreeModel->setCurrentDebugNodePath({});
    m_adminSerialLabel->setText(tr("--"));
    ApplicationDiagnostics::recordAction(
        QStringLiteral("RUN_REQUESTED"),
        QStringLiteral("uutCount=%1; firstUut=%2")
            .arg(uutCount)
            .arg(m_activeAdminUutId));
    if (uutCount > 1) {
        m_viewModel->run(uutCount);
    } else {
        QVariantMap variables;
        variables.insert(QStringLiteral("sn"), QString{});
        variables.insert(QStringLiteral("serialNumber"), QString{});
        m_viewModel->runUut(m_activeAdminUutId, variables);
    }
    showRunPage();
}

void MainWindow::startAdminRunWithSerials(const QStringList& serialNumbers)
{
    if (!m_viewModel || !m_viewModel->canRun() || serialNumbers.isEmpty()) {
        statusBar()->showMessage(
            tr("Compile the sequence before starting a test"), 4000);
        return;
    }

    QVector<RunRequest::UutInput> inputs;
    inputs.reserve(serialNumbers.size());
    for (int index = 0; index < serialNumbers.size(); ++index) {
        const auto sn = serialNumbers[index].trimmed();
        RunRequest::UutInput input;
        input.uutId = serialNumbers.size() == 1
            ? sn
            : QStringLiteral("UUT-%1").arg(index + 1);
        input.variables.insert(QStringLiteral("sn"), sn);
        input.variables.insert(QStringLiteral("serialNumber"), sn);
        inputs.push_back(std::move(input));
    }

    m_activeAdminSerialNumber = serialNumbers.first().trimmed();
    m_activeAdminUutId = inputs.first().uutId;
    m_viewModel->setBreakpoints(m_sequenceTreeModel->breakpointSpecs());
    m_sequenceTreeModel->setCurrentDebugNodePath({});
    m_adminSerialLabel->setText(m_activeAdminSerialNumber);
    ApplicationDiagnostics::recordAction(
        QStringLiteral("RUN_REQUESTED"),
        QStringLiteral("uutCount=%1; firstUut=%2")
            .arg(inputs.size())
            .arg(m_activeAdminUutId));
    m_viewModel->runUuts(inputs);
    showRunPage();
}

void MainWindow::showProductRoutingError(const QString& message)
{
    const auto text = message.trimmed().isEmpty()
        ? tr("Product routing failed")
        : message.trimmed();
    statusBar()->showMessage(text.section(QLatin1Char('\n'), 0, 0), 10000);
    QTimer::singleShot(0, this, [this, text] {
        m_scanDialog->hide();
        QMessageBox::warning(this, tr("Product routing"), text);
        showStartupScanDialog();
    });
}

void MainWindow::beginAdminRunIteration(int iteration, int totalIterations)
{
    m_currentReportSaved = false;
    m_currentAdminRunCounted = false;
    m_adminTerminalNodes.clear();
    m_runtimeTimelineModel->clear();
    const auto artifactContext = runArtifactContextFromDocuments(
        m_sequenceDocument ? m_sequenceDocument->rootObject() : QJsonObject{},
        m_sequenceDocument ? m_sequenceDocument->filePath() : QString{},
        m_stationDocument ? m_stationDocument->rootObject() : QJsonObject{},
        m_stationDocument ? m_stationDocument->filePath() : QString{},
        m_activeAdminSerialNumber);
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
        runArtifactSettingsFromStation(
            m_stationDocument ? m_stationDocument->rootObject() : QJsonObject{},
            m_stationDocument ? m_stationDocument->filePath() : QString()),
        artifactContext,
        artifactUuts);
    if (!artifact.success) {
        statusBar()->showMessage(
            tr("Cannot create report files: %1").arg(artifact.errorMessage),
            10000);
    }
    auto preview = m_adminPreviewReport;
    preview.completed = false;
    preview.hasError = false;
    preview.state = PicoATE::Core::ExecutionState::Idle;
    const auto activeUuts = m_viewModel->activeRunUuts();
    const auto previewTemplate = preview.uuts.isEmpty()
        ? PicoATE::Core::UutReport{}
        : preview.uuts.first();
    preview.uuts.clear();
    for (const auto& input : activeUuts) {
        auto uut = previewTemplate;
        uut.uutId = input.uutId;
        uut.serialNumber = input.variables.value(
            QStringLiteral("serialNumber")).toString().trimmed();
        if (uut.serialNumber.isEmpty()) {
            uut.serialNumber = input.variables.value(
                QStringLiteral("sn")).toString().trimmed();
        }
        uut.completed = false;
        uut.hasError = false;
        uut.outcome = PicoATE::Core::NodeOutcome::Unknown;
        preview.uuts.push_back(std::move(uut));
    }
    m_uutOverviewModel->resetForRun(m_adminPreviewReport, activeUuts);
    m_selectedAdminUutId = activeUuts.isEmpty()
        ? PicoATE::Core::UutId{}
        : activeUuts.first().uutId;
    m_adminUutOverview->setSelectedUutId(m_selectedAdminUutId);
    m_uutStepModel->setVisibleUutId(m_selectedAdminUutId);
    m_runtimeTimelineProxy->setVisibleUutId(m_selectedAdminUutId);
    rebuildAdminUutButtons();
    displayReport(preview);
    m_resultView->clearSelection();
    m_resultView->setCurrentIndex({});
    m_attemptModel->setStep(std::nullopt);
    m_measurementModel->setMeasurements({});
    m_adminLastAutoFollowLine = 0;
    m_adminLastAutoFollowUutId.clear();
    m_adminLastAutoFollowNodeId.clear();
    updateAdminProgress();
    m_adminElapsed.restart();
    m_adminElapsedTimer->start();
    if (ShowAdminUutOverview && activeUuts.size() > 1) {
        showAdminUutOverview();
    } else {
        showAdminUutDetails(m_selectedAdminUutId);
    }
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
    const bool canScan = m_viewModel &&
        (m_autoRouteBySn ? m_viewModel->canChangeSources()
                         : m_viewModel->canRun());
    if (!canScan) {
        statusBar()->showMessage(
            m_autoRouteBySn
                ? tr("Wait for the current operation to finish before scanning")
                : tr("Compile the sequence before scanning"),
            4000);
        return;
    }
    SnValidationRules rules;
    if (!m_autoRouteBySn) {
        const auto station = m_stationDocument
            ? m_stationDocument->rootObject()
            : QJsonObject{};
        rules.exactLength = qBound(
            0, station.value(QStringLiteral("snLength")).toInt(0), 256);
        rules.wildcardPattern = station.value(QStringLiteral("snPattern"))
                                    .toString().trimmed();
        rules.allowedRegex = station.value(QStringLiteral("snAllowedRegex"))
                                 .toString().trimmed();
    }
    m_scanDialog->setValidationRules(std::move(rules));
    m_scanDialog->setSlotCount(m_uutCount
                                   ? qMax(1, m_uutCount->value())
                                   : 1);
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
        completeAdminWorkspaceInitialization();
        return;
    }

    if (PluginCatalog::discoverPluginFiles(pluginDirectory).isEmpty()) {
        loadPluginRegistry();
        if (interactive) {
            statusBar()->showMessage(
                tr("No PicoATE plugin DLL was found in the plugins directory"),
                7000);
        }
        completeAdminWorkspaceInitialization();
        return;
    }

    const QStringList nativeHostCandidates = {
        QDir(applicationDirectory).absoluteFilePath(
            QStringLiteral("PicoATE.NativeHost.exe")),
        QDir(applicationDirectory).absoluteFilePath(
            QStringLiteral("../../../src/nativehost/Debug/PicoATE.NativeHost.exe")),
    };
    if (firstExistingPath(nativeHostCandidates).isEmpty()) {
        // Keep the last valid registry usable even when the scanner host is
        // temporarily unavailable. Startup no longer preloads it in the
        // MainWindow constructor.
        loadPluginRegistry();
        const auto message = tr("No compatible PicoATE.NativeHost.exe was found. Rebuild or redeploy the UI so the Host supports --describe. Plugin DLLs are never loaded directly in the UI process.");
        if (interactive) {
            QMessageBox::critical(this, tr("Scan Plugins"), message);
        } else {
            statusBar()->showMessage(message, 7000);
        }
        completeAdminWorkspaceInitialization();
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
                    completeAdminWorkspaceInitialization();
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
                completeAdminWorkspaceInitialization();
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

void MainWindow::completeAdminWorkspaceInitialization()
{
    hideStartupOverlay();
    if (!m_adminWorkspaceInitializing) {
        return;
    }
    m_adminWorkspaceInitializing = false;
    emit adminWorkspaceReady();
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
    QElapsedTimer loadTimer;
    loadTimer.start();
    qint64 previousStageMs = 0;
    const auto recordStage = [&loadTimer, &previousStageMs](const char* stage) {
        const auto elapsed = loadTimer.elapsed();
        ApplicationDiagnostics::recordAction(
            QStringLiteral("ADMIN_PLUGIN_REGISTRY_STAGE"),
            QStringLiteral("%1=%2ms,total=%3ms")
                .arg(QString::fromLatin1(stage))
                .arg(elapsed - previousStageMs)
                .arg(elapsed));
        previousStageMs = elapsed;
    };
    const auto registryPath = QDir(QCoreApplication::applicationDirPath())
        .absoluteFilePath(QStringLiteral("plugins/PluginRegistry.json"));
    if (!QFileInfo::exists(registryPath)) {
        m_pluginFunctionModel->setPlugins({});
        serviceAdminStartupAnimation();
        m_stepPropertyEditor->setPluginRegistry({});
        serviceAdminStartupAnimation();
        if (m_stationPropertyEditor) {
            m_stationPropertyEditor->setPluginRegistry({});
        }
        serviceAdminStartupAnimation();
        if (m_stationDeviceModel) {
            m_stationDeviceModel->setPluginRegistry({});
        }
        serviceAdminStartupAnimation();
        updatePluginDeviceBindings();
        updateStationEditor();
        recordStage("empty");
        return;
    }
    const auto registry = PluginCatalog::loadRegistry(registryPath);
    recordStage("parse");
    m_pluginFunctionModel->setPlugins(registry.plugins);
    serviceAdminStartupAnimation();
    recordStage("function-model");
    m_stepPropertyEditor->setPluginRegistry(registry.plugins);
    serviceAdminStartupAnimation();
    recordStage("step-editor");
    if (m_stationPropertyEditor) {
        m_stationPropertyEditor->setPluginRegistry(registry.plugins);
    }
    serviceAdminStartupAnimation();
    recordStage("station-editor");
    if (m_stationDeviceModel) {
        m_stationDeviceModel->setPluginRegistry(registry.plugins);
    }
    serviceAdminStartupAnimation();
    recordStage("station-model");
    updatePluginDeviceBindings();
    serviceAdminStartupAnimation();
    updateStationEditor();
    serviceAdminStartupAnimation();
    recordStage("bindings");
    if (!registry.ok()) {
        statusBar()->showMessage(
            tr("Plugin registry contains %1 error(s)").arg(registry.errors.size()),
            7000);
    }
    if (m_pluginFunctionView) {
        m_pluginFunctionView->expandAll();
        m_pluginFunctionView->resizeColumnToContents(0);
    }
    serviceAdminStartupAnimation();
    recordStage("view");
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
#if defined(PICOATE_UI_TEST_PROJECT_DIR)
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
    if (m_newProjectTemplate) {
        title += QStringLiteral(" - ") + tr("New Project Template");
        if ((m_sequenceDocument && m_sequenceDocument->isModified()) ||
            (m_stationDocument && m_stationDocument->isModified()) ||
            (m_stepPropertyEditor &&
             m_stepPropertyEditor->hasPendingChanges()) ||
            (m_stationPropertyEditor &&
             m_stationPropertyEditor->hasPendingChanges()) ||
            (m_stationSettingsEditor &&
             m_stationSettingsEditor->hasPendingChanges())) {
            title += QLatin1Char('*');
        }
        setWindowTitle(title);
        return;
    }
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
    mainToolbar->setFloatable(false);
    mainToolbar->setIconSize(QSize(20, 20));
    mainToolbar->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);

    m_newProjectAction = new QAction(tr("New Project..."), this);
    m_newProjectAction->setObjectName(QStringLiteral("newProjectAction"));
    m_newProjectAction->setShortcut(QKeySequence::New);
    m_newProjectAction->setToolTip(
        tr("Create an empty Sequence and Station project"));
    connect(m_newProjectAction, &QAction::triggered,
            this, &MainWindow::createNewProject);

    m_openSequenceAction = new QAction(
        toolbarIcon("folder-open"),
        tr("Open Sequence"),
        this);
    m_openSequenceAction->setToolTip(tr("Open sequence JSON"));
    connect(m_openSequenceAction, &QAction::triggered, this, [this] { chooseSequence(); });
    m_recentSequenceMenu = new QMenu(tr("Open Recent Sequence"), fileMenu);
    m_recentSequenceMenu->setObjectName(QStringLiteral("recentSequenceMenu"));

    m_saveSequenceAction = new QAction(
        toolbarIcon("save"), tr("Save Sequence"), this);
    m_saveSequenceAction->setObjectName(QStringLiteral("saveSequenceAction"));
    m_saveSequenceAction->setShortcut(QKeySequence::Save);
    m_saveSequenceAction->setToolTip(tr("Save sequence JSON"));
    connect(m_saveSequenceAction, &QAction::triggered,
            this, &MainWindow::saveActiveDocument);

    m_saveSequenceAsAction = new QAction(tr("Save Sequence As..."), this);
    m_saveSequenceAsAction->setShortcut(QKeySequence::SaveAs);
    connect(m_saveSequenceAsAction, &QAction::triggered, this, [this] { saveSequenceAs(); });

    m_undoAction = new QAction(
        toolbarIcon("undo-2"), tr("Undo"), this);
    m_undoAction->setShortcut(QKeySequence::Undo);
    m_undoAction->setToolTip(tr("Undo last sequence edit"));
    connect(m_undoAction, &QAction::triggered,
            this, [this] { applyUndoRedo(false); });
    m_redoAction = new QAction(
        toolbarIcon("redo-2"), tr("Redo"), this);
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
        toolbarIcon("list-plus"), tr("Add Step"), this);
    m_addStepAction->setToolTip(tr("Add step after selection or inside a container"));
    connect(m_addStepAction, &QAction::triggered, this, [this] { addSequenceStep(); });

    m_deleteStepAction = new QAction(
        toolbarIcon("trash-2"), tr("Delete Step"), this);
    m_deleteStepAction->setObjectName(QStringLiteral("deleteStepAction"));
    m_deleteStepAction->setShortcut(QKeySequence::Delete);
    connect(m_deleteStepAction, &QAction::triggered, this, [this] { deleteSequenceStep(); });

    m_copyStepAction = new QAction(
        toolbarIcon("copy"),
        tr("Copy Selected"), this);
    m_copyStepAction->setObjectName(QStringLiteral("copyStepAction"));
    m_copyStepAction->setToolTip(
        tr("Copy selected Steps and TestItems (Ctrl+C)"));
    connect(m_copyStepAction, &QAction::triggered,
            this, &MainWindow::copySequenceSteps);

    m_pasteStepAction = new QAction(
        toolbarIcon("clipboard-paste"),
        tr("Paste"), this);
    m_pasteStepAction->setObjectName(QStringLiteral("pasteStepAction"));
    m_pasteStepAction->setToolTip(
        tr("Paste copied items after the current item (Ctrl+V)"));
    connect(m_pasteStepAction, &QAction::triggered,
            this, &MainWindow::pasteSequenceSteps);

    m_findFlowFieldAction = new QAction(tr("Find Flow Item"), this);
    m_findFlowFieldAction->setObjectName(QStringLiteral("findFlowFieldAction"));
    m_findFlowFieldAction->setShortcut(QKeySequence::Find);
    m_findFlowFieldAction->setShortcutContext(Qt::WidgetWithChildrenShortcut);
    m_findFlowFieldAction->setToolTip(
        tr("Find by Step name, ID, function, or device (Ctrl+F)"));
    connect(m_findFlowFieldAction, &QAction::triggered, this, [this] {
        if (!m_flowFieldSearch) return;
        m_flowFieldSearch->parentWidget()->show();
        m_flowFieldSearch->show();
        m_flowFieldSearch->setFocus();
        m_flowFieldSearch->selectAll();
    });

    m_sequenceVariablesAction = new QAction(
        toolbarIcon("variable"),
        tr("Variables"),
        this);
    m_sequenceVariablesAction->setObjectName(
        QStringLiteral("sequenceVariablesAction"));
    m_sequenceVariablesAction->setToolTip(
        tr("Edit shared and per-UUT sequence variables"));
    connect(m_sequenceVariablesAction, &QAction::triggered,
            this, &MainWindow::editSequenceVariables);

    m_importRegisterConfigAction = new QAction(
        toolbarIcon("file-spreadsheet"),
        tr("Import Register Table"),
        this);
    m_importRegisterConfigAction->setObjectName(
        QStringLiteral("importRegisterConfigAction"));
    m_importRegisterConfigAction->setToolTip(
        tr("Choose a project Register workbook and replace MAIN/register_config"));
    m_importRegisterConfigAction->setVisible(
        !registerImporterExecutablePath().isEmpty());
    connect(m_importRegisterConfigAction, &QAction::triggered,
            this, &MainWindow::importRegisterConfiguration);

    m_wrapTestItemAction = new QAction(
        toolbarIcon("combine"), tr("Wrap in TestItem"), this);
    m_wrapTestItemAction->setObjectName(QStringLiteral("wrapTestItemAction"));
    m_wrapTestItemAction->setToolTip(
        tr("Wrap selected contiguous steps in a TestItem"));
    connect(m_wrapTestItemAction, &QAction::triggered,
            this, [this] { wrapSelectedStepsInTestItem(); });

    m_enableStepsAction = new QAction(
        toolbarIcon("circle-check-big"), tr("Enable Selected"), this);
    m_enableStepsAction->setObjectName(QStringLiteral("enableStepsAction"));
    m_enableStepsAction->setToolTip(tr("Enable all selected steps"));
    connect(m_enableStepsAction, &QAction::triggered,
            this, [this] { setSelectedSequenceStepsEnabled(true); });

    m_disableStepsAction = new QAction(
        toolbarIcon("circle-x"), tr("Disable Selected"), this);
    m_disableStepsAction->setObjectName(QStringLiteral("disableStepsAction"));
    m_disableStepsAction->setToolTip(tr("Disable all selected steps"));
    connect(m_disableStepsAction, &QAction::triggered,
            this, [this] { setSelectedSequenceStepsEnabled(false); });

    m_moveStepUpAction = new QAction(
        toolbarIcon("arrow-up"), tr("Move Up"), this);
    connect(m_moveStepUpAction, &QAction::triggered, this, [this] { moveSequenceStep(-1); });

    m_moveStepDownAction = new QAction(
        toolbarIcon("arrow-down"), tr("Move Down"), this);
    connect(m_moveStepDownAction, &QAction::triggered, this, [this] { moveSequenceStep(1); });

    m_expandSequencePhasesAction = new QAction(
        toolbarIcon("chevrons-up-down"), tr("Expand Flow"), this);
    m_expandSequencePhasesAction->setObjectName(
        QStringLiteral("expandSequencePhasesAction"));
    m_expandSequencePhasesAction->setToolTip(
        tr("Expand all items under Setup, Main, and Cleanup"));
    connect(m_expandSequencePhasesAction, &QAction::triggered,
            this, &MainWindow::expandSequencePhases);

    m_collapseSequencePhasesAction = new QAction(
        toolbarIcon("chevrons-down-up"), tr("Collapse Flow"), this);
    m_collapseSequencePhasesAction->setObjectName(
        QStringLiteral("collapseSequencePhasesAction"));
    m_collapseSequencePhasesAction->setToolTip(
        tr("Show only the first level under Setup, Main, and Cleanup"));
    connect(m_collapseSequencePhasesAction, &QAction::triggered,
            this, &MainWindow::collapseSequencePhasesToFirstLevel);

    m_openStationAction = new QAction(
        toolbarIcon("folder-cog"),
        tr("Open Station"),
        this);
    m_openStationAction->setToolTip(tr("Open station JSON"));
    connect(m_openStationAction, &QAction::triggered, this, [this] { chooseStation(); });
    m_recentStationMenu = new QMenu(tr("Open Recent Station"), fileMenu);
    m_recentStationMenu->setObjectName(QStringLiteral("recentStationMenu"));

    m_saveStationAction = new QAction(
        toolbarIcon("save"), tr("Save Station"), this);
    m_saveStationAction->setToolTip(tr("Save station JSON"));
    connect(m_saveStationAction, &QAction::triggered,
            this, [this] {
                if (m_newProjectTemplate) {
                    saveActiveDocument();
                } else {
                    confirmAndSaveStation();
                }
            });
    m_saveStationAsAction = new QAction(tr("Save Station As..."), this);
    connect(m_saveStationAsAction, &QAction::triggered,
            this, [this] { saveStationAs(); });

    m_stationUndoAction = new QAction(
        toolbarIcon("undo-2"), tr("Undo"), this);
    m_stationUndoAction->setToolTip(tr("Undo last station edit"));
    connect(m_stationUndoAction, &QAction::triggered,
            this, [this] { applyStationUndoRedo(false); });
    m_stationRedoAction = new QAction(
        toolbarIcon("redo-2"), tr("Redo"), this);
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
        toolbarIcon("list-plus"), tr("Add Device"), this);
    connect(m_addDeviceAction, &QAction::triggered,
            this, [this] { addStationDevice(); });
    m_duplicateDeviceAction = new QAction(
        toolbarIcon("copy-plus"), tr("Duplicate Device"), this);
    connect(m_duplicateDeviceAction, &QAction::triggered,
            this, [this] { duplicateStationDevice(); });
    m_deleteDeviceAction = new QAction(
        toolbarIcon("trash-2"), tr("Delete Device"), this);
    m_deleteDeviceAction->setObjectName(QStringLiteral("deleteDeviceAction"));
    connect(m_deleteDeviceAction, &QAction::triggered,
            this, [this] { deleteStationDevice(); });
    m_fillPreviousDeviceSlotAction = new QAction(
        toolbarIcon("list-restart"),
        tr("Fill Previous Empty ID"),
        this);
    m_fillPreviousDeviceSlotAction->setObjectName(
        QStringLiteral("fillPreviousDeviceSlotAction"));
    m_fillPreviousDeviceSlotAction->setToolTip(
        tr("Move this configuration into the earliest empty logical ID of the same type"));
    connect(m_fillPreviousDeviceSlotAction, &QAction::triggered,
            this, [this] { fillPreviousStationDeviceSlot(); });
    m_moveDeviceUpAction = new QAction(
        toolbarIcon("arrow-up"), tr("Move Device Up"), this);
    connect(m_moveDeviceUpAction, &QAction::triggered,
            this, [this] { moveStationDevice(-1); });
    m_moveDeviceDownAction = new QAction(
        toolbarIcon("arrow-down"), tr("Move Device Down"), this);
    connect(m_moveDeviceDownAction, &QAction::triggered,
            this, [this] { moveStationDevice(1); });
    m_testDeviceConnectionAction = new QAction(
        toolbarIcon("plug-zap"),
        tr("Test Connection"),
        this);
    m_testDeviceConnectionAction->setObjectName(
        QStringLiteral("testDeviceConnectionAction"));
    m_testDeviceConnectionAction->setToolTip(tr("Open, health-check, and close selected device"));
    connect(m_testDeviceConnectionAction, &QAction::triggered,
            this, [this] { testSelectedStationDevice(); });

    m_compileAction = new QAction(
        toolbarIcon("refresh-cw"),
        tr("Compile"),
        this);
    m_compileAction->setObjectName(QStringLiteral("compileAction"));
    m_compileAction->setToolTip(tr("Compile selected sequence"));
    connect(m_compileAction, &QAction::triggered,
            this, &MainWindow::compileSequence);

    m_runAction = new QAction(
        toolbarIcon("play"),
        tr("Run"),
        this);
    m_runAction->setObjectName(QStringLiteral("runAction"));
    m_runAction->setToolTip(tr("Run compiled sequence"));
    connect(m_runAction,
            &QAction::triggered,
            this,
            &MainWindow::runSequence);

    m_pauseAction = new QAction(
        toolbarIcon("pause"),
        tr("Pause"),
        this);
    m_pauseAction->setToolTip(tr("Pause after the running step completes"));
    connect(m_pauseAction, &QAction::triggered, m_viewModel, &ExecutionViewModel::pause);

    m_resumeAction = new QAction(
        toolbarIcon("play"),
        tr("Resume"),
        this);
    m_resumeAction->setToolTip(tr("Resume the paused execution"));
    connect(m_resumeAction, &QAction::triggered, m_viewModel, &ExecutionViewModel::resume);

    m_stepIntoAction = new QAction(
        toolbarIcon("corner-right-down"),
        tr("Step Into"),
        this);
    m_stepIntoAction->setObjectName(QStringLiteral("stepIntoAction"));
    m_stepIntoAction->setToolTip(tr("Run one scheduler step and pause again"));
    connect(m_stepIntoAction, &QAction::triggered, m_viewModel, &ExecutionViewModel::stepInto);

    m_stepOverAction = new QAction(
        toolbarIcon("step-forward"),
        tr("Step Over"),
        this);
    m_stepOverAction->setToolTip(tr("Run current step or structural block and pause again"));
    connect(m_stepOverAction, &QAction::triggered, m_viewModel, &ExecutionViewModel::stepOver);

    m_stopAction = new QAction(
        toolbarIcon("square"),
        tr("Stop"),
        this);
    m_stopAction->setToolTip(tr("Request graceful stop"));
    connect(m_stopAction, &QAction::triggered, this, [this] { m_viewModel->stop(); });

    m_scanAction = new QAction(
        toolbarIcon("scan-barcode"),
        tr("Scan SN"),
        this);
    m_scanAction->setObjectName(QStringLiteral("adminScanAction"));
    m_scanAction->setToolTip(tr("Open or cancel the barcode dialog"));
    connect(m_scanAction, &QAction::triggered, this, &MainWindow::toggleScanDialog);

    m_productRoutingAction = new QAction(
        toolbarIcon("list-restart"),
        tr("Product Routing"),
        this);
    m_productRoutingAction->setObjectName(
        QStringLiteral("adminProductRoutingAction"));
    m_productRoutingAction->setToolTip(
        tr("Configure SN patterns and their test sequences"));
    connect(m_productRoutingAction, &QAction::triggered,
            this, &MainWindow::openProductRoutingConfiguration);

    m_scanPluginsAction = new QAction(
        toolbarIcon("package-search"),
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

    fileMenu->addAction(m_newProjectAction);
    fileMenu->addSeparator();
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
    editMenu->addAction(m_importRegisterConfigAction);
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
    toolsMenu->addAction(m_productRoutingAction);
    toolsMenu->addAction(m_scanPluginsAction);
    toolsMenu->addAction(m_importRegisterConfigAction);
    viewMenu->addAction(m_resetLayoutAction);

    mainToolbar->addAction(m_openSequenceAction);
    mainToolbar->addAction(m_openStationAction);
    mainToolbar->addSeparator();
    m_uutCount = new QSpinBox(mainToolbar);
    m_uutCount->setObjectName(QStringLiteral("uutCountSpinBox"));
    m_uutCount->setRange(1, 64);
    m_uutCount->setValue(1);
    m_uutCount->setPrefix(tr("UUTs "));
    m_uutCount->setAlignment(Qt::AlignCenter);
    m_uutCount->setFixedWidth(88);
    m_uutCount->setToolTip(tr("Number of UUTs in this run"));
    if (ShowAdminUutCountControl) {
        mainToolbar->addWidget(m_uutCount);
        mainToolbar->addSeparator();
    } else {
        m_uutCount->setValue(1);
        m_uutCount->hide();
    }
    mainToolbar->addAction(m_compileAction);
    mainToolbar->addAction(m_runAction);
    mainToolbar->addAction(m_pauseAction);
    mainToolbar->addAction(m_resumeAction);
    mainToolbar->addAction(m_stepIntoAction);
    mainToolbar->addAction(m_stepOverAction);
    mainToolbar->addAction(m_stopAction);
    mainToolbar->addSeparator();
    mainToolbar->addAction(m_scanAction);
    mainToolbar->addAction(m_productRoutingAction);
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

    constexpr int RunSidebarWidth = 230;
    auto* brandHeader = new QHBoxLayout;
    brandHeader->setContentsMargins(0, 0, 0, 0);
    brandHeader->setSpacing(style()->pixelMetric(QStyle::PM_SplitterWidth));

    auto* brandSlot = new QWidget(central);
    brandSlot->setObjectName(QStringLiteral("adminBrandSlot"));
    brandSlot->setFixedWidth(RunSidebarWidth);
    auto* brandSlotLayout = new QHBoxLayout(brandSlot);
    brandSlotLayout->setContentsMargins(0, 0, 0, 0);
    brandSlotLayout->setSpacing(0);

    auto* brandLogo = new QLabel(brandSlot);
    brandLogo->setObjectName(QStringLiteral("adminBrandLogo"));
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

    m_adminSequenceLabel = new QLabel(tr("No sequence selected"), central);
    m_adminSequenceLabel->setObjectName(QStringLiteral("adminSequenceLabel"));
    m_adminSequenceLabel->setAlignment(Qt::AlignCenter);
    m_adminSequenceLabel->setMinimumHeight(36);
    brandHeader->addWidget(m_adminSequenceLabel, 1);
    rootLayout->addLayout(brandHeader);

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
    sequenceToolbar->setFloatable(false);
    sequenceToolbar->setIconSize(QSize(20, 20));
    sequenceToolbar->setToolButtonStyle(Qt::ToolButtonIconOnly);
    sequenceToolbar->addAction(m_saveSequenceAction);
    sequenceToolbar->addSeparator();
    sequenceToolbar->addAction(m_undoAction);
    sequenceToolbar->addAction(m_redoAction);
    sequenceToolbar->addSeparator();
    sequenceToolbar->addAction(m_addStepAction);
    sequenceToolbar->addAction(m_sequenceVariablesAction);
    sequenceToolbar->addAction(m_importRegisterConfigAction);
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
    sequenceToolbar->addAction(m_expandSequencePhasesAction);
    sequenceToolbar->addAction(m_collapseSequencePhasesAction);
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
    functionPanel->setMinimumWidth(180);
    functionPanel->setMaximumWidth(360);
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
    m_sequenceTreeView->setMinimumWidth(280);
    polishReadableTreeView(m_sequenceTreeView);
    installProportionalHeader(m_sequenceTreeView, {5, 2, 2, 1, 1, 2, 2});
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
        tr("Find Step, ID, function, or device"));
    m_flowFieldSearch->setClearButtonEnabled(true);
    m_flowFieldSearch->setFixedHeight(32);
    m_flowFieldSearch->setMaximumWidth(320);
    m_flowFieldSearch->setMinimumWidth(180);
    m_flowFieldSearch->setStyleSheet(QStringLiteral(
        "QLineEdit#flowFieldSearch {"
        " background: #f7f8f9;"
        " border: 1px solid #cbd2d7;"
        " border-top: 0;"
        " border-bottom-left-radius: 8px;"
        " border-bottom-right-radius: 8px;"
        " padding: 4px 28px 5px 12px;"
        " color: #2c343a;"
        " selection-background-color: #d3e6f2;"
        "}"
        "QLineEdit#flowFieldSearch:focus {"
        " border-color: #718793;"
        " background: #ffffff;"
        "}"));
    m_flowFieldSearch->hide();
    m_flowFieldSearch->installEventFilter(this);
    flowFieldSearchLayout->addWidget(m_flowFieldSearch, 1);
    flowFieldSearchLayout->addStretch();
    flowFieldSearchRow->hide();
    connect(m_flowFieldSearch, &QLineEdit::returnPressed, this, [this] {
        const auto query = m_flowFieldSearch->text().trimmed();
        const auto matches = m_sequenceTreeModel->indexesMatchingText(query);
        if (matches.isEmpty()) {
            m_flowSearchMatchIndex = -1;
            if (!query.isEmpty()) {
                statusBar()->showMessage(
                    tr("No Flow item matches '%1'").arg(query), 7000);
            }
            return;
        }

        m_flowSearchMatchIndex = (m_flowSearchMatchIndex + 1) % matches.size();
        const auto match = matches.at(m_flowSearchMatchIndex);
        for (auto parent = match.parent(); parent.isValid();
             parent = parent.parent()) {
            m_sequenceTreeView->setExpanded(parent, true);
        }
        m_sequenceTreeView->selectionModel()->setCurrentIndex(
            match,
            QItemSelectionModel::ClearAndSelect |
                QItemSelectionModel::Rows);
        m_sequenceTreeView->scrollTo(
            match, QAbstractItemView::EnsureVisible);
        statusBar()->showMessage(
            tr("Match %1 of %2 for '%3'")
                .arg(m_flowSearchMatchIndex + 1)
                .arg(matches.size())
                .arg(query),
            5000);
    });
    connect(m_flowFieldSearch, &QLineEdit::textChanged, this,
            [this] { m_flowSearchMatchIndex = -1; });
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
    serviceAdminStartupAnimation();

    m_stationEditorPage = new QWidget(m_workspaceTabs);
    auto* stationEditorPage = m_stationEditorPage;
    stationEditorPage->setObjectName(QStringLiteral("stationEditorPage"));
    auto* stationEditorLayout = new QVBoxLayout(stationEditorPage);
    stationEditorLayout->setContentsMargins(0, 0, 0, 0);
    stationEditorLayout->setSpacing(6);
    auto* stationToolbar = new QToolBar(stationEditorPage);
    stationToolbar->setObjectName(QStringLiteral("stationToolbar"));
    stationToolbar->setMovable(false);
    stationToolbar->setFloatable(false);
    stationToolbar->setIconSize(QSize(20, 20));
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
    auto* stationSettingsScroll = new QScrollArea(stationWorkArea);
    stationSettingsScroll->setObjectName(
        QStringLiteral("stationSettingsScrollArea"));
    stationSettingsScroll->setWidgetResizable(true);
    stationSettingsScroll->setFrameShape(QFrame::NoFrame);
    stationSettingsScroll->setHorizontalScrollBarPolicy(
        Qt::ScrollBarAlwaysOff);
    stationSettingsScroll->setWidget(m_stationSettingsEditor);
    stationSettingsScroll->setMinimumWidth(180);

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
    m_stationDeviceView->setMinimumWidth(300);
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
    stationWorkArea->addWidget(stationSettingsScroll);
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
    serviceAdminStartupAnimation();

    m_runTestPage = new QWidget(m_workspaceTabs);
    auto* runPage = m_runTestPage;
    runPage->setObjectName(QStringLiteral("adminRunPage"));
    auto* runPageLayout = new QVBoxLayout(runPage);
    runPageLayout->setContentsMargins(0, 0, 0, 0);
    runPageLayout->setSpacing(8);

    m_adminRunStack = new QStackedWidget(runPage);
    m_adminRunStack->setObjectName(QStringLiteral("adminRunStack"));
    m_adminRunOverviewPage = new QWidget(m_adminRunStack);
    m_adminRunOverviewPage->setObjectName(
        QStringLiteral("adminRunOverviewPage"));
    auto* overviewLayout = new QVBoxLayout(m_adminRunOverviewPage);
    overviewLayout->setContentsMargins(0, 0, 0, 0);
    m_adminUutOverview = new MultiUutOverviewWidget(m_adminRunOverviewPage);
    m_adminUutOverview->setModel(m_uutOverviewModel);
    overviewLayout->addWidget(m_adminUutOverview);
    m_adminRunStack->addWidget(m_adminRunOverviewPage);

    m_adminRunDetailPage = new QWidget(m_adminRunStack);
    m_adminRunDetailPage->setObjectName(QStringLiteral("adminRunDetailPage"));
    auto* runDetailLayout = new QVBoxLayout(m_adminRunDetailPage);
    runDetailLayout->setContentsMargins(0, 0, 0, 0);
    runDetailLayout->setSpacing(8);
    auto* detailNavigation = new QWidget(runPage);
    detailNavigation->setObjectName(QStringLiteral("adminUutDetailNavigation"));
    detailNavigation->setVisible(
        ShowAdminUutOverview || ShowAdminUutSwitcher);
    auto* detailNavigationLayout = new QHBoxLayout(detailNavigation);
    detailNavigationLayout->setContentsMargins(0, 2, 6, 2);
    detailNavigationLayout->setSpacing(
        style()->pixelMetric(QStyle::PM_SplitterWidth));

    m_adminUutNavigationLead = new QWidget(detailNavigation);
    m_adminUutNavigationLead->setObjectName(
        QStringLiteral("adminUutNavigationLead"));
    m_adminUutNavigationLead->setFixedWidth(RunSidebarWidth);
    auto* navigationLeadLayout = new QHBoxLayout(m_adminUutNavigationLead);
    navigationLeadLayout->setContentsMargins(6, 0, 6, 0);
    navigationLeadLayout->setSpacing(8);
    m_adminBackToOverview = new QPushButton(
        overviewIndicatorIcon(0.0), tr("Overview"),
        m_adminUutNavigationLead);
    m_adminBackToOverview->setObjectName(
        QStringLiteral("adminBackToUutOverview"));
    m_adminBackToOverview->setToolTip(tr("Return to the UUT overview"));
    m_adminBackToOverview->setCheckable(true);
    m_adminBackToOverview->setIconSize(QSize(18, 18));
    m_adminBackToOverview->setProperty("overviewIndicatorFill", 0.0);
    m_adminBackToOverview->setEnabled(false);
    m_adminBackToOverview->setVisible(ShowAdminUutOverview);
    auto* overviewIndicatorAnimation = new QVariantAnimation(
        m_adminBackToOverview);
    overviewIndicatorAnimation->setDuration(140);
    connect(overviewIndicatorAnimation, &QVariantAnimation::valueChanged,
            m_adminBackToOverview,
            [button = m_adminBackToOverview](const QVariant& value) {
                const qreal fill = value.toReal();
                button->setProperty("overviewIndicatorFill", fill);
                button->setIcon(overviewIndicatorIcon(fill));
            });
    connect(m_adminBackToOverview, &QPushButton::toggled,
            m_adminBackToOverview,
            [button = m_adminBackToOverview,
             overviewIndicatorAnimation](bool checked) {
                overviewIndicatorAnimation->stop();
                overviewIndicatorAnimation->setStartValue(
                    button->property("overviewIndicatorFill").toReal());
                overviewIndicatorAnimation->setEndValue(checked ? 1.0 : 0.0);
                overviewIndicatorAnimation->start();
            });
    auto* detailTitle = new QLabel(tr("UUT DETAILS"), m_adminUutNavigationLead);
    detailTitle->setObjectName(QStringLiteral("adminSectionTitle"));
    navigationLeadLayout->addWidget(m_adminBackToOverview);
    navigationLeadLayout->addWidget(detailTitle);
    navigationLeadLayout->addStretch(1);

    m_adminUutNavigationGroup = new QButtonGroup(detailNavigation);
    m_adminUutNavigationGroup->setObjectName(
        QStringLiteral("adminUutNavigationGroup"));
    m_adminUutNavigationGroup->setExclusive(true);

    auto* uutButtonsHost = new QWidget(detailNavigation);
    uutButtonsHost->setObjectName(QStringLiteral("adminUutButtonsHost"));
    uutButtonsHost->setVisible(ShowAdminUutSwitcher);
    m_adminUutNavigationLayout = new QHBoxLayout(uutButtonsHost);
    m_adminUutNavigationLayout->setContentsMargins(0, 0, 0, 0);
    m_adminUutNavigationLayout->setSpacing(6);
    m_adminUutNavigationLayout->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    detailNavigationLayout->addWidget(m_adminUutNavigationLead);
    detailNavigationLayout->addWidget(uutButtonsHost, 1);
    m_adminRunStack->addWidget(m_adminRunDetailPage);
    m_adminRunStack->setCurrentWidget(m_adminRunDetailPage);
    connect(m_adminUutOverview, &MultiUutOverviewWidget::uutActivated,
            this, &MainWindow::showAdminUutDetails);
    connect(m_adminBackToOverview, &QPushButton::clicked,
            this, &MainWindow::showAdminUutOverview);
    connect(m_adminUutNavigationGroup, &QButtonGroup::idClicked,
            this, [this](int id) {
                const auto* button = m_adminUutNavigationGroup->button(id);
                if (button) {
                    showAdminUutDetails(button->property("uutId").toString());
                }
            });

    auto* splitter = new QSplitter(Qt::Horizontal, runPage);
    splitter->setObjectName(QStringLiteral("runSplitter"));
    splitter->setChildrenCollapsible(false);

    auto* sidebar = new QFrame(splitter);
    sidebar->setObjectName(QStringLiteral("adminRunSidebar"));
    sidebar->setMinimumWidth(185);
    sidebar->setMaximumWidth(265);
    sidebar->installEventFilter(this);
    auto* sidebarLayout = new QVBoxLayout(sidebar);
    sidebarLayout->setContentsMargins(18, 18, 18, 18);
    sidebarLayout->setSpacing(12);
    auto* unitTitle = new QLabel(tr("UNIT UNDER TEST"), sidebar);
    unitTitle->setObjectName(QStringLiteral("adminSectionTitle"));
    sidebarLayout->addWidget(unitTitle);
    auto* unitDetails = new QFormLayout;
    unitDetails->setHorizontalSpacing(12);
    unitDetails->setVerticalSpacing(10);
    m_adminSerialCaption = new QLabel(tr("SN"), sidebar);
    m_adminSerialCaption->setObjectName(
        QStringLiteral("adminSerialCaption"));
    m_adminSerialLabel = new QLabel(tr("--"), sidebar);
    m_adminSerialLabel->setObjectName(QStringLiteral("adminSerialLabel"));
    m_adminStationLabel = new QLabel(tr("--"), sidebar);
    m_adminStationLabel->setObjectName(QStringLiteral("adminStationLabel"));
    m_adminModelLabel = new QLabel(tr("--"), sidebar);
    m_adminModelLabel->setObjectName(QStringLiteral("adminModelLabel"));
    m_adminCustomerIdLabel = new QLabel(tr("--"), sidebar);
    m_adminCustomerIdLabel->setObjectName(
        QStringLiteral("adminCustomerIdLabel"));
    m_adminOrderLabel = new QLabel(tr("--"), sidebar);
    m_adminTesterLabel = new QLabel(tr("--"), sidebar);
    m_adminJigLabel = new QLabel(tr("--"), sidebar);
    unitDetails->addRow(m_adminSerialCaption, m_adminSerialLabel);
    unitDetails->addRow(tr("Station ID"), m_adminStationLabel);
    unitDetails->addRow(tr("Model"), m_adminModelLabel);
    unitDetails->addRow(tr("Customer ID"), m_adminCustomerIdLabel);
    unitDetails->addRow(tr("Order"), m_adminOrderLabel);
    unitDetails->addRow(tr("Tester"), m_adminTesterLabel);
    unitDetails->addRow(tr("Jig No."), m_adminJigLabel);
    sidebarLayout->addLayout(unitDetails);
    sidebarLayout->addStretch(1);

    m_adminYieldChart = new YieldDonutWidget(sidebar);
    m_adminYieldChart->setObjectName(QStringLiteral("adminYieldChart"));
    sidebarLayout->addWidget(m_adminYieldChart, 0, Qt::AlignHCenter);

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

    auto* runDataSplitter = new QSplitter(Qt::Vertical, m_adminRunDetailPage);
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
    m_resultView->setItemDelegateForColumn(
        UutStepModel::ActualColumn,
        new ParserActualDelegate(m_resultView));
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
    m_runtimeTimelineView->setModel(m_runtimeTimelineProxy);
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

    serviceAdminStartupAnimation();
    m_historyPage = new QWidget(m_workspaceTabs);
    auto* historyPage = m_historyPage;
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
    auto* exportPdf = new QPushButton(
        style()->standardIcon(QStyle::SP_DialogSaveButton), tr("PDF"), historyPage);
    openHistory->setToolTip(tr("Open selected report"));
    exportText->setToolTip(tr("Export selected report as TXT"));
    exportCsv->setToolTip(tr("Export selected report as CSV"));
    exportXlsx->setToolTip(tr("Export selected report as XLSX"));
    exportPdf->setToolTip(tr("Export selected report as PDF"));
    historyCommands->addWidget(openHistory);
    historyCommands->addWidget(exportText);
    historyCommands->addWidget(exportCsv);
    historyCommands->addWidget(exportXlsx);
    historyCommands->addWidget(exportPdf);
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
    connect(exportPdf, &QPushButton::clicked, this, [this] {
        exportSelectedHistory(HistoryExportFormat::Pdf);
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
    runDetailLayout->addWidget(runDataSplitter, 1);

    m_adminProgressPanel = new QWidget(runPage);
    m_adminProgressPanel->setObjectName(QStringLiteral("adminProgressPanel"));
    auto* progressLayout = new QVBoxLayout(m_adminProgressPanel);
    progressLayout->setContentsMargins(12, 7, 12, 7);
    m_adminProgress = new QProgressBar(m_adminProgressPanel);
    m_adminProgress->setObjectName(QStringLiteral("adminRunProgress"));
    m_adminProgress->setRange(0, 100);
    m_adminProgress->setValue(0);
    progressLayout->addWidget(m_adminProgress);
    splitter->addWidget(sidebar);
    splitter->addWidget(m_adminRunStack);
    splitter->setStretchFactor(0, 0);
    splitter->setStretchFactor(1, 1);
    splitter->setSizes({RunSidebarWidth, 930});
    runPageLayout->addWidget(detailNavigation);
    runPageLayout->addWidget(splitter, 1);
    runPageLayout->addWidget(m_adminProgressPanel);

    statusBar()->setObjectName(QStringLiteral("adminStatusBar"));
    auto* statsBar = new QWidget(statusBar());
    statsBar->setObjectName(QStringLiteral("adminStatsBar"));
    auto* statsLayout = new QHBoxLayout(statsBar);
    statsLayout->setContentsMargins(10, 2, 10, 2);
    statsLayout->setSpacing(18);
    const auto createCounter = [statsBar](const QString& objectName) {
        auto* label = new QLabel(statsBar);
        label->setObjectName(objectName);
        label->setAlignment(Qt::AlignCenter);
        label->setMinimumWidth(92);
        return label;
    };
    m_adminPassCount = createCounter(QStringLiteral("adminPassCount"));
    m_adminFailCount = createCounter(QStringLiteral("adminFailCount"));
    m_adminTotalCount = createCounter(QStringLiteral("adminTotalCount"));
    m_adminAverageTime = createCounter(QStringLiteral("adminAverageTime"));
    statsLayout->addWidget(m_adminPassCount);
    statsLayout->addWidget(m_adminFailCount);
    statsLayout->addWidget(m_adminTotalCount);
    statsLayout->addStretch(1);
    m_adminAverageTime->setMinimumWidth(190);
    statsLayout->addWidget(m_adminAverageTime);
    statusBar()->addPermanentWidget(statsBar, 1);

    m_workspaceTabs->insertTab(0, runPage, tr("Run Test"));
    m_workspaceTabs->addTab(historyPage, tr("Reports"));
    rootLayout->addWidget(m_workspaceTabs, 1);

    setCentralWidget(central);
    buildStartupOverlay();
    serviceAdminStartupAnimation();

    setStyleSheet(QStringLiteral(R"css(
        QToolBar#runnerToolbar {
            spacing: 4px;
            padding: 5px 9px;
        }
        QToolBar#runnerToolbar QToolButton {
            margin: 1px;
            padding: 5px 8px;
            font-weight: 600;
        }
        QToolBar#sequenceToolbar QToolButton,
        QToolBar#stationToolbar QToolButton {
            margin: 1px;
            padding: 5px;
        }
        QWidget#adminRunPage { background: #f4f6f7; color: #20262b; }
        QLabel#adminSequenceLabel {
            background: #ffffff; border: 1px solid #d7dde1; border-radius: 6px;
            font-size: 15px; font-weight: 600; padding: 8px 12px;
        }
        QFrame#adminRunSidebar, QWidget#adminProgressPanel {
            background: #ffffff; border: 1px solid #d7dde1; border-radius: 6px;
        }
        QWidget#adminUutDetailNavigation {
            background: #ffffff; border: 1px solid #d7dde1; border-radius: 6px;
        }
        QPushButton#adminBackToUutOverview {
            background: transparent; border: 0; color: #344048;
            min-height: 30px; padding: 2px 9px; font-weight: 600;
        }
        QPushButton#adminBackToUutOverview:hover {
            background: #e7f1f7; border-radius: 4px;
        }
        QPushButton#adminBackToUutOverview:checked {
            background: #f0f3f5; border-radius: 4px;
        }
        QPushButton[adminUutSwitch="true"] {
            background: #ffffff; border: 1px solid #c7ced3; border-radius: 4px;
            color: #20262b; min-height: 30px; padding: 1px 12px;
            font-weight: 700;
        }
        QPushButton[adminUutSwitch="true"]:hover {
            background: #f0f3f5; border-color: #89939a;
        }
        QPushButton[adminUutSwitch="true"]:checked,
        QPushButton[adminUutSwitch="true"]:checked:hover {
            background: #202328; border-color: #202328; color: #ffffff;
        }
        QStatusBar#adminStatusBar {
            background: #f8f9fa; border-top: 1px solid #dce1e4;
        }
        QWidget#adminStatsBar { background: transparent; border: 0; }
        QLabel#adminSectionTitle { color: #344048; font-size: 13px; font-weight: 700; }
        QLabel#adminMetricCaption { color: #707b83; font-size: 11px; font-weight: 600; }
        QLabel#adminElapsedLabel {
            background: #eef2f4; border: 1px solid #d4dce1; border-radius: 6px;
            color: #263139; font-size: 20px; font-weight: 600; padding: 10px 6px;
        }
        QLabel#adminPassCount { color: #2f7548; font-weight: 700; }
        QLabel#adminFailCount { color: #a43838; font-weight: 700; }
        QLabel#adminTotalCount { color: #344048; font-weight: 700; }
        QLabel#adminAverageTime { color: #56636c; font-weight: 700; }
        QProgressBar#adminRunProgress {
            border: 1px solid #c6ced3; background: #e9edef;
            border-radius: 5px; min-height: 23px; text-align: center;
        }
        QProgressBar#adminRunProgress::chunk { background: #4f7d5d; border-radius: 4px; }
        QTreeView, QTableView, QListView {
            selection-background-color: #dcecf6;
            selection-color: #20262b;
        }
        QTreeView::item:hover, QTreeView::item:selected,
        QTableView::item:hover, QTableView::item:selected,
        QListView::item:hover, QListView::item:selected {
            background: #dcecf6; color: #20262b;
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
            background: #ffffff; color: #20262b;
            border-bottom: 2px solid #30383e;
        }
    )css"));
    serviceAdminStartupAnimation();

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
    m_newProjectAction->setEnabled(canChangeSources);
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
    m_scanAction->setEnabled(
        m_autoRouteBySn ? canChangeSources : m_viewModel->canRun());
    m_productRoutingAction->setEnabled(canChangeSources);
    m_uutCount->setEnabled(
        ShowAdminUutCountControl && canChangeSources);

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
    const bool templateHasChanges = m_newProjectTemplate &&
        (sequenceHasChanges || stationHasChanges);
    const bool stationActive = isStationWorkspaceActive();
    m_saveSequenceAction->setText(
        stationActive ? tr("Save Station") : tr("Save Sequence"));
    m_saveSequenceAction->setToolTip(
        m_newProjectTemplate
            ? tr("Save the Sequence and Station as a new project")
            : (stationActive ? tr("Save Station configuration")
                             : tr("Save sequence JSON")));
    m_saveSequenceAction->setEnabled(
        canChangeSources &&
        (templateHasChanges ||
         (stationActive ? stationHasChanges : sequenceHasChanges)));
    m_saveSequenceAsAction->setText(
        m_newProjectTemplate ? tr("Save New Project As...")
                             : tr("Save Sequence As..."));
    m_saveSequenceAsAction->setEnabled(canChangeSources && hasDocument);
    m_undoAction->setEnabled(
        canChangeSources && m_sequenceDocument->undoStack()->canUndo());
    m_redoAction->setEnabled(
        canChangeSources && m_sequenceDocument->undoStack()->canRedo());
    m_addStepAction->setEnabled(canChangeSources && hasDocument && hasSelection);
    m_sequenceVariablesAction->setEnabled(canChangeSources && hasDocument);
    const bool registerImporterAvailable =
        !registerImporterExecutablePath().isEmpty();
    m_importRegisterConfigAction->setVisible(registerImporterAvailable);
    m_importRegisterConfigAction->setEnabled(
        registerImporterAvailable && canChangeSources && hasDocument &&
        !m_registerImportProcess);
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
    m_expandSequencePhasesAction->setEnabled(hasDocument);
    m_collapseSequencePhasesAction->setEnabled(hasDocument);
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
        canChangeSources && hasStation &&
        (stationHasChanges || templateHasChanges));
    m_saveStationAsAction->setText(
        m_newProjectTemplate ? tr("Save New Project As...")
                             : tr("Save Station As..."));
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

    QVector<RunRequest::UutInput> previewUuts;
    const int uutCount = m_uutCount ? qMax(1, m_uutCount->value()) : 1;
    previewUuts.reserve(uutCount);
    for (int index = 1; index <= uutCount; ++index) {
        RunRequest::UutInput input;
        input.uutId = QStringLiteral("UUT-%1").arg(index);
        input.variables.insert(QStringLiteral("sn"), QString{});
        input.variables.insert(QStringLiteral("serialNumber"), QString{});
        previewUuts.push_back(std::move(input));
    }

    auto preview = m_adminPreviewReport;
    preview.completed = false;
    preview.hasError = false;
    preview.state = PicoATE::Core::ExecutionState::Idle;
    const auto previewTemplate = preview.uuts.isEmpty()
        ? PicoATE::Core::UutReport{}
        : preview.uuts.first();
    preview.uuts.clear();
    for (const auto& input : previewUuts) {
        auto uut = previewTemplate;
        uut.uutId = input.uutId;
        uut.serialNumber.clear();
        uut.completed = false;
        uut.hasError = false;
        uut.outcome = PicoATE::Core::NodeOutcome::Unknown;
        preview.uuts.push_back(std::move(uut));
    }

    m_uutOverviewModel->resetForRun(preview, previewUuts);
    m_selectedAdminUutId = previewUuts.isEmpty()
        ? PicoATE::Core::UutId{}
        : previewUuts.first().uutId;
    m_adminUutOverview->setSelectedUutId(m_selectedAdminUutId);
    m_uutStepModel->setVisibleUutId(m_selectedAdminUutId);
    m_runtimeTimelineProxy->setVisibleUutId(m_selectedAdminUutId);
    displayReport(preview);
    m_resultView->clearSelection();
    m_resultView->setCurrentIndex({});
    m_attemptModel->setStep(std::nullopt);
    m_measurementModel->setMeasurements({});
    rebuildAdminUutButtons();
    if (ShowAdminUutOverview && previewUuts.size() > 1) {
        showAdminUutOverview();
    } else {
        showAdminUutDetails(m_selectedAdminUutId);
    }
    updateAdminProgress();
}

void MainWindow::updateAdminRunState(UiRunState state)
{
    if (!m_adminOverallResult) {
        return;
    }
    m_adminSessionState = state;
    if (state == UiRunState::Starting) {
        m_adminStopRequested = false;
    } else if (state == UiRunState::Stopping) {
        m_adminStopRequested = true;
    }
    const bool showingOverview = m_adminRunStack && m_adminRunOverviewPage &&
        m_adminRunStack->currentWidget() == m_adminRunOverviewPage;
    setAdminOverallResultTypography(
        m_adminOverallResult, showingOverview, m_responsiveLayoutMode == 1);
    if (showingOverview) {
        m_adminOverallResult->setText(
            adminOverviewStateText(state, m_adminStopRequested));
        m_adminOverallResult->setStyleSheet(
            adminOverviewStateStyle(state, m_adminStopRequested));
    } else if (m_adminRunStack && m_adminRunDetailPage &&
               m_adminRunStack->currentWidget() == m_adminRunDetailPage &&
               m_uutOverviewModel &&
               m_uutOverviewModel->rowForUut(m_selectedAdminUutId) >= 0) {
        updateSelectedAdminUutSummary();
    } else {
        m_adminOverallResult->setText(adminRunStateText(state));
        m_adminOverallResult->setStyleSheet(adminRunStateStyle(state));
    }
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
    if (!m_adminStationLabel || !m_adminModelLabel ||
        !m_adminCustomerIdLabel || !m_stationDocument ||
        m_stationDocument->filePath().isEmpty()) {
        return;
    }
    const auto result = PicoATE::Core::loadStationConfigFile(
        m_stationDocument->filePath());
    const auto stationId = result.config.stationId.isEmpty()
        ? QFileInfo(m_stationDocument->filePath()).completeBaseName()
        : result.config.stationId;
    m_adminStationLabel->setText(stationId);
    m_adminModelLabel->setText(result.config.model.trimmed().isEmpty()
                                   ? tr("--")
                                   : result.config.model.trimmed());
    m_adminCustomerIdLabel->setText(
        result.config.customerId.trimmed().isEmpty()
            ? tr("--")
            : result.config.customerId.trimmed());
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
    m_adminPassCount->setText(tr("PASS %1").arg(m_adminPassedUnits));
    m_adminFailCount->setText(tr("FAIL %1").arg(m_adminFailedUnits));
    m_adminTotalCount->setText(tr("TOTAL %1").arg(total));
    m_adminYieldChart->setCounts(m_adminPassedUnits, m_adminFailedUnits);
    const qint64 average = total > 0
        ? m_adminTotalCompletedDurationMs / total
        : 0;
    m_adminAverageTime->setText(tr("AVERAGE TIME %1:%2.%3")
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
    if (m_uutOverviewModel) {
        m_uutOverviewModel->setSessionElapsedMs(elapsed);
    }
    m_adminElapsedLabel->setText(QStringLiteral("%1:%2.%3")
        .arg(elapsed / 60000, 2, 10, QLatin1Char('0'))
        .arg(elapsed / 1000 % 60, 2, 10, QLatin1Char('0'))
        .arg(elapsed % 1000, 3, 10, QLatin1Char('0')));
}

void MainWindow::showAdminUutOverview()
{
    if (!ShowAdminUutOverview || !m_adminRunStack ||
        !m_adminRunOverviewPage ||
        !m_uutOverviewModel || m_uutOverviewModel->rowCount() <= 1) {
        showAdminUutDetails(m_selectedAdminUutId);
        return;
    }
    if (m_adminUutNavigationGroup) {
        m_adminUutNavigationGroup->setExclusive(false);
        for (auto* button : m_adminUutNavigationGroup->buttons()) {
            button->setChecked(false);
        }
        m_adminUutNavigationGroup->setExclusive(true);
    }
    if (m_adminSerialCaption) {
        m_adminSerialCaption->hide();
    }
    if (m_adminSerialLabel) {
        m_adminSerialLabel->hide();
    }
    if (m_adminProgressPanel) {
        m_adminProgressPanel->hide();
    }
    if (m_adminBackToOverview) {
        m_adminBackToOverview->setChecked(true);
    }
    m_adminRunStack->setCurrentWidget(m_adminRunOverviewPage);
    setAdminOverallResultTypography(
        m_adminOverallResult, true, m_responsiveLayoutMode == 1);
    m_adminOverallResult->setText(
        adminOverviewStateText(m_adminSessionState, m_adminStopRequested));
    m_adminOverallResult->setStyleSheet(
        adminOverviewStateStyle(m_adminSessionState, m_adminStopRequested));
}

void MainWindow::showAdminUutDetails(const PicoATE::Core::UutId& uutId)
{
    if (!m_uutOverviewModel || !m_adminRunStack || !m_adminRunDetailPage) {
        return;
    }
    auto selected = uutId;
    int row = m_uutOverviewModel->rowForUut(selected);
    if (row < 0 && m_uutOverviewModel->rowCount() > 0) {
        const auto first = m_uutOverviewModel->entryAt(0);
        selected = first ? first->uutId : PicoATE::Core::UutId{};
        row = 0;
    }
    if (row < 0) {
        return;
    }

    m_selectedAdminUutId = selected;
    m_uutStepModel->setVisibleUutId(selected);
    m_runtimeTimelineProxy->setVisibleUutId(selected);
    m_adminUutOverview->setSelectedUutId(selected);
    if (m_adminUutNavigationGroup) {
        for (auto* button : m_adminUutNavigationGroup->buttons()) {
            if (button->property("uutId").toString() == selected) {
                button->setChecked(true);
                break;
            }
        }
    }
    if (m_adminBackToOverview) {
        m_adminBackToOverview->setVisible(ShowAdminUutOverview);
        m_adminBackToOverview->setEnabled(
            ShowAdminUutOverview && m_uutOverviewModel->rowCount() > 1);
    }
    if (m_adminSerialCaption) {
        m_adminSerialCaption->show();
    }
    if (m_adminSerialLabel) {
        m_adminSerialLabel->show();
    }
    if (m_adminProgressPanel) {
        m_adminProgressPanel->show();
    }
    if (m_adminBackToOverview) {
        m_adminBackToOverview->setChecked(false);
    }
    m_adminRunStack->setCurrentWidget(m_adminRunDetailPage);
    setAdminOverallResultTypography(
        m_adminOverallResult, false, m_responsiveLayoutMode == 1);
    m_adminLastAutoFollowLine = 0;
    m_adminLastAutoFollowUutId = selected;
    m_adminLastAutoFollowNodeId.clear();
    m_resultView->expandAll();
    m_resultView->clearSelection();
    m_resultView->setCurrentIndex({});
    m_attemptModel->setStep(std::nullopt);
    m_measurementModel->setMeasurements({});
    updateSelectedAdminUutSummary();
    if (m_runtimeTimelineProxy->rowCount() > 0) {
        m_runtimeTimelineView->scrollToBottom();
    }
}

void MainWindow::rebuildAdminUutButtons()
{
    if (!m_adminUutNavigationGroup || !m_adminUutNavigationLayout ||
        !m_uutOverviewModel) {
        return;
    }

    const auto existingButtons = m_adminUutNavigationGroup->buttons();
    for (auto* button : existingButtons) {
        m_adminUutNavigationGroup->removeButton(button);
        m_adminUutNavigationLayout->removeWidget(button);
        delete button;
    }

    if (!ShowAdminUutSwitcher) {
        if (m_adminBackToOverview) {
            m_adminBackToOverview->setEnabled(
                ShowAdminUutOverview && m_uutOverviewModel->rowCount() > 1);
        }
        return;
    }

    struct ButtonDefinition {
        PicoATE::Core::UutId uutId;
        QString serialNumber;
        QString text;
    };
    QVector<ButtonDefinition> definitions;
    definitions.reserve(m_uutOverviewModel->rowCount());
    QFont buttonFont;
    int commonButtonWidth = 92;
    for (int row = 0; row < m_uutOverviewModel->rowCount(); ++row) {
        const auto entry = m_uutOverviewModel->entryAt(row);
        if (!entry) {
            continue;
        }
        const auto prefix = QStringLiteral("UUT%1").arg(row + 1);
        const auto serialNumber = entry->serialNumber.trimmed();
        const auto text = serialNumber.isEmpty()
            ? prefix
            : QStringLiteral("%1-%2").arg(prefix, serialNumber);
        if (definitions.isEmpty()) {
            buttonFont = m_adminBackToOverview->font();
            buttonFont.setBold(true);
        }
        commonButtonWidth = qMax(
            commonButtonWidth,
            QFontMetrics(buttonFont).horizontalAdvance(text) + 30);
        definitions.push_back({entry->uutId, serialNumber, text});
    }
    commonButtonWidth = qMin(commonButtonWidth, 240);

    for (int row = 0; row < definitions.size(); ++row) {
        const auto& definition = definitions.at(row);
        auto* button = new QPushButton(
            definition.text, m_adminUutNavigationLayout->parentWidget());
        button->setObjectName(
            QStringLiteral("adminUutButton_%1").arg(row + 1));
        button->setProperty("adminUutSwitch", true);
        button->setProperty("uutId", definition.uutId);
        button->setCheckable(true);
        button->setFont(buttonFont);
        button->setFixedWidth(commonButtonWidth);
        button->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Preferred);
        button->setToolTip(definition.serialNumber.isEmpty()
                               ? definition.uutId
                               : tr("%1 | SN: %2")
                                     .arg(definition.uutId,
                                          definition.serialNumber));
        m_adminUutNavigationGroup->addButton(button, row + 1);
        m_adminUutNavigationLayout->addWidget(button);
    }

    m_adminBackToOverview->setEnabled(
        ShowAdminUutOverview && m_uutOverviewModel->rowCount() > 1);
    if (m_adminRunStack &&
        m_adminRunStack->currentWidget() == m_adminRunOverviewPage &&
        m_uutOverviewModel->rowCount() > 1) {
        m_adminUutNavigationGroup->setExclusive(false);
        for (auto* button : m_adminUutNavigationGroup->buttons()) {
            button->setChecked(false);
        }
        m_adminUutNavigationGroup->setExclusive(true);
        return;
    }
    for (auto* button : m_adminUutNavigationGroup->buttons()) {
        if (button->property("uutId").toString() == m_selectedAdminUutId) {
            button->setChecked(true);
            return;
        }
    }
}

void MainWindow::updateSelectedAdminUutSummary()
{
    if (!m_uutOverviewModel || m_selectedAdminUutId.isEmpty()) {
        return;
    }
    const int row = m_uutOverviewModel->rowForUut(m_selectedAdminUutId);
    const auto entry = m_uutOverviewModel->entryAt(row);
    if (!entry) {
        return;
    }
    m_adminSerialLabel->setText(entry->serialNumber.isEmpty()
                                    ? tr("--")
                                    : entry->serialNumber);
    if (m_adminRunStack && m_adminRunOverviewPage &&
        m_adminRunStack->currentWidget() == m_adminRunOverviewPage) {
        m_adminOverallResult->setText(adminOverviewStateText(
            m_adminSessionState, m_adminStopRequested));
        m_adminOverallResult->setStyleSheet(adminOverviewStateStyle(
            m_adminSessionState, m_adminStopRequested));
        return;
    }
    UiRunState displayState = UiRunState::Ready;
    switch (entry->state) {
    case UutOverviewState::Running:
    case UutOverviewState::Paused:
        displayState = UiRunState::Running;
        break;
    case UutOverviewState::Passed:
        displayState = UiRunState::Completed;
        break;
    case UutOverviewState::Failed:
    case UutOverviewState::Stopped:
        displayState = UiRunState::Failed;
        break;
    case UutOverviewState::Waiting:
        displayState = UiRunState::Ready;
        break;
    }
    m_adminOverallResult->setText(
        entry->state == UutOverviewState::Waiting
            ? tr("WAITING")
            : adminRunStateText(displayState));
    m_adminOverallResult->setStyleSheet(adminRunStateStyle(displayState));
    m_adminProgress->setValue(entry->progress);
}

void MainWindow::updateReport()
{
    const auto report = m_viewModel->report();
    if (report.completed &&
        report.state == PicoATE::Core::ExecutionState::Aborted) {
        m_adminStopRequested = true;
    }
    if (m_uutOverviewModel && m_adminElapsed.isValid()) {
        m_uutOverviewModel->setSessionElapsedMs(m_adminElapsed.elapsed());
    }
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
        for (const auto& uut : report.uuts) {
            if (uut.completed && !uut.hasError &&
                uut.outcome != PicoATE::Core::NodeOutcome::Cancelled) {
                ++m_adminPassedUnits;
            } else {
                ++m_adminFailedUnits;
            }
            m_adminTotalCompletedDurationMs += uut.durationMs >= 0
                ? uut.durationMs
                : 0;
        }
        m_currentAdminRunCounted = true;
        updateAdminYield();
    }
    m_uutOverviewModel->setReport(report);
    if (m_uutOverviewModel->rowForUut(m_selectedAdminUutId) < 0 &&
        m_uutOverviewModel->rowCount() > 0) {
        m_selectedAdminUutId = m_uutOverviewModel->entryAt(0)->uutId;
    }
    m_adminUutOverview->setSelectedUutId(m_selectedAdminUutId);
    m_uutStepModel->setVisibleUutId(m_selectedAdminUutId);
    m_runtimeTimelineProxy->setVisibleUutId(m_selectedAdminUutId);
    rebuildAdminUutButtons();
    displayReport(report, true);
    updateSelectedAdminUutSummary();
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

void MainWindow::displayReport(const PicoATE::Core::ExecutionReport& report,
                               bool preserveRuntimePosition)
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
    } else if (preserveRuntimePosition &&
               !m_adminLastAutoFollowNodeId.isEmpty()) {
        const auto followed = m_uutStepModel->indexForStep(
            m_adminLastAutoFollowUutId, m_adminLastAutoFollowNodeId);
        m_resultView->clearSelection();
        m_resultView->setCurrentIndex({});
        if (followed.isValid()) {
            m_resultView->scrollTo(followed,
                                   QAbstractItemView::PositionAtBottom);
        }
    } else {
        selectInitialResult();
    }
}

void MainWindow::applyRuntimeEvents(
    const QVector<PicoATE::Core::RuntimeEvent>& events)
{
    m_operatorPromptPresenter->applyRuntimeEvents(events);
    const bool detailVisible = isRunDetailVisible();
    PicoATE::Core::UutId selectedUutId;
    PicoATE::Core::NodeId selectedStepId;
    if (detailVisible) {
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
    }

    if (m_adminElapsed.isValid()) {
        m_uutOverviewModel->setSessionElapsedMs(m_adminElapsed.elapsed());
    }
    m_uutOverviewModel->applyRuntimeEvents(events);
    m_uutStepModel->applyRuntimeEvents(events);
    m_deviceStatusModel->applyRuntimeEvents(events);
    const auto logLines = m_runtimeTimelineModel->applyRuntimeEvents(events);
    const auto written = m_runArtifactWriter->appendLogLines(logLines);
    if (!written.success) {
        statusBar()->showMessage(
            tr("TXT log write failed: %1").arg(written.errorMessage),
            10000);
    }
    if (detailVisible && m_runtimeTimelineView->model()->rowCount() > 0) {
        m_runtimeTimelineView->scrollToBottom();
    }
    if (detailVisible) {
        const auto restored = m_uutStepModel->indexForStep(selectedUutId,
                                                            selectedStepId);
        if (restored.isValid()) {
            m_resultView->setCurrentIndex(restored);
            updateStepDetails(restored);
        } else if (m_resultView->currentIndex().isValid()) {
            updateStepDetails(m_resultView->currentIndex());
        } else {
            m_attemptModel->setStep(std::nullopt);
            m_measurementModel->setMeasurements({});
        }
    }

    for (const auto& event : events) {
        if (!event.nodeId.isEmpty() &&
            runtimeEventCarriesActivationState(event.kind)) {
            if (adminIsTerminalActivation(event.activationState)) {
                m_adminTerminalNodes.insert(event.nodeId);
            } else {
                m_adminTerminalNodes.remove(event.nodeId);
            }
        }
        if (!event.nodeId.isEmpty() &&
            event.activationState == PicoATE::Core::ActivationState::Running) {
            const auto index = m_uutStepModel->indexForStep(event.uutId,
                                                             event.nodeId);
            if (index.isValid()) {
                const int line = m_uutStepModel->visualLineNumber(index);
                if (line > m_adminLastAutoFollowLine) {
                    m_adminLastAutoFollowLine = line;
                    m_adminLastAutoFollowUutId = event.uutId;
                    m_adminLastAutoFollowNodeId = event.nodeId;
                    if (detailVisible) {
                        m_resultView->scrollTo(
                            index, QAbstractItemView::PositionAtBottom);
                    }
                }
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
    if (isRunWorkspaceActive()) {
        updateAdminProgress();
        updateSelectedAdminUutSummary();
    }
}

void MainWindow::refreshVisibleRuntimeViews()
{
    if (!isRunWorkspaceActive()) {
        return;
    }
    updateAdminProgress();
    updateSelectedAdminUutSummary();
    if (!isRunDetailVisible()) {
        return;
    }
    if (m_runtimeTimelineView && m_runtimeTimelineView->model() &&
        m_runtimeTimelineView->model()->rowCount() > 0) {
        m_runtimeTimelineView->scrollToBottom();
    }
    if (m_resultView && m_resultView->currentIndex().isValid()) {
        updateStepDetails(m_resultView->currentIndex());
    }
    if (!m_adminLastAutoFollowNodeId.isEmpty()) {
        const auto followed = m_uutStepModel->indexForStep(
            m_adminLastAutoFollowUutId, m_adminLastAutoFollowNodeId);
        if (followed.isValid()) {
            m_resultView->scrollTo(followed,
                                   QAbstractItemView::PositionAtBottom);
        }
    }
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
    const auto sourceIndex = m_runtimeTimelineModel->index(
        row, RuntimeTimelineModel::MessageColumn);
    const auto index = m_runtimeTimelineProxy->mapFromSource(sourceIndex);
    if (!index.isValid()) {
        return;
    }
    m_runtimeTimelineView->setCurrentIndex(index);
    m_runtimeTimelineView->scrollTo(index, QAbstractItemView::PositionAtCenter);
}

void MainWindow::selectTimelineEvent(const QModelIndex& index)
{
    if (!index.isValid() || !m_runtimeTimelineModel) {
        return;
    }

    const auto sourceIndex = m_runtimeTimelineProxy->mapToSource(index);
    const auto event = m_runtimeTimelineModel->eventAt(sourceIndex.row());
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
    const auto sourceLogIndex = m_runtimeTimelineModel->index(
        row, RuntimeTimelineModel::MessageColumn);
    const auto logIndex = m_runtimeTimelineProxy->mapFromSource(sourceLogIndex);
    if (!logIndex.isValid()) {
        return;
    }
    m_runtimeTimelineView->setCurrentIndex(logIndex);
    m_runtimeTimelineView->scrollTo(
        logIndex, QAbstractItemView::PositionAtCenter);
}

void MainWindow::selectFlowNodeForResult(const QModelIndex& index)
{
    const auto step = m_uutStepModel->stepAt(index);
    if (!step || !m_sequenceTreeModel || !m_sequenceTreeView) {
        return;
    }

    const auto nodePath = step->nodePath.isEmpty() ? step->stepId : step->nodePath;
    if (nodePath.isEmpty()) {
        return;
    }
    m_sequenceTreeModel->setCurrentDebugNodePath(nodePath);
    const auto flowIndex = m_sequenceTreeModel->indexForNodePath(nodePath);
    if (!flowIndex.isValid()) {
        return;
    }
    m_selectedSequencePath = m_sequenceTreeModel->pathForIndex(flowIndex);
    m_sequenceTreeView->setCurrentIndex(flowIndex);
    m_sequenceTreeView->scrollTo(flowIndex, QAbstractItemView::PositionAtCenter);
    if (m_stepPropertyEditor) {
        m_stepPropertyEditor->setCurrentItem(m_selectedSequencePath);
    }
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
    m_historyLoaded = true;
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
    const bool pdf = format == HistoryExportFormat::Pdf;
    const auto suffix = pdf
        ? QStringLiteral("pdf")
        : (xlsx ? QStringLiteral("xlsx")
                : (csv ? QStringLiteral("csv") : QStringLiteral("txt")));
    const auto title = pdf
        ? tr("Export PDF Report")
        : (xlsx ? tr("Export XLSX Report")
                : (csv ? tr("Export CSV Report") : tr("Export TXT Report")));
    const auto filter = pdf
        ? tr("PDF Report (*.pdf)")
        : (xlsx ? tr("Excel Workbook (*.xlsx)")
                : (csv ? tr("CSV Report (*.csv)") : tr("TXT Report (*.txt)")));
    const auto path = QFileDialog::getSaveFileName(
        this,
        title,
        entry->id + '.' + suffix,
        filter);
    if (path.isEmpty()) {
        return;
    }
    auto result = pdf
        ? ReportExporter::savePdf(path, loaded.report)
        : (xlsx ? ReportExporter::saveXlsx(path, loaded.report)
                : (csv ? ReportExporter::saveCsv(path, loaded.report)
                       : ReportExporter::saveText(path, loaded.report)));
    if (result.success && (csv || xlsx)) {
        result = ReportExporter::makeReadOnly(path);
    }
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
    m_uutCount->setValue(
        ShowAdminUutCountControl
            ? settings.value(QStringLiteral("UutCount"), 1).toInt()
            : 1);
    m_connectionTimeoutMs->setValue(
        settings.value(QStringLiteral("ConnectionTimeoutMs"), 5000).toInt());
    m_responsiveLayoutMode = settings.value(
        QStringLiteral("ResponsiveLayoutMode"), -1).toInt();
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
    settings.setValue(QStringLiteral("UutCount"),
                      ShowAdminUutCountControl ? m_uutCount->value() : 1);
    settings.setValue(QStringLiteral("ConnectionTimeoutMs"),
                      m_connectionTimeoutMs->value());
    settings.setValue(QStringLiteral("ResponsiveLayoutMode"),
                      m_responsiveLayoutMode >= 0
                          ? m_responsiveLayoutMode
                          : (width() < 1400 || height() < 760 ? 1 : 0));
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
    m_responsiveLayoutMode = -1;
    applyResponsiveLayout(true);
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
