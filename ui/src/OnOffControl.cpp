#include "OnOffControl.h"

#include <QAbstractItemModel>
#include <QApplication>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QStyle>
#include <QWidget>

namespace PicoATE::Ui {

namespace {

constexpr int SwitchWidth = 58;
constexpr int SwitchHeight = 26;

void paintSwitch(QPainter& painter,
                 const QRect& bounds,
                 bool checked,
                 bool enabled)
{
    const QRect centered(bounds.center().x() - SwitchWidth / 2,
                         bounds.center().y() - SwitchHeight / 2,
                         SwitchWidth,
                         SwitchHeight);
    const QColor trackColor = !enabled
        ? QColor(QStringLiteral("#d0d5dd"))
        : checked ? QColor(QStringLiteral("#2e90fa"))
                  : QColor(QStringLiteral("#98a2b3"));

    painter.save();
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setPen(Qt::NoPen);
    painter.setBrush(trackColor);
    painter.drawRoundedRect(centered, SwitchHeight / 2, SwitchHeight / 2);

    painter.setPen(Qt::white);
    auto font = painter.font();
    font.setBold(true);
    font.setPointSize(8);
    painter.setFont(font);
    const QRect textRect = checked
        ? centered.adjusted(4, 0, -24, 0)
        : centered.adjusted(24, 0, -4, 0);
    painter.drawText(textRect,
                     Qt::AlignCenter,
                     checked ? QStringLiteral("ON") : QStringLiteral("OFF"));

    constexpr int knobDiameter = 20;
    const int knobX = checked
        ? centered.right() - knobDiameter - 2
        : centered.left() + 3;
    painter.setBrush(Qt::white);
    painter.drawEllipse(QRect(knobX,
                              centered.top() + 3,
                              knobDiameter,
                              knobDiameter));
    painter.restore();
}

} // namespace

OnOffSwitch::OnOffSwitch(QWidget* parent)
    : QAbstractButton(parent)
{
    setCheckable(true);
    setCursor(Qt::PointingHandCursor);
    setFocusPolicy(Qt::StrongFocus);
    setFixedSize(SwitchWidth, SwitchHeight);
    connect(this, &QAbstractButton::toggled, this, [this] { update(); });
}

QSize OnOffSwitch::sizeHint() const
{
    return {SwitchWidth, SwitchHeight};
}

void OnOffSwitch::paintEvent(QPaintEvent*)
{
    QPainter painter(this);
    paintSwitch(painter, rect(), isChecked(), isEnabled());
}

OnOffItemDelegate::OnOffItemDelegate(QObject* parent)
    : QStyledItemDelegate(parent)
{
    setObjectName(QStringLiteral("onOffItemDelegate"));
}

void OnOffItemDelegate::paint(QPainter* painter,
                              const QStyleOptionViewItem& option,
                              const QModelIndex& index) const
{
    QStyleOptionViewItem backgroundOption(option);
    initStyleOption(&backgroundOption, index);
    backgroundOption.text.clear();
    backgroundOption.features &= ~QStyleOptionViewItem::HasCheckIndicator;
    auto* style = option.widget ? option.widget->style() : QApplication::style();
    style->drawControl(QStyle::CE_ItemViewItem,
                       &backgroundOption,
                       painter,
                       option.widget);
    paintSwitch(*painter,
                option.rect,
                index.data(Qt::CheckStateRole).toInt() == Qt::Checked,
                option.state.testFlag(QStyle::State_Enabled));
}

QSize OnOffItemDelegate::sizeHint(const QStyleOptionViewItem&,
                                  const QModelIndex&) const
{
    return {SwitchWidth + 12, SwitchHeight + 8};
}

bool OnOffItemDelegate::editorEvent(QEvent* event,
                                    QAbstractItemModel* model,
                                    const QStyleOptionViewItem& option,
                                    const QModelIndex& index)
{
    if (!model || !(index.flags() & Qt::ItemIsEnabled) ||
        !(index.flags() & Qt::ItemIsUserCheckable)) {
        return false;
    }

    bool activate = false;
    if (event->type() == QEvent::MouseButtonRelease) {
        const auto* mouseEvent = static_cast<QMouseEvent*>(event);
        activate = mouseEvent->button() == Qt::LeftButton &&
            option.rect.contains(mouseEvent->position().toPoint());
    } else if (event->type() == QEvent::KeyPress) {
        const auto* keyEvent = static_cast<QKeyEvent*>(event);
        activate = keyEvent->key() == Qt::Key_Space ||
            keyEvent->key() == Qt::Key_Select;
    }
    if (!activate) {
        return false;
    }

    const auto nextState = index.data(Qt::CheckStateRole).toInt() == Qt::Checked
        ? Qt::Unchecked
        : Qt::Checked;
    return model->setData(index, nextState, Qt::CheckStateRole);
}

} // namespace PicoATE::Ui
