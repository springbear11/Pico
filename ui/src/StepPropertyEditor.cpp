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
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QSizePolicy>
#include <QSpinBox>
#include <QTabWidget>
#include <QTextCursor>
#include <QTimer>
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

    auto* title = new QLabel(tr("Properties"), this);
    auto titleFont = title->font();
    titleFont.setBold(true);
    title->setFont(titleFont);
    root->addWidget(title);

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

    auto* commands = new QHBoxLayout;
    commands->addStretch(1);
    m_applyButton = new QPushButton(tr("Apply"), this);
    m_applyButton->setObjectName(QStringLiteral("applyPropertiesButton"));
    m_applyButton->setDefault(false);
    commands->addWidget(m_applyButton);
    root->addLayout(commands);

    connect(m_kindCombo,
            &QComboBox::currentIndexChanged,
            this,
            [this] { if (!m_loading) updateKindRows(); });
    connect(m_applyButton, &QPushButton::clicked,
            this, &StepPropertyEditor::applyChanges);
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
    }

    setCurrentItem({});
}

SequenceItemPath StepPropertyEditor::currentPath() const
{
    return m_path;
}

void StepPropertyEditor::setCurrentItem(const SequenceItemPath& path)
{
    m_path = path;
    loadCurrentObject();
}

void StepPropertyEditor::setEditable(bool editable)
{
    m_editable = editable;
    m_tabs->setEnabled(editable && m_path.isValid());
    m_applyButton->setEnabled(editable && m_path.isValid());
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
    else if (field == "inputs") { widget = m_inputsEdit; tabIndex = 1; }
    else if (field == "parameters") { widget = m_parametersEdit; tabIndex = 1; }
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
    m_sourceObject = m_document && m_path.isValid()
        ? m_document->objectAt(m_path)
        : QJsonObject{};
    const bool valid = !m_sourceObject.isEmpty();
    m_isGroup = valid && m_path.isGroup();
    m_emptyLabel->setVisible(!valid);
    m_tabs->setVisible(valid);
    m_applyButton->setVisible(valid);
    if (!valid) {
        m_loading = false;
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
    m_parametersEdit->setPlainText(objectText(m_sourceObject.value("parameters").toObject()));
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

    setFormRowVisible(m_generalForm, m_keyEdit, !m_isGroup);
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
    setEditable(m_editable);
}

void StepPropertyEditor::updateKindRows()
{
    if (m_isGroup) {
        return;
    }
    const auto kind = m_kindCombo->currentData().toString();
    const bool action = kind == "action";
    const bool inputData = action || kind == "limit" ||
                           kind == "statement" || kind == "sequenceCall";
    const bool parameters = kind != "barrier" && kind != "loop" &&
                            kind != "testItem";
    const bool loop = kind == "loop";
    const bool barrier = kind == "barrier";

    setFormRowVisible(m_dataForm, m_moduleIdEdit, action);
    setFormRowVisible(m_dataForm, m_functionEdit, action);
    m_pluginInputsGroup->setVisible(action && !m_pluginInputEditors.isEmpty());
    setFormRowVisible(m_dataForm, m_inputsEdit, inputData);
    setFormRowVisible(m_dataForm, m_parametersEdit, parameters);
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
    m_pluginInputsGroup->setTitle(tr("Plugin Parameters - %1").arg(function->name));

    for (const auto& definition : function->inputs) {
        QVariant value;
        const auto current = currentInputs.value(definition.key);
        if (!current.isUndefined()) {
            value = current.toVariant();
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

        auto objectName = definition.key;
        objectName.replace(QLatin1Char('.'), QLatin1Char('_'));
        editor->setObjectName(QStringLiteral("pluginInput_%1").arg(objectName));
        editor->setEnabled(m_editable);
        if (!definition.unit.isEmpty()) {
            editor->setToolTip(tr("Unit: %1").arg(definition.unit));
        }
        const auto label = definition.required
            ? tr("%1 *").arg(definition.name)
            : definition.name;
        m_pluginInputsForm->addRow(label, editor);
        m_pluginInputEditors.push_back({definition, editor});
    }
    m_pluginInputsGroup->show();
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

void StepPropertyEditor::applyChanges()
{
    if (!m_document || !m_path.isValid() || m_sourceObject.isEmpty()) {
        return;
    }
    if (m_idEdit->text().trimmed().isEmpty()) {
        showError(tr("ID cannot be empty"));
        return;
    }

    QJsonObject inputs;
    QJsonObject parameters;
    QJsonArray resources;
    QString error;
    if (!m_isGroup && !parseObjectText(m_inputsEdit->toPlainText(), inputs, error)) {
        showError(tr("Inputs: %1").arg(error));
        return;
    }
    if (!m_isGroup && !mergePluginInputValues(inputs, error)) {
        showError(error);
        return;
    }
    if (!m_isGroup && !parseObjectText(m_parametersEdit->toPlainText(), parameters, error)) {
        showError(tr("Parameters: %1").arg(error));
        return;
    }
    if (!m_isGroup && !parseArrayText(m_resourcesEdit->toPlainText(), resources, error)) {
        showError(tr("Resources: %1").arg(error));
        return;
    }

    auto updated = m_sourceObject;
    updated.insert("id", m_idEdit->text().trimmed());
    insertOrRemove(updated, "name", m_nameEdit->text());
    updated.insert("kind", m_kindCombo->currentData().toString());
    updated.remove("type");
    updated.insert("enabled", m_enabledCheck->isChecked());

    if (!m_isGroup) {
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
        return;
    }
    m_errorLabel->hide();
    emit itemApplied(m_path);
}

void StepPropertyEditor::showError(const QString& message)
{
    m_errorLabel->setText(message);
    m_errorLabel->show();
}

} // namespace PicoATE::Ui
