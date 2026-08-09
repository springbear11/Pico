#include "FunctionIconProvider.h"

#include "PicoATE/Core/ExecutionReport.h"

#include <QHash>
#include <QIconEngine>
#include <QPainter>
#include <QPolygonF>

#include <algorithm>
#include <utility>

namespace PicoATE::Ui {

namespace {

QString normalized(QString value)
{
    value = value.trimmed().toLower();
    value.remove('-');
    value.remove('_');
    value.remove(' ');
    return value;
}

QString iconKeyForModule(const QString& moduleId)
{
    if (moduleId.compare(QStringLiteral("builtin.data-parser"),
                         Qt::CaseInsensitive) == 0) {
        return QStringLiteral("parser");
    }
    if (moduleId.compare(QStringLiteral("builtin.value-tools"),
                         Qt::CaseInsensitive) == 0) {
        return QStringLiteral("calculator");
    }
    if (moduleId.startsWith(QStringLiteral("builtin."),
                            Qt::CaseInsensitive)) {
        return QStringLiteral("basic");
    }
    return QStringLiteral("plugin");
}

QString iconKeyForKind(const QString& kind)
{
    const auto value = normalized(kind);
    if (value == QStringLiteral("wait")) return QStringLiteral("wait");
    if (value == QStringLiteral("operatorprompt")) return QStringLiteral("message");
    if (value == QStringLiteral("limit")) return QStringLiteral("limit");
    if (value == QStringLiteral("testitem")) return QStringLiteral("test-item");
    if (value == QStringLiteral("loop")) return QStringLiteral("loop");
    if (value == QStringLiteral("break")) return QStringLiteral("break");
    if (value == QStringLiteral("counter")) return QStringLiteral("counter");
    if (value == QStringLiteral("aggregate")) return QStringLiteral("aggregate");
    if (value == QStringLiteral("barrier")) return QStringLiteral("barrier");
    if (value == QStringLiteral("noop")) return QStringLiteral("noop");
    if (value == QStringLiteral("action") || value == QStringLiteral("cleanup")) {
        return QStringLiteral("plugin");
    }
    return QStringLiteral("basic");
}

QString iconKeyForKind(PicoATE::Core::ExecNodeKind kind)
{
    using PicoATE::Core::ExecNodeKind;
    switch (kind) {
    case ExecNodeKind::Wait: return QStringLiteral("wait");
    case ExecNodeKind::OperatorPrompt: return QStringLiteral("message");
    case ExecNodeKind::Limit: return QStringLiteral("limit");
    case ExecNodeKind::TestItem: return QStringLiteral("test-item");
    case ExecNodeKind::Loop: return QStringLiteral("loop");
    case ExecNodeKind::Break: return QStringLiteral("break");
    case ExecNodeKind::Counter: return QStringLiteral("counter");
    case ExecNodeKind::Aggregate: return QStringLiteral("aggregate");
    case ExecNodeKind::Barrier: return QStringLiteral("barrier");
    case ExecNodeKind::Noop: return QStringLiteral("noop");
    case ExecNodeKind::Action:
    case ExecNodeKind::Cleanup:
        return QStringLiteral("plugin");
    case ExecNodeKind::Statement:
    case ExecNodeKind::SequenceCall:
        return QStringLiteral("basic");
    }
    return QStringLiteral("basic");
}

void paintFunctionGlyph(QPainter& painter, const QString& key)
{
    const QColor color(key == QStringLiteral("plugin")
                           ? QStringLiteral("#5d6570")
                           : QStringLiteral("#47758b"));
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setPen(QPen(color, 1.35, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    painter.setBrush(Qt::NoBrush);

    if (key == QStringLiteral("wait")) {
        painter.drawEllipse(QRectF(2.25, 2.25, 11.5, 11.5));
        painter.drawLine(QPointF(8.0, 4.5), QPointF(8.0, 8.0));
        painter.drawLine(QPointF(8.0, 8.0), QPointF(10.6, 9.4));
    } else if (key == QStringLiteral("message")) {
        painter.drawRoundedRect(QRectF(1.75, 2.5, 12.5, 9.5), 2.0, 2.0);
        painter.drawLine(QPointF(5.0, 12.0), QPointF(3.7, 14.0));
        painter.drawLine(QPointF(4.6, 6.0), QPointF(11.4, 6.0));
        painter.drawLine(QPointF(4.6, 8.7), QPointF(9.4, 8.7));
    } else if (key == QStringLiteral("limit")) {
        painter.drawEllipse(QRectF(2.25, 2.25, 11.5, 11.5));
        painter.drawLine(QPointF(4.8, 8.1), QPointF(7.0, 10.2));
        painter.drawLine(QPointF(7.0, 10.2), QPointF(11.4, 5.5));
    } else if (key == QStringLiteral("test-item")) {
        painter.drawRoundedRect(QRectF(2.0, 2.0, 12.0, 12.0), 1.5, 1.5);
        for (const qreal y : {5.0, 8.0, 11.0}) {
            painter.drawEllipse(QRectF(4.0, y - 0.6, 1.2, 1.2));
            painter.drawLine(QPointF(7.0, y), QPointF(11.7, y));
        }
    } else if (key == QStringLiteral("loop")) {
        painter.drawArc(QRectF(2.0, 3.0, 12.0, 9.0), 35 * 16, 145 * 16);
        painter.drawArc(QRectF(2.0, 4.0, 12.0, 9.0), 215 * 16, 145 * 16);
        painter.drawLine(QPointF(12.7, 3.8), QPointF(13.8, 6.0));
        painter.drawLine(QPointF(12.7, 3.8), QPointF(10.4, 4.1));
        painter.drawLine(QPointF(3.3, 12.2), QPointF(2.2, 10.0));
        painter.drawLine(QPointF(3.3, 12.2), QPointF(5.6, 11.9));
    } else if (key == QStringLiteral("break")) {
        painter.drawRoundedRect(QRectF(3.0, 3.0, 10.0, 10.0), 1.5, 1.5);
        painter.drawLine(QPointF(5.5, 8.0), QPointF(10.5, 8.0));
    } else if (key == QStringLiteral("counter")) {
        painter.drawLine(QPointF(5.4, 2.5), QPointF(4.0, 13.5));
        painter.drawLine(QPointF(11.4, 2.5), QPointF(10.0, 13.5));
        painter.drawLine(QPointF(2.5, 6.0), QPointF(13.5, 6.0));
        painter.drawLine(QPointF(2.5, 10.0), QPointF(13.5, 10.0));
    } else if (key == QStringLiteral("aggregate")) {
        painter.drawLine(QPointF(2.5, 13.5), QPointF(13.5, 13.5));
        painter.drawLine(QPointF(2.5, 13.5), QPointF(2.5, 3.0));
        painter.drawRect(QRectF(4.2, 9.0, 2.0, 4.5));
        painter.drawRect(QRectF(7.3, 6.3, 2.0, 7.2));
        painter.drawRect(QRectF(10.4, 3.5, 2.0, 10.0));
    } else if (key == QStringLiteral("barrier")) {
        painter.drawLine(QPointF(8.0, 2.0), QPointF(8.0, 14.0));
        painter.drawLine(QPointF(2.0, 4.0), QPointF(6.0, 7.0));
        painter.drawLine(QPointF(2.0, 12.0), QPointF(6.0, 9.0));
        painter.drawLine(QPointF(10.0, 8.0), QPointF(14.0, 8.0));
        painter.drawLine(QPointF(12.1, 6.2), QPointF(14.0, 8.0));
        painter.drawLine(QPointF(12.1, 9.8), QPointF(14.0, 8.0));
    } else if (key == QStringLiteral("parser")) {
        painter.drawPolyline(QPolygonF({QPointF(5.6, 3.0), QPointF(2.2, 8.0),
                                        QPointF(5.6, 13.0)}));
        painter.drawPolyline(QPolygonF({QPointF(10.4, 3.0), QPointF(13.8, 8.0),
                                        QPointF(10.4, 13.0)}));
        painter.drawLine(QPointF(8.9, 2.8), QPointF(7.1, 13.2));
    } else if (key == QStringLiteral("calculator")) {
        painter.drawRoundedRect(QRectF(2.5, 1.8, 11.0, 12.5), 1.4, 1.4);
        painter.drawRect(QRectF(4.2, 3.4, 7.6, 2.7));
        for (const qreal x : {5.0, 8.0, 11.0}) {
            for (const qreal y : {8.5, 11.5}) {
                painter.drawEllipse(QRectF(x - 0.55, y - 0.55, 1.1, 1.1));
            }
        }
    } else if (key == QStringLiteral("noop")) {
        painter.drawEllipse(QRectF(2.5, 2.5, 11.0, 11.0));
        painter.drawLine(QPointF(5.0, 8.0), QPointF(11.0, 8.0));
    } else if (key == QStringLiteral("plugin")) {
        painter.drawRoundedRect(QRectF(3.4, 3.4, 9.2, 9.2), 1.5, 1.5);
        for (const qreal position : {5.5, 8.0, 10.5}) {
            painter.drawLine(QPointF(position, 1.5), QPointF(position, 3.4));
            painter.drawLine(QPointF(position, 12.6), QPointF(position, 14.5));
            painter.drawLine(QPointF(1.5, position), QPointF(3.4, position));
            painter.drawLine(QPointF(12.6, position), QPointF(14.5, position));
        }
        painter.drawRoundedRect(QRectF(6.0, 6.0, 4.0, 4.0), 0.8, 0.8);
    } else {
        painter.setBrush(color);
        for (const QPointF point : {QPointF(5.0, 5.0), QPointF(11.0, 5.0),
                                    QPointF(5.0, 11.0), QPointF(11.0, 11.0)}) {
            painter.drawRoundedRect(QRectF(point.x() - 1.4, point.y() - 1.4,
                                           2.8, 2.8), 0.6, 0.6);
        }
    }
}

class FunctionIconEngine final : public QIconEngine
{
public:
    explicit FunctionIconEngine(QString key)
        : m_key(std::move(key))
    {
    }

    QIconEngine* clone() const override
    {
        return new FunctionIconEngine(m_key);
    }

    void paint(QPainter* painter,
               const QRect& rect,
               QIcon::Mode,
               QIcon::State) override
    {
        if (!painter || rect.isEmpty()) {
            return;
        }
        const qreal side = std::min(rect.width(), rect.height());
        const qreal left = rect.left() + (rect.width() - side) / 2.0;
        const qreal top = rect.top() + (rect.height() - side) / 2.0;
        painter->save();
        painter->translate(left, top);
        painter->scale(side / 16.0, side / 16.0);
        paintFunctionGlyph(*painter, m_key);
        painter->restore();
    }

private:
    QString m_key;
};

} // namespace

QIcon functionIcon(const QString& key)
{
    static QHash<QString, QIcon> icons;
    const auto normalizedKey = key.isEmpty() ? QStringLiteral("basic") : key;
    const auto existing = icons.constFind(normalizedKey);
    if (existing != icons.constEnd()) {
        return existing.value();
    }

    QIcon icon(new FunctionIconEngine(normalizedKey));
    icons.insert(normalizedKey, icon);
    return icon;
}

QString functionIconKey(const QJsonObject& step)
{
    const auto moduleId = step.value(QStringLiteral("moduleId")).toString().trimmed();
    return moduleId.isEmpty()
        ? iconKeyForKind(step.value(QStringLiteral("kind")).toString())
        : iconKeyForModule(moduleId);
}

QString functionIconKey(const PicoATE::Core::StepReport& step)
{
    return step.moduleId.trimmed().isEmpty()
        ? iconKeyForKind(step.kind)
        : iconKeyForModule(step.moduleId);
}

QIcon functionIconForStep(const QJsonObject& step)
{
    return functionIcon(functionIconKey(step));
}

QIcon functionIconForStep(const PicoATE::Core::StepReport& step)
{
    return functionIcon(functionIconKey(step));
}

} // namespace PicoATE::Ui
