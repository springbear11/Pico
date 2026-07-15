#include "StepPropertyEditor.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QPlainTextEdit>
#include <QScrollArea>
#include <QSizePolicy>
#include <QSpinBox>
#include <QTabWidget>
#include <QTextCursor>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>

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

QJsonValue jsonScalarFromText(const QString& source)
{
    const auto text = source.trimmed();
    if (text.isEmpty()) {
        return QJsonValue(QJsonValue::Undefined);
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

} // namespace

StepPropertyEditor::StepPropertyEditor(SequenceDocument* document,
                                       QWidget* parent)
    : QWidget(parent)
    , m_document(document)
{
    setObjectName(QStringLiteral("stepPropertyEditor"));
    setMinimumWidth(300);

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
    buildDataPage();
    buildPolicyPage();
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
            [this] { if (!m_loading) updateKindRows(); });
    connect(m_limitComparisonCombo,
            &QComboBox::currentIndexChanged,
            this,
            [this] { if (!m_loading) updateLimitRows(); });
    connect(m_moduleIdEdit, &QLineEdit::editingFinished,
            this, &StepPropertyEditor::rebuildPluginInputEditors);
    connect(m_functionEdit, &QLineEdit::editingFinished,
            this, &StepPropertyEditor::rebuildPluginInputEditors);

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

void StepPropertyEditor::observeDraftWidget(QWidget* widget)
{
    if (!widget || widget->property("picoateDraftObserved").toBool()) {
        return;
    }
    widget->setProperty("picoateDraftObserved", true);
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
    m_editable = editable;
    const bool hasObject = !m_sourceObject.isEmpty();
    const bool canEdit = editable && m_path.isValid() && !m_previewing;
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
    for (auto* check : m_tabs->findChildren<QCheckBox*>()) {
        check->setEnabled(canEdit && !check->property("stationInherited").toBool());
    }
    for (auto* spin : m_tabs->findChildren<QSpinBox*>()) {
        spin->setEnabled(canEdit && !spin->property("stationInherited").toBool());
    }
    for (auto* spin : m_tabs->findChildren<QDoubleSpinBox*>()) {
        spin->setEnabled(canEdit && !spin->property("stationInherited").toBool());
    }
    for (auto* button : m_tabs->findChildren<QToolButton*>()) {
        button->setEnabled(canEdit && !button->property("stationInherited").toBool());
    }
}

void StepPropertyEditor::setPluginRegistry(QVector<PluginManifest> plugins)
{
    m_plugins = std::move(plugins);
    rebuildPluginInputEditors();
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
        widget = segments.size() > 1 && segments[1] == QStringLiteral("actual")
            ? static_cast<QWidget*>(m_limitActualEdit)
            : static_cast<QWidget*>(m_inputsEdit);
        tabIndex = 1;
    }
    else if (field == "parameters") {
        tabIndex = 1;
        if (!m_isGroup && m_kindCombo->currentData().toString() == QStringLiteral("limit")) {
            if (nested == QStringLiteral("comparison")) widget = m_limitComparisonCombo;
            else if (nested == QStringLiteral("expected")) widget = m_limitExpectedEdit;
            else if (nested == QStringLiteral("lower") || nested == QStringLiteral("lowerLimit")) widget = m_limitLowerEdit;
            else if (nested == QStringLiteral("upper") || nested == QStringLiteral("upperLimit")) widget = m_limitUpperEdit;
            else if (nested == QStringLiteral("tolerance")) widget = m_limitToleranceSpin;
            else if (nested == QStringLiteral("inclusive")) widget = m_limitInclusiveCheck;
            else if (nested == QStringLiteral("measurementName")) widget = m_limitMeasurementNameEdit;
            else if (nested == QStringLiteral("unit")) widget = m_limitUnitEdit;
        }
        if (!widget) widget = m_parametersEdit;
    }
    else if (field == "ms") { widget = m_waitMsSpin; tabIndex = 1; }
    else if (field == "loop") {
        tabIndex = 1;
        if (nested == "from") widget = m_loopFromSpin;
        else if (nested == "to") widget = m_loopToSpin;
        else if (nested == "step") widget = m_loopStepSpin;
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
        tabIndex = 2;
        if (nested == "onError") widget = m_onErrorCombo;
        else if (nested == "onTimeout") widget = m_onTimeoutCombo;
        else if (nested == "cleanupRegionId") widget = m_cleanupRegionEdit;
        else if (nested == "stopUutOnFailure") widget = m_stopUutCheck;
        else widget = m_onFailCombo;
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
    auto* page = new QWidget(m_tabs);
    m_generalForm = new QFormLayout(page);
    m_generalForm->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
    m_generalForm->setRowWrapPolicy(QFormLayout::WrapLongRows);
    m_generalForm->setContentsMargins(8, 8, 8, 8);

    m_idEdit = new QLineEdit(page);
    m_idEdit->setObjectName(QStringLiteral("propertyIdEdit"));
    m_generalForm->addRow(tr("ID"), m_idEdit);
    m_keyEdit = new QLineEdit(page);
    m_keyEdit->setObjectName(QStringLiteral("propertyKeyEdit"));
    m_generalForm->addRow(tr("Key"), m_keyEdit);
    m_nameEdit = new QLineEdit(page);
    m_nameEdit->setObjectName(QStringLiteral("propertyNameEdit"));
    m_generalForm->addRow(tr("Name"), m_nameEdit);
    m_kindCombo = new QComboBox(page);
    m_kindCombo->setObjectName(QStringLiteral("propertyKindCombo"));
    m_generalForm->addRow(tr("Kind"), m_kindCombo);
    m_enabledCheck = new QCheckBox(page);
    m_enabledCheck->setObjectName(QStringLiteral("propertyEnabledCheck"));
    m_generalForm->addRow(tr("Enabled"), m_enabledCheck);
    m_alwaysRunCheck = new QCheckBox(page);
    m_generalForm->addRow(tr("Always run"), m_alwaysRunCheck);
    m_resultRecordingCheck = new QCheckBox(page);
    m_generalForm->addRow(tr("Record result"), m_resultRecordingCheck);
    m_checkpointBeforeCheck = new QCheckBox(page);
    m_generalForm->addRow(tr("Checkpoint before"), m_checkpointBeforeCheck);
    m_checkpointAfterCheck = new QCheckBox(page);
    m_generalForm->addRow(tr("Checkpoint after"), m_checkpointAfterCheck);
    m_tagsEdit = new QLineEdit(page);
    m_tagsEdit->setObjectName(QStringLiteral("propertyTagsEdit"));
    m_generalForm->addRow(tr("Tags"), m_tagsEdit);

    m_tabs->addTab(page, tr("General"));
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
    m_dataForm->addRow(tr("Module ID"), m_moduleIdEdit);
    m_functionEdit = new QLineEdit(content);
    m_functionEdit->setObjectName(QStringLiteral("propertyFunctionEdit"));
    m_dataForm->addRow(tr("Function"), m_functionEdit);
    m_pluginInputsGroup = new QGroupBox(tr("Plugin Parameters"), content);
    m_pluginInputsGroup->setObjectName(QStringLiteral("pluginInputsGroup"));
    m_pluginInputsForm = new QFormLayout(m_pluginInputsGroup);
    m_pluginInputsForm->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
    m_pluginInputsForm->setRowWrapPolicy(QFormLayout::WrapLongRows);
    m_pluginInputsGroup->hide();
    m_dataForm->addRow(m_pluginInputsGroup);
    m_inputsEdit = new QPlainTextEdit(content);
    m_inputsEdit->setObjectName(QStringLiteral("propertyInputsEdit"));
    m_inputsEdit->setMinimumHeight(100);
    m_dataForm->addRow(tr("Advanced inputs (JSON)"), m_inputsEdit);
    m_limitActualEdit = new QLineEdit(content);
    m_limitActualEdit->setObjectName(QStringLiteral("propertyLimitActualEdit"));
    m_limitActualEdit->setPlaceholderText(tr("Value or ${step:...outputs...}"));
    m_limitActualField = wrapExpressionEditor(m_limitActualEdit);
    if (auto* picker = m_limitActualField->findChild<QToolButton*>(
            QStringLiteral("expressionPickerButton"))) {
        m_limitExpressionMenu = picker->menu();
    }
    m_dataForm->addRow(tr("Actual value"), m_limitActualField);
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
    m_dataForm->addRow(tr("Comparison"), m_limitComparisonCombo);
    m_limitExpectedEdit = new QLineEdit(content);
    m_limitExpectedEdit->setObjectName(QStringLiteral("propertyLimitExpectedEdit"));
    m_limitExpectedEdit->setPlaceholderText(tr("Expected value or threshold"));
    m_dataForm->addRow(tr("Expected / threshold"), m_limitExpectedEdit);
    m_limitLowerEdit = new QLineEdit(content);
    m_limitLowerEdit->setObjectName(QStringLiteral("propertyLimitLowerEdit"));
    m_dataForm->addRow(tr("Lower limit"), m_limitLowerEdit);
    m_limitUpperEdit = new QLineEdit(content);
    m_limitUpperEdit->setObjectName(QStringLiteral("propertyLimitUpperEdit"));
    m_dataForm->addRow(tr("Upper limit"), m_limitUpperEdit);
    m_limitToleranceSpin = new QDoubleSpinBox(content);
    m_limitToleranceSpin->setObjectName(QStringLiteral("propertyLimitToleranceSpin"));
    m_limitToleranceSpin->setRange(0.0, std::numeric_limits<double>::max());
    m_limitToleranceSpin->setDecimals(9);
    m_limitToleranceSpin->setSingleStep(0.1);
    m_dataForm->addRow(tr("Tolerance (+/-)"), m_limitToleranceSpin);
    m_limitInclusiveCheck = new QCheckBox(content);
    m_limitInclusiveCheck->setObjectName(QStringLiteral("propertyLimitInclusiveCheck"));
    m_dataForm->addRow(tr("Include boundaries"), m_limitInclusiveCheck);
    m_limitMeasurementNameEdit = new QLineEdit(content);
    m_limitMeasurementNameEdit->setObjectName(QStringLiteral("propertyLimitMeasurementNameEdit"));
    m_dataForm->addRow(tr("Measurement name"), m_limitMeasurementNameEdit);
    m_limitUnitEdit = new QLineEdit(content);
    m_limitUnitEdit->setObjectName(QStringLiteral("propertyLimitUnitEdit"));
    m_dataForm->addRow(tr("Unit"), m_limitUnitEdit);
    m_parametersEdit = new QPlainTextEdit(content);
    m_parametersEdit->setObjectName(QStringLiteral("propertyParametersEdit"));
    m_parametersEdit->setMinimumHeight(100);
    m_dataForm->addRow(tr("Parameters (JSON)"), m_parametersEdit);

    m_waitMsSpin = new QSpinBox(content);
    m_waitMsSpin->setRange(0, std::numeric_limits<int>::max());
    m_waitMsSpin->setSuffix(tr(" ms"));
    m_dataForm->addRow(tr("Duration"), m_waitMsSpin);

    m_loopVariableEdit = new QLineEdit(content);
    m_dataForm->addRow(tr("Loop variable"), m_loopVariableEdit);
    m_loopFromSpin = new QSpinBox(content);
    m_loopFromSpin->setRange(std::numeric_limits<int>::min(), std::numeric_limits<int>::max());
    m_dataForm->addRow(tr("From"), m_loopFromSpin);
    m_loopToSpin = new QSpinBox(content);
    m_loopToSpin->setRange(std::numeric_limits<int>::min(), std::numeric_limits<int>::max());
    m_dataForm->addRow(tr("To"), m_loopToSpin);
    m_loopStepSpin = new QSpinBox(content);
    m_loopStepSpin->setRange(std::numeric_limits<int>::min(), std::numeric_limits<int>::max());
    m_dataForm->addRow(tr("Step"), m_loopStepSpin);

    m_barrierNameEdit = new QLineEdit(content);
    m_dataForm->addRow(tr("Barrier name"), m_barrierNameEdit);
    m_cohortIdEdit = new QLineEdit(content);
    m_dataForm->addRow(tr("Cohort ID"), m_cohortIdEdit);
    m_expectedUutSpin = new QSpinBox(content);
    m_expectedUutSpin->setRange(-1, 100000);
    m_dataForm->addRow(tr("Expected UUTs"), m_expectedUutSpin);
    m_quorumCountSpin = new QSpinBox(content);
    m_quorumCountSpin->setRange(-1, 100000);
    m_dataForm->addRow(tr("Quorum count"), m_quorumCountSpin);
    m_quorumRatioSpin = new QDoubleSpinBox(content);
    m_quorumRatioSpin->setRange(0.0, 1.0);
    m_quorumRatioSpin->setDecimals(3);
    m_quorumRatioSpin->setSingleStep(0.05);
    m_dataForm->addRow(tr("Quorum ratio"), m_quorumRatioSpin);
    m_arrivalTimeoutSpin = new QSpinBox(content);
    m_arrivalTimeoutSpin->setRange(0, std::numeric_limits<int>::max());
    m_arrivalTimeoutSpin->setSuffix(tr(" ms"));
    m_dataForm->addRow(tr("Arrival timeout"), m_arrivalTimeoutSpin);
    m_releaseTimeoutSpin = new QSpinBox(content);
    m_releaseTimeoutSpin->setRange(0, std::numeric_limits<int>::max());
    m_releaseTimeoutSpin->setSuffix(tr(" ms"));
    m_dataForm->addRow(tr("Release timeout"), m_releaseTimeoutSpin);
    m_arrivalPolicyCombo = new QComboBox(content);
    addItems(m_arrivalPolicyCombo, {"WaitAll", "DropFailed", "CountFailed", "Quorum", "BestEffort", "ManualDecision"});
    m_dataForm->addRow(tr("Arrival policy"), m_arrivalPolicyCombo);
    m_releasePolicyCombo = new QComboBox(content);
    addItems(m_releasePolicyCombo, {"Lockstep", "Latch", "Cohort", "RollingWindow"});
    m_dataForm->addRow(tr("Release policy"), m_releasePolicyCombo);
    m_failurePolicyCombo = new QComboBox(content);
    addItems(m_failurePolicyCombo, {"FailBarrier", "RemoveFailedMember", "HoldFailedMember", "ContinueWithWarning", "AbortCohort"});
    m_dataForm->addRow(tr("Failure policy"), m_failurePolicyCombo);
    m_barrierTimeoutPolicyCombo = new QComboBox(content);
    addItems(m_barrierTimeoutPolicyCombo, {"FailArrivedAndWaiting", "ReleaseArrived", "ReleaseIfQuorumReached", "AbortCohort", "RequestOperatorDecision"});
    m_dataForm->addRow(tr("Timeout policy"), m_barrierTimeoutPolicyCombo);
    m_releaseResourcesCheck = new QCheckBox(content);
    m_dataForm->addRow(tr("Release resources"), m_releaseResourcesCheck);

    auto* scroll = new QScrollArea(m_tabs);
    scroll->setWidgetResizable(true);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setWidget(content);
    m_tabs->addTab(scroll, tr("Data"));
}

void StepPropertyEditor::buildPolicyPage()
{
    auto* content = new QWidget;
    m_policyForm = new QFormLayout(content);
    m_policyForm->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
    m_policyForm->setRowWrapPolicy(QFormLayout::WrapLongRows);
    m_policyForm->setContentsMargins(8, 8, 8, 8);

    m_maxAttemptsSpin = new QSpinBox(content);
    m_maxAttemptsSpin->setRange(1, 100000);
    m_policyForm->addRow(tr("Max attempts"), m_maxAttemptsSpin);
    m_retryDelaySpin = new QSpinBox(content);
    m_retryDelaySpin->setRange(0, std::numeric_limits<int>::max());
    m_retryDelaySpin->setSuffix(tr(" ms"));
    m_policyForm->addRow(tr("Retry delay"), m_retryDelaySpin);
    m_retryWhenEdit = new QLineEdit(content);
    m_policyForm->addRow(tr("Retry when"), m_retryWhenEdit);
    m_timeoutSpin = new QSpinBox(content);
    m_timeoutSpin->setRange(0, std::numeric_limits<int>::max());
    m_timeoutSpin->setSuffix(tr(" ms"));
    m_policyForm->addRow(tr("Timeout"), m_timeoutSpin);

    m_onFailCombo = new QComboBox(content);
    m_onErrorCombo = new QComboBox(content);
    m_onTimeoutCombo = new QComboBox(content);
    for (auto* combo : {m_onFailCombo, m_onErrorCombo, m_onTimeoutCombo}) {
        addItems(combo, {"Continue", "StopUut", "Retry", "RunCleanup", "Abort"});
    }
    m_policyForm->addRow(tr("On fail"), m_onFailCombo);
    m_policyForm->addRow(tr("On error"), m_onErrorCombo);
    m_policyForm->addRow(tr("On timeout"), m_onTimeoutCombo);
    m_cleanupRegionEdit = new QLineEdit(content);
    m_policyForm->addRow(tr("Cleanup region"), m_cleanupRegionEdit);
    m_stopUutCheck = new QCheckBox(content);
    m_policyForm->addRow(tr("Stop UUT on failure"), m_stopUutCheck);
    m_resourcesEdit = new QPlainTextEdit(content);
    m_resourcesEdit->setObjectName(QStringLiteral("propertyResourcesEdit"));
    m_resourcesEdit->setMinimumHeight(140);
    m_policyForm->addRow(tr("Resources (JSON)"), m_resourcesEdit);

    auto* scroll = new QScrollArea(m_tabs);
    scroll->setWidgetResizable(true);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setWidget(content);
    m_tabs->addTab(scroll, tr("Policies"));
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
        addItems(m_kindCombo, {"noop", "wait", "action", "limit", "barrier", "cleanup", "loop", "testItem", "statement", "sequenceCall"});
    }
    setComboValue(m_kindCombo,
                  m_sourceObject.value("kind").toString(
                      m_sourceObject.value("type").toString(m_isGroup ? "custom" : "noop")));
    m_enabledCheck->setChecked(m_sourceObject.value("enabled").toBool(true));
    m_alwaysRunCheck->setChecked(m_sourceObject.value("alwaysRun").toBool(false));
    m_resultRecordingCheck->setChecked(m_sourceObject.value("resultRecording").toBool(true));
    m_checkpointBeforeCheck->setChecked(m_sourceObject.value("checkpointBefore").toBool(false));
    m_checkpointAfterCheck->setChecked(m_sourceObject.value("checkpointAfter").toBool(false));
    m_tagsEdit->setText(tagsText(m_sourceObject.value("tags").toArray()));

    m_moduleIdEdit->setText(m_sourceObject.value("moduleId").toString());
    m_functionEdit->setText(m_sourceObject.value("function").toString());
    m_inputsEdit->setPlainText(objectText(m_sourceObject.value("inputs").toObject()));
    m_limitActualEdit->setText(
        m_sourceObject.value("inputs").toObject().value("actual").toVariant().toString());
    const auto parameters = m_sourceObject.value("parameters").toObject();
    m_parametersEdit->setPlainText(objectText(parameters));
    setComboValue(m_limitComparisonCombo, limitEditorMode(parameters));
    m_limitExpectedEdit->setText(jsonValueText(parameters.value(QStringLiteral("expected"))));
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
    m_waitMsSpin->setValue(m_sourceObject.value("ms").toInt(
        m_sourceObject.value("parameters").toObject().value("ms").toInt(0)));

    const auto loop = m_sourceObject.value("loop").toObject();
    m_loopVariableEdit->setText(loop.value("variable").toString("i"));
    m_loopFromSpin->setValue(loop.value("from").toInt(0));
    m_loopToSpin->setValue(loop.value("to").toInt(0));
    m_loopStepSpin->setValue(loop.value("step").toInt(1));

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
    setComboValue(m_arrivalPolicyCombo, barrier.value("arrivalPolicy").toString("WaitAll"));
    setComboValue(m_releasePolicyCombo, barrier.value("releasePolicy").toString("Lockstep"));
    setComboValue(m_failurePolicyCombo, barrier.value("failurePolicy").toString("FailBarrier"));
    setComboValue(m_barrierTimeoutPolicyCombo, barrier.value("timeoutPolicy").toString("FailArrivedAndWaiting"));
    m_releaseResourcesCheck->setChecked(barrier.value("releaseHeldResourcesOnWait").toBool(true));

    const auto retry = m_sourceObject.value("retry").toObject();
    m_maxAttemptsSpin->setValue(retry.value("maxAttempts").toInt(1));
    m_retryDelaySpin->setValue(retry.value("delayMs").toInt(0));
    m_retryWhenEdit->setText(retry.value("retryWhen").toString());
    const auto timeout = m_sourceObject.value("timeout").toObject();
    m_timeoutSpin->setValue(timeout.value("timeoutMs").toInt(
        m_sourceObject.value("timeoutMs").toInt(0)));
    const auto errorPolicy = m_sourceObject.value("errorPolicy").toObject();
    setComboValue(m_onFailCombo, errorPolicy.value("onFail").toString("StopUut"));
    setComboValue(m_onErrorCombo, errorPolicy.value("onError").toString("StopUut"));
    setComboValue(m_onTimeoutCombo, errorPolicy.value("onTimeout").toString("StopUut"));
    m_cleanupRegionEdit->setText(errorPolicy.value("cleanupRegionId").toString());
    m_stopUutCheck->setChecked(errorPolicy.value("stopUutOnFailure").toBool(true));
    m_resourcesEdit->setPlainText(arrayText(m_sourceObject.value("resources").toArray()));

    rebuildPluginInputEditors();
    rebuildExpressionMenu(m_limitExpressionMenu, m_limitActualEdit);

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
    setEditable(m_editable);
}

