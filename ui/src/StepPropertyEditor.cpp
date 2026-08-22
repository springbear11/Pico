#include "StepPropertyEditor.h"

#include "LoadingSpinner.h"
#include "OnOffControl.h"
#include "ProjectResourcePaths.h"

#include <QAbstractButton>
#include <QAbstractItemView>
#include <QCheckBox>
#include <QColor>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QDoubleSpinBox>
#include <QFileInfo>
#include <QFormLayout>
#include <QGroupBox>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QIcon>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QLabel>
#include <QLineEdit>
#include <QListView>
#include <QMenu>
#include <QPersistentModelIndex>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QRegularExpression>
#include <QResizeEvent>
#include <QScrollArea>
#include <QSet>
#include <QSignalBlocker>
#include <QSizePolicy>
#include <QSpinBox>
#include <QStandardItemModel>
#include <QStyle>
#include <QTabWidget>
#include <QTableWidget>
#include <QTextCursor>
#include <QTimer>
#include <QToolButton>
#include <QVariantAnimation>
#include <QVBoxLayout>

#include <algorithm>
#include <array>
#include <limits>
#include <cmath>
#include <utility>

namespace PicoATE::Ui {

namespace {

QString objectText(const QJsonObject& object)
{
    return object.isEmpty()
        ? QString()
        : QString::fromUtf8(QJsonDocument(object).toJson(QJsonDocument::Indented)).trimmed();
}

QString arrayText(const QJsonArray& array)
{
    return array.isEmpty()
        ? QString()
        : QString::fromUtf8(QJsonDocument(array).toJson(QJsonDocument::Indented)).trimmed();
}

bool parseObjectText(const QString& text,
                     QJsonObject& object,
                     QString& errorMessage)
{
    if (text.trimmed().isEmpty()) {
        object = {};
        return true;
    }

    QJsonParseError error;
    const auto document = QJsonDocument::fromJson(text.toUtf8(), &error);
    if (error.error != QJsonParseError::NoError) {
        errorMessage = QObject::tr("JSON error at offset %1: %2")
                           .arg(error.offset)
                           .arg(error.errorString());
        return false;
    }
    if (!document.isObject()) {
        errorMessage = QObject::tr("Expected a JSON object");
        return false;
    }
    object = document.object();
    return true;
}

bool parseArrayText(const QString& text,
                    QJsonArray& array,
                    QString& errorMessage)
{
    if (text.trimmed().isEmpty()) {
        array = {};
        return true;
    }

    QJsonParseError error;
    const auto document = QJsonDocument::fromJson(text.toUtf8(), &error);
    if (error.error != QJsonParseError::NoError) {
        errorMessage = QObject::tr("JSON error at offset %1: %2")
                           .arg(error.offset)
                           .arg(error.errorString());
        return false;
    }
    if (!document.isArray()) {
        errorMessage = QObject::tr("Expected a JSON array");
        return false;
    }
    array = document.array();
    return true;
}

bool parseCanIdentifier(const QJsonValue& value, quint32& result)
{
    if (value.isDouble()) {
        const double number = value.toDouble(-1.0);
        if (!std::isfinite(number) || number < 0.0 ||
            number > static_cast<double>(0x1FFFFFFF) || std::floor(number) != number) {
            return false;
        }
        result = static_cast<quint32>(number);
        return true;
    }
    if (!value.isString()) {
        return false;
    }
    const auto text = value.toString().trimmed();
    bool ok = false;
    const auto number = text.startsWith(QStringLiteral("0x"), Qt::CaseInsensitive)
        ? text.mid(2).toULongLong(&ok, 16)
        : text.toULongLong(&ok, 10);
    if (!ok || number > 0x1FFFFFFFULL) {
        return false;
    }
    result = static_cast<quint32>(number);
    return true;
}

void setFormRowVisible(QFormLayout* form, QWidget* field, bool visible)
{
    if (!form || !field) {
        return;
    }
    field->setVisible(visible);
    if (auto* label = form->labelForField(field)) {
        label->setVisible(visible);
    }
}

void setComboValue(QComboBox* combo, const QString& value)
{
    const int index = combo->findData(value, Qt::UserRole, Qt::MatchFixedString);
    if (index >= 0) {
        combo->setCurrentIndex(index);
        return;
    }
    if (!value.isEmpty()) {
        combo->addItem(value, value);
        combo->setCurrentIndex(combo->count() - 1);
    }
}

void insertOrRemove(QJsonObject& object,
                    const QString& key,
                    const QString& value)
{
    const auto trimmed = value.trimmed();
    if (trimmed.isEmpty()) {
        object.remove(key);
    } else {
        object.insert(key, trimmed);
    }
}

QJsonArray tagsFromText(const QString& text)
{
    QJsonArray tags;
    for (const auto& source : text.split(',', Qt::SkipEmptyParts)) {
        const auto tag = source.trimmed();
        if (!tag.isEmpty()) {
            tags.push_back(tag);
        }
    }
    return tags;
}

QString tagsText(const QJsonArray& tags)
{
    QStringList values;
    for (const auto& tag : tags) {
        if (tag.isString()) {
            values.push_back(tag.toString());
        }
    }
    return values.join(QStringLiteral(", "));
}

void addItems(QComboBox* combo, std::initializer_list<const char*> values)
{
    combo->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    combo->setMinimumContentsLength(10);
    combo->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
    for (const auto* value : values) {
        combo->addItem(QString::fromLatin1(value), QString::fromLatin1(value));
    }
}

QString normalizedLimitComparison(QString value)
{
    value = value.trimmed().toLower();
    value.remove(QLatin1Char('-'));
    value.remove(QLatin1Char('_'));
    value.remove(QLatin1Char(' '));
    return value;
}

QString limitEditorMode(const QJsonObject& parameters)
{
    const auto comparison = normalizedLimitComparison(
        parameters.value(QStringLiteral("comparison")).toString(
            QStringLiteral("between")));
    if (comparison == QStringLiteral("between") ||
        comparison == QStringLiteral("range")) {
        return parameters.contains(QStringLiteral("lower")) ||
               parameters.contains(QStringLiteral("lowerLimit")) ||
               parameters.contains(QStringLiteral("upper")) ||
               parameters.contains(QStringLiteral("upperLimit"))
            ? QStringLiteral("betweenLimits")
            : QStringLiteral("betweenTolerance");
    }
    if (comparison == QStringLiteral("==") || comparison == QStringLiteral("eq")) return QStringLiteral("equal");
    if (comparison == QStringLiteral("!=") || comparison == QStringLiteral("ne")) return QStringLiteral("notEqual");
    if (comparison == QStringLiteral(">") || comparison == QStringLiteral("gt")) return QStringLiteral("greaterThan");
    if (comparison == QStringLiteral(">=") || comparison == QStringLiteral("ge") || comparison == QStringLiteral("gte")) return QStringLiteral("greaterOrEqual");
    if (comparison == QStringLiteral("<") || comparison == QStringLiteral("lt")) return QStringLiteral("lessThan");
    if (comparison == QStringLiteral("<=") || comparison == QStringLiteral("le") || comparison == QStringLiteral("lte")) return QStringLiteral("lessOrEqual");
    if (comparison == QStringLiteral("greaterthan")) return QStringLiteral("greaterThan");
    if (comparison == QStringLiteral("greaterorequal")) return QStringLiteral("greaterOrEqual");
    if (comparison == QStringLiteral("lessthan")) return QStringLiteral("lessThan");
    if (comparison == QStringLiteral("lessorequal")) return QStringLiteral("lessOrEqual");
    if (comparison == QStringLiteral("notequal")) return QStringLiteral("notEqual");
    if (comparison == QStringLiteral("startswith")) return QStringLiteral("startsWith");
    if (comparison == QStringLiteral("endswith")) return QStringLiteral("endsWith");
    if (comparison == QStringLiteral("istrue")) return QStringLiteral("isTrue");
    if (comparison == QStringLiteral("isfalse")) return QStringLiteral("isFalse");
    return comparison;
}

QString runtimeLimitComparison(const QString& editorMode)
{
    if (editorMode == QStringLiteral("betweenTolerance") ||
        editorMode == QStringLiteral("betweenLimits")) {
        return QStringLiteral("between");
    }
    return editorMode;
}

QString jsonValueText(const QJsonValue& value)
{
    if (value.isUndefined() || value.isNull()) {
        return {};
    }
    if (value.isString()) {
        return value.toString();
    }
    return QString::fromUtf8(
        QJsonDocument(QJsonArray{value}).toJson(QJsonDocument::Compact))
        .mid(1).chopped(1);
}

int decimalPlacesFromLiteral(const QString& source, bool& exceedsLimit)
{
    exceedsLimit = false;
    static const QRegularExpression decimalPattern(
        QStringLiteral(R"(^[+-]?(?:[0-9]+\.([0-9]+)|\.([0-9]+))$)"));
    const auto match = decimalPattern.match(source.trimmed());
    if (!match.hasMatch()) {
        return -1;
    }
    const auto digits = !match.captured(1).isEmpty()
        ? match.captured(1).size()
        : match.captured(2).size();
    exceedsLimit = digits > 15;
    return digits;
}

bool usesNumericExpectedPrecision(const QString& editorMode)
{
    return editorMode == QStringLiteral("betweenTolerance") ||
           editorMode == QStringLiteral("equal") ||
           editorMode == QStringLiteral("notEqual") ||
           editorMode == QStringLiteral("greaterThan") ||
           editorMode == QStringLiteral("greaterOrEqual") ||
           editorMode == QStringLiteral("lessThan") ||
           editorMode == QStringLiteral("lessOrEqual");
}

QString limitExpectedEditorText(const QJsonObject& parameters)
{
    const auto expected = parameters.value(QStringLiteral("expected"));
    const auto decimalPlacesValue = parameters.value(QStringLiteral("decimalPlaces"));
    if (!expected.isDouble() || !decimalPlacesValue.isDouble()) {
        return jsonValueText(expected);
    }
    const auto decimalPlacesNumber = decimalPlacesValue.toDouble(-1.0);
    if (!std::isfinite(decimalPlacesNumber) ||
        std::trunc(decimalPlacesNumber) != decimalPlacesNumber ||
        decimalPlacesNumber < 0.0 || decimalPlacesNumber > 15.0) {
        return jsonValueText(expected);
    }
    return QString::number(
        expected.toDouble(), 'f', static_cast<int>(decimalPlacesNumber));
}

bool integerLiteralRequiresText(const QString& text)
{
    if (text.isEmpty()) {
        return false;
    }
    int index = 0;
    if (text.front() == QLatin1Char('-')) {
        index = 1;
    }
    if (index >= text.size()) {
        return false;
    }
    for (; index < text.size(); ++index) {
        if (text.at(index) < QLatin1Char('0') ||
            text.at(index) > QLatin1Char('9')) {
            return false;
        }
    }

    constexpr qint64 maximumExactInteger = 9007199254740992LL;
    bool ok = false;
    if (text.startsWith(QLatin1Char('-'))) {
        const auto value = text.toLongLong(&ok, 10);
        return !ok || value < -maximumExactInteger;
    }
    const auto value = text.toULongLong(&ok, 10);
    return !ok || value > static_cast<quint64>(maximumExactInteger);
}

QJsonValue jsonScalarFromText(const QString& source)
{
    const auto text = source.trimmed();
    if (text.isEmpty()) {
        return QJsonValue(QJsonValue::Undefined);
    }
    if (integerLiteralRequiresText(text)) {
        return text;
    }
    const auto parsed = QJsonDocument::fromJson(
        (QStringLiteral("[") + text + QStringLiteral("]")).toUtf8());
    if (parsed.isArray() && parsed.array().size() == 1 &&
        !parsed.array().first().isObject() && !parsed.array().first().isArray()) {
        return parsed.array().first();
    }
    return text;
}

void insertOrRemoveScalar(QJsonObject& object,
                          const QString& key,
                          const QString& text)
{
    const auto value = jsonScalarFromText(text);
    if (value.isUndefined()) {
        object.remove(key);
    } else {
        object.insert(key, value);
    }
}

QString stepOutputTooltip(const StepOutputExpressionCandidate& candidate)
{
    auto details = pluginParameterTypeName(candidate.type);
    if (!candidate.unit.isEmpty()) {
        details += QStringLiteral(" / %1").arg(candidate.unit);
    }
    return details;
}

template <typename AddExpression>
void appendStepOutputExpressionMenus(
    QMenu* root,
    const QVector<StepOutputExpressionCandidate>& candidates,
    AddExpression addExpression)
{
    QHash<QString, QMenu*> phaseMenus;
    QHash<QString, QMenu*> stepMenus;
    for (const auto& candidate : candidates) {
        const auto phase = candidate.phase.trimmed().isEmpty()
            ? QStringLiteral("MAIN")
            : candidate.phase.trimmed().toUpper();
        auto* phaseMenu = phaseMenus.value(phase, nullptr);
        if (!phaseMenu) {
            phaseMenu = root->addMenu(phase);
            phaseMenu->setObjectName(QStringLiteral("expressionPhaseMenu.%1").arg(phase));
            phaseMenus.insert(phase, phaseMenu);
        }

        auto hierarchy = candidate.stepHierarchy;
        auto pathSegments = candidate.stepPath.split(
            QLatin1Char('.'), Qt::SkipEmptyParts);
        if (hierarchy.isEmpty()) {
            hierarchy.push_back(
                QStringLiteral("%1 - %2").arg(candidate.stepPath, candidate.stepName));
            pathSegments = {candidate.stepPath};
        }

        auto* parentMenu = phaseMenu;
        auto hierarchyKey = phase;
        for (int index = 0; index < hierarchy.size(); ++index) {
            const auto pathSegment = index < pathSegments.size()
                ? pathSegments[index]
                : hierarchy[index];
            hierarchyKey += QStringLiteral("/%1").arg(pathSegment);
            auto* stepMenu = stepMenus.value(hierarchyKey, nullptr);
            if (!stepMenu) {
                stepMenu = parentMenu->addMenu(hierarchy[index]);
                stepMenu->setProperty("stepPath", pathSegments.mid(0, index + 1).join('.'));
                stepMenus.insert(hierarchyKey, stepMenu);
            }
            parentMenu = stepMenu;
        }

        addExpression(parentMenu, candidate, stepOutputTooltip(candidate));
    }
}

struct ExpressionPickerLocation {
    QString phase;
    QString parentStepPath;
};

ExpressionPickerLocation expressionPickerLocation(
    const QJsonObject& sequence,
    const SequenceItemPath& currentPath)
{
    ExpressionPickerLocation location;
    const auto groups = sequence.value(QStringLiteral("groups")).toArray();
    if (!currentPath.isValid() || currentPath.groupIndex < 0 ||
        currentPath.groupIndex >= groups.size() ||
        !groups[currentPath.groupIndex].isObject()) {
        return location;
    }

    const auto group = groups[currentPath.groupIndex].toObject();
    auto kind = group.value(QStringLiteral("kind")).toString().trimmed().toLower();
    kind.remove(QLatin1Char('-'));
    kind.remove(QLatin1Char('_'));
    location.phase = kind == QStringLiteral("setup")
        ? QStringLiteral("SETUP")
        : kind == QStringLiteral("cleanup")
            ? QStringLiteral("CLEANUP")
            : QStringLiteral("MAIN");

    QStringList parentSegments;
    auto steps = group.value(QStringLiteral("steps")).toArray();
    for (int depth = 0; depth + 1 < currentPath.stepIndices.size(); ++depth) {
        const int row = currentPath.stepIndices[depth];
        if (row < 0 || row >= steps.size() || !steps[row].isObject()) {
            break;
        }
        const auto step = steps[row].toObject();
        const auto id = step.value(QStringLiteral("id")).toString().trimmed();
        const auto key = step.value(QStringLiteral("key")).toString().trimmed();
        const auto segment = depth == 0 || key.isEmpty() ? id : key;
        if (!segment.isEmpty()) {
            parentSegments.push_back(segment);
        }
        steps = step.value(QStringLiteral("steps")).toArray();
    }
    location.parentStepPath = parentSegments.join(QLatin1Char('.'));
    return location;
}

constexpr int ExpressionPhaseRole = Qt::UserRole + 1;
constexpr int ExpressionStepPathRole = Qt::UserRole + 2;

void styleExpressionPickerButton(QToolButton* button)
{
    if (!button) {
        return;
    }
    button->setCursor(Qt::PointingHandCursor);
    button->setStyleSheet(QStringLiteral(
        "QToolButton { background: #f9fafb; color: #2d3943; "
        "border: 1px solid #98a5b1; border-radius: 4px; font-weight: 600; }"
        "QToolButton:hover { background: #e8f3f9; border-color: #568aa7; }"
        "QToolButton:pressed { background: #d8eaf4; border-color: #3f7898; }"
        "QToolButton:disabled { background: #f1f2f3; color: #a7adb3; "
        "border-color: #d4d8dc; }"));
}

class ExpressionPickerDialog final : public QDialog
{
public:
    explicit ExpressionPickerDialog(const QMenu* sourceMenu,
                                    const ExpressionPickerLocation& preferredLocation,
                                    QWidget* parent = nullptr)
        : QDialog(parent)
    {
        setObjectName(QStringLiteral("expressionPickerDialog"));
        setWindowTitle(tr("Select Value"));
        setWindowFlag(Qt::WindowContextHelpButtonHint, false);
        setModal(true);
        setMinimumSize(640, 380);
        resize(780, 470);
        setSizeGripEnabled(true);

        auto* root = new QVBoxLayout(this);
        root->setContentsMargins(16, 16, 16, 14);
        root->setSpacing(10);

        auto* navigation = new QHBoxLayout;
        navigation->setContentsMargins(0, 0, 0, 0);
        navigation->setSpacing(8);
        m_backButton = new QToolButton(this);
        m_backButton->setObjectName(QStringLiteral("expressionPickerBackButton"));
        m_backButton->setIcon(style()->standardIcon(QStyle::SP_ArrowBack));
        m_backButton->setToolTip(tr("Back one level"));
        m_backButton->setFixedSize(30, 28);
        m_backButton->setEnabled(false);
        navigation->addWidget(m_backButton);
        m_pathLabel = new QLabel(this);
        m_pathLabel->setObjectName(QStringLiteral("expressionPickerPath"));
        navigation->addWidget(m_pathLabel, 1);
        root->addLayout(navigation);

        m_model = new QStandardItemModel(this);
        appendMenuItems(m_model->invisibleRootItem(), sourceMenu);
        m_emptyRootItem = new QStandardItem;
        m_emptyRootItem->setFlags(Qt::NoItemFlags);
        m_model->appendRow(m_emptyRootItem);

        auto* columnsFrame = new QWidget(this);
        columnsFrame->setObjectName(QStringLiteral("expressionPickerColumnsFrame"));
        auto* columnsLayout = new QHBoxLayout(columnsFrame);
        columnsLayout->setContentsMargins(0, 0, 0, 0);
        columnsLayout->setSpacing(1);
        for (int column = 0; column < VisibleExpressionColumns; ++column) {
            auto* view = new QListView(columnsFrame);
            view->setObjectName(
                QStringLiteral("expressionPickerColumn%1").arg(column));
            view->setModel(m_model);
            view->setEditTriggers(QAbstractItemView::NoEditTriggers);
            view->setSelectionMode(QAbstractItemView::SingleSelection);
            view->setSelectionBehavior(QAbstractItemView::SelectRows);
            view->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
            view->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
            view->setTextElideMode(Qt::ElideRight);
            view->setUniformItemSizes(true);
            view->setEnabled(false);
            columnsLayout->addWidget(view, 1);
            m_columnViews[static_cast<std::size_t>(column)] = view;
        }
        root->addWidget(columnsFrame, 1);

        auto* footer = new QHBoxLayout;
        footer->setContentsMargins(0, 0, 0, 0);
        footer->setSpacing(10);
        m_preview = new QLineEdit(this);
        m_preview->setObjectName(QStringLiteral("expressionPickerPreview"));
        m_preview->setReadOnly(true);
        m_preview->setMinimumHeight(34);
        footer->addWidget(m_preview, 1);

        auto* buttons = new QDialogButtonBox(QDialogButtonBox::Cancel, this);
        m_insertButton = buttons->addButton(tr("Insert"), QDialogButtonBox::AcceptRole);
        m_insertButton->setObjectName(QStringLiteral("expressionPickerInsertButton"));
        m_insertButton->setEnabled(false);
        footer->addWidget(buttons);
        root->addLayout(footer);

        for (int column = 0; column < VisibleExpressionColumns; ++column) {
            auto* view = m_columnViews[static_cast<std::size_t>(column)];
            connect(view->selectionModel(), &QItemSelectionModel::currentChanged,
                    this, [this, column](const QModelIndex& current) {
                handleColumnSelection(column, current);
            });
            connect(view, &QListView::doubleClicked,
                    this, [this](const QModelIndex& index) {
                const auto expression = index.data(Qt::UserRole).toString();
                if (expression.isEmpty()) {
                    return;
                }
                m_selectedExpression = expression;
                accept();
            });
        }
        connect(m_insertButton, &QPushButton::clicked, this, &QDialog::accept);
        connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
        connect(m_backButton, &QToolButton::clicked,
                this, [this] { navigateBack(); });

        const auto previousSource = lastStepSourceIndex();
        if (previousSource.isValid()) {
            initializeBrowser(previousSource);
        } else {
            const auto preferred = preferredIndex(preferredLocation);
            if (preferred.isValid()) {
                initializeBrowser(preferred);
            } else {
                setBrowserRoot({}, {});
            }
        }

        setStyleSheet(QStringLiteral(
            "QDialog#expressionPickerDialog { background: #f5f6f8; }"
            "QWidget#expressionPickerColumnsFrame { background: #d6dbe2; "
            "border: 1px solid #d6dbe2; border-radius: 5px; }"
            "QListView#expressionPickerColumn0, QListView#expressionPickerColumn1, "
            "QListView#expressionPickerColumn2 { background: #ffffff; "
            "border: none; outline: none; }"
            "QListView#expressionPickerColumn0::item, "
            "QListView#expressionPickerColumn1::item, "
            "QListView#expressionPickerColumn2::item { min-height: 30px; "
            "padding: 5px 9px; color: #252a31; }"
            "QListView#expressionPickerColumn0::item:hover, "
            "QListView#expressionPickerColumn1::item:hover, "
            "QListView#expressionPickerColumn2::item:hover { background: #edf5fa; }"
            "QListView#expressionPickerColumn0::item:selected, "
            "QListView#expressionPickerColumn1::item:selected, "
            "QListView#expressionPickerColumn2::item:selected { "
            "background: #d8ebf7; color: #1f252b; }"
            "QToolButton#expressionPickerBackButton { background: #ffffff; "
            "border: 1px solid #c4cbd3; border-radius: 4px; }"
            "QToolButton#expressionPickerBackButton:hover { background: #edf5fa; "
            "border-color: #8baec3; }"
            "QLabel#expressionPickerPath { color: #5d6670; font-weight: 600; }"
            "QLineEdit#expressionPickerPreview { background: #ffffff; "
            "border: 1px solid #d6dbe2; border-radius: 4px; padding: 6px 9px; }"
            "QPushButton#expressionPickerInsertButton { min-width: 88px; "
            "min-height: 32px; padding: 0 14px; }"));
    }

    QString selectedExpression() const
    {
        return m_selectedExpression;
    }

private:
    static void appendMenuItems(QStandardItem* parent, const QMenu* menu)
    {
        if (!parent || !menu) {
            return;
        }
        for (auto* action : menu->actions()) {
            if (!action || action->isSeparator()) {
                continue;
            }
            auto* item = new QStandardItem(action->icon(), action->text());
            item->setEditable(false);
            item->setEnabled(action->isEnabled());
            item->setToolTip(action->toolTip());
            if (const auto* childMenu = action->menu()) {
                const auto phasePrefix = QStringLiteral("expressionPhaseMenu.");
                if (childMenu->objectName().startsWith(phasePrefix)) {
                    item->setData(childMenu->objectName().mid(phasePrefix.size()),
                                  ExpressionPhaseRole);
                }
                const auto stepPath = childMenu->property("stepPath").toString();
                if (!stepPath.isEmpty()) {
                    item->setData(stepPath, ExpressionStepPathRole);
                }
                appendMenuItems(item, childMenu);
            } else {
                item->setData(action->data().toString(), Qt::UserRole);
            }
            parent->appendRow(item);
        }
    }

    static QString phaseForIndex(QModelIndex index)
    {
        while (index.isValid()) {
            const auto phase = index.data(ExpressionPhaseRole).toString();
            if (!phase.isEmpty()) {
                return phase;
            }
            index = index.parent();
        }
        return {};
    }

    static QModelIndex findLocationIndex(
        const QAbstractItemModel* model,
        int role,
        const QString& value,
        const QString& phase,
        const QModelIndex& parent = {})
    {
        if (!model) {
            return {};
        }
        for (int row = 0; row < model->rowCount(parent); ++row) {
            const auto index = model->index(row, 0, parent);
            if (index.data(role).toString() == value &&
                (phase.isEmpty() || phaseForIndex(index) == phase)) {
                return index;
            }
            const auto nested = findLocationIndex(
                model, role, value, phase, index);
            if (nested.isValid()) {
                return nested;
            }
        }
        return {};
    }

    QModelIndex preferredIndex(const ExpressionPickerLocation& location) const
    {
        auto stepPath = location.parentStepPath;
        while (!stepPath.isEmpty()) {
            const auto index = findLocationIndex(
                m_model, ExpressionStepPathRole, stepPath, location.phase);
            if (index.isValid()) {
                return index;
            }
            const int separator = stepPath.lastIndexOf(QLatin1Char('.'));
            stepPath = separator < 0 ? QString{} : stepPath.left(separator);
        }
        return findLocationIndex(
            m_model, ExpressionPhaseRole, location.phase, location.phase);
    }

