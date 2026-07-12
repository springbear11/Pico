#pragma once

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

    QJsonObject deviceAt(int row) const;
    void markConnectionTesting(const QString& deviceId);
    void setConnectionTestResult(const DeviceConnectionTestResult& result);
    void clearConnectionStatus(const QString& deviceId = {});

private slots:
    void rebuild();

private:
    QPointer<StationDocument> m_document;
    QHash<QString, QString> m_connectionStates;
    QHash<QString, QString> m_connectionDetails;
};

} // namespace PicoATE::Ui