void StepPropertyEditor::updateKindRows()
{
    if (m_isGroup) {
        return;
    }
    const auto kind = m_kindCombo->currentData().toString();
    const bool action = kind == "action";
    const bool inputData = action || kind == "statement" || kind == "sequenceCall";
    const bool parameters = kind != "barrier" && kind != "loop" &&
                            kind != "testItem";
    const bool loop = kind == "loop";
    const bool barrier = kind == "barrier";
    const bool limit = kind == "limit";

    setFormRowVisible(m_dataForm, m_moduleIdEdit, action);
    setFormRowVisible(m_dataForm, m_functionEdit, action);
    m_pluginInputsGroup->setVisible(action && !m_pluginInputEditors.isEmpty());
    setFormRowVisible(m_dataForm, m_inputsEdit, inputData);
    setFormRowVisible(m_dataForm, m_limitActualField, limit);
    setFormRowVisible(m_dataForm, m_limitComparisonCombo, limit);
    setFormRowVisible(m_dataForm, m_limitMeasurementNameEdit, limit);
    setFormRowVisible(m_dataForm, m_limitUnitEdit, limit);
    setFormRowVisible(m_dataForm, m_parametersEdit, parameters && !limit);
    setFormRowVisible(m_dataForm, m_waitMsSpin, kind == "wait");
    const std::array<QWidget*, 4> loopFields = {
        m_loopVariableEdit, m_loopFromSpin, m_loopToSpin, m_loopStepSpin};
    for (auto* field : loopFields) {
        setFormRowVisible(m_dataForm, field, loop);
    }
    const std::array<QWidget*, 12> barrierFields = {
        m_barrierNameEdit, m_cohortIdEdit, m_expectedUutSpin,
        m_quorumCountSpin, m_quorumRatioSpin, m_arrivalTimeoutSpin,
        m_releaseTimeoutSpin, m_arrivalPolicyCombo, m_releasePolicyCombo,
        m_failurePolicyCombo, m_barrierTimeoutPolicyCombo,
        m_releaseResourcesCheck};
    for (auto* field : barrierFields) {
        setFormRowVisible(m_dataForm, field, barrier);
    }
    updateLimitRows();
}

