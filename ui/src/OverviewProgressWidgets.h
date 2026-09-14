#pragma once

#include "RunnerModels.h"
#include "UiLanguage.h"
#include "FunctionIconProvider.h"

#include <QAbstractButton>
#include <QLabel>
#include <QLinearGradient>
#include <QPainter>
#include <QPainterPath>
#include <QPaintEvent>
#include <QPolygonF>
#include <QPixmap>

namespace PicoATE::Ui {

inline QColor overviewResultColor(bool passed)
{
    return QColor(passed ? "#2f7548" : "#a43838");
}

inline QPixmap softenedOverviewSnapshot(QWidget* surface)
{
    if (!surface || !surface->isVisible()) return {};
    const auto source = surface->grab();
    return source.scaled(qMax(1, source.width() / 12), qMax(1, source.height() / 12),
                         Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
}

class BatchStatusProgressLabel final : public QLabel
{
public:
    explicit BatchStatusProgressLabel(QWidget* parent = nullptr) : QLabel(parent)
    {
        setStyleSheet(QStringLiteral("font-size:22px;font-weight:700;"));
        setMinimumSize(170, 88);
        updateStateIcon();
        refreshProperties();
        connect(&UiLanguage::instance(), &UiLanguage::languageChanged, this, [this] {
            refreshProperties();
            update();
        });
    }

    void setState(UiRunState state, bool stopped)
    {
        const auto next = stopped && state == UiRunState::Completed ? UiRunState::Failed : state;
        if (m_state != next) {
            m_state = next;
            updateStateIcon();
        }
        refreshProperties();
        update();
    }

    void setStepCounts(qint64 completed, qint64 total)
    {
        total = qMax<qint64>(0, total);
        completed = qBound<qint64>(0, completed, total);
        if (completed == m_completed && total == m_total) return;
        m_completed = completed;
        m_total = total;
        refreshProperties();
        update();
    }

    QSize sizeHint() const override { return {210, 90}; }
    QSize minimumSizeHint() const override { return {170, 88}; }

    QString titleText() const { return uiText("OVERALL TEST STATUS"); }
    QString progressCaption() const { return uiText("TOTAL PROGRESS"); }
    QRectF titleRect() const { return contentLayout().title; }
    QRectF stateTextRect() const { return contentLayout().state; }
    QRectF percentageRect() const { return contentLayout().percent; }
    QRectF progressTrackRect() const { return contentLayout().track; }
    QRectF stateIconRect() const { return contentLayout().icon; }
    QColor accentColor() const
    {
        switch (m_state) {
        case UiRunState::Starting:
        case UiRunState::Running:
        case UiRunState::Stopping: return QColor("#e5ac24");
        case UiRunState::Pausing:
        case UiRunState::Paused: return QColor("#3d7898");
        case UiRunState::Completed: return overviewResultColor(true);
        case UiRunState::Failed:
        case UiRunState::CompileFailed: return overviewResultColor(false);
        default: return QColor("#81909a");
        }
    }

    QColor surfaceColor() const
    {
        switch (m_state) {
        case UiRunState::Starting:
        case UiRunState::Running:
        case UiRunState::Stopping: return QColor("#fffdf5");
        case UiRunState::Pausing:
        case UiRunState::Paused: return QColor("#f4f9fc");
        case UiRunState::Completed: return QColor("#f4faf6");
        case UiRunState::Failed:
        case UiRunState::CompileFailed: return QColor("#fff6f6");
        default: return QColor("#f7f9fa");
        }
    }

protected:
    void paintEvent(QPaintEvent*) override
    {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.setRenderHint(QPainter::SmoothPixmapTransform);
        const auto accent = accentColor();
        const QRectF box = QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5);
        QPainterPath path;
        path.addRoundedRect(box, 6, 6);
        painter.fillPath(path, surfaceColor());
        painter.save();
        painter.setClipPath(path);
        painter.fillRect(QRectF(box.left(), box.top(), 4, box.height()), accent);
        painter.restore();

        const auto boxes = contentLayout();
        painter.setPen(QColor("#344048"));
        painter.setFont(sizedFont(12, QFont::DemiBold));
        painter.drawText(boxes.title, Qt::AlignLeft | Qt::AlignVCenter,
                         painter.fontMetrics().elidedText(titleText(), Qt::ElideRight, int(boxes.title.width())));
        painter.drawPixmap(boxes.icon.toRect(), m_stateIcon);
        painter.setFont(boxes.stateFont);
        painter.setPen(QColor("#20272b"));
        painter.drawText(boxes.state, Qt::AlignLeft | Qt::AlignVCenter,
                         painter.fontMetrics().elidedText(text(), Qt::ElideRight, int(boxes.state.width())));
        painter.setFont(sizedFont(width() < 220 ? 14 : 16, QFont::DemiBold));
        painter.drawText(boxes.percent, Qt::AlignRight | Qt::AlignVCenter, percentageText());
        painter.setFont(sizedFont(10, QFont::DemiBold));
        painter.setPen(QColor("#65737c"));
        painter.drawText(boxes.progressCaption, Qt::AlignLeft | Qt::AlignVCenter, progressCaption());

        QPainterPath track;
        track.addRoundedRect(boxes.track, 3, 3);
        painter.fillPath(track, QColor("#e8ecef"));
        painter.save();
        painter.setClipPath(track);
        const qreal fraction = m_total > 0 ? qreal(m_completed) / m_total : 0;
        painter.fillRect(QRectF(boxes.track.topLeft(), QSizeF(boxes.track.width() * fraction, boxes.track.height())), accent);
        painter.restore();
    }

private:
    struct ContentLayout {
        QRectF title, icon, state, percent, progressCaption, track;
        QFont stateFont;
    };

    QFont sizedFont(int pixels, QFont::Weight weight) const
    {
        auto value = font();
        value.setPixelSize(pixels);
        value.setWeight(weight);
        return value;
    }

    QString percentageText() const { return QStringLiteral("%1%").arg(percentValue()); }

    ContentLayout contentLayout() const
    {
        const bool narrow = width() < 220;
        const QRectF inner = QRectF(rect()).adjusted(narrow ? 14 : 18, 8, narrow ? -10 : -12, -9);
        const QRectF title(inner.left(), inner.top(), inner.width(), 18);
        const QRectF bottom(inner.left(), inner.bottom() - 16, inner.width(), 16);
        const QRectF middle(inner.left(), title.bottom() + 3, inner.width(), bottom.top() - title.bottom() - 7);
        const int iconSize = narrow ? 20 : 26;
        const QRectF icon(middle.left(), middle.center().y() - iconSize / 2.0, iconSize, iconSize);
        const int percentWidth = QFontMetrics(sizedFont(narrow ? 14 : 16, QFont::DemiBold))
                                     .horizontalAdvance(percentageText()) + 2;
        const QRectF percent(middle.right() - percentWidth, middle.top(), percentWidth, middle.height());
        const int stateLeft = qRound(icon.right()) + (narrow ? 6 : 10);
        const QRectF state(stateLeft, middle.top(), qMax(1, qRound(percent.left()) - (narrow ? 6 : 12) - stateLeft), middle.height());
        auto stateFont = sizedFont(24, QFont::Bold);
        int pixels = 24;
        while (pixels > 12 && (QFontMetrics(stateFont).horizontalAdvance(text()) > state.width() ||
                               QFontMetrics(stateFont).height() > state.height()))
            stateFont.setPixelSize(--pixels);
        const int captionWidth = QFontMetrics(sizedFont(10, QFont::DemiBold)).horizontalAdvance(progressCaption()) + 2;
        const QRectF caption(bottom.left(), bottom.top(), captionWidth, bottom.height());
        const QRectF track(caption.right() + 8, bottom.center().y() - 3,
                           qMax(1.0, bottom.right() - caption.right() - 8), 6);
        return {title, icon, state, percent, caption, track, stateFont};
    }

    void updateStateIcon()
    {
        QIcon icon;
        switch (m_state) {
        case UiRunState::Completed: icon = QIcon(":/icons/circle-check.svg"); break;
        case UiRunState::Failed:
        case UiRunState::CompileFailed: icon = QIcon(":/icons/circle-x.svg"); break;
        case UiRunState::Pausing:
        case UiRunState::Paused: icon = QIcon(":/icons/pause.svg"); break;
        case UiRunState::Stopping: icon = QIcon(":/icons/square.svg"); break;
        default: icon = functionIcon(QStringLiteral("wait")); break;
        }
        m_stateIcon = QPixmap(48, 48);
        m_stateIcon.setDevicePixelRatio(2.0);
        m_stateIcon.fill(Qt::transparent);
        QPainter painter(&m_stateIcon);
        icon.paint(&painter, QRect(0, 0, 24, 24));
        painter.setCompositionMode(QPainter::CompositionMode_SourceIn);
        painter.fillRect(QRect(0, 0, 24, 24), accentColor());
    }

    int percentValue() const { return m_total > 0 ? int(m_completed * 100 / m_total) : 0; }
    void refreshProperties()
    {
        setProperty("completedSteps", m_completed);
        setProperty("totalSteps", m_total);
        setProperty("progressPercent", percentValue());
        setProperty("statusTitle", titleText());
        const auto description = QStringLiteral("%1: %2  %3%  (%4 / %5)")
            .arg(titleText(), text()).arg(percentValue()).arg(m_completed).arg(m_total);
        setToolTip(description);
        setAccessibleName(description);
    }

    UiRunState m_state = UiRunState::Empty;
    QPixmap m_stateIcon;
    qint64 m_completed = 0;
    qint64 m_total = 0;
};

class UutResultOverlay final : public QAbstractButton
{
public:
    explicit UutResultOverlay(QWidget* parent = nullptr) : QAbstractButton(parent)
    {
        setObjectName(QStringLiteral("uutOverviewResultOverlay"));
        setCursor(Qt::PointingHandCursor);
        setFocusPolicy(Qt::NoFocus);
        hide();
        connect(&UiLanguage::instance(), &UiLanguage::languageChanged,
                this, [this] { refreshText(); });
    }

