#include "ReportExporter.h"
#include "MeasurementDisplay.h"
#include "SimpleXlsxWriter.h"

#include "PicoATE/Core/MeasurementTypes.h"

#include <QDir>
#include <QFile>
#include <QFontMetrics>
#include <QImage>
#include <QPageLayout>
#include <QPageSize>
#include <QPainter>
#include <QPdfWriter>
#include <QSaveFile>

#include <algorithm>
#include <cmath>
#include <numeric>

namespace PicoATE::Ui {

namespace {

QByteArray reportLogoPng()
{
    const auto read = [](const QString& path) {
        QFile file(path);
        return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray{};
    };
    auto bytes = read(QStringLiteral(":/branding/Sinexcel.png"));
#ifdef PICOATE_PROJECT_DIR
    if (bytes.isEmpty()) {
        bytes = read(QDir(QStringLiteral(PICOATE_PROJECT_DIR)).filePath(
            QStringLiteral("ui/src/assets/Sinexcel.png")));
    }
#endif
    return bytes;
}

QString reportLimitDisplay(
    const PicoATE::Core::MeasurementResult& measurement,
    bool lower)
{
    const auto value = lower
        ? measurementLowerLimitDisplay(measurement)
        : measurementUpperLimitDisplay(measurement);
    return value == QStringLiteral("-") ? QString{} : value;
}

QString reportActualDisplay(
    const PicoATE::Core::MeasurementResult& measurement)
{
    return !measurement.value.isValid() || measurement.value.isNull()
        ? QString{}
        : measurementActualDisplay(measurement);
}

QString csvCell(QString value)
{
    value.replace('"', "\"\"");
    return '"' + value + '"';
}

QString outcomeToken(PicoATE::Core::NodeOutcome outcome)
{
    using PicoATE::Core::NodeOutcome;
    switch (outcome) {
    case NodeOutcome::Passed: return QStringLiteral("PASS");
    case NodeOutcome::Failed: return QStringLiteral("FAIL");
    case NodeOutcome::Error: return QStringLiteral("ERROR");
    case NodeOutcome::Timeout: return QStringLiteral("TIMEOUT");
    case NodeOutcome::Cancelled: return QStringLiteral("CANCELLED");
    case NodeOutcome::Skipped: return QStringLiteral("SKIPPED");
    case NodeOutcome::Unknown: return QStringLiteral("UNKNOWN");
    }
    return QStringLiteral("UNKNOWN");
}

bool reportPassed(const PicoATE::Core::ExecutionReport& report)
{
    return report.completed && !report.hasError &&
           report.state == PicoATE::Core::ExecutionState::Completed;
}

QString totalDurationText(const PicoATE::Core::ExecutionReportMetadata& metadata)
{
    qint64 durationMs = metadata.durationMs;
    if (durationMs < 0 && metadata.startedAt.isValid() &&
        metadata.finishedAt.isValid()) {
        durationMs = metadata.startedAt.msecsTo(metadata.finishedAt);
    }
    if (durationMs < 0) {
        return {};
    }

    const auto hours = durationMs / 3600000;
    durationMs %= 3600000;
    const auto minutes = durationMs / 60000;
    durationMs %= 60000;
    const auto seconds = durationMs / 1000;
    const auto milliseconds = durationMs % 1000;
    return QStringLiteral("%1:%2:%3.%4")
        .arg(hours, 2, 10, QLatin1Char('0'))
        .arg(minutes, 2, 10, QLatin1Char('0'))
        .arg(seconds, 2, 10, QLatin1Char('0'))
        .arg(milliseconds, 3, 10, QLatin1Char('0'));
}

struct ReportSummary {
    QString model;
    QString customerId;
    QString sequenceName;
    QString serialNumber;
    QString stationId;
    QString jigNo;
    QString order;
    QString tester;
    QString testTime;
    QString totalDuration;
    QString overallResult;
    int totalTestItems = 0;
};

int recordedStepCount(const PicoATE::Core::StepReport& step)
{
    int count = step.resultRecording ? 1 : 0;
    for (const auto& child : step.children) {
        count += recordedStepCount(child);
    }
    return count;
}

int recordedStepCount(const PicoATE::Core::ExecutionReport& report)
{
    int count = 0;
    for (const auto& step : report.sessionSteps) {
        if (step.phase == PicoATE::Core::ExecutionPhase::Setup ||
            step.phase == PicoATE::Core::ExecutionPhase::Cleanup) {
            count += recordedStepCount(step);
        }
    }
    for (const auto& uut : report.uuts) {
        for (const auto& step : uut.steps) {
            count += recordedStepCount(step);
        }
    }
    return count;
}

ReportSummary reportSummary(const PicoATE::Core::ExecutionReport& report)
{
    ReportSummary summary;
    summary.model = report.metadata.model;
    summary.customerId = report.metadata.customerId;
    summary.sequenceName = report.metadata.sequenceName.trimmed();
    if (summary.sequenceName.isEmpty()) {
        summary.sequenceName = report.sequenceId;
    }
    summary.serialNumber = report.metadata.serialNumber;
    summary.stationId = report.metadata.stationId;
    summary.jigNo = report.metadata.jigNo;
    summary.order = report.metadata.order;
    summary.tester = report.metadata.tester;
    summary.testTime = report.metadata.startedAt.isValid()
        ? report.metadata.startedAt.toLocalTime().toString(
              QStringLiteral("yyyy-MM-dd HH:mm:ss"))
        : QString();
    summary.totalDuration = totalDurationText(report.metadata);
    summary.overallResult = reportPassed(report)
        ? QStringLiteral("PASS")
        : QStringLiteral("FAIL");
    summary.totalTestItems = recordedStepCount(report);
    return summary;
}

QString csvLine(const QStringList& cells)
{
    QStringList escaped;
    escaped.reserve(cells.size());
    for (const auto& cell : cells) {
        escaped.push_back(csvCell(cell));
    }
    return escaped.join(QLatin1Char(',')) + QStringLiteral("\r\n");
}

QString summaryBlock(const QString& label, const QString& value)
{
    return label + QLatin1Char('\n') +
           (value.trimmed().isEmpty() ? QStringLiteral("--") : value);
}

QString displayValue(const QString& value)
{
    return value.trimmed().isEmpty() ? QStringLiteral("--") : value;
}

int xlsxColumnWidthPixels(double width)
{
    return qRound(width * 7.0 + 5.0);
}

QPair<int, int> xlsxImageAnchor(const QVector<double>& columnWidths,
                                int imageWidthPixels)
{
    int sheetWidth = 0;
    for (const auto width : columnWidths) {
        sheetWidth += xlsxColumnWidthPixels(width);
    }
    int left = qMax(0, (sheetWidth - imageWidthPixels) / 2);
    for (int column = 0; column < columnWidths.size(); ++column) {
        const auto columnWidth = xlsxColumnWidthPixels(columnWidths[column]);
        if (left < columnWidth) {
            return {column, left};
        }
        left -= columnWidth;
    }
    return {qMax(0, columnWidths.size() - 1), 0};
}

XlsxRow sequenceInfoRow(const QString& sequenceName,
                        const QString& serialNumber)
{
    XlsxRow row;
    row.cells = {
        QStringLiteral("Sequence Name"), displayValue(sequenceName),
        {}, {}, QStringLiteral("SN"), displayValue(serialNumber), {}, {},
    };
    row.height = 30.0;
    row.mergedColumnRanges = {{1, 3}, {5, 7}};
    row.cellStyles.insert(0, XlsxRowStyle::SummaryInfo);
    row.cellStyles.insert(4, XlsxRowStyle::SummaryInfo);
    return row;
}

XlsxRow summaryInfoRow(const QString& firstLabel,
                       const QString& firstValue,
                       const QString& secondLabel,
                       const QString& secondValue,
                       const QString& thirdLabel,
                       const QString& thirdValue)
{
    XlsxRow row;
    row.cells = {
        firstLabel, displayValue(firstValue),
        secondLabel, displayValue(secondValue),
        thirdLabel, displayValue(thirdValue), {}, {},
    };
    row.height = 30.0;
    row.mergedColumnRanges = {{5, 7}};
    for (const int column : {0, 2, 4}) {
        row.cellStyles.insert(column, XlsxRowStyle::SummaryInfo);
    }
    return row;
}

QByteArray detailCsvHeader()
{
    return QByteArrayLiteral(
        "\"No.\",\"Test Item\",\"ERRORCODE\",\"Lower Limit\",\"Upper Limit\","
        "\"Actual Value\",\"Test Result\",\"Duration Ms\"\r\n");
}

QStringList reportCells(const PicoATE::Core::StepReport& step,
                        const PicoATE::Core::MeasurementResult* measurement,
                        int sequenceNumber)
{
    const auto* attempt = step.attempts.isEmpty() ? nullptr : &step.attempts.constLast();
    const auto errorCode = measurement && !measurement->errorCode.isEmpty()
        ? measurement->errorCode
        : (attempt ? attempt->errorCode : QString());
    return {
        QString::number(sequenceNumber),
        step.displayName.isEmpty() ? step.stepId : step.displayName,
        errorCode,
        measurement ? reportLimitDisplay(*measurement, true) : QString(),
        measurement ? reportLimitDisplay(*measurement, false) : QString(),
        measurement ? reportActualDisplay(*measurement) : QString(),
        outcomeToken(step.outcome),
        step.durationMs >= 0 ? QString::number(step.durationMs) : QString(),
    };
}

QString csvRow(const PicoATE::Core::StepReport& step,
               const PicoATE::Core::MeasurementResult* measurement,
               int sequenceNumber)
{
    const auto cells = reportCells(step, measurement, sequenceNumber);
    QStringList escaped;
    for (const auto& cell : cells) escaped.push_back(csvCell(cell));
    return escaped.join(',') + "\r\n";
}

XlsxRowStyle xlsxStyle(PicoATE::Core::NodeOutcome outcome)
{
    using PicoATE::Core::NodeOutcome;
    switch (outcome) {
    case NodeOutcome::Passed: return XlsxRowStyle::Passed;
    case NodeOutcome::Skipped: return XlsxRowStyle::Skipped;
    case NodeOutcome::Failed:
    case NodeOutcome::Error:
    case NodeOutcome::Timeout:
    case NodeOutcome::Cancelled:
        return XlsxRowStyle::Failed;
    case NodeOutcome::Unknown:
        return XlsxRowStyle::Normal;
    }
    return XlsxRowStyle::Normal;
}

XlsxRow xlsxRow(const PicoATE::Core::StepReport& step,
                const PicoATE::Core::MeasurementResult* measurement,
                int sequenceNumber)
{
    XlsxRow row;
    row.cells = reportCells(step, measurement, sequenceNumber);
    row.style = xlsxStyle(step.outcome);
    row.numericColumns.insert(0);
    for (const int column : {3, 4}) {
        bool numeric = false;
        row.cells[column].toDouble(&numeric);
        if (numeric) {
            row.numericColumns.insert(column);
        }
    }
    if (measurement && measurement->value.isValid() &&
        measurement->value.metaType().id() != QMetaType::QString &&
        measurement->value.metaType().id() != QMetaType::Bool) {
        bool numeric = false;
        row.cells[5].toDouble(&numeric);
        if (numeric) {
            row.numericColumns.insert(5);
        }
    }
    if (step.durationMs >= 0) {
        row.numericColumns.insert(7);
    }
    return row;
}

void appendStepXlsx(QVector<XlsxRow>& rows,
                    const PicoATE::Core::StepReport& step,
                    int& sequenceNumber)
{
    const auto& measurements = !step.measurements.isEmpty()
        ? step.measurements
        : (step.attempts.isEmpty()
               ? QVector<PicoATE::Core::MeasurementResult>{}
               : step.attempts.constLast().measurements);
    if (step.resultRecording) {
        if (measurements.isEmpty()) {
            rows.push_back(xlsxRow(step, nullptr, sequenceNumber++));
        } else {
            for (const auto& measurement : measurements) {
                rows.push_back(xlsxRow(step, &measurement, sequenceNumber++));
            }
        }
    }
    for (const auto& child : step.children) {
        appendStepXlsx(rows, child, sequenceNumber);
    }
}

void appendStepCsv(QByteArray& csv,
                   const PicoATE::Core::StepReport& step,
                   int& sequenceNumber)
{
    const auto& measurements = !step.measurements.isEmpty()
        ? step.measurements
        : (step.attempts.isEmpty()
               ? QVector<PicoATE::Core::MeasurementResult>{}
               : step.attempts.constLast().measurements);
    if (step.resultRecording) {
        if (measurements.isEmpty()) {
            csv += csvRow(step, nullptr, sequenceNumber++).toUtf8();
        } else {
            for (const auto& measurement : measurements) {
                csv += csvRow(step, &measurement, sequenceNumber++).toUtf8();
            }
        }
    }
    for (const auto& child : step.children) {
        appendStepCsv(csv, child, sequenceNumber);
    }
}

struct PdfDetailRow {
    QStringList cells;
    PicoATE::Core::NodeOutcome outcome = PicoATE::Core::NodeOutcome::Unknown;
};

void appendStepPdf(QVector<PdfDetailRow>& rows,
                   const PicoATE::Core::StepReport& step,
                   int& sequenceNumber)
{
    const auto& measurements = !step.measurements.isEmpty()
        ? step.measurements
        : (step.attempts.isEmpty()
               ? QVector<PicoATE::Core::MeasurementResult>{}
               : step.attempts.constLast().measurements);
    if (step.resultRecording) {
        if (measurements.isEmpty()) {
            rows.push_back({reportCells(step, nullptr, sequenceNumber++), step.outcome});
        } else {
            for (const auto& measurement : measurements) {
                rows.push_back({reportCells(step, &measurement, sequenceNumber++),
                                step.outcome});
            }
        }
    }
    for (const auto& child : step.children) {
        appendStepPdf(rows, child, sequenceNumber);
    }
}

QVector<PdfDetailRow> pdfDetailRows(
    const PicoATE::Core::ExecutionReport& report)
{
    QVector<PdfDetailRow> rows;
    int sequenceNumber = 1;
    for (const auto& step : report.sessionSteps) {
        if (step.phase == PicoATE::Core::ExecutionPhase::Setup) {
            appendStepPdf(rows, step, sequenceNumber);
        }
    }
    for (const auto& uut : report.uuts) {
        for (const auto& step : uut.steps) {
            appendStepPdf(rows, step, sequenceNumber);
        }
    }
    for (const auto& step : report.sessionSteps) {
        if (step.phase == PicoATE::Core::ExecutionPhase::Cleanup) {
            appendStepPdf(rows, step, sequenceNumber);
        }
    }
    return rows;
}

QFont reportPdfFont(double pointSize, bool bold = false)
{
    QFont font(QStringLiteral("Microsoft YaHei UI"));
    font.setPointSizeF(pointSize);
    font.setWeight(bold ? QFont::DemiBold : QFont::Normal);
    return font;
}

void drawPdfText(QPainter& painter,
                 const QRectF& rect,
                 const QString& value,
                 double pointSize,
                 const QColor& color,
                 Qt::Alignment alignment = Qt::AlignLeft | Qt::AlignVCenter,
                 bool bold = false)
{
    const auto font = reportPdfFont(pointSize, bold);
    painter.setFont(font);
    painter.setPen(color);
    const auto available = qMax(0, qFloor(rect.width() - 12.0));
    const auto shown = QFontMetrics(font).elidedText(
        value, Qt::ElideRight, available);
    painter.drawText(rect.adjusted(6, 0, -6, 0), alignment, shown);
}

qreal drawPdfPageHeader(QPainter& painter,
                        const QSize& pageSize,
                        const QImage& logo,
                        const ReportSummary& summary,
                        bool continuation)
{
    const QColor ink(QStringLiteral("#172033"));
    const QColor muted(QStringLiteral("#667085"));
    const QColor border(QStringLiteral("#DDE3EA"));
    const QColor green(QStringLiteral("#4DB766"));
    constexpr qreal margin = 54.0;
    constexpr qreal top = 28.0;
    if (!logo.isNull()) {
        const auto target = logo.size().scaled(
            QSize(260, 42), Qt::KeepAspectRatio);
        painter.drawImage(QRectF(margin, top, target.width(), target.height()), logo);
    }
    drawPdfText(painter,
                QRectF(pageSize.width() - 620.0, top - 4.0, 566.0, 34.0),
                QStringLiteral("自动化测试报告"),
                15.0,
                ink,
                Qt::AlignRight | Qt::AlignVCenter,
                true);
    const auto reportId = summary.serialNumber.trimmed().isEmpty()
        ? summary.testTime
        : summary.serialNumber + QStringLiteral(" | ") + summary.testTime;
    drawPdfText(painter,
                QRectF(pageSize.width() - 700.0, top + 30.0, 646.0, 24.0),
                continuation
                    ? QStringLiteral("AUTOMATED TEST REPORT | CONTINUED | %1").arg(reportId)
                    : QStringLiteral("AUTOMATED TEST REPORT | %1").arg(reportId),
                7.3,
                muted,
                Qt::AlignRight | Qt::AlignVCenter);
    painter.setPen(QPen(green, 3.0));
    painter.drawLine(QPointF(margin, top + 62.0), QPointF(margin + 145.0, top + 62.0));
    painter.setPen(QPen(border, 1.0));
    painter.drawLine(QPointF(margin + 145.0, top + 62.0),
                     QPointF(pageSize.width() - margin, top + 62.0));
    return top + 80.0;
}

qreal drawPdfMetadata(QPainter& painter,
                      qreal x,
                      qreal y,
                      qreal width,
                      const ReportSummary& summary)
{
    const QColor ink(QStringLiteral("#172033"));
    const QColor muted(QStringLiteral("#667085"));
    const QColor border(QStringLiteral("#DDE3EA"));
    const QVector<QVector<QPair<QString, QString>>> rows = {
        {{QStringLiteral("产品型号"), summary.model},
         {QStringLiteral("工站"), summary.stationId},
         {QStringLiteral("治具编号"), summary.jigNo}},
        {{QStringLiteral("客户编号"), summary.customerId},
         {QStringLiteral("工单"), summary.order},
         {QStringLiteral("测试员"), summary.tester}},
        {{QStringLiteral("测试脚本"), summary.sequenceName},
         {QStringLiteral("SN"), summary.serialNumber},
         {QStringLiteral("测试时间"), summary.testTime}},
    };
    constexpr qreal rowHeight = 32.0;
    const auto columnWidth = width / 3.0;
    painter.setPen(QPen(border, 1.0));
    painter.setBrush(Qt::white);
    painter.drawRoundedRect(QRectF(x, y, width, rowHeight * rows.size()), 7.0, 7.0);
    for (int row = 0; row < rows.size(); ++row) {
        if (row > 0) {
            painter.drawLine(QPointF(x, y + row * rowHeight),
                             QPointF(x + width, y + row * rowHeight));
        }
        for (int column = 0; column < rows[row].size(); ++column) {
            const auto left = x + column * columnWidth;
            if (column > 0) {
                painter.drawLine(QPointF(left, y + row * rowHeight),
                                 QPointF(left, y + (row + 1) * rowHeight));
            }
            const auto& field = rows[row][column];
            drawPdfText(painter,
                        QRectF(left + 7.0, y + row * rowHeight, 90.0, rowHeight),
                        field.first,
                        6.5,
                        muted);
            drawPdfText(painter,
                        QRectF(left + 96.0,
                               y + row * rowHeight,
                               columnWidth - 103.0,
                               rowHeight),
                        displayValue(field.second),
                        7.5,
                        ink,
                        Qt::AlignLeft | Qt::AlignVCenter,
                        true);
        }
    }
    return y + rowHeight * rows.size();
}

qreal drawPdfSummary(QPainter& painter,
                     qreal x,
                     qreal y,
                     qreal width,
                     const ReportSummary& summary)
{
    const QColor navy(QStringLiteral("#303A6D"));
    const QColor ink(QStringLiteral("#172033"));
    const QColor muted(QStringLiteral("#667085"));
    const QColor border(QStringLiteral("#DDE3EA"));
    const QColor surface(QStringLiteral("#F5F7FA"));
    const bool passed = summary.overallResult == QStringLiteral("PASS");
    const QColor resultColor(passed ? QStringLiteral("#17783B")
                                    : QStringLiteral("#A61B1B"));
    const QColor resultBackground(passed ? QStringLiteral("#EAF7EE")
                                         : QStringLiteral("#FDECEC"));
    constexpr qreal gap = 12.0;
    constexpr qreal height = 60.0;
    const auto sideWidth = (width - gap * 2.0) * 0.25;
    const auto centerWidth = width - gap * 2.0 - sideWidth * 2.0;
    struct Card {
        qreal left;
        qreal width;
        QString label;
        QString value;
        QColor valueColor;
        QColor background;
    };
    const QVector<Card> cards = {
        {x, sideWidth, QStringLiteral("测试项总数"),
         QString::number(summary.totalTestItems), navy, surface},
        {x + sideWidth + gap, centerWidth, QStringLiteral("整轮结果"),
         summary.overallResult, resultColor, resultBackground},
        {x + sideWidth + gap + centerWidth + gap, sideWidth,
         QStringLiteral("总用时"), displayValue(summary.totalDuration),
         navy, surface},
    };
    for (const auto& card : cards) {
        painter.setPen(QPen(border, 1.0));
        painter.setBrush(card.background);
        painter.drawRoundedRect(QRectF(card.left, y, card.width, height), 7.0, 7.0);
        drawPdfText(painter,
                    QRectF(card.left, y + 4.0, card.width, 20.0),
                    card.label,
                    7.0,
                    muted,
                    Qt::AlignCenter);
        drawPdfText(painter,
                    QRectF(card.left, y + 22.0, card.width, 32.0),
                    card.value,
                    card.label == QStringLiteral("整轮结果") ? 15.0 : 11.0,
                    card.valueColor,
                    Qt::AlignCenter,
                    true);
    }
    return y + height;
}

QVector<qreal> pdfColumnWidths(qreal width)
{
    const QVector<qreal> factors = {0.05, 0.26, 0.14, 0.12,
                                    0.12, 0.16, 0.07, 0.08};
    QVector<qreal> widths;
    widths.reserve(factors.size());
    for (const auto factor : factors) {
        widths.push_back(width * factor);
    }
    return widths;
}

void drawPdfTableHeader(QPainter& painter,
                        qreal x,
                        qreal y,
                        const QVector<qreal>& widths)
{
    const QColor navy(QStringLiteral("#303A6D"));
    const QColor divider(QStringLiteral("#59628B"));
    const QStringList headers = {QStringLiteral("序号"),
                                 QStringLiteral("测试项"),
                                 QStringLiteral("错误码"),
                                 QStringLiteral("下限"),
                                 QStringLiteral("上限"),
                                 QStringLiteral("实测值"),
                                 QStringLiteral("结果"),
                                 QStringLiteral("用时(ms)")};
    constexpr qreal height = 32.0;
    painter.fillRect(QRectF(x, y, std::accumulate(widths.cbegin(), widths.cend(), 0.0),
                            height),
                     navy);
    qreal left = x;
    for (int column = 0; column < widths.size(); ++column) {
        if (column > 0) {
            painter.setPen(QPen(divider, 1.0));
            painter.drawLine(QPointF(left, y), QPointF(left, y + height));
        }
        drawPdfText(painter,
                    QRectF(left, y, widths[column], height),
                    headers[column],
                    7.5,
                    Qt::white,
                    Qt::AlignCenter,
                    true);
        left += widths[column];
    }
}

void drawPdfTableRow(QPainter& painter,
                     qreal x,
                     qreal y,
                     const QVector<qreal>& widths,
                     const PdfDetailRow& row,
                     bool alternate)
{
    const QColor ink(QStringLiteral("#172033"));
    const QColor muted(QStringLiteral("#667085"));
    const QColor border(QStringLiteral("#DDE3EA"));
    const QColor alternateColor(QStringLiteral("#F8FAFC"));
    constexpr qreal height = 30.0;
    const auto totalWidth = std::accumulate(widths.cbegin(), widths.cend(), 0.0);
    painter.fillRect(QRectF(x, y, totalWidth, height),
                     alternate ? alternateColor : Qt::white);
    qreal left = x;
    for (int column = 0; column < widths.size(); ++column) {
        painter.setPen(QPen(border, 1.0));
        painter.drawLine(QPointF(left, y), QPointF(left, y + height));
        const auto value = column < row.cells.size() ? row.cells[column] : QString{};
        if (column == 6) {
            QColor foreground = muted;
            QColor background(QStringLiteral("#EEF1F4"));
            if (row.outcome == PicoATE::Core::NodeOutcome::Passed) {
                foreground = QColor(QStringLiteral("#17783B"));
                background = QColor(QStringLiteral("#EAF7EE"));
            } else if (row.outcome == PicoATE::Core::NodeOutcome::Failed ||
                       row.outcome == PicoATE::Core::NodeOutcome::Error ||
                       row.outcome == PicoATE::Core::NodeOutcome::Timeout ||
                       row.outcome == PicoATE::Core::NodeOutcome::Cancelled) {
                foreground = QColor(QStringLiteral("#A61B1B"));
                background = QColor(QStringLiteral("#FDECEC"));
            }
            const auto badgeWidth = qMin(widths[column] - 14.0, 76.0);
            const QRectF badge(left + (widths[column] - badgeWidth) / 2.0,
                               y + 5.0,
                               badgeWidth,
                               20.0);
            painter.setPen(Qt::NoPen);
            painter.setBrush(background);
            painter.drawRoundedRect(badge, 11.0, 11.0);
            drawPdfText(painter, badge, value, 6.7, foreground, Qt::AlignCenter, true);
        } else {
            const auto alignment = column == 1 || column == 2
                ? Qt::AlignLeft | Qt::AlignVCenter
                : Qt::AlignCenter;
            drawPdfText(painter,
                        QRectF(left, y, widths[column], height),
                        value.trimmed().isEmpty() ? QStringLiteral("-") : value,
                        7.0,
                        value.trimmed().isEmpty() ? muted : ink,
                        alignment);
        }
        left += widths[column];
    }
    painter.setPen(QPen(border, 1.0));
    painter.drawLine(QPointF(left, y), QPointF(left, y + height));
    painter.drawLine(QPointF(x, y + height), QPointF(x + totalWidth, y + height));
}

void drawPdfFooter(QPainter& painter,
                   const QSize& pageSize,
                   int page,
                   int pageCount)
{
    const QColor muted(QStringLiteral("#667085"));
    const QColor border(QStringLiteral("#DDE3EA"));
    constexpr qreal margin = 54.0;
    const auto y = pageSize.height() - 38.0;
    painter.setPen(QPen(border, 1.0));
    painter.drawLine(QPointF(margin, y - 12.0),
                     QPointF(pageSize.width() - margin, y - 12.0));
    drawPdfText(painter,
                QRectF(margin, y - 3.0, 500.0, 25.0),
                QStringLiteral("PicoATE 自动生成"),
                6.5,
                muted);
    drawPdfText(painter,
                QRectF(pageSize.width() - 300.0, y - 3.0, 246.0, 25.0),
                QStringLiteral("第 %1 页 / 共 %2 页").arg(page).arg(pageCount),
                6.5,
                muted,
                Qt::AlignRight | Qt::AlignVCenter);
}

void appendStepText(QByteArray& text,
                    const PicoATE::Core::StepReport& step,
                    int depth)
{
    auto name = step.displayName.isEmpty() ? step.stepId : step.displayName;
    name.replace(QLatin1Char(' '), QLatin1Char('_'));
    name = name.toUpper();
    const auto indentation = QString(depth * 4, QLatin1Char(' '));
    const bool testItem = !step.children.isEmpty();
    text += (testItem
                 ? QStringLiteral("%1======================== %2_TESTITEM_START ========================\r\n")
                       .arg(indentation, name)
                 : QStringLiteral("%1------------------------ %2_STEP_START ------------------------\r\n")
                       .arg(indentation, name))
                .toUtf8();
    for (const auto& child : step.children) {
        appendStepText(text, child, depth + 1);
    }
    text += QStringLiteral("%1RESULT:%2\r\n")
                .arg(indentation, outcomeToken(step.outcome))
                .toUtf8();
    text += (testItem
                 ? QStringLiteral("%1======================== %2_TESTITEM_END ========================\r\n\r\n\r\n\r\n")
                       .arg(indentation, name)
                 : QStringLiteral("%1------------------------ %2_STEP_END ------------------------\r\n")
                       .arg(indentation, name))
                .toUtf8();
}

ReportExportResult writeFile(const QString& filePath, const QByteArray& bytes)
{
    ReportExportResult result;
    QSaveFile file(filePath);
    if (!file.open(QIODevice::WriteOnly)) {
        result.errorMessage = file.errorString();
        return result;
    }
    if (file.write(bytes) != bytes.size() || !file.commit()) {
        result.errorMessage = file.errorString();
        return result;
    }
    result.success = true;
    return result;
}

} // namespace

ReportExportResult ReportExporter::saveText(
    const QString& filePath,
    const PicoATE::Core::ExecutionReport& report)
{
    QByteArray text("\xEF\xBB\xBF");
    for (const auto& step : report.sessionSteps) {
        if (step.phase == PicoATE::Core::ExecutionPhase::Setup) {
            appendStepText(text, step, 0);
        }
    }
    for (const auto& uut : report.uuts) {
        text += QStringLiteral("UUT:%1\r\n").arg(uut.uutId).toUtf8();
        for (const auto& step : uut.steps) {
            appendStepText(text, step, 0);
        }
    }
    for (const auto& step : report.sessionSteps) {
        if (step.phase == PicoATE::Core::ExecutionPhase::Cleanup) {
            appendStepText(text, step, 0);
        }
    }
    return writeFile(filePath, text);
}

ReportExportResult ReportExporter::saveCsv(
    const QString& filePath,
    const PicoATE::Core::ExecutionReport& report)
{
    QByteArray csv("\xEF\xBB\xBF");
    const auto summary = reportSummary(report);
    csv += csvLine({QStringLiteral("Sequence Name"), summary.sequenceName,
                    {}, {}, QStringLiteral("SN"), summary.serialNumber, {}, {}})
               .toUtf8();
    csv += csvLine({QStringLiteral("Station ID"), summary.stationId,
                    QStringLiteral("Model"), summary.model,
                    QStringLiteral("Customer ID"), summary.customerId, {}, {}}).toUtf8();
    csv += csvLine({QStringLiteral("Jig No"), summary.jigNo,
                    QStringLiteral("Order"), summary.order,
                    QStringLiteral("Tester"), summary.tester, {}, {}}).toUtf8();
    csv += csvLine({QStringLiteral("Test Time"), summary.testTime,
                    {}, {}, {}, {}, {}, {}}).toUtf8();
    csv += csvLine({
        QStringLiteral("TOTAL TEST ITEMS: %1").arg(summary.totalTestItems),
        {}, summary.overallResult, {}, {}, {},
        QStringLiteral("TOTAL DURATION: %1").arg(displayValue(summary.totalDuration)),
        {},
    }).toUtf8();
    csv += detailCsvHeader();
    int sequenceNumber = 1;
    for (const auto& step : report.sessionSteps) {
        if (step.phase == PicoATE::Core::ExecutionPhase::Setup) {
            appendStepCsv(csv, step, sequenceNumber);
        }
    }
    for (const auto& uut : report.uuts) {
        for (const auto& step : uut.steps) {
            appendStepCsv(csv, step, sequenceNumber);
        }
    }
    for (const auto& step : report.sessionSteps) {
        if (step.phase == PicoATE::Core::ExecutionPhase::Cleanup) {
            appendStepCsv(csv, step, sequenceNumber);
        }
    }
    return writeFile(filePath, csv);
}

ReportExportResult ReportExporter::saveXlsx(
    const QString& filePath,
    const PicoATE::Core::ExecutionReport& report)
{
    const QVector<double> columnWidths = {
        8.0, 34.0, 30.0, 18.0, 18.0, 28.0, 14.0, 20.0};
    QVector<XlsxRow> rows;
    QVector<XlsxImage> images;
    const auto logo = reportLogoPng();
    if (!logo.isEmpty()) {
        XlsxRow logoSpace;
        logoSpace.cells = {QString(), QString(), QString(), QString(),
                           QString(), QString(), QString(), QString()};
        logoSpace.height = 60.0;
        logoSpace.mergedColumnRanges = {{0, 7}};
        logoSpace.excludeFromAutoFilter = true;
        rows.push_back(std::move(logoSpace));
        constexpr int logoWidth = 320;
        const auto anchor = xlsxImageAnchor(columnWidths, logoWidth);
        images.push_back({logo,
                          QStringLiteral("SINEXCEL Logo"),
                          anchor.first,
                          0,
                          anchor.second,
                          18,
                          logoWidth,
                          44});
    }
    const auto summary = reportSummary(report);
    rows.push_back(sequenceInfoRow(summary.sequenceName, summary.serialNumber));
    rows.push_back(summaryInfoRow(
        QStringLiteral("Station ID"), summary.stationId,
        QStringLiteral("Model"), summary.model,
        QStringLiteral("Customer ID"), summary.customerId));
    rows.push_back(summaryInfoRow(
        QStringLiteral("Jig No"), summary.jigNo,
        QStringLiteral("Order"), summary.order,
        QStringLiteral("Tester"), summary.tester));
    rows.push_back(summaryInfoRow(
        QStringLiteral("Test Time"), summary.testTime,
        {}, {}, {}, {}));

    XlsxRow totalRow;
    totalRow.cells = {
        QStringLiteral("TOTAL TEST ITEMS\n%1").arg(summary.totalTestItems),
        summary.overallResult, {}, {}, {}, {},
        summaryBlock(QStringLiteral("TOTAL DURATION"), summary.totalDuration), {},
    };
    totalRow.style = XlsxRowStyle::SummaryDuration;
    totalRow.cellStyles.insert(
        1,
        reportPassed(report)
            ? XlsxRowStyle::SummaryPassed
            : XlsxRowStyle::SummaryFailed);
    totalRow.height = 54.0;
    totalRow.mergedColumnRanges = {{1, 5}, {6, 7}};
    totalRow.excludeFromAutoFilter = true;
    rows.push_back(std::move(totalRow));

    rows.push_back({{QStringLiteral("No."),
                     QStringLiteral("Test Item"),
                     QStringLiteral("ERRORCODE"),
                     QStringLiteral("Lower Limit"),
                     QStringLiteral("Upper Limit"),
                     QStringLiteral("Actual Value"),
                     QStringLiteral("Test Result"),
                     QStringLiteral("Duration Ms")},
                    XlsxRowStyle::Header});
    int sequenceNumber = 1;
    for (const auto& step : report.sessionSteps) {
        if (step.phase == PicoATE::Core::ExecutionPhase::Setup) {
            appendStepXlsx(rows, step, sequenceNumber);
        }
    }
    for (const auto& uut : report.uuts) {
        for (const auto& step : uut.steps) {
            appendStepXlsx(rows, step, sequenceNumber);
        }
    }
    for (const auto& step : report.sessionSteps) {
        if (step.phase == PicoATE::Core::ExecutionPhase::Cleanup) {
            appendStepXlsx(rows, step, sequenceNumber);
        }
    }

    const auto written = writeSimpleXlsx(
        filePath,
        QStringLiteral("Test Report"),
        columnWidths,
        rows,
        images);
    return {written.success, written.errorMessage};
}

ReportExportResult ReportExporter::savePdf(
    const QString& filePath,
    const PicoATE::Core::ExecutionReport& report)
{
    QSaveFile output(filePath);
    if (!output.open(QIODevice::WriteOnly)) {
        return {false, output.errorString()};
    }

    bool painted = false;
    {
        QPdfWriter writer(&output);
        writer.setTitle(QStringLiteral("PicoATE Automated Test Report"));
        writer.setCreator(QStringLiteral("PicoATE"));
        writer.setResolution(96);
        writer.setPageLayout(QPageLayout(
            QPageSize(QPageSize::A4),
            QPageLayout::Landscape,
            QMarginsF(0, 0, 0, 0),
            QPageLayout::Millimeter));

        QPainter painter;
        if (!painter.begin(&writer)) {
            output.cancelWriting();
            return {false, QStringLiteral("Cannot start PDF writer")};
        }
        painter.setRenderHint(QPainter::Antialiasing, true);
        painter.setRenderHint(QPainter::TextAntialiasing, true);
        painter.setRenderHint(QPainter::SmoothPixmapTransform, true);

        const auto pageSize = painter.viewport().size();
        constexpr qreal margin = 54.0;
        constexpr qreal tableHeaderHeight = 32.0;
        constexpr qreal rowHeight = 30.0;
        constexpr qreal bottom = 54.0;
        const auto tableWidth = pageSize.width() - margin * 2.0;
        const auto widths = pdfColumnWidths(tableWidth);
        const auto summary = reportSummary(report);
        const auto details = pdfDetailRows(report);
        const auto logo = QImage::fromData(reportLogoPng(), "PNG");

        const auto firstHeaderBottom = 108.0;
        const auto firstMetadataBottom = firstHeaderBottom + 96.0;
        const auto firstSummaryBottom = firstMetadataBottom + 10.0 + 60.0;
        const auto firstTableY = firstSummaryBottom + 36.0;
        const auto continuedTableY = firstHeaderBottom + 34.0;
        const auto usableBottom = pageSize.height() - bottom;
        const auto capacity = [&](qreal tableY) {
            return qMax(0, qFloor((usableBottom - tableY - tableHeaderHeight) /
                                  rowHeight));
        };
        const auto firstCapacity = capacity(firstTableY);
        const auto continuedCapacity = qMax(1, capacity(continuedTableY));
        const auto remaining = qMax(0, details.size() - firstCapacity);
        const auto pageCount = 1 +
            (remaining == 0 ? 0 : (remaining + continuedCapacity - 1) /
                                      continuedCapacity);

        int detailIndex = 0;
        for (int page = 1; page <= pageCount; ++page) {
            if (page > 1 && !writer.newPage()) {
                painter.end();
                output.cancelWriting();
                return {false, QStringLiteral("Cannot add PDF page")};
            }
            auto cursorY = drawPdfPageHeader(
                painter, pageSize, logo, summary, page > 1);
            if (page == 1) {
                cursorY = drawPdfMetadata(
                    painter, margin, cursorY, tableWidth, summary) + 10.0;
                cursorY = drawPdfSummary(
                    painter, margin, cursorY, tableWidth, summary) + 10.0;
            } else {
                cursorY += 8.0;
            }
            drawPdfText(painter,
                        QRectF(margin, cursorY, tableWidth, 20.0),
                        QStringLiteral("测试明细"),
                        9.0,
                        QColor(QStringLiteral("#172033")),
                        Qt::AlignLeft | Qt::AlignVCenter,
                        true);
            cursorY += 26.0;
            drawPdfTableHeader(painter, margin, cursorY, widths);
            cursorY += tableHeaderHeight;
            const auto pageCapacity = page == 1 ? firstCapacity : continuedCapacity;
            for (int row = 0;
                 row < pageCapacity && detailIndex < details.size();
                 ++row, ++detailIndex) {
                drawPdfTableRow(painter,
                                margin,
                                cursorY,
                                widths,
                                details[detailIndex],
                                detailIndex % 2 != 0);
                cursorY += rowHeight;
            }
            drawPdfFooter(painter, pageSize, page, pageCount);
        }
        painted = painter.end();
    }
    if (!painted) {
        output.cancelWriting();
        return {false, QStringLiteral("Cannot finish PDF report")};
    }
    if (!output.commit()) {
        return {false, output.errorString()};
    }
    return {true, {}};
}

QByteArray ReportExporter::csvHeader()
{
    QByteArray csv("\xEF\xBB\xBF");
    csv += detailCsvHeader();
    return csv;
}

} // namespace PicoATE::Ui
