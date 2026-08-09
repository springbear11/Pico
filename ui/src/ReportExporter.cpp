#include "ReportExporter.h"
#include "SimpleXlsxWriter.h"

#include "PicoATE/Core/MeasurementTypes.h"

#include <QJsonDocument>
#include <QSaveFile>

namespace PicoATE::Ui {

namespace {

QString csvCell(QString value)
{
    value.replace('"', "\"\"");
    return '"' + value + '"';
}

QString variantText(const QVariant& value)
{
    if (!value.isValid() || value.isNull()) return {};
    if (value.metaType().id() == QMetaType::QVariantMap ||
        value.metaType().id() == QMetaType::QVariantList) {
        return QString::fromUtf8(
            QJsonDocument::fromVariant(value).toJson(QJsonDocument::Compact));
    }
    return value.toString();
}

QString optionalLimit(bool present, double value)
{
    return present ? QString::number(value, 'g', 15) : QString();
}

QString normalizedComparison(QString value)
{
    value = value.trimmed().toLower();
    value.remove(QLatin1Char('-'));
    value.remove(QLatin1Char('_'));
    value.remove(QLatin1Char(' '));
    return value;
}

QString inferredLimit(const PicoATE::Core::MeasurementResult& measurement,
                      bool lower)
{
    const auto display = measurement.attributes.value(
        lower ? QStringLiteral("displayLower") : QStringLiteral("displayUpper"));
    if (display.isValid() && !display.isNull()) {
        return variantText(display);
    }
    if (lower && measurement.hasLowerLimit) {
        return optionalLimit(true, measurement.lowerLimit);
    }
    if (!lower && measurement.hasUpperLimit) {
        return optionalLimit(true, measurement.upperLimit);
    }

    const auto comparison = normalizedComparison(
        measurement.attributes.value(QStringLiteral("comparison")).toString());
    const auto expected = measurement.attributes.value(QStringLiteral("expected"));
    if (!expected.isValid() || expected.isNull()) {
        return {};
    }

    const bool equality = comparison == QStringLiteral("==") ||
        comparison == QStringLiteral("eq") ||
        comparison == QStringLiteral("equal") ||
        comparison == QStringLiteral("!=") ||
        comparison == QStringLiteral("ne") ||
        comparison == QStringLiteral("notequal");
    const bool lowerBound = comparison == QStringLiteral(">") ||
        comparison == QStringLiteral(">=") ||
        comparison == QStringLiteral("gt") ||
        comparison == QStringLiteral("ge") ||
        comparison == QStringLiteral("gte") ||
        comparison == QStringLiteral("greaterthan") ||
        comparison == QStringLiteral("greaterorequal");
    const bool upperBound = comparison == QStringLiteral("<") ||
        comparison == QStringLiteral("<=") ||
        comparison == QStringLiteral("lt") ||
        comparison == QStringLiteral("le") ||
        comparison == QStringLiteral("lte") ||
        comparison == QStringLiteral("lessthan") ||
        comparison == QStringLiteral("lessorequal");
    if ((!equality && lower && !lowerBound) ||
        (!equality && !lower && !upperBound)) {
        return {};
    }
    if (equality &&
        (expected.metaType().id() == QMetaType::QString ||
         expected.metaType().id() == QMetaType::QByteArray)) {
        return variantText(expected);
    }

    bool expectedOk = false;
    const double expectedNumber = expected.toDouble(&expectedOk);
    bool toleranceOk = false;
    const double tolerance = measurement.attributes
                                 .value(QStringLiteral("tolerance"), 0.0)
                                 .toDouble(&toleranceOk);
    if (equality && expectedOk && toleranceOk) {
        return QString::number(
            lower ? expectedNumber - tolerance : expectedNumber + tolerance,
            'g',
            15);
    }
    return variantText(expected);
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
    QString name;
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
    summary.name = report.metadata.name;
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

XlsxRow sequenceInfoRow(const QString& sequenceName,
                        const QString& serialNumber)
{
    XlsxRow row;
    row.cells = {
        QStringLiteral("Sequence Name"), displayValue(sequenceName),
        {}, {}, QStringLiteral("SN"), displayValue(serialNumber), {},
    };
    row.height = 30.0;
    row.mergedColumnRanges = {{1, 3}, {5, 6}};
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
        thirdLabel, displayValue(thirdValue), {},
    };
    row.height = 30.0;
    row.mergedColumnRanges = {{5, 6}};
    for (const int column : {0, 2, 4}) {
        row.cellStyles.insert(column, XlsxRowStyle::SummaryInfo);
    }
    return row;
}

QByteArray detailCsvHeader()
{
    return QByteArrayLiteral(
        "\"Test Item\",\"ERRORCODE\",\"Lower Limit\",\"Upper Limit\","
        "\"Actual Value\",\"Test Result\",\"Duration Ms\"\r\n");
}

QStringList reportCells(const PicoATE::Core::StepReport& step,
                        const PicoATE::Core::MeasurementResult* measurement)
{
    const auto* attempt = step.attempts.isEmpty() ? nullptr : &step.attempts.constLast();
    const auto errorCode = measurement && !measurement->errorCode.isEmpty()
        ? measurement->errorCode
        : (attempt ? attempt->errorCode : QString());
    return {
        step.displayName.isEmpty() ? step.stepId : step.displayName,
        errorCode,
        measurement ? inferredLimit(*measurement, true) : QString(),
        measurement ? inferredLimit(*measurement, false) : QString(),
        measurement ? variantText(measurement->value) : QString(),
        outcomeToken(step.outcome),
        step.durationMs >= 0 ? QString::number(step.durationMs) : QString(),
    };
}

QString csvRow(const PicoATE::Core::StepReport& step,
               const PicoATE::Core::MeasurementResult* measurement)
{
    const auto cells = reportCells(step, measurement);
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
                const PicoATE::Core::MeasurementResult* measurement)
{
    XlsxRow row;
    row.cells = reportCells(step, measurement);
    row.style = xlsxStyle(step.outcome);
    for (const int column : {2, 3}) {
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
        row.cells[4].toDouble(&numeric);
        if (numeric) {
            row.numericColumns.insert(4);
        }
    }
    if (step.durationMs >= 0) {
        row.numericColumns.insert(6);
    }
    return row;
}

void appendStepXlsx(QVector<XlsxRow>& rows,
                    const PicoATE::Core::StepReport& step)
{
    const auto& measurements = !step.measurements.isEmpty()
        ? step.measurements
        : (step.attempts.isEmpty()
               ? QVector<PicoATE::Core::MeasurementResult>{}
               : step.attempts.constLast().measurements);
    if (step.resultRecording) {
        if (measurements.isEmpty()) {
            rows.push_back(xlsxRow(step, nullptr));
        } else {
            for (const auto& measurement : measurements) {
                rows.push_back(xlsxRow(step, &measurement));
            }
        }
    }
    for (const auto& child : step.children) {
        appendStepXlsx(rows, child);
    }
}

void appendStepCsv(QByteArray& csv,
                   const PicoATE::Core::StepReport& step)
{
    const auto& measurements = !step.measurements.isEmpty()
        ? step.measurements
        : (step.attempts.isEmpty()
               ? QVector<PicoATE::Core::MeasurementResult>{}
               : step.attempts.constLast().measurements);
    if (step.resultRecording) {
        if (measurements.isEmpty()) {
            csv += csvRow(step, nullptr).toUtf8();
        } else {
            for (const auto& measurement : measurements) {
                csv += csvRow(step, &measurement).toUtf8();
            }
        }
    }
    for (const auto& child : step.children) {
        appendStepCsv(csv, child);
    }
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
                    {}, {}, QStringLiteral("SN"), summary.serialNumber, {}})
               .toUtf8();
    csv += csvLine({QStringLiteral("Name"), summary.name,
                    QStringLiteral("Station ID"), summary.stationId,
                    QStringLiteral("Jig No"), summary.jigNo, {}}).toUtf8();
    csv += csvLine({QStringLiteral("Order"), summary.order,
                    QStringLiteral("Tester"), summary.tester,
                    QStringLiteral("Test Time"), summary.testTime, {}}).toUtf8();
    csv += detailCsvHeader();
    for (const auto& step : report.sessionSteps) {
        if (step.phase == PicoATE::Core::ExecutionPhase::Setup) {
            appendStepCsv(csv, step);
        }
    }
    for (const auto& uut : report.uuts) {
        for (const auto& step : uut.steps) {
            appendStepCsv(csv, step);
        }
    }
    for (const auto& step : report.sessionSteps) {
        if (step.phase == PicoATE::Core::ExecutionPhase::Cleanup) {
            appendStepCsv(csv, step);
        }
    }
    csv += csvLine({
        QStringLiteral("TOTAL TEST ITEMS: %1").arg(summary.totalTestItems),
        {}, {}, summary.overallResult, {}, {},
        QStringLiteral("TOTAL DURATION: %1").arg(displayValue(summary.totalDuration)),
    }).toUtf8();
    return writeFile(filePath, csv);
}

