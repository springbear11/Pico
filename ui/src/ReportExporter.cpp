#include "ReportExporter.h"

#include "PicoATE/Core/ExecutionReportJson.h"
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

QString csvRow(const PicoATE::Core::UutReport& uut,
               const PicoATE::Core::StepReport& step,
               const PicoATE::Core::AttemptReport* attempt,
               const PicoATE::Core::MeasurementResult* measurement)
{
    const auto attemptOutcome = attempt ? PicoATE::Core::nodeOutcomeName(attempt->outcome) : QString();
    const auto errorCode = measurement && !measurement->errorCode.isEmpty()
        ? measurement->errorCode
        : (attempt ? attempt->errorCode : QString());
    const auto errorMessage = measurement && !measurement->errorMessage.isEmpty()
        ? measurement->errorMessage
        : (attempt ? attempt->errorMessage : QString());
    const QStringList cells = {
        uut.uutId,
        step.stepId,
        step.displayName,
        PicoATE::Core::nodeOutcomeName(step.outcome),
        attempt ? QString::number(attempt->index) : QString(),
        attemptOutcome,
        attempt && attempt->loopIteration.active
            ? QString::number(attempt->loopIteration.iterationNumber)
            : QString(),
        attempt && attempt->loopIteration.active
            ? QString::number(attempt->loopIteration.value)
            : QString(),
        measurement ? measurement->name : QString(),
        measurement ? variantText(measurement->value) : QString(),
        measurement ? measurement->unit : QString(),
        measurement ? optionalLimit(measurement->hasLowerLimit, measurement->lowerLimit) : QString(),
        measurement ? optionalLimit(measurement->hasUpperLimit, measurement->upperLimit) : QString(),
        measurement ? PicoATE::Core::measurementStatusName(measurement->status) : QString(),
        errorCode,
        errorMessage,
    };
    QStringList escaped;
    for (const auto& cell : cells) escaped.push_back(csvCell(cell));
    return escaped.join(',') + "\r\n";
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

ReportExportResult ReportExporter::saveJson(
    const QString& filePath,
    const PicoATE::Core::ExecutionReport& report)
{
    return writeFile(filePath, PicoATE::Core::serializeExecutionReport(report));
}

ReportExportResult ReportExporter::saveCsv(
    const QString& filePath,
    const PicoATE::Core::ExecutionReport& report)
{
    QByteArray csv("\xEF\xBB\xBF");
    csv += "\"UUT\",\"Step ID\",\"Step Name\",\"Step Outcome\",\"Attempt\","
           "\"Attempt Outcome\",\"Loop Iteration\",\"Loop Value\",\"Measurement\","
           "\"Value\",\"Unit\",\"Lower Limit\",\"Upper Limit\",\"Measurement Status\","
           "\"Error Code\",\"Error Message\"\r\n";
    for (const auto& uut : report.uuts) {
        for (const auto& step : uut.steps) {
            if (step.attempts.isEmpty()) {
                csv += csvRow(uut, step, nullptr, nullptr).toUtf8();
                continue;
            }
            for (const auto& attempt : step.attempts) {
                if (attempt.measurements.isEmpty()) {
                    csv += csvRow(uut, step, &attempt, nullptr).toUtf8();
                    continue;
                }
                for (const auto& measurement : attempt.measurements) {
                    csv += csvRow(uut, step, &attempt, &measurement).toUtf8();
                }
            }
        }
    }
    return writeFile(filePath, csv);
}

} // namespace PicoATE::Ui
