#include "FieldDeviceDialog.h"

#include <QApplication>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPointer>
#include <QPushButton>
#include <QSaveFile>
#include <QSignalBlocker>
#include <QThread>
#include <QTimer>
#include <QVBoxLayout>

#include <algorithm>
#include <memory>

namespace PicoATE::Ui {

namespace {

QString connectionKindLabel(const QString& kind)
{
    if (kind == QStringLiteral("canSerial")) return QObject::tr("CAN Serial Number");
    if (kind == QStringLiteral("visa")) return QObject::tr("VISA Resource");
    if (kind == QStringLiteral("serialPort")) return QObject::tr("COM Port");
    if (kind == QStringLiteral("tcpIp")) return QObject::tr("TCP / IP");
    return QObject::tr("Manual Resource");
}

QString registryPath(const QJsonObject& station, const QString& stationPath)
{
    const auto configured = station.value(QStringLiteral("pluginRegistry"))
                                .toString(QStringLiteral("plugins/PluginRegistry.json"));
    if (QFileInfo(configured).isAbsolute()) {
        return QFileInfo(configured).absoluteFilePath();
    }
    return QFileInfo(QFileInfo(stationPath).absoluteDir().absoluteFilePath(configured))
        .absoluteFilePath();
}

} // namespace

FieldDeviceDialog::FieldDeviceDialog(QString stationPath, QWidget* parent)
    : QDialog(parent)
    , m_stationPath(QFileInfo(std::move(stationPath)).absoluteFilePath())
{
    setObjectName(QStringLiteral("fieldDeviceDialog"));
    setWindowTitle(tr("Field Device Configuration"));
    setMinimumSize(760, 470);
    buildUi();
    if (loadStation()) {
        const auto registry = PluginCatalog::loadRegistry(
            registryPath(m_station, m_stationPath));
        m_plugins = registry.plugins;
    }
    reloadDevices();
}

void FieldDeviceDialog::buildUi()
{
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(18, 16, 18, 16);
    root->setSpacing(12);

    auto* title = new QLabel(tr("Configure station connection resources"), this);
    title->setObjectName(QStringLiteral("fieldDeviceTitle"));
    root->addWidget(title);

    auto* body = new QHBoxLayout;
    body->setSpacing(14);
    m_deviceList = new QListWidget(this);
    m_deviceList->setObjectName(QStringLiteral("fieldDeviceList"));
    m_deviceList->setMinimumWidth(250);
    body->addWidget(m_deviceList, 1);

    auto* editor = new QWidget(this);
    auto* editorLayout = new QVBoxLayout(editor);
    editorLayout->setContentsMargins(8, 6, 8, 6);
    auto* form = new QFormLayout;
    form->setLabelAlignment(Qt::AlignRight | Qt::AlignVCenter);
    m_connectionKind = new QComboBox(editor);
    m_connectionKind->setObjectName(QStringLiteral("fieldConnectionKind"));
    m_resourceCombo = new QComboBox(editor);
    m_resourceCombo->setObjectName(QStringLiteral("fieldResourceCombo"));
    m_resourceCombo->setEditable(true);
    m_resourceCombo->setInsertPolicy(QComboBox::NoInsert);
    form->addRow(tr("Connection"), m_connectionKind);
    form->addRow(tr("Resource"), m_resourceCombo);
    editorLayout->addLayout(form);

    auto* actions = new QHBoxLayout;
    m_refreshButton = new QPushButton(tr("Refresh"), editor);
    m_refreshButton->setObjectName(QStringLiteral("fieldRefreshButton"));
    actions->addWidget(m_refreshButton);
    actions->addStretch(1);
    editorLayout->addLayout(actions);
    m_statusLabel = new QLabel(editor);
    m_statusLabel->setObjectName(QStringLiteral("fieldDeviceStatus"));
    m_statusLabel->setWordWrap(true);
    editorLayout->addWidget(m_statusLabel);
    editorLayout->addStretch(1);
    body->addWidget(editor, 2);
    root->addLayout(body, 1);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
    m_applyButton = buttons->addButton(tr("Apply and Save"), QDialogButtonBox::AcceptRole);
    m_applyButton->setObjectName(QStringLiteral("fieldApplyButton"));
    root->addWidget(buttons);

    connect(m_deviceList, &QListWidget::currentRowChanged,
            this, &FieldDeviceDialog::selectDevice);
    connect(m_connectionKind, &QComboBox::currentIndexChanged,
            this, [this] { updateEditor(); refreshResources(); });
    connect(m_refreshButton, &QPushButton::clicked,
            this, &FieldDeviceDialog::refreshResources);
    connect(m_applyButton, &QPushButton::clicked,
            this, &FieldDeviceDialog::applyAndSave);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    m_refreshTimer = new QTimer(this);
    m_refreshTimer->setInterval(3000);
    connect(m_refreshTimer, &QTimer::timeout, this, [this] {
        const auto parsed = PicoATE::Core::deviceConnectionKindFromString(
            m_connectionKind->currentData().toString());
        if (isVisible() && !m_busy && m_selectedRow >= 0 && parsed &&
            PicoATE::Core::discoveryKindForConnection(*parsed).has_value() &&
            *parsed != PicoATE::Core::DeviceConnectionKind::CanSerial) {
            refreshResources();
        }
    });
    m_refreshTimer->start();

    setStyleSheet(QStringLiteral(R"css(
        QDialog#fieldDeviceDialog { background: #f5f7f9; color: #26333d; }
        QLabel#fieldDeviceTitle { font-size: 17px; font-weight: 700; padding: 4px; }
        QListWidget#fieldDeviceList, QComboBox {
            background: white; border: 1px solid #c8d0d7; border-radius: 4px;
            min-height: 30px; padding: 3px 7px;
        }
        QListWidget#fieldDeviceList::item { min-height: 48px; padding: 6px 9px; }
        QListWidget#fieldDeviceList::item:selected { background: #cfe4f3; color: #1e2b34; }
        QLabel#fieldDeviceStatus { color: #52636f; padding: 8px 2px; }
        QPushButton { min-height: 30px; padding: 3px 12px; }
    )css"));
}

bool FieldDeviceDialog::loadStation(QString* errorMessage)
{
    QFile file(m_stationPath);
    if (!file.open(QIODevice::ReadOnly)) {
        if (errorMessage) *errorMessage = file.errorString();
        return false;
    }
    QJsonParseError parseError;
    const auto document = QJsonDocument::fromJson(file.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        if (errorMessage) *errorMessage = parseError.errorString();
        return false;
    }
    m_station = document.object();
    return true;
}

void FieldDeviceDialog::reloadDevices()
{
    const auto currentId = m_selectedRow >= 0 && m_selectedRow < m_devices.size()
        ? m_devices[m_selectedRow].logicalId : QString();
    m_devices = PicoATE::Core::stationFieldDevices(m_station);
    m_deviceList->clear();
    int selection = -1;
    for (int index = 0; index < m_devices.size(); ++index) {
        const auto& device = m_devices[index];
        auto* item = new QListWidgetItem(
            QStringLiteral("%1\n%2  |  %3")
                .arg(device.logicalId,
                     device.driverId.isEmpty() ? tr("No driver") : device.driverId,
                     device.resource.isEmpty() ? tr("Not configured") : device.resource),
            m_deviceList);
        item->setData(Qt::UserRole, device.logicalId);
        if (device.logicalId == currentId) selection = index;
    }
    if (selection < 0 && !m_devices.isEmpty()) selection = 0;
    m_deviceList->setCurrentRow(selection);
}

void FieldDeviceDialog::selectDevice(int row)
{
    m_selectedRow = row;
    m_resources.clear();
    m_resourceCombo->clear();
    if (row < 0 || row >= m_devices.size()) {
        updateEditor();
        return;
    }
    const auto& device = m_devices[row];
    reloadConnectionKinds();
    const auto comboIndex = m_connectionKind->findData(device.connectionKind);
    m_connectionKind->setCurrentIndex(comboIndex >= 0 ? comboIndex : 0);
    m_resourceCombo->setEditText(device.resource);
    updateEditor();
    refreshResources();
}

void FieldDeviceDialog::reloadConnectionKinds()
{
    if (m_selectedRow < 0 || m_selectedRow >= m_devices.size()) return;
    const auto& device = m_devices[m_selectedRow];
    QStringList kinds;
    const auto plugin = std::find_if(
        m_plugins.cbegin(), m_plugins.cend(),
        [&device](const PluginManifest& candidate) {
            return candidate.moduleId == device.driverId;
        });
    if (plugin != m_plugins.cend()) kinds = plugin->connectionKinds;
    if (kinds.isEmpty()) kinds = {device.connectionKind};
    const QSignalBlocker blocker(m_connectionKind);
    m_connectionKind->clear();
    for (const auto& value : kinds) {
        const auto parsed = PicoATE::Core::deviceConnectionKindFromString(value);
        if (!parsed) continue;
        const auto canonical = PicoATE::Core::deviceConnectionKindName(*parsed);
        if (m_connectionKind->findData(canonical) < 0) {
            m_connectionKind->addItem(connectionKindLabel(canonical), canonical);
        }
    }
    if (m_connectionKind->count() == 0) {
        m_connectionKind->addItem(connectionKindLabel(QStringLiteral("manual")),
                                  QStringLiteral("manual"));
    }
}

void FieldDeviceDialog::updateEditor()
{
    const bool selected = m_selectedRow >= 0 && m_selectedRow < m_devices.size();
    const auto parsed = PicoATE::Core::deviceConnectionKindFromString(
        m_connectionKind->currentData().toString());
    const bool enumerable = parsed &&
        PicoATE::Core::discoveryKindForConnection(*parsed).has_value();
    m_resourceCombo->setVisible(selected);
    m_refreshButton->setVisible(selected && enumerable);
    m_refreshButton->setText(parsed &&
        *parsed == PicoATE::Core::DeviceConnectionKind::CanSerial
            ? tr("Scan Devices") : tr("Refresh"));
    if (auto* edit = m_resourceCombo->lineEdit()) {
        const auto kind = m_connectionKind->currentData().toString();
        if (kind == QStringLiteral("canSerial"))
            edit->setPlaceholderText(tr("Select or enter the device serial number"));
        else if (kind == QStringLiteral("visa"))
            edit->setPlaceholderText(tr("Select or enter a VISA resource"));
        else if (kind == QStringLiteral("serialPort"))
            edit->setPlaceholderText(tr("Select or enter a COM port"));
        else if (kind == QStringLiteral("tcpIp"))
            edit->setPlaceholderText(tr("Enter an IP address or host:port"));
        else
            edit->setPlaceholderText(tr("Enter a connection resource"));
    }
    m_applyButton->setEnabled(selected && !m_busy);
}

void FieldDeviceDialog::refreshResources()
{
    if (m_busy || m_selectedRow < 0 || m_selectedRow >= m_devices.size()) return;
    const auto kind = PicoATE::Core::deviceConnectionKindFromString(
        m_connectionKind->currentData().toString());
    if (!kind) return;
    const auto discoveryKind = PicoATE::Core::discoveryKindForConnection(*kind);
    if (!discoveryKind) return;
    if (*kind == PicoATE::Core::DeviceConnectionKind::CanSerial) {
        scanCanDevices();
        return;
    }
    PicoATE::Core::DeviceDiscoveryRequest request;
    request.kind = *discoveryKind;
    startDiscovery(request);
}

void FieldDeviceDialog::scanCanDevices()
{
    const auto& device = m_devices[m_selectedRow];
    PicoATE::Core::DeviceDiscoveryRequest request;
    request.kind = PicoATE::Core::DeviceDiscoveryKind::CanDevice;
    request.driverId = device.driverId;
    request.pluginDllPath = pluginDllPath(device.driverId);
    request.nativeHostProgram = nativeHostProgram();
    request.timeoutMs = 12000;
    startDiscovery(request);
}

void FieldDeviceDialog::startDiscovery(
    const PicoATE::Core::DeviceDiscoveryRequest& request)
{
    m_busy = true;
    updateEditor();
    m_statusLabel->setText(tr("Scanning resources..."));
    m_refreshButton->setEnabled(false);
    const QPointer<FieldDeviceDialog> guard(this);
    auto* thread = QThread::create([guard, request] {
        PicoATE::Core::SystemDeviceDiscoveryService service;
        const auto result = service.discover(request);
        if (guard) {
            QMetaObject::invokeMethod(guard, [guard, result] {
                if (guard) guard->finishResourceDiscovery(result);
            }, Qt::QueuedConnection);
        }
    });
    connect(thread, &QThread::finished, thread, &QObject::deleteLater);
    thread->start();
}

void FieldDeviceDialog::finishResourceDiscovery(
    const PicoATE::Core::DeviceDiscoveryResult& result)
{
    m_busy = false;
    m_refreshButton->setEnabled(true);
    m_resources = result.resources;
    const auto previous = m_resourceCombo->currentText().trimmed();
    m_resourceCombo->clear();
    for (const auto& resource : m_resources) {
        m_resourceCombo->addItem(resource.displayName, resource.resourceId);
    }
    const auto previousIndex = m_resourceCombo->findData(previous);
    if (previousIndex >= 0) m_resourceCombo->setCurrentIndex(previousIndex);
    else m_resourceCombo->setEditText(previous);
    m_statusLabel->setText(result.ok()
        ? tr("Found %1 resource(s)").arg(result.resources.size())
        : tr("Scan failed: %1").arg(result.errorMessage));
    updateEditor();
}

void FieldDeviceDialog::applyAndSave()
{
    if (m_selectedRow < 0 || m_selectedRow >= m_devices.size()) return;
    const auto kind = m_connectionKind->currentData().toString();
    const bool selectedEnumeratedItem = m_resourceCombo->currentIndex() >= 0 &&
        m_resourceCombo->currentText() ==
            m_resourceCombo->itemText(m_resourceCombo->currentIndex());
    const auto resource = selectedEnumeratedItem
        ? m_resourceCombo->currentData().toString().trimmed()
        : m_resourceCombo->currentText().trimmed();
    if (resource.isEmpty()) {
        QMessageBox::warning(this, tr("Field Device Configuration"),
                             tr("Select or enter a connection resource first."));
        return;
    }

    QString error;
    if (!PicoATE::Core::applyStationFieldBinding(
            m_station, m_devices[m_selectedRow].logicalId,
            kind, resource, &error)) {
        QMessageBox::critical(this, tr("Field Device Configuration"), error);
        return;
    }
    QSaveFile file(m_stationPath);
    if (!file.open(QIODevice::WriteOnly) ||
        file.write(QJsonDocument(m_station).toJson(QJsonDocument::Indented)) < 0 ||
        !file.commit()) {
        QMessageBox::critical(this, tr("Field Device Configuration"), file.errorString());
        return;
    }
    m_statusLabel->setText(tr("Saved %1 = %2")
                               .arg(m_devices[m_selectedRow].logicalId, resource));
    reloadDevices();
    emit stationSaved();
}

QString FieldDeviceDialog::pluginDllPath(const QString& driverId) const
{
    const auto path = registryPath(m_station, m_stationPath);
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) return {};
    const auto root = QJsonDocument::fromJson(file.readAll()).object();
    for (const auto& value : root.value(QStringLiteral("plugins")).toArray()) {
        const auto plugin = value.toObject();
        if (plugin.value(QStringLiteral("moduleId")).toString() != driverId) continue;
        const auto dll = plugin.value(QStringLiteral("dll")).toString();
        return QFileInfo(dll).isAbsolute()
            ? QFileInfo(dll).absoluteFilePath()
            : QFileInfo(QFileInfo(path).absoluteDir().absoluteFilePath(dll)).absoluteFilePath();
    }
    return {};
}

QString FieldDeviceDialog::nativeHostProgram() const
{
    const auto local = QDir(QCoreApplication::applicationDirPath())
                           .absoluteFilePath(QStringLiteral("PicoATE.NativeHost.exe"));
    return QFileInfo::exists(local) ? QFileInfo(local).absoluteFilePath() : QString();
}

} // namespace PicoATE::Ui
