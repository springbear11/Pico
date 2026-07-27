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
#include <QThread>
#include <QTimer>
#include <QVBoxLayout>

#include <algorithm>
#include <memory>

namespace PicoATE::Ui {

namespace {

enum ConnectionKind {
    CanConnection,
    SerialConnection,
    VisaConnection,
    IpConnection
};

ConnectionKind defaultConnectionKind(const PicoATE::Core::StationFieldDevice& device)
{
    if (device.deviceType == QStringLiteral("CAN")) return CanConnection;
    if (device.deviceType == QStringLiteral("SERIAL") ||
        device.deviceType == QStringLiteral("MODBUS")) return SerialConnection;
    if (device.deviceType == QStringLiteral("DMM") ||
        device.deviceType == QStringLiteral("PSU") ||
        device.deviceType == QStringLiteral("SCOPE") ||
        device.address.contains(QStringLiteral("::"))) return VisaConnection;
    return IpConnection;
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
    loadStation();
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
    m_connectionKind->addItem(tr("CAN Serial Number"), CanConnection);
    m_connectionKind->addItem(tr("COM Port"), SerialConnection);
    m_connectionKind->addItem(tr("VISA Resource"), VisaConnection);
    m_connectionKind->addItem(tr("IP / Address"), IpConnection);
    m_resourceCombo = new QComboBox(editor);
    m_resourceCombo->setObjectName(QStringLiteral("fieldResourceCombo"));
    m_manualAddress = new QLineEdit(editor);
    m_manualAddress->setObjectName(QStringLiteral("fieldManualAddress"));
    m_manualAddress->setPlaceholderText(tr("Enter IP address, host:port, or resource address"));
    form->addRow(tr("Connection"), m_connectionKind);
    form->addRow(tr("Detected Resource"), m_resourceCombo);
    form->addRow(tr("Address"), m_manualAddress);
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
        if (isVisible() && !m_busy && m_selectedRow >= 0 &&
            m_connectionKind->currentData().toInt() != CanConnection &&
            m_connectionKind->currentData().toInt() != IpConnection) {
            refreshResources();
        }
    });
    m_refreshTimer->start();

