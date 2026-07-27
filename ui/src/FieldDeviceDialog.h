#pragma once

#include "PicoATE/Core/DeviceDiscovery.h"

#include <QDialog>
#include <QHash>
#include <QJsonObject>

class QComboBox;
class QLabel;
class QLineEdit;
class QListWidget;
class QPushButton;
class QTimer;

namespace PicoATE::Ui {

class FieldDeviceDialog final : public QDialog
{
    Q_OBJECT

public:
    explicit FieldDeviceDialog(QString stationPath, QWidget* parent = nullptr);

    void resolveEffectiveStation(bool requireCanBindings = false);

signals:
    void stationSaved();
    void effectiveStationReady(const QByteArray& stationJson,
                               const QString& errorMessage);

private:
    void buildUi();
    bool loadStation(QString* errorMessage = nullptr);
    void reloadDevices();
    void selectDevice(int row);
    void updateEditor();
    void refreshResources();
    void scanCanDevices();
    void startDiscovery(const PicoATE::Core::DeviceDiscoveryRequest& request,
                        bool effectiveStationScan);
    void finishResourceDiscovery(const PicoATE::Core::DeviceDiscoveryResult& result);
    void applyAndSave();
    QString pluginDllPath(const QString& driverId) const;
    QString nativeHostProgram() const;

    QString m_stationPath;
    QJsonObject m_station;
    QVector<PicoATE::Core::StationFieldDevice> m_devices;
    QVector<PicoATE::Core::DiscoveredDeviceResource> m_resources;
    int m_selectedRow = -1;
    bool m_busy = false;
    QListWidget* m_deviceList = nullptr;
    QComboBox* m_connectionKind = nullptr;
    QComboBox* m_resourceCombo = nullptr;
    QLineEdit* m_manualAddress = nullptr;
    QLabel* m_statusLabel = nullptr;
    QPushButton* m_refreshButton = nullptr;
    QPushButton* m_applyButton = nullptr;
    QTimer* m_refreshTimer = nullptr;
};

} // namespace PicoATE::Ui
