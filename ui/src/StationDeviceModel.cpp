#include "StationDeviceModel.h"

#include <QBrush>
#include <QColor>
#include <QFileInfo>
#include <QJsonArray>
#include <QRegularExpression>
#include <QSet>

#include <algorithm>

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

QString normalizedType(const QJsonObject& device)
{
    const auto value = valueWithAlias(
        device, QStringLiteral("deviceType"), QStringLiteral("type"))
                           .trimmed().toUpper();
    return value.isEmpty() ? QStringLiteral("PLUGIN") : value;
}

QString deviceId(const QJsonObject& device)
{
    return valueWithAlias(device, QStringLiteral("deviceId"),
                          QStringLiteral("id"))
        .trimmed();
}

QString driverId(const QJsonObject& device)
{
    return valueWithAlias(device, QStringLiteral("driverId"),
                          QStringLiteral("driver"))
        .trimmed();
}

QString canonicalCanBaseId(const QJsonObject& device)
{
    static const QRegularExpression pattern(
        QStringLiteral("^CAN([1-9][0-9]*)(?:\\.CH[1-9][0-9]*)?$"),
        QRegularExpression::CaseInsensitiveOption);
    const auto match = pattern.match(deviceId(device));
    return match.hasMatch()
        ? QStringLiteral("CAN%1").arg(match.captured(1).toInt())
        : QString{};
}

int channelIndex(const QJsonObject& device)
{
    return qMax(0, device.value(QStringLiteral("options"))
                       .toObject()
                       .value(QStringLiteral("channelIndex"))
                       .toInt());
}

QString canGroupKey(const QJsonObject& device, int row)
{
    const auto driver = driverId(device);
    const auto options = device.value(QStringLiteral("options")).toObject();
    const auto hardwareId = options.value(QStringLiteral("hardwareId"))
                                .toVariant().toString().trimmed().toLower();
    const auto serialNumber = options.value(QStringLiteral("serialNumber"))
                                  .toVariant().toString().trimmed().toLower();
    if (driver.isEmpty()) {
        return QStringLiteral("empty:%1").arg(row);
    }
    return QStringLiteral("%1|%2|%3|%4")
        .arg(driver.toLower())
        .arg(options.value(QStringLiteral("deviceIndex")).toInt())
        .arg(hardwareId, serialNumber);
}

} // namespace

StationDeviceModel::StationDeviceModel(StationDocument* document, QObject* parent)
    : QAbstractTableModel(parent)
    , m_document(document)
{
    Q_ASSERT(m_document);
    connect(m_document, &StationDocument::documentChanged,
            this, &StationDeviceModel::rebuild);
    rebuildRows();
}

int StationDeviceModel::rowCount(const QModelIndex& parent) const
{
    return parent.isValid() ? 0 : m_rows.size();
}

int StationDeviceModel::columnCount(const QModelIndex&) const
{
    return ColumnCount;
}