    void setResult(bool passed, const QString& uutId, const QString& serialNumber = {})
    {
        const auto serial = serialNumber.trimmed();
        const bool changed = m_passed != passed || m_uutId != uutId || m_serialNumber != serial;
        if (!changed) return;
        setDown(false);
        m_passed = passed;
        m_uutId = uutId;
        m_serialNumber = serial;
        refreshText();
        update();
    }

    QString contextText() const
    {
        return m_serialNumber.isEmpty() ? m_uutId
            : m_uutId + QStringLiteral(" - ") + m_serialNumber;
    }

    QRect contextRect() const { return textLayout().context; }
    QRect resultTextRect() const { return textLayout().result; }

    QRect panelRect() const
    {
        const int panelWidth = qMax(1, qMin(300, qRound(width() * 0.60)));
        const int panelHeight = qMax(1, qMin(144, qRound(height() * 0.44)));
        return QRect((width() - panelWidth) / 2, (height() - panelHeight) / 2,
                     panelWidth, panelHeight);
    }

    void setBackdrop(const QPixmap& backdrop)
    {
        m_backdrop = backdrop;
        update();
    }

protected:
    void paintEvent(QPaintEvent*) override
    {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.setRenderHint(QPainter::SmoothPixmapTransform);
        if (!m_backdrop.isNull()) painter.drawPixmap(rect(), m_backdrop);
        painter.fillRect(rect(), QColor(231, 239, 245, 160));
        painter.setPen(Qt::NoPen);
        painter.setBrush(overviewResultColor(m_passed));
        const auto panel = panelRect();
        painter.drawRoundedRect(QRectF(panel), 7, 7);
        const auto textBoxes = textLayout();
        painter.setPen(Qt::white);
        painter.setFont(contextFont());
        painter.drawText(textBoxes.context, Qt::AlignCenter,
                         QFontMetrics(contextFont()).elidedText(contextText(), Qt::ElideRight,
                                                               textBoxes.context.width()));
        painter.setFont(textBoxes.resultFont);
        painter.drawText(textBoxes.result, Qt::AlignCenter, text());
    }

private:
    struct TextLayout {
        QRect context;
        QRect result;
        QFont resultFont;
    };

