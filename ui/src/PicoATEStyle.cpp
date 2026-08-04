#include "PicoATEStyle.h"

#include <QPainter>
#include <QPainterPath>
#include <QStyleOption>

#include <algorithm>

namespace PicoATE::Ui {

PicoATEStyle::PicoATEStyle() = default;

int PicoATEStyle::pixelMetric(PixelMetric metric,
                              const QStyleOption* option,
                              const QWidget* widget) const
{
    if (metric == PM_IndicatorWidth || metric == PM_IndicatorHeight) {
        return 18;
    }
    return QProxyStyle::pixelMetric(metric, option, widget);
}

void PicoATEStyle::drawPrimitive(PrimitiveElement element,
                                 const QStyleOption* option,
                                 QPainter* painter,
                                 const QWidget* widget) const
{
    if (element != PE_IndicatorCheckBox &&
        element != PE_IndicatorItemViewItemCheck) {
        QProxyStyle::drawPrimitive(element, option, painter, widget);
        return;
    }
    if (!option || !painter || option->rect.isEmpty()) {
        return;
    }

    const int side = std::min({18, option->rect.width(), option->rect.height()});
    const QRectF box(option->rect.center().x() - side / 2.0,
                     option->rect.center().y() - side / 2.0,
                     side, side);
    const bool enabled = option->state.testFlag(State_Enabled);
    const bool checked = option->state.testFlag(State_On);
    const bool partial = option->state.testFlag(State_NoChange);
    const bool hovered = option->state.testFlag(State_MouseOver);

    QColor fill = checked || partial
        ? QColor(QStringLiteral("#34393e"))
        : QColor(QStringLiteral("#ffffff"));
    QColor border = checked || partial
        ? QColor(QStringLiteral("#292e33"))
        : QColor(QStringLiteral("#7d8790"));
    if (hovered && !checked && !partial) {
        fill = QColor(QStringLiteral("#f0f4f6"));
        border = QColor(QStringLiteral("#59636c"));
    }
    if (!enabled) {
        fill.setAlpha(120);
        border.setAlpha(120);
    }

    painter->save();
    painter->setRenderHint(QPainter::Antialiasing, true);
    painter->setPen(QPen(border, 1.1));
    painter->setBrush(fill);
    painter->drawRoundedRect(box.adjusted(0.6, 0.6, -0.6, -0.6), 4.2, 4.2);

    if (checked) {
        QPainterPath check;
        check.moveTo(box.left() + 4.2, box.top() + 9.1);
        check.lineTo(box.left() + 7.5, box.top() + 12.0);
        check.lineTo(box.left() + 13.8, box.top() + 5.7);
        QColor checkColor(QStringLiteral("#151a1e"));
        if (!enabled) {
            checkColor.setAlpha(120);
        }
        QPen checkPen(checkColor, 1.7, Qt::SolidLine, Qt::RoundCap,
                      Qt::RoundJoin);
        painter->setPen(checkPen);
        painter->setBrush(Qt::NoBrush);
        painter->drawPath(check);
    } else if (partial) {
        QColor markColor(QStringLiteral("#151a1e"));
        if (!enabled) {
            markColor.setAlpha(120);
        }
        painter->setPen(QPen(markColor, 1.8, Qt::SolidLine, Qt::RoundCap));
        painter->drawLine(QPointF(box.left() + 4.5, box.center().y()),
                          QPointF(box.right() - 4.5, box.center().y()));
    }
    painter->restore();
}

} // namespace PicoATE::Ui
