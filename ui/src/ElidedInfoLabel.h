#pragma once

#include "UiLanguage.h"

#include <QApplication>
#include <QClipboard>
#include <QContextMenuEvent>
#include <QHelpEvent>
#include <QLabel>
#include <QMenu>
#include <QPainter>
#include <QToolTip>

namespace PicoATE::Ui {

class ElidedInfoLabel final : public QLabel
{
public:
    ElidedInfoLabel(const QString& value, QWidget* parent) : QLabel(value, parent)
    {
        setTextFormat(Qt::PlainText);
        setWordWrap(false);
        setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
    }

    void setMaximumCharacters(int count)
    {
        m_maximumCharacters = qMax(0, count);
        update();
    }

    QString displayText() const
    {
        const auto characters = text().toUcs4();
        const auto shortened = m_maximumCharacters > 0 && characters.size() > m_maximumCharacters
            ? QString::fromUcs4(characters.constData(), m_maximumCharacters) + QChar(0x2026)
            : text();
        return fontMetrics().elidedText(shortened, Qt::ElideRight,
                                        qMax(0, contentsRect().width() - 2));
    }
    QSize sizeHint() const override { return {160, fontMetrics().height() + 2}; }
    QSize minimumSizeHint() const override { return {0, fontMetrics().height() + 2}; }

protected:
    void changeEvent(QEvent* event) override
    {
        QLabel::changeEvent(event);
        if (event->type() == QEvent::FontChange || event->type() == QEvent::StyleChange) {
            setMinimumHeight(fontMetrics().height() + 2);
            updateGeometry();
        }
    }

    void paintEvent(QPaintEvent*) override
    {
        QPainter painter(this);
        painter.setPen(palette().color(foregroundRole()));
        painter.drawText(contentsRect(), Qt::AlignLeft | Qt::AlignVCenter,
                         displayText());
    }
    bool event(QEvent* event) override
    {
        if (event->type() == QEvent::ToolTip) {
            const auto* help = static_cast<QHelpEvent*>(event);
            QToolTip::showText(help->globalPos(), text().toHtmlEscaped(), this);
            return true;
        }
        return QLabel::event(event);
    }
    void contextMenuEvent(QContextMenuEvent* event) override
    {
        QMenu menu(this);
        auto* copy = menu.addAction(uiText("Copy"));
        if (menu.exec(event->globalPos()) == copy) {
            QApplication::clipboard()->setText(text());
        }
    }
private:
    int m_maximumCharacters = 24;
};

} // namespace PicoATE::Ui