ReportExportResult ReportExporter::saveXlsx(
    const QString& filePath,
    const PicoATE::Core::ExecutionReport& report)
{
    QVector<XlsxRow> rows;
    const auto summary = reportSummary(report);
    rows.push_back(sequenceInfoRow(summary.sequenceName, summary.serialNumber));
    rows.push_back(summaryInfoRow(
        QStringLiteral("Name"), summary.name,
        QStringLiteral("Station ID"), summary.stationId,
        QStringLiteral("Jig No"), summary.jigNo));
    rows.push_back(summaryInfoRow(
        QStringLiteral("Order"), summary.order,
        QStringLiteral("Tester"), summary.tester,
        QStringLiteral("Test Time"), summary.testTime));

    rows.push_back({{QStringLiteral("Test Item"),
                     QStringLiteral("ERRORCODE"),
                     QStringLiteral("Lower Limit"),
                     QStringLiteral("Upper Limit"),
                     QStringLiteral("Actual Value"),
                     QStringLiteral("Test Result"),
                     QStringLiteral("Duration Ms")},
                    XlsxRowStyle::Header});
    for (const auto& step : report.sessionSteps) {
        if (step.phase == PicoATE::Core::ExecutionPhase::Setup) {
            appendStepXlsx(rows, step);
        }
    }
    for (const auto& uut : report.uuts) {
        for (const auto& step : uut.steps) {
            appendStepXlsx(rows, step);
        }
    }
    for (const auto& step : report.sessionSteps) {
        if (step.phase == PicoATE::Core::ExecutionPhase::Cleanup) {
            appendStepXlsx(rows, step);
        }
    }

    XlsxRow totalRow;
    totalRow.cells = {
        QStringLiteral("TOTAL TEST ITEMS\n%1").arg(summary.totalTestItems),
        summary.overallResult, {}, {}, {}, {},
        summaryBlock(QStringLiteral("TOTAL DURATION"), summary.totalDuration),
    };
    totalRow.style = XlsxRowStyle::SummaryDuration;
    totalRow.cellStyles.insert(
        1,
        reportPassed(report)
            ? XlsxRowStyle::SummaryPassed
            : XlsxRowStyle::SummaryFailed);
    totalRow.height = 54.0;
    totalRow.mergedColumnRanges = {{1, 5}};
    totalRow.excludeFromAutoFilter = true;
    rows.push_back(std::move(totalRow));

    const auto written = writeSimpleXlsx(
        filePath,
        QStringLiteral("Test Report"),
        {34.0, 30.0, 18.0, 18.0, 28.0, 14.0, 20.0},
        rows);
    return {written.success, written.errorMessage};
}

QByteArray ReportExporter::csvHeader()
{
    QByteArray csv("\xEF\xBB\xBF");
    csv += detailCsvHeader();
    return csv;
}

} // namespace PicoATE::Ui
