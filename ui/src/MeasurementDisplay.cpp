#include "MeasurementDisplay.h"

#include "PicoATE/Core/MeasurementTypes.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>

namespace PicoATE::Ui {

namespace {

enum class ComparisonKind {
    Between,
    Equal,
    NotEqual,
    Greater,
    GreaterOrEqual,
    Less,
    LessOrEqual,
    Contains,
    StartsWith,
    EndsWith,
    Boolean,
    Unknown,
};

QString variantText(const QVariant& value)
{
    if (!value.isValid() || value.isNull()) {
        return {};
    }
    const auto json = QJsonValue::fromVariant(value);
    if (json.isObject()) {
        return QString::fromUtf8(
            QJsonDocument(json.toObject()).toJson(QJsonDocument::Compact));
    }
    if (json.isArray()) {
        return QString::fromUtf8(
            QJsonDocument(json.toArray()).toJson(QJsonDocument::Compact));
    }
    if (json.isBool()) {
        return json.toBool() ? QStringLiteral("true")
                             : QStringLiteral("false");
    }
    if (json.isDouble()) {
        return QString::number(json.toDouble(), 'g', 15);
    }
    return value.toString();
}

QString normalizedComparison(QString value)
{
    value = value.trimmed().toLower();
    value.remove(QLatin1Char('-'));
    value.remove(QLatin1Char('_'));
    value.remove(QLatin1Char(' '));
    return value;
}

ComparisonKind comparisonKind(
    const PicoATE::Core::MeasurementResult& measurement)
{
    const auto comparison = normalizedComparison(
        measurement.attributes.value(QStringLiteral("comparison")).toString());
    if (comparison == QStringLiteral("between") ||
        comparison == QStringLiteral("range")) {
        return ComparisonKind::Between;
    }
    if (comparison == QStringLiteral("==") ||
        comparison == QStringLiteral("eq") ||
        comparison == QStringLiteral("equal")) {
        return ComparisonKind::Equal;
    }
    if (comparison == QStringLiteral("!=") ||
        comparison == QStringLiteral("ne") ||
        comparison == QStringLiteral("notequal")) {
        return ComparisonKind::NotEqual;
    }
    if (comparison == QStringLiteral(">") ||
        comparison == QStringLiteral("gt") ||
        comparison == QStringLiteral("greaterthan")) {
        return ComparisonKind::Greater;
    }
    if (comparison == QStringLiteral(">=") ||
        comparison == QStringLiteral("ge") ||
        comparison == QStringLiteral("gte") ||
        comparison == QStringLiteral("greaterorequal")) {
        return ComparisonKind::GreaterOrEqual;
    }
    if (comparison == QStringLiteral("<") ||
        comparison == QStringLiteral("lt") ||
        comparison == QStringLiteral("lessthan")) {
        return ComparisonKind::Less;
    }
    if (comparison == QStringLiteral("<=") ||
        comparison == QStringLiteral("le") ||
        comparison == QStringLiteral("lte") ||
        comparison == QStringLiteral("lessorequal")) {
        return ComparisonKind::LessOrEqual;
    }
    if (comparison == QStringLiteral("contains")) {
        return ComparisonKind::Contains;
    }
    if (comparison == QStringLiteral("startswith")) {
        return ComparisonKind::StartsWith;
    }
    if (comparison == QStringLiteral("endswith")) {
        return ComparisonKind::EndsWith;
    }
    if (comparison == QStringLiteral("istrue") ||
        comparison == QStringLiteral("isfalse")) {
        return ComparisonKind::Boolean;
    }
    if (comparison.isEmpty()) {
        if (measurement.hasLowerLimit && measurement.hasUpperLimit) {
            return ComparisonKind::Between;
        }
        if (measurement.hasLowerLimit) {
            return ComparisonKind::GreaterOrEqual;
        }
        if (measurement.hasUpperLimit) {
            return ComparisonKind::LessOrEqual;
        }
    }
    return ComparisonKind::Unknown;
}

bool belongsInColumn(ComparisonKind kind, bool lower)
{
    switch (kind) {
    case ComparisonKind::Greater:
    case ComparisonKind::GreaterOrEqual:
    case ComparisonKind::Contains:
    case ComparisonKind::StartsWith:
    case ComparisonKind::EndsWith:
        return lower;
    case ComparisonKind::Less:
    case ComparisonKind::LessOrEqual:
        return !lower;
    default:
        return true;
    }
}

QString comparisonOperator(
    const PicoATE::Core::MeasurementResult& measurement,
    ComparisonKind kind,
    bool lower)
{
    switch (kind) {
    case ComparisonKind::Between: {
        const bool inclusive = measurement.attributes
                                   .value(QStringLiteral("inclusive"), true)
                                   .toBool();
        return lower ? (inclusive ? QStringLiteral(">=")
                                  : QStringLiteral(">"))
                     : (inclusive ? QStringLiteral("<=")
                                  : QStringLiteral("<"));
    }
    case ComparisonKind::Equal: {
        bool toleranceOk = false;
        const double tolerance = measurement.attributes
                                     .value(QStringLiteral("tolerance"), 0.0)
                                     .toDouble(&toleranceOk);
        if (toleranceOk && tolerance > 0.0) {
            return lower ? QStringLiteral(">=") : QStringLiteral("<=");
        }
        return QStringLiteral("=");
    }
    case ComparisonKind::NotEqual:
        return QStringLiteral("!=");
    case ComparisonKind::Greater:
        return QStringLiteral(">");
    case ComparisonKind::GreaterOrEqual:
        return QStringLiteral(">=");
    case ComparisonKind::Less:
        return QStringLiteral("<");
    case ComparisonKind::LessOrEqual:
        return QStringLiteral("<=");
    case ComparisonKind::Contains:
        return QStringLiteral("CONTAINS");
    case ComparisonKind::StartsWith:
        return QStringLiteral("STARTS WITH");
    case ComparisonKind::EndsWith:
        return QStringLiteral("ENDS WITH");
    case ComparisonKind::Boolean:
        return QStringLiteral("=");
    case ComparisonKind::Unknown:
        return {};
    }
    return {};
}

QString valueWithUnit(QString value,
                      const PicoATE::Core::MeasurementResult& measurement)
{
    value = value.trimmed();
    const auto unit = measurement.unit.trimmed();
    if (value.isEmpty() || unit.isEmpty()) {
        return value;
    }
    if (value.endsWith(QStringLiteral(" %1").arg(unit),
                       Qt::CaseInsensitive)) {
        return value;
    }
    return QStringLiteral("%1 %2").arg(value, unit);
}

QString rawLimitValue(const PicoATE::Core::MeasurementResult& measurement,
                      bool lower)
{
    const auto display = measurement.attributes.value(
        lower ? QStringLiteral("displayLower")
              : QStringLiteral("displayUpper"));
    if (display.isValid() && !display.isNull()) {
        return variantText(display);
    }
    if (lower && measurement.hasLowerLimit) {
        return QString::number(measurement.lowerLimit, 'g', 15);
    }
    if (!lower && measurement.hasUpperLimit) {
        return QString::number(measurement.upperLimit, 'g', 15);
    }
    const auto expected = measurement.attributes.value(
        QStringLiteral("expected"));
    return variantText(expected);
}

QString limitDisplay(const PicoATE::Core::MeasurementResult& measurement,
                     bool lower)
{
    const auto kind = comparisonKind(measurement);
    if (!belongsInColumn(kind, lower)) {
        return QStringLiteral("-");
    }
    const auto rawValue = rawLimitValue(measurement, lower);
    if (rawValue.isEmpty()) {
        return QStringLiteral("-");
    }
    const auto value = valueWithUnit(rawValue, measurement);
    const auto operation = comparisonOperator(measurement, kind, lower);
    return operation.isEmpty()
        ? value
        : QStringLiteral("%1 %2").arg(operation, value);
}

} // namespace

QString measurementActualDisplay(
    const PicoATE::Core::MeasurementResult& measurement)
{
    if (!measurement.value.isValid() || measurement.value.isNull()) {
        return QStringLiteral("-");
    }
    return valueWithUnit(variantText(measurement.value), measurement);
}

QString measurementLowerLimitDisplay(
    const PicoATE::Core::MeasurementResult& measurement)
{
    return limitDisplay(measurement, true);
}

QString measurementUpperLimitDisplay(
    const PicoATE::Core::MeasurementResult& measurement)
{
    return limitDisplay(measurement, false);
}

QString measurementLimitsDisplay(
    const PicoATE::Core::MeasurementResult& measurement)
{
    const auto lower = measurementLowerLimitDisplay(measurement);
    const auto upper = measurementUpperLimitDisplay(measurement);
    if (lower == QStringLiteral("-")) {
        return upper;
    }
    if (upper == QStringLiteral("-") || upper == lower) {
        return lower;
    }
    return QStringLiteral("%1 .. %2").arg(lower, upper);
}

} // namespace PicoATE::Ui
