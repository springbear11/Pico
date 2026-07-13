#pragma once

#include "PluginCatalog.h"

#include <QPointer>
#include <QWidget>

class QAbstractButton;
class QCheckBox;
class QComboBox;
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
    void setEditable(bool editable);
    void setStationPageVisible(bool visible);
    void setPluginRegistry(QVector<PluginManifest> plugins);
    bool focusField(const QString& path);

signals:
    void stationApplied();
    void deviceApplied(int row);

private slots:
    void applyStation();
    void applyDevice();

private:
    void buildStationPage();
    void buildDevicePage();
    void reload();
    void loadStation();
    void loadDevice();
    void showStationError(const QString& message);
    void showDeviceError(const QString& message);

    QPointer<StationDocument> m_document;
    QTabWidget* m_tabs = nullptr;
    QLineEdit* m_stationIdEdit = nullptr;
    QLineEdit* m_stationNameEdit = nullptr;
    QCheckBox* m_scanDialogEnabledCheck = nullptr;
    QPlainTextEdit* m_metadataEdit = nullptr;
    QLabel* m_stationError = nullptr;
    QPushButton* m_applyStationButton = nullptr;
    QLineEdit* m_deviceIdEdit = nullptr;
    QLineEdit* m_deviceTypeEdit = nullptr;
    QLineEdit* m_driverIdEdit = nullptr;
    QComboBox* m_pluginCombo = nullptr;
    QLineEdit* m_addressEdit = nullptr;
    QSpinBox* m_timeoutSpin = nullptr;
    QComboBox* m_lifetimeCombo = nullptr;
    QAbstractButton* m_enabledCheck = nullptr;
    QPlainTextEdit* m_optionsEdit = nullptr;
    QLabel* m_deviceError = nullptr;
    QPushButton* m_applyDeviceButton = nullptr;
    int m_currentRow = -1;
    bool m_editable = true;
    QVector<PluginManifest> m_plugins;
};

} // namespace PicoATE::Ui
