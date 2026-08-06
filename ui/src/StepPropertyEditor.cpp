#include "StepPropertyEditor.h"

#include "OnOffControl.h"

#include <QAbstractButton>
#include <QAbstractItemView>
#include <QCheckBox>
#include <QColor>
#include <QComboBox>
#include <QCoreApplication>
#include <QDir>
#include <QDoubleSpinBox>
#include <QFileInfo>
#include <QFormLayout>
#include <QGroupBox>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QPlainTextEdit>
#include <QScrollArea>
#include <QSet>
#include <QSignalBlocker>
#include <QSizePolicy>
#include <QSpinBox>
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
            [this] {
                if (m_loading) {
                    return;
                }
                updateKindRows();
                if (m_kindCombo->currentData().toString() ==
                    QStringLiteral("operatorPrompt")) {
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
    rebuildDeviceChoices();
    rebuildPluginInputEditors();
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
        m_advancedJsonToggle->setChecked(true);
        widget = m_errorPolicyEdit;
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
    m_resultRecordingCheck->setObjectName(
        QStringLiteral("propertyResultRecordingCheck"));
    m_resultRecordingCheck->setToolTip(tr(
        "Include this item in CSV and XLSX reports. TXT logs and the overall pass/fail result are unaffected."));
    m_generalForm->addRow(tr("Record in CSV / XLSX"), m_resultRecordingCheck);
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
    m_functionEdit = new QComboBox(content);
    m_functionEdit->setObjectName(QStringLiteral("propertyFunctionEdit"));
    m_functionEdit->setEditable(false);
    m_dataForm->addRow(tr("Function"), m_functionEdit);
    m_deviceIdCombo = new QComboBox(content);
    m_deviceIdCombo->setObjectName(QStringLiteral("propertyDeviceIdCombo"));
    m_deviceIdCombo->setToolTip(
        tr("Station devices that support the selected plugin function"));
    m_dataForm->addRow(tr("Target device"), m_deviceIdCombo);
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
    m_limitExpectedField = wrapExpressionEditor(m_limitExpectedEdit);
    m_limitExpectedField->setObjectName(QStringLiteral("propertyLimitExpectedField"));
    m_dataForm->addRow(tr("Expected / threshold"), m_limitExpectedField);
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

    m_counterConditionEdit = new QLineEdit(content);
    m_counterConditionEdit->setObjectName(QStringLiteral("propertyCounterConditionEdit"));
    m_counterConditionEdit->setPlaceholderText(tr("Every execution (default)"));
    m_counterConditionEdit->setToolTip(tr(
        "Leave empty to count every execution, or choose a boolean Step output with fx."));
    m_counterConditionField = wrapExpressionEditor(m_counterConditionEdit);
    if (auto* picker = m_counterConditionField->findChild<QToolButton*>(
            QStringLiteral("expressionPickerButton"))) {
        m_counterConditionMenu = picker->menu();
    }
    m_dataForm->addRow(tr("Count trigger"), m_counterConditionField);
    m_counterModeCombo = new QComboBox(content);
    m_counterModeCombo->setObjectName(QStringLiteral("propertyCounterModeCombo"));
    m_counterModeCombo->addItem(tr("Consecutive (reset when false)"),
                                QStringLiteral("consecutive"));
    m_counterModeCombo->addItem(tr("Total (keep value when false)"),
                                QStringLiteral("total"));
    m_dataForm->addRow(tr("Counter mode"), m_counterModeCombo);
    m_counterStartSpin = new QDoubleSpinBox(content);
    m_counterStartSpin->setRange(-1.0e12, 1.0e12);
    m_counterStartSpin->setDecimals(6);
    m_dataForm->addRow(tr("Start value"), m_counterStartSpin);
    m_counterIncrementSpin = new QDoubleSpinBox(content);
    m_counterIncrementSpin->setRange(-1.0e12, 1.0e12);
    m_counterIncrementSpin->setDecimals(6);
    m_counterIncrementSpin->setValue(1.0);
    m_dataForm->addRow(tr("Increment"), m_counterIncrementSpin);

    m_aggregateValueEdit = new QLineEdit(content);
    m_aggregateValueEdit->setObjectName(QStringLiteral("propertyAggregateValueEdit"));
    m_aggregateValueEdit->setPlaceholderText(tr("Numeric value or ${step:...outputs...}"));
    m_aggregateValueField = wrapExpressionEditor(m_aggregateValueEdit);
    if (auto* picker = m_aggregateValueField->findChild<QToolButton*>(
            QStringLiteral("expressionPickerButton"))) {
        m_aggregateValueMenu = picker->menu();
    }
    m_dataForm->addRow(tr("Value to aggregate"), m_aggregateValueField);
    m_waitMsSpin = new QSpinBox(content);
    m_waitMsSpin->setRange(0, std::numeric_limits<int>::max());
    m_waitMsSpin->setSuffix(tr(" ms"));
    m_dataForm->addRow(tr("Duration"), m_waitMsSpin);

    m_promptModeCombo = new QComboBox(content);
    m_promptModeCombo->setObjectName(QStringLiteral("propertyPromptModeCombo"));
    m_promptModeCombo->addItem(tr("Manual confirmation (default)"),
                               QStringLiteral("confirm"));
    m_promptModeCombo->addItem(tr("Conditional confirmation (Step PASS)"),
                               QStringLiteral("notice"));
    m_promptModeCombo->addItem(tr("Operator PASS / FAIL judgment"),
                               QStringLiteral("judgment"));
    m_dataForm->addRow(tr("Mode"), m_promptModeCombo);
    m_promptTitleEdit = new QLineEdit(content);
    m_promptTitleEdit->setObjectName(QStringLiteral("propertyPromptTitleEdit"));
    m_dataForm->addRow(tr("Window title"), m_promptTitleEdit);
    m_promptMessageEdit = new QPlainTextEdit(content);
    m_promptMessageEdit->setObjectName(QStringLiteral("propertyPromptMessageEdit"));
    m_promptMessageEdit->setMinimumHeight(90);
    m_promptMessageEdit->setPlaceholderText(
        tr("Tell the operator what to do. Runtime values can be inserted with fx."));
    m_promptMessageField = wrapPromptMessageEditor(m_promptMessageEdit);
    m_dataForm->addRow(tr("Message"), m_promptMessageField);
    m_promptImageCombo = new QComboBox(content);
    m_promptImageCombo->setObjectName(QStringLiteral("propertyPromptImageCombo"));
    m_promptImageCombo->setInsertPolicy(QComboBox::NoInsert);
    m_promptImageCombo->setToolTip(
        tr("Optional PNG/JPG image from the image folder beside PicoATE.UI.exe"));
    m_dataForm->addRow(tr("Image (optional)"), m_promptImageCombo);
    m_promptConfirmTextEdit = new QLineEdit(content);
    m_promptConfirmTextEdit->setObjectName(QStringLiteral("propertyPromptConfirmTextEdit"));
    m_dataForm->addRow(tr("Button text"), m_promptConfirmTextEdit);
    m_promptCloseOnStepCombo = new QComboBox(content);
    m_promptCloseOnStepCombo->setObjectName(QStringLiteral("propertyPromptCloseOnStepCombo"));
    m_promptCloseOnStepCombo->setEditable(true);
    m_promptCloseOnStepCombo->setInsertPolicy(QComboBox::NoInsert);
    m_promptCloseOnStepCombo->setToolTip(
        tr("Select a later enabled step. The prompt closes after that step finishes."));
    m_dataForm->addRow(tr("Close after step"), m_promptCloseOnStepCombo);
    m_promptDialogKeyEdit = new QLineEdit(content);
    m_promptDialogKeyEdit->setObjectName(QStringLiteral("propertyPromptDialogKeyEdit"));
    m_promptDialogKeyEdit->setPlaceholderText(tr("Example: rgb-lamp-check"));
    m_promptDialogKeyEdit->setToolTip(
        tr("Use the same key on an observation prompt and a later judgment to reuse one window."));
    m_dataForm->addRow(tr("Dialog key (optional)"), m_promptDialogKeyEdit);
    m_promptPassTextEdit = new QLineEdit(content);
    m_promptPassTextEdit->setObjectName(QStringLiteral("propertyPromptPassTextEdit"));
    m_dataForm->addRow(tr("PASS button text"), m_promptPassTextEdit);
    m_promptFailTextEdit = new QLineEdit(content);
    m_promptFailTextEdit->setObjectName(QStringLiteral("propertyPromptFailTextEdit"));
    m_dataForm->addRow(tr("FAIL button text"), m_promptFailTextEdit);
    m_promptFailureCodeEdit = new QLineEdit(content);
    m_promptFailureCodeEdit->setObjectName(QStringLiteral("propertyPromptFailureCodeEdit"));
    m_promptFailureCodeEdit->setPlaceholderText(QStringLiteral("OperatorCheckFailed"));
    m_dataForm->addRow(tr("FAIL error code"), m_promptFailureCodeEdit);
    m_promptTimeoutSpin = new QSpinBox(content);
    m_promptTimeoutSpin->setObjectName(QStringLiteral("propertyPromptTimeoutSpin"));
    m_promptTimeoutSpin->setRange(0, std::numeric_limits<int>::max());
    m_promptTimeoutSpin->setSuffix(tr(" ms"));
    m_promptTimeoutSpin->setToolTip(tr("0 means no timeout for confirmation prompts"));
    m_dataForm->addRow(tr("Response timeout"), m_promptTimeoutSpin);

    m_loopTypeCombo = new QComboBox(content);
    m_loopTypeCombo->setObjectName(QStringLiteral("propertyLoopTypeCombo"));
    m_loopTypeCombo->addItem(tr("For Loop"), QStringLiteral("for"));
    m_loopTypeCombo->addItem(tr("While Loop"), QStringLiteral("while"));
    m_dataForm->addRow(tr("Loop type"), m_loopTypeCombo);
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

    m_conditionIntervalSpin = new QSpinBox(content);
    m_conditionIntervalSpin->setObjectName(
        QStringLiteral("propertyConditionIntervalSpin"));
    m_conditionIntervalSpin->setRange(0, std::numeric_limits<int>::max());
    m_conditionIntervalSpin->setSuffix(tr(" ms"));
    m_dataForm->addRow(tr("Delay between iterations"), m_conditionIntervalSpin);
    m_conditionMaxIterationsSpin = new QSpinBox(content);
    m_conditionMaxIterationsSpin->setObjectName(
        QStringLiteral("propertyConditionMaxIterationsSpin"));
    m_conditionMaxIterationsSpin->setRange(0, std::numeric_limits<int>::max());
    m_conditionMaxIterationsSpin->setSpecialValueText(tr("Disabled"));
    m_dataForm->addRow(tr("Maximum iterations"), m_conditionMaxIterationsSpin);
    m_conditionTimeoutSpin = new QSpinBox(content);
    m_conditionTimeoutSpin->setObjectName(
        QStringLiteral("propertyConditionTimeoutSpin"));
    m_conditionTimeoutSpin->setRange(0, std::numeric_limits<int>::max());
    m_conditionTimeoutSpin->setSuffix(tr(" ms"));
    m_conditionTimeoutSpin->setSpecialValueText(tr("Disabled"));
    m_dataForm->addRow(tr("Overall timeout"), m_conditionTimeoutSpin);
    m_conditionIterationErrorCombo = new QComboBox(content);
    m_conditionIterationErrorCombo->setObjectName(
        QStringLiteral("propertyConditionIterationErrorCombo"));
    m_conditionIterationErrorCombo->addItem(tr("Abort loop"),
                                            QStringLiteral("abortLoop"));
    m_conditionIterationErrorCombo->addItem(tr("Continue next iteration"),
                                            QStringLiteral("continueLoop"));
    m_dataForm->addRow(tr("Iteration Error / Timeout"),
                       m_conditionIterationErrorCombo);

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
        tr("Legacy per-Step policy. Station Failure Handling overrides it at runtime."));
    m_advancedJsonForm->addRow(tr("Legacy error policy"), m_errorPolicyEdit);
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

    m_periodicEnabledCheck = new QCheckBox(tr("Run this Action in the background"), content);
    m_periodicEnabledCheck->setObjectName(QStringLiteral("propertyPeriodicEnabledCheck"));
    m_periodicEnabledCheck->setToolTip(
        tr("Register this top-level Setup Action as a cooperative periodic task."));
    m_policyForm->addRow(tr("Periodic task"), m_periodicEnabledCheck);
    m_periodicIntervalSpin = new QSpinBox(content);
    m_periodicIntervalSpin->setObjectName(QStringLiteral("propertyPeriodicIntervalSpin"));
    m_periodicIntervalSpin->setRange(1, std::numeric_limits<int>::max());
    m_periodicIntervalSpin->setSuffix(tr(" ms"));
    m_policyForm->addRow(tr("Interval"), m_periodicIntervalSpin);
    m_periodicRunImmediatelyCheck = new QCheckBox(tr("Run once immediately after registration"), content);
    m_periodicRunImmediatelyCheck->setObjectName(
        QStringLiteral("propertyPeriodicRunImmediatelyCheck"));
    m_policyForm->addRow(tr("First run"), m_periodicRunImmediatelyCheck);
    m_periodicCounterStartSpin = new QSpinBox(content);
    m_periodicCounterStartSpin->setObjectName(
        QStringLiteral("propertyPeriodicCounterStartSpin"));
    m_periodicCounterStartSpin->setRange(0, std::numeric_limits<int>::max());
    m_periodicCounterStartSpin->setValue(1);
    m_policyForm->addRow(tr("Counter start"), m_periodicCounterStartSpin);
    m_periodicCounterIncrementSpin = new QSpinBox(content);
    m_periodicCounterIncrementSpin->setObjectName(
        QStringLiteral("propertyPeriodicCounterIncrementSpin"));
    m_periodicCounterIncrementSpin->setRange(1, std::numeric_limits<int>::max());
    m_periodicCounterIncrementSpin->setValue(1);
    m_policyForm->addRow(tr("Counter increment"), m_periodicCounterIncrementSpin);
    m_periodicCounterWrapAtSpin = new QSpinBox(content);
    m_periodicCounterWrapAtSpin->setObjectName(
        QStringLiteral("propertyPeriodicCounterWrapAtSpin"));
    m_periodicCounterWrapAtSpin->setRange(0, std::numeric_limits<int>::max());
    m_periodicCounterWrapAtSpin->setSpecialValueText(tr("No wrap"));
    m_policyForm->addRow(tr("Counter wrap at"), m_periodicCounterWrapAtSpin);

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
        addItems(m_kindCombo, {"noop", "wait", "action", "limit", "break", "counter", "aggregate", "operatorPrompt", "barrier", "cleanup", "loop", "testItem", "statement", "sequenceCall"});
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
    m_inputsEdit->setPlainText(objectText(m_sourceObject.value("inputs").toObject()));
    rebuildFunctionChoices(m_sourceObject.value("function").toString());
    m_errorPolicyEdit->setPlainText(
        objectText(m_sourceObject.value("errorPolicy").toObject()));
    m_advancedJsonToggle->setChecked(false);
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
    m_promptTitleEdit->setText(
        prompt.value("title").toString(tr("Message")));
    m_promptMessageEdit->setPlainText(prompt.value("message").toString());
    const auto editorKind = m_kindCombo->currentData().toString();
    if (editorKind == QStringLiteral("operatorPrompt")) {
        rebuildPromptImageChoices(prompt.value("image").toString());
    }
    m_promptConfirmTextEdit->setText(
        prompt.value("confirmText").toString(QStringLiteral("OK")));
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
                  loop.value("iterationErrorPolicy").toString("abortLoop"));

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
    setFormRowVisible(m_dataForm, m_promptCloseOnStepCombo, noticePrompt);
    setFormRowVisible(m_dataForm, m_promptDialogKeyEdit,
                      noticePrompt || judgmentPrompt);
    setFormRowVisible(m_dataForm, m_promptPassTextEdit, judgmentPrompt);
    setFormRowVisible(m_dataForm, m_promptFailTextEdit, judgmentPrompt);
    setFormRowVisible(m_dataForm, m_promptFailureCodeEdit, judgmentPrompt);
    setFormRowVisible(m_dataForm, m_promptTimeoutSpin, operatorPrompt);
    setFormRowVisible(m_dataForm, m_loopTypeCombo, loop);
    const std::array<QWidget*, 12> barrierFields = {
        m_barrierNameEdit, m_cohortIdEdit, m_expectedUutSpin,
        m_quorumCountSpin, m_quorumRatioSpin, m_arrivalTimeoutSpin,
        m_releaseTimeoutSpin, m_arrivalPolicyCombo, m_releasePolicyCombo,
        m_failurePolicyCombo, m_barrierTimeoutPolicyCombo,
        m_releaseResourcesCheck};
    for (auto* field : barrierFields) {
        setFormRowVisible(m_dataForm, field, barrier);
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
        (kind == QStringLiteral("limit") || kind == QStringLiteral("break"));
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
    if (kind == QStringLiteral("limit") || kind == QStringLiteral("break")) {
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
    if (kind == QStringLiteral("limit") || kind == QStringLiteral("break")) {
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
            controlsLayout->setSpacing(4);
            controlsLayout->addStretch(1);
            auto* add = new QToolButton(controls);
            add->setText(QStringLiteral("+"));
            add->setToolTip(tr("Add value"));
            add->setFixedSize(28, 28);
            auto* remove = new QToolButton(controls);
            remove->setText(QStringLiteral("-"));
            remove->setToolTip(tr("Remove selected value"));
            remove->setFixedSize(28, 28);
            controlsLayout->addWidget(add);
            controlsLayout->addWidget(remove);
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
        m_pluginInputsForm->addRow(label, fieldWidget);
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
    nameEdit->setText(name);
    auto* valueEdit = new QLineEdit(table);
    valueEdit->setObjectName(QStringLiteral("expressionListValue"));
    valueEdit->setPlaceholderText(tr("Number or ${step:...outputs...}"));
    if (value.isValid()) {
        valueEdit->setText(value.toString());
    }
    table->setCellWidget(row, 0, nameEdit);
    table->setCellWidget(row, 1, wrapExpressionEditor(valueEdit));
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
        action->setData(candidate.expression);
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
    button->setPopupMode(QToolButton::InstantPopup);
    button->setFixedSize(28, 28);
    auto* menu = new QMenu(button);
    rebuildPromptExpressionMenu(menu, editor);
    connect(menu, &QMenu::aboutToShow, this,
            [this, menu, editor] {
                rebuildPromptExpressionMenu(menu, editor);
            });
    button->setMenu(menu);
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
    if (!candidates.isEmpty()) {
        auto* outputsMenu = menu->addMenu(tr("Previous Step Outputs"));
        QHash<QString, QMenu*> sourceMenus;
        for (const auto& candidate : candidates) {
            auto* sourceMenu = sourceMenus.value(candidate.stepPath, nullptr);
            if (!sourceMenu) {
                sourceMenu = outputsMenu->addMenu(
                    QStringLiteral("%1 - %2")
                        .arg(candidate.stepPath, candidate.stepName));
                sourceMenus.insert(candidate.stepPath, sourceMenu);
            }
            auto tooltip = pluginParameterTypeName(candidate.type);
            if (!candidate.unit.isEmpty()) {
                tooltip += QStringLiteral(" / %1").arg(candidate.unit);
            }
            addExpression(sourceMenu,
                          QStringLiteral("%1 [%2]")
                              .arg(candidate.outputName, candidate.outputKey),
                          candidate.expression,
                          tooltip);
        }
    }

    if (auto* button = qobject_cast<QToolButton*>(menu->parentWidget())) {
        button->setEnabled(m_editable && !m_previewing);
    }
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

    QStringList roots = {
        QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("image")),
        QDir(QDir::currentPath()).filePath(QStringLiteral("image")),
    };
    QSet<QString> seenRoots;
    QSet<QString> seenFiles;
    for (const auto& root : roots) {
        const auto absoluteRoot = QFileInfo(root).absoluteFilePath();
        const auto rootKey = QDir::cleanPath(absoluteRoot).toLower();
        if (seenRoots.contains(rootKey)) {
            continue;
        }
        seenRoots.insert(rootKey);

        const QDir directory(absoluteRoot);
        const auto files = directory.entryList(
            {QStringLiteral("*.png"), QStringLiteral("*.jpg"),
             QStringLiteral("*.jpeg")},
            QDir::Files | QDir::Readable,
            QDir::Name | QDir::IgnoreCase);
        for (const auto& file : files) {
            const auto fileKey = file.toLower();
            if (seenFiles.contains(fileKey)) {
                continue;
            }
            seenFiles.insert(fileKey);
            m_promptImageCombo->addItem(file, file);
            m_promptImageCombo->setItemData(
                m_promptImageCombo->count() - 1,
                directory.absoluteFilePath(file),
                Qt::ToolTipRole);
        }
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
            tr("The configured image is not currently available in the image folder"),
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

    QJsonObject inputs;
    QJsonObject parameters;
    QJsonObject errorPolicy;
    QJsonArray resources;
    QString error;
    if (!m_isGroup && !parseObjectText(m_inputsEdit->toPlainText(), inputs, error)) {
        showError(tr("Inputs: %1").arg(error));
        return false;
    }
    if (!m_isGroup &&
        m_moduleIdEdit->text().trimmed() == QStringLiteral("device")) {
        const auto deviceId = m_deviceIdCombo->currentData().toString();
        if (deviceId.isEmpty()) {
            showError(tr("Select a compatible target device"));
            return false;
        }
        inputs.insert(QStringLiteral("deviceId"), deviceId);
    }
    QWidget* invalidPluginInput = nullptr;
    if (!m_isGroup &&
        !mergePluginInputValues(inputs, error, &invalidPluginInput)) {
        flashValidationError(invalidPluginInput);
        showError(error);
        return false;
    }
    if (!m_isGroup && !parseObjectText(m_parametersEdit->toPlainText(), parameters, error)) {
        showError(tr("Parameters: %1").arg(error));
        return false;
    }
    if (!m_isGroup &&
        !parseObjectText(m_errorPolicyEdit->toPlainText(), errorPolicy, error)) {
        showError(tr("Legacy error policy: %1").arg(error));
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
        insertOrRemove(updated, "moduleId", m_moduleIdEdit->text());
        insertOrRemove(updated, "function",
                       m_functionEdit->currentData().toString());
        const auto kind = m_kindCombo->currentData().toString();
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
            } else if (mode == QStringLiteral("notice")) {
                prompt.remove("confirmText");
                insertOrRemove(prompt, "closeOnStep", selectedPromptCloseStep());
                insertOrRemove(prompt, "dialogKey", m_promptDialogKeyEdit->text());
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
            }
            prompt.insert("timeoutMs", m_promptTimeoutSpin->value());
            updated.insert("prompt", prompt);
        } else {
            updated.remove("prompt");
        }
        if (inputs.isEmpty()) updated.remove("inputs"); else updated.insert("inputs", inputs);
        if (parameters.isEmpty()) updated.remove("parameters"); else updated.insert("parameters", parameters);
        if (resources.isEmpty()) updated.remove("resources"); else updated.insert("resources", resources);
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