    static void findLastStepSource(
        const QAbstractItemModel* model,
        const QModelIndex& parent,
        QModelIndex& lastSource)
    {
        if (!model) {
            return;
        }
        for (int row = 0; row < model->rowCount(parent); ++row) {
            const auto index = model->index(row, 0, parent);
            if (!index.data(Qt::UserRole).toString().isEmpty() &&
                !phaseForIndex(index).isEmpty()) {
                lastSource = index.parent();
            }
            findLastStepSource(model, index, lastSource);
        }
    }

    QModelIndex lastStepSourceIndex() const
    {
        QModelIndex source;
        findLastStepSource(m_model, {}, source);
        return source;
    }

    static QStringList indexPath(QModelIndex index)
    {
        QStringList path;
        while (index.isValid()) {
            path.prepend(index.data().toString());
            index = index.parent();
        }
        return path;
    }

    void updateExpressionSelection(const QModelIndex& current)
    {
        m_selectedExpression = current.data(Qt::UserRole).toString();
        m_preview->setText(m_selectedExpression);
        m_insertButton->setEnabled(!m_selectedExpression.isEmpty());
    }

    void updateNavigation()
    {
        const QModelIndex root = m_browserRoot;
        m_backButton->setEnabled(!m_rootHistory.isEmpty());
        if (!root.isValid()) {
            m_pathLabel->clear();
            m_pathLabel->setToolTip({});
            return;
        }
        const auto path = indexPath(root);
        m_pathLabel->setText(QStringLiteral("... / %1").arg(root.data().toString()));
        m_pathLabel->setToolTip(path.join(QStringLiteral(" / ")));
    }

    static QVector<QModelIndex> pathBelowRoot(const QModelIndex& root,
                                              QModelIndex focus)
    {
        QVector<QModelIndex> path;
        while (focus.isValid() && focus != root) {
            path.prepend(focus);
            focus = focus.parent();
        }
        if (focus != root) {
            path.clear();
        }
        return path;
    }

    void configureColumn(int column,
                         const QModelIndex& root,
                         bool active)
    {
        auto* view = m_columnViews[static_cast<std::size_t>(column)];
        view->selectionModel()->clear();
        if (!active) {
            view->setRootIndex(m_emptyRootItem->index());
            view->setEnabled(false);
            return;
        }

        view->setRootIndex(root);
        if (!root.isValid()) {
            view->setRowHidden(m_emptyRootItem->row(), true);
        }
        view->setEnabled(m_model->rowCount(root) > 0);
    }

    void clearColumnsAfter(int column)
    {
        for (int next = column + 1; next < VisibleExpressionColumns; ++next) {
            configureColumn(next, {}, false);
        }
    }

    void handleColumnSelection(int column, const QModelIndex& current)
    {
        if (m_adjustingColumns) {
            return;
        }

        if (current.isValid() && m_model->hasChildren(current) &&
            column == VisibleExpressionColumns - 1) {
            drillInto(current);
            return;
        }

        m_adjustingColumns = true;
        clearColumnsAfter(column);
        if (current.isValid() && m_model->hasChildren(current) &&
            column + 1 < VisibleExpressionColumns) {
            configureColumn(column + 1, current, true);
        }
        m_adjustingColumns = false;
        updateExpressionSelection(current);
    }

    void setBrowserRoot(const QModelIndex& root, const QModelIndex& focus)
    {
        m_adjustingColumns = true;
        m_browserRoot = root;
        for (int column = 0; column < VisibleExpressionColumns; ++column) {
            configureColumn(column, {}, false);
        }
        configureColumn(0, root, true);

        const auto path = pathBelowRoot(root, focus);
        const int visiblePathSize = std::min(
            static_cast<int>(path.size()), VisibleExpressionColumns);
        for (int column = 0; column < visiblePathSize; ++column) {
            auto* view = m_columnViews[static_cast<std::size_t>(column)];
            const auto index = path[column];
            view->setCurrentIndex(index);
            view->scrollTo(index, QAbstractItemView::PositionAtCenter);
            if (m_model->hasChildren(index) &&
                column + 1 < VisibleExpressionColumns) {
                configureColumn(column + 1, index, true);
            }
        }
        m_adjustingColumns = false;
        updateExpressionSelection(focus);
        updateNavigation();
    }

    void initializeBrowser(const QModelIndex& focus)
    {
        const auto root = focus.parent().parent();
        m_rootHistory.clear();
        if (root.isValid()) {
            QVector<QPersistentModelIndex> ancestry;
            for (auto index = root; index.isValid(); index = index.parent()) {
                ancestry.prepend(QPersistentModelIndex(index));
            }
            m_rootHistory.push_back({});
            for (int index = 0; index + 1 < ancestry.size(); ++index) {
                m_rootHistory.push_back(ancestry[index]);
            }
        }
        setBrowserRoot(root, focus);
    }

    void drillInto(const QModelIndex& current)
    {
        const auto newRoot = current.parent().parent();
        const QModelIndex currentRoot = m_browserRoot;
        if (newRoot == currentRoot) {
            return;
        }
        m_rootHistory.push_back(m_browserRoot);
        setBrowserRoot(newRoot, current);
    }

    void navigateBack()
    {
        if (m_rootHistory.isEmpty()) {
            return;
        }
        const QModelIndex oldRoot = m_browserRoot;
        const QModelIndex previousRoot = m_rootHistory.takeLast();
        setBrowserRoot(previousRoot, oldRoot);
    }

    static constexpr int VisibleExpressionColumns = 3;

