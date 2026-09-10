#include "PromptCountdownWidget.h"
#include "UiLanguage.h"
#include "FunctionIconProvider.h"

#include <QDateTime>
#include <QHideEvent>
#include <QHBoxLayout>
#include <QLabel>
#include <QPainter>
#include <QProgressBar>
#include <QShowEvent>
#include <QVBoxLayout>

#include <chrono>

namespace PicoATE::Ui {
namespace {
constexpr auto DeadlineKey = "_uiPromptDeadlineMs";
constexpr auto DurationKey = "_uiPromptDurationMs";

qint64 monotonicMs()
{
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
}

QString remainingText(qint64 milliseconds)
{
    const auto tenths = (milliseconds + 99) / 100;
    return QStringLiteral("%1:%2.%3")
        .arg(tenths / 600, 2, 10, QLatin1Char('0'))
        .arg((tenths / 10) % 60, 2, 10, QLatin1Char('0'))
        .arg(tenths % 10);
}
}

PromptCountdownWidget::PromptCountdownWidget(QWidget* parent) : QWidget(parent)
{
    setObjectName(QStringLiteral("promptCountdown"));
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 4, 0, 0);
    layout->setSpacing(5);
    auto* row = new QHBoxLayout;
    row->setContentsMargins(0, 0, 0, 0);
    row->setSpacing(5);
    row->setAlignment(Qt::AlignRight);
    m_icon = new QLabel(this);
    m_icon->setObjectName(QStringLiteral("promptTimeoutIcon"));
    m_icon->setFixedSize(16, 16);
    QPixmap clockPixmap(32, 32);
    clockPixmap.fill(Qt::transparent);
    clockPixmap.setDevicePixelRatio(2);
    {
        QPainter painter(&clockPixmap);
        functionIcon("wait").paint(&painter, QRect(0, 0, 16, 16));
    }
    m_icon->setPixmap(clockPixmap);
    m_icon->hide();
    m_label = new QLabel(this);
    m_label->setObjectName(QStringLiteral("promptTimeoutLabel"));
    m_label->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    m_label->setMinimumHeight(18);
    m_label->setStyleSheet("font-size:12px;font-weight:600;color:#52636e;");
    m_progress = new QProgressBar(this);
    m_progress->setObjectName(QStringLiteral("promptTimeoutProgress"));
    m_progress->setRange(0, 1000);
    m_progress->setFixedHeight(7);
    m_progress->setTextVisible(false);
    row->addWidget(m_icon);
    row->addWidget(m_label);
    layout->addLayout(row);
    layout->addWidget(m_progress);
    m_timer.setInterval(100);
    connect(&m_timer, &QTimer::timeout, this, &PromptCountdownWidget::refresh);
    connect(&UiLanguage::instance(), &UiLanguage::languageChanged, this, &PromptCountdownWidget::refresh);
    refresh();
}

PicoATE::Core::RuntimeEvent PromptCountdownWidget::withTiming(
    PicoATE::Core::RuntimeEvent event, const PicoATE::Core::RuntimeEvent* previous)
{
    if (previous && previous->details.value("promptInstanceId") == event.details.value("promptInstanceId") &&
        previous->details.contains(DurationKey)) {
        event.details.insert(DurationKey, previous->details.value(DurationKey));
        event.details.insert(DeadlineKey, previous->details.value(DeadlineKey));
    }
    if (!event.details.contains(DurationKey)) {
        const bool notice = event.details.value("mode").toString().trimmed().toLower() == "notice";
        const auto timeout = notice ? 0 : qMax(0, event.details.value("timeoutMs", 60000).toInt());
        const qint64 age = event.timestampUtc.isValid()
            ? qMax<qint64>(0, event.timestampUtc.msecsTo(QDateTime::currentDateTimeUtc())) : 0;
        // UI-only monotonic anchors survive dialog/card moves and card rebuilds.
        event.details.insert(DurationKey, timeout);
        event.details.insert(DeadlineKey, monotonicMs() + qMax<qint64>(0, timeout - age));
    }
    return event;
}

