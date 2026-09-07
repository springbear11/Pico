#pragma once

#include "UiLanguage.h"

#include <QPainter>
#include <QPaintEvent>
#include <QSize>
#include <QSizePolicy>
#include <QWidget>

#include <algorithm>

namespace PicoATE::Ui {

class YieldDonutWidget final : public QWidget
{
public:
    explicit YieldDonutWidget(QWidget* parent = nullptr)
        : QWidget(parent)
    {
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
        setMinimumHeight(64);
        setMaximumHeight(100);
        setMinimumWidth(150);
        setAccessibleName(QStringLiteral("Yield"));
        updateProperties();
        connect(&UiLanguage::instance(), &UiLanguage::languageChanged,
                this, [this] {
            updateProperties();
            update();
        });
    }

    void setCounts(int passed, int failed)
    {
        passed = std::max(0, passed);
        failed = std::max(0, failed);
        if (m_passed == passed && m_failed == failed) {
            return;
        }
        m_passed = passed;
        m_failed = failed;
        updateProperties();
        update();
    }

    [[nodiscard]] QSize sizeHint() const override
    {
        return {176, 92};
    }

protected:
    void paintEvent(QPaintEvent* event) override
    {
        Q_UNUSED(event);

        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing, true);
        const qreal scale = std::min<qreal>(1.0,
            std::min(width() / 180.0, height() / 100.0));
        painter.translate((width() - 180.0 * scale) / 2.0,
                          (height() - 100.0 * scale) / 2.0);
        painter.scale(scale, scale);

        constexpr qreal ringWidth = 11.0;
        constexpr int fullArc = 180 * 16;
        constexpr qreal diameter = 160.0;
        const QRectF ringRect((180.0 - diameter) / 2.0,
                              5.0,
                              diameter,
                              diameter);

        QPen pen(QColor(QStringLiteral("#dce3e8")), ringWidth);
        pen.setCapStyle(Qt::RoundCap);
        painter.setPen(pen);
        painter.setBrush(Qt::NoBrush);
        constexpr int startAngle = 180 * 16;
        painter.drawArc(ringRect, startAngle, -fullArc);

        const int total = m_passed + m_failed;
        if (total > 0) {
            const int passedSpan = qRound(
                static_cast<double>(fullArc) * m_passed / total);
            const int failedSpan = fullArc - passedSpan;

            if (passedSpan > 0) {
                pen.setColor(QColor(QStringLiteral("#3f9661")));
                painter.setPen(pen);
                painter.drawArc(ringRect, startAngle, -passedSpan);
            }
            if (failedSpan > 0) {
                pen.setColor(QColor(QStringLiteral("#cf5656")));
                painter.setPen(pen);
                painter.drawArc(ringRect, startAngle - passedSpan, -failedSpan);
            }
        }

        const QPointF center = ringRect.center();
        QFont percentFont = font();
        percentFont.setPointSizeF(std::max(14.0, 10.0 / scale));
        percentFont.setWeight(QFont::DemiBold);
        painter.setFont(percentFont);
        painter.setPen(QColor(QStringLiteral("#24313b")));
        painter.drawText(QRectF(center.x() - 54.0,
                                center.y() - 40.0,
                                108.0,
                                26.0),
                         Qt::AlignCenter,
                         QStringLiteral("%1%").arg(yieldPercent(), 0, 'f', 1));

        QFont captionFont = font();
        captionFont.setPointSizeF(std::max(8.5, 7.0 / scale));
        captionFont.setWeight(QFont::DemiBold);
        painter.setFont(captionFont);
        painter.setPen(QColor(QStringLiteral("#697780")));
        painter.drawText(QRectF(center.x() - 54.0,
                                center.y() - 15.0,
                                108.0,
                                20.0),
                         Qt::AlignCenter,
                         uiText("YIELD"));
    }

private:
    [[nodiscard]] double yieldPercent() const
    {
        const int total = m_passed + m_failed;
        return total > 0
            ? static_cast<double>(m_passed) * 100.0 / total
            : 0.0;
    }

    void updateProperties()
    {
        setProperty("passedCount", m_passed);
        setProperty("failedCount", m_failed);
        setProperty("yieldPercent", yieldPercent());
        setAccessibleDescription(
            QStringLiteral("%1 percent yield, %2 passed, %3 failed")
                .arg(yieldPercent(), 0, 'f', 1)
                .arg(m_passed)
                .arg(m_failed));
        setToolTip(uiText("PASS %1  |  FAIL %2  |  YIELD %3%")
                       .arg(m_passed)
                       .arg(m_failed)
                       .arg(yieldPercent(), 0, 'f', 1));
    }

    int m_passed = 0;
    int m_failed = 0;
};

} // namespace PicoATE::Ui
