#include "AdminStartupSplash.h"

#include "LoadingSpinner.h"

#include <QHideEvent>
#include <QFrame>
#include <QGraphicsDropShadowEffect>
#include <QLabel>
#include <QPixmap>
#include <QShowEvent>
#include <QVBoxLayout>

namespace PicoATE::Ui {

AdminStartupSplash::AdminStartupSplash(QWidget* parent)
    : QWidget(parent, Qt::SplashScreen | Qt::FramelessWindowHint)
{
    setObjectName(QStringLiteral("adminStartupSplash"));
    setWindowTitle(tr("PicoATE"));
    setAttribute(Qt::WA_TranslucentBackground);
    setCursor(Qt::BusyCursor);
    setFixedSize(450, 300);
    setStyleSheet(QStringLiteral(
        "QFrame#adminStartupCard {"
        "  background: #ffffff;"
        "  border: 1px solid #d7dbe0;"
        "  border-radius: 7px;"
        "}"));

    auto* rootLayout = new QVBoxLayout(this);
    rootLayout->setContentsMargins(16, 16, 16, 16);
    rootLayout->setSpacing(0);

    auto* card = new QFrame(this);
    card->setObjectName(QStringLiteral("adminStartupCard"));
    auto* shadow = new QGraphicsDropShadowEffect(card);
    shadow->setBlurRadius(36.0);
    shadow->setOffset(0.0, 10.0);
    shadow->setColor(QColor(32, 38, 45, 70));
    card->setGraphicsEffect(shadow);
    rootLayout->addWidget(card);

    auto* cardLayout = new QVBoxLayout(card);
    cardLayout->setContentsMargins(24, 24, 24, 24);
    cardLayout->setSpacing(0);
    cardLayout->addStretch(1);

    auto* content = new QWidget(card);
    content->setObjectName(QStringLiteral("adminStartupContent"));
    auto* contentLayout = new QVBoxLayout(content);
    contentLayout->setContentsMargins(0, 0, 0, 0);
    contentLayout->setSpacing(16);

    m_logo = new QLabel(content);
    m_logo->setObjectName(QStringLiteral("adminStartupLogo"));
    m_logo->setAccessibleName(tr("PICO"));
    m_logo->setAlignment(Qt::AlignCenter);
    m_logo->setFixedSize(190, 155);
    const QPixmap source(QStringLiteral(
        ":/branding/PicoATE-Lockup-Vertical.png"));
    const qreal pixelRatio = devicePixelRatioF();
    auto scaled = source.scaled(
        QSize(qRound(m_logo->width() * pixelRatio),
              qRound(m_logo->height() * pixelRatio)),
        Qt::KeepAspectRatio,
        Qt::SmoothTransformation);
    scaled.setDevicePixelRatio(pixelRatio);
    m_logo->setPixmap(scaled);
    contentLayout->addWidget(m_logo, 0, Qt::AlignHCenter);

    m_spinner = new LoadingSpinner(content);
    m_spinner->setObjectName(QStringLiteral("adminStartupSplashSpinner"));
    m_spinner->setFixedSize(34, 34);
    m_spinner->setColor(QColor(QStringLiteral("#3f4a54")));
    contentLayout->addWidget(m_spinner, 0, Qt::AlignHCenter);

    cardLayout->addWidget(content, 0, Qt::AlignCenter);
    cardLayout->addStretch(1);
}

void AdminStartupSplash::showEvent(QShowEvent* event)
{
    QWidget::showEvent(event);
    m_spinner->setRunning(true);
}

void AdminStartupSplash::hideEvent(QHideEvent* event)
{
    m_spinner->setRunning(false);
    QWidget::hideEvent(event);
}

} // namespace PicoATE::Ui
