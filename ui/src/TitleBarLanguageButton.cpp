#include "TitleBarLanguageButton.h"

#include "UiLanguage.h"

#include <QApplication>
#include <QDialog>
#include <QEvent>
#include <QMainWindow>
#include <QMenuBar>
#include <QMessageBox>
#include <QPainter>
#include <QWindow>
#include <QtMath>

#ifdef Q_OS_WIN
#include <qt_windows.h>
#include <dwmapi.h>
#endif

namespace PicoATE::Ui {

TitleBarLanguageButton::TitleBarLanguageButton(QMainWindow* owner)
    : QToolButton(owner), m_owner(owner)
{
    setObjectName(QStringLiteral("uiLanguageButton"));
    setFocusPolicy(Qt::NoFocus);
    setAutoRaise(true);
    setCheckable(true);
    setFixedSize(52, 24);
    setAttribute(Qt::WA_Hover);

#ifdef Q_OS_WIN
    m_nativeCaption = QGuiApplication::platformName() == QStringLiteral("windows");
#endif
    if (m_nativeCaption) {
        // An owned, non-activating caption surface leaves the OS frame, snapping,
        // resize hit-testing and native caption buttons entirely unchanged.
        setWindowFlags(Qt::Tool | Qt::FramelessWindowHint |
                       Qt::WindowDoesNotAcceptFocus | Qt::NoDropShadowWindowHint);
        setAttribute(Qt::WA_ShowWithoutActivating);
        setAttribute(Qt::WA_QuitOnClose, false);
        setAttribute(Qt::WA_TranslucentBackground);
        qApp->installEventFilter(this);
        m_positionUpdate.setSingleShot(true);
        connect(&m_positionUpdate, &QTimer::timeout,
                this, &TitleBarLanguageButton::synchronizeCaption);
    } else {
        owner->menuBar()->setCornerWidget(this, Qt::TopRightCorner);
    }

    m_animation.setObjectName(QStringLiteral("uiLanguageAnimation"));
    m_animation.setDuration(170);
    m_animation.setEasingCurve(QEasingCurve::InOutCubic);
    connect(&m_animation, &QVariantAnimation::valueChanged,
            this, [this](const QVariant& value) {
        m_chineseProgress = value.toReal();
        setProperty("languageProgress", m_chineseProgress);
        update();
    });
    connect(&UiLanguage::instance(), &UiLanguage::languageChanged,
            this, [this] { refreshLanguage(true); });
    connect(this, &QToolButton::clicked, owner, [this, owner] {
        auto& language = UiLanguage::instance();
        if (!language.setChinese(!language.isChinese())) {
            refreshLanguage(false);
            QMessageBox::warning(owner, QStringLiteral("Language"),
                                 QStringLiteral("Unable to load UI translations."));
        }
    });
    refreshLanguage(false);
}

void TitleBarLanguageButton::refreshLanguage(bool animate)
{
    const bool chinese = UiLanguage::instance().isChinese();
    setText(chinese ? QString::fromUtf8("\xe4\xb8\xad") : QStringLiteral("EN"));
    setChecked(chinese);
    setToolTip(chinese ? QStringLiteral("Switch to English")
                      : QString::fromUtf8("\xe5\x88\x87\xe6\x8d\xa2\xe5\x88\xb0\xe4\xb8\xad\xe6\x96\x87"));
    setAccessibleName(toolTip());
    m_animation.stop();
    const qreal target = chinese ? 1 : 0;
    if (animate && isVisible()) {
        m_animation.setStartValue(m_chineseProgress);
        m_animation.setEndValue(target);
        m_animation.start();
    } else {
        m_chineseProgress = target;
        setProperty("languageProgress", target);
        update();
    }
}

bool TitleBarLanguageButton::eventFilter(QObject* watched, QEvent* event)
{
    if (event->type() == QEvent::ApplicationStateChange ||
        event->type() == QEvent::WindowActivate || event->type() == QEvent::WindowDeactivate) {
        m_positionUpdate.start(0);
    }
    if (watched == m_owner) {
        switch (event->type()) {
        case QEvent::Hide:
            hide();
            break;
        case QEvent::Move:
        case QEvent::Resize:
            synchronizeCaption();
            break;
        case QEvent::Show:
        case QEvent::WindowStateChange:
        case QEvent::WinIdChange:
        case QEvent::ScreenChangeInternal:
        case QEvent::DevicePixelRatioChange:
        case QEvent::WindowActivate:
        case QEvent::WindowDeactivate:
        case QEvent::ApplicationPaletteChange:
        case QEvent::EnabledChange:
            m_positionUpdate.start(0);
            break;
        default:
            break;
        }
    } else if ((event->type() == QEvent::Show || event->type() == QEvent::Hide) &&
               qobject_cast<QDialog*>(watched)) {
        m_positionUpdate.start(0);
    }
    return QToolButton::eventFilter(watched, event);
}

void TitleBarLanguageButton::synchronizeCaption()
{
#ifdef Q_OS_WIN
    if (!m_nativeCaption) return;
    const auto* active = QApplication::activeWindow();
    const bool ownerActive = active && (active == m_owner || m_owner->isAncestorOf(active));
    if (!ownerActive || !m_owner->isVisible() || m_owner->isMinimized() || m_owner->isFullScreen() ||
        m_owner->testAttribute(Qt::WA_DontShowOnScreen) || QApplication::activeModalWidget()) {
        hide();
        return;
    }
    const auto ownerHwnd = reinterpret_cast<HWND>(m_owner->winId());
    if (m_styledOwner != m_owner->winId()) {
        // Windows 11 supports native caption colors without replacing its frame.
        const COLORREF background = RGB(243, 243, 243);
        const COLORREF foreground = RGB(37, 43, 48);
        DwmSetWindowAttribute(ownerHwnd, DWMWA_CAPTION_COLOR, &background, sizeof(background));
        DwmSetWindowAttribute(ownerHwnd, DWMWA_TEXT_COLOR, &foreground, sizeof(foreground));
        m_styledOwner = m_owner->winId();
    }
    RECT frame{};
    RECT buttons{};
    POINT clientTop{0, 0};
    if (!GetWindowRect(ownerHwnd, &frame) || !ClientToScreen(ownerHwnd, &clientTop)) {
        hide();
        return;
    }
    const qreal scale = m_owner->devicePixelRatioF();
    if (FAILED(DwmGetWindowAttribute(ownerHwnd, DWMWA_CAPTION_BUTTON_BOUNDS,
                                     &buttons, sizeof(buttons))) ||
        buttons.right <= buttons.left || buttons.bottom <= buttons.top) {
        TITLEBARINFOEX info{};
        info.cbSize = sizeof(info);
        SendMessage(ownerHwnd, WM_GETTITLEBARINFOEX, 0, reinterpret_cast<LPARAM>(&info));
        buttons = info.rgrect[2]; // Native minimize button, in screen coordinates.
        OffsetRect(&buttons, -frame.left, -frame.top);
    }
    const int captionTop = frame.top + buttons.top;
    const int bottom = qMin(int(frame.top + buttons.bottom), int(clientTop.y));
    const int nativeWidth = qRound(52 * scale);
    const int edgeInset = qMax(1, qRound(scale));
    const int logicalHeight = qMin(24, qFloor((bottom - captionTop - 2 * edgeInset) / scale));
    const int nativeHeight = qRound(logicalHeight * scale);
    const int top = captionTop + (bottom - captionTop - nativeHeight) / 2;
    const int left = frame.left + buttons.left - nativeWidth - qRound(8 * scale);
    if (buttons.left <= 0 || logicalHeight < 16 || left <= frame.left) {
        hide();
        return;
    }

    setEnabled(m_owner->isEnabled());
    setFixedSize(52, logicalHeight);
    if (!windowHandle()) {
        const auto buttonHwnd = reinterpret_cast<HWND>(winId());
        const COLORREF border = DWMWA_COLOR_NONE;
        DwmSetWindowAttribute(buttonHwnd, DWMWA_BORDER_COLOR, &border, sizeof(border));
    }
    windowHandle()->setScreen(m_owner->screen());
    windowHandle()->setTransientParent(m_owner->windowHandle());
    SetWindowPos(reinterpret_cast<HWND>(winId()), nullptr,
                 left, top, nativeWidth, nativeHeight,
                 SWP_NOACTIVATE | SWP_NOZORDER | SWP_NOOWNERZORDER);
    if (!isVisible()) show();
    update();
#endif
}

void TitleBarLanguageButton::paintEvent(QPaintEvent*)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    if (m_nativeCaption) {
        painter.setCompositionMode(QPainter::CompositionMode_Source);
        painter.fillRect(rect(), Qt::transparent);
        painter.setCompositionMode(QPainter::CompositionMode_SourceOver);
    } else {
        painter.fillRect(rect(), palette().window());
    }
    const auto blend = [this](const QColor& from, const QColor& to) {
        const qreal amount = m_chineseProgress;
        return QColor::fromRgbF(from.redF() * (1 - amount) + to.redF() * amount,
                                from.greenF() * (1 - amount) + to.greenF() * amount,
                                from.blueF() * (1 - amount) + to.blueF() * amount);
    };
    const bool hovered = underMouse() && isEnabled();
    const QColor light = isDown() ? QColor("#e5e8ec")
                                  : QColor(hovered ? "#f2f3f5" : "#ffffff");
    const QColor dark = QColor(isDown() ? "#24272b" : "#34383e");
    const QColor background = blend(light, dark);
    const QColor border = blend(QColor(hovered ? "#9fa6ae" : "#c9cdd3"), dark);
    const QRectF pill = QRectF(rect()).adjusted(1, 1, -1, -1);
    painter.setOpacity(isEnabled() ? 1.0 : 0.5);
    painter.setPen(QPen(border, 1));
    painter.setBrush(background);
    painter.drawRoundedRect(pill, pill.height() / 2, pill.height() / 2);
    QFont labelFont = font();
    labelFont.setPixelSize(qMin(13, height() - 5));
    labelFont.setWeight(QFont::Bold);
    painter.setFont(labelFont);
    painter.setPen(background.lightnessF() < 0.55 ? Qt::white : QColor("#34383e"));
    painter.drawText(pill, Qt::AlignCenter, text());
}

} // namespace PicoATE::Ui