    setStyleSheet(QStringLiteral(R"css(
        QDialog#fieldDeviceDialog { background: #f5f7f9; color: #26333d; }
        QLabel#fieldDeviceTitle { font-size: 17px; font-weight: 700; padding: 4px; }
        QListWidget#fieldDeviceList, QComboBox, QLineEdit {
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
        const auto binding = device.deviceType == QStringLiteral("CAN")
            ? device.serialNumber : device.address;
        auto* item = new QListWidgetItem(
            QStringLiteral("%1\n%2  |  %3")
                .arg(device.logicalId,
                     device.driverId.isEmpty() ? tr("No driver") : device.driverId,
                     binding.isEmpty() ? tr("Not configured") : binding),
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
    const auto kind = defaultConnectionKind(device);
    const auto comboIndex = m_connectionKind->findData(kind);
    m_connectionKind->setCurrentIndex(comboIndex);
    m_connectionKind->setEnabled(device.deviceType != QStringLiteral("CAN"));
    m_manualAddress->setText(device.deviceType == QStringLiteral("CAN")
                                 ? device.serialNumber : device.address);
    updateEditor();
    refreshResources();
}

void FieldDeviceDialog::updateEditor()
{
    const bool selected = m_selectedRow >= 0 && m_selectedRow < m_devices.size();
    const auto kind = static_cast<ConnectionKind>(m_connectionKind->currentData().toInt());
    const bool manual = kind == IpConnection;
    m_resourceCombo->setVisible(selected && !manual);
    m_manualAddress->setVisible(selected && manual);
    m_refreshButton->setVisible(selected && !manual);
    m_refreshButton->setText(kind == CanConnection ? tr("Scan Devices") : tr("Refresh"));
    m_applyButton->setEnabled(selected && !m_busy);
}

void FieldDeviceDialog::refreshResources()
{
    if (m_busy || m_selectedRow < 0 || m_selectedRow >= m_devices.size()) return;
    const auto kind = static_cast<ConnectionKind>(m_connectionKind->currentData().toInt());
    if (kind == IpConnection) return;
    if (kind == CanConnection) {
        scanCanDevices();
        return;
    }
    PicoATE::Core::DeviceDiscoveryRequest request;
    request.kind = kind == SerialConnection
        ? PicoATE::Core::DeviceDiscoveryKind::SerialPort
        : PicoATE::Core::DeviceDiscoveryKind::VisaResource;
    startDiscovery(request, false);
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
    startDiscovery(request, false);
}

void FieldDeviceDialog::startDiscovery(
    const PicoATE::Core::DeviceDiscoveryRequest& request,
    bool effectiveStationScan)
{
    Q_UNUSED(effectiveStationScan);
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
    const auto previous = m_selectedRow >= 0 && m_selectedRow < m_devices.size()
        ? (m_devices[m_selectedRow].deviceType == QStringLiteral("CAN")
               ? m_devices[m_selectedRow].serialNumber : m_devices[m_selectedRow].address)
        : QString();
    m_resourceCombo->clear();
    for (const auto& resource : m_resources) {
        m_resourceCombo->addItem(resource.displayName, resource.resourceId);
    }
    const auto previousIndex = m_resourceCombo->findData(previous);
    if (previousIndex >= 0) m_resourceCombo->setCurrentIndex(previousIndex);
    m_statusLabel->setText(result.ok()
        ? tr("Found %1 resource(s)").arg(result.resources.size())
        : tr("Scan failed: %1").arg(result.errorMessage));
    updateEditor();
}

void FieldDeviceDialog::applyAndSave()
{
    if (m_selectedRow < 0 || m_selectedRow >= m_devices.size()) return;
    const auto kind = static_cast<ConnectionKind>(m_connectionKind->currentData().toInt());
    const auto resource = kind == IpConnection
        ? m_manualAddress->text().trimmed()
        : m_resourceCombo->currentData().toString().trimmed();
    if (resource.isEmpty()) {
        QMessageBox::warning(this, tr("Field Device Configuration"),
                             tr("Select or enter a connection resource first."));
        return;
    }

    QString error;
    if (!PicoATE::Core::applyStationFieldBinding(
            m_station, m_devices[m_selectedRow].logicalId, resource, &error)) {
        QMessageBox::critical(this, tr("Field Device Configuration"), error);
        return;
    }
    m_station = PicoATE::Core::effectiveStationSnapshot(m_station, {});
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

void FieldDeviceDialog::resolveEffectiveStation(bool requireCanBindings)
{
    QString error;
    if (!loadStation(&error)) {
        emit effectiveStationReady({}, tr("Cannot load StationSystem: %1").arg(error));
        return;
    }
    const auto station = m_station;
    const auto devices = PicoATE::Core::stationFieldDevices(station);
    struct ScanTarget { QString driverId; QString dll; QVariantMap options; };
    QVector<ScanTarget> targets;
    for (const auto& device : devices) {
        if (device.deviceType != QStringLiteral("CAN") || device.serialNumber.isEmpty()) continue;
        const auto duplicate = std::any_of(targets.cbegin(), targets.cend(),
            [&device](const auto& target) { return target.driverId == device.driverId; });
        if (!duplicate) targets.push_back({device.driverId, pluginDllPath(device.driverId), {}});
    }

    const auto host = nativeHostProgram();
    const QPointer<FieldDeviceDialog> guard(this);
    auto* thread = QThread::create([guard, station, devices, targets, host, requireCanBindings] {
        QHash<QString, QHash<QString, int>> indicesByDriver;
        QString failure;
        PicoATE::Core::SystemDeviceDiscoveryService service;
        for (const auto& target : targets) {
            PicoATE::Core::DeviceDiscoveryRequest request;
            request.kind = PicoATE::Core::DeviceDiscoveryKind::CanDevice;
            request.driverId = target.driverId;
            request.pluginDllPath = target.dll;
            request.nativeHostProgram = host;
            request.options = target.options;
            request.timeoutMs = 12000;
            const auto found = service.discover(request);
            if (!found.ok()) {
                failure = QStringLiteral("%1: %2").arg(target.driverId, found.errorMessage);
                break;
            }
            for (const auto& resource : found.resources) {
                indicesByDriver[target.driverId].insert(
                    resource.serialNumber,
                    resource.runtimeLocator.value(QStringLiteral("deviceIndex")).toInt());
            }
        }
        QHash<QString, int> effectiveIndices;
        if (failure.isEmpty()) {
            for (const auto& device : devices) {
                if (device.deviceType != QStringLiteral("CAN")) continue;
                if (device.serialNumber.isEmpty()) {
                    if (requireCanBindings) {
                        failure = QStringLiteral("%1: select a CAN serial number in Devices before running")
                                      .arg(device.logicalId);
                        break;
                    }
                    continue;
                }
                const auto driver = indicesByDriver.constFind(device.driverId);
                if (driver == indicesByDriver.constEnd() || !driver->contains(device.serialNumber)) {
                    failure = QStringLiteral("%1: configured CAN serial number %2 was not found")
                                  .arg(device.logicalId, device.serialNumber);
                    break;
                }
                effectiveIndices.insert(device.logicalId, driver->value(device.serialNumber));
            }
        }
        const auto snapshot = failure.isEmpty()
            ? QJsonDocument(PicoATE::Core::effectiveStationSnapshot(station, effectiveIndices))
                  .toJson(QJsonDocument::Compact)
            : QByteArray();
        if (guard) {
            QMetaObject::invokeMethod(guard, [guard, snapshot, failure] {
                if (guard) emit guard->effectiveStationReady(snapshot, failure);
            }, Qt::QueuedConnection);
        }
    });
    connect(thread, &QThread::finished, thread, &QObject::deleteLater);
    thread->start();
}

} // namespace PicoATE::Ui
