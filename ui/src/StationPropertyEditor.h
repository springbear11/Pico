#pragma once

#include "PluginCatalog.h"
#include "PicoATE/Core/DeviceDiscovery.h"

#include <QHash>
#include <QPointer>
#include <QWidget>

class QAbstractButton;
class QCheckBox;
class QComboBox;
class QFormLayout;
class QGroupBox;
class QLabel;
class QLineEdit;
class QPlainTextEdit;
class QPushButton;
class QSpinBox;
class QTabWidget;

namespace PicoATE::Ui {

class StationDocument;

class StationPropertyEditor final : public QWidget
{
    Q_OBJECT

public:
    explicit StationPropertyEditor(StationDocument* document,
                                   QWidget* parent = nullptr);

    void setCurrentDevice(int row);
    void setCurrentDevices(QVector<int> rows, QString logicalBaseId = {});
    void setEditable(bool editable);
    void setStationPageVisible(bool visible);
    void setPluginRegistry(QVector<PluginManifest> plugins);
    bool focusField(const QString& path);
    bool hasPendingChanges() const;
    bool commitPendingChanges();
    void discardPendingChanges();
    int currentDeviceRow() const;

signals:
    void stationApplied();
    void deviceApplied(int row);
    void pendingChangesChanged(bool pending);

private:
    struct OptionEditor {
        PluginParameterDefinition definition;
        QWidget* widget = nullptr;
        int channelIndex = -1;
    };

    void buildStationPage();
    void buildDevicePage();
    void reload();
    void loadStation();
    void loadDevice();
    void reloadPluginChoices(const QString& selectedModuleId = {});
    void reloadConnectionKinds(const QString& selectedKind = {});
    void refreshResources();
    void finishResourceDiscovery(
        const PicoATE::Core::DeviceDiscoveryResult& result);
    void rebuildChannelSwitches();
    void rebuildOptionEditors();
    void updateLogicalIdPreview();
    void updateAddressPresentation();
    void showStationError(const QString& message);
    void showDeviceError(const QString& message);
    bool commitStation();
    bool commitDevice();
    bool collectOptionValues(QJsonObject& sharedOptions,
                             QHash<int, QJsonObject>& channelOptions,
                             QString& errorMessage) const;
    const PluginManifest* selectedPlugin() const;
    const PluginFunctionDefinition* connectionFunction(
        const PluginManifest& plugin) const;
    QString effectiveBaseId() const;
    void markPendingChanges();
    void setPendingChanges(bool pending);

    QPointer<StationDocument> m_document;
    QLabel* m_title = nullptr;
    QTabWidget* m_tabs = nullptr;
    QLineEdit* m_stationIdEdit = nullptr;
    QLineEdit* m_stationNameEdit = nullptr;
    QCheckBox* m_scanDialogEnabledCheck = nullptr;
    QPlainTextEdit* m_metadataEdit = nullptr;
    QLabel* m_stationError = nullptr;
    QLineEdit* m_deviceIdEdit = nullptr;
    QComboBox* m_deviceTypeCombo = nullptr;
    QComboBox* m_pluginCombo = nullptr;
    QComboBox* m_connectionKindCombo = nullptr;
    QLabel* m_addressLabel = nullptr;
    QComboBox* m_resourceCombo = nullptr;
    QPushButton* m_refreshResourceButton = nullptr;
    QLabel* m_resourceStatus = nullptr;
    QSpinBox* m_timeoutSpin = nullptr;
    QComboBox* m_lifetimeCombo = nullptr;
    QLabel* m_channelsLabel = nullptr;
    QWidget* m_channelsWidget = nullptr;
    QGroupBox* m_optionsGroup = nullptr;
    QFormLayout* m_optionsForm = nullptr;
    QLabel* m_optionsHint = nullptr;
    QLabel* m_deviceError = nullptr;
    QVector<int> m_currentRows;
    QString m_logicalBaseId;
    QString m_loadedDeviceType;
    QString m_loadedDriverId;
    QHash<int, bool> m_loadedChannelEnabled;
    QVector<QAbstractButton*> m_channelSwitches;
    QVector<OptionEditor> m_optionEditors;
    bool m_editable = true;
    bool m_loading = false;
    bool m_pendingChanges = false;
    bool m_resourceDiscoveryBusy = false;
    QVector<PluginManifest> m_plugins;
};

} // namespace PicoATE::Ui