void PromptCountdownWidget::configure(const PicoATE::Core::RuntimeEvent& source)
{
    const auto event = withTiming(source);
    m_conditionClosed = event.details.value("mode").toString().trimmed().toLower() == "notice";
    m_durationMs = event.details.value(DurationKey).toLongLong();
    m_deadlineMs = event.details.value(DeadlineKey).toLongLong();
    m_responsePending = false;
    refresh();
    updateTimer();
}

qint64 PromptCountdownWidget::remainingMs() const
{
    if (m_durationMs <= 0) return -1;
    return m_responsePending ? m_frozenRemainingMs : qBound<qint64>(0, m_deadlineMs - monotonicMs(), m_durationMs);
}

void PromptCountdownWidget::setCompact(bool compact)
{
    m_compact = compact;
    m_progress->setVisible(!compact);
    layout()->setContentsMargins(0, compact ? 0 : 4, 0, 0);
    setSizePolicy(compact ? QSizePolicy::Maximum : QSizePolicy::Expanding, QSizePolicy::Fixed);
    refresh();
}

void PromptCountdownWidget::setResponsePending(bool pending)
{
    if (pending && !m_responsePending) m_frozenRemainingMs = remainingMs();
    m_responsePending = pending;
    refresh();
    updateTimer();
}

void PromptCountdownWidget::showEvent(QShowEvent* event)
{
    QWidget::showEvent(event);
    refresh();
    updateTimer();
}

void PromptCountdownWidget::hideEvent(QHideEvent* event)
{
    m_timer.stop();
    QWidget::hideEvent(event);
}

void PromptCountdownWidget::updateTimer()
{
    if (isVisible() && m_durationMs > 0 && !m_responsePending && remainingMs() > 0) m_timer.start();
    else m_timer.stop();
}

void PromptCountdownWidget::refresh()
{
    const auto remaining = remainingMs();
    const int value = m_durationMs > 0 ? static_cast<int>(qMax<qint64>(0, remaining) * 1000 / m_durationMs) : 1000;
    m_progress->setValue(value);
    const auto seconds = (qMax<qint64>(0, remaining) + 999) / 1000;
    const auto compactTime = QStringLiteral("%1:%2")
        .arg(seconds / 60, 2, 10, QLatin1Char('0')).arg(seconds % 60, 2, 10, QLatin1Char('0'));
    m_label->setText(m_responsePending ? uiText("Response submitted") : m_conditionClosed
        ? uiText("Closes automatically later") : m_durationMs <= 0 ? uiText("No timeout") : remaining > 0
        ? (m_compact ? compactTime : uiText("Remaining %1").arg(remainingText(remaining)))
        : uiText("Awaiting timeout handling"));
    m_icon->setVisible(m_compact && m_durationMs > 0 && !m_responsePending);
    m_label->setToolTip(m_durationMs > 0 ? uiText("Remaining %1").arg(remainingText(qMax<qint64>(0, remaining))) : m_label->text());
    const int tone = m_responsePending || m_durationMs <= 0 ? 0 : value <= 100 ? 3 : value <= 250 ? 2 : 1;
    if (tone != m_tone || m_label->property("compact").toBool() != m_compact) {
        m_tone = tone;
        m_label->setProperty("compact", m_compact);
        const QString color = tone == 0 ? "#a0adb5" : tone == 1 ? "#54849a" : tone == 2 ? "#b18525" : "#b34040";
        m_label->setStyleSheet(QStringLiteral("font-size:%1px;font-weight:600;color:%2;")
            .arg(m_compact && m_durationMs > 0 && !m_responsePending ? 14 : 12)
            .arg(m_compact && tone >= 2 ? color : QStringLiteral("#52636e")));
        m_progress->setStyleSheet(QStringLiteral(
            "QProgressBar#promptTimeoutProgress{border:0;border-radius:3px;background:#dfe6ea;"
            "min-height:7px;max-height:7px;padding:0;}"
            "QProgressBar#promptTimeoutProgress::chunk{border-radius:3px;background:%1;}").arg(color));
    }
    const int labelWidth = m_label->fontMetrics().horizontalAdvance(m_label->text()) + 2;
    m_label->setMinimumWidth(m_compact ? labelWidth : 0);
    m_label->setMaximumWidth(m_compact ? labelWidth : QWIDGETSIZE_MAX);
    if (remaining == 0) m_timer.stop();
}

} // namespace PicoATE::Ui