QVariant StationDeviceModel::data(const QModelIndex& index, int role) const
{
    const auto* row = rowAt(index);
    if (!row || row->documentRows.isEmpty() || !m_document) {
        return {};
    }
    const auto device = m_document->deviceAt(row->documentRows.front());
    const auto driver = driverId(device);
    bool anyEnabled = false;
    bool allEnabled = true;
    for (const int documentRow : row->documentRows) {
        const bool enabled = m_document->deviceAt(documentRow)
                                 .value(QStringLiteral("enabled")).toBool(true);
        anyEnabled = anyEnabled || enabled;
        allEnabled = allEnabled && enabled;
    }

    if (role == DocumentRowRole) {
        return row->documentRows.front();
    }
    if (role == DocumentRowsRole) {
        QVariantList rows;
        for (const int documentRow : row->documentRows) {
            rows.push_back(documentRow);
        }
        return rows;
    }
    if (role == DeviceGroupRole) {
        return row->deviceType == QStringLiteral("CAN");
    }
    if (role == DeviceTypeRole) {
        return row->deviceType;
    }
    if (role == DriverIdRole) {
        return driver;
    }
    if (role == LogicalBaseIdRole) {
        return row->baseId;
    }
    if (role == Qt::CheckStateRole && index.column() == EnabledColumn) {
        return allEnabled ? Qt::Checked
                          : anyEnabled ? Qt::PartiallyChecked : Qt::Unchecked;
    }
    if (role == Qt::ForegroundRole && !anyEnabled) {
        return QBrush(QColor(QStringLiteral("#98a2b3")));
    }
    if (role == Qt::ToolTipRole && index.column() == ConnectionColumn) {
        QStringList details;
        for (const int documentRow : row->documentRows) {
            const auto id = deviceId(m_document->deviceAt(documentRow));
            if (!m_connectionDetails.value(id).isEmpty()) {
                details.push_back(QStringLiteral("%1: %2")
                                      .arg(id, m_connectionDetails.value(id)));
            }
        }
        return details.join(QLatin1Char('\n'));
    }
    if (role != Qt::DisplayRole && role != Qt::EditRole &&
        role != Qt::ToolTipRole) {
        return {};
    }

    switch (index.column()) {
    case DeviceIdColumn:
        return logicalId(index);
    case DeviceTypeColumn:
        return row->deviceType;
    case DriverIdColumn:
        return role == Qt::EditRole ? driver : pluginDisplayName(driver);
    case AddressColumn:
        return valueWithAlias(device, QStringLiteral("address"),
                              QStringLiteral("visaAddress"));
    case LifetimeColumn:
        return device.value(QStringLiteral("lifetime"))
            .toString(QStringLiteral("Station"));
    case ConnectionColumn: {
        if (!anyEnabled) {
            return tr("Disabled");
        }
        QStringList states;
        for (const int documentRow : row->documentRows) {
            const auto channel = m_document->deviceAt(documentRow);
            if (!channel.value(QStringLiteral("enabled")).toBool(true)) {
                continue;
            }
            states.push_back(m_connectionStates.value(
                deviceId(channel), tr("Not tested")));
        }
        states.removeDuplicates();
        return states.join(QStringLiteral(" / "));
    }
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
    const auto* row = rowAt(index);
    if (!m_document || !row || index.column() != EnabledColumn ||
        role != Qt::CheckStateRole) {
        return false;
    }
    auto root = m_document->rootObject();
    auto devices = root.value(QStringLiteral("devices")).toArray();
    const bool enabled = value.toInt() == Qt::Checked;
    for (const int documentRow : row->documentRows) {
        auto device = devices[documentRow].toObject();
        device.insert(QStringLiteral("enabled"), enabled);
        devices[documentRow] = device;
    }
    root.insert(QStringLiteral("devices"), devices);
    return m_document->replaceRootObject(std::move(root));
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
    case DriverIdColumn: return tr("Driver / Model");
    case AddressColumn: return tr("Address");
    case LifetimeColumn: return tr("Lifetime");
    case ConnectionColumn: return tr("Connection");
    case EnabledColumn: return tr("Enable");
    default: return {};
    }
}

void StationDeviceModel::setPluginRegistry(QVector<PluginManifest> plugins)
{
    beginResetModel();
    m_plugins = std::move(plugins);
    rebuildRows();
    endResetModel();
}

QJsonObject StationDeviceModel::deviceAt(const QModelIndex& index) const
{
    const auto* row = rowAt(index);
    return row && !row->documentRows.isEmpty() && m_document
        ? m_document->deviceAt(row->documentRows.front())
        : QJsonObject{};
}

int StationDeviceModel::documentRow(const QModelIndex& index) const
{
    const auto* row = rowAt(index);
    return row && !row->documentRows.isEmpty() ? row->documentRows.front() : -1;
}

QVector<int> StationDeviceModel::documentRows(const QModelIndex& index) const
{
    const auto* row = rowAt(index);
    return row ? row->documentRows : QVector<int>{};
}

QModelIndex StationDeviceModel::indexForDocumentRow(int documentRow) const
{
    for (int row = 0; row < m_rows.size(); ++row) {
        if (m_rows[row].documentRows.contains(documentRow)) {
            return index(row, 0);
        }
    }
    return {};
}

bool StationDeviceModel::isDeviceGroup(const QModelIndex& index) const
{
    const auto* row = rowAt(index);
    return row && row->deviceType == QStringLiteral("CAN");
}

QString StationDeviceModel::logicalId(const QModelIndex& index) const
{
    const auto* row = rowAt(index);
    if (!row || !m_document) {
        return {};
    }
    if (row->deviceType != QStringLiteral("CAN")) {
        return row->baseId;
    }
    QStringList ids;
    QVector<QPair<int, int>> channels;
    for (const int documentRow : row->documentRows) {
        const auto device = m_document->deviceAt(documentRow);
        if (device.value(QStringLiteral("enabled")).toBool(true)) {
            channels.push_back({channelIndex(device), documentRow});
        }
    }
    std::sort(channels.begin(), channels.end());
    for (const auto& channel : channels) {
        ids.push_back(QStringLiteral("%1.CH%2")
                          .arg(row->baseId)
                          .arg(channel.first + 1));
    }
    return ids.isEmpty() ? tr("%1 (No active channel)").arg(row->baseId)
                         : ids.join(QStringLiteral(" / "));
}

QString StationDeviceModel::logicalBaseId(const QModelIndex& index) const
{
    const auto* row = rowAt(index);
    return row ? row->baseId : QString{};
}

QString StationDeviceModel::generatedLogicalId(const QModelIndex& index,
                                                int documentRow) const
{
    const auto* row = rowAt(index);
    if (!row || !row->documentRows.contains(documentRow) || !m_document) {
        return {};
    }
    return row->deviceType == QStringLiteral("CAN")
        ? QStringLiteral("%1.CH%2")
              .arg(row->baseId)
              .arg(channelIndex(m_document->deviceAt(documentRow)) + 1)
        : row->baseId;
}

