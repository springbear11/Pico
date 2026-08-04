#include "SequenceEditorTreeView.h"

#include "PluginFunctionModel.h"
#include "SequenceTreeModel.h"

#include <QDragLeaveEvent>
#include <QDragMoveEvent>
#include <QDropEvent>
#include <QMimeData>
#include <QPainter>
#include <QTimer>

namespace PicoATE::Ui {

namespace {

constexpr auto SequenceItemMimeType =
    "application/x-picoate-sequence-item-path";

} // namespace

SequenceEditorTreeView::SequenceEditorTreeView(QWidget* parent)
    : QTreeView(parent)
    , m_hoverExpandTimer(new QTimer(this))
{
    setDropIndicatorShown(false);
    m_hoverExpandTimer->setSingleShot(true);
    m_hoverExpandTimer->setInterval(450);
    connect(m_hoverExpandTimer, &QTimer::timeout, this, [this] {
        if (m_dropTarget.placement == DropPlacement::Into &&
            m_dropTarget.hoveredIndex.isValid() && model() &&
            model()->rowCount(m_dropTarget.hoveredIndex) > 0 &&
            !isExpanded(m_dropTarget.hoveredIndex)) {
            expand(m_dropTarget.hoveredIndex);
        }
    });
}

SequenceEditorTreeView::DropPreview
SequenceEditorTreeView::dropPreviewAt(const QPoint& position) const
{
    const auto target = resolveDropTarget(position);
    return DropPreview{target.placement, target.parentIndex, target.row};
}

void SequenceEditorTreeView::dragMoveEvent(QDragMoveEvent* event)
{
    // Keep QAbstractItemView's edge auto-scroll behavior, then replace only
    // its ambiguous drop target with the explicit container/sibling target.
    QTreeView::dragMoveEvent(event);
    if (!supportsMimeData(event->mimeData())) {
        clearDropTarget();
        return;
    }

    auto target = resolveDropTarget(event->position().toPoint());
    if (!target.isValid()) {
        clearDropTarget();
        event->ignore();
        return;
    }

    setDropTarget(std::move(target));
    event->setDropAction(event->source() == this ? Qt::MoveAction
                                                 : Qt::CopyAction);
    event->accept();
}

void SequenceEditorTreeView::dragLeaveEvent(QDragLeaveEvent* event)
{
    clearDropTarget();
    QTreeView::dragLeaveEvent(event);
}

void SequenceEditorTreeView::dropEvent(QDropEvent* event)
{
    const auto target = resolveDropTarget(event->position().toPoint());
    auto* sequenceModel = qobject_cast<SequenceTreeModel*>(model());
    if (!sequenceModel || !target.isValid() ||
        !supportsMimeData(event->mimeData())) {
        clearDropTarget();
        event->ignore();
        return;
    }

    const auto action = event->source() == this ? Qt::MoveAction
                                                : Qt::CopyAction;
    const bool accepted = sequenceModel->dropMimeData(
        event->mimeData(), action, target.row, 0, target.parentIndex);
    clearDropTarget();
    if (!accepted) {
        event->ignore();
        return;
    }
    event->setDropAction(action);
    event->accept();
}

void SequenceEditorTreeView::paintEvent(QPaintEvent* event)
{
    QTreeView::paintEvent(event);
    if (!m_dropTarget.hoveredIndex.isValid() ||
        m_dropTarget.placement == DropPlacement::Invalid) {
        return;
    }

    auto rect = visualRect(m_dropTarget.hoveredIndex);
    if (!rect.isValid()) {
        return;
    }
    rect.setLeft(1);
    rect.setRight(viewport()->width() - 2);

    QPainter painter(viewport());
    painter.setRenderHint(QPainter::Antialiasing, false);
    const QColor accent(QStringLiteral("#5aa8dc"));
    if (m_dropTarget.placement == DropPlacement::Into) {
        painter.fillRect(rect, QColor(90, 168, 220, 40));
        painter.setPen(QPen(accent, 2));
        painter.drawRect(rect.adjusted(1, 1, -1, -1));
        const int childIndent = qMin(
            rect.right() - 8,
            visualRect(m_dropTarget.hoveredIndex).left() + indentation());
        painter.drawLine(childIndent, rect.bottom() - 2,
                         rect.right() - 6, rect.bottom() - 2);
        return;
    }

    const int y = m_dropTarget.placement == DropPlacement::Before
        ? rect.top() : rect.bottom();
    painter.setPen(QPen(accent, 2));
    painter.drawLine(rect.left() + 4, y, rect.right() - 4, y);
}

bool SequenceEditorTreeView::supportsMimeData(const QMimeData* mimeData) const
{
    return mimeData &&
        (mimeData->hasFormat(SequenceItemMimeType) ||
         mimeData->hasFormat(PluginFunctionMimeType));
}

SequenceEditorTreeView::DropTarget
SequenceEditorTreeView::resolveDropTarget(const QPoint& position) const
{
    DropTarget target;
    auto* sequenceModel = qobject_cast<SequenceTreeModel*>(model());
    auto hovered = indexAt(position);
    if (!sequenceModel || !hovered.isValid()) {
        return target;
    }
    hovered = hovered.siblingAtColumn(SequenceTreeModel::NameColumn);
    const auto rect = visualRect(hovered);
    if (!rect.isValid()) {
        return target;
    }

    if (sequenceModel->canContainSteps(hovered)) {
        const int edgeHeight = qBound(3, rect.height() / 5, 6);
        if (position.y() > rect.top() + edgeHeight &&
            position.y() < rect.bottom() - edgeHeight) {
            target.placement = DropPlacement::Into;
            target.hoveredIndex = hovered;
            target.parentIndex = hovered;
            target.row = sequenceModel->rowCount(hovered);
            return target;
        }
    }

    const auto parent = hovered.parent();
    if (!parent.isValid() || !sequenceModel->canContainSteps(parent)) {
        return target;
    }
    target.placement = position.y() < rect.center().y()
        ? DropPlacement::Before : DropPlacement::After;
    target.hoveredIndex = hovered;
    target.parentIndex = parent;
    target.row = hovered.row() +
        (target.placement == DropPlacement::After ? 1 : 0);
    return target;
}

void SequenceEditorTreeView::setDropTarget(DropTarget target)
{
    const bool hoverChanged =
        target.hoveredIndex != m_dropTarget.hoveredIndex ||
        target.placement != m_dropTarget.placement;
    m_dropTarget = std::move(target);
    if (hoverChanged) {
        m_hoverExpandTimer->stop();
        if (m_dropTarget.placement == DropPlacement::Into &&
            m_dropTarget.hoveredIndex.isValid() &&
            !isExpanded(m_dropTarget.hoveredIndex)) {
            m_hoverExpandTimer->start();
        }
    }
    viewport()->update();
}

void SequenceEditorTreeView::clearDropTarget()
{
    m_hoverExpandTimer->stop();
    if (m_dropTarget.placement == DropPlacement::Invalid) {
        return;
    }
    m_dropTarget = {};
    viewport()->update();
}

} // namespace PicoATE::Ui
