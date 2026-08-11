#pragma once

#include "PluginCatalog.h"
#include "PicoATE/Core/DeviceDiscovery.h"

#include <QDialog>
#include <QHash>
#include <QJsonObject>

class QComboBox;
class QLabel;
class QListWidget;
class QPushButton;
class QTimer;

namespace PicoATE::Ui {

class FieldDeviceDialog final : public QDialog
{
    Q_OBJECT

public:
    explicit FieldDeviceDialog(QString stationPath, QWidget* parent = nullptr);

signals:
    void stationSaved();

public slots:
    void reject() override;

private:
    void buildUi();
    bool loadStation(QString* errorMessage = nullptr);
    void reloadDevices();
    void selectDevice(int row);
    void reloadConnectionKinds();
    void updateEditor();
    void refreshResources();
    void scanCanDevices();
    void startDiscovery(const PicoATE::Core::DeviceDiscoveryRequest& request);
    void finishResourceDiscovery(const PicoATE::Core::DeviceDiscoveryResult& result);
    void applyCurrentDevice();
    bool stageCurrentDevice(QString* errorMessage = nullptr);
    bool saveAllDevices();
    bool writeStation();
    QString selectedResource() const;
    void updateDeviceListItem(int row);
    QString pluginDllPath(const QString& driverId) const;
    QString nativeHostProgram() const;

    QString m_stationPath;
    QJsonObject m_station;
    QVector<PicoATE::Core::StationFieldDevice> m_devices;
    QVector<PicoATE::Core::DiscoveredDeviceResource> m_resources;
    QVector<PluginManifest> m_plugins;
    int m_selectedRow = -1;
    bool m_busy = false;
    bool m_updatingEditor = false;
    bool m_editorDirty = false;
    bool m_stationDirty = false;
    QListWidget* m_deviceList = nullptr;
    QComboBox* m_connectionKind = nullptr;
    QComboBox* m_resourceCombo = nullptr;
    QLabel* m_statusLabel = nullptr;
    QPushButton* m_refreshButton = nullptr;
    QPushButton* m_applyButton = nullptr;
    QPushButton* m_saveAllButton = nullptr;
    QPushButton* m_closeButton = nullptr;
    QTimer* m_refreshTimer = nullptr;
};

} // namespace PicoATE::Ui
