#pragma once

#include <QStyledItemDelegate>

namespace PicoATE::Ui {

class ParserActualDelegate final : public QStyledItemDelegate {
public:
    explicit ParserActualDelegate(QObject* parent = nullptr);

    void paint(QPainter* painter,
               const QStyleOptionViewItem& option,
               const QModelIndex& index) const override;
};

} // namespace PicoATE::Ui
