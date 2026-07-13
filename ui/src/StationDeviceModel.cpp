#include "StationDeviceModel.h"

#include <QBrush>
#include <QColor>

namespace PicoATE::Ui {

namespace {

QString valueWithAlias(const QJsonObject& object,
                       const QString& key,
                       const QString& alias = {})
{
    const auto value = object.value(key).toString();
    return value.isEmpty() && !alias.isEmpty()
        ? object.value(alias).toString()
        : value;
}

} // namespace

StationDeviceModel::StationDeviceModel(StationDocument* document, QObject* parent)
    : QAbstractTableModel(parent)
    , m_document(document)
{
    Q_ASSERT(m_document);
    connect(m_document, &StationDocument::documentChanged,
            this, &StationDeviceModel::rebuild);
}

int StationDeviceModel::rowCount(const QModelIndex& parent) const
{
    return parent.isValid() || !m_document ? 0 : m_document->deviceCount();
}

int StationDeviceModel::columnCount(const QModelIndex&) const
{
    return ColumnCount;
}

QVariant StationDeviceModel::data(const QModelIndex& index, int role) const
{
    if (!m_document || !index.isValid() || index.row() >= rowCount()) {
        return {};
    }
    const auto device = m_document->deviceAt(index.row());
    const auto deviceId = valueWithAlias(device, "deviceId", "id");
    if (role == Qt::CheckStateRole && index.column() == EnabledColumn) {
        return device.value("enabled").toBool(true) ? Qt::Checked : Qt::Unchecked;
    }
    const bool enabled = device.value("enabled").toBool(true);
    if (role == Qt::ForegroundRole && !enabled) {
        return QBrush(QColor(QStringLiteral("#98a2b3")));
    }
    if (role == Qt::ToolTipRole && index.column() == ConnectionColumn) {
        return m_connectionDetails.value(deviceId);
    }
    if (role != Qt::DisplayRole && role != Qt::ToolTipRole) {
        return {};
    }

    switch (index.column()) {
    case DeviceIdColumn:
        return valueWithAlias(device, "deviceId", "id");
    case DeviceTypeColumn:
        return valueWithAlias(device, "deviceType", "type");
    case DriverIdColumn:
        return valueWithAlias(device, "driverId", "driver");
    case AddressColumn:
        return valueWithAlias(device, "address", "visaAddress");
    case LifetimeColumn:
        return device.value("lifetime").toString("Station");
    case ConnectionColumn:
        return enabled ? m_connectionStates.value(deviceId, tr("Not tested"))
                       : tr("Disabled");
    case EnabledColumn:
        return {};
    default:
        return {};
    }
}

bool StationDeviceModel::setData(const QModelIndex& index,
                                 const QVariant& value,
                                 int role)
{
    if (!m_document || !index.isValid() || index.column() != EnabledColumn ||
        role != Qt::CheckStateRole) {
        return false;
    }
    return m_document->setDeviceValue(
        index.row(), "enabled", value.toInt() == Qt::Checked);
}

Qt::ItemFlags StationDeviceModel::flags(const QModelIndex& index) const
{
    if (!index.isValid()) {
        return Qt::NoItemFlags;
    }
    auto result = QAbstractTableModel::flags(index);
    if (index.column() == EnabledColumn) {
        result |= Qt::ItemIsUserCheckable;
    }
    return result;
}

QVariant StationDeviceModel::headerData(int section,
                                        Qt::Orientation orientation,
                                        int role) const
{
    if (orientation != Qt::Horizontal || role != Qt::DisplayRole) {
        return {};
    }
    switch (section) {
    case DeviceIdColumn: return tr("Logical ID");
    case DeviceTypeColumn: return tr("Type");
    case DriverIdColumn: return tr("Driver");
    case AddressColumn: return tr("Address");
    case LifetimeColumn: return tr("Lifetime");
    case ConnectionColumn: return tr("Connection");
    case EnabledColumn: return tr("Enable");
    default: return {};
    }
}

QJsonObject StationDeviceModel::deviceAt(int row) const
{
    return m_document ? m_document->deviceAt(row) : QJsonObject{};
}

void StationDeviceModel::markConnectionTesting(const QString& deviceId)
{
    m_connectionStates.insert(deviceId, tr("Testing"));
    m_connectionDetails.insert(deviceId, tr("Opening device and checking health"));
    rebuild();
}

void StationDeviceModel::setConnectionTestResult(
    const DeviceConnectionTestResult& result)
{
    m_connectionStates.insert(
        result.deviceId, deviceConnectionTestOutcomeName(result.outcome));
    QString detail = result.errorMessage;
    if (!result.errorCode.isEmpty()) {
        detail = detail.isEmpty()
            ? result.errorCode
            : QString("%1: %2").arg(result.errorCode, detail);
    }
    if (result.elapsedMs > 0) {
        detail += QString(" (%1 ms)").arg(result.elapsedMs);
    }
    m_connectionDetails.insert(result.deviceId, detail.trimmed());
    rebuild();
}

void StationDeviceModel::clearConnectionStatus(const QString& deviceId)
{
    if (deviceId.isEmpty()) {
        m_connectionStates.clear();
        m_connectionDetails.clear();
    } else {
        m_connectionStates.remove(deviceId);
        m_connectionDetails.remove(deviceId);
    }
    rebuild();
}

void StationDeviceModel::rebuild()
{
    beginResetModel();
    endResetModel();
}

} // namespace PicoATE::Ui
