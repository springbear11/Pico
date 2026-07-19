#include "FlowTargetSelector.h"

#include <QAction>
#include <QFrame>
#include <QIcon>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QStyle>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>
#include <QWidgetAction>

#include <algorithm>

namespace PicoATE::Ui {

namespace {

constexpr int MaximumShortcutDevices = 4;

void clearLayout(QLayout* layout)
{
    while (layout && layout->count() > 0) {
        const auto item = layout->takeAt(0);
        if (item->widget()) {
            item->widget()->setParent(nullptr);
            item->widget()->deleteLater();
        }
        delete item;
    }
}

QString deviceButtonText(const FlowTargetDevice& device)
{
    if (device.driverName.isEmpty()) {
        return device.logicalId + QStringLiteral("\n") + QObject::tr("No driver");
    }
    return device.logicalId + QStringLiteral("\n") + device.driverName;
}

QIcon deviceIcon(QWidget* owner, const QString& deviceType)
{
    const auto type = deviceType.trimmed().toUpper();
    const auto icon = type == QStringLiteral("CAN") ||
                      type == QStringLiteral("SERIAL") ||
                      type == QStringLiteral("MODBUS")
        ? QStyle::SP_DriveNetIcon
        : QStyle::SP_ComputerIcon;
    return owner->style()->standardIcon(icon, {}, owner);
}

} // namespace

FlowTargetSelector::FlowTargetSelector(QWidget* parent)
    : QWidget(parent)
{
    setObjectName(QStringLiteral("flowTargetSelector"));
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(8, 6, 8, 6);
    root->setSpacing(6);

    m_titleLabel = new QLabel(tr("Target Device"), this);
    m_titleLabel->setObjectName(QStringLiteral("flowTargetTitle"));
    root->addWidget(m_titleLabel);

    m_shortcutWidget = new QWidget(this);
    m_shortcutWidget->setObjectName(QStringLiteral("flowTargetShortcuts"));
    m_shortcutLayout = new QHBoxLayout(m_shortcutWidget);
    m_shortcutLayout->setContentsMargins(0, 0, 0, 0);
    m_shortcutLayout->setSpacing(4);
    root->addWidget(m_shortcutWidget);

    m_channelWidget = new QWidget(this);
    m_channelWidget->setObjectName(QStringLiteral("flowTargetChannels"));
    auto* channelRoot = new QVBoxLayout(m_channelWidget);
    channelRoot->setContentsMargins(0, 0, 0, 0);
    channelRoot->setSpacing(3);
    m_channelLabel = new QLabel(tr("Channel"), m_channelWidget);
    channelRoot->addWidget(m_channelLabel);
    auto* channelButtons = new QWidget(m_channelWidget);
    m_channelLayout = new QHBoxLayout(channelButtons);
    m_channelLayout->setContentsMargins(0, 0, 0, 0);
    m_channelLayout->setSpacing(0);
    channelRoot->addWidget(channelButtons);
    root->addWidget(m_channelWidget);

    m_currentLabel = new QLabel(this);
    m_currentLabel->setObjectName(QStringLiteral("flowCurrentTarget"));
    m_currentLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    root->addWidget(m_currentLabel);

    auto* separator = new QFrame(this);
    separator->setFrameShape(QFrame::HLine);
    separator->setObjectName(QStringLiteral("flowTargetSeparator"));
    root->addWidget(separator);

    setStyleSheet(QStringLiteral(R"(
        QWidget#flowTargetSelector { background: #fbfcfd; }
        QLabel#flowTargetTitle { color: #344054; font-weight: 600; }
        QToolButton[deviceShortcut="true"] {
            min-width: 54px; min-height: 40px; padding: 3px 5px;
            border: 1px solid #d7dce2; border-radius: 5px;
            background: #ffffff; color: #344054;
        }
        QToolButton[deviceShortcut="true"]:checked {
            border-color: #75a7e8; background: #eaf3ff; color: #175cd3;
        }
        QToolButton[deviceShortcut="true"]:disabled {
            color: #98a2b3; background: #f5f6f7;
        }
        QToolButton#flowMoreDevices {
            min-width: 26px; min-height: 40px; padding: 2px;
            border: 1px solid #d7dce2; border-radius: 5px; background: #ffffff;
        }
        QToolButton[channelButton="true"] {
            min-height: 25px; padding: 1px 10px; border: 1px solid #d0d5dd;
            background: #ffffff; color: #475467;
        }
        QToolButton[channelButton="true"]:checked {
            border-color: #75a7e8; background: #eaf3ff; color: #175cd3;
        }
        QLabel#flowCurrentTarget { color: #475467; padding: 2px 0; }
        QFrame#flowTargetSeparator { color: #e4e7ec; }
    )"));
}

void FlowTargetSelector::setDevices(QVector<FlowTargetDevice> devices)
{
    const auto previousTarget = m_currentTargetId;
    m_devices = std::move(devices);
    std::sort(m_devices.begin(), m_devices.end(),
              [](const FlowTargetDevice& left, const FlowTargetDevice& right) {
                  const int typeOrder = left.deviceType.compare(
                      right.deviceType, Qt::CaseInsensitive);
                  return typeOrder == 0
                      ? left.logicalId.compare(right.logicalId,
                                               Qt::CaseInsensitive) < 0
                      : typeOrder < 0;
              });

    if (!deviceById(m_currentDeviceId)) {
        m_currentDeviceId.clear();
        m_currentTargetId.clear();
    }
    if (m_currentDeviceId.isEmpty()) {
        const auto firstConfigured = std::find_if(
            m_devices.cbegin(), m_devices.cend(),
            [](const FlowTargetDevice& device) { return device.configured; });
        if (firstConfigured != m_devices.cend()) {
            m_currentDeviceId = firstConfigured->logicalId;
        } else if (!m_devices.isEmpty()) {
            m_currentDeviceId = m_devices.first().logicalId;
        }
    }
    selectDevice(m_currentDeviceId, previousTarget);
}

QVector<FlowTargetDevice> FlowTargetSelector::devices() const
{
    return m_devices;
}

QString FlowTargetSelector::currentDeviceId() const
{
    return m_currentDeviceId;
}

QString FlowTargetSelector::currentTargetId() const
{
    return m_currentTargetId;
}

bool FlowTargetSelector::selectTarget(const QString& targetId)
{
    const auto* device = deviceForTarget(targetId);
    if (!device || !device->configured) {
        return false;
    }
    selectDevice(device->logicalId, targetId);
    return m_currentTargetId == targetId;
}

const FlowTargetDevice* FlowTargetSelector::deviceById(
    const QString& logicalId) const
{
    const auto iterator = std::find_if(
        m_devices.cbegin(), m_devices.cend(),
        [&](const FlowTargetDevice& device) {
            return device.logicalId.compare(logicalId, Qt::CaseInsensitive) == 0;
        });
    return iterator == m_devices.cend() ? nullptr : &*iterator;
}

const FlowTargetDevice* FlowTargetSelector::deviceForTarget(
    const QString& targetId) const
{
    const auto iterator = std::find_if(
        m_devices.cbegin(), m_devices.cend(),
        [&](const FlowTargetDevice& device) {
            return device.targetIds.contains(targetId, Qt::CaseInsensitive);
        });
    return iterator == m_devices.cend() ? nullptr : &*iterator;
}

void FlowTargetSelector::selectDevice(const QString& logicalId,
                                      const QString& preferredTarget)
{
    const auto oldTarget = m_currentTargetId;
    const auto* device = deviceById(logicalId);
    m_currentDeviceId = device ? device->logicalId : QString{};
    m_currentTargetId.clear();
    if (device && device->configured && !device->targetIds.isEmpty()) {
        const auto remembered = m_lastTargetByDevice.value(device->logicalId);
        if (device->targetIds.contains(preferredTarget, Qt::CaseInsensitive)) {
            m_currentTargetId = preferredTarget;
        } else if (device->targetIds.contains(remembered, Qt::CaseInsensitive)) {
            m_currentTargetId = remembered;
        } else {
            m_currentTargetId = device->targetIds.first();
        }
        m_lastTargetByDevice.insert(device->logicalId, m_currentTargetId);
        rememberDevice(device->logicalId);
    }
    rebuildShortcuts();
    rebuildChannels();
    rebuildMoreMenu();
    updateCurrentLabel();
    if (oldTarget != m_currentTargetId) {
        emit targetChanged(m_currentTargetId);
    }
}

void FlowTargetSelector::rebuildShortcuts()
{
    clearLayout(m_shortcutLayout);
    for (const auto& id : shortcutDeviceIds()) {
        const auto* device = deviceById(id);
        if (!device) {
            continue;
        }
        auto* button = new QToolButton(m_shortcutWidget);
        button->setProperty("deviceShortcut", true);
        button->setCheckable(true);
        button->setChecked(device->logicalId == m_currentDeviceId);
        button->setEnabled(device->configured);
        button->setText(deviceButtonText(*device));
        button->setIcon(deviceIcon(button, device->deviceType));
        button->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
        button->setToolTip(device->configured
            ? tr("Use %1 as the target device").arg(device->logicalId)
            : tr("%1 has no configured driver").arg(device->logicalId));
        button->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
        connect(button, &QToolButton::clicked, this,
                [this, id = device->logicalId] { selectDevice(id); });
        m_shortcutLayout->addWidget(button, 1);
    }

    m_moreButton = new QToolButton(m_shortcutWidget);
    m_moreButton->setObjectName(QStringLiteral("flowMoreDevices"));
    m_moreButton->setText(QStringLiteral("..."));
    m_moreButton->setPopupMode(QToolButton::InstantPopup);
    m_moreButton->setToolTip(tr("Choose another Station device"));
    m_moreButton->setVisible(m_devices.size() > MaximumShortcutDevices);
    m_shortcutLayout->addWidget(m_moreButton);
}

void FlowTargetSelector::rebuildChannels()
{
    clearLayout(m_channelLayout);
    const auto* device = deviceById(m_currentDeviceId);
    const bool multipleTargets = device && device->targetIds.size() > 1;
    m_channelWidget->setVisible(multipleTargets);
    if (!multipleTargets) {
        return;
    }

    for (int index = 0; index < device->targetIds.size(); ++index) {
        const auto target = device->targetIds[index];
        const auto name = index < device->channelNames.size()
            ? device->channelNames[index]
            : target;
        auto* button = new QToolButton(m_channelWidget);
        button->setProperty("channelButton", true);
        button->setCheckable(true);
        button->setChecked(target == m_currentTargetId);
        button->setText(name);
        button->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
        connect(button, &QToolButton::clicked, this, [this, target] {
            const auto oldTarget = m_currentTargetId;
            m_currentTargetId = target;
            m_lastTargetByDevice.insert(m_currentDeviceId, target);
            rebuildChannels();
            updateCurrentLabel();
            if (oldTarget != target) {
                emit targetChanged(target);
            }
        });
        m_channelLayout->addWidget(button, 1);
    }
}

void FlowTargetSelector::rebuildMoreMenu()
{
    if (!m_moreButton) {
        return;
    }
    auto* menu = new QMenu(m_moreButton);
    menu->setObjectName(QStringLiteral("flowTargetDeviceMenu"));

    auto* search = new QLineEdit(menu);
    search->setObjectName(QStringLiteral("flowTargetSearch"));
    search->setPlaceholderText(tr("Search devices"));
    search->setClearButtonEnabled(true);
    auto* searchAction = new QWidgetAction(menu);
    searchAction->setDefaultWidget(search);
    menu->addAction(searchAction);
    menu->addSeparator();

    QVector<QAction*> deviceActions;
    QString currentType;
    for (const auto& device : m_devices) {
        if (currentType != device.deviceType) {
            currentType = device.deviceType;
            menu->addSection(currentType);
        }
        const auto suffix = device.driverName.isEmpty()
            ? tr("No driver") : device.driverName;
        auto* action = menu->addAction(
            QStringLiteral("%1    %2").arg(device.logicalId, suffix));
        action->setData(device.logicalId);
        action->setCheckable(true);
        action->setChecked(device.logicalId == m_currentDeviceId);
        action->setEnabled(device.configured);
        connect(action, &QAction::triggered, this,
                [this, id = device.logicalId] { selectDevice(id); });
        deviceActions.push_back(action);
    }
    connect(search, &QLineEdit::textChanged, menu,
            [deviceActions](const QString& text) {
                for (auto* action : deviceActions) {
                    action->setVisible(action->text().contains(
                        text.trimmed(), Qt::CaseInsensitive));
                }
            });
    connect(menu, &QMenu::aboutToShow, search, [search] {
        search->clear();
        QTimer::singleShot(0, search, [search] { search->setFocus(); });
    });
    m_moreButton->setMenu(menu);
}

void FlowTargetSelector::updateCurrentLabel()
{
    if (!m_currentTargetId.isEmpty()) {
        m_currentLabel->setText(
            tr("Current Target  %1").arg(m_currentTargetId));
        m_currentLabel->setToolTip({});
        return;
    }
    const auto* device = deviceById(m_currentDeviceId);
    if (device && !device->configured) {
        m_currentLabel->setText(
            tr("Current Target  %1 (No driver)").arg(device->logicalId));
        m_currentLabel->setToolTip(
            tr("Configure a driver in Station Config before using plugin functions"));
    } else {
        m_currentLabel->setText(tr("Current Target  None"));
        m_currentLabel->setToolTip({});
    }
}

QStringList FlowTargetSelector::shortcutDeviceIds() const
{
    if (m_devices.size() <= MaximumShortcutDevices) {
        QStringList result;
        for (const auto& device : m_devices) {
            result.push_back(device.logicalId);
        }
        return result;
    }

    QStringList result;
    if (!m_currentDeviceId.isEmpty()) {
        result.push_back(m_currentDeviceId);
    }
    for (const auto& id : m_recentDeviceIds) {
        if (!result.contains(id) && deviceById(id)) {
            result.push_back(id);
        }
        if (result.size() == MaximumShortcutDevices) {
            return result;
        }
    }
    for (const auto& device : m_devices) {
        if (!result.contains(device.logicalId)) {
            result.push_back(device.logicalId);
        }
        if (result.size() == MaximumShortcutDevices) {
            break;
        }
    }
    return result;
}

void FlowTargetSelector::rememberDevice(const QString& logicalId)
{
    m_recentDeviceIds.removeAll(logicalId);
    m_recentDeviceIds.prepend(logicalId);
    while (m_recentDeviceIds.size() > MaximumShortcutDevices) {
        m_recentDeviceIds.removeLast();
    }
}

} // namespace PicoATE::Ui
