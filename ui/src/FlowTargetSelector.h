#pragma once

#include <QHash>
#include <QString>
#include <QStringList>
#include <QVector>
#include <QWidget>

class QLabel;
class QHBoxLayout;
class QToolButton;
class QVBoxLayout;

namespace PicoATE::Ui {

struct FlowTargetDevice {
    QString logicalId;
    QString deviceType;
    QString driverName;
    QString moduleId;
    QStringList targetIds;
    QStringList channelNames;
    bool configured = false;
};

class FlowTargetSelector final : public QWidget
{
    Q_OBJECT

public:
    explicit FlowTargetSelector(QWidget* parent = nullptr);

    void setDevices(QVector<FlowTargetDevice> devices);
    QVector<FlowTargetDevice> devices() const;
    QString currentDeviceId() const;
    QString currentTargetId() const;
    bool selectTarget(const QString& targetId);

signals:
    void targetChanged(const QString& targetId);

private:
    const FlowTargetDevice* deviceById(const QString& logicalId) const;
    const FlowTargetDevice* deviceForTarget(const QString& targetId) const;
    void selectDevice(const QString& logicalId,
                      const QString& preferredTarget = {});
    void toggleDevice(const QString& logicalId);
    void rebuildShortcuts();
    void rebuildChannels();
    void rebuildMoreMenu();
    void updateCurrentLabel();
    QStringList shortcutDeviceIds() const;
    void rememberDevice(const QString& logicalId);

    QVector<FlowTargetDevice> m_devices;
    QString m_currentDeviceId;
    QString m_currentTargetId;
    QStringList m_recentDeviceIds;
    QHash<QString, QString> m_lastTargetByDevice;
    QLabel* m_titleLabel = nullptr;
    QWidget* m_shortcutWidget = nullptr;
    QHBoxLayout* m_shortcutLayout = nullptr;
    QToolButton* m_moreButton = nullptr;
    QWidget* m_channelWidget = nullptr;
    QHBoxLayout* m_channelLayout = nullptr;
    QLabel* m_channelLabel = nullptr;
    QLabel* m_currentLabel = nullptr;
};

} // namespace PicoATE::Ui
