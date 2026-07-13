#pragma once

#include <QAbstractButton>
#include <QStyledItemDelegate>

namespace PicoATE::Ui {

class OnOffSwitch final : public QAbstractButton
{
public:
    explicit OnOffSwitch(QWidget* parent = nullptr);

    QSize sizeHint() const override;

protected:
    void paintEvent(QPaintEvent* event) override;
};

class OnOffItemDelegate final : public QStyledItemDelegate
{
public:
    explicit OnOffItemDelegate(QObject* parent = nullptr);

    void paint(QPainter* painter,
               const QStyleOptionViewItem& option,
               const QModelIndex& index) const override;
    QSize sizeHint(const QStyleOptionViewItem& option,
                   const QModelIndex& index) const override;
    bool editorEvent(QEvent* event,
                     QAbstractItemModel* model,
                     const QStyleOptionViewItem& option,
                     const QModelIndex& index) override;
};

} // namespace PicoATE::Ui