QVector<PluginManifest> StationDeviceModel::pluginsForType(
    const QString& deviceType) const
{
    QVector<PluginManifest> result;
    const auto type = deviceType.trimmed().toUpper();
    for (const auto& plugin : m_plugins) {
        if (plugin.category.trimmed().toUpper() == type) {
            result.push_back(plugin);
        }
    }
    return result;
}

void StationDeviceModel::markConnectionTesting(const QString& id)
{
    m_connectionStates.insert(id, tr("Testing"));
    m_connectionDetails.insert(id, tr("Opening device and checking health"));
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
            : QStringLiteral("%1: %2").arg(result.errorCode, detail);
    }
    if (result.elapsedMs > 0) {
        detail += QStringLiteral(" (%1 ms)").arg(result.elapsedMs);
    }
    m_connectionDetails.insert(result.deviceId, detail.trimmed());
    rebuild();
}

void StationDeviceModel::clearConnectionStatus(const QString& id)
{
    if (id.isEmpty()) {
        m_connectionStates.clear();
        m_connectionDetails.clear();
    } else {
        m_connectionStates.remove(id);
        m_connectionDetails.remove(id);
    }
    rebuild();
}

const StationDeviceModel::DeviceRow* StationDeviceModel::rowAt(
    const QModelIndex& index) const
{
    return index.isValid() && index.row() >= 0 && index.row() < m_rows.size()
        ? &m_rows[index.row()]
        : nullptr;
}

QString StationDeviceModel::pluginDisplayName(const QString& id) const
{
    return id;
}

void StationDeviceModel::rebuildRows()
{
    m_rows.clear();
    if (!m_document) {
        return;
    }
    QHash<QString, QVector<int>> canGroups;
    QHash<QString, int> typeCounts;
    QSet<QString> reservedCanBaseIds;
    QSet<QString> usedCanBaseIds;
    int nextCanIndex = 1;
    for (int documentRow = 0;
         documentRow < m_document->deviceCount(); ++documentRow) {
        const auto device = m_document->deviceAt(documentRow);
        if (normalizedType(device) != QStringLiteral("CAN")) {
            continue;
        }
        const auto baseId = canonicalCanBaseId(device);
        if (!baseId.isEmpty()) {
            reservedCanBaseIds.insert(baseId);
        }
    }
    for (int documentRow = 0;
         documentRow < m_document->deviceCount(); ++documentRow) {
        const auto device = m_document->deviceAt(documentRow);
        const auto type = normalizedType(device);
        if (type == QStringLiteral("CAN")) {
            const auto key = canGroupKey(device, documentRow);
            const int currentChannel = channelIndex(device);
            int matchingGroup = -1;
            const auto candidates = canGroups.value(key);
            for (auto iterator = candidates.crbegin();
                 iterator != candidates.crend(); ++iterator) {
                bool channelAlreadyUsed = false;
                for (const int existingRow : m_rows[*iterator].documentRows) {
                    channelAlreadyUsed = channelAlreadyUsed ||
                        channelIndex(m_document->deviceAt(existingRow)) ==
                        currentChannel;
                }
                if (!channelAlreadyUsed) {
                    matchingGroup = *iterator;
                    break;
                }
            }
            if (matchingGroup >= 0) {
                m_rows[matchingGroup].documentRows.push_back(documentRow);
                continue;
            }
            DeviceRow row;
            row.documentRows.push_back(documentRow);
            row.deviceType = type;
            row.baseId = canonicalCanBaseId(device);
            if (row.baseId.isEmpty() || usedCanBaseIds.contains(row.baseId)) {
                QString candidate;
                do {
                    candidate = QStringLiteral("CAN%1").arg(nextCanIndex++);
                } while (reservedCanBaseIds.contains(candidate) ||
                         usedCanBaseIds.contains(candidate));
                row.baseId = std::move(candidate);
            }
            usedCanBaseIds.insert(row.baseId);
            canGroups[key].push_back(m_rows.size());
            m_rows.push_back(std::move(row));
            continue;
        }
        DeviceRow row;
        row.documentRows.push_back(documentRow);
        row.deviceType = type;
        row.baseId = QStringLiteral("%1%2").arg(type).arg(++typeCounts[type]);
        m_rows.push_back(std::move(row));
    }
    for (auto& row : m_rows) {
        if (row.deviceType != QStringLiteral("CAN")) {
            continue;
        }
        std::sort(row.documentRows.begin(), row.documentRows.end(),
                  [this](int left, int right) {
                      return channelIndex(m_document->deviceAt(left)) <
                             channelIndex(m_document->deviceAt(right));
                  });
    }
}

void StationDeviceModel::rebuild()
{
    beginResetModel();
    rebuildRows();
    endResetModel();
}

} // namespace PicoATE::Ui
