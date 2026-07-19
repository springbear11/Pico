#pragma once

#include "PluginCatalog.h"
#include "StationDocument.h"

#include <QAbstractTableModel>
#include <QHash>
#include <QPointer>

namespace PicoATE::Ui {

class StationDeviceModel final : public QAbstractTableModel
{
    Q_OBJECT

public:
    enum Column {
        DeviceIdColumn,
        DeviceTypeColumn,
        DriverIdColumn,
        AddressColumn,
        LifetimeColumn,
        ConnectionColumn,
        EnabledColumn,
        ColumnCount
    };

    enum Role {
        DocumentRowRole = Qt::UserRole + 1,
        DocumentRowsRole,
        DeviceGroupRole,
        DeviceTypeRole,
        DriverIdRole,
        LogicalBaseIdRole
    };

    explicit StationDeviceModel(StationDocument* document,
                                QObject* parent = nullptr);

    int rowCount(const QModelIndex& parent = {}) const override;
    int columnCount(const QModelIndex& parent = {}) const override;
    QVariant data(const QModelIndex& index,
                  int role = Qt::DisplayRole) const override;
    bool setData(const QModelIndex& index,
                 const QVariant& value,
                 int role = Qt::EditRole) override;
    Qt::ItemFlags flags(const QModelIndex& index) const override;
    QVariant headerData(int section,
                        Qt::Orientation orientation,
                        int role = Qt::DisplayRole) const override;

    void setPluginRegistry(QVector<PluginManifest> plugins);
    QJsonObject deviceAt(const QModelIndex& index) const;
    int documentRow(const QModelIndex& index) const;
    QVector<int> documentRows(const QModelIndex& index) const;
    QModelIndex indexForDocumentRow(int documentRow) const;
    bool isDeviceGroup(const QModelIndex& index) const;
    QString logicalId(const QModelIndex& index) const;
    QString logicalBaseId(const QModelIndex& index) const;
    QString generatedLogicalId(const QModelIndex& index,
                               int documentRow) const;
    QVector<PluginManifest> pluginsForType(const QString& deviceType) const;
    void markConnectionTesting(const QString& deviceId);
    void setConnectionTestResult(const DeviceConnectionTestResult& result);
    void clearConnectionStatus(const QString& deviceId = {});

private slots:
    void rebuild();

private:
    struct DeviceRow {
        QVector<int> documentRows;
        QString deviceType;
        QString baseId;
    };

    const DeviceRow* rowAt(const QModelIndex& index) const;
    QString pluginDisplayName(const QString& driverId) const;
    void rebuildRows();

    QPointer<StationDocument> m_document;
    QVector<PluginManifest> m_plugins;
    QVector<DeviceRow> m_rows;
    QHash<QString, QString> m_connectionStates;
    QHash<QString, QString> m_connectionDetails;
};

} // namespace PicoATE::Ui
