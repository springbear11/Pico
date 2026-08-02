#include "LoadingSpinner.h"

#include <QPainter>
#include <QTimer>

namespace PicoATE::Ui {

LoadingSpinner::LoadingSpinner(QWidget* parent)
    : QWidget(parent)
    , m_timer(new QTimer(this))
{
    setObjectName(QStringLiteral("loadingSpinner"));
    setFixedSize(28, 28);
    setAttribute(Qt::WA_TransparentForMouseEvents);
    hide();
    m_timer->setInterval(70);
    connect(m_timer, &QTimer::timeout, this, [this] {
        m_frame = (m_frame + 1) % 12;
        update();
    });
}

bool LoadingSpinner::isRunning() const
{
    return m_timer->isActive();
}

void LoadingSpinner::setColor(const QColor& color)
{
    if (!color.isValid() || m_color == color) {
        return;
    }
    m_color = color;
    update();
}

void LoadingSpinner::setRunning(bool running)
{
    if (running == isRunning()) {
        return;
    }
    if (running) {
        m_frame = 0;
        m_timer->start();
        show();
    } else {
        m_timer->stop();
        hide();
    }
    update();
}

QSize LoadingSpinner::sizeHint() const
{
    return {28, 28};
}

void LoadingSpinner::paintEvent(QPaintEvent*)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.translate(rect().center());

    constexpr int SegmentCount = 12;
    for (int segment = 0; segment < SegmentCount; ++segment) {
        const int distance = (segment - m_frame + SegmentCount) % SegmentCount;
        QColor color = m_color;
        color.setAlpha(230 - distance * 15);
        QPen pen(color, 2.4, Qt::SolidLine, Qt::RoundCap);
        painter.setPen(pen);
        painter.drawLine(QPointF(0.0, -7.0), QPointF(0.0, -11.0));
        painter.rotate(360.0 / SegmentCount);
    }
}

} // namespace PicoATE::Ui
