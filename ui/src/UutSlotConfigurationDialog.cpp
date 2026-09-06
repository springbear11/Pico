#include "UutSlotConfigurationDialog.h"
#include "UiTextBinding.h"

#include <QDialog>
#include <QDialogButtonBox>
#include <QFrame>
#include <QGridLayout>
#include <QLabel>
#include <QPushButton>
#include <QScrollArea>
#include <QVBoxLayout>
#include <QWidget>

#include <algorithm>
#include <utility>

namespace PicoATE::Ui {

QVector<bool> normalizeUutSlotEnabledStates(
    int slotCount,
    const QVector<bool>& enabledStates)
{
    slotCount = qBound(1, slotCount, 64);
    QVector<bool> normalized(slotCount, true);
    for (int index = 0; index < qMin(slotCount, enabledStates.size()); ++index) {
        normalized[index] = enabledStates[index];
    }
    return normalized;
}

int enabledUutSlotCount(const QVector<bool>& enabledStates)
{
    return static_cast<int>(std::count(
        enabledStates.cbegin(), enabledStates.cend(), true));
}

std::optional<QVector<bool>> showUutSlotConfigurationDialog(
    QWidget* parent,
    int slotCount,
    const QVector<bool>& enabledStates)
{
    auto states = normalizeUutSlotEnabledStates(slotCount, enabledStates);

    QDialog dialog(parent);
    dialog.setObjectName(QStringLiteral("uutSlotConfigurationDialog"));
    bindUiText(&dialog, "windowTitle", "UUT Stations");
    dialog.setModal(true);
    dialog.setMinimumWidth(420);
    dialog.setMaximumWidth(520);

    auto* root = new QVBoxLayout(&dialog);
    root->setContentsMargins(24, 20, 24, 18);
    root->setSpacing(14);

    auto* title = makeUiLabel("UUT STATIONS", &dialog);
    title->setObjectName(QStringLiteral("uutSlotConfigurationTitle"));
    root->addWidget(title);

    auto* summary = new QLabel(&dialog);
    summary->setObjectName(QStringLiteral("uutSlotConfigurationSummary"));
    root->addWidget(summary);

    auto* scroll = new QScrollArea(&dialog);
    scroll->setObjectName(QStringLiteral("uutSlotConfigurationScroll"));
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    auto* host = new QWidget(scroll);
    auto* grid = new QGridLayout(host);
    grid->setContentsMargins(0, 0, 0, 0);
    grid->setHorizontalSpacing(8);
    grid->setVerticalSpacing(8);

    QVector<QPushButton*> slotButtons;
    slotButtons.reserve(states.size());
    const int columnCount = qMin(4, states.size());
    for (int index = 0; index < states.size(); ++index) {
        auto* button = new QPushButton(
            QObject::tr("UUT %1").arg(index + 1), host);
        button->setObjectName(
            QStringLiteral("uutSlotToggle%1").arg(index + 1));
        button->setProperty("uutSlotToggle", true);
        button->setCheckable(true);
        button->setChecked(states[index]);
        button->setMinimumSize(82, 38);
        const auto refreshTooltip = [button, index] {
            button->setToolTip(uiText("Enable or disable UUT %1").arg(index + 1));
        };
        refreshTooltip();
        QObject::connect(&UiLanguage::instance(), &UiLanguage::languageChanged,
                         button, refreshTooltip);
        grid->addWidget(button, index / columnCount, index % columnCount);
        slotButtons.push_back(button);
    }
    for (int column = 0; column < columnCount; ++column) {
        grid->setColumnStretch(column, 1);
    }
    scroll->setWidget(host);
    scroll->setMinimumHeight(qMin(260, 48 * ((states.size() + 3) / 4) + 8));
    root->addWidget(scroll);

    auto* commands = new QHBoxLayout;
    auto* enableAll = makeUiButton("Enable All", &dialog);
    enableAll->setObjectName(QStringLiteral("uutSlotEnableAllButton"));
    commands->addWidget(enableAll);
    commands->addStretch(1);
    auto* buttons = new QDialogButtonBox(
        QDialogButtonBox::Cancel | QDialogButtonBox::Ok, &dialog);
    buttons->setObjectName(QStringLiteral("uutSlotConfigurationButtons"));
    bindUiText(buttons->button(QDialogButtonBox::Ok), "text", "Apply");
    bindUiText(buttons->button(QDialogButtonBox::Cancel), "text", "Cancel");
    commands->addWidget(buttons);
    root->addLayout(commands);

    const auto refresh = [&] {
        for (int index = 0; index < slotButtons.size(); ++index) {
            states[index] = slotButtons[index]->isChecked();
        }
        const int activeCount = enabledUutSlotCount(states);
        summary->setText(uiText("%1 / %2 active")
                             .arg(activeCount)
                             .arg(states.size()));
        buttons->button(QDialogButtonBox::Ok)->setEnabled(activeCount > 0);
    };
    QObject::connect(&UiLanguage::instance(), &UiLanguage::languageChanged,
                     &dialog, refresh);
    for (auto* button : std::as_const(slotButtons)) {
        QObject::connect(button, &QPushButton::toggled, &dialog, refresh);
    }
    QObject::connect(enableAll, &QPushButton::clicked, &dialog, [&] {
        for (auto* button : std::as_const(slotButtons)) {
            button->setChecked(true);
        }
        refresh();
    });
    QObject::connect(buttons, &QDialogButtonBox::accepted,
                     &dialog, &QDialog::accept);
    QObject::connect(buttons, &QDialogButtonBox::rejected,
                     &dialog, &QDialog::reject);

    dialog.setStyleSheet(QStringLiteral(R"css(
        QDialog#uutSlotConfigurationDialog { background: #f7f8f9; }
        QLabel#uutSlotConfigurationTitle {
            color: #273038; font-size: 15px; font-weight: 800;
        }
        QLabel#uutSlotConfigurationSummary {
            color: #69757d; font-size: 11px; font-weight: 600;
        }
        QPushButton[uutSlotToggle="true"] {
            border: 1px solid #cbd4da; border-radius: 6px;
            background: #ffffff; color: #68737b; font-weight: 700;
        }
        QPushButton[uutSlotToggle="true"]:checked {
            border-color: #252a2f; background: #252a2f; color: #ffffff;
        }
        QPushButton[uutSlotToggle="true"]:hover {
            border-color: #52616b;
        }
        QPushButton#uutSlotEnableAllButton {
            min-height: 30px; padding: 0 12px;
        }
    )css"));
    refresh();

    if (dialog.exec() != QDialog::Accepted) {
        return std::nullopt;
    }
    return states;
}

} // namespace PicoATE::Ui
