#include "StationPropertyEditor.h"

#include "OnOffControl.h"
#include "StationDocument.h"

#include <QCheckBox>
#include <QComboBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QSpinBox>
#include <QTabWidget>
#include <QVBoxLayout>

#include <array>
#include <algorithm>
#include <utility>

namespace PicoATE::Ui {

namespace {

QString objectText(const QJsonObject& object)
{
    return QString::fromUtf8(QJsonDocument(object).toJson(QJsonDocument::Indented));
}

bool parseObject(const QString& text, QJsonObject& object, QString& error)
{
    if (text.trimmed().isEmpty()) {
        object = {};
        return true;
    }
    QJsonParseError parseError;
    const auto document = QJsonDocument::fromJson(text.toUtf8(), &parseError);
    if (parseError.error != QJsonParseError::NoError) {
        error = parseError.errorString();
        return false;
    }
    if (!document.isObject()) {
        error = QObject::tr("Expected a JSON object");
        return false;
    }
    object = document.object();
    return true;
}

QString valueWithAlias(const QJsonObject& object,
                       const QString& key,
                       const QString& alias = {})
{
    const auto value = object.value(key).toString();
    return value.isEmpty() && !alias.isEmpty()
        ? object.value(alias).toString()
        : value;
}

QWidget* scrollPage(QWidget* content, QWidget* parent)
{
    auto* scroll = new QScrollArea(parent);
    scroll->setWidgetResizable(true);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setWidget(content);
    return scroll;
}

} // namespace

StationPropertyEditor::StationPropertyEditor(StationDocument* document,
                                             QWidget* parent)
    : QWidget(parent)
    , m_document(document)
{
    Q_ASSERT(m_document);
    setObjectName(QStringLiteral("stationPropertyEditor"));
    setMinimumWidth(240);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(8, 0, 0, 0);
    layout->setSpacing(6);
    m_title = new QLabel(tr("Properties"), this);
    auto font = m_title->font();
    font.setBold(true);
    m_title->setFont(font);
    layout->addWidget(m_title);
    m_tabs = new QTabWidget(this);
    buildStationPage();
    buildDevicePage();
    layout->addWidget(m_tabs, 1);

    for (auto* child : findChildren<QWidget*>()) {
        if (qobject_cast<QLineEdit*>(child) ||
            qobject_cast<QPlainTextEdit*>(child) ||
            qobject_cast<QComboBox*>(child)) {
            auto policy = child->sizePolicy();
            policy.setHorizontalPolicy(QSizePolicy::Ignored);
            child->setSizePolicy(policy);
        }
    }

    const auto markPending = [this] { markPendingChanges(); };
    for (auto* edit : {m_deviceIdEdit, m_deviceTypeEdit, m_driverIdEdit,
                       m_addressEdit}) {
        connect(edit, &QLineEdit::textEdited, this, markPending);
    }
    connect(m_pluginCombo, &QComboBox::currentIndexChanged, this, markPending);
    connect(m_timeoutSpin, &QSpinBox::valueChanged, this, markPending);
    connect(m_lifetimeCombo, &QComboBox::currentIndexChanged, this, markPending);
    connect(m_enabledCheck, &QAbstractButton::toggled, this, markPending);
    connect(m_optionsEdit, &QPlainTextEdit::textChanged, this, markPending);

    connect(m_document, &StationDocument::documentChanged,
            this, &StationPropertyEditor::reload);
    reload();
}

void StationPropertyEditor::buildStationPage()
{
    auto* content = new QWidget;
    auto* layout = new QVBoxLayout(content);
    auto* form = new QFormLayout;
    form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
    form->setRowWrapPolicy(QFormLayout::WrapLongRows);
    m_stationIdEdit = new QLineEdit(content);
    m_stationIdEdit->setObjectName(QStringLiteral("stationIdEdit"));
    form->addRow(tr("Station ID"), m_stationIdEdit);
    m_stationNameEdit = new QLineEdit(content);
    m_stationNameEdit->setObjectName(QStringLiteral("stationNameEdit"));
    form->addRow(tr("Name"), m_stationNameEdit);
    m_scanDialogEnabledCheck = new QCheckBox(content);
    m_scanDialogEnabledCheck->setObjectName(QStringLiteral("scanDialogEnabledCheck"));
    m_scanDialogEnabledCheck->setToolTip(
        tr("Show the mandatory SN scan dialog in TEST mode"));
    form->addRow(tr("Enable Scan Dialog"), m_scanDialogEnabledCheck);
    m_metadataEdit = new QPlainTextEdit(content);
    m_metadataEdit->setObjectName(QStringLiteral("stationMetadataEdit"));
    m_metadataEdit->setFixedHeight(150);
    form->addRow(tr("Metadata (JSON)"), m_metadataEdit);
    layout->addLayout(form);
    m_stationError = new QLabel(content);
    m_stationError->setWordWrap(true);
    m_stationError->setStyleSheet(QStringLiteral("color: #d9534f;"));
    m_stationError->hide();
    layout->addWidget(m_stationError);
    layout->addStretch(1);
    m_tabs->addTab(scrollPage(content, m_tabs), tr("Station"));
}

void StationPropertyEditor::buildDevicePage()
{
    auto* content = new QWidget;
    auto* layout = new QVBoxLayout(content);
    auto* form = new QFormLayout;
    form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
    form->setRowWrapPolicy(QFormLayout::WrapLongRows);
    m_deviceIdEdit = new QLineEdit(content);
    m_deviceIdEdit->setObjectName(QStringLiteral("deviceIdEdit"));
    form->addRow(tr("Logical ID"), m_deviceIdEdit);
    m_deviceTypeEdit = new QLineEdit(content);
    m_deviceTypeEdit->setObjectName(QStringLiteral("deviceTypeEdit"));
    form->addRow(tr("Type"), m_deviceTypeEdit);
    m_driverIdEdit = new QLineEdit(content);
    m_driverIdEdit->setObjectName(QStringLiteral("deviceDriverIdEdit"));
    form->addRow(tr("Driver"), m_driverIdEdit);
    m_pluginCombo = new QComboBox(content);
    m_pluginCombo->setObjectName(QStringLiteral("devicePluginCombo"));
    form->addRow(tr("Plugin / Model"), m_pluginCombo);
    m_addressEdit = new QLineEdit(content);
    m_addressEdit->setObjectName(QStringLiteral("deviceAddressEdit"));
    form->addRow(tr("Address"), m_addressEdit);
    m_timeoutSpin = new QSpinBox(content);
    m_timeoutSpin->setObjectName(QStringLiteral("deviceTimeoutMsSpin"));
    m_timeoutSpin->setRange(1, 600000);
    m_timeoutSpin->setSuffix(tr(" ms"));
    form->addRow(tr("Timeout"), m_timeoutSpin);
    m_lifetimeCombo = new QComboBox(content);
    for (const auto* lifetime : {"Step", "Run", "Station"}) {
        m_lifetimeCombo->addItem(QString::fromLatin1(lifetime),
                                 QString::fromLatin1(lifetime));
    }
    form->addRow(tr("Lifetime"), m_lifetimeCombo);
    m_enabledCheck = new OnOffSwitch(content);
    m_enabledCheck->setObjectName(QStringLiteral("deviceEnabledSwitch"));
    m_enabledCheck->setAccessibleName(tr("Enable device"));
    form->addRow(tr("Enabled"), m_enabledCheck);
    m_optionsEdit = new QPlainTextEdit(content);
    m_optionsEdit->setObjectName(QStringLiteral("deviceOptionsEdit"));
    m_optionsEdit->setFixedHeight(180);
    form->addRow(tr("Options (JSON)"), m_optionsEdit);
    layout->addLayout(form);
    m_deviceError = new QLabel(content);
    m_deviceError->setObjectName(QStringLiteral("devicePropertyError"));
    m_deviceError->setWordWrap(true);
    m_deviceError->setStyleSheet(QStringLiteral("color: #d9534f;"));
    m_deviceError->hide();
    layout->addWidget(m_deviceError);
    layout->addStretch(1);
    m_tabs->addTab(scrollPage(content, m_tabs), tr("Device"));
}

void StationPropertyEditor::setCurrentDevice(int row)
{
    if (row == m_currentRow && m_pendingChanges) {
        return;
    }
    m_currentRow = row;
    loadDevice();
}

bool StationPropertyEditor::hasPendingChanges() const
{
    return m_pendingChanges;
}

bool StationPropertyEditor::commitPendingChanges()
{
    if (!m_pendingChanges) {
        return true;
    }
    return commitDevice();
}

void StationPropertyEditor::discardPendingChanges()
{
    setPendingChanges(false);
    loadDevice();
}

int StationPropertyEditor::currentDeviceRow() const
{
    return m_currentRow;
}

void StationPropertyEditor::setEditable(bool editable)
{
    m_editable = editable;
    for (auto* widget : findChildren<QWidget*>()) {
        if (widget != m_tabs) {
            widget->setEnabled(editable);
        }
    }
    m_tabs->setEnabled(true);
    if (!m_pendingChanges) {
        loadDevice();
    }
}

void StationPropertyEditor::setStationPageVisible(bool visible)
{
    m_tabs->setTabVisible(0, visible);
    if (!visible) {
        m_tabs->setCurrentIndex(1);
    }
}

void StationPropertyEditor::setPluginRegistry(QVector<PluginManifest> plugins)
{
    m_loading = true;
    m_plugins = std::move(plugins);
    m_pluginCombo->clear();
    m_pluginCombo->addItem(tr("Manual / legacy driver"), QString());
    for (const auto& plugin : m_plugins) {
        m_pluginCombo->addItem(
            QStringLiteral("%1 / %2").arg(plugin.category, plugin.name),
            plugin.moduleId);
    }
    m_loading = false;
    loadDevice();
}

bool StationPropertyEditor::focusField(const QString& path)
{
    QWidget* field = nullptr;
    if (path == "stationId" || path == "id") field = m_stationIdEdit;
    else if (path == "name") field = m_stationNameEdit;
    else if (path == "scanDialogEnabled") field = m_scanDialogEnabledCheck;
    else if (path.startsWith("metadata")) field = m_metadataEdit;
    else if (path.endsWith("deviceId") || path.endsWith(".id")) field = m_deviceIdEdit;
    else if (path.endsWith("deviceType") || path.endsWith(".type")) field = m_deviceTypeEdit;
    else if (path.endsWith("driverId") || path.endsWith(".driver")) field = m_driverIdEdit;
    else if (path.endsWith("pluginPath")) field = m_pluginCombo;
    else if (path.endsWith("address") || path.endsWith("visaAddress")) field = m_addressEdit;
    else if (path.endsWith("lifetime")) field = m_lifetimeCombo;
    else if (path.endsWith("timeoutMs")) field = m_timeoutSpin;
    else if (path.endsWith("enabled")) field = m_enabledCheck;
    else if (path.contains("options")) field = m_optionsEdit;
    if (!field) {
        return false;
    }
    m_tabs->setCurrentIndex(path.startsWith("devices[") ? 1 : 0);
    field->setFocus(Qt::OtherFocusReason);
    return true;
}

void StationPropertyEditor::reload()
{
    loadStation();
    if (!m_pendingChanges) {
        loadDevice();
    }
}

void StationPropertyEditor::loadStation()
{
    m_loading = true;
    const auto root = m_document ? m_document->rootObject() : QJsonObject{};
    const bool valid = !root.isEmpty();
    m_stationIdEdit->setText(valueWithAlias(root, "stationId", "id"));
    m_stationNameEdit->setText(root.value("name").toString());
    m_scanDialogEnabledCheck->setChecked(
        root.value("scanDialogEnabled").toBool(true));
    m_metadataEdit->setPlainText(objectText(root.value("metadata").toObject()));
    m_stationError->hide();
    m_loading = false;
}

void StationPropertyEditor::loadDevice()
{
    m_loading = true;
    const auto device = m_document ? m_document->deviceAt(m_currentRow) : QJsonObject{};
    const bool valid = !device.isEmpty();
    m_deviceIdEdit->setText(valueWithAlias(device, "deviceId", "id"));
    m_deviceTypeEdit->setText(valueWithAlias(device, "deviceType", "type"));
    m_driverIdEdit->setText(valueWithAlias(device, "driverId", "driver"));
    const int pluginIndex = m_pluginCombo->findData(
        m_driverIdEdit->text(), Qt::UserRole, Qt::MatchFixedString);
    m_pluginCombo->setCurrentIndex(pluginIndex < 0 ? 0 : pluginIndex);
    m_addressEdit->setText(valueWithAlias(device, "address", "visaAddress"));
    m_timeoutSpin->setValue(device.value("timeoutMs").toInt(30000));
    const auto lifetime = device.value("lifetime").toString("Station");
    const int lifetimeIndex = m_lifetimeCombo->findData(lifetime, Qt::UserRole,
                                                        Qt::MatchFixedString);
    m_lifetimeCombo->setCurrentIndex(lifetimeIndex < 0 ? 2 : lifetimeIndex);
    m_enabledCheck->setChecked(device.value("enabled").toBool(true));
    m_optionsEdit->setPlainText(objectText(device.value("options").toObject()));
    const std::array<QWidget*, 9> deviceFields = {
        m_deviceIdEdit, m_deviceTypeEdit, m_driverIdEdit, m_pluginCombo,
        m_addressEdit, m_timeoutSpin, m_lifetimeCombo, m_enabledCheck,
        m_optionsEdit};
    for (auto* widget : deviceFields) {
        widget->setEnabled(m_editable && valid);
    }
    m_deviceError->hide();
    m_loading = false;
}

bool StationPropertyEditor::commitStation()
{
    if (!m_document || m_document->isEmpty()) {
        return false;
    }
    if (m_stationIdEdit->text().trimmed().isEmpty()) {
        showStationError(tr("Station ID cannot be empty"));
        return false;
    }
    QJsonObject metadata;
    QString parseError;
    if (!parseObject(m_metadataEdit->toPlainText(), metadata, parseError)) {
        showStationError(tr("Metadata: %1").arg(parseError));
        return false;
    }

    auto root = m_document->rootObject();
    root.insert("stationId", m_stationIdEdit->text().trimmed());
    root.remove("id");
    root.insert("name", m_stationNameEdit->text().trimmed());
    root.insert("scanDialogEnabled", m_scanDialogEnabledCheck->isChecked());
    root.insert("metadata", metadata);
    const bool changed = m_document->replaceRootObject(std::move(root));
    m_stationError->hide();
    if (changed) {
        emit stationApplied();
    }
    return true;
}

bool StationPropertyEditor::commitDevice()
{
    if (!m_document || m_currentRow < 0) {
        return false;
    }
    const int appliedRow = m_currentRow;
    const auto originalDevice = m_document->deviceAt(appliedRow);
    if (originalDevice.isEmpty()) {
        showDeviceError(tr("The selected device no longer exists"));
        return false;
    }
    if (m_deviceIdEdit->text().trimmed().isEmpty()) {
        showDeviceError(tr("Logical device ID cannot be empty"));
        return false;
    }
    if (m_driverIdEdit->text().trimmed().isEmpty() &&
        m_pluginCombo->currentData().toString().isEmpty()) {
        showDeviceError(tr("Driver ID cannot be empty"));
        return false;
    }
    QJsonObject options;
    QString parseError;
    if (!parseObject(m_optionsEdit->toPlainText(), options, parseError)) {
        showDeviceError(tr("Options: %1").arg(parseError));
        return false;
    }

    auto device = originalDevice;
    const auto originalDriverId = valueWithAlias(device, "driverId", "driver").trimmed();
    const auto originalPluginPath = device.value("pluginPath").toString().trimmed();
    const auto selectedModuleId = m_pluginCombo->currentData().toString();
    if (!selectedModuleId.isEmpty()) {
        const auto iterator = std::find_if(
            m_plugins.cbegin(), m_plugins.cend(),
            [&selectedModuleId](const PluginManifest& plugin) {
                return plugin.moduleId == selectedModuleId;
            });
        if (iterator != m_plugins.cend()) {
            m_driverIdEdit->setText(iterator->moduleId);
            m_deviceTypeEdit->setText(iterator->category);
            // Applying enable/address/options changes must not silently replace a
            // deployed DLL with the catalog's source/build copy. Only a real
            // plugin-model change (or a missing binding) owns pluginPath.
            if (selectedModuleId != originalDriverId || originalPluginPath.isEmpty()) {
                device.insert("pluginPath", iterator->dllPath);
            }
        }
    } else {
        device.remove("pluginPath");
    }
    device.insert("deviceId", m_deviceIdEdit->text().trimmed());
    device.remove("id");
    device.insert("deviceType", m_deviceTypeEdit->text().trimmed());
    device.remove("type");
    device.insert("driverId", m_driverIdEdit->text().trimmed());
    device.remove("driver");
    device.insert("address", m_addressEdit->text().trimmed());
    device.remove("visaAddress");
    device.insert("lifetime", m_lifetimeCombo->currentData().toString());
    device.insert("timeoutMs", m_timeoutSpin->value());
    device.insert("enabled", m_enabledCheck->isChecked());
    device.insert("options", options);
    if (device == originalDevice) {
        m_deviceError->hide();
        setPendingChanges(false);
        return true;
    }
    if (!m_document->replaceDevice(appliedRow, std::move(device))) {
        showDeviceError(m_document->deviceAt(appliedRow).isEmpty()
                            ? tr("The selected device no longer exists")
                            : tr("Unable to update the selected device"));
        return false;
    }
    m_deviceError->hide();
    setPendingChanges(false);
    loadDevice();
    emit deviceApplied(appliedRow);
    return true;
}

void StationPropertyEditor::markPendingChanges()
{
    if (!m_loading && m_editable && m_currentRow >= 0) {
        setPendingChanges(true);
    }
}

void StationPropertyEditor::setPendingChanges(bool pending)
{
    if (m_pendingChanges == pending) {
        return;
    }
    m_pendingChanges = pending;
    if (m_title) {
        m_title->setText(m_pendingChanges ? tr("Properties *") : tr("Properties"));
    }
    emit pendingChangesChanged(m_pendingChanges);
}

void StationPropertyEditor::showStationError(const QString& message)
{
    m_stationError->setText(message);
    m_stationError->show();
}

void StationPropertyEditor::showDeviceError(const QString& message)
{
    m_deviceError->setText(message);
    m_deviceError->show();
}

} // namespace PicoATE::Ui