    QFont contextFont() const
    {
        auto value = font();
        value.setPixelSize(12);
        value.setWeight(QFont::Medium);
        return value;
    }

    TextLayout textLayout() const
    {
        const auto content = panelRect().adjusted(24, 20, -24, -20);
        const int contextHeight = QFontMetrics(contextFont()).height();
        constexpr int gap = 4;
        auto resultFont = font();
        resultFont.setBold(true);
        int pixels = 52;
        resultFont.setPixelSize(pixels);
        while (pixels > 24 &&
               (QFontMetrics(resultFont).horizontalAdvance(text()) > content.width() ||
                QFontMetrics(resultFont).height() + contextHeight + gap > content.height())) {
            resultFont.setPixelSize(--pixels);
        }
        const int resultHeight = QFontMetrics(resultFont).height();
        const int top = content.top() + (content.height() - contextHeight - gap - resultHeight) / 2;
        return {QRect(content.left(), top, content.width(), contextHeight),
                QRect(content.left(), top + contextHeight + gap, content.width(), resultHeight), resultFont};
    }

    void refreshText()
    {
        const auto label = uiText(m_passed ? "PASS" : "FAIL");
        if (text() != label) setText(label);
        setAccessibleName(contextText() + QStringLiteral(" ") + label);
        setToolTip(contextText().toHtmlEscaped());
        setProperty("resultContext", contextText());
        setProperty("resultPassed", m_passed);
        setProperty("resultColor", overviewResultColor(m_passed));
    }

