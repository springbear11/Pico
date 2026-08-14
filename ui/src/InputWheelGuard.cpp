#include "InputWheelGuard.h"

#include <QAbstractItemView>
#include <QAbstractScrollArea>
#include <QAbstractSpinBox>
#include <QComboBox>
#include <QCoreApplication>
#include <QScrollBar>
#include <QWheelEvent>
#include <QWidget>

namespace PicoATE::Ui {

namespace {

QWidget* wheelControlledInput(QWidget* widget)
{
    for (auto* current = widget; current; current = current->parentWidget()) {
        if (qobject_cast<QComboBox*>(current) ||
            qobject_cast<QAbstractSpinBox*>(current)) {
            return current;
        }
        if (qobject_cast<QAbstractScrollArea*>(current)) {
            break;
        }
    }
    return nullptr;
}

QAbstractScrollArea* containingScrollArea(QWidget* widget)
{
    for (auto* current = widget ? widget->parentWidget() : nullptr;
         current;
         current = current->parentWidget()) {
        if (auto* scrollArea = qobject_cast<QAbstractScrollArea*>(current)) {
            return scrollArea;
        }
    }
    return nullptr;
}

void forwardWheelToPage(QWidget* input, const QWheelEvent& event)
{
    auto* scrollArea = containingScrollArea(input);
    if (!scrollArea || !scrollArea->viewport()) {
        return;
    }

    const auto* verticalBar = scrollArea->verticalScrollBar();
    const auto* horizontalBar = scrollArea->horizontalScrollBar();
    const bool canScrollVertically = verticalBar && verticalBar->maximum() > 0;
    const bool canScrollHorizontally = horizontalBar && horizontalBar->maximum() > 0;
    if (!canScrollVertically && !canScrollHorizontally) {
        return;
    }

    auto* viewport = scrollArea->viewport();
    const QPointF localPosition = viewport->mapFromGlobal(
        event.globalPosition().toPoint());
    QWheelEvent forwarded(localPosition,
                          event.globalPosition(),
                          event.pixelDelta(),
                          event.angleDelta(),
                          event.buttons(),
                          event.modifiers(),
                          event.phase(),
                          event.inverted(),
                          event.source(),
                          event.pointingDevice());
    QCoreApplication::sendEvent(viewport, &forwarded);
}

} // namespace

InputWheelGuard::InputWheelGuard(QObject* parent)
    : QObject(parent)
{
}

bool InputWheelGuard::eventFilter(QObject* watched, QEvent* event)
{
    if (!event || event->type() != QEvent::Wheel) {
        return QObject::eventFilter(watched, event);
    }

    auto* watchedWidget = qobject_cast<QWidget*>(watched);
    auto* input = wheelControlledInput(watchedWidget);
    if (!input) {
        return QObject::eventFilter(watched, event);
    }

    const auto* wheelEvent = static_cast<QWheelEvent*>(event);
    if (wheelEvent->modifiers().testFlag(Qt::ControlModifier) &&
        input->hasFocus()) {
        return QObject::eventFilter(watched, event);
    }

    if (auto* combo = qobject_cast<QComboBox*>(input);
        combo && combo->view() && combo->view()->isVisible()) {
        return QObject::eventFilter(watched, event);
    }

    forwardWheelToPage(input, *wheelEvent);
    return true;
}

} // namespace PicoATE::Ui
