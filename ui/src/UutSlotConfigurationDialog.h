#pragma once

#include <QVector>

#include <optional>

class QWidget;

namespace PicoATE::Ui {

QVector<bool> normalizeUutSlotEnabledStates(
    int slotCount,
    const QVector<bool>& enabledStates = {});
int enabledUutSlotCount(const QVector<bool>& enabledStates);

std::optional<QVector<bool>> showUutSlotConfigurationDialog(
    QWidget* parent,
    int slotCount,
    const QVector<bool>& enabledStates);

} // namespace PicoATE::Ui
