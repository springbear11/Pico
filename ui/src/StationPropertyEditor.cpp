#include "StationPropertyEditor.h"

#include "OnOffControl.h"
#include "StationDocument.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFont>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QScrollArea>
#include <QSpinBox>
#include <QTabWidget>
#include <QVBoxLayout>

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <utility>

namespace PicoATE::Ui {

namespace {

QString objectText(const QJsonObject& object)
{
    return QString::fromUtf8(
        QJsonDocument(object).toJson(QJsonDocument::Indented));
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

QString normalizedType(QString type)
{
    type = type.trimmed().toUpper();
    return type.isEmpty() ? QStringLiteral("PLUGIN") : type;
}

int optionChannelIndex(const QJsonObject& device)
{
    return qMax(0, device.value(QStringLiteral("options"))
                       .toObject()
                       .value(QStringLiteral("channelIndex"))
                       .toInt());
}

bool isCanDeviceScopedOption(const QString& key)
{
    static const std::array<QString, 4> keys = {
        QStringLiteral("deviceType"),
        QStringLiteral("deviceIndex"),
        QStringLiteral("hardwareId"),
        QStringLiteral("serialNumber")};
    return std::find(keys.cbegin(), keys.cend(), key) != keys.cend();
}

bool isTopLevelDeviceField(const QString& key)
{
    return key == QStringLiteral("deviceId") ||
           key == QStringLiteral("channelIndex") ||
           key == QStringLiteral("address") ||
           key == QStringLiteral("visaAddress");
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

void clearForm(QFormLayout* form)
{
    while (form && form->rowCount() > 0) {
        const auto row = form->takeRow(0);
        if (row.labelItem) {
            delete row.labelItem->widget();
            delete row.labelItem;
        }
        if (row.fieldItem) {
            delete row.fieldItem->widget();
            delete row.fieldItem;
        }
    }
}

} // namespace

StationPropertyEditor::StationPropertyEditor(StationDocument* document,
                                             QWidget* parent)
    : QWidget(parent)
    , m_document(document)
{
    Q_ASSERT(m_document);
    setObjectName(QStringLiteral("stationPropertyEditor"));
    setMinimumWidth(260);

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

    connect(m_deviceTypeCombo, &QComboBox::currentIndexChanged,
            this, [this] {
                if (m_loading) {
                    return;
                }
                reloadPluginChoices();
                updateAddressPresentation();
                rebuildChannelSwitches();
                rebuildOptionEditors();
                updateLogicalIdPreview();
                markPendingChanges();
            });
    connect(m_pluginCombo, &QComboBox::currentIndexChanged,
            this, [this] {
                if (m_loading) {
                    return;
                }
                rebuildChannelSwitches();
                rebuildOptionEditors();
                markPendingChanges();
            });
    connect(m_addressEdit, &QLineEdit::textEdited,
            this, &StationPropertyEditor::markPendingChanges);
    connect(m_timeoutSpin, &QSpinBox::valueChanged,
            this, &StationPropertyEditor::markPendingChanges);
    connect(m_lifetimeCombo, &QComboBox::currentIndexChanged,
            this, &StationPropertyEditor::markPendingChanges);

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
    m_scanDialogEnabledCheck->setObjectName(
        QStringLiteral("scanDialogEnabledCheck"));
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
    content->setFont(QFont(QStringLiteral("Microsoft YaHei UI"), 9));
    auto* layout = new QVBoxLayout(content);
    auto* form = new QFormLayout;
    form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
    form->setRowWrapPolicy(QFormLayout::WrapLongRows);

    m_deviceIdEdit = new QLineEdit(content);
    m_deviceIdEdit->setObjectName(QStringLiteral("deviceIdEdit"));
    m_deviceIdEdit->setReadOnly(true);
    m_deviceIdEdit->setMinimumHeight(32);
    m_deviceIdEdit->setFont(
        QFont(QStringLiteral("Segoe UI"), 10, QFont::DemiBold));
    m_deviceIdEdit->setStyleSheet(QStringLiteral(
        "QLineEdit { background: #eef1f5; color: #475467; "
        "border: 1px solid #d0d5dd; border-radius: 4px; "
        "padding: 5px 8px; selection-background-color: #b9ddff; }"));
    m_deviceIdEdit->setToolTip(tr("Generated automatically from device type, order, and enabled channels"));
    form->addRow(tr("Logical ID"), m_deviceIdEdit);

    m_deviceTypeCombo = new QComboBox(content);
    m_deviceTypeCombo->setObjectName(QStringLiteral("deviceTypeCombo"));
    for (const auto* type : {"CAN", "DMM", "PSU", "SCOPE", "MCU",
                             "SERIAL", "MODBUS", "PLUGIN"}) {
        m_deviceTypeCombo->addItem(QString::fromLatin1(type),
                                   QString::fromLatin1(type));
    }
    form->addRow(tr("Type"), m_deviceTypeCombo);

    m_pluginCombo = new QComboBox(content);
    m_pluginCombo->setObjectName(QStringLiteral("devicePluginCombo"));
    form->addRow(tr("Driver / Model"), m_pluginCombo);

    m_addressEdit = new QLineEdit(content);
    m_addressEdit->setObjectName(QStringLiteral("deviceAddressEdit"));
    m_addressLabel = new QLabel(tr("Address"), content);
    form->addRow(m_addressLabel, m_addressEdit);

    m_timeoutSpin = new QSpinBox(content);
    m_timeoutSpin->setObjectName(QStringLiteral("deviceTimeoutMsSpin"));
    m_timeoutSpin->setRange(1, 600000);
    m_timeoutSpin->setSuffix(tr(" ms"));
    form->addRow(tr("Timeout"), m_timeoutSpin);

    m_lifetimeCombo = new QComboBox(content);
    m_lifetimeCombo->setObjectName(QStringLiteral("deviceLifetimeCombo"));
    for (const auto* lifetime : {"Step", "Run", "Station"}) {
        m_lifetimeCombo->addItem(QString::fromLatin1(lifetime),
                                 QString::fromLatin1(lifetime));
    }
    form->addRow(tr("Lifetime"), m_lifetimeCombo);

    m_channelsLabel = new QLabel(tr("Channels"), content);
    m_channelsWidget = new QWidget(content);
    m_channelsWidget->setObjectName(QStringLiteral("deviceChannelsWidget"));
    m_channelsWidget->setLayout(new QHBoxLayout);
    m_channelsWidget->layout()->setContentsMargins(0, 0, 0, 0);
    m_channelsWidget->layout()->setSpacing(12);
    form->addRow(m_channelsLabel, m_channelsWidget);
    layout->addLayout(form);

    m_optionsGroup = new QGroupBox(tr("Device Options"), content);
    m_optionsGroup->setObjectName(QStringLiteral("deviceOptionsGroup"));
    auto* optionsLayout = new QVBoxLayout(m_optionsGroup);
    m_optionsForm = new QFormLayout;
    m_optionsForm->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
    m_optionsForm->setRowWrapPolicy(QFormLayout::WrapLongRows);
    optionsLayout->addLayout(m_optionsForm);
    m_optionsHint = new QLabel(m_optionsGroup);
    m_optionsHint->setWordWrap(true);
    optionsLayout->addWidget(m_optionsHint);
    layout->addWidget(m_optionsGroup);

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
    setCurrentDevices(row >= 0 ? QVector<int>{row} : QVector<int>{});
}

void StationPropertyEditor::setCurrentDevices(QVector<int> rows,
                                              QString logicalBaseId)
{
    if (rows == m_currentRows && logicalBaseId == m_logicalBaseId &&
        m_pendingChanges) {
        return;
    }
    m_currentRows = std::move(rows);
    m_logicalBaseId = std::move(logicalBaseId);
    loadDevice();
}

bool StationPropertyEditor::hasPendingChanges() const
{
    return m_pendingChanges;
}

bool StationPropertyEditor::commitPendingChanges()
{
    return !m_pendingChanges || commitDevice();
}

void StationPropertyEditor::discardPendingChanges()
{
    setPendingChanges(false);
    loadDevice();
}

int StationPropertyEditor::currentDeviceRow() const
{
    return m_currentRows.isEmpty() ? -1 : m_currentRows.front();
}

void StationPropertyEditor::setEditable(bool editable)
{
    m_editable = editable;
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
    m_plugins = std::move(plugins);
    loadDevice();
}

bool StationPropertyEditor::focusField(const QString& path)
{
    QWidget* field = nullptr;
    if (path == QStringLiteral("stationId") || path == QStringLiteral("id")) {
        field = m_stationIdEdit;
    } else if (path == QStringLiteral("name")) {
        field = m_stationNameEdit;
    } else if (path == QStringLiteral("scanDialogEnabled")) {
        field = m_scanDialogEnabledCheck;
    } else if (path.startsWith(QStringLiteral("metadata"))) {
        field = m_metadataEdit;
    } else if (path.endsWith(QStringLiteral("deviceId")) ||
               path.endsWith(QStringLiteral(".id"))) {
        field = m_deviceIdEdit;
    } else if (path.endsWith(QStringLiteral("deviceType")) ||
               path.endsWith(QStringLiteral(".type"))) {
        field = m_deviceTypeCombo;
    } else if (path.endsWith(QStringLiteral("driverId")) ||
               path.endsWith(QStringLiteral(".driver")) ||
               path.endsWith(QStringLiteral("pluginPath"))) {
        field = m_pluginCombo;
    } else if (path.endsWith(QStringLiteral("address")) ||
               path.endsWith(QStringLiteral("visaAddress"))) {
        field = m_addressEdit;
    } else if (path.endsWith(QStringLiteral("lifetime"))) {
        field = m_lifetimeCombo;
    } else if (path.endsWith(QStringLiteral("timeoutMs"))) {
        field = m_timeoutSpin;
    } else if (path.endsWith(QStringLiteral("enabled"))) {
        field = m_deviceTypeCombo->currentData().toString() ==
                QStringLiteral("CAN") ? m_channelsWidget : m_deviceIdEdit;
    } else if (path.contains(QStringLiteral("options"))) {
        for (const auto& item : m_optionEditors) {
            if (path.endsWith(item.definition.key)) {
                field = item.widget;
                break;
            }
        }
        if (!field && path.endsWith(QStringLiteral("channelIndex"))) {
            field = m_channelsWidget;
        }
        if (!field) {
            field = m_optionsGroup;
        }
    }
    if (!field) {
        return false;
    }
    m_tabs->setCurrentIndex(path.startsWith(QStringLiteral("devices[")) ? 1 : 0);
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
    m_stationIdEdit->setText(valueWithAlias(
        root, QStringLiteral("stationId"), QStringLiteral("id")));
    m_stationNameEdit->setText(root.value(QStringLiteral("name")).toString());
    m_scanDialogEnabledCheck->setChecked(
        root.value(QStringLiteral("scanDialogEnabled")).toBool(true));
    m_metadataEdit->setPlainText(
        objectText(root.value(QStringLiteral("metadata")).toObject()));
    m_stationError->hide();
    m_loading = false;
}

void StationPropertyEditor::loadDevice()
{
    m_loading = true;
    const auto device = m_document && !m_currentRows.isEmpty()
        ? m_document->deviceAt(m_currentRows.front())
        : QJsonObject{};
    const bool valid = !device.isEmpty();
    m_loadedDeviceType = normalizedType(valueWithAlias(
        device, QStringLiteral("deviceType"), QStringLiteral("type")));
    m_loadedDriverId = valueWithAlias(
        device, QStringLiteral("driverId"), QStringLiteral("driver"));

    int typeIndex = m_deviceTypeCombo->findData(m_loadedDeviceType);
    if (typeIndex < 0 && !m_loadedDeviceType.isEmpty()) {
        m_deviceTypeCombo->addItem(m_loadedDeviceType, m_loadedDeviceType);
        typeIndex = m_deviceTypeCombo->count() - 1;
    }
    m_deviceTypeCombo->setCurrentIndex(typeIndex < 0 ? 0 : typeIndex);
    reloadPluginChoices(m_loadedDriverId);
    m_addressEdit->setText(valueWithAlias(
        device, QStringLiteral("address"), QStringLiteral("visaAddress")));
    updateAddressPresentation();
    m_timeoutSpin->setValue(
        device.value(QStringLiteral("timeoutMs")).toInt(30000));
    const auto lifetime = device.value(QStringLiteral("lifetime"))
                              .toString(QStringLiteral("Station"));
    const int lifetimeIndex = m_lifetimeCombo->findData(lifetime);
    m_lifetimeCombo->setCurrentIndex(lifetimeIndex < 0 ? 2 : lifetimeIndex);
    rebuildChannelSwitches();
    rebuildOptionEditors();
    updateLogicalIdPreview();

    const std::array<QWidget*, 6> fields = {
        m_deviceIdEdit, m_deviceTypeCombo, m_pluginCombo, m_addressEdit,
        m_timeoutSpin, m_lifetimeCombo};
    for (auto* widget : fields) {
        widget->setEnabled(m_editable && valid);
    }
    m_deviceIdEdit->setReadOnly(true);
    m_deviceError->hide();
    m_loading = false;
}

void StationPropertyEditor::reloadPluginChoices(const QString& selectedModuleId)
{
    const bool wasLoading = m_loading;
    m_loading = true;
    const auto selected = selectedModuleId.isEmpty()
        ? m_pluginCombo->currentData().toString()
        : selectedModuleId;
    const auto type = m_deviceTypeCombo->currentData().toString();
    auto effectiveSelected = selected;
    const auto selectedPlugin = std::find_if(
        m_plugins.cbegin(), m_plugins.cend(),
        [&effectiveSelected](const PluginManifest& plugin) {
            return plugin.moduleId == effectiveSelected;
        });
    if ((selectedPlugin != m_plugins.cend() &&
         selectedPlugin->category.compare(type, Qt::CaseInsensitive) != 0) ||
        (selectedPlugin == m_plugins.cend() && type != m_loadedDeviceType)) {
        effectiveSelected.clear();
    }
    m_pluginCombo->clear();
    for (const auto& plugin : m_plugins) {
        if (plugin.category.trimmed().compare(type, Qt::CaseInsensitive) != 0) {
            continue;
        }
        m_pluginCombo->addItem(plugin.moduleId, plugin.moduleId);
        const int item = m_pluginCombo->count() - 1;
        const auto detail = plugin.name.isEmpty()
            ? plugin.dllPath
            : QStringLiteral("%1\n%2").arg(plugin.name, plugin.dllPath);
        m_pluginCombo->setItemData(item, detail, Qt::ToolTipRole);
    }
    if (!effectiveSelected.isEmpty() &&
        m_pluginCombo->findData(effectiveSelected) < 0) {
        m_pluginCombo->addItem(
            tr("%1 (current driver unavailable)").arg(effectiveSelected),
            effectiveSelected);
    }
    if (m_pluginCombo->count() == 0) {
        m_pluginCombo->addItem(tr("No compatible driver found"), QVariant{});
        m_pluginCombo->setEnabled(false);
    } else {
        const int pluginIndex = m_pluginCombo->findData(effectiveSelected);
        m_pluginCombo->setCurrentIndex(pluginIndex >= 0 ? pluginIndex : 0);
        m_pluginCombo->setEnabled(m_editable && !m_currentRows.isEmpty());
    }
    m_loading = wasLoading;
}

void StationPropertyEditor::rebuildChannelSwitches()
{
    auto* layout = qobject_cast<QHBoxLayout*>(m_channelsWidget->layout());
    while (layout && layout->count() > 0) {
        auto* item = layout->takeAt(0);
        delete item->widget();
        delete item;
    }
    m_channelSwitches.clear();
    const bool can = m_deviceTypeCombo->currentData().toString() ==
                     QStringLiteral("CAN");
    m_channelsLabel->setVisible(can);
    m_channelsWidget->setVisible(can);
    if (!can || !layout) {
        return;
    }

    int count = 2;
    if (const auto* plugin = selectedPlugin()) {
        if (const auto* function = connectionFunction(*plugin)) {
            const auto iterator = std::find_if(
                function->inputs.cbegin(), function->inputs.cend(),
                [](const PluginParameterDefinition& input) {
                    return input.key == QStringLiteral("channelIndex");
                });
            if (iterator != function->inputs.cend() && iterator->maximum) {
                count = qMax(1, static_cast<int>(*iterator->maximum) + 1);
            }
        }
    }
    for (const int row : m_currentRows) {
        count = qMax(count, optionChannelIndex(m_document->deviceAt(row)) + 1);
    }

    for (int channel = 0; channel < count; ++channel) {
        auto* cell = new QWidget(m_channelsWidget);
        auto* cellLayout = new QVBoxLayout(cell);
        cellLayout->setContentsMargins(0, 0, 0, 0);
        cellLayout->setSpacing(3);
        auto* label = new QLabel(tr("CH%1").arg(channel + 1), cell);
        label->setAlignment(Qt::AlignCenter);
        auto* toggle = new OnOffSwitch(cell);
        toggle->setObjectName(QStringLiteral("deviceChannel%1Switch")
                                  .arg(channel + 1));
        bool enabled = false;
        for (const int row : m_currentRows) {
            const auto device = m_document->deviceAt(row);
            if (optionChannelIndex(device) == channel) {
                enabled = device.value(QStringLiteral("enabled")).toBool(true);
                break;
            }
        }
        toggle->setChecked(enabled);
        toggle->setEnabled(m_editable && !m_currentRows.isEmpty());
        connect(toggle, &QAbstractButton::toggled,
                this, [this] {
                    updateLogicalIdPreview();
                    markPendingChanges();
                });
        cellLayout->addWidget(label);
        cellLayout->addWidget(toggle);
        layout->addWidget(cell);
        m_channelSwitches.push_back(toggle);
    }
    layout->addStretch(1);
}

void StationPropertyEditor::rebuildOptionEditors()
{
    clearForm(m_optionsForm);
    m_optionEditors.clear();
    const bool can = m_deviceTypeCombo->currentData().toString() ==
                     QStringLiteral("CAN");
    QHash<int, QJsonObject> currentOptionsByChannel;
    for (const int row : m_currentRows) {
        const auto device = m_document->deviceAt(row);
        currentOptionsByChannel.insert(
            optionChannelIndex(device),
            device.value(QStringLiteral("options")).toObject());
    }
    const auto sharedOptions = m_document && !m_currentRows.isEmpty()
        ? m_document->deviceAt(m_currentRows.front())
              .value(QStringLiteral("options")).toObject()
        : QJsonObject{};
    const auto* plugin = selectedPlugin();
    const auto* function = plugin ? connectionFunction(*plugin) : nullptr;
    if (!function) {
        m_optionsHint->setText(
            plugin ? tr("This driver does not describe connection options.")
                   : tr("Scan or select a compatible driver to configure device options."));
        m_optionsHint->show();
        return;
    }

    const auto addEditor = [this](const PluginParameterDefinition& definition,
                                  const QVariant& value,
                                  int channelIndex) {
        QWidget* editor = nullptr;
        if (definition.type == PluginParameterType::Boolean) {
            auto* toggle = new OnOffSwitch(m_optionsGroup);
            toggle->setChecked(value.toBool());
            connect(toggle, &QAbstractButton::toggled,
                    this, &StationPropertyEditor::markPendingChanges);
            editor = toggle;
        } else if (definition.type == PluginParameterType::Enumeration) {
            auto* combo = new QComboBox(m_optionsGroup);
            for (const auto& option : definition.options) {
                combo->addItem(option.label, option.value);
            }
            const int selected = combo->findData(value);
            combo->setCurrentIndex(selected >= 0 ? selected : 0);
            connect(combo, &QComboBox::currentIndexChanged,
                    this, &StationPropertyEditor::markPendingChanges);
            editor = combo;
        } else if (definition.type == PluginParameterType::Integer) {
            auto* spin = new QSpinBox(m_optionsGroup);
            spin->setRange(
                definition.minimum
                    ? static_cast<int>(std::ceil(*definition.minimum))
                    : std::numeric_limits<int>::min(),
                definition.maximum
                    ? static_cast<int>(std::floor(*definition.maximum))
                    : std::numeric_limits<int>::max());
            spin->setValue(value.toInt());
            if (!definition.unit.isEmpty()) {
                spin->setSuffix(QStringLiteral(" %1").arg(definition.unit));
            }
            connect(spin, &QSpinBox::valueChanged,
                    this, &StationPropertyEditor::markPendingChanges);
            editor = spin;
        } else if (definition.type == PluginParameterType::Number) {
            auto* spin = new QDoubleSpinBox(m_optionsGroup);
            spin->setDecimals(6);
            spin->setRange(definition.minimum.value_or(-1.0e12),
                           definition.maximum.value_or(1.0e12));
            spin->setValue(value.toDouble());
            if (!definition.unit.isEmpty()) {
                spin->setSuffix(QStringLiteral(" %1").arg(definition.unit));
            }
            connect(spin, &QDoubleSpinBox::valueChanged,
                    this, &StationPropertyEditor::markPendingChanges);
            editor = spin;
        } else {
            auto* edit = new QLineEdit(m_optionsGroup);
            edit->setText(value.toString());
            if (definition.type == PluginParameterType::HexBytes) {
                edit->setPlaceholderText(tr("Example: 01 02 03 04"));
            }
            connect(edit, &QLineEdit::textEdited,
                    this, &StationPropertyEditor::markPendingChanges);
            editor = edit;
        }
        auto objectName = definition.key;
        objectName.replace(QLatin1Char('.'), QLatin1Char('_'));
        editor->setObjectName(channelIndex >= 0
            ? QStringLiteral("deviceOption_ch%1_%2")
                  .arg(channelIndex + 1).arg(objectName)
            : QStringLiteral("deviceOption_%1").arg(objectName));
        editor->setEnabled(m_editable && !m_currentRows.isEmpty());
        QString label = definition.name.isEmpty()
            ? definition.key : definition.name;
        if (channelIndex >= 0) {
            label = tr("CH%1 %2").arg(channelIndex + 1).arg(label);
        }
        if (definition.required) {
            label += QStringLiteral(" *");
        }
        m_optionsForm->addRow(label, editor);
        m_optionEditors.push_back({definition, editor, channelIndex});
    };

    for (const auto& definition : function->inputs) {
        if (isTopLevelDeviceField(definition.key)) {
            continue;
        }
        const bool perChannel = can &&
                                !isCanDeviceScopedOption(definition.key);
        if (perChannel) {
            for (int channel = 0; channel < m_channelSwitches.size(); ++channel) {
                QVariant value = currentOptionsByChannel.value(channel)
                                     .value(definition.key).toVariant();
                if (!value.isValid() && definition.defaultValue.isValid()) {
                    value = definition.defaultValue;
                }
                addEditor(definition, value, channel);
            }
        } else {
            QVariant value = sharedOptions.value(definition.key).toVariant();
            if (!value.isValid() && definition.defaultValue.isValid()) {
                value = definition.defaultValue;
            }
            addEditor(definition, value, -1);
        }
    }
    m_optionsHint->setVisible(m_optionEditors.isEmpty());
    m_optionsHint->setText(m_optionEditors.isEmpty()
        ? tr("This driver has no configurable connection options.")
        : QString{});
}

void StationPropertyEditor::updateLogicalIdPreview()
{
    const auto baseId = effectiveBaseId();
    if (m_deviceTypeCombo->currentData().toString() != QStringLiteral("CAN")) {
        m_deviceIdEdit->setText(baseId);
        return;
    }
    QStringList ids;
    for (int channel = 0; channel < m_channelSwitches.size(); ++channel) {
        if (m_channelSwitches[channel]->isChecked()) {
            ids.push_back(QStringLiteral("%1.CH%2").arg(baseId).arg(channel + 1));
        }
    }
    m_deviceIdEdit->setText(ids.isEmpty()
        ? tr("%1 (No active channel)").arg(baseId)
        : ids.join(QStringLiteral(" / ")));
}

void StationPropertyEditor::updateAddressPresentation()
{
    const auto type = m_deviceTypeCombo->currentData()
                          .toString().trimmed().toUpper();
    const bool visible = type != QStringLiteral("CAN");
    m_addressLabel->setVisible(visible);
    m_addressEdit->setVisible(visible);
    if (type == QStringLiteral("SERIAL")) {
        m_addressLabel->setText(tr("Port"));
        m_addressEdit->setPlaceholderText(tr("For example: COM3"));
    } else if (type == QStringLiteral("MODBUS")) {
        m_addressLabel->setText(tr("Port / IP"));
        m_addressEdit->setPlaceholderText(
            tr("For example: COM3 or 192.168.1.20"));
    } else if (type == QStringLiteral("DMM") ||
               type == QStringLiteral("PSU") ||
               type == QStringLiteral("SCOPE")) {
        m_addressLabel->setText(tr("Resource / Address"));
        m_addressEdit->setPlaceholderText(
            tr("VISA resource, IP address, or serial port"));
    } else if (type == QStringLiteral("MCU")) {
        m_addressLabel->setText(tr("Port / Address"));
        m_addressEdit->setPlaceholderText(
            tr("Serial port, IP, or USB identifier"));
    } else {
        m_addressLabel->setText(tr("Address"));
        m_addressEdit->setPlaceholderText({});
    }
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
    root.insert(QStringLiteral("stationId"),
                m_stationIdEdit->text().trimmed());
    root.remove(QStringLiteral("id"));
    root.insert(QStringLiteral("name"),
                m_stationNameEdit->text().trimmed());
    root.insert(QStringLiteral("scanDialogEnabled"),
                m_scanDialogEnabledCheck->isChecked());
    root.insert(QStringLiteral("metadata"), metadata);
    const bool changed = m_document->replaceRootObject(std::move(root));
    m_stationError->hide();
    if (changed) {
        emit stationApplied();
    }
    return true;
}

bool StationPropertyEditor::commitDevice()
{
    if (!m_document || m_currentRows.isEmpty()) {
        return false;
    }
    const auto original = m_document->deviceAt(m_currentRows.front());
    if (original.isEmpty()) {
        showDeviceError(tr("The selected device no longer exists"));
        return false;
    }
    const auto* plugin = selectedPlugin();
    const bool can = m_deviceTypeCombo->currentData().toString() ==
                     QStringLiteral("CAN");
    bool anyEnabled = can
        ? false
        : original.value(QStringLiteral("enabled")).toBool(true);
    for (const auto* channel : m_channelSwitches) {
        anyEnabled = anyEnabled || channel->isChecked();
    }
    if (anyEnabled && !plugin) {
        showDeviceError(tr("No compatible Driver / Model is available for this device"));
        return false;
    }

    QJsonObject sharedOptions = original.value(QStringLiteral("options")).toObject();
    QHash<int, QJsonObject> channelOptions;
    for (const int row : m_currentRows) {
        const auto device = m_document->deviceAt(row);
        channelOptions.insert(
            optionChannelIndex(device),
            device.value(QStringLiteral("options")).toObject());
    }
    QString optionError;
    if (!collectOptionValues(sharedOptions, channelOptions, optionError)) {
        showDeviceError(optionError);
        return false;
    }

    auto makeDevice = [&](int channel, bool enabled) {
        auto device = original;
        device.insert(QStringLiteral("deviceId"),
                      can ? QStringLiteral("%1.CH%2")
                                .arg(effectiveBaseId()).arg(channel + 1)
                          : effectiveBaseId());
        device.remove(QStringLiteral("id"));
        device.insert(QStringLiteral("deviceType"),
                      m_deviceTypeCombo->currentData().toString());
        device.remove(QStringLiteral("type"));
        if (plugin) {
            device.insert(QStringLiteral("driverId"), plugin->moduleId);
        } else {
            device.remove(QStringLiteral("driverId"));
            device.remove(QStringLiteral("driver"));
        }
        if (m_addressEdit->isVisible()) {
            device.insert(QStringLiteral("address"),
                          m_addressEdit->text().trimmed());
            device.remove(QStringLiteral("visaAddress"));
        } else {
            device.remove(QStringLiteral("address"));
            device.remove(QStringLiteral("visaAddress"));
        }
        device.insert(QStringLiteral("lifetime"),
                      m_lifetimeCombo->currentData().toString());
        device.insert(QStringLiteral("timeoutMs"), m_timeoutSpin->value());
        device.insert(QStringLiteral("enabled"), enabled);
        auto effectiveOptions = channelOptions.value(channel, sharedOptions);
        effectiveOptions.remove(QStringLiteral("address"));
        effectiveOptions.remove(QStringLiteral("visaAddress"));
        for (const auto& item : m_optionEditors) {
            if (item.channelIndex < 0 && sharedOptions.contains(item.definition.key)) {
                effectiveOptions.insert(item.definition.key,
                                        sharedOptions.value(item.definition.key));
            }
        }
        if (can) {
            effectiveOptions.insert(QStringLiteral("channelIndex"), channel);
        } else {
            effectiveOptions.remove(QStringLiteral("channelIndex"));
        }
        device.insert(QStringLiteral("options"), effectiveOptions);
        return device;
    };

    QVector<QJsonObject> replacements;
    if (can) {
        for (int channel = 0; channel < m_channelSwitches.size(); ++channel) {
            replacements.push_back(makeDevice(
                channel, m_channelSwitches[channel]->isChecked()));
        }
    } else {
        replacements.push_back(makeDevice(
            0, original.value(QStringLiteral("enabled")).toBool(true)));
    }

    auto root = m_document->rootObject();
    auto devices = root.value(QStringLiteral("devices")).toArray();
    auto sortedRows = m_currentRows;
    std::sort(sortedRows.begin(), sortedRows.end(), std::greater<int>());
    for (const int row : sortedRows) {
        if (row >= 0 && row < devices.size()) {
            devices.removeAt(row);
        }
    }
    const auto selectedType = m_deviceTypeCombo->currentData().toString();
    int insertionRow = devices.size();
    for (int index = 0; index < devices.size(); ++index) {
        const auto type = normalizedType(valueWithAlias(
            devices[index].toObject(),
            QStringLiteral("deviceType"),
            QStringLiteral("type")));
        if (type == selectedType) {
            insertionRow = index + 1;
        }
    }
    for (int index = 0; index < replacements.size(); ++index) {
        devices.insert(insertionRow + index, replacements[index]);
    }
    root.insert(QStringLiteral("devices"), devices);
    if (root != m_document->rootObject()) {
        m_document->replaceRootObject(std::move(root));
    }

    m_currentRows.clear();
    for (int index = 0; index < replacements.size(); ++index) {
        m_currentRows.push_back(insertionRow + index);
    }
    m_deviceError->hide();
    setPendingChanges(false);
    loadDevice();
    emit deviceApplied(insertionRow);
    return true;
}

bool StationPropertyEditor::collectOptionValues(
    QJsonObject& sharedOptions,
    QHash<int, QJsonObject>& channelOptions,
    QString& errorMessage) const
{
    const auto storeOptions = [&](const OptionEditor& item,
                                  const QJsonObject& options) {
        if (item.channelIndex >= 0) {
            channelOptions[item.channelIndex] = options;
        } else {
            sharedOptions = options;
        }
    };

    for (const auto& item : m_optionEditors) {
        const auto& definition = item.definition;
        auto options = item.channelIndex >= 0
            ? channelOptions.value(item.channelIndex)
            : sharedOptions;
        if (const auto* toggle = qobject_cast<const QAbstractButton*>(item.widget)) {
            options.insert(definition.key, toggle->isChecked());
            storeOptions(item, options);
            continue;
        }
        if (const auto* combo = qobject_cast<const QComboBox*>(item.widget)) {
            if (combo->currentIndex() < 0 || !combo->currentData().isValid()) {
                if (definition.required) {
                    errorMessage = tr("%1 is required").arg(definition.name);
                    return false;
                }
                options.remove(definition.key);
            } else {
                options.insert(definition.key,
                               QJsonValue::fromVariant(combo->currentData()));
            }
            storeOptions(item, options);
            continue;
        }
        if (const auto* spin = qobject_cast<const QSpinBox*>(item.widget)) {
            options.insert(definition.key, spin->value());
            storeOptions(item, options);
            continue;
        }
        if (const auto* spin = qobject_cast<const QDoubleSpinBox*>(item.widget)) {
            options.insert(definition.key, spin->value());
            storeOptions(item, options);
            continue;
        }
        const auto* edit = qobject_cast<const QLineEdit*>(item.widget);
        const auto text = edit ? edit->text().trimmed() : QString{};
        if (text.isEmpty()) {
            if (definition.required) {
                errorMessage = tr("%1 is required").arg(definition.name);
                return false;
            }
            options.remove(definition.key);
        } else {
            options.insert(definition.key, text);
        }
        storeOptions(item, options);
    }

    sharedOptions.remove(QStringLiteral("deviceId"));
    sharedOptions.remove(QStringLiteral("channelIndex"));
    for (auto iterator = channelOptions.begin();
         iterator != channelOptions.end(); ++iterator) {
        iterator.value().remove(QStringLiteral("deviceId"));
    }
    return true;
}

const PluginManifest* StationPropertyEditor::selectedPlugin() const
{
    const auto moduleId = m_pluginCombo->currentData().toString();
    const auto iterator = std::find_if(
        m_plugins.cbegin(), m_plugins.cend(),
        [&moduleId](const PluginManifest& plugin) {
            return plugin.moduleId == moduleId;
        });
    return iterator == m_plugins.cend() ? nullptr : &*iterator;
}

const PluginFunctionDefinition* StationPropertyEditor::connectionFunction(
    const PluginManifest& plugin) const
{
    for (const auto* id : {"open", "connect", "connectCan"}) {
        const auto iterator = std::find_if(
            plugin.functions.cbegin(), plugin.functions.cend(),
            [id](const PluginFunctionDefinition& function) {
                return function.id.compare(QString::fromLatin1(id),
                                           Qt::CaseInsensitive) == 0;
            });
        if (iterator != plugin.functions.cend()) {
            return &*iterator;
        }
    }
    return nullptr;
}

QString StationPropertyEditor::effectiveBaseId() const
{
    const auto type = m_deviceTypeCombo->currentData().toString();
    if (!m_logicalBaseId.isEmpty() && type == m_loadedDeviceType) {
        return m_logicalBaseId;
    }
    return m_document
        ? m_document->nextDeviceIdForType(type, currentDeviceRow())
        : QStringLiteral("%11").arg(type);
}

void StationPropertyEditor::markPendingChanges()
{
    if (!m_loading && m_editable && !m_currentRows.isEmpty()) {
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
        m_title->setText(pending ? tr("Properties *") : tr("Properties"));
    }
    emit pendingChangesChanged(pending);
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