    bool m_passed = false;
    QString m_uutId;
    QString m_serialNumber;
    QPixmap m_backdrop;
};

class UutProgressRing final : public QWidget
{
public:
    explicit UutProgressRing(QWidget* parent = nullptr) : QWidget(parent)
    {
        setObjectName(QStringLiteral("uutOverviewProgressRing"));
        setFixedSize(112, 112);
        setAttribute(Qt::WA_TransparentForMouseEvents);
        refreshProperties();
        connect(&UiLanguage::instance(), &UiLanguage::languageChanged,
                this, [this] { refreshProperties(); });
    }

    void setProgress(int completed, int total, UutOverviewState state)
    {
        total = qMax(0, total);
        completed = qBound(0, completed, total);
        if (completed == m_completed && total == m_total && state == m_state) return;
        m_completed = completed;
        m_total = total;
        m_state = state;
        refreshProperties();
        update();
    }

protected:
    void paintEvent(QPaintEvent*) override
    {
        const auto color = accent();
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        const auto ring = ringRect();
        const bool terminal = !resultMark().isEmpty();
        const bool disabled = m_state == UutOverviewState::Disabled;

        // Static translucent layers suggest frosted glass without sampling the live UI.
        painter.setPen(Qt::NoPen);
        painter.setBrush(QColor(40, 58, 70, disabled ? 3 : 8));
        painter.drawEllipse(ring.translated(0, 1.5));
        QLinearGradient glass(ring.topLeft(), ring.bottomLeft());
        glass.setColorAt(0, QColor(255, 255, 255, disabled ? 110 : 205));
        glass.setColorAt(0.55, QColor(251, 253, 255, disabled ? 65 : 140));
        glass.setColorAt(1, QColor(234, 242, 248, disabled ? 50 : 110));
        painter.setBrush(glass);
        painter.drawEllipse(ring);

        painter.save();
        if (terminal) {
            QPainterPath outsideMark;
            outsideMark.addRect(rect());
            QPainterPath slot;
            slot.addEllipse(resultMarkRect().adjusted(-1, -1, 1, 1));
            painter.setClipPath(outsideMark.subtracted(slot));
        }
        painter.setBrush(Qt::NoBrush);
        painter.setPen(QPen(QColor(255, 255, 255, 185), 1));
        painter.drawEllipse(ring.adjusted(-2.5, -2.5, 2.5, 2.5));
        QPen pen(QColor(210, 220, 228, 210), 3.5, Qt::SolidLine, Qt::RoundCap);
        painter.setPen(pen);
        painter.drawEllipse(ring);
        if (m_total > 0 && m_completed > 0 && !disabled) {
            pen.setColor(color);
            pen.setWidthF(4);
            painter.setPen(pen);
            painter.drawArc(ring, 90 * 16, -qRound(360.0 * 16 * m_completed / m_total));
        }
        painter.setPen(QPen(QColor(255, 255, 255, 175), 1));
        painter.drawArc(ring.adjusted(3.5, 3.5, -3.5, -3.5), 20 * 16, 140 * 16);
        painter.restore();

        auto counts = centerText();
        QFont countFont = font();
        countFont.setWeight(QFont::DemiBold);
        int pixels = 16;
        countFont.setPixelSize(pixels);
        while (pixels > 11 && QFontMetrics(countFont).horizontalAdvance(counts) > ring.width() - 18) {
            countFont.setPixelSize(--pixels);
        }
        if (QFontMetrics(countFont).horizontalAdvance(counts) > ring.width() - 18) {
            counts = QStringLiteral("%1\n/ %2").arg(m_completed).arg(m_total);
        }
        painter.setFont(countFont);
        painter.setPen(disabled ? QColor("#8b979f") : QColor("#34464f"));
        painter.drawText(ring.adjusted(9, 0, -9, 0), Qt::AlignCenter, counts);

        if (terminal) {
            const auto mark = resultMarkRect().center();
            painter.setPen(QPen(color, 2.8, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
            if (m_state == UutOverviewState::Passed) {
                painter.drawPolyline(QPolygonF({mark + QPointF(-7.2, 0),
                                                mark + QPointF(-1.2, 6),
                                                mark + QPointF(8.4, -6)}));
            } else {
                painter.drawLine(mark + QPointF(-6, -6), mark + QPointF(6, 6));
                painter.drawLine(mark + QPointF(6, -6), mark + QPointF(-6, 6));
            }
        }
    }

private:
    QRectF ringRect() const { return QRectF(rect()).adjusted(8, 8, -8, -8); }
    QRectF resultMarkRect() const
    {
        const auto ring = ringRect();
        const auto center = ring.center() + QPointF(ring.width() * 0.4330127019, -ring.height() * 0.25);
        return QRectF(center - QPointF(12, 12), QSizeF(24, 24));
    }
    QColor accent() const
    {
        switch (m_state) {
        case UutOverviewState::Running: return QColor("#a87500");
        case UutOverviewState::Paused: return QColor("#35677f");
        case UutOverviewState::Passed: return overviewResultColor(true);
        case UutOverviewState::Failed:
        case UutOverviewState::Stopped: return overviewResultColor(false);
        default: return QColor("#7d888f");
        }
    }
    QString centerText() const
    {
        if (m_state == UutOverviewState::Disabled) return QStringLiteral("--");
        return QStringLiteral("%1 / %2").arg(m_completed).arg(m_total);
    }
    QString resultMark() const
    {
        if (m_state == UutOverviewState::Passed) return QString(QChar(0x2713));
        if (m_state == UutOverviewState::Failed || m_state == UutOverviewState::Stopped) return QString(QChar(0x00d7));
        return {};
    }
    void refreshProperties()
    {
        setProperty("completedSteps", m_completed);
        setProperty("totalSteps", m_total);
        setProperty("centerText", centerText());
        setProperty("resultMark", resultMark());
        setProperty("resultMarkRect", resultMark().isEmpty() ? QRectF{} : resultMarkRect());
        setProperty("ringState", int(m_state));
        setProperty("accentColor", accent());
        setProperty("progressPercent", m_total > 0 ? int(qint64(m_completed) * 100 / m_total) : 0);
        const auto description = QStringLiteral("%1: %2 / %3")
            .arg(uiStateText(uutOverviewStateName(m_state))).arg(m_completed).arg(m_total);
        setAccessibleName(description);
        setToolTip(description);
    }

    int m_completed = 0;
    int m_total = 0;
    UutOverviewState m_state = UutOverviewState::Waiting;
};

} // namespace PicoATE::Ui