    std::array<QListView*, VisibleExpressionColumns> m_columnViews{};
    QStandardItemModel* m_model = nullptr;
    QStandardItem* m_emptyRootItem = nullptr;
    QToolButton* m_backButton = nullptr;
    QLabel* m_pathLabel = nullptr;
    QLineEdit* m_preview = nullptr;
    QPushButton* m_insertButton = nullptr;
    QPersistentModelIndex m_browserRoot;
    QVector<QPersistentModelIndex> m_rootHistory;
    bool m_adjustingColumns = false;
    QString m_selectedExpression;
};

QString selectExpressionFromMenu(
    const QMenu* menu,
    const ExpressionPickerLocation& preferredLocation,
    QWidget* parent)
{
    ExpressionPickerDialog dialog(menu, preferredLocation, parent);
    return dialog.exec() == QDialog::Accepted
        ? dialog.selectedExpression()
        : QString{};
}

} // namespace

StepPropertyEditor::StepPropertyEditor(SequenceDocument* document,
                                       QWidget* parent)
    : QWidget(parent)
    , m_document(document)
{
    setObjectName(QStringLiteral("stepPropertyEditor"));
    setMinimumWidth(240);

    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(8, 0, 0, 0);
    root->setSpacing(6);

    m_titleLabel = new QLabel(tr("Properties"), this);
    m_titleLabel->setObjectName(QStringLiteral("propertyEditorTitle"));
    auto titleFont = m_titleLabel->font();
    titleFont.setBold(true);
    m_titleLabel->setFont(titleFont);
    root->addWidget(m_titleLabel);

    m_emptyLabel = new QLabel(tr("No sequence item selected"), this);
    root->addWidget(m_emptyLabel);

    m_tabs = new QTabWidget(this);
    buildGeneralPage();
    serviceAdminStartupAnimation();
    buildDataPage();
    serviceAdminStartupAnimation();
    buildPolicyPage();
    serviceAdminStartupAnimation();
    root->addWidget(m_tabs, 1);

    m_errorLabel = new QLabel(this);
    m_errorLabel->setObjectName(QStringLiteral("propertyErrorLabel"));
    m_errorLabel->setWordWrap(true);
    m_errorLabel->setStyleSheet(QStringLiteral("color: #d9534f;"));
    m_errorLabel->hide();
    root->addWidget(m_errorLabel);

    connect(m_kindCombo,
            &QComboBox::currentIndexChanged,
            this,
            [this] {
                if (m_loading) {
                    return;
                }
                const auto kind = m_kindCombo->currentData().toString();
                updateKindRows();
                if (kind == QStringLiteral("operatorPrompt")) {
                    rebuildPromptImageChoices(selectedPromptImage());
                    rebuildPromptCloseStepChoices(selectedPromptCloseStep());
                }
            });
    connect(m_limitComparisonCombo,
            &QComboBox::currentIndexChanged,
            this,
            [this] { if (!m_loading) updateLimitRows(); });
    connect(m_promptModeCombo,
            &QComboBox::currentIndexChanged,
            this,
            [this] { if (!m_loading) updateKindRows(); });
    connect(m_loopTypeCombo,
            &QComboBox::currentIndexChanged,
            this,
            [this] { if (!m_loading) updateLoopRows(); });
    connect(m_periodicEnabledCheck,
            &QCheckBox::toggled,
            this,
            [this] { if (!m_loading) updateKindRows(); });
    for (auto* policyCombo : {m_onFailPolicyCombo,
                              m_onErrorPolicyCombo,
                              m_onTimeoutPolicyCombo}) {
        connect(policyCombo,
                &QComboBox::currentIndexChanged,
                this,
                [this] { updateFailureJumpVisibility(); });
    }
    const auto rebuildCallEditors = [this] {
        rebuildFunctionChoices();
        rebuildDeviceChoices();
        rebuildPluginInputEditors();
        updateKindRows();
    };
    connect(m_moduleIdEdit, &QLineEdit::editingFinished,
            this, rebuildCallEditors);
    connect(m_functionEdit, &QComboBox::currentIndexChanged,
            this, [this] {
                if (m_loading) {
                    return;
                }
                rebuildDeviceChoices();
                rebuildPluginInputEditors();
                updateKindRows();
            });
    connect(m_deviceIdCombo, &QComboBox::currentIndexChanged,
            this, [this] {
                if (m_loading) {
                    return;
                }
                QJsonObject inputs;
                QString ignoredError;
                if (!parseObjectText(m_inputsEdit->toPlainText(), inputs,
                                     ignoredError)) {
                    return;
                }
                const auto deviceId = m_deviceIdCombo->currentData().toString();
                if (deviceId.isEmpty()) {
                    inputs.remove(QStringLiteral("deviceId"));
                } else {
                    inputs.insert(QStringLiteral("deviceId"), deviceId);
                }
                m_inputsEdit->setPlainText(objectText(inputs));
                rebuildFunctionChoices(
                    m_functionEdit->currentData().toString());
                rebuildPluginInputEditors();
                updateAdvancedJsonVisibility();
            });

    for (auto* child : findChildren<QWidget*>()) {
        if (qobject_cast<QLineEdit*>(child) ||
            qobject_cast<QPlainTextEdit*>(child) ||
            qobject_cast<QComboBox*>(child) ||
            qobject_cast<QSpinBox*>(child) ||
            qobject_cast<QDoubleSpinBox*>(child)) {
            auto policy = child->sizePolicy();
            policy.setHorizontalPolicy(QSizePolicy::Ignored);
            child->setSizePolicy(policy);
            child->setMinimumWidth(0);
        }
        observeDraftWidget(child);
    }

    setCurrentItem({});
}

void StepPropertyEditor::resizeEvent(QResizeEvent* event)
{
    QWidget::resizeEvent(event);
    applyResponsiveFormLayout();
}

void StepPropertyEditor::applyResponsiveFormLayout()
{
    const bool compact = width() < 330;
    if (m_compactFormLayout == compact) {
        return;
    }
    m_compactFormLayout = compact;

    const auto policy = compact
        ? QFormLayout::WrapAllRows
        : QFormLayout::WrapLongRows;
    for (auto* form : {m_generalForm, m_dataForm, m_pluginInputsForm,
                       m_advancedJsonForm, m_policyForm}) {
        if (form) {
            form->setRowWrapPolicy(policy);
            form->invalidate();
        }
    }
}

void StepPropertyEditor::addInspectableRow(QFormLayout* form,
                                           const QString& label,
                                           QWidget* field,
                                           const QString& fieldPath,
                                           const QString& displayName)
{
    auto* labelWidget = new QWidget(form->parentWidget());
    auto* layout = new QHBoxLayout(labelWidget);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(4);
    layout->addStretch(1);

    auto* text = new QLabel(label, labelWidget);
    layout->addWidget(text);

    auto* inspect = new QToolButton(labelWidget);
    auto objectSuffix = fieldPath;
    objectSuffix.replace(QLatin1Char('.'), QLatin1Char('_'));
    inspect->setObjectName(
        QStringLiteral("inspectField_%1").arg(objectSuffix));
    inspect->setProperty("picoateInspectionButton", true);
    inspect->setProperty("picoateDraftObserved", true);
    inspect->setProperty("inspectionFieldPath", fieldPath);
    inspect->setCheckable(true);
    inspect->setChecked(m_inspectionField == fieldPath);
    inspect->setAutoRaise(true);
    inspect->setFocusPolicy(Qt::NoFocus);
    inspect->setFixedSize(22, 22);
    inspect->setIcon(QIcon(QStringLiteral(":/icons/eye.svg")));
    inspect->setIconSize(QSize(15, 15));
    const auto friendlyName = displayName.isEmpty() ? label : displayName;
    inspect->setToolTip(tr("Show '%1' for every Flow item")
                            .arg(friendlyName));
    inspect->setStyleSheet(QStringLiteral(
        "QToolButton { border: 0; border-radius: 4px; padding: 2px; }"
        "QToolButton:hover { background: #e8f2f8; }"
        "QToolButton:checked { background: #d5eaf6; }"));
    connect(inspect, &QToolButton::clicked, this,
            [this, fieldPath, friendlyName] {
                const bool clear = m_inspectionField == fieldPath;
                emit inspectionFieldRequested(clear ? QString{} : fieldPath,
                                              clear ? QString{} : friendlyName);
            });
    layout->addWidget(inspect);
    form->addRow(labelWidget, field);
}

SequenceItemPath StepPropertyEditor::currentPath() const
{
    return m_path;
}

bool StepPropertyEditor::hasPendingChanges() const
{
    return m_draftDirty && m_path.isValid() && !m_previewing;
}

void StepPropertyEditor::discardPendingChanges()
{
    if (hasPendingChanges()) {
        loadCurrentObject();
    }
}

void StepPropertyEditor::setInspectionField(const QString& fieldPath)
{
    m_inspectionField = fieldPath.trimmed();
    for (auto* button : findChildren<QToolButton*>()) {
        if (!button->property("picoateInspectionButton").toBool()) {
            continue;
        }
        const QSignalBlocker blocker(button);
        button->setChecked(
            button->property("inspectionFieldPath").toString() ==
            m_inspectionField);
    }
}

void StepPropertyEditor::observeDraftWidget(QWidget* widget)
{
    if (!widget || widget->property("picoateDraftObserved").toBool()) {
        return;
    }
    widget->setProperty("picoateDraftObserved", true);
    if (widget->property("picoateDisclosure").toBool()) {
        return;
    }
    if (auto* edit = qobject_cast<QLineEdit*>(widget)) {
        connect(edit, &QLineEdit::textChanged, this,
                [this] { markDraftDirty(); });
    } else if (auto* edit = qobject_cast<QPlainTextEdit*>(widget)) {
        connect(edit, &QPlainTextEdit::textChanged, this,
                [this] { markDraftDirty(); });
    } else if (auto* combo = qobject_cast<QComboBox*>(widget)) {
        connect(combo, &QComboBox::currentIndexChanged, this,
                [this] { markDraftDirty(); });
    } else if (auto* check = qobject_cast<QCheckBox*>(widget)) {
        connect(check, &QCheckBox::toggled, this,
                [this] { markDraftDirty(); });
    } else if (auto* button = qobject_cast<QAbstractButton*>(widget)) {
        connect(button, &QAbstractButton::toggled, this,
                [this] { markDraftDirty(); });
    } else if (auto* spin = qobject_cast<QSpinBox*>(widget)) {
        connect(spin, &QSpinBox::valueChanged, this,
                [this] { markDraftDirty(); });
    } else if (auto* spin = qobject_cast<QDoubleSpinBox*>(widget)) {
        connect(spin, &QDoubleSpinBox::valueChanged, this,
                [this] { markDraftDirty(); });
    }
}

void StepPropertyEditor::markDraftDirty()
{
    if (!m_loading && m_editable && m_path.isValid() && !m_previewing) {
        setDraftDirty(true);
    }
}

void StepPropertyEditor::setDraftDirty(bool dirty)
{
    if (m_draftDirty == dirty) {
        return;
    }
    m_draftDirty = dirty;
    m_titleLabel->setText(m_previewing
        ? tr("Function Preview")
        : (dirty ? tr("Properties *") : tr("Properties")));
    emit pendingChangesChanged(dirty);
}

void StepPropertyEditor::setCurrentItem(const SequenceItemPath& path)
{
    if (!m_previewing && path == m_path && m_document) {
        const auto currentObject = path.isValid()
            ? m_document->objectAt(path)
            : QJsonObject{};
        if (currentObject == m_sourceObject) {
            return;
        }
    }
    m_previewing = false;
    m_previewObject = {};
    m_path = path;
    loadCurrentObject();
}

void StepPropertyEditor::setPreviewObject(QJsonObject object)
{
    m_path = {};
    m_previewObject = std::move(object);
    m_previewing = !m_previewObject.isEmpty();
    loadCurrentObject();
}

void StepPropertyEditor::setEditable(bool editable)
{
    if (m_editable == editable) {
        return;
    }
    m_editable = editable;
    applyEditableState();
}

void StepPropertyEditor::applyEditableState()
{
    const bool hasObject = !m_sourceObject.isEmpty();
    const bool canEdit = m_editable && m_path.isValid() && !m_previewing;
    m_tabs->setEnabled(hasObject);
    for (auto* edit : m_tabs->findChildren<QLineEdit*>()) {
        edit->setReadOnly(!canEdit || edit->property("stationInherited").toBool());
    }
    for (auto* edit : m_tabs->findChildren<QPlainTextEdit*>()) {
        edit->setReadOnly(!canEdit);
    }
    for (auto* combo : m_tabs->findChildren<QComboBox*>()) {
        combo->setEnabled(canEdit && !combo->property("stationInherited").toBool());
    }
    if (m_deviceIdCombo) {
        bool hasCompatibleDevice = false;
        for (int index = 0; index < m_deviceIdCombo->count(); ++index) {
            hasCompatibleDevice = hasCompatibleDevice ||
                m_deviceIdCombo->itemData(index).isValid();
        }
        m_deviceIdCombo->setEnabled(canEdit && hasCompatibleDevice);
    }
    for (auto* check : m_tabs->findChildren<QCheckBox*>()) {
        check->setEnabled(canEdit && !check->property("stationInherited").toBool());
    }
    for (auto* button : m_tabs->findChildren<QAbstractButton*>()) {
        if (dynamic_cast<OnOffSwitch*>(button)) {
            button->setEnabled(
                canEdit && !button->property("stationInherited").toBool());
        }
    }
    for (auto* spin : m_tabs->findChildren<QSpinBox*>()) {
        spin->setEnabled(canEdit && !spin->property("stationInherited").toBool());
    }
    for (auto* spin : m_tabs->findChildren<QDoubleSpinBox*>()) {
        spin->setEnabled(canEdit && !spin->property("stationInherited").toBool());
    }
    for (auto* button : m_tabs->findChildren<QToolButton*>()) {
        if (button->property("picoateInspectionButton").toBool()) {
            button->setEnabled(hasObject);
            continue;
        }
        button->setEnabled(canEdit && !button->property("stationInherited").toBool());
    }
    if (m_advancedJsonToggle) {
        m_advancedJsonToggle->setEnabled(hasObject);
    }
}

void StepPropertyEditor::setPluginRegistry(QVector<PluginManifest> plugins)
{
    const auto hasDataParser = std::any_of(
        plugins.cbegin(), plugins.cend(), [](const PluginManifest& plugin) {
            return plugin.moduleId.compare(
                       QStringLiteral("builtin.data-parser"),
                       Qt::CaseInsensitive) == 0;
        });
    if (!hasDataParser) {
        plugins.push_back(builtInDataParserManifest());
    }
    const auto hasValueTools = std::any_of(
        plugins.cbegin(), plugins.cend(), [](const PluginManifest& plugin) {
            return plugin.moduleId.compare(
                       QStringLiteral("builtin.value-tools"),
                       Qt::CaseInsensitive) == 0;
        });
    if (!hasValueTools) {
        plugins.push_back(builtInValueToolsManifest());
    }
    m_plugins = std::move(plugins);
    rebuildFunctionChoices();
    serviceAdminStartupAnimation();
    rebuildDeviceChoices();
    serviceAdminStartupAnimation();
    rebuildPluginInputEditors();
    serviceAdminStartupAnimation();
    updateAdvancedJsonVisibility();
}

bool StepPropertyEditor::focusField(const QString& fieldPath)
{
    if (!m_path.isValid() || fieldPath.isEmpty()) {
        return false;
    }

    const auto segments = fieldPath.split('.', Qt::SkipEmptyParts);
    if (segments.isEmpty()) {
        return false;
    }
    QString field = segments.first();
    const int arrayMarker = field.indexOf('[');
    if (arrayMarker >= 0) {
        field.truncate(arrayMarker);
    }
    const QString nested = segments.size() > 1 ? segments.at(1) : QString();

    QWidget* widget = nullptr;
    int tabIndex = 0;
    if (field == "id") widget = m_idEdit;
    else if (field == "key") widget = m_keyEdit;
    else if (field == "name") widget = m_nameEdit;
    else if (field == "kind" || field == "type") widget = m_kindCombo;
    else if (field == "enabled") widget = m_enabledCheck;
    else if (field == "alwaysRun") widget = m_alwaysRunCheck;
    else if (field == "resultRecording") widget = m_resultRecordingCheck;
    else if (field == "checkpointBefore") widget = m_checkpointBeforeCheck;
    else if (field == "checkpointAfter") widget = m_checkpointAfterCheck;
    else if (field == "tags") widget = m_tagsEdit;
    else if (field == "moduleId") { widget = m_moduleIdEdit; tabIndex = 1; }
    else if (field == "function") { widget = m_functionEdit; tabIndex = 1; }
    else if (field == "inputs") {
        if (nested == QStringLiteral("actual")) {
            widget = m_limitActualEdit;
        } else if (nested == QStringLiteral("condition")) {
            widget = m_counterConditionEdit;
        } else if (nested == QStringLiteral("value")) {
            widget = m_aggregateValueEdit;
        } else if (nested == QStringLiteral("deviceId")) {
            widget = m_deviceIdCombo;
        } else {
            const auto editor = std::find_if(
                m_pluginInputEditors.cbegin(), m_pluginInputEditors.cend(),
                [&](const PluginInputEditor& item) {
                    return item.definition.key == nested;
                });
            if (editor != m_pluginInputEditors.cend()) {
                widget = editor->widget;
            } else {
                m_advancedJsonToggle->setChecked(true);
                widget = m_inputsEdit;
            }
        }
        tabIndex = 1;
    }
    else if (field == "parameters") {
        tabIndex = 1;
        if (!m_isGroup &&
            (m_kindCombo->currentData().toString() == QStringLiteral("limit") ||
             m_kindCombo->currentData().toString() == QStringLiteral("break"))) {
            if (nested == QStringLiteral("comparison")) widget = m_limitComparisonCombo;
            else if (nested == QStringLiteral("expected")) widget = m_limitExpectedEdit;
            else if (nested == QStringLiteral("lower") || nested == QStringLiteral("lowerLimit")) widget = m_limitLowerEdit;
            else if (nested == QStringLiteral("upper") || nested == QStringLiteral("upperLimit")) widget = m_limitUpperEdit;
            else if (nested == QStringLiteral("tolerance")) widget = m_limitToleranceSpin;
            else if (nested == QStringLiteral("inclusive")) widget = m_limitInclusiveCheck;
            else if (nested == QStringLiteral("measurementName")) widget = m_limitMeasurementNameEdit;
            else if (nested == QStringLiteral("unit")) widget = m_limitUnitEdit;
        }
        if (!widget) {
            const auto kind = m_kindCombo->currentData().toString();
            if (kind == QStringLiteral("counter")) {
                if (nested == QStringLiteral("mode")) widget = m_counterModeCombo;
                else if (nested == QStringLiteral("start")) widget = m_counterStartSpin;
                else if (nested == QStringLiteral("increment")) widget = m_counterIncrementSpin;
            }
        }
        if (!widget) {
            m_advancedJsonToggle->setChecked(true);
            widget = m_parametersEdit;
        }
    }
    else if (field == "ms") { widget = m_waitMsSpin; tabIndex = 1; }
    else if (field == "prompt") {
        tabIndex = 1;
        if (nested == "mode") widget = m_promptModeCombo;
        else if (nested == "title") widget = m_promptTitleEdit;
        else if (nested == "message") widget = m_promptMessageEdit;
        else if (nested == "image") widget = m_promptImageCombo;
        else if (nested == "confirmText") widget = m_promptConfirmTextEdit;
        else if (nested == "inputType") widget = m_promptInputTypeCombo;
        else if (nested == "inputPlaceholder") widget = m_promptInputPlaceholderEdit;
        else if (nested == "defaultValue") widget = m_promptDefaultValueEdit;
        else if (nested == "closeOnStep") widget = m_promptCloseOnStepCombo;
        else if (nested == "dialogKey") widget = m_promptDialogKeyEdit;
        else if (nested == "passText") widget = m_promptPassTextEdit;
        else if (nested == "failText") widget = m_promptFailTextEdit;
        else if (nested == "failureCode") widget = m_promptFailureCodeEdit;
        else if (nested == "timeoutMs") widget = m_promptTimeoutSpin;
        else widget = m_promptMessageEdit;
    }
    else if (field == "loop") {
        tabIndex = 1;
        if (nested == "type") widget = m_loopTypeCombo;
        else if (nested == "from") widget = m_loopFromSpin;
        else if (nested == "to") widget = m_loopToSpin;
        else if (nested == "step") widget = m_loopStepSpin;
        else if (nested == "intervalMs") widget = m_conditionIntervalSpin;
        else if (nested == "maxIterations") widget = m_conditionMaxIterationsSpin;
        else if (nested == "timeoutMs") widget = m_conditionTimeoutSpin;
        else if (nested == "iterationErrorPolicy") widget = m_conditionIterationErrorCombo;
        else widget = m_loopVariableEdit;
    } else if (field == "barrier") {
        tabIndex = 1;
        if (nested == "cohortId") widget = m_cohortIdEdit;
        else if (nested == "expectedUutCount") widget = m_expectedUutSpin;
        else if (nested == "quorumCount") widget = m_quorumCountSpin;
        else if (nested == "quorumRatio") widget = m_quorumRatioSpin;
        else if (nested == "arrivalTimeoutMs") widget = m_arrivalTimeoutSpin;
        else if (nested == "releaseTimeoutMs") widget = m_releaseTimeoutSpin;
        else if (nested == "arrivalPolicy") widget = m_arrivalPolicyCombo;
        else if (nested == "releasePolicy") widget = m_releasePolicyCombo;
        else if (nested == "failurePolicy") widget = m_failurePolicyCombo;
        else if (nested == "timeoutPolicy") widget = m_barrierTimeoutPolicyCombo;
        else if (nested == "releaseHeldResourcesOnWait") widget = m_releaseResourcesCheck;
        else widget = m_barrierNameEdit;
    } else if (field == "retry") {
        tabIndex = 2;
        if (nested == "delayMs") widget = m_retryDelaySpin;
        else if (nested == "retryWhen") widget = m_retryWhenEdit;
        else widget = m_maxAttemptsSpin;
    } else if (field == "timeout" || field == "timeoutMs") {
        widget = m_timeoutSpin;
        tabIndex = 2;
    } else if (field == "errorPolicy") {
        tabIndex = 1;
        if (nested == QStringLiteral("onFail")) widget = m_onFailPolicyCombo;
        else if (nested == QStringLiteral("onError")) widget = m_onErrorPolicyCombo;
        else if (nested == QStringLiteral("onTimeout")) widget = m_onTimeoutPolicyCombo;
        else if (nested == QStringLiteral("onFailTarget")) widget = m_onFailTargetCombo;
        else if (nested == QStringLiteral("onErrorTarget")) widget = m_onErrorTargetCombo;
        else if (nested == QStringLiteral("onTimeoutTarget")) widget = m_onTimeoutTargetCombo;
        else {
            m_advancedJsonToggle->setChecked(true);
            widget = m_errorPolicyEdit;
        }
    } else if (field == "resources") {
        widget = m_resourcesEdit;
        tabIndex = 2;
    } else if (field == "steps") {
        m_tabs->setCurrentIndex(1);
        m_tabs->setFocus(Qt::OtherFocusReason);
        return true;
    } else {
        static const QStringList barrierFields = {
            "barrierName", "cohortId", "expectedUutCount", "quorumCount",
            "quorumRatio", "arrivalTimeoutMs", "releaseTimeoutMs",
            "arrivalPolicy", "releasePolicy", "failurePolicy",
            "timeoutPolicy", "releaseHeldResourcesOnWait"};
        if (barrierFields.contains(field)) {
            return focusField(QStringLiteral("barrier.%1").arg(field));
        }
    }

    if (!widget) {
        return false;
    }
    m_tabs->setCurrentIndex(tabIndex);
    if (!widget->isVisibleTo(this)) {
        return false;
    }
    widget->setFocus(Qt::OtherFocusReason);
    if (auto* edit = qobject_cast<QLineEdit*>(widget)) {
        edit->selectAll();
    } else if (auto* edit = qobject_cast<QPlainTextEdit*>(widget)) {
        edit->moveCursor(QTextCursor::Start);
        edit->ensureCursorVisible();
    } else if (auto* spin = qobject_cast<QSpinBox*>(widget)) {
        spin->selectAll();
    } else if (auto* spin = qobject_cast<QDoubleSpinBox*>(widget)) {
        spin->selectAll();
    }
    for (QWidget* parent = widget->parentWidget(); parent; parent = parent->parentWidget()) {
        if (auto* scroll = qobject_cast<QScrollArea*>(parent)) {
            scroll->ensureWidgetVisible(widget, 8, 8);
            break;
        }
    }

    const auto previousStyle = widget->styleSheet();
    widget->setStyleSheet(previousStyle +
        QStringLiteral("; border: 2px solid #c43b3b;"));
    QTimer::singleShot(1600, widget, [widget, previousStyle] {
        widget->setStyleSheet(previousStyle);
    });
    return true;
}

void StepPropertyEditor::buildGeneralPage()
{
    auto* content = new QWidget;
    m_generalForm = new QFormLayout(content);
    m_generalForm->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
    m_generalForm->setRowWrapPolicy(QFormLayout::WrapLongRows);
    m_generalForm->setContentsMargins(8, 8, 8, 8);

    m_idEdit = new QLineEdit(content);
    m_idEdit->setObjectName(QStringLiteral("propertyIdEdit"));
    addInspectableRow(m_generalForm, tr("ID"), m_idEdit,
                      QStringLiteral("id"));
    m_keyEdit = new QLineEdit(content);
    m_keyEdit->setObjectName(QStringLiteral("propertyKeyEdit"));
    addInspectableRow(m_generalForm, tr("Key"), m_keyEdit,
                      QStringLiteral("key"));
    m_nameEdit = new QLineEdit(content);
    m_nameEdit->setObjectName(QStringLiteral("propertyNameEdit"));
    addInspectableRow(m_generalForm, tr("Name"), m_nameEdit,
                      QStringLiteral("name"));
    m_kindCombo = new QComboBox(content);
    m_kindCombo->setObjectName(QStringLiteral("propertyKindCombo"));
    addInspectableRow(m_generalForm, tr("Kind"), m_kindCombo,
                      QStringLiteral("kind"));
    m_enabledCheck = new QCheckBox(content);
    m_enabledCheck->setObjectName(QStringLiteral("propertyEnabledCheck"));
    addInspectableRow(m_generalForm, tr("Enabled"), m_enabledCheck,
                      QStringLiteral("enabled"));
    m_alwaysRunCheck = new QCheckBox(content);
    addInspectableRow(m_generalForm, tr("Always run"), m_alwaysRunCheck,
                      QStringLiteral("alwaysRun"));
    m_resultRecordingCheck = new QCheckBox(content);
    m_resultRecordingCheck->setObjectName(
        QStringLiteral("propertyResultRecordingCheck"));
    m_resultRecordingCheck->setToolTip(tr(
        "Include this item in CSV, XLSX, and PDF reports. TXT logs and the overall pass/fail result are unaffected."));
    addInspectableRow(m_generalForm, tr("Record in CSV / XLSX / PDF"),
                      m_resultRecordingCheck,
                      QStringLiteral("resultRecording"));
    m_checkpointBeforeCheck = new QCheckBox(content);
    addInspectableRow(m_generalForm, tr("Checkpoint before"),
                      m_checkpointBeforeCheck,
                      QStringLiteral("checkpointBefore"));
    m_checkpointAfterCheck = new QCheckBox(content);
    addInspectableRow(m_generalForm, tr("Checkpoint after"),
                      m_checkpointAfterCheck,
                      QStringLiteral("checkpointAfter"));
    m_tagsEdit = new QLineEdit(content);
    m_tagsEdit->setObjectName(QStringLiteral("propertyTagsEdit"));
    addInspectableRow(m_generalForm, tr("Tags"), m_tagsEdit,
                      QStringLiteral("tags"));

    auto* scroll = new QScrollArea(m_tabs);
    scroll->setWidgetResizable(true);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setWidget(content);
    m_tabs->addTab(scroll, tr("General"));
}

void StepPropertyEditor::buildDataPage()
{
    auto* content = new QWidget;
    m_dataForm = new QFormLayout(content);
    m_dataForm->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
    m_dataForm->setRowWrapPolicy(QFormLayout::WrapLongRows);
    m_dataForm->setContentsMargins(8, 8, 8, 8);

    m_moduleIdEdit = new QLineEdit(content);
    m_moduleIdEdit->setObjectName(QStringLiteral("propertyModuleIdEdit"));
    addInspectableRow(m_dataForm, tr("Module ID"), m_moduleIdEdit,
                      QStringLiteral("moduleId"));
    m_functionEdit = new QComboBox(content);
    m_functionEdit->setObjectName(QStringLiteral("propertyFunctionEdit"));
    m_functionEdit->setEditable(false);
    addInspectableRow(m_dataForm, tr("Function"), m_functionEdit,
                      QStringLiteral("function"));
    m_deviceIdCombo = new QComboBox(content);
    m_deviceIdCombo->setObjectName(QStringLiteral("propertyDeviceIdCombo"));
    m_deviceIdCombo->setToolTip(
        tr("Station devices that support the selected plugin function"));
    addInspectableRow(m_dataForm, tr("Target device"), m_deviceIdCombo,
                      QStringLiteral("inputs.deviceId"));
    m_pluginInputsGroup = new QGroupBox(tr("Function Arguments"), content);
    m_pluginInputsGroup->setObjectName(QStringLiteral("pluginInputsGroup"));
    m_pluginInputsForm = new QFormLayout(m_pluginInputsGroup);
    m_pluginInputsForm->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
    m_pluginInputsForm->setRowWrapPolicy(QFormLayout::WrapLongRows);
    m_pluginInputsGroup->hide();
    m_dataForm->addRow(m_pluginInputsGroup);
    m_limitActualEdit = new QLineEdit(content);
    m_limitActualEdit->setObjectName(QStringLiteral("propertyLimitActualEdit"));
    m_limitActualEdit->setPlaceholderText(tr("Value or ${step:...outputs...}"));
    m_limitActualField = wrapExpressionEditor(m_limitActualEdit);
    if (auto* picker = m_limitActualField->findChild<QToolButton*>(
            QStringLiteral("expressionPickerButton"))) {
        picker->setToolTip(tr("Select a sequence variable or previous Step output"));
    }
    addInspectableRow(m_dataForm, tr("Actual value"), m_limitActualField,
                      QStringLiteral("inputs.actual"));
    m_limitComparisonCombo = new QComboBox(content);
    m_limitComparisonCombo->setObjectName(QStringLiteral("propertyLimitComparisonCombo"));
    m_limitComparisonCombo->addItem(tr("Between (expected +/- tolerance)"), QStringLiteral("betweenTolerance"));
    m_limitComparisonCombo->addItem(tr("Between (lower / upper)"), QStringLiteral("betweenLimits"));
    m_limitComparisonCombo->addItem(tr("Equal"), QStringLiteral("equal"));
    m_limitComparisonCombo->addItem(tr("Not equal"), QStringLiteral("notEqual"));
    m_limitComparisonCombo->addItem(tr("Greater than"), QStringLiteral("greaterThan"));
    m_limitComparisonCombo->addItem(tr("Greater than or equal"), QStringLiteral("greaterOrEqual"));
    m_limitComparisonCombo->addItem(tr("Less than"), QStringLiteral("lessThan"));
    m_limitComparisonCombo->addItem(tr("Less than or equal"), QStringLiteral("lessOrEqual"));
    m_limitComparisonCombo->addItem(tr("Contains"), QStringLiteral("contains"));
    m_limitComparisonCombo->addItem(tr("Starts with"), QStringLiteral("startsWith"));
    m_limitComparisonCombo->addItem(tr("Ends with"), QStringLiteral("endsWith"));
    m_limitComparisonCombo->addItem(tr("Is true"), QStringLiteral("isTrue"));
    m_limitComparisonCombo->addItem(tr("Is false"), QStringLiteral("isFalse"));
    addInspectableRow(m_dataForm, tr("Comparison"), m_limitComparisonCombo,
                      QStringLiteral("parameters.comparison"));
    m_limitExpectedEdit = new QLineEdit(content);
    m_limitExpectedEdit->setObjectName(QStringLiteral("propertyLimitExpectedEdit"));
    m_limitExpectedEdit->setPlaceholderText(tr("Expected value or threshold"));
    m_limitExpectedEdit->setToolTip(
        tr("For decimal literals, trailing zeros set the comparison precision"));
    m_limitExpectedField = wrapExpressionEditor(m_limitExpectedEdit);
    m_limitExpectedField->setObjectName(QStringLiteral("propertyLimitExpectedField"));
    addInspectableRow(m_dataForm, tr("Expected / threshold"),
                      m_limitExpectedField,
                      QStringLiteral("parameters.expected"));
    m_limitLowerEdit = new QLineEdit(content);
    m_limitLowerEdit->setObjectName(QStringLiteral("propertyLimitLowerEdit"));
    addInspectableRow(m_dataForm, tr("Lower limit"), m_limitLowerEdit,
                      QStringLiteral("parameters.lower"));
    m_limitUpperEdit = new QLineEdit(content);
    m_limitUpperEdit->setObjectName(QStringLiteral("propertyLimitUpperEdit"));
    addInspectableRow(m_dataForm, tr("Upper limit"), m_limitUpperEdit,
                      QStringLiteral("parameters.upper"));
    m_limitToleranceSpin = new QDoubleSpinBox(content);
    m_limitToleranceSpin->setObjectName(QStringLiteral("propertyLimitToleranceSpin"));
    m_limitToleranceSpin->setRange(0.0, std::numeric_limits<double>::max());
    m_limitToleranceSpin->setDecimals(9);
    m_limitToleranceSpin->setSingleStep(0.1);
    addInspectableRow(m_dataForm, tr("Tolerance (+/-)"),
                      m_limitToleranceSpin,
                      QStringLiteral("parameters.tolerance"));
    m_limitInclusiveCheck = new QCheckBox(content);
    m_limitInclusiveCheck->setObjectName(QStringLiteral("propertyLimitInclusiveCheck"));
    addInspectableRow(m_dataForm, tr("Include boundaries"),
                      m_limitInclusiveCheck,
                      QStringLiteral("parameters.inclusive"));
    m_limitMeasurementNameEdit = new QLineEdit(content);
    m_limitMeasurementNameEdit->setObjectName(QStringLiteral("propertyLimitMeasurementNameEdit"));
    addInspectableRow(m_dataForm, tr("Measurement name"),
                      m_limitMeasurementNameEdit,
                      QStringLiteral("parameters.measurementName"));
    m_limitUnitEdit = new QLineEdit(content);
    m_limitUnitEdit->setObjectName(QStringLiteral("propertyLimitUnitEdit"));
    addInspectableRow(m_dataForm, tr("Unit"), m_limitUnitEdit,
                      QStringLiteral("parameters.unit"));
    serviceAdminStartupAnimation();

    m_counterConditionEdit = new QLineEdit(content);
    m_counterConditionEdit->setObjectName(QStringLiteral("propertyCounterConditionEdit"));
    m_counterConditionEdit->setPlaceholderText(tr("Every execution (default)"));
    m_counterConditionEdit->setToolTip(tr(
        "Leave empty to count every execution, or choose a boolean Step output with fx."));
    m_counterConditionField = wrapExpressionEditor(m_counterConditionEdit);
    if (auto* picker = m_counterConditionField->findChild<QToolButton*>(
            QStringLiteral("expressionPickerButton"))) {
        picker->setToolTip(tr("Select a sequence variable or previous Step output"));
    }
    addInspectableRow(m_dataForm, tr("Count trigger"),
                      m_counterConditionField,
                      QStringLiteral("inputs.condition"));
    m_counterModeCombo = new QComboBox(content);
    m_counterModeCombo->setObjectName(QStringLiteral("propertyCounterModeCombo"));
    m_counterModeCombo->addItem(tr("Consecutive (reset when false)"),
                                QStringLiteral("consecutive"));
    m_counterModeCombo->addItem(tr("Total (keep value when false)"),
                                QStringLiteral("total"));
    addInspectableRow(m_dataForm, tr("Counter mode"), m_counterModeCombo,
                      QStringLiteral("parameters.mode"));
    m_counterStartSpin = new QDoubleSpinBox(content);
    m_counterStartSpin->setRange(-1.0e12, 1.0e12);
    m_counterStartSpin->setDecimals(6);
    addInspectableRow(m_dataForm, tr("Start value"), m_counterStartSpin,
                      QStringLiteral("parameters.start"));
    m_counterIncrementSpin = new QDoubleSpinBox(content);
    m_counterIncrementSpin->setRange(-1.0e12, 1.0e12);
    m_counterIncrementSpin->setDecimals(6);
    m_counterIncrementSpin->setValue(1.0);
    addInspectableRow(m_dataForm, tr("Increment"), m_counterIncrementSpin,
                      QStringLiteral("parameters.increment"));

    m_aggregateValueEdit = new QLineEdit(content);
    m_aggregateValueEdit->setObjectName(QStringLiteral("propertyAggregateValueEdit"));
    m_aggregateValueEdit->setPlaceholderText(tr("Numeric value or ${step:...outputs...}"));
    m_aggregateValueField = wrapExpressionEditor(m_aggregateValueEdit);
    if (auto* picker = m_aggregateValueField->findChild<QToolButton*>(
            QStringLiteral("expressionPickerButton"))) {
        picker->setToolTip(tr("Select a sequence variable or previous Step output"));
    }
    addInspectableRow(m_dataForm, tr("Value to aggregate"),
                      m_aggregateValueField,
                      QStringLiteral("inputs.value"));
    m_waitMsSpin = new QSpinBox(content);
    m_waitMsSpin->setRange(0, std::numeric_limits<int>::max());
    m_waitMsSpin->setSuffix(tr(" ms"));
    addInspectableRow(m_dataForm, tr("Duration"), m_waitMsSpin,
                      QStringLiteral("ms"));

    m_promptModeCombo = new QComboBox(content);
    m_promptModeCombo->setObjectName(QStringLiteral("propertyPromptModeCombo"));
    m_promptModeCombo->addItem(tr("Manual confirmation (default)"),
                               QStringLiteral("confirm"));
    m_promptModeCombo->addItem(tr("Conditional confirmation (Step PASS)"),
                               QStringLiteral("notice"));
    m_promptModeCombo->addItem(tr("Operator PASS / FAIL judgment"),
                               QStringLiteral("judgment"));
    m_promptModeCombo->addItem(tr("Operator input"),
                               QStringLiteral("input"));
    addInspectableRow(m_dataForm, tr("Mode"), m_promptModeCombo,
                      QStringLiteral("prompt.mode"));
    m_promptTitleEdit = new QLineEdit(content);
    m_promptTitleEdit->setObjectName(QStringLiteral("propertyPromptTitleEdit"));
    addInspectableRow(m_dataForm, tr("Window title"), m_promptTitleEdit,
                      QStringLiteral("prompt.title"));
    m_promptMessageEdit = new QPlainTextEdit(content);
    m_promptMessageEdit->setObjectName(QStringLiteral("propertyPromptMessageEdit"));
    m_promptMessageEdit->setMinimumHeight(90);
    m_promptMessageEdit->setPlaceholderText(
        tr("Tell the operator what to do. Runtime values can be inserted with fx."));
    m_promptMessageField = wrapPromptMessageEditor(m_promptMessageEdit);
    addInspectableRow(m_dataForm, tr("Message"), m_promptMessageField,
                      QStringLiteral("prompt.message"));
    m_promptImageCombo = new QComboBox(content);
    m_promptImageCombo->setObjectName(QStringLiteral("propertyPromptImageCombo"));
    m_promptImageCombo->setInsertPolicy(QComboBox::NoInsert);
    m_promptImageCombo->setToolTip(
        tr("Optional PNG/JPG image from this project's images folder"));
    addInspectableRow(m_dataForm, tr("Image (optional)"),
                      m_promptImageCombo,
                      QStringLiteral("prompt.image"));
    m_promptConfirmTextEdit = new QLineEdit(content);
    m_promptConfirmTextEdit->setObjectName(QStringLiteral("propertyPromptConfirmTextEdit"));
    addInspectableRow(m_dataForm, tr("Button text"),
                      m_promptConfirmTextEdit,
                      QStringLiteral("prompt.confirmText"));
    m_promptInputTypeCombo = new QComboBox(content);
    m_promptInputTypeCombo->setObjectName(QStringLiteral("propertyPromptInputTypeCombo"));
    m_promptInputTypeCombo->addItem(tr("Text"), QStringLiteral("text"));
    m_promptInputTypeCombo->addItem(tr("Integer"), QStringLiteral("integer"));
    m_promptInputTypeCombo->addItem(tr("Number"), QStringLiteral("number"));
    addInspectableRow(m_dataForm, tr("Input type"),
                      m_promptInputTypeCombo,
                      QStringLiteral("prompt.inputType"));
    m_promptInputPlaceholderEdit = new QLineEdit(content);
    m_promptInputPlaceholderEdit->setObjectName(
        QStringLiteral("propertyPromptInputPlaceholderEdit"));
    m_promptInputPlaceholderEdit->setPlaceholderText(
        tr("Optional hint shown inside the input box"));
    addInspectableRow(m_dataForm, tr("Input hint (optional)"),
                      m_promptInputPlaceholderEdit,
                      QStringLiteral("prompt.inputPlaceholder"));
    m_promptDefaultValueEdit = new QLineEdit(content);
    m_promptDefaultValueEdit->setObjectName(
        QStringLiteral("propertyPromptDefaultValueEdit"));
    addInspectableRow(m_dataForm, tr("Default value (optional)"),
                      m_promptDefaultValueEdit,
                      QStringLiteral("prompt.defaultValue"));
    m_promptCloseOnStepCombo = new QComboBox(content);
    m_promptCloseOnStepCombo->setObjectName(QStringLiteral("propertyPromptCloseOnStepCombo"));
    m_promptCloseOnStepCombo->setEditable(true);
    m_promptCloseOnStepCombo->setInsertPolicy(QComboBox::NoInsert);
    m_promptCloseOnStepCombo->setToolTip(
        tr("Select a later enabled step. The prompt closes after that step finishes."));
    addInspectableRow(m_dataForm, tr("Close after step"),
                      m_promptCloseOnStepCombo,
                      QStringLiteral("prompt.closeOnStep"));
    m_promptDialogKeyEdit = new QLineEdit(content);
    m_promptDialogKeyEdit->setObjectName(QStringLiteral("propertyPromptDialogKeyEdit"));
    m_promptDialogKeyEdit->setPlaceholderText(tr("Example: rgb-lamp-check"));
    m_promptDialogKeyEdit->setToolTip(
        tr("Use the same key on an observation prompt and a later judgment to reuse one window."));
    addInspectableRow(m_dataForm, tr("Dialog key (optional)"),
                      m_promptDialogKeyEdit,
                      QStringLiteral("prompt.dialogKey"));
    m_promptPassTextEdit = new QLineEdit(content);
    m_promptPassTextEdit->setObjectName(QStringLiteral("propertyPromptPassTextEdit"));
    addInspectableRow(m_dataForm, tr("PASS button text"),
                      m_promptPassTextEdit,
                      QStringLiteral("prompt.passText"));
    m_promptFailTextEdit = new QLineEdit(content);
    m_promptFailTextEdit->setObjectName(QStringLiteral("propertyPromptFailTextEdit"));
    addInspectableRow(m_dataForm, tr("FAIL button text"),
                      m_promptFailTextEdit,
                      QStringLiteral("prompt.failText"));
    m_promptFailureCodeEdit = new QLineEdit(content);
    m_promptFailureCodeEdit->setObjectName(QStringLiteral("propertyPromptFailureCodeEdit"));
    m_promptFailureCodeEdit->setPlaceholderText(QStringLiteral("OperatorCheckFailed"));
    addInspectableRow(m_dataForm, tr("FAIL error code"),
                      m_promptFailureCodeEdit,
                      QStringLiteral("prompt.failureCode"));
    m_promptTimeoutSpin = new QSpinBox(content);
    m_promptTimeoutSpin->setObjectName(QStringLiteral("propertyPromptTimeoutSpin"));
    m_promptTimeoutSpin->setRange(0, std::numeric_limits<int>::max());
    m_promptTimeoutSpin->setSuffix(tr(" ms"));
    m_promptTimeoutSpin->setToolTip(tr("0 means no timeout for confirmation prompts"));
    addInspectableRow(m_dataForm, tr("Response timeout"),
                      m_promptTimeoutSpin,
                      QStringLiteral("prompt.timeoutMs"));
    serviceAdminStartupAnimation();

    m_loopTypeCombo = new QComboBox(content);
    m_loopTypeCombo->setObjectName(QStringLiteral("propertyLoopTypeCombo"));
    m_loopTypeCombo->addItem(tr("For Loop"), QStringLiteral("for"));
    m_loopTypeCombo->addItem(tr("While Loop"), QStringLiteral("while"));
    addInspectableRow(m_dataForm, tr("Loop type"), m_loopTypeCombo,
                      QStringLiteral("loop.type"));
    m_loopVariableEdit = new QLineEdit(content);
    addInspectableRow(m_dataForm, tr("Loop variable"), m_loopVariableEdit,
                      QStringLiteral("loop.variable"));
    m_loopFromSpin = new QSpinBox(content);
    m_loopFromSpin->setRange(std::numeric_limits<int>::min(), std::numeric_limits<int>::max());
    addInspectableRow(m_dataForm, tr("From"), m_loopFromSpin,
                      QStringLiteral("loop.from"));
    m_loopToSpin = new QSpinBox(content);
    m_loopToSpin->setRange(std::numeric_limits<int>::min(), std::numeric_limits<int>::max());
    addInspectableRow(m_dataForm, tr("To"), m_loopToSpin,
                      QStringLiteral("loop.to"));
    m_loopStepSpin = new QSpinBox(content);
    m_loopStepSpin->setRange(std::numeric_limits<int>::min(), std::numeric_limits<int>::max());
    addInspectableRow(m_dataForm, tr("Step"), m_loopStepSpin,
                      QStringLiteral("loop.step"));

    m_conditionIntervalSpin = new QSpinBox(content);
    m_conditionIntervalSpin->setObjectName(
        QStringLiteral("propertyConditionIntervalSpin"));
    m_conditionIntervalSpin->setRange(0, std::numeric_limits<int>::max());
    m_conditionIntervalSpin->setSuffix(tr(" ms"));
    addInspectableRow(m_dataForm, tr("Delay between iterations"),
                      m_conditionIntervalSpin,
                      QStringLiteral("loop.intervalMs"));
    m_conditionMaxIterationsSpin = new QSpinBox(content);
    m_conditionMaxIterationsSpin->setObjectName(
        QStringLiteral("propertyConditionMaxIterationsSpin"));
    m_conditionMaxIterationsSpin->setRange(0, std::numeric_limits<int>::max());
    m_conditionMaxIterationsSpin->setSpecialValueText(tr("Disabled"));
    addInspectableRow(m_dataForm, tr("Maximum iterations"),
                      m_conditionMaxIterationsSpin,
                      QStringLiteral("loop.maxIterations"));
    m_conditionTimeoutSpin = new QSpinBox(content);
    m_conditionTimeoutSpin->setObjectName(
        QStringLiteral("propertyConditionTimeoutSpin"));
    m_conditionTimeoutSpin->setRange(0, std::numeric_limits<int>::max());
    m_conditionTimeoutSpin->setSuffix(tr(" ms"));
    m_conditionTimeoutSpin->setSpecialValueText(tr("Disabled"));
    addInspectableRow(m_dataForm, tr("Overall timeout"),
                      m_conditionTimeoutSpin,
                      QStringLiteral("loop.timeoutMs"));
    m_conditionIterationErrorCombo = new QComboBox(content);
    m_conditionIterationErrorCombo->setObjectName(
        QStringLiteral("propertyConditionIterationErrorCombo"));
    m_conditionIterationErrorCombo->addItem(
        tr("Continue on Fail; stop on Error / Timeout"),
        QStringLiteral("continueOnFail"));
    m_conditionIterationErrorCombo->addItem(
        tr("Stop on Fail / Error / Timeout"),
        QStringLiteral("abortLoop"));
    m_conditionIterationErrorCombo->addItem(
        tr("Continue on Fail / Error / Timeout"),
        QStringLiteral("continueLoop"));
    addInspectableRow(m_dataForm, tr("Iteration failure handling"),
                      m_conditionIterationErrorCombo,
                      QStringLiteral("loop.iterationErrorPolicy"));
    serviceAdminStartupAnimation();

    m_barrierNameEdit = new QLineEdit(content);
    m_barrierNameEdit->setObjectName(QStringLiteral("propertyBarrierNameEdit"));
    m_barrierNameEdit->setPlaceholderText(tr("Defaults to the step ID"));
    addInspectableRow(m_dataForm, tr("Barrier name (optional)"), m_barrierNameEdit,
                      QStringLiteral("barrier.barrierName"));
    m_cohortIdEdit = new QLineEdit(content);
    addInspectableRow(m_dataForm, tr("Cohort ID"), m_cohortIdEdit,
                      QStringLiteral("barrier.cohortId"));
    m_expectedUutSpin = new QSpinBox(content);
    m_expectedUutSpin->setObjectName(
        QStringLiteral("propertyBarrierExpectedUutSpin"));
    m_expectedUutSpin->setRange(-1, 100000);
    addInspectableRow(m_dataForm, tr("Expected UUTs"), m_expectedUutSpin,
                      QStringLiteral("barrier.expectedUutCount"));
    m_quorumCountSpin = new QSpinBox(content);
    m_quorumCountSpin->setRange(-1, 100000);
    addInspectableRow(m_dataForm, tr("Quorum count"), m_quorumCountSpin,
                      QStringLiteral("barrier.quorumCount"));
    m_quorumRatioSpin = new QDoubleSpinBox(content);
    m_quorumRatioSpin->setRange(0.0, 1.0);
    m_quorumRatioSpin->setDecimals(3);
    m_quorumRatioSpin->setSingleStep(0.05);
    addInspectableRow(m_dataForm, tr("Quorum ratio"), m_quorumRatioSpin,
                      QStringLiteral("barrier.quorumRatio"));
    m_arrivalTimeoutSpin = new QSpinBox(content);
    m_arrivalTimeoutSpin->setRange(0, std::numeric_limits<int>::max());
    m_arrivalTimeoutSpin->setSuffix(tr(" ms"));
    addInspectableRow(m_dataForm, tr("Arrival timeout"),
                      m_arrivalTimeoutSpin,
                      QStringLiteral("barrier.arrivalTimeoutMs"));
    m_releaseTimeoutSpin = new QSpinBox(content);
    m_releaseTimeoutSpin->setRange(0, std::numeric_limits<int>::max());
    m_releaseTimeoutSpin->setSuffix(tr(" ms"));
    addInspectableRow(m_dataForm, tr("Release timeout"),
                      m_releaseTimeoutSpin,
                      QStringLiteral("barrier.releaseTimeoutMs"));
    m_arrivalPolicyCombo = new QComboBox(content);
    m_arrivalPolicyCombo->setObjectName(
        QStringLiteral("propertyBarrierArrivalPolicyCombo"));
    addItems(m_arrivalPolicyCombo, {"WaitAll", "DropFailed", "CountFailed", "Quorum", "BestEffort", "ManualDecision"});
    addInspectableRow(m_dataForm, tr("Arrival policy"),
                      m_arrivalPolicyCombo,
                      QStringLiteral("barrier.arrivalPolicy"));
    m_releasePolicyCombo = new QComboBox(content);
    addItems(m_releasePolicyCombo, {"Lockstep", "Latch", "Cohort", "RollingWindow"});
    addInspectableRow(m_dataForm, tr("Release policy"),
                      m_releasePolicyCombo,
                      QStringLiteral("barrier.releasePolicy"));
    m_failurePolicyCombo = new QComboBox(content);
    m_failurePolicyCombo->setObjectName(
        QStringLiteral("propertyBarrierFailurePolicyCombo"));
    addItems(m_failurePolicyCombo, {"FailBarrier", "RemoveFailedMember", "HoldFailedMember", "ContinueWithWarning", "AbortCohort"});
    addInspectableRow(m_dataForm, tr("Failure policy"),
                      m_failurePolicyCombo,
                      QStringLiteral("barrier.failurePolicy"));
    m_barrierTimeoutPolicyCombo = new QComboBox(content);
    addItems(m_barrierTimeoutPolicyCombo, {"FailArrivedAndWaiting", "ReleaseArrived", "ReleaseIfQuorumReached", "AbortCohort", "RequestOperatorDecision"});
    addInspectableRow(m_dataForm, tr("Timeout policy"),
                      m_barrierTimeoutPolicyCombo,
                      QStringLiteral("barrier.timeoutPolicy"));
    m_releaseResourcesCheck = new QCheckBox(content);
    addInspectableRow(m_dataForm, tr("Release resources"),
                      m_releaseResourcesCheck,
                      QStringLiteral("barrier.releaseHeldResourcesOnWait"));
    serviceAdminStartupAnimation();

    m_advancedJsonToggle = new QToolButton(content);
    m_advancedJsonToggle->setObjectName(
        QStringLiteral("propertyAdvancedJsonToggle"));
    m_advancedJsonToggle->setText(tr("Advanced JSON"));
    m_advancedJsonToggle->setCheckable(true);
    m_advancedJsonToggle->setArrowType(Qt::RightArrow);
    m_advancedJsonToggle->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    m_advancedJsonToggle->setProperty("picoateDisclosure", true);
    m_advancedJsonToggle->setToolTip(
        tr("Inspect or edit legacy and unrecognized fields"));
    m_dataForm->addRow(m_advancedJsonToggle);

    m_advancedJsonContent = new QWidget(content);
    m_advancedJsonContent->setObjectName(
        QStringLiteral("propertyAdvancedJsonContent"));
    m_advancedJsonForm = new QFormLayout(m_advancedJsonContent);
    m_advancedJsonForm->setContentsMargins(16, 2, 0, 4);
    m_advancedJsonForm->setFieldGrowthPolicy(
        QFormLayout::AllNonFixedFieldsGrow);
    m_advancedJsonForm->setRowWrapPolicy(QFormLayout::WrapLongRows);
    m_inputsEdit = new QPlainTextEdit(m_advancedJsonContent);
    m_inputsEdit->setObjectName(QStringLiteral("propertyInputsEdit"));
    m_inputsEdit->setMinimumHeight(80);
    m_inputsEdit->setToolTip(
        tr("Raw runtime inputs. Prefer the typed fields above."));
    m_advancedJsonForm->addRow(tr("Inputs"), m_inputsEdit);
    m_parametersEdit = new QPlainTextEdit(m_advancedJsonContent);
    m_parametersEdit->setObjectName(QStringLiteral("propertyParametersEdit"));
    m_parametersEdit->setMinimumHeight(80);
    m_parametersEdit->setToolTip(
        tr("Raw Step handler parameters. Usually not needed for plugin functions."));
    m_advancedJsonForm->addRow(tr("Parameters"), m_parametersEdit);
    m_errorPolicyEdit = new QPlainTextEdit(m_advancedJsonContent);
    m_errorPolicyEdit->setObjectName(QStringLiteral("propertyLegacyErrorPolicyEdit"));
    m_errorPolicyEdit->setMinimumHeight(80);
    m_errorPolicyEdit->setToolTip(
        tr("Advanced fields such as cleanupRegionId. Outcome policies are configured on the Policies tab."));
    m_advancedJsonForm->addRow(tr("Advanced error policy"), m_errorPolicyEdit);
    m_advancedJsonContent->hide();
    m_dataForm->addRow(m_advancedJsonContent);

    connect(m_advancedJsonToggle, &QToolButton::toggled,
            this, [this](bool expanded) {
                m_advancedJsonToggle->setArrowType(
                    expanded ? Qt::DownArrow : Qt::RightArrow);
                m_advancedJsonContent->setVisible(
                    expanded && !m_advancedJsonToggle->isHidden());
            });

    auto* scroll = new QScrollArea(m_tabs);
    scroll->setWidgetResizable(true);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setWidget(content);
    m_tabs->addTab(scroll, tr("Data"));
    serviceAdminStartupAnimation();
}

void StepPropertyEditor::buildPolicyPage()
{
    auto* content = new QWidget;
    m_policyForm = new QFormLayout(content);
    m_policyForm->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
    m_policyForm->setRowWrapPolicy(QFormLayout::WrapLongRows);
    m_policyForm->setContentsMargins(8, 8, 8, 8);

    m_maxAttemptsSpin = new QSpinBox(content);
    m_maxAttemptsSpin->setObjectName(QStringLiteral("propertyMaxAttemptsSpin"));
    m_maxAttemptsSpin->setRange(1, 100000);
    addInspectableRow(m_policyForm, tr("Max attempts"), m_maxAttemptsSpin,
                      QStringLiteral("retry.maxAttempts"));
    m_retryDelaySpin = new QSpinBox(content);
    m_retryDelaySpin->setRange(0, std::numeric_limits<int>::max());
    m_retryDelaySpin->setSuffix(tr(" ms"));
    addInspectableRow(m_policyForm, tr("Retry delay"), m_retryDelaySpin,
                      QStringLiteral("retry.delayMs"));
    m_retryWhenEdit = new QLineEdit(content);
    m_retryWhenEdit->setObjectName(QStringLiteral("propertyRetryWhenEdit"));
    addInspectableRow(m_policyForm, tr("Retry when"), m_retryWhenEdit,
                      QStringLiteral("retry.retryWhen"));
    setFormRowVisible(m_policyForm, m_retryWhenEdit, false);
    m_timeoutSpin = new QSpinBox(content);
    m_timeoutSpin->setRange(0, std::numeric_limits<int>::max());
    m_timeoutSpin->setSuffix(tr(" ms"));
    addInspectableRow(m_policyForm, tr("Timeout"), m_timeoutSpin,
                      QStringLiteral("timeout.timeoutMs"));

    m_periodicEnabledCheck = new QCheckBox(tr("Run this Action in the background"), content);
    m_periodicEnabledCheck->setObjectName(QStringLiteral("propertyPeriodicEnabledCheck"));
    m_periodicEnabledCheck->setToolTip(
        tr("Register this top-level Setup Action as a cooperative periodic task."));
    m_policyForm->addRow(tr("Periodic task"), m_periodicEnabledCheck);
    m_periodicIntervalSpin = new QSpinBox(content);
    m_periodicIntervalSpin->setObjectName(QStringLiteral("propertyPeriodicIntervalSpin"));
    m_periodicIntervalSpin->setRange(1, std::numeric_limits<int>::max());
    m_periodicIntervalSpin->setSuffix(tr(" ms"));
    addInspectableRow(m_policyForm, tr("Interval"), m_periodicIntervalSpin,
                      QStringLiteral("periodic.intervalMs"));
    m_periodicRunImmediatelyCheck = new QCheckBox(tr("Run once immediately after registration"), content);
    m_periodicRunImmediatelyCheck->setObjectName(
        QStringLiteral("propertyPeriodicRunImmediatelyCheck"));
    addInspectableRow(m_policyForm, tr("First run"),
                      m_periodicRunImmediatelyCheck,
                      QStringLiteral("periodic.runImmediately"));
    m_periodicCounterStartSpin = new QSpinBox(content);
    m_periodicCounterStartSpin->setObjectName(
        QStringLiteral("propertyPeriodicCounterStartSpin"));
    m_periodicCounterStartSpin->setRange(0, std::numeric_limits<int>::max());
    m_periodicCounterStartSpin->setValue(1);
    addInspectableRow(m_policyForm, tr("Counter start"),
                      m_periodicCounterStartSpin,
                      QStringLiteral("periodic.counter.start"));
    m_periodicCounterIncrementSpin = new QSpinBox(content);
    m_periodicCounterIncrementSpin->setObjectName(
        QStringLiteral("propertyPeriodicCounterIncrementSpin"));
    m_periodicCounterIncrementSpin->setRange(1, std::numeric_limits<int>::max());
    m_periodicCounterIncrementSpin->setValue(1);
    addInspectableRow(m_policyForm, tr("Counter increment"),
                      m_periodicCounterIncrementSpin,
                      QStringLiteral("periodic.counter.increment"));
    m_periodicCounterWrapAtSpin = new QSpinBox(content);
    m_periodicCounterWrapAtSpin->setObjectName(
        QStringLiteral("propertyPeriodicCounterWrapAtSpin"));
    m_periodicCounterWrapAtSpin->setRange(0, std::numeric_limits<int>::max());
    m_periodicCounterWrapAtSpin->setSpecialValueText(tr("No wrap"));
    addInspectableRow(m_policyForm, tr("Counter wrap at"),
                      m_periodicCounterWrapAtSpin,
                      QStringLiteral("periodic.counter.wrapAt"));

    m_resourcesEdit = new QPlainTextEdit(content);
    m_resourcesEdit->setObjectName(QStringLiteral("propertyResourcesEdit"));
    m_resourcesEdit->setMinimumHeight(140);
    addInspectableRow(m_policyForm, tr("Resources (JSON)"),
                      m_resourcesEdit,
                      QStringLiteral("resources"));

    const auto createOutcomePolicyCombo = [content](const QString& objectName) {
        auto* combo = new QComboBox(content);
        combo->setObjectName(objectName);
        combo->addItem(QObject::tr("Default (parent / Station)"),
                       QStringLiteral("Inherit"));
        combo->addItem(QObject::tr("Continue"), QStringLiteral("Continue"));
        combo->addItem(QObject::tr("Stop current UUT"), QStringLiteral("StopUut"));
        combo->addItem(QObject::tr("Jump to later step"), QStringLiteral("JumpTo"));
        combo->addItem(QObject::tr("Run Cleanup"), QStringLiteral("RunCleanup"));
        combo->addItem(QObject::tr("Abort Session"), QStringLiteral("Abort"));
        combo->setToolTip(QObject::tr(
            "Default inherits the parent TestItem policy, then the Station failure policy."));
        return combo;
    };
    m_onFailPolicyCombo = createOutcomePolicyCombo(
        QStringLiteral("propertyOnFailPolicyCombo"));
    m_onErrorPolicyCombo = createOutcomePolicyCombo(
        QStringLiteral("propertyOnErrorPolicyCombo"));
    m_onTimeoutPolicyCombo = createOutcomePolicyCombo(
        QStringLiteral("propertyOnTimeoutPolicyCombo"));
    const auto createJumpTargetCombo = [content](const QString& objectName) {
        auto* combo = new QComboBox(content);
        combo->setObjectName(objectName);
        combo->setToolTip(QObject::tr(
            "The failed step stays failed. Intermediate sibling steps are recorded as Skipped."));
        return combo;
    };
    m_onFailTargetCombo = createJumpTargetCombo(
        QStringLiteral("propertyOnFailTargetCombo"));
    m_onErrorTargetCombo = createJumpTargetCombo(
        QStringLiteral("propertyOnErrorTargetCombo"));
    m_onTimeoutTargetCombo = createJumpTargetCombo(
        QStringLiteral("propertyOnTimeoutTargetCombo"));
    addInspectableRow(m_policyForm, QStringLiteral("onFail"),
                      m_onFailPolicyCombo,
                      QStringLiteral("errorPolicy.onFail"));
    addInspectableRow(m_policyForm, tr("onFail target"),
                      m_onFailTargetCombo,
                      QStringLiteral("errorPolicy.onFailTarget"));
    addInspectableRow(m_policyForm, QStringLiteral("onError"),
                      m_onErrorPolicyCombo,
                      QStringLiteral("errorPolicy.onError"));
    addInspectableRow(m_policyForm, tr("onError target"),
                      m_onErrorTargetCombo,
                      QStringLiteral("errorPolicy.onErrorTarget"));
    addInspectableRow(m_policyForm, QStringLiteral("onTimeout"),
                      m_onTimeoutPolicyCombo,
                      QStringLiteral("errorPolicy.onTimeout"));
    addInspectableRow(m_policyForm, tr("onTimeout target"),
                      m_onTimeoutTargetCombo,
                      QStringLiteral("errorPolicy.onTimeoutTarget"));

    auto* scroll = new QScrollArea(m_tabs);
    scroll->setWidgetResizable(true);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setWidget(content);
    m_tabs->addTab(scroll, tr("Policies"));
    serviceAdminStartupAnimation();
}

void StepPropertyEditor::loadCurrentObject()
{
    m_loading = true;
    m_errorLabel->hide();
    m_sourceObject = m_previewing
        ? m_previewObject
        : (m_document && m_path.isValid()
               ? m_document->objectAt(m_path)
               : QJsonObject{});
    const bool valid = !m_sourceObject.isEmpty();
    m_isGroup = valid && !m_previewing && m_path.isGroup();
    const bool standardGroup = m_isGroup && m_document &&
                               m_document->isStandardGroup(m_path);
    m_titleLabel->setText(m_previewing ? tr("Function Preview")
                                      : tr("Properties"));
    m_emptyLabel->setVisible(!valid);
    m_tabs->setVisible(valid);
    if (!valid) {
        m_loading = false;
        setDraftDirty(false);
        return;
    }

    m_idEdit->setText(m_sourceObject.value("id").toString());
    m_keyEdit->setText(m_sourceObject.value("key").toString());
    m_nameEdit->setText(m_sourceObject.value("name").toString());
    m_kindCombo->clear();
    if (m_isGroup) {
        addItems(m_kindCombo, {"setup", "main", "custom", "cleanup"});
    } else {
        addItems(m_kindCombo, {"noop", "wait", "action", "limit", "break", "counter", "aggregate", "operatorPrompt", "barrier", "loop", "testItem", "statement", "sequenceCall"});
    }
    const auto rawKind = m_sourceObject.value(QStringLiteral("kind")).toString(
        m_sourceObject.value(QStringLiteral("type")).toString(
            m_isGroup ? QStringLiteral("custom") : QStringLiteral("noop")));
    const auto displayedKind = canonicalizeSequenceItemForUi(
        QJsonObject{{QStringLiteral("kind"), rawKind}}, m_isGroup)
                                   .value(QStringLiteral("kind"))
                                   .toString(rawKind);
    setComboValue(m_kindCombo, displayedKind);
    m_enabledCheck->setChecked(m_sourceObject.value("enabled").toBool(true));
    m_alwaysRunCheck->setChecked(m_sourceObject.value("alwaysRun").toBool(false));
    m_resultRecordingCheck->setChecked(m_sourceObject.value("resultRecording").toBool(true));
    m_checkpointBeforeCheck->setChecked(m_sourceObject.value("checkpointBefore").toBool(false));
    m_checkpointAfterCheck->setChecked(m_sourceObject.value("checkpointAfter").toBool(false));
    m_tagsEdit->setText(tagsText(m_sourceObject.value("tags").toArray()));

    m_moduleIdEdit->setText(m_sourceObject.value("moduleId").toString());
    m_inputsEdit->setPlainText(objectText(m_sourceObject.value("inputs").toObject()));
    rebuildFunctionChoices(m_sourceObject.value("function").toString());
    auto errorPolicy = m_sourceObject.value("errorPolicy").toObject();
    const auto loadOutcomePolicy = [&errorPolicy](const QString& key) {
        auto value = errorPolicy.value(key).toString(QStringLiteral("Inherit"));
        if (value.compare(QStringLiteral("InheritStation"),
                          Qt::CaseInsensitive) == 0 ||
            value.compare(QStringLiteral("StationDefault"),
                          Qt::CaseInsensitive) == 0) {
            value = QStringLiteral("Inherit");
        }
        return value;
    };
    setComboValue(m_onFailPolicyCombo,
                  loadOutcomePolicy(QStringLiteral("onFail")));
    setComboValue(m_onErrorPolicyCombo,
                  loadOutcomePolicy(QStringLiteral("onError")));
    setComboValue(m_onTimeoutPolicyCombo,
                  loadOutcomePolicy(QStringLiteral("onTimeout")));
    rebuildFailureJumpChoices(
        m_onFailTargetCombo,
        errorPolicy.value(QStringLiteral("onFailTarget")).toString());
    rebuildFailureJumpChoices(
        m_onErrorTargetCombo,
        errorPolicy.value(QStringLiteral("onErrorTarget")).toString());
    rebuildFailureJumpChoices(
        m_onTimeoutTargetCombo,
        errorPolicy.value(QStringLiteral("onTimeoutTarget")).toString());
    updateFailureJumpVisibility();
    errorPolicy.remove(QStringLiteral("onFail"));
    errorPolicy.remove(QStringLiteral("onError"));
    errorPolicy.remove(QStringLiteral("onTimeout"));
    errorPolicy.remove(QStringLiteral("onFailTarget"));
    errorPolicy.remove(QStringLiteral("onErrorTarget"));
    errorPolicy.remove(QStringLiteral("onTimeoutTarget"));
    m_errorPolicyEdit->setPlainText(objectText(errorPolicy));
    m_advancedJsonToggle->setChecked(false);
    m_limitActualEdit->setText(
        m_sourceObject.value("inputs").toObject().value("actual").toVariant().toString());
    const auto parameters = m_sourceObject.value("parameters").toObject();
    m_parametersEdit->setPlainText(objectText(parameters));
    setComboValue(m_limitComparisonCombo, limitEditorMode(parameters));
    m_limitExpectedEdit->setText(limitExpectedEditorText(parameters));
    m_limitLowerEdit->setText(jsonValueText(
        parameters.contains(QStringLiteral("lower"))
            ? parameters.value(QStringLiteral("lower"))
            : parameters.value(QStringLiteral("lowerLimit"))));
    m_limitUpperEdit->setText(jsonValueText(
        parameters.contains(QStringLiteral("upper"))
            ? parameters.value(QStringLiteral("upper"))
            : parameters.value(QStringLiteral("upperLimit"))));
    m_limitToleranceSpin->setValue(parameters.value(QStringLiteral("tolerance")).toDouble(0.0));
    m_limitInclusiveCheck->setChecked(parameters.value(QStringLiteral("inclusive")).toBool(true));
    m_limitMeasurementNameEdit->setText(parameters.value(QStringLiteral("measurementName")).toString());
    m_limitUnitEdit->setText(parameters.value(QStringLiteral("unit")).toString());
    const auto inputs = m_sourceObject.value("inputs").toObject();
    const auto counterCondition = inputs.value(QStringLiteral("condition"));
    m_counterConditionEdit->setText(
        counterCondition.isUndefined() ||
                (counterCondition.isBool() && counterCondition.toBool())
            ? QString()
            : jsonValueText(counterCondition));
    setComboValue(m_counterModeCombo,
                  parameters.value("mode").toString(QStringLiteral("consecutive")));
    m_counterStartSpin->setValue(parameters.value("start").toDouble(0.0));
    m_counterIncrementSpin->setValue(parameters.value("increment").toDouble(1.0));
    m_aggregateValueEdit->setText(jsonValueText(inputs.value("value")));
    m_waitMsSpin->setValue(m_sourceObject.value("ms").toInt(
        m_sourceObject.value("parameters").toObject().value("ms").toInt(0)));

    const auto prompt = m_sourceObject.value("prompt").toObject();
    setComboValue(m_promptModeCombo,
                  prompt.value("mode").toString(QStringLiteral("confirm")));
    const auto promptMode = m_promptModeCombo->currentData().toString();
    m_promptTitleEdit->setText(
        prompt.value("title").toString(tr("Message")));
    m_promptMessageEdit->setPlainText(prompt.value("message").toString());
    const auto editorKind = m_kindCombo->currentData().toString();
    if (editorKind == QStringLiteral("operatorPrompt")) {
        rebuildPromptImageChoices(prompt.value("image").toString());
    }
    m_promptConfirmTextEdit->setText(
        prompt.value("confirmText").toString(
            promptMode == QStringLiteral("input")
                ? QStringLiteral("Submit")
                : QStringLiteral("OK")));
    setComboValue(m_promptInputTypeCombo,
                  prompt.value("inputType").toString(QStringLiteral("text")));
    m_promptInputPlaceholderEdit->setText(
        prompt.value("inputPlaceholder").toString());
    m_promptDefaultValueEdit->setText(
        jsonValueText(prompt.value("defaultValue")));
    if (editorKind == QStringLiteral("operatorPrompt")) {
        rebuildPromptCloseStepChoices(prompt.value("closeOnStep").toString());
    }
    m_promptDialogKeyEdit->setText(prompt.value("dialogKey").toString());
    m_promptPassTextEdit->setText(
        prompt.value("passText").toString(QStringLiteral("PASS")));
    m_promptFailTextEdit->setText(
        prompt.value("failText").toString(QStringLiteral("FAIL")));
    m_promptFailureCodeEdit->setText(
        prompt.value("failureCode").toString(QStringLiteral("OperatorCheckFailed")));
    m_promptTimeoutSpin->setValue(prompt.value("timeoutMs").toInt(60000));

    const auto loop = m_sourceObject.value("loop").toObject();
    auto loopType = loop.value("type").toString("for");
    if (loopType.compare(QStringLiteral("condition"), Qt::CaseInsensitive) == 0) {
        loopType = QStringLiteral("while");
    }
    setComboValue(m_loopTypeCombo, loopType);
    m_loopVariableEdit->setText(loop.value("variable").toString("i"));
    m_loopFromSpin->setValue(loop.value("from").toInt(0));
    m_loopToSpin->setValue(loop.value("to").toInt(0));
    m_loopStepSpin->setValue(loop.value("step").toInt(1));
    m_conditionIntervalSpin->setValue(loop.value("intervalMs").toInt(0));
    m_conditionMaxIterationsSpin->setValue(loop.value("maxIterations").toInt(100));
    m_conditionTimeoutSpin->setValue(loop.value("timeoutMs").toInt(60000));
    setComboValue(m_conditionIterationErrorCombo,
                  loop.value("iterationErrorPolicy").toString("continueOnFail"));

    auto barrier = m_sourceObject.value("barrier").toObject();
    if (barrier.isEmpty()) {
        barrier = m_sourceObject;
    }
    m_barrierNameEdit->setText(barrier.value("barrierName").toString(m_idEdit->text()));
    m_cohortIdEdit->setText(barrier.value("cohortId").toString("default"));
    m_expectedUutSpin->setValue(barrier.value("expectedUutCount").toInt(-1));
    m_quorumCountSpin->setValue(barrier.value("quorumCount").toInt(-1));
    m_quorumRatioSpin->setValue(barrier.value("quorumRatio").toDouble(1.0));
    m_arrivalTimeoutSpin->setValue(barrier.value("arrivalTimeoutMs").toInt(60000));
    m_releaseTimeoutSpin->setValue(barrier.value("releaseTimeoutMs").toInt(5000));
    setComboValue(m_arrivalPolicyCombo, barrier.value("arrivalPolicy").toString("DropFailed"));
    setComboValue(m_releasePolicyCombo, barrier.value("releasePolicy").toString("Lockstep"));
    setComboValue(m_failurePolicyCombo, barrier.value("failurePolicy").toString("RemoveFailedMember"));
    setComboValue(m_barrierTimeoutPolicyCombo, barrier.value("timeoutPolicy").toString("FailArrivedAndWaiting"));
    m_releaseResourcesCheck->setChecked(barrier.value("releaseHeldResourcesOnWait").toBool(true));

    const auto retry = m_sourceObject.value("retry").toObject();
    m_maxAttemptsSpin->setValue(
        retry.value("maxAttempts").toInt(1));
    m_retryDelaySpin->setValue(retry.value("delayMs").toInt(0));
    m_retryWhenEdit->setText(retry.value("retryWhen").toString());
    const auto timeout = m_sourceObject.value("timeout").toObject();
    m_timeoutSpin->setValue(timeout.value("timeoutMs").toInt(
        m_sourceObject.value("timeoutMs").toInt(0)));
    const auto periodic = m_sourceObject.value("periodic").toObject();
    const auto periodicCounter = periodic.value(QStringLiteral("counter")).toObject();
    m_periodicEnabledCheck->setChecked(!periodic.isEmpty());
    m_periodicIntervalSpin->setValue(periodic.value("intervalMs").toInt(5000));
    m_periodicRunImmediatelyCheck->setChecked(
        periodic.value("runImmediately").toBool(true));
    m_periodicCounterStartSpin->setValue(
        periodicCounter.value(QStringLiteral("start")).toInt(1));
    m_periodicCounterIncrementSpin->setValue(
        periodicCounter.value(QStringLiteral("increment")).toInt(1));
    m_periodicCounterWrapAtSpin->setValue(
        periodicCounter.value(QStringLiteral("wrapAt")).toInt(0));
    m_resourcesEdit->setPlainText(arrayText(m_sourceObject.value("resources").toArray()));

    rebuildDeviceChoices();
    rebuildPluginInputEditors();
    setFormRowVisible(m_generalForm, m_idEdit, !standardGroup);
    setFormRowVisible(m_generalForm, m_kindCombo, !standardGroup);
    setFormRowVisible(m_generalForm, m_keyEdit, !m_isGroup);
    setFormRowVisible(m_generalForm, m_enabledCheck, !m_isGroup);
    setFormRowVisible(m_generalForm, m_alwaysRunCheck, !m_isGroup);
    setFormRowVisible(m_generalForm, m_resultRecordingCheck, !m_isGroup);
    setFormRowVisible(m_generalForm, m_checkpointBeforeCheck, !m_isGroup);
    setFormRowVisible(m_generalForm, m_checkpointAfterCheck, !m_isGroup);
    setFormRowVisible(m_generalForm, m_tagsEdit, !m_isGroup);
    m_tabs->setTabEnabled(1, !m_isGroup);
    m_tabs->setTabEnabled(2, !m_isGroup);
    if (m_isGroup && m_tabs->currentIndex() != 0) {
        m_tabs->setCurrentIndex(0);
    }
    updateKindRows();
    m_loading = false;
    setDraftDirty(false);
    applyEditableState();
}

void StepPropertyEditor::updateKindRows()
{
    if (m_isGroup) {
        return;
    }
    const auto kind = m_kindCombo->currentData().toString();
    const bool action = kind == "action";
    const bool moduleCall = (action || kind == "cleanup") &&
                            !m_moduleIdEdit->text().trimmed().isEmpty();
    const bool logicalDevice = moduleCall &&
        m_moduleIdEdit->text().trimmed() == QStringLiteral("device");
    const bool loop = kind == "loop";
    const bool barrier = kind == "barrier";
    const bool limit = kind == "limit";
    const bool breakIf = kind == "break";
    const bool predicate = limit || breakIf;
    const bool counter = kind == "counter";
    const bool aggregate = kind == "aggregate";
    const bool operatorPrompt = kind == "operatorPrompt";
    const bool noticePrompt = operatorPrompt &&
        m_promptModeCombo->currentData().toString() == QStringLiteral("notice");
    const bool judgmentPrompt = operatorPrompt &&
        m_promptModeCombo->currentData().toString() == QStringLiteral("judgment");
    const bool inputPrompt = operatorPrompt &&
        m_promptModeCombo->currentData().toString() == QStringLiteral("input");
    const bool periodic = action && m_periodicEnabledCheck->isChecked();

    setFormRowVisible(m_dataForm, m_moduleIdEdit, action || moduleCall);
    setFormRowVisible(m_dataForm, m_functionEdit, action || moduleCall);
    setFormRowVisible(m_dataForm, m_deviceIdCombo, logicalDevice);
    m_pluginInputsGroup->setVisible(moduleCall && !m_pluginInputEditors.isEmpty());
    setFormRowVisible(m_dataForm, m_limitActualField, predicate);
    setFormRowVisible(m_dataForm, m_limitComparisonCombo, predicate);
    setFormRowVisible(m_dataForm, m_limitMeasurementNameEdit, limit);
    setFormRowVisible(m_dataForm, m_limitUnitEdit, limit);
    setFormRowVisible(m_dataForm, m_counterConditionField, counter);
    setFormRowVisible(m_dataForm, m_counterModeCombo, counter);
    setFormRowVisible(m_dataForm, m_counterStartSpin, counter);
    setFormRowVisible(m_dataForm, m_counterIncrementSpin, counter);
    setFormRowVisible(m_dataForm, m_aggregateValueField, aggregate);
    setFormRowVisible(m_dataForm, m_waitMsSpin, kind == "wait");
    setFormRowVisible(m_dataForm, m_promptModeCombo, operatorPrompt);
    setFormRowVisible(m_dataForm, m_promptTitleEdit, operatorPrompt);
    setFormRowVisible(m_dataForm, m_promptMessageField, operatorPrompt);
    setFormRowVisible(m_dataForm, m_promptImageCombo, operatorPrompt);
    setFormRowVisible(m_dataForm, m_promptConfirmTextEdit,
                      operatorPrompt && !noticePrompt && !judgmentPrompt);
    setFormRowVisible(m_dataForm, m_promptInputTypeCombo, inputPrompt);
    setFormRowVisible(m_dataForm, m_promptInputPlaceholderEdit, inputPrompt);
    setFormRowVisible(m_dataForm, m_promptDefaultValueEdit, inputPrompt);
    setFormRowVisible(m_dataForm, m_promptCloseOnStepCombo, noticePrompt);
    setFormRowVisible(m_dataForm, m_promptDialogKeyEdit,
                      noticePrompt || judgmentPrompt);
    setFormRowVisible(m_dataForm, m_promptPassTextEdit, judgmentPrompt);
    setFormRowVisible(m_dataForm, m_promptFailTextEdit, judgmentPrompt);
    setFormRowVisible(m_dataForm, m_promptFailureCodeEdit, judgmentPrompt);
    setFormRowVisible(m_dataForm, m_promptTimeoutSpin, operatorPrompt);
    setFormRowVisible(m_dataForm, m_loopTypeCombo, loop);
    setFormRowVisible(m_dataForm, m_barrierNameEdit, barrier);
    const std::array<QWidget*, 11> advancedBarrierFields = {
        m_cohortIdEdit, m_expectedUutSpin,
        m_quorumCountSpin, m_quorumRatioSpin, m_arrivalTimeoutSpin,
        m_releaseTimeoutSpin, m_arrivalPolicyCombo, m_releasePolicyCombo,
        m_failurePolicyCombo, m_barrierTimeoutPolicyCombo,
        m_releaseResourcesCheck};
    for (auto* field : advancedBarrierFields) {
        setFormRowVisible(m_dataForm, field, false);
    }
    setFormRowVisible(m_policyForm, m_periodicEnabledCheck, action);
    setFormRowVisible(m_policyForm, m_periodicIntervalSpin, periodic);
    setFormRowVisible(m_policyForm, m_periodicRunImmediatelyCheck, periodic);
    setFormRowVisible(m_policyForm, m_periodicCounterStartSpin, periodic);
    setFormRowVisible(m_policyForm, m_periodicCounterIncrementSpin, periodic);
    setFormRowVisible(m_policyForm, m_periodicCounterWrapAtSpin, periodic);
    updateLoopRows();
    updateLimitRows();
    updateAdvancedJsonVisibility();
}

void StepPropertyEditor::updateLoopRows()
{
    const bool loop = !m_isGroup &&
        m_kindCombo->currentData().toString() == QStringLiteral("loop");
    const bool whileLoop = loop &&
        m_loopTypeCombo->currentData().toString() == QStringLiteral("while");
    const bool forLoop = loop && !whileLoop;

    setFormRowVisible(m_dataForm, m_loopTypeCombo, loop);
    setFormRowVisible(m_dataForm, m_loopVariableEdit, forLoop);
    setFormRowVisible(m_dataForm, m_loopFromSpin, forLoop);
    setFormRowVisible(m_dataForm, m_loopToSpin, forLoop);
    setFormRowVisible(m_dataForm, m_loopStepSpin, forLoop);
    setFormRowVisible(m_dataForm, m_conditionIntervalSpin, whileLoop);
    setFormRowVisible(m_dataForm, m_conditionMaxIterationsSpin, whileLoop);
    setFormRowVisible(m_dataForm, m_conditionTimeoutSpin, whileLoop);
    setFormRowVisible(m_dataForm, m_conditionIterationErrorCombo, whileLoop);
}

void StepPropertyEditor::updateLimitRows()
{
    const auto kind = m_kindCombo->currentData().toString();
    const bool predicate = !m_isGroup &&
        (kind == QStringLiteral("limit") ||
         kind == QStringLiteral("break"));
    const auto mode = m_limitComparisonCombo->currentData().toString();
    const bool betweenTolerance = mode == QStringLiteral("betweenTolerance");
    const bool betweenLimits = mode == QStringLiteral("betweenLimits");
    const bool equality = mode == QStringLiteral("equal") ||
                          mode == QStringLiteral("notEqual");
    const bool boolean = mode == QStringLiteral("isTrue") ||
                         mode == QStringLiteral("isFalse");
    if (auto* label = qobject_cast<QLabel*>(
            m_dataForm->labelForField(m_limitExpectedField))) {
        if (mode == QStringLiteral("greaterThan") ||
            mode == QStringLiteral("greaterOrEqual")) {
            label->setText(tr("Lower threshold"));
            m_limitExpectedEdit->setPlaceholderText(tr("Value must be above this threshold"));
        } else if (mode == QStringLiteral("lessThan") ||
                   mode == QStringLiteral("lessOrEqual")) {
            label->setText(tr("Upper threshold"));
            m_limitExpectedEdit->setPlaceholderText(tr("Value must be below this threshold"));
        } else if (mode == QStringLiteral("contains") ||
                   mode == QStringLiteral("startsWith") ||
                   mode == QStringLiteral("endsWith")) {
            label->setText(tr("Expected text"));
            m_limitExpectedEdit->setPlaceholderText(tr("Text to compare"));
        } else {
            label->setText(tr("Expected value"));
            m_limitExpectedEdit->setPlaceholderText(tr("Expected value"));
        }
    }
    setFormRowVisible(m_dataForm, m_limitExpectedField,
                      predicate && !betweenLimits && !boolean);
    setFormRowVisible(m_dataForm, m_limitLowerEdit, predicate && betweenLimits);
    setFormRowVisible(m_dataForm, m_limitUpperEdit, predicate && betweenLimits);
    setFormRowVisible(m_dataForm, m_limitToleranceSpin,
                      predicate && (betweenTolerance || equality));
    setFormRowVisible(m_dataForm, m_limitInclusiveCheck,
                      predicate && (betweenTolerance || betweenLimits));
}

void StepPropertyEditor::updateAdvancedJsonVisibility()
{
    if (!m_advancedJsonToggle || !m_advancedJsonContent || m_isGroup) {
        if (m_advancedJsonToggle) {
            m_advancedJsonToggle->hide();
        }
        if (m_advancedJsonContent) {
            m_advancedJsonContent->hide();
        }
        return;
    }

    QJsonObject inputs;
    QJsonObject parameters;
    QJsonObject legacyErrorPolicy;
    QString ignoredError;
    parseObjectText(m_inputsEdit->toPlainText(), inputs, ignoredError);
    parseObjectText(m_parametersEdit->toPlainText(), parameters, ignoredError);
    parseObjectText(m_errorPolicyEdit->toPlainText(), legacyErrorPolicy, ignoredError);

    const auto kind = m_kindCombo->currentData().toString();
    const bool moduleCall = (kind == QStringLiteral("action") ||
                             kind == QStringLiteral("cleanup")) &&
                            !m_moduleIdEdit->text().trimmed().isEmpty();
    const bool rawOnlyKind = kind == QStringLiteral("statement") ||
                             kind == QStringLiteral("sequenceCall");

    QSet<QString> knownInputs;
    if (kind == QStringLiteral("limit") ||
        kind == QStringLiteral("break")) {
        knownInputs.insert(QStringLiteral("actual"));
    } else if (kind == QStringLiteral("counter")) {
        knownInputs.insert(QStringLiteral("condition"));
    } else if (kind == QStringLiteral("aggregate")) {
        knownInputs.insert(QStringLiteral("value"));
    }
    if (moduleCall) {
        knownInputs.insert(QStringLiteral("deviceId"));
        if (const auto* function = currentPluginFunction()) {
            for (const auto& input : function->inputs) {
                knownInputs.insert(input.key);
            }
        }
    }

    QSet<QString> knownParameters;
    if (kind == QStringLiteral("limit") ||
        kind == QStringLiteral("break")) {
        knownParameters = {
            QStringLiteral("comparison"), QStringLiteral("expected"),
            QStringLiteral("lower"), QStringLiteral("lowerLimit"),
            QStringLiteral("upper"), QStringLiteral("upperLimit"),
            QStringLiteral("tolerance"), QStringLiteral("inclusive"),
            QStringLiteral("measurementName"), QStringLiteral("unit")};
    } else if (kind == QStringLiteral("counter")) {
        knownParameters = {QStringLiteral("mode"), QStringLiteral("start"),
                           QStringLiteral("increment")};
    } else if (kind == QStringLiteral("wait")) {
        knownParameters.insert(QStringLiteral("ms"));
    }

    int extraFieldCount = 0;
    for (auto iterator = inputs.constBegin(); iterator != inputs.constEnd();
         ++iterator) {
        if (!knownInputs.contains(iterator.key())) {
            ++extraFieldCount;
        }
    }
    for (auto iterator = parameters.constBegin();
         iterator != parameters.constEnd(); ++iterator) {
        if (!knownParameters.contains(iterator.key())) {
            ++extraFieldCount;
        }
    }

    const bool genericModuleCall = moduleCall && !currentPluginFunction();
    const bool showAdvanced = moduleCall || rawOnlyKind ||
                              extraFieldCount > 0 || !legacyErrorPolicy.isEmpty();
    const bool showInputs = moduleCall || rawOnlyKind || !inputs.isEmpty();
    const bool showParameters = rawOnlyKind || genericModuleCall ||
                                !parameters.isEmpty();

    m_advancedJsonToggle->setText(extraFieldCount > 0
        ? tr("Advanced JSON (%1 extra)").arg(extraFieldCount)
        : tr("Advanced JSON"));
    m_advancedJsonToggle->setVisible(showAdvanced);
    setFormRowVisible(m_advancedJsonForm, m_inputsEdit,
                      showAdvanced && showInputs);
    setFormRowVisible(m_advancedJsonForm, m_parametersEdit,
                      showAdvanced && showParameters);
    setFormRowVisible(m_advancedJsonForm, m_errorPolicyEdit,
                      showAdvanced && !legacyErrorPolicy.isEmpty());
    m_advancedJsonContent->setVisible(
        showAdvanced && m_advancedJsonToggle->isChecked());
    if (!showAdvanced) {
        m_advancedJsonToggle->setChecked(false);
    }
}

void StepPropertyEditor::rebuildFunctionChoices(const QString& selectedFunction)
{
    if (!m_functionEdit) {
        return;
    }

    const auto selected = selectedFunction.isEmpty()
        ? m_functionEdit->currentData().toString()
        : selectedFunction;
    QStringList moduleIds;
    const auto moduleId = m_moduleIdEdit->text().trimmed();
    if (moduleId == QStringLiteral("device")) {
        QJsonObject inputs;
        QString ignoredError;
        parseObjectText(m_inputsEdit->toPlainText(), inputs, ignoredError);
        const auto deviceId = inputs.value(QStringLiteral("deviceId")).toString();
        const auto boundModule = m_pluginByDeviceId.value(deviceId);
        if (!boundModule.isEmpty()) {
            moduleIds.push_back(boundModule);
        } else {
            moduleIds = m_pluginByDeviceId.values();
            moduleIds.removeDuplicates();
        }
    } else if (!moduleId.isEmpty()) {
        moduleIds.push_back(moduleId);
    }

    QSignalBlocker blocker(m_functionEdit);
    m_functionEdit->clear();
    QSet<QString> addedFunctions;
    for (const auto& plugin : m_plugins) {
        if (!moduleIds.contains(plugin.moduleId)) {
            continue;
        }
        for (const auto& function : plugin.functions) {
            if (!function.paletteVisible && function.id != selected) {
                continue;
            }
            if (function.id.isEmpty() || addedFunctions.contains(function.id)) {
                continue;
            }
            const auto label = function.name.isEmpty() ||
                                       function.name == function.id
                ? function.id
                : QStringLiteral("%1 (%2)").arg(function.name, function.id);
            m_functionEdit->addItem(label, function.id);
            addedFunctions.insert(function.id);
        }
    }

    int selectedIndex = m_functionEdit->findData(selected);
    if (!selected.isEmpty() && selectedIndex < 0) {
        m_functionEdit->addItem(
            tr("%1 (Unavailable)").arg(selected), selected);
        selectedIndex = m_functionEdit->count() - 1;
    }
    if (m_functionEdit->count() == 0) {
        m_functionEdit->addItem(tr("No plugin functions available"), QVariant{});
        m_functionEdit->setEnabled(false);
        return;
    }
    m_functionEdit->setCurrentIndex(selectedIndex >= 0 ? selectedIndex : 0);
    m_functionEdit->setEnabled(m_editable && !m_previewing);
}

void StepPropertyEditor::rebuildDeviceChoices()
{
    if (!m_deviceIdCombo) {
        return;
    }

    QJsonObject inputs;
    QString ignoredError;
    parseObjectText(m_inputsEdit->toPlainText(), inputs, ignoredError);
    const auto currentDeviceId = inputs.value(
        QStringLiteral("deviceId")).toString();
    const auto functionId = m_functionEdit->currentData().toString();
    const bool logicalDevice = m_moduleIdEdit->text().trimmed() ==
                               QStringLiteral("device");

    QSignalBlocker blocker(m_deviceIdCombo);
    m_deviceIdCombo->clear();
    if (!logicalDevice || functionId.isEmpty()) {
        return;
    }

    QStringList compatibleDevices;
    for (auto iterator = m_pluginByDeviceId.constBegin();
         iterator != m_pluginByDeviceId.constEnd(); ++iterator) {
        const auto plugin = std::find_if(
            m_plugins.cbegin(), m_plugins.cend(),
            [&](const PluginManifest& manifest) {
                return manifest.moduleId == iterator.value();
            });
        if (plugin == m_plugins.cend()) {
            continue;
        }
        const bool supportsFunction = std::any_of(
            plugin->functions.cbegin(), plugin->functions.cend(),
            [&](const PluginFunctionDefinition& function) {
                return function.id == functionId;
            });
        if (supportsFunction) {
            compatibleDevices.push_back(iterator.key());
        }
    }
    compatibleDevices.removeDuplicates();
    compatibleDevices.sort(Qt::CaseInsensitive);
    for (const auto& deviceId : compatibleDevices) {
        m_deviceIdCombo->addItem(deviceId, deviceId);
    }

    int selected = m_deviceIdCombo->findData(currentDeviceId);
    if (!currentDeviceId.isEmpty() && selected < 0) {
        m_deviceIdCombo->insertItem(
            0, tr("%1 (Unavailable)").arg(currentDeviceId), currentDeviceId);
        selected = 0;
    }
    if (m_deviceIdCombo->count() == 0) {
        m_deviceIdCombo->addItem(tr("No compatible Station device"),
                                 QVariant{});
        m_deviceIdCombo->setEnabled(false);
    } else {
        m_deviceIdCombo->setEnabled(m_editable && !m_previewing);
        m_deviceIdCombo->setCurrentIndex(selected >= 0 ? selected : 0);
    }
}

const PluginFunctionDefinition* StepPropertyEditor::currentPluginFunction() const
{
    auto moduleId = m_moduleIdEdit->text().trimmed();
    if (moduleId == QStringLiteral("device")) {
        auto deviceId = m_deviceIdCombo
            ? m_deviceIdCombo->currentData().toString() : QString{};
        if (deviceId.isEmpty()) {
            QJsonObject inputs;
            QString ignoredError;
            if (parseObjectText(m_inputsEdit->toPlainText(), inputs,
                                ignoredError)) {
                deviceId = inputs.value(QStringLiteral("deviceId")).toString();
            }
        }
        moduleId = m_pluginByDeviceId.value(deviceId);
    }
    const auto functionId = m_functionEdit->currentData().toString();
    for (const auto& plugin : m_plugins) {
        if (plugin.moduleId != moduleId) {
            continue;
        }
        for (const auto& function : plugin.functions) {
            if (function.id == functionId) {
                return &function;
            }
        }
    }
    return nullptr;
}

void StepPropertyEditor::setDevicePluginBindings(
    QHash<QString, QString> pluginByDeviceId)
{
    m_pluginByDeviceId = std::move(pluginByDeviceId);
    rebuildFunctionChoices();
    rebuildDeviceChoices();
    rebuildPluginInputEditors();
    updateAdvancedJsonVisibility();
}

void StepPropertyEditor::setDeviceConfigurations(
    QHash<QString, QJsonObject> deviceConfigurations)
{
    m_deviceConfigurations = std::move(deviceConfigurations);
    rebuildPluginInputEditors();
}

void StepPropertyEditor::rebuildPluginInputEditors()
{
    if (!m_pluginInputsForm || !m_pluginInputsGroup) {
        return;
    }
    while (m_pluginInputsForm->rowCount() > 0) {
        const auto row = m_pluginInputsForm->takeRow(0);
        if (row.labelItem) {
            delete row.labelItem->widget();
            delete row.labelItem;
        }
        if (row.fieldItem) {
            delete row.fieldItem->widget();
            delete row.fieldItem;
        }
    }
    m_pluginInputEditors.clear();
    m_parserFieldsField = nullptr;
    m_parserFieldsTable = nullptr;
    m_parserFieldAddButton = nullptr;
    m_parserFieldRemoveButton = nullptr;

    const auto* function = currentPluginFunction();
    const auto kind = m_kindCombo->currentData().toString();
    const bool moduleCall = !m_isGroup &&
        (kind == QStringLiteral("action") ||
         kind == QStringLiteral("cleanup"));
    if (!function || function->inputs.isEmpty() || !moduleCall) {
        m_pluginInputsGroup->hide();
        return;
    }
    const bool canFunction = std::any_of(
        m_plugins.cbegin(), m_plugins.cend(), [function](const auto& plugin) {
            return plugin.category.compare(QStringLiteral("CAN"), Qt::CaseInsensitive) == 0 &&
                   std::any_of(plugin.functions.cbegin(), plugin.functions.cend(),
                               [function](const auto& candidate) {
                                   return &candidate == function;
                               });
        });

    QJsonObject currentInputs;
    QString ignoredError;
    parseObjectText(m_inputsEdit->toPlainText(), currentInputs, ignoredError);
    const auto moduleId = m_moduleIdEdit->text().trimmed();
    const auto functionId = m_functionEdit->currentData().toString();
    const bool logicalDeviceConnection = moduleId == QStringLiteral("device") &&
        (functionId.compare(QStringLiteral("open"), Qt::CaseInsensitive) == 0 ||
         functionId.compare(QStringLiteral("connect"), Qt::CaseInsensitive) == 0 ||
         functionId.compare(QStringLiteral("connectCan"), Qt::CaseInsensitive) == 0);
    const auto deviceId = currentInputs.value(QStringLiteral("deviceId")).toString();
    const auto stationInputs = m_deviceConfigurations.value(deviceId);
    m_pluginInputsGroup->setTitle(logicalDeviceConnection
        ? tr("Connection Settings - Station Config")
        : tr("Arguments - %1").arg(function->name));

    for (const auto& definition : function->inputs) {
        QVariant value;
        const auto current = currentInputs.value(definition.key);
        const bool inheritedFromStation = logicalDeviceConnection;
        if (inheritedFromStation && stationInputs.contains(definition.key)) {
            value = stationInputs.value(definition.key).toVariant();
        } else if (!inheritedFromStation && !current.isUndefined()) {
            value = current.toVariant();
        } else if (definition.defaultValue.isValid()) {
            value = definition.defaultValue;
        }

        QWidget* editor = nullptr;
        QWidget* customFieldWidget = nullptr;
        if (definition.type == PluginParameterType::Boolean) {
            if (!definition.required && !definition.defaultValue.isValid()) {
                auto* combo = new QComboBox(m_pluginInputsGroup);
                combo->addItem(tr("Not set"), QVariant{});
                combo->addItem(tr("ON"), true);
                combo->addItem(tr("OFF"), false);
                const int selected = value.isValid()
                    ? combo->findData(value.toBool()) : 0;
                combo->setCurrentIndex(selected >= 0 ? selected : 0);
                editor = combo;
            } else {
                auto* toggle = new OnOffSwitch(m_pluginInputsGroup);
                toggle->setChecked(value.toBool());
                editor = toggle;
            }
        } else if (definition.type == PluginParameterType::Enumeration) {
            auto* combo = new QComboBox(m_pluginInputsGroup);
            if (!value.isValid()) {
                combo->addItem(definition.required ? tr("Select...")
                                                   : tr("Not set"),
                               QVariant{});
            }
            for (const auto& option : definition.options) {
                combo->addItem(option.label, option.value);
            }
            const int selected = value.isValid() ? combo->findData(value) : 0;
            combo->setCurrentIndex(selected >= 0 ? selected : 0);
            editor = combo;
        } else if (definition.type == PluginParameterType::ExpressionList) {
            auto* container = new QWidget(m_pluginInputsGroup);
            auto* layout = new QVBoxLayout(container);
            layout->setContentsMargins(0, 0, 0, 0);
            layout->setSpacing(4);

            auto* table = new QTableWidget(container);
            table->setColumnCount(2);
            table->setHorizontalHeaderLabels({tr("Name"), tr("Value / Expression")});
            table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
            table->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
            table->verticalHeader()->hide();
            table->setSelectionBehavior(QAbstractItemView::SelectRows);
            table->setSelectionMode(QAbstractItemView::SingleSelection);
            table->verticalHeader()->setMinimumSectionSize(38);
            table->verticalHeader()->setDefaultSectionSize(38);
            table->setMinimumHeight(150);

            const auto configured = value.toList();
            for (int valueIndex = 0; valueIndex < configured.size(); ++valueIndex) {
                const auto item = configured[valueIndex].toMap();
                appendExpressionListRow(
                    table,
                    item.value(QStringLiteral("name"),
                               QStringLiteral("Value %1").arg(valueIndex + 1)).toString(),
                    item.contains(QStringLiteral("value"))
                        ? item.value(QStringLiteral("value"))
                        : configured[valueIndex]);
            }
            if (table->rowCount() == 0) {
                appendExpressionListRow(table, tr("Value 1"), {});
            }

            auto* controls = new QWidget(container);
            auto* controlsLayout = new QHBoxLayout(controls);
            controlsLayout->setContentsMargins(0, 0, 0, 0);
            controlsLayout->setSpacing(8);
            auto* add = new QToolButton(controls);
            add->setObjectName(QStringLiteral("expressionListAddButton"));
            add->setText(tr("Add value"));
            add->setIcon(QIcon(QStringLiteral(":/icons/list-plus.svg")));
            add->setIconSize(QSize(16, 16));
            add->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
            add->setToolTip(tr("Add value"));
            add->setMinimumSize(96, 30);
            auto* remove = new QToolButton(controls);
            remove->setObjectName(QStringLiteral("expressionListRemoveButton"));
            remove->setText(tr("Remove"));
            remove->setIcon(QIcon(QStringLiteral(":/icons/trash-2.svg")));
            remove->setIconSize(QSize(16, 16));
            remove->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
            remove->setToolTip(tr("Remove selected value"));
            remove->setMinimumSize(88, 30);
            const auto controlStyle = QStringLiteral(
                "QToolButton { background: #ffffff; color: #2d3943; "
                "border: 1px solid #b7c1c9; border-radius: 4px; "
                "padding: 3px 9px; font-weight: 600; }"
                "QToolButton:hover { background: #e8f3f9; border-color: #568aa7; }"
                "QToolButton:pressed { background: #d8eaf4; border-color: #3f7898; }"
                "QToolButton:disabled { color: #a7adb3; background: #f3f5f6; "
                "border-color: #d7dde1; }");
            add->setStyleSheet(controlStyle);
            remove->setStyleSheet(controlStyle);
            controlsLayout->addWidget(add);
            controlsLayout->addWidget(remove);
            controlsLayout->addStretch(1);
            layout->addWidget(table);
            layout->addWidget(controls);

            connect(add, &QToolButton::clicked, this, [this, table] {
                appendExpressionListRow(
                    table,
                    tr("Value %1").arg(table->rowCount() + 1),
                    {});
                table->selectRow(table->rowCount() - 1);
                markDraftDirty();
            });
            connect(remove, &QToolButton::clicked, this, [this, table] {
                const int row = table->currentRow();
                if (row < 0) {
                    return;
                }
                table->removeRow(row);
                markDraftDirty();
            });

            editor = table;
            customFieldWidget = container;
        } else {
            auto* edit = new QLineEdit(m_pluginInputsGroup);
            if (value.isValid()) {
                edit->setText(value.toString());
            }
            if (definition.type == PluginParameterType::HexBytes) {
                edit->setPlaceholderText(tr("Example: 01 02 03 04"));
            } else if (canFunction && definition.key == QStringLiteral("id")) {
                edit->setPlaceholderText(tr("0x000-0x7FF"));
            } else if (canFunction && definition.key == QStringLiteral("filterId")) {
                edit->setPlaceholderText(tr("0x000-0x7FF"));
            } else if (canFunction && definition.key == QStringLiteral("filterMask")) {
                edit->setPlaceholderText(tr("0x000=Any; 0x7FF=Exact"));
            } else if (definition.type == PluginParameterType::Integer ||
                       definition.type == PluginParameterType::Number) {
                QString hint = definition.type == PluginParameterType::Integer
                    ? tr("Integer or ${expression}")
                    : tr("Number or ${expression}");
                if (definition.minimum || definition.maximum) {
                    hint += tr("  range %1 .. %2")
                        .arg(definition.minimum
                                 ? QString::number(*definition.minimum)
                                 : QStringLiteral("-inf"),
                             definition.maximum
                                 ? QString::number(*definition.maximum)
                                 : QStringLiteral("+inf"));
                }
                edit->setPlaceholderText(hint);
            }
            editor = edit;
        }

        editor->setProperty("stationInherited", inheritedFromStation);
        auto objectName = definition.key;
        objectName.replace(QLatin1Char('.'), QLatin1Char('_'));
        editor->setObjectName(QStringLiteral("pluginInput_%1").arg(objectName));
        editor->setEnabled(m_editable && !inheritedFromStation);
        QString tooltip;
        if (inheritedFromStation) {
            tooltip = tr("Inherited from Station Config and not stored in this Step");
        }
        if (!definition.unit.isEmpty()) {
            if (!tooltip.isEmpty()) {
                tooltip += QLatin1Char('\n');
            }
            tooltip += tr("Unit: %1").arg(definition.unit);
        }
        editor->setToolTip(tooltip);
        QString displayName = definition.name;
        if (canFunction) {
            if (definition.key == QStringLiteral("id")) {
                displayName = tr("CAN ID");
            } else if (definition.key == QStringLiteral("filterId")) {
                displayName = tr("Filter ID");
            } else if (definition.key == QStringLiteral("filterMask")) {
                displayName = tr("Filter Mask");
            }
        }
        auto label = definition.required
            ? tr("%1 *").arg(displayName)
            : displayName;
        if (inheritedFromStation) {
            label += tr(" (Station)");
        } else if (logicalDeviceConnection) {
            label += tr(" (Step override)");
        }
        auto* fieldWidget = customFieldWidget ? customFieldWidget : editor;
        if (auto* lineEdit = qobject_cast<QLineEdit*>(editor)) {
            fieldWidget = wrapExpressionEditor(lineEdit);
            if (inheritedFromStation) {
                for (auto* button : fieldWidget->findChildren<QToolButton*>()) {
                    button->setProperty("stationInherited", true);
                    button->setEnabled(false);
                }
            }
        }
        if (inheritedFromStation) {
            m_pluginInputsForm->addRow(label, fieldWidget);
        } else {
            addInspectableRow(m_pluginInputsForm,
                              label,
                              fieldWidget,
                              QStringLiteral("inputs.%1").arg(definition.key),
                              displayName);
        }
        m_pluginInputEditors.push_back(
            {definition, editor, fieldWidget, inheritedFromStation});
        observeDraftWidget(editor);
    }

    if (!parserFieldSourceKey().isEmpty()) {
        m_parserFieldsField = new QWidget(m_pluginInputsGroup);
        auto* mappingLayout = new QVBoxLayout(m_parserFieldsField);
        mappingLayout->setContentsMargins(0, 0, 0, 0);
        mappingLayout->setSpacing(4);

        m_parserFieldsTable = new QTableWidget(m_parserFieldsField);
        m_parserFieldsTable->setObjectName(
            QStringLiteral("parserNamedFieldsTable"));
        m_parserFieldsTable->setColumnCount(3);
        m_parserFieldsTable->setHorizontalHeaderLabels(
            {parserFieldSourceKey() == QStringLiteral("group")
                 ? tr("Capture Group") : tr("Field Index"),
             tr("Output Name"), tr("Output Type")});
        m_parserFieldsTable->horizontalHeader()->setSectionResizeMode(
            0, QHeaderView::ResizeToContents);
        m_parserFieldsTable->horizontalHeader()->setSectionResizeMode(
            1, QHeaderView::Stretch);
        m_parserFieldsTable->horizontalHeader()->setSectionResizeMode(
            2, QHeaderView::ResizeToContents);
        m_parserFieldsTable->verticalHeader()->hide();
        m_parserFieldsTable->setSelectionBehavior(
            QAbstractItemView::SelectRows);
        m_parserFieldsTable->setSelectionMode(
            QAbstractItemView::SingleSelection);
        m_parserFieldsTable->setMinimumHeight(128);
        m_parserFieldsTable->setMaximumHeight(220);
        mappingLayout->addWidget(m_parserFieldsTable);

        auto* buttonLayout = new QHBoxLayout;
        buttonLayout->setContentsMargins(0, 0, 0, 0);
        m_parserFieldAddButton = new QToolButton(m_parserFieldsField);
        m_parserFieldAddButton->setObjectName(
            QStringLiteral("parserNamedFieldAddButton"));
        m_parserFieldAddButton->setText(QStringLiteral("+"));
        m_parserFieldAddButton->setToolTip(tr("Add named output"));
        m_parserFieldAddButton->setFixedSize(28, 28);
        m_parserFieldRemoveButton = new QToolButton(m_parserFieldsField);
        m_parserFieldRemoveButton->setObjectName(
            QStringLiteral("parserNamedFieldRemoveButton"));
        m_parserFieldRemoveButton->setText(QStringLiteral("-"));
        m_parserFieldRemoveButton->setToolTip(tr("Remove selected output"));
        m_parserFieldRemoveButton->setFixedSize(28, 28);
        buttonLayout->addWidget(m_parserFieldAddButton);
        buttonLayout->addWidget(m_parserFieldRemoveButton);
        buttonLayout->addStretch(1);
        mappingLayout->addLayout(buttonLayout);

        for (const auto& value : currentInputs
                                      .value(QStringLiteral("fields"))
                                      .toArray()) {
            if (!value.isObject()) {
                continue;
            }
            const auto field = value.toObject();
            appendParserFieldMappingRow(
                field.value(parserFieldSourceKey()).toInt(),
                field.value(QStringLiteral("name")).toString(),
                field.value(QStringLiteral("type"))
                    .toString(QStringLiteral("string")));
        }

        connect(m_parserFieldAddButton, &QToolButton::clicked, this, [this] {
            if (!m_parserFieldsTable || m_parserFieldsTable->rowCount() >= 128) {
                return;
            }
            const int row = m_parserFieldsTable->rowCount();
            appendParserFieldMappingRow(
                row, QStringLiteral("field%1").arg(row + 1),
                QStringLiteral("string"));
            m_parserFieldsTable->selectRow(row);
            markDraftDirty();
        });
        connect(m_parserFieldRemoveButton, &QToolButton::clicked, this, [this] {
            if (!m_parserFieldsTable) {
                return;
            }
            const int row = m_parserFieldsTable->currentRow();
            if (row < 0) {
                return;
            }
            m_parserFieldsTable->removeRow(row);
            m_parserFieldRemoveButton->setEnabled(
                m_editable && m_parserFieldsTable->rowCount() > 0);
            markDraftDirty();
        });
        connect(m_parserFieldsTable, &QTableWidget::currentCellChanged,
                this, [this](int currentRow) {
            if (m_parserFieldRemoveButton) {
                m_parserFieldRemoveButton->setEnabled(
                    m_editable && currentRow >= 0);
            }
        });
        m_pluginInputsForm->addRow(tr("Named Outputs"), m_parserFieldsField);

        for (const auto& item : std::as_const(m_pluginInputEditors)) {
            if (item.definition.key != QStringLiteral("resultMode")) {
                continue;
            }
            if (auto* combo = qobject_cast<QComboBox*>(item.widget)) {
                connect(combo, &QComboBox::currentIndexChanged, this,
                        [this] { updateParserFieldMappingVisibility(); });
            }
            break;
        }
        updateParserFieldMappingVisibility();
    }
    if (canFunction) {
        for (const auto& item : std::as_const(m_pluginInputEditors)) {
            if (item.definition.key != QStringLiteral("extended")) {
                continue;
            }
            if (auto* button = qobject_cast<QAbstractButton*>(item.widget)) {
                connect(button, &QAbstractButton::toggled, this,
                        [this] { refreshCanIdentifierHints(); });
            } else if (auto* combo = qobject_cast<QComboBox*>(item.widget)) {
                connect(combo, &QComboBox::currentIndexChanged, this,
                        [this] { refreshCanIdentifierHints(); });
            }
        }
        refreshCanIdentifierHints();
    }
    for (const auto& item : std::as_const(m_pluginInputEditors)) {
        if (item.definition.key != QStringLiteral("operation")) {
            continue;
        }
        if (auto* combo = qobject_cast<QComboBox*>(item.widget)) {
            connect(combo, &QComboBox::currentIndexChanged, this,
                    [this] { updateValueToolInputVisibility(); });
        }
    }
    QSet<QString> visibilityControllers;
    for (const auto& item : std::as_const(m_pluginInputEditors)) {
        if (!item.definition.visibleWhenKey.isEmpty()) {
            visibilityControllers.insert(item.definition.visibleWhenKey);
        }
    }
    for (const auto& item : std::as_const(m_pluginInputEditors)) {
        if (!visibilityControllers.contains(item.definition.key)) {
            continue;
        }
        if (auto* combo = qobject_cast<QComboBox*>(item.widget)) {
            connect(combo, &QComboBox::currentIndexChanged, this,
                    [this] { updateConditionalPluginInputVisibility(); });
        } else if (auto* button = qobject_cast<QAbstractButton*>(item.widget)) {
            connect(button, &QAbstractButton::toggled, this,
                    [this] { updateConditionalPluginInputVisibility(); });
        } else if (auto* edit = qobject_cast<QLineEdit*>(item.widget)) {
            connect(edit, &QLineEdit::textChanged, this,
                    [this] { updateConditionalPluginInputVisibility(); });
        }
    }
    updateConditionalPluginInputVisibility();
    updateValueToolInputVisibility();
    m_pluginInputsGroup->show();
}

QString StepPropertyEditor::parserFieldSourceKey() const
{
    if (!m_moduleIdEdit || !m_functionEdit ||
        m_moduleIdEdit->text().trimmed().compare(
            QStringLiteral("builtin.data-parser"),
            Qt::CaseInsensitive) != 0) {
        return {};
    }
    const auto function = m_functionEdit->currentData().toString();
    if (function.compare(QStringLiteral("splitText"),
                         Qt::CaseInsensitive) == 0) {
        return QStringLiteral("index");
    }
    if (function.compare(QStringLiteral("regexCapture"),
                         Qt::CaseInsensitive) == 0) {
        return QStringLiteral("group");
    }
    return {};
}

bool StepPropertyEditor::isMultipleFieldParserFunction() const
{
    if (parserFieldSourceKey().isEmpty()) {
        return false;
    }
    for (const auto& item : m_pluginInputEditors) {
        if (item.definition.key != QStringLiteral("resultMode")) {
            continue;
        }
        if (const auto* combo = qobject_cast<const QComboBox*>(item.widget)) {
            return combo->currentData().toString().compare(
                       QStringLiteral("multiple"),
                       Qt::CaseInsensitive) == 0;
        }
    }
    return false;
}

void StepPropertyEditor::appendParserFieldMappingRow(
    int sourceIndex, const QString& name, const QString& outputType)
{
    if (!m_parserFieldsTable) {
        return;
    }
    const int row = m_parserFieldsTable->rowCount();
    m_parserFieldsTable->insertRow(row);

    auto* source = new QSpinBox(m_parserFieldsTable);
    source->setObjectName(
        QStringLiteral("parserNamedFieldSource_%1").arg(row));
    if (parserFieldSourceKey() == QStringLiteral("group")) {
        source->setRange(0, 1000);
    } else {
        source->setRange(-100000, 100000);
    }
    source->setValue(sourceIndex);
    source->setToolTip(parserFieldSourceKey() == QStringLiteral("group")
        ? tr("Regular-expression capture group; group 0 is the full match")
        : tr("Zero-based field index; negative values count from the end"));

    auto* outputName = new QLineEdit(m_parserFieldsTable);
    outputName->setObjectName(
        QStringLiteral("parserNamedFieldName_%1").arg(row));
    outputName->setPlaceholderText(tr("Example: SN1"));
    outputName->setText(name);
    outputName->setToolTip(
        tr("Starts with a letter or underscore; use letters, digits, and underscores"));

    auto* type = new QComboBox(m_parserFieldsTable);
    type->setObjectName(
        QStringLiteral("parserNamedFieldType_%1").arg(row));
    type->addItem(tr("String"), QStringLiteral("string"));
    type->addItem(tr("Signed Integer"), QStringLiteral("integer"));
    type->addItem(tr("Unsigned Integer"), QStringLiteral("unsigned"));
    type->addItem(tr("Number"), QStringLiteral("number"));
    type->addItem(tr("Boolean"), QStringLiteral("boolean"));
    type->addItem(tr("Hex Number"), QStringLiteral("hex"));
    const int selectedType = type->findData(outputType.trimmed().toLower());
    type->setCurrentIndex(selectedType >= 0 ? selectedType : 0);

    m_parserFieldsTable->setCellWidget(row, 0, source);
    m_parserFieldsTable->setCellWidget(row, 1, outputName);
    m_parserFieldsTable->setCellWidget(row, 2, type);
    observeDraftWidget(source);
    observeDraftWidget(outputName);
    observeDraftWidget(type);
    if (m_parserFieldAddButton) {
        m_parserFieldAddButton->setEnabled(
            m_editable && m_parserFieldsTable->rowCount() < 128);
    }
}

void StepPropertyEditor::updateParserFieldMappingVisibility()
{
    if (!m_pluginInputsForm || parserFieldSourceKey().isEmpty()) {
        return;
    }
    const bool multiple = isMultipleFieldParserFunction();
    for (const auto& item : std::as_const(m_pluginInputEditors)) {
        const bool singleOnly = item.definition.key == QStringLiteral("fieldIndex") ||
                                item.definition.key == QStringLiteral("captureGroup") ||
                                item.definition.key == QStringLiteral("outputType");
        if (singleOnly) {
            item.fieldWidget->setProperty("parserInputInactive", multiple);
        }
    }
    refreshPluginInputRowVisibility();
    setFormRowVisible(m_pluginInputsForm, m_parserFieldsField, multiple);
    if (multiple && m_parserFieldsTable &&
        m_parserFieldsTable->rowCount() == 0) {
        appendParserFieldMappingRow(0, QStringLiteral("field1"),
                                    QStringLiteral("string"));
    }
    if (m_parserFieldAddButton) {
        m_parserFieldAddButton->setEnabled(
            m_editable && multiple && m_parserFieldsTable &&
            m_parserFieldsTable->rowCount() < 128);
    }
    if (m_parserFieldRemoveButton) {
        m_parserFieldRemoveButton->setEnabled(
            m_editable && multiple && m_parserFieldsTable &&
            m_parserFieldsTable->currentRow() >= 0);
    }
}

bool StepPropertyEditor::mergeParserFieldMappings(
    QJsonObject& inputs, QString& errorMessage, QWidget** invalidWidget) const
{
    const auto sourceKey = parserFieldSourceKey();
    if (sourceKey.isEmpty()) {
        return true;
    }
    if (!isMultipleFieldParserFunction()) {
        inputs.remove(QStringLiteral("fields"));
        return true;
    }
    inputs.remove(sourceKey == QStringLiteral("group")
                      ? QStringLiteral("captureGroup")
                      : QStringLiteral("fieldIndex"));
    inputs.remove(QStringLiteral("outputType"));
    if (!m_parserFieldsTable || m_parserFieldsTable->rowCount() == 0) {
        errorMessage = tr("Add at least one named output");
        if (invalidWidget) {
            *invalidWidget = m_parserFieldsField;
        }
        return false;
    }

    static const QRegularExpression namePattern(
        QStringLiteral("^[A-Za-z_][A-Za-z0-9_]*$"));
    QSet<QString> names;
    QJsonArray mappings;
    for (int row = 0; row < m_parserFieldsTable->rowCount(); ++row) {
        const auto* source = qobject_cast<const QSpinBox*>(
            m_parserFieldsTable->cellWidget(row, 0));
        const auto* nameEdit = qobject_cast<const QLineEdit*>(
            m_parserFieldsTable->cellWidget(row, 1));
        const auto* type = qobject_cast<const QComboBox*>(
            m_parserFieldsTable->cellWidget(row, 2));
        if (!source || !nameEdit || !type) {
            errorMessage = tr("Named output row %1 is incomplete").arg(row + 1);
            if (invalidWidget) {
                *invalidWidget = m_parserFieldsTable;
            }
            return false;
        }
        const auto name = nameEdit->text().trimmed();
        if (!namePattern.match(name).hasMatch()) {
            errorMessage = tr(
                "Output name '%1' must start with a letter or underscore and "
                "contain only letters, digits, or underscores").arg(name);
            if (invalidWidget) {
                *invalidWidget = const_cast<QLineEdit*>(nameEdit);
            }
            return false;
        }
        const auto nameKey = name.toCaseFolded();
        if (names.contains(nameKey)) {
            errorMessage = tr("Output name '%1' is duplicated").arg(name);
            if (invalidWidget) {
                *invalidWidget = const_cast<QLineEdit*>(nameEdit);
            }
            return false;
        }
        names.insert(nameKey);
        mappings.push_back(QJsonObject{
            {sourceKey, source->value()},
            {QStringLiteral("name"), name},
            {QStringLiteral("type"), type->currentData().toString()}});
    }
    inputs.insert(QStringLiteral("fields"), mappings);
    return true;
}

void StepPropertyEditor::refreshCanIdentifierHints()
{
    bool extended = false;
    for (const auto& item : std::as_const(m_pluginInputEditors)) {
        if (item.definition.key != QStringLiteral("extended")) {
            continue;
        }
        if (const auto* button = qobject_cast<const QAbstractButton*>(item.widget)) {
            extended = button->isChecked();
        } else if (const auto* combo = qobject_cast<const QComboBox*>(item.widget)) {
            extended = combo->currentData().toBool();
        }
        break;
    }

    for (const auto& item : std::as_const(m_pluginInputEditors)) {
        auto* edit = qobject_cast<QLineEdit*>(item.widget);
        if (!edit) {
            continue;
        }
        if (item.definition.key == QStringLiteral("id") ||
            item.definition.key == QStringLiteral("filterId")) {
            edit->setPlaceholderText(
                extended ? tr("0x00000000-0x1FFFFFFF")
                         : tr("0x000-0x7FF"));
        } else if (item.definition.key == QStringLiteral("filterMask")) {
            edit->setPlaceholderText(
                extended ? tr("0x00000000=Any; 0x1FFFFFFF=Exact")
                         : tr("0x000=Any; 0x7FF=Exact"));
        }
    }
}

QWidget* StepPropertyEditor::wrapExpressionEditor(QLineEdit* editor)
{
    auto* container = new QWidget(editor->parentWidget());
    auto* layout = new QHBoxLayout(container);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(4);
    layout->addWidget(editor, 1);

    auto* button = new QToolButton(container);
    button->setObjectName(QStringLiteral("expressionPickerButton"));
    button->setText(QStringLiteral("fx"));
    button->setToolTip(tr("Insert a sequence variable or previous Step output"));
    button->setFixedSize(28, 28);
    styleExpressionPickerButton(button);
    connect(button, &QToolButton::clicked, this,
            [this, button, editor] {
        QMenu sourceMenu(button);
        rebuildExpressionMenu(&sourceMenu, editor);
        const auto location = m_document
            ? expressionPickerLocation(m_document->rootObject(), m_path)
            : ExpressionPickerLocation{};
        const auto expression = selectExpressionFromMenu(
            &sourceMenu, location, button->window());
        if (expression.isEmpty()) {
            return;
        }
        editor->setText(expression);
        editor->setFocus();
        editor->selectAll();
    });
    layout->addWidget(button);
    return container;
}

void StepPropertyEditor::updateValueToolInputVisibility()
{
    if (!m_pluginInputsForm || !m_moduleIdEdit || !m_functionEdit ||
        m_moduleIdEdit->text().trimmed().compare(
            QStringLiteral("builtin.value-tools"), Qt::CaseInsensitive) != 0 ||
        m_functionEdit->currentData().toString().compare(
            QStringLiteral("calculate"), Qt::CaseInsensitive) != 0) {
        return;
    }

    QString operation;
    for (const auto& item : std::as_const(m_pluginInputEditors)) {
        if (item.definition.key == QStringLiteral("operation")) {
            if (const auto* combo = qobject_cast<const QComboBox*>(item.widget)) {
                operation = combo->currentData().toString().trimmed().toLower();
            }
            break;
        }
    }
    const QSet<QString> unary = {
        QStringLiteral("absolute"), QStringLiteral("negate"),
        QStringLiteral("squareroot"), QStringLiteral("round"),
        QStringLiteral("floor"), QStringLiteral("ceil"),
        QStringLiteral("clamp")};

    for (const auto& item : std::as_const(m_pluginInputEditors)) {
        bool visible = true;
        if (item.definition.key == QStringLiteral("b")) {
            visible = !unary.contains(operation);
        } else if (item.definition.key == QStringLiteral("minimum") ||
                   item.definition.key == QStringLiteral("maximum")) {
            visible = operation == QStringLiteral("clamp");
        } else if (item.definition.key == QStringLiteral("decimals")) {
            visible = operation == QStringLiteral("round");
        }
        item.fieldWidget->setProperty("valueToolInactive", !visible);
    }
    refreshPluginInputRowVisibility();
}

void StepPropertyEditor::updateConditionalPluginInputVisibility()
{
    if (!m_pluginInputsForm) {
        return;
    }
    const auto currentValue = [this](const QString& key) -> QVariant {
        const auto item = std::find_if(
            m_pluginInputEditors.cbegin(), m_pluginInputEditors.cend(),
            [&key](const PluginInputEditor& candidate) {
                return candidate.definition.key == key;
            });
        if (item == m_pluginInputEditors.cend()) {
            return {};
        }
        if (const auto* combo = qobject_cast<const QComboBox*>(item->widget)) {
            return combo->currentData();
        }
        if (const auto* button =
                qobject_cast<const QAbstractButton*>(item->widget)) {
            return button->isChecked();
        }
        if (const auto* edit = qobject_cast<const QLineEdit*>(item->widget)) {
            return edit->text().trimmed();
        }
        return {};
    };

    for (const auto& item : std::as_const(m_pluginInputEditors)) {
        bool visible = item.definition.visibleWhenKey.isEmpty();
        if (!visible) {
            const auto controller = currentValue(item.definition.visibleWhenKey);
            visible = std::any_of(
                item.definition.visibleWhenValues.cbegin(),
                item.definition.visibleWhenValues.cend(),
                [&controller](const QVariant& allowed) {
                    return controller == allowed ||
                           controller.toString() == allowed.toString();
                });
        }
        item.fieldWidget->setProperty("pluginInputInactive", !visible);
    }
    refreshPluginInputRowVisibility();
}

void StepPropertyEditor::refreshPluginInputRowVisibility()
{
    if (!m_pluginInputsForm) {
        return;
    }
    for (const auto& item : std::as_const(m_pluginInputEditors)) {
        const bool visible =
            !item.fieldWidget->property("pluginInputInactive").toBool() &&
            !item.fieldWidget->property("valueToolInactive").toBool() &&
            !item.fieldWidget->property("parserInputInactive").toBool();
        setFormRowVisible(m_pluginInputsForm, item.fieldWidget, visible);
    }
}

void StepPropertyEditor::appendExpressionListRow(QTableWidget* table,
                                                 const QString& name,
                                                 const QVariant& value)
{
    if (!table) {
        return;
    }
    const int row = table->rowCount();
    table->insertRow(row);

    auto* nameEdit = new QLineEdit(table);
    nameEdit->setObjectName(QStringLiteral("expressionListName"));
    nameEdit->setFixedHeight(32);
    nameEdit->setText(name);
    auto* valueEdit = new QLineEdit(table);
    valueEdit->setObjectName(QStringLiteral("expressionListValue"));
    valueEdit->setFixedHeight(32);
    valueEdit->setPlaceholderText(tr("Number or ${step:...outputs...}"));
    if (value.isValid()) {
        valueEdit->setText(value.toString());
    }
    auto* valueField = wrapExpressionEditor(valueEdit);
    valueField->setFixedHeight(32);
    table->setCellWidget(row, 0, nameEdit);
    table->setCellWidget(row, 1, valueField);
    table->setRowHeight(row, 38);
    observeDraftWidget(nameEdit);
    observeDraftWidget(valueEdit);
}

void StepPropertyEditor::rebuildExpressionMenu(QMenu* menu, QLineEdit* editor)
{
    if (!menu || !editor) {
        return;
    }
    menu->clear();
    int variableCount = 0;
    auto* runtimeMenu = menu->addMenu(tr("Runtime Values"));
    const std::array<std::pair<QString, QString>, 11> runtimeValues = {{
        {tr("Serial Number (SN)"), QStringLiteral("${var.serialNumber}")},
        {tr("Serial Number (legacy alias)"), QStringLiteral("${sn}")},
        {tr("UUT Serial Number"), QStringLiteral("${uut.serialNumber}")},
        {tr("UUT ID"), QStringLiteral("${uut.id}")},
        {tr("UUT Index (0-based)"), QStringLiteral("${uut.index}")},
        {tr("UUT Number (1-based)"), QStringLiteral("${uut.number}")},
        {tr("UUT Slot (1-based)"), QStringLiteral("${uut.slot}")},
        {tr("Frame ID"), QStringLiteral("${frame.id}")},
        {tr("Attempt ID"), QStringLiteral("${attempt.id}")},
        {tr("Attempt Index (0-based)"), QStringLiteral("${attempt.index}")},
        {tr("Attempt Number (1-based)"), QStringLiteral("${attempt.number}")},
    }};
    for (const auto& [label, expression] : runtimeValues) {
        auto* action = runtimeMenu->addAction(label);
        action->setData(expression);
        action->setToolTip(expression);
        connect(action, &QAction::triggered, editor, [editor, expression] {
            editor->setText(expression);
            editor->setFocus();
            editor->selectAll();
        });
        ++variableCount;
    }
    if (m_document) {
        const auto definitions = m_document->sequenceVariables();
        QMenu* variableMenu = nullptr;
        for (const auto& value : definitions) {
            if (!value.isObject()) {
                continue;
            }
            const auto definition = value.toObject();
            const auto name = definition.value(QStringLiteral("name"))
                                  .toString().trimmed();
            if (name.isEmpty()) {
                continue;
            }
            if (!variableMenu) {
                variableMenu = menu->addMenu(tr("Sequence Variables"));
            }
            const auto type = definition.value(QStringLiteral("type"))
                                  .toString(QStringLiteral("string"));
            const auto scope = definition.value(QStringLiteral("scope"))
                                   .toString(QStringLiteral("shared"));
            auto* action = variableMenu->addAction(
                QStringLiteral("%1  [%2 / %3]").arg(name, type, scope));
            action->setData(QStringLiteral("${var.%1}").arg(name));
            action->setToolTip(
                definition.value(QStringLiteral("description")).toString());
            connect(action, &QAction::triggered, editor,
                    [editor, expression = QStringLiteral("${var.%1}").arg(name)] {
                        editor->setText(expression);
                        editor->setFocus();
                        editor->selectAll();
                    });
            ++variableCount;
        }
    }
    if (!m_isGroup &&
        m_kindCombo->currentData().toString() == QStringLiteral("action") &&
        m_periodicEnabledCheck->isChecked()) {
        auto* periodicMenu = menu->addMenu(tr("Periodic Task"));
        const std::array<std::pair<QString, QString>, 4> periodicValues = {{
            {tr("Counter value"), QStringLiteral("${periodic.counter}")},
            {tr("Invocation number (1-based)"), QStringLiteral("${periodic.number}")},
            {tr("Invocation index (0-based)"), QStringLiteral("${periodic.index}")},
            {tr("Request ID"), QStringLiteral("${periodic.requestId}")},
        }};
        for (const auto& [label, expression] : periodicValues) {
            auto* action = periodicMenu->addAction(label);
            action->setData(expression);
            action->setToolTip(expression);
            connect(action, &QAction::triggered, editor,
                    [editor, expression] {
                        editor->setText(expression);
                        editor->setFocus();
                        editor->selectAll();
                    });
            ++variableCount;
        }
    }
    const auto candidates = m_document
        ? buildStepOutputExpressionCandidates(
              m_document->rootObject(), m_path, m_plugins, m_pluginByDeviceId)
        : QVector<StepOutputExpressionCandidate>{};
    appendStepOutputExpressionMenus(
        menu,
        candidates,
        [editor](QMenu* sourceMenu,
                 const StepOutputExpressionCandidate& candidate,
                 const QString& tooltip) {
            auto* action = sourceMenu->addAction(
                QStringLiteral("%1 [%2]")
                    .arg(candidate.outputName, candidate.outputKey));
            action->setData(candidate.expression);
            action->setToolTip(tooltip);
            connect(action, &QAction::triggered, editor,
                    [editor, expression = candidate.expression] {
                        editor->setText(expression);
                        editor->setFocus();
                        editor->selectAll();
                    });
        });
    if (variableCount == 0 && candidates.isEmpty()) {
        auto* unavailable = menu->addAction(tr("No runtime values available"));
        unavailable->setEnabled(false);
    }
    if (auto* button = qobject_cast<QToolButton*>(menu->parentWidget())) {
        button->setEnabled(m_editable && !m_previewing);
    }
}

QWidget* StepPropertyEditor::wrapPromptMessageEditor(QPlainTextEdit* editor)
{
    auto* container = new QWidget(editor->parentWidget());
    auto* layout = new QHBoxLayout(container);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(4);
    layout->addWidget(editor, 1);

    auto* button = new QToolButton(container);
    button->setObjectName(QStringLiteral("promptValuePickerButton"));
    button->setText(QStringLiteral("fx"));
    button->setToolTip(tr("Insert a runtime value at the message cursor"));
    button->setFixedSize(28, 28);
    styleExpressionPickerButton(button);
    connect(button, &QToolButton::clicked, this,
            [this, button, editor] {
        QMenu sourceMenu(button);
        rebuildPromptExpressionMenu(&sourceMenu, editor);
        const auto location = m_document
            ? expressionPickerLocation(m_document->rootObject(), m_path)
            : ExpressionPickerLocation{};
        const auto expression = selectExpressionFromMenu(
            &sourceMenu, location, button->window());
        if (expression.isEmpty()) {
            return;
        }
        auto cursor = editor->textCursor();
        cursor.insertText(expression);
        editor->setTextCursor(cursor);
        editor->setFocus();
    });
    layout->addWidget(button, 0, Qt::AlignTop);
    return container;
}

void StepPropertyEditor::rebuildPromptExpressionMenu(
    QMenu* menu, QPlainTextEdit* editor)
{
    if (!menu || !editor) {
        return;
    }
    menu->clear();

    const auto addExpression = [editor](QMenu* target,
                                        const QString& label,
                                        const QString& expression,
                                        const QString& tooltip = {}) {
        auto* action = target->addAction(label);
        action->setData(expression);
        action->setToolTip(tooltip);
        QObject::connect(action, &QAction::triggered, editor,
                         [editor, expression] {
            auto cursor = editor->textCursor();
            cursor.insertText(expression);
            editor->setTextCursor(cursor);
            editor->setFocus();
        });
    };

    auto* runtimeMenu = menu->addMenu(tr("Runtime Values"));
    const std::array<std::pair<QString, QString>, 11> runtimeValues = {{
        {tr("Serial Number (SN)"), QStringLiteral("${var.serialNumber}")},
        {tr("Serial Number (legacy alias)"), QStringLiteral("${sn}")},
        {tr("UUT Serial Number"), QStringLiteral("${uut.serialNumber}")},
        {tr("UUT ID"), QStringLiteral("${uut.id}")},
        {tr("UUT Index (0-based)"), QStringLiteral("${uut.index}")},
        {tr("UUT Number (1-based)"), QStringLiteral("${uut.number}")},
        {tr("UUT Slot (1-based)"), QStringLiteral("${uut.slot}")},
        {tr("Frame ID"), QStringLiteral("${frame.id}")},
        {tr("Attempt ID"), QStringLiteral("${attempt.id}")},
        {tr("Attempt Index (0-based)"), QStringLiteral("${attempt.index}")},
        {tr("Attempt Number (1-based)"), QStringLiteral("${attempt.number}")},
    }};
    for (const auto& [label, expression] : runtimeValues) {
        addExpression(runtimeMenu, label, expression, expression);
    }

    if (m_document) {
        QMenu* variableMenu = nullptr;
        for (const auto& value : m_document->sequenceVariables()) {
            if (!value.isObject()) {
                continue;
            }
            const auto definition = value.toObject();
            const auto name = definition.value(QStringLiteral("name"))
                                  .toString().trimmed();
            if (name.isEmpty()) {
                continue;
            }
            if (!variableMenu) {
                variableMenu = menu->addMenu(tr("Sequence Variables"));
            }
            const auto type = definition.value(QStringLiteral("type"))
                                  .toString(QStringLiteral("string"));
            const auto scope = definition.value(QStringLiteral("scope"))
                                   .toString(QStringLiteral("shared"));
            addExpression(
                variableMenu,
                QStringLiteral("%1  [%2 / %3]").arg(name, type, scope),
                QStringLiteral("${var.%1}").arg(name),
                definition.value(QStringLiteral("description")).toString());
        }
    }

    const auto candidates = m_document
        ? buildStepOutputExpressionCandidates(
              m_document->rootObject(), m_path, m_plugins, m_pluginByDeviceId)
        : QVector<StepOutputExpressionCandidate>{};
    appendStepOutputExpressionMenus(
        menu,
        candidates,
        [&addExpression](QMenu* sourceMenu,
                         const StepOutputExpressionCandidate& candidate,
                         const QString& tooltip) {
            addExpression(sourceMenu,
                          QStringLiteral("%1 [%2]")
                              .arg(candidate.outputName, candidate.outputKey),
                          candidate.expression,
                          tooltip);
        });

    if (auto* button = qobject_cast<QToolButton*>(menu->parentWidget())) {
        button->setEnabled(m_editable && !m_previewing);
    }
}

void StepPropertyEditor::rebuildFailureJumpChoices(
    QComboBox* combo,
    const QString& selectedPath)
{
    if (!combo) {
        return;
    }

    const QSignalBlocker blocker(combo);
    const auto requested = selectedPath.trimmed();
    combo->clear();
    combo->addItem(tr("Select a later sibling step..."), QString{});
    const auto candidates = m_document && !m_previewing
        ? buildFollowingSiblingStepReferenceCandidates(
              m_document->rootObject(), m_path)
        : QVector<FollowingStepReferenceCandidate>{};
    for (const auto& candidate : candidates) {
        const auto label = candidate.stepName.trimmed().isEmpty()
            ? candidate.stepPath
            : QStringLiteral("%1 - %2").arg(candidate.stepPath, candidate.stepName);
        combo->addItem(label, candidate.stepPath);
    }

    int selectedIndex = requested.isEmpty() ? 0 : combo->findData(requested);
    if (selectedIndex < 0) {
        combo->addItem(
            tr("%1 (not an available later sibling)").arg(requested),
            requested);
        selectedIndex = combo->count() - 1;
        combo->setItemData(
            selectedIndex,
            tr("This target will fail compilation until it becomes a later sibling step"),
            Qt::ToolTipRole);
    }
    combo->setCurrentIndex(selectedIndex);
}

void StepPropertyEditor::updateFailureJumpVisibility()
{
    if (!m_policyForm) {
        return;
    }
    const auto update = [this](QComboBox* policy, QComboBox* target) {
        setFormRowVisible(
            m_policyForm,
            target,
            !m_isGroup && policy &&
                policy->currentData().toString() == QStringLiteral("JumpTo"));
    };
    update(m_onFailPolicyCombo, m_onFailTargetCombo);
    update(m_onErrorPolicyCombo, m_onErrorTargetCombo);
    update(m_onTimeoutPolicyCombo, m_onTimeoutTargetCombo);
}

void StepPropertyEditor::rebuildPromptCloseStepChoices(const QString& selectedPath)
{
    if (!m_promptCloseOnStepCombo) {
        return;
    }
    const auto requested = selectedPath.trimmed();
    m_promptCloseOnStepCombo->clear();
    m_promptCloseOnStepCombo->addItem(
        tr("Next enabled step (default)"), QString{});

    const auto candidates = m_document && !m_previewing
        ? buildFollowingStepReferenceCandidates(m_document->rootObject(), m_path)
        : QVector<FollowingStepReferenceCandidate>{};
    for (const auto& candidate : candidates) {
        const auto label = candidate.stepName.trimmed().isEmpty()
            ? candidate.stepPath
            : QStringLiteral("%1 - %2").arg(candidate.stepPath, candidate.stepName);
        m_promptCloseOnStepCombo->addItem(label, candidate.stepPath);
        const int index = m_promptCloseOnStepCombo->count() - 1;
        m_promptCloseOnStepCombo->setItemData(
            index,
            candidate.kind.isEmpty()
                ? tr("Close after this step finishes")
                : tr("%1 step; close after it finishes").arg(candidate.kind),
            Qt::ToolTipRole);
    }

    int selectedIndex = requested.isEmpty()
        ? 0
        : m_promptCloseOnStepCombo->findData(requested);
    if (selectedIndex < 0) {
        m_promptCloseOnStepCombo->addItem(
            tr("%1 (not available in the later flow)").arg(requested), requested);
        selectedIndex = m_promptCloseOnStepCombo->count() - 1;
        m_promptCloseOnStepCombo->setItemData(
            selectedIndex,
            tr("This reference will fail compilation until the target is restored"),
            Qt::ToolTipRole);
    }
    m_promptCloseOnStepCombo->setCurrentIndex(selectedIndex);
}

QString StepPropertyEditor::selectedPromptCloseStep() const
{
    if (!m_promptCloseOnStepCombo) {
        return {};
    }
    const int index = m_promptCloseOnStepCombo->currentIndex();
    const auto data = m_promptCloseOnStepCombo->currentData();
    return index >= 0 &&
           m_promptCloseOnStepCombo->currentText() ==
               m_promptCloseOnStepCombo->itemText(index) &&
           data.isValid()
        ? data.toString().trimmed()
        : m_promptCloseOnStepCombo->currentText().trimmed();
}

void StepPropertyEditor::rebuildPromptImageChoices(const QString& selectedImage)
{
    if (!m_promptImageCombo) {
        return;
    }

    const QSignalBlocker blocker(m_promptImageCombo);
    m_promptImageCombo->clear();
    m_promptImageCombo->addItem(tr("No image"), QString{});

    const auto sequencePath = m_document ? m_document->filePath() : QString{};
    const auto imagesDirectory =
        ProjectResourcePaths::imagesDirectoryForSequence(sequencePath);
    for (const auto& file :
         ProjectResourcePaths::availableImages(sequencePath)) {
        m_promptImageCombo->addItem(file, file);
        m_promptImageCombo->setItemData(
            m_promptImageCombo->count() - 1,
            QDir(imagesDirectory).absoluteFilePath(file),
            Qt::ToolTipRole);
    }

    const auto requested = selectedImage.trimmed();
    int selectedIndex = requested.isEmpty()
        ? 0
        : m_promptImageCombo->findData(requested);
    if (selectedIndex < 0) {
        m_promptImageCombo->addItem(
            tr("%1 (not found)").arg(requested), requested);
        selectedIndex = m_promptImageCombo->count() - 1;
        m_promptImageCombo->setItemData(
            selectedIndex,
            tr("The configured image is not currently available in this project's images folder"),
            Qt::ToolTipRole);
    }
    m_promptImageCombo->setCurrentIndex(selectedIndex);
}

QString StepPropertyEditor::selectedPromptImage() const
{
    return m_promptImageCombo && m_promptImageCombo->currentIndex() >= 0
        ? m_promptImageCombo->currentData().toString().trimmed()
        : QString{};
}

bool StepPropertyEditor::mergePluginInputValues(QJsonObject& inputs,
                                                QString& errorMessage,
                                                QWidget** invalidWidget) const
{
    const auto isExpression = [](const QString& value) {
        const auto trimmed = value.trimmed();
        return trimmed.startsWith(QStringLiteral("${")) &&
               trimmed.endsWith(QLatin1Char('}'));
    };

    for (const auto& item : m_pluginInputEditors) {
        const auto& definition = item.definition;
        if (item.inheritedFromStation) {
            inputs.remove(definition.key);
            continue;
        }
        if (item.fieldWidget &&
            (item.fieldWidget->property("valueToolInactive").toBool() ||
             item.fieldWidget->property("pluginInputInactive").toBool() ||
             item.fieldWidget->property("parserInputInactive").toBool())) {
            inputs.remove(definition.key);
            continue;
        }
        if (definition.type == PluginParameterType::ExpressionList) {
            const auto* table = qobject_cast<const QTableWidget*>(item.widget);
            QJsonArray values;
            if (table) {
                for (int row = 0; row < table->rowCount(); ++row) {
                    const auto* nameEdit = qobject_cast<const QLineEdit*>(
                        table->cellWidget(row, 0));
                    const auto* valueField = table->cellWidget(row, 1);
                    const auto* valueEdit = valueField
                        ? valueField->findChild<QLineEdit*>(
                              QStringLiteral("expressionListValue"))
                        : nullptr;
                    if (!valueEdit || valueEdit->text().trimmed().isEmpty()) {
                        continue;
                    }
                    const auto text = valueEdit->text().trimmed();
                    QJsonValue parsedValue;
                    if (isExpression(text)) {
                        parsedValue = text;
                    } else {
                        bool ok = false;
                        const double number = text.toDouble(&ok);
                        if (!ok || !std::isfinite(number)) {
                            errorMessage = tr("Statistics value %1 must be a number or expression")
                                               .arg(row + 1);
                            if (invalidWidget) {
                                *invalidWidget = const_cast<QLineEdit*>(valueEdit);
                            }
                            return false;
                        }
                        parsedValue = number;
                    }
                    auto name = nameEdit ? nameEdit->text().trimmed() : QString{};
                    if (name.isEmpty()) {
                        name = tr("Value %1").arg(row + 1);
                    }
                    values.push_back(QJsonObject{
                        {QStringLiteral("name"), name},
                        {QStringLiteral("value"), parsedValue}});
                }
            }
            if (definition.required && values.isEmpty()) {
                errorMessage = tr("%1 requires at least one value")
                                   .arg(definition.name);
                if (invalidWidget) *invalidWidget = item.fieldWidget;
                return false;
            }
            if (values.isEmpty()) inputs.remove(definition.key);
            else inputs.insert(definition.key, values);
            continue;
        }
        if (const auto* toggle =
                qobject_cast<const QAbstractButton*>(item.widget)) {
            inputs.insert(definition.key, toggle->isChecked());
            continue;
        }
        if (const auto* combo = qobject_cast<const QComboBox*>(item.widget)) {
            const auto value = combo->currentData();
            if (!value.isValid()) {
                if (definition.required) {
                    errorMessage = tr("%1 is required").arg(definition.name);
                    if (invalidWidget) *invalidWidget = item.widget;
                    return false;
                }
                inputs.remove(definition.key);
            } else {
                inputs.insert(definition.key, QJsonValue::fromVariant(value));
            }
            continue;
        }

        const auto* edit = qobject_cast<const QLineEdit*>(item.widget);
        if (!edit) {
            continue;
        }
        const auto text = edit->text().trimmed();
        if (text.isEmpty()) {
            if (definition.required) {
                errorMessage = tr("%1 is required").arg(definition.name);
                if (invalidWidget) *invalidWidget = item.widget;
                return false;
            }
            if (definition.allowEmpty) {
                inputs.insert(definition.key, QString{});
            } else {
                inputs.remove(definition.key);
            }
            continue;
        }

        if (definition.type == PluginParameterType::Integer) {
            if (isExpression(text)) {
                inputs.insert(definition.key, text);
                continue;
            }
            bool ok = false;
            const auto number = text.toLongLong(&ok);
            if (!ok) {
                errorMessage = tr("%1 must be an integer or expression")
                                   .arg(definition.name);
                if (invalidWidget) *invalidWidget = item.widget;
                return false;
            }
            if ((definition.minimum && number < *definition.minimum) ||
                (definition.maximum && number > *definition.maximum)) {
                errorMessage = tr("%1 is outside the allowed range")
                                   .arg(definition.name);
                if (invalidWidget) *invalidWidget = item.widget;
                return false;
            }
            inputs.insert(definition.key, static_cast<double>(number));
        } else if (definition.type == PluginParameterType::Number) {
            if (isExpression(text)) {
                inputs.insert(definition.key, text);
                continue;
            }
            bool ok = false;
            const double number = text.toDouble(&ok);
            if (!ok || !std::isfinite(number)) {
                errorMessage = tr("%1 must be a number or expression")
                                   .arg(definition.name);
                if (invalidWidget) *invalidWidget = item.widget;
                return false;
            }
            if ((definition.minimum && number < *definition.minimum) ||
                (definition.maximum && number > *definition.maximum)) {
                errorMessage = tr("%1 is outside the allowed range")
                                   .arg(definition.name);
                if (invalidWidget) *invalidWidget = item.widget;
                return false;
            }
            inputs.insert(definition.key, number);
        } else {
            inputs.insert(definition.key, text);
        }
    }

    const auto* function = currentPluginFunction();
    if (!function) {
        return true;
    }
    if (!mergeParserFieldMappings(inputs, errorMessage, invalidWidget)) {
        return false;
    }
    const bool canFunction = std::any_of(
        m_plugins.cbegin(), m_plugins.cend(), [function](const auto& plugin) {
            return plugin.category.compare(QStringLiteral("CAN"), Qt::CaseInsensitive) == 0 &&
                   std::any_of(plugin.functions.cbegin(), plugin.functions.cend(),
                               [function](const auto& candidate) {
                                   return &candidate == function;
                               });
        });
    if (!canFunction) {
        return true;
    }
    const auto hasInputDefinition = [function](const QString& key) {
        return std::any_of(function->inputs.cbegin(), function->inputs.cend(),
                           [&key](const auto& definition) {
                               return definition.key == key;
                           });
    };
    const auto validateCanValue = [&](const QString& key,
                                      quint32 maximum,
                                      const QString& rangeText) {
        if (!hasInputDefinition(key) || !inputs.contains(key)) {
            return true;
        }
        const auto value = inputs.value(key);
        if (value.isString() && isExpression(value.toString())) {
            return true;
        }
        quint32 parsed = 0;
        if (!parseCanIdentifier(value, parsed) || parsed > maximum) {
            const auto displayName = key == QStringLiteral("id")
                ? tr("CAN ID")
                : (key == QStringLiteral("filterId") ? tr("Filter ID")
                                                       : tr("Filter Mask"));
            errorMessage = tr("%1 must be in range %2").arg(displayName, rangeText);
            if (invalidWidget) {
                const auto iterator = std::find_if(
                    m_pluginInputEditors.cbegin(), m_pluginInputEditors.cend(),
                    [&key](const auto& editor) {
                        return editor.definition.key == key;
                    });
                *invalidWidget = iterator == m_pluginInputEditors.cend()
                    ? nullptr : iterator->widget;
            }
            return false;
        }
        return true;
    };

    const bool extended = inputs.value(QStringLiteral("extended")).toBool(false);
    if (!validateCanValue(QStringLiteral("id"),
                          extended ? 0x1FFFFFFF : 0x7FF,
                          extended ? QStringLiteral("0x00000000~0x1FFFFFFF")
                                   : QStringLiteral("0x000~0x7FF (Extended Frame is OFF)")) ||
        !validateCanValue(QStringLiteral("filterId"),
                          extended ? 0x1FFFFFFF : 0x7FF,
                          extended ? QStringLiteral("0x00000000~0x1FFFFFFF")
                                   : QStringLiteral("0x000~0x7FF (Extended Frame is OFF)")) ||
        !validateCanValue(QStringLiteral("filterMask"),
                          extended ? 0x1FFFFFFF : 0x7FF,
                          extended ? QStringLiteral("0x00000000~0x1FFFFFFF")
                                   : QStringLiteral("0x000~0x7FF (Extended Frame is OFF)"))) {
        return false;
    }
    return true;
}

void StepPropertyEditor::flashValidationError(QWidget* widget)
{
    if (!widget) {
        return;
    }
    widget->setFocus(Qt::OtherFocusReason);
    widget->setProperty("validationError", true);
    auto* animation = new QVariantAnimation(widget);
    animation->setStartValue(QColor(QStringLiteral("#fff1f0")));
    animation->setEndValue(QColor(QStringLiteral("#ffffff")));
    animation->setDuration(220);
    animation->setLoopCount(3);
    connect(animation, &QVariantAnimation::valueChanged, widget,
            [widget](const QVariant& value) {
                if (!widget->property("validationError").toBool()) {
                    return;
                }
                widget->setStyleSheet(QStringLiteral(
                    "border: 2px solid #d92d20; border-radius: 3px; "
                    "background: %1;").arg(value.value<QColor>().name()));
            });
    connect(animation, &QVariantAnimation::finished, widget, [widget] {
        if (widget->property("validationError").toBool()) {
            widget->setStyleSheet(QStringLiteral(
                "border: 2px solid #d92d20; border-radius: 3px; "
                "background: #fff1f0;"));
        }
    });
    if (!widget->property("validationClearConnected").toBool()) {
        if (auto* edit = qobject_cast<QLineEdit*>(widget)) {
            connect(edit, &QLineEdit::textChanged, widget, [widget] {
                if (!widget->property("validationError").toBool()) {
                    return;
                }
                widget->setProperty("validationError", false);
                widget->setStyleSheet({});
                for (auto* animation : widget->findChildren<QVariantAnimation*>()) {
                    animation->stop();
                    animation->deleteLater();
                }
            });
            widget->setProperty("validationClearConnected", true);
        }
    }
    animation->start(QAbstractAnimation::DeleteWhenStopped);
}

bool StepPropertyEditor::commitPendingChanges()
{
    if (!hasPendingChanges()) {
        return true;
    }
    if (!m_document || !m_path.isValid() || m_sourceObject.isEmpty()) {
        return false;
    }
    if (m_idEdit->text().trimmed().isEmpty()) {
        showError(tr("ID cannot be empty"));
        return false;
    }

    const auto kind = m_isGroup
        ? QString{}
        : m_kindCombo->currentData().toString();
    const auto rawSourceKind = m_sourceObject.value(QStringLiteral("kind")).toString(
        m_sourceObject.value(QStringLiteral("type")).toString());
    const auto sourceKind = canonicalizeSequenceItemForUi(
        QJsonObject{{QStringLiteral("kind"), rawSourceKind}}, false)
                                .value(QStringLiteral("kind"))
                                .toString(rawSourceKind);
    const auto dataFamily = [](const QString& value) {
        if (value == QStringLiteral("action") || value == QStringLiteral("cleanup")) {
            return QStringLiteral("module");
        }
        if (value == QStringLiteral("limit") ||
            value == QStringLiteral("break")) {
            return QStringLiteral("predicate");
        }
        if (value == QStringLiteral("counter")) return QStringLiteral("counter");
        if (value == QStringLiteral("aggregate")) return QStringLiteral("aggregate");
        if (value == QStringLiteral("statement") ||
            value == QStringLiteral("sequenceCall")) {
            return QStringLiteral("raw");
        }
        return QString{};
    };
    const bool resetDataForKindChange = !m_isGroup && sourceKind != kind &&
        dataFamily(sourceKind) != dataFamily(kind);
    const bool moduleKind = kind == QStringLiteral("action") ||
                            kind == QStringLiteral("cleanup");
    const bool moduleCall = moduleKind &&
        !m_moduleIdEdit->text().trimmed().isEmpty();

    QJsonObject inputs;
    QJsonObject parameters;
    QJsonObject errorPolicy;
    QJsonArray resources;
    QString error;
    if (!m_isGroup && !parseObjectText(m_inputsEdit->toPlainText(), inputs, error)) {
        showError(tr("Inputs: %1").arg(error));
        return false;
    }
    if (resetDataForKindChange) {
        inputs = {};
    }
    if (!m_isGroup && moduleCall &&
        m_moduleIdEdit->text().trimmed() == QStringLiteral("device")) {
        const auto deviceId = m_deviceIdCombo->currentData().toString();
        if (deviceId.isEmpty()) {
            showError(tr("Select a compatible target device"));
            return false;
        }
        inputs.insert(QStringLiteral("deviceId"), deviceId);
    }
    QWidget* invalidPluginInput = nullptr;
    if (!m_isGroup && moduleCall &&
        !mergePluginInputValues(inputs, error, &invalidPluginInput)) {
        flashValidationError(invalidPluginInput);
        showError(error);
        return false;
    }
    if (!m_isGroup && !parseObjectText(m_parametersEdit->toPlainText(), parameters, error)) {
        showError(tr("Parameters: %1").arg(error));
        return false;
    }
    if (resetDataForKindChange) {
        parameters = {};
    }
    if (!m_isGroup &&
        !parseObjectText(m_errorPolicyEdit->toPlainText(), errorPolicy, error)) {
        showError(tr("Advanced error policy: %1").arg(error));
        return false;
    }
    if (!m_isGroup && !parseArrayText(m_resourcesEdit->toPlainText(), resources, error)) {
        showError(tr("Resources: %1").arg(error));
        return false;
    }

    auto updated = m_sourceObject;
    const bool standardGroup = m_isGroup && m_document &&
                               m_document->isStandardGroup(m_path);
    if (!standardGroup) {
        updated.insert("id", m_idEdit->text().trimmed());
    }
    insertOrRemove(updated, "name", m_nameEdit->text());
    if (!standardGroup) {
        updated.insert("kind", m_kindCombo->currentData().toString());
        updated.remove("type");
    }

    if (!m_isGroup) {
        updated.insert("enabled", m_enabledCheck->isChecked());
        insertOrRemove(updated, "key", m_keyEdit->text());
        updated.insert("alwaysRun", m_alwaysRunCheck->isChecked());
        updated.insert("resultRecording", m_resultRecordingCheck->isChecked());
        updated.insert("checkpointBefore", m_checkpointBeforeCheck->isChecked());
        updated.insert("checkpointAfter", m_checkpointAfterCheck->isChecked());
        const auto tags = tagsFromText(m_tagsEdit->text());
        if (tags.isEmpty()) updated.remove("tags"); else updated.insert("tags", tags);
        if (moduleKind) {
            insertOrRemove(updated, "moduleId", m_moduleIdEdit->text());
            if (m_moduleIdEdit->text().trimmed().isEmpty()) {
                updated.remove(QStringLiteral("function"));
            } else {
                insertOrRemove(updated, "function",
                               m_functionEdit->currentData().toString());
            }
        } else {
            updated.remove(QStringLiteral("moduleId"));
            updated.remove(QStringLiteral("function"));
        }
        if (kind == "wait") {
            parameters.remove("ms");
            updated.insert("ms", m_waitMsSpin->value());
        }
        if (kind == "limit" || kind == "break") {
            const auto actual = m_limitActualEdit->text().trimmed();
            if (actual.isEmpty()) {
                showError(tr("Value to test is required"));
                return false;
            }
            inputs.insert("actual", actual);
            const auto mode = m_limitComparisonCombo->currentData().toString();
            const bool betweenTolerance = mode == QStringLiteral("betweenTolerance");
            const bool betweenLimits = mode == QStringLiteral("betweenLimits");
            const bool boolean = mode == QStringLiteral("isTrue") ||
                                 mode == QStringLiteral("isFalse");
            if (!boolean && !betweenLimits &&
                m_limitExpectedEdit->text().trimmed().isEmpty()) {
                showError(tr("Expected / threshold is required for this comparison"));
                return false;
            }
            if (betweenLimits &&
                (m_limitLowerEdit->text().trimmed().isEmpty() ||
                 m_limitUpperEdit->text().trimmed().isEmpty())) {
                showError(tr("Both lower and upper limits are required"));
                return false;
            }
            parameters.insert(QStringLiteral("comparison"),
                              runtimeLimitComparison(mode));
            parameters.remove(QStringLiteral("lowerLimit"));
            parameters.remove(QStringLiteral("upperLimit"));
            parameters.remove(QStringLiteral("decimalPlaces"));
            if (betweenLimits) {
                parameters.remove(QStringLiteral("expected"));
                parameters.remove(QStringLiteral("tolerance"));
                insertOrRemoveScalar(parameters, QStringLiteral("lower"),
                                     m_limitLowerEdit->text());
                insertOrRemoveScalar(parameters, QStringLiteral("upper"),
                                     m_limitUpperEdit->text());
            } else {
                parameters.remove(QStringLiteral("lower"));
                parameters.remove(QStringLiteral("upper"));
                if (boolean) {
                    parameters.remove(QStringLiteral("expected"));
                } else {
                    insertOrRemoveScalar(parameters, QStringLiteral("expected"),
                                         m_limitExpectedEdit->text());
                    if (usesNumericExpectedPrecision(mode)) {
                        bool exceedsLimit = false;
                        const auto decimalPlaces = decimalPlacesFromLiteral(
                            m_limitExpectedEdit->text(), exceedsLimit);
                        if (exceedsLimit) {
                            flashValidationError(m_limitExpectedEdit);
                            showError(tr("Comparison precision supports at most 15 decimal places"));
                            return false;
                        }
                        if (decimalPlaces >= 0) {
                            parameters.insert(
                                QStringLiteral("decimalPlaces"), decimalPlaces);
                        }
                    }
                }
                if (betweenTolerance || mode == QStringLiteral("equal") ||
                    mode == QStringLiteral("notEqual")) {
                    parameters.insert(QStringLiteral("tolerance"),
                                      m_limitToleranceSpin->value());
                } else {
                    parameters.remove(QStringLiteral("tolerance"));
                }
            }
            if (betweenTolerance || betweenLimits) {
                parameters.insert(QStringLiteral("inclusive"),
                                  m_limitInclusiveCheck->isChecked());
            } else {
                parameters.remove(QStringLiteral("inclusive"));
            }
            if (kind == "limit") {
                insertOrRemove(parameters, QStringLiteral("measurementName"),
                               m_limitMeasurementNameEdit->text());
                insertOrRemove(parameters, QStringLiteral("unit"),
                               m_limitUnitEdit->text());
            } else {
                parameters.remove(QStringLiteral("measurementName"));
                parameters.remove(QStringLiteral("unit"));
            }
        }
        if (kind == "counter") {
            insertOrRemoveScalar(inputs, QStringLiteral("condition"),
                                 m_counterConditionEdit->text());
            parameters.insert(QStringLiteral("mode"),
                              m_counterModeCombo->currentData().toString());
            parameters.insert(QStringLiteral("start"), m_counterStartSpin->value());
            parameters.insert(QStringLiteral("increment"),
                              m_counterIncrementSpin->value());
        }
        if (kind == "aggregate") {
            if (m_aggregateValueEdit->text().trimmed().isEmpty()) {
                showError(tr("Aggregate value is required"));
                return false;
            }
            insertOrRemoveScalar(inputs, QStringLiteral("value"),
                                 m_aggregateValueEdit->text());
        }
        if (kind == "operatorPrompt") {
            const auto message = m_promptMessageEdit->toPlainText().trimmed();
            if (message.isEmpty()) {
                showError(tr("Operator prompt message is required"));
                return false;
            }
            const auto mode = m_promptModeCombo->currentData().toString();
            auto prompt = updated.value("prompt").toObject();
            prompt.insert("mode", mode);
            insertOrRemove(prompt, "title", m_promptTitleEdit->text());
            prompt.insert("message", message);
            insertOrRemove(prompt, "image", selectedPromptImage());
            if (mode == QStringLiteral("confirm")) {
                const auto buttonText = m_promptConfirmTextEdit->text().trimmed();
                if (buttonText.isEmpty()) {
                    showError(tr("Confirmation button text is required"));
                    return false;
                }
                prompt.insert("confirmText", buttonText);
                prompt.remove("closeOnStep");
                prompt.remove("dialogKey");
                prompt.remove("passText");
                prompt.remove("failText");
                prompt.remove("failureCode");
                prompt.remove("inputType");
                prompt.remove("inputPlaceholder");
                prompt.remove("defaultValue");
            } else if (mode == QStringLiteral("notice")) {
                prompt.remove("confirmText");
                insertOrRemove(prompt, "closeOnStep", selectedPromptCloseStep());
                insertOrRemove(prompt, "dialogKey", m_promptDialogKeyEdit->text());
                prompt.remove("passText");
                prompt.remove("failText");
                prompt.remove("failureCode");
                prompt.remove("inputType");
                prompt.remove("inputPlaceholder");
                prompt.remove("defaultValue");
            } else if (mode == QStringLiteral("input")) {
                const auto buttonText = m_promptConfirmTextEdit->text().trimmed();
                if (buttonText.isEmpty()) {
                    showError(tr("Submit button text is required"));
                    return false;
                }
                const auto inputType = m_promptInputTypeCombo->currentData().toString();
                const auto defaultValue = m_promptDefaultValueEdit->text().trimmed();
                if (!defaultValue.isEmpty()) {
                    bool valid = true;
                    if (inputType == QStringLiteral("integer")) {
                        defaultValue.toLongLong(&valid, 10);
                    } else if (inputType == QStringLiteral("number")) {
                        defaultValue.toDouble(&valid);
                    }
                    if (!valid) {
                        showError(tr("Default value does not match the selected input type"));
                        return false;
                    }
                }
                prompt.insert("confirmText", buttonText);
                prompt.insert("inputType", inputType);
                insertOrRemove(prompt, "inputPlaceholder",
                               m_promptInputPlaceholderEdit->text());
                if (defaultValue.isEmpty()) {
                    prompt.remove("defaultValue");
                } else if (inputType == QStringLiteral("integer")) {
                    prompt.insert("defaultValue",
                                  static_cast<double>(defaultValue.toLongLong()));
                } else if (inputType == QStringLiteral("number")) {
                    prompt.insert("defaultValue", defaultValue.toDouble());
                } else {
                    prompt.insert("defaultValue", defaultValue);
                }
                prompt.remove("closeOnStep");
                prompt.remove("dialogKey");
                prompt.remove("passText");
                prompt.remove("failText");
                prompt.remove("failureCode");
            } else {
                const auto passText = m_promptPassTextEdit->text().trimmed();
                const auto failText = m_promptFailTextEdit->text().trimmed();
                const auto failureCode = m_promptFailureCodeEdit->text().trimmed();
                if (passText.isEmpty() || failText.isEmpty() || failureCode.isEmpty()) {
                    showError(tr("PASS text, FAIL text, and FAIL error code are required"));
                    return false;
                }
                prompt.remove("confirmText");
                prompt.remove("closeOnStep");
                insertOrRemove(prompt, "dialogKey", m_promptDialogKeyEdit->text());
                prompt.insert("passText", passText);
                prompt.insert("failText", failText);
                prompt.insert("failureCode", failureCode);
                prompt.remove("inputType");
                prompt.remove("inputPlaceholder");
                prompt.remove("defaultValue");
            }
            prompt.insert("timeoutMs", m_promptTimeoutSpin->value());
            updated.insert("prompt", prompt);
        } else {
            updated.remove("prompt");
        }
        if (inputs.isEmpty()) updated.remove("inputs"); else updated.insert("inputs", inputs);
        if (parameters.isEmpty()) updated.remove("parameters"); else updated.insert("parameters", parameters);
        if (resources.isEmpty()) updated.remove("resources"); else updated.insert("resources", resources);
        const auto storeOutcomePolicy = [this, &errorPolicy](
                                            const QString& key,
                                            QComboBox* combo,
                                            const QString& targetKey,
                                            QComboBox* targetCombo) {
            const auto value = combo->currentData().toString();
            if (value == QStringLiteral("Inherit")) {
                errorPolicy.remove(key);
            } else {
                errorPolicy.insert(key, value);
            }
            if (value != QStringLiteral("JumpTo")) {
                errorPolicy.remove(targetKey);
                return true;
            }
            const auto target = targetCombo->currentData().toString().trimmed();
            if (target.isEmpty()) {
                showError(tr("Select a later step for %1").arg(key));
                flashValidationError(targetCombo);
                return false;
            }
            errorPolicy.insert(targetKey, target);
            return true;
        };
        if (!storeOutcomePolicy(QStringLiteral("onFail"),
                                m_onFailPolicyCombo,
                                QStringLiteral("onFailTarget"),
                                m_onFailTargetCombo) ||
            !storeOutcomePolicy(QStringLiteral("onError"),
                                m_onErrorPolicyCombo,
                                QStringLiteral("onErrorTarget"),
                                m_onErrorTargetCombo) ||
            !storeOutcomePolicy(QStringLiteral("onTimeout"),
                                m_onTimeoutPolicyCombo,
                                QStringLiteral("onTimeoutTarget"),
                                m_onTimeoutTargetCombo)) {
            return false;
        }
        if (errorPolicy.isEmpty()) updated.remove("errorPolicy");
        else updated.insert("errorPolicy", errorPolicy);

        auto retry = updated.value("retry").toObject();
        retry.insert("maxAttempts", m_maxAttemptsSpin->value());
        retry.insert("delayMs", m_retryDelaySpin->value());
        insertOrRemove(retry, "retryWhen", m_retryWhenEdit->text());
        updated.insert("retry", retry);

        auto timeout = updated.value("timeout").toObject();
        timeout.insert("timeoutMs", m_timeoutSpin->value());
        updated.insert("timeout", timeout);
        updated.remove("timeoutMs");

        if (kind == QStringLiteral("action") &&
            m_periodicEnabledCheck->isChecked()) {
            QJsonObject periodic;
            periodic.insert(QStringLiteral("intervalMs"),
                            m_periodicIntervalSpin->value());
            periodic.insert(QStringLiteral("runImmediately"),
                            m_periodicRunImmediatelyCheck->isChecked());
            QJsonObject counter;
            counter.insert(QStringLiteral("start"),
                           m_periodicCounterStartSpin->value());
            counter.insert(QStringLiteral("increment"),
                           m_periodicCounterIncrementSpin->value());
            counter.insert(QStringLiteral("wrapAt"),
                           m_periodicCounterWrapAtSpin->value());
            periodic.insert(QStringLiteral("counter"), counter);
            updated.insert(QStringLiteral("periodic"), periodic);
        } else {
            updated.remove(QStringLiteral("periodic"));
        }

        if (kind == "loop") {
            QJsonObject loop;
            const auto loopType = m_loopTypeCombo->currentData().toString();
            loop.insert("type", loopType);
            if (loopType == QStringLiteral("while")) {
                if (m_conditionMaxIterationsSpin->value() <= 0 &&
                    m_conditionTimeoutSpin->value() <= 0) {
                    showError(tr("While Loop requires Maximum iterations or Overall timeout"));
                    return false;
                }
                loop.insert("intervalMs", m_conditionIntervalSpin->value());
                loop.insert("maxIterations", m_conditionMaxIterationsSpin->value());
                loop.insert("timeoutMs", m_conditionTimeoutSpin->value());
                loop.insert("iterationErrorPolicy",
                            m_conditionIterationErrorCombo->currentData().toString());
            } else {
                loop.insert("variable", m_loopVariableEdit->text().trimmed());
                loop.insert("from", m_loopFromSpin->value());
                loop.insert("to", m_loopToSpin->value());
                loop.insert("step", m_loopStepSpin->value());
            }
            updated.insert("loop", loop);
        } else {
            updated.remove("loop");
        }
        if (kind == "barrier") {
            auto barrier = updated.value("barrier").toObject();
            const auto barrierName = m_barrierNameEdit->text().trimmed();
            if (barrierName.isEmpty()) {
                barrier.remove(QStringLiteral("barrierName"));
            } else {
                barrier.insert(QStringLiteral("barrierName"), barrierName);
            }
            if (barrier.isEmpty()) {
                updated.remove(QStringLiteral("barrier"));
            } else {
                updated.insert(QStringLiteral("barrier"), barrier);
            }
        } else {
            updated.remove(QStringLiteral("barrier"));
        }
    }

    updated = canonicalizeSequenceItemForUi(std::move(updated), m_isGroup);

    const auto clearSuccessfulDraft = [this] {
        for (const auto& item : std::as_const(m_pluginInputEditors)) {
            if (!item.widget ||
                !item.widget->property("validationError").toBool()) {
                continue;
            }
            item.widget->setProperty("validationError", false);
            item.widget->setStyleSheet({});
            for (auto* animation :
                 item.widget->findChildren<QVariantAnimation*>()) {
                animation->stop();
                animation->deleteLater();
            }
        }
        m_errorLabel->hide();
        setDraftDirty(false);
    };

    auto targetPath = m_path;
    auto currentObject = m_document->objectAt(targetPath);
    if (currentObject.isEmpty() || currentObject != m_sourceObject) {
        const auto relocatedPath = m_document->findItemPath(
            m_sourceObject, targetPath);
        if (!relocatedPath.isValid()) {
            showError(tr("The selected sequence item no longer exists"));
            return false;
        }
        targetPath = relocatedPath;
        m_path = relocatedPath;
        currentObject = m_document->objectAt(targetPath);
    }

    // Reverting an invalid draft can leave the editor marked dirty even though
    // every field once again matches the document. That is a successful no-op,
    // not a missing sequence item.
    if (updated == currentObject) {
        m_sourceObject = currentObject;
        clearSuccessfulDraft();
        return true;
    }

    const auto previousSourceObject = m_sourceObject;
    // replaceItemObject emits documentChanged synchronously. Publish the new
    // source first so MainWindow does not rebuild this editor while the same
    // draft is being committed.
    m_sourceObject = updated;
    if (!m_document->replaceItemObject(targetPath, updated)) {
        m_sourceObject = previousSourceObject;
        showError(tr("The selected sequence item no longer exists"));
        return false;
    }
    clearSuccessfulDraft();
    emit itemApplied(targetPath);
    return true;
}

void StepPropertyEditor::showError(const QString& message)
{
    m_errorLabel->setText(message);
    m_errorLabel->show();
}

} // namespace PicoATE::Ui