void StepPropertyEditor::updateLimitRows()
{
    const bool limit = !m_isGroup &&
        m_kindCombo->currentData().toString() == QStringLiteral("limit");
    const auto mode = m_limitComparisonCombo->currentData().toString();
    const bool betweenTolerance = mode == QStringLiteral("betweenTolerance");
    const bool betweenLimits = mode == QStringLiteral("betweenLimits");
    const bool equality = mode == QStringLiteral("equal") ||
                          mode == QStringLiteral("notEqual");
    const bool boolean = mode == QStringLiteral("isTrue") ||
                         mode == QStringLiteral("isFalse");
    if (auto* label = qobject_cast<QLabel*>(
            m_dataForm->labelForField(m_limitExpectedEdit))) {
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
    setFormRowVisible(m_dataForm, m_limitExpectedEdit,
                      limit && !betweenLimits && !boolean);
    setFormRowVisible(m_dataForm, m_limitLowerEdit, limit && betweenLimits);
    setFormRowVisible(m_dataForm, m_limitUpperEdit, limit && betweenLimits);
    setFormRowVisible(m_dataForm, m_limitToleranceSpin,
                      limit && (betweenTolerance || equality));
    setFormRowVisible(m_dataForm, m_limitInclusiveCheck,
                      limit && (betweenTolerance || betweenLimits));
}

const PluginFunctionDefinition* StepPropertyEditor::currentPluginFunction() const
{
    auto moduleId = m_moduleIdEdit->text().trimmed();
    if (moduleId == QStringLiteral("device")) {
        QJsonObject inputs;
        QString ignoredError;
        if (parseObjectText(m_inputsEdit->toPlainText(), inputs, ignoredError)) {
            moduleId = m_pluginByDeviceId.value(
                inputs.value(QStringLiteral("deviceId")).toString());
        }
    }
    const auto functionId = m_functionEdit->text().trimmed();
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
    rebuildPluginInputEditors();
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

    const auto* function = currentPluginFunction();
    const bool action = !m_isGroup &&
        m_kindCombo->currentData().toString() == QStringLiteral("action");
    if (!function || function->inputs.isEmpty() || !action) {
        m_pluginInputsGroup->hide();
        return;
    }

    QJsonObject currentInputs;
    QString ignoredError;
    parseObjectText(m_inputsEdit->toPlainText(), currentInputs, ignoredError);
    const auto moduleId = m_moduleIdEdit->text().trimmed();
    const auto functionId = m_functionEdit->text().trimmed();
    const bool logicalDeviceConnection = moduleId == QStringLiteral("device") &&
        (functionId.compare(QStringLiteral("open"), Qt::CaseInsensitive) == 0 ||
         functionId.compare(QStringLiteral("connect"), Qt::CaseInsensitive) == 0 ||
         functionId.compare(QStringLiteral("connectCan"), Qt::CaseInsensitive) == 0);
    const auto deviceId = currentInputs.value(QStringLiteral("deviceId")).toString();
    const auto stationInputs = m_deviceConfigurations.value(deviceId);
    m_pluginInputsGroup->setTitle(logicalDeviceConnection
        ? tr("Connection Parameters - Station Config")
        : tr("Plugin Parameters - %1").arg(function->name));

    for (const auto& definition : function->inputs) {
        QVariant value;
        const auto current = currentInputs.value(definition.key);
        const bool inheritedFromStation = logicalDeviceConnection && current.isUndefined();
        if (!current.isUndefined()) {
            value = current.toVariant();
        } else if (inheritedFromStation && stationInputs.contains(definition.key)) {
            value = stationInputs.value(definition.key).toVariant();
        } else if (definition.defaultValue.isValid()) {
            value = definition.defaultValue;
        }

        QWidget* editor = nullptr;
        if (definition.type == PluginParameterType::Boolean) {
            auto* check = new QCheckBox(m_pluginInputsGroup);
            check->setTristate(!definition.required && !definition.defaultValue.isValid());
            if (!value.isValid() && check->isTristate()) {
                check->setCheckState(Qt::PartiallyChecked);
            } else {
                check->setChecked(value.toBool());
            }
            editor = check;
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
        } else {
            auto* edit = new QLineEdit(m_pluginInputsGroup);
            if (value.isValid()) {
                edit->setText(value.toString());
            }
            if (definition.type == PluginParameterType::HexBytes) {
                edit->setPlaceholderText(tr("Example: 01 02 03 04"));
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
        auto label = definition.required
            ? tr("%1 *").arg(definition.name)
            : definition.name;
        if (inheritedFromStation) {
            label += tr(" (Station)");
        } else if (logicalDeviceConnection) {
            label += tr(" (Step override)");
        }
        auto* fieldWidget = editor;
        if (auto* lineEdit = qobject_cast<QLineEdit*>(editor)) {
            fieldWidget = wrapExpressionEditor(lineEdit);
            if (inheritedFromStation) {
                for (auto* button : fieldWidget->findChildren<QToolButton*>()) {
                    button->setProperty("stationInherited", true);
                    button->setEnabled(false);
                }
            }
        }
        m_pluginInputsForm->addRow(label, fieldWidget);
        m_pluginInputEditors.push_back({definition, editor, inheritedFromStation});
        observeDraftWidget(editor);
    }
    m_pluginInputsGroup->show();
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
    button->setToolTip(tr("Insert an output from a previous step"));
    button->setPopupMode(QToolButton::InstantPopup);
    button->setFixedSize(28, 28);
    auto* menu = new QMenu(button);
    rebuildExpressionMenu(menu, editor);
    connect(menu, &QMenu::aboutToShow, this,
            [this, menu, editor] { rebuildExpressionMenu(menu, editor); });
    button->setMenu(menu);
    layout->addWidget(button);
    return container;
}

void StepPropertyEditor::rebuildExpressionMenu(QMenu* menu, QLineEdit* editor)
{
    if (!menu || !editor) {
        return;
    }
    menu->clear();
    const auto candidates = m_document
        ? buildStepOutputExpressionCandidates(
              m_document->rootObject(), m_path, m_plugins, m_pluginByDeviceId)
        : QVector<StepOutputExpressionCandidate>{};
    QHash<QString, QMenu*> sourceMenus;
    for (const auto& candidate : candidates) {
        auto* sourceMenu = sourceMenus.value(candidate.stepPath, nullptr);
        if (!sourceMenu) {
            sourceMenu = menu->addMenu(
                QStringLiteral("%1 - %2").arg(candidate.stepPath, candidate.stepName));
            sourceMenus.insert(candidate.stepPath, sourceMenu);
        }
        auto* action = sourceMenu->addAction(
            QStringLiteral("%1 [%2]")
                .arg(candidate.outputName, candidate.outputKey));
        auto details = pluginParameterTypeName(candidate.type);
        if (!candidate.unit.isEmpty()) {
            details += QStringLiteral(" / %1").arg(candidate.unit);
        }
        action->setToolTip(details);
        connect(action, &QAction::triggered, editor,
                [editor, expression = candidate.expression] {
                    editor->setText(expression);
                    editor->setFocus();
                    editor->selectAll();
                });
    }
    if (auto* button = qobject_cast<QToolButton*>(menu->parentWidget())) {
        button->setEnabled(m_editable && !candidates.isEmpty());
    }
}

bool StepPropertyEditor::mergePluginInputValues(QJsonObject& inputs,
                                                QString& errorMessage) const
{
    const auto isExpression = [](const QString& value) {
        const auto trimmed = value.trimmed();
        return trimmed.startsWith(QStringLiteral("${")) &&
               trimmed.endsWith(QLatin1Char('}'));
    };

    for (const auto& item : m_pluginInputEditors) {
        const auto& definition = item.definition;
        if (item.inheritedFromStation) {
            continue;
        }
        if (const auto* check = qobject_cast<const QCheckBox*>(item.widget)) {
            if (check->isTristate() &&
                check->checkState() == Qt::PartiallyChecked) {
                if (definition.required) {
                    errorMessage = tr("%1 is required").arg(definition.name);
                    return false;
                }
                inputs.remove(definition.key);
            } else {
                inputs.insert(definition.key, check->isChecked());
            }
            continue;
        }
        if (const auto* combo = qobject_cast<const QComboBox*>(item.widget)) {
            const auto value = combo->currentData();
            if (!value.isValid()) {
                if (definition.required) {
                    errorMessage = tr("%1 is required").arg(definition.name);
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
                return false;
            }
            inputs.remove(definition.key);
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
                return false;
            }
            if ((definition.minimum && number < *definition.minimum) ||
                (definition.maximum && number > *definition.maximum)) {
                errorMessage = tr("%1 is outside the allowed range")
                                   .arg(definition.name);
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
                return false;
            }
            if ((definition.minimum && number < *definition.minimum) ||
                (definition.maximum && number > *definition.maximum)) {
                errorMessage = tr("%1 is outside the allowed range")
                                   .arg(definition.name);
                return false;
            }
            inputs.insert(definition.key, number);
        } else {
            inputs.insert(definition.key, text);
        }
    }
    return true;
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

    QJsonObject inputs;
    QJsonObject parameters;
    QJsonArray resources;
    QString error;
    if (!m_isGroup && !parseObjectText(m_inputsEdit->toPlainText(), inputs, error)) {
        showError(tr("Inputs: %1").arg(error));
        return false;
    }
    if (!m_isGroup && !mergePluginInputValues(inputs, error)) {
        showError(error);
        return false;
    }
    if (!m_isGroup && !parseObjectText(m_parametersEdit->toPlainText(), parameters, error)) {
        showError(tr("Parameters: %1").arg(error));
        return false;
    }
    if (!m_isGroup && !parseArrayText(m_resourcesEdit->toPlainText(), resources, error)) {
        showError(tr("Resources: %1").arg(error));
        return false;
    }

    auto updated = m_sourceObject;
    updated.insert("id", m_idEdit->text().trimmed());
    insertOrRemove(updated, "name", m_nameEdit->text());
    updated.insert("kind", m_kindCombo->currentData().toString());
    updated.remove("type");

    if (!m_isGroup) {
        updated.insert("enabled", m_enabledCheck->isChecked());
        insertOrRemove(updated, "key", m_keyEdit->text());
        updated.insert("alwaysRun", m_alwaysRunCheck->isChecked());
        updated.insert("resultRecording", m_resultRecordingCheck->isChecked());
        updated.insert("checkpointBefore", m_checkpointBeforeCheck->isChecked());
        updated.insert("checkpointAfter", m_checkpointAfterCheck->isChecked());
        const auto tags = tagsFromText(m_tagsEdit->text());
        if (tags.isEmpty()) updated.remove("tags"); else updated.insert("tags", tags);
        insertOrRemove(updated, "moduleId", m_moduleIdEdit->text());
        insertOrRemove(updated, "function", m_functionEdit->text());
        const auto kind = m_kindCombo->currentData().toString();
        if (kind == "wait") {
            parameters.remove("ms");
            updated.insert("ms", m_waitMsSpin->value());
        }
        if (kind == "limit") {
            const auto actual = m_limitActualEdit->text().trimmed();
            if (actual.isEmpty()) {
                inputs.remove("actual");
            } else {
                inputs.insert("actual", actual);
            }
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
            insertOrRemove(parameters, QStringLiteral("measurementName"),
                           m_limitMeasurementNameEdit->text());
            insertOrRemove(parameters, QStringLiteral("unit"),
                           m_limitUnitEdit->text());
        }
        if (inputs.isEmpty()) updated.remove("inputs"); else updated.insert("inputs", inputs);
        if (parameters.isEmpty()) updated.remove("parameters"); else updated.insert("parameters", parameters);
        if (resources.isEmpty()) updated.remove("resources"); else updated.insert("resources", resources);

        auto retry = updated.value("retry").toObject();
        retry.insert("maxAttempts", m_maxAttemptsSpin->value());
        retry.insert("delayMs", m_retryDelaySpin->value());
        insertOrRemove(retry, "retryWhen", m_retryWhenEdit->text());
        updated.insert("retry", retry);

        auto timeout = updated.value("timeout").toObject();
        timeout.insert("timeoutMs", m_timeoutSpin->value());
        updated.insert("timeout", timeout);
        updated.remove("timeoutMs");

        auto policy = updated.value("errorPolicy").toObject();
        policy.insert("onFail", m_onFailCombo->currentData().toString());
        policy.insert("onError", m_onErrorCombo->currentData().toString());
        policy.insert("onTimeout", m_onTimeoutCombo->currentData().toString());
        insertOrRemove(policy, "cleanupRegionId", m_cleanupRegionEdit->text());
        policy.insert("stopUutOnFailure", m_stopUutCheck->isChecked());
        updated.insert("errorPolicy", policy);

        if (kind == "loop") {
            auto loop = updated.value("loop").toObject();
            loop.insert("type", "for");
            loop.insert("variable", m_loopVariableEdit->text().trimmed());
            loop.insert("from", m_loopFromSpin->value());
            loop.insert("to", m_loopToSpin->value());
            loop.insert("step", m_loopStepSpin->value());
            updated.insert("loop", loop);
        }
        if (kind == "barrier") {
            auto barrier = updated.value("barrier").toObject();
            barrier.insert("barrierName", m_barrierNameEdit->text().trimmed());
            barrier.insert("cohortId", m_cohortIdEdit->text().trimmed());
            barrier.insert("expectedUutCount", m_expectedUutSpin->value());
            barrier.insert("quorumCount", m_quorumCountSpin->value());
            barrier.insert("quorumRatio", m_quorumRatioSpin->value());
            barrier.insert("arrivalTimeoutMs", m_arrivalTimeoutSpin->value());
            barrier.insert("releaseTimeoutMs", m_releaseTimeoutSpin->value());
            barrier.insert("arrivalPolicy", m_arrivalPolicyCombo->currentData().toString());
            barrier.insert("releasePolicy", m_releasePolicyCombo->currentData().toString());
            barrier.insert("failurePolicy", m_failurePolicyCombo->currentData().toString());
            barrier.insert("timeoutPolicy", m_barrierTimeoutPolicyCombo->currentData().toString());
            barrier.insert("releaseHeldResourcesOnWait", m_releaseResourcesCheck->isChecked());
            updated.insert("barrier", barrier);
        }
    }

    if (!m_document->replaceItemObject(m_path, std::move(updated))) {
        showError(tr("The selected sequence item no longer exists"));
        return false;
    }
    m_errorLabel->hide();
    setDraftDirty(false);
    emit itemApplied(m_path);
    return true;
}

void StepPropertyEditor::showError(const QString& message)
{
    m_errorLabel->setText(message);
    m_errorLabel->show();
}

} // namespace PicoATE::Ui
