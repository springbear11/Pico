#pragma once

#include <QPersistentModelIndex>
#include <QTreeView>

class QDragLeaveEvent;
class QDragMoveEvent;
class QDropEvent;
class QMimeData;
class QPaintEvent;
class QTimer;

namespace PicoATE::Ui {

class SequenceEditorTreeView final : public QTreeView
{
public:
    enum class DropPlacement {
        Invalid,
        Into,
        Before,
        After,
    };

    struct DropPreview {
        DropPlacement placement = DropPlacement::Invalid;
        QModelIndex parentIndex;
        int row = -1;

        bool isValid() const
        {
            return placement != DropPlacement::Invalid && parentIndex.isValid();
        }
    };

    explicit SequenceEditorTreeView(QWidget* parent = nullptr);
    DropPreview dropPreviewAt(const QPoint& position) const;

protected:
    void dragMoveEvent(QDragMoveEvent* event) override;
    void dragLeaveEvent(QDragLeaveEvent* event) override;
    void dropEvent(QDropEvent* event) override;
    void paintEvent(QPaintEvent* event) override;

private:
    struct DropTarget {
        DropPlacement placement = DropPlacement::Invalid;
        QPersistentModelIndex hoveredIndex;
        QPersistentModelIndex parentIndex;
        int row = -1;

        bool isValid() const
        {
            return placement != DropPlacement::Invalid && parentIndex.isValid();
        }
    };

    bool supportsMimeData(const QMimeData* mimeData) const;
    DropTarget resolveDropTarget(const QPoint& position) const;
    void setDropTarget(DropTarget target);
    void clearDropTarget();

    DropTarget m_dropTarget;
    QTimer* m_hoverExpandTimer = nullptr;
};

} // namespace PicoATE::Ui
