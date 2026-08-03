#include "PicoATE/Core/MeasurementTypes.h"

#include <QMetaType>
#include <cmath>

namespace PicoATE::Core {

namespace {

bool readDoubleLimit(const QVariantMap& map,
                     std::initializer_list<const char*> keys,
                     double& value)
{
    for (const auto* key : keys) {
        if (!map.contains(QString::fromLatin1(key)) || map.value(QString::fromLatin1(key)).isNull()) {
            continue;
        }
        bool ok = false;
        const double parsed = map.value(QString::fromLatin1(key)).toDouble(&ok);
        if (!ok) {
            continue;
        }
        value = parsed;
        return true;
    }
    return false;
}

QString normalizedComparison(QString value)
{
    value = value.trimmed().toLower();
    if (value == ">" || value == ">=" || value == "<" || value == "<=" ||
        value == "==" || value == "!=") {
        return value;
    }
    value.remove('-');
    value.remove('_');
    value.remove(' ');
    return value;
}

bool configuredValue(const QVariantMap& configuration,
                     const QString& key,
                     QVariant& value)
{
    const auto inputs = configuration.value(QStringLiteral("inputs")).toMap();
    if (inputs.contains(key)) {
        value = inputs.value(key);
        return true;
    }
    if (configuration.contains(key)) {
        value = configuration.value(key);
        return true;
    }
    return false;
}

bool finiteNumber(const QVariant& value, double& number)
{
    if (!value.isValid() || value.isNull() ||
        value.metaType().id() == QMetaType::Bool ||
        value.metaType().id() == QMetaType::QVariantMap ||
        value.metaType().id() == QMetaType::QVariantList) {
        return false;
    }
    bool ok = false;
    number = value.toDouble(&ok);
    return ok && std::isfinite(number);
}

QString displayValue(const QVariant& value)
{
    if (!value.isValid() || value.isNull()) {
        return {};
    }
    if (value.metaType().id() == QMetaType::Bool) {
        return value.toBool() ? QStringLiteral("true") : QStringLiteral("false");
    }
    return value.toString();
}

void setDisplayRange(MeasurementResult& measurement,
                     const QVariant& lower,
                     const QVariant& upper)
{
    if (lower.isValid() && !lower.isNull()) {
        measurement.attributes.insert(QStringLiteral("displayLower"), lower);
    }
    if (upper.isValid() && !upper.isNull()) {
        measurement.attributes.insert(QStringLiteral("displayUpper"), upper);
    }
}

MeasurementStatus statusFromOutcomeLikeValue(const QVariantMap& map,
                                             MeasurementStatus defaultStatus)
{
    if (map.contains("status")) {
        return measurementStatusFromString(map.value("status").toString());
    }
    if (map.contains("result")) {
        return measurementStatusFromString(map.value("result").toString());
    }
    if (map.contains("outcome")) {
        return measurementStatusFromString(map.value("outcome").toString());
    }
    if (map.contains("passed")) {
        return map.value("passed").toBool() ? MeasurementStatus::Passed : MeasurementStatus::Failed;
    }
    return defaultStatus;
}

} // namespace

QString measurementStatusName(MeasurementStatus status)
{
    switch (status) {
    case MeasurementStatus::Passed:
        return "Passed";
    case MeasurementStatus::Failed:
        return "Failed";
    case MeasurementStatus::Error:
        return "Error";
    case MeasurementStatus::Skipped:
        return "Skipped";
    case MeasurementStatus::Unknown:
        return "Unknown";
    }
    return "Unknown";
}

MeasurementStatus measurementStatusFromString(const QString& value)
{
    if (value.compare("Passed", Qt::CaseInsensitive) == 0 ||
        value.compare("Pass", Qt::CaseInsensitive) == 0) {
        return MeasurementStatus::Passed;
    }
    if (value.compare("Failed", Qt::CaseInsensitive) == 0 ||
        value.compare("Fail", Qt::CaseInsensitive) == 0 ||
        value.compare("LimitFail", Qt::CaseInsensitive) == 0) {
        return MeasurementStatus::Failed;
    }
    if (value.compare("Error", Qt::CaseInsensitive) == 0 ||
        value.compare("Timeout", Qt::CaseInsensitive) == 0) {
        return MeasurementStatus::Error;
    }
    if (value.compare("Skipped", Qt::CaseInsensitive) == 0) {
        return MeasurementStatus::Skipped;
    }
    return MeasurementStatus::Unknown;
}

QVariantMap measurementToMap(const MeasurementResult& measurement)
{
    QVariantMap map = measurement.attributes;
    map.insert("name", measurement.name);
    if (measurement.value.isValid()) {
        map.insert("value", measurement.value);
    }
    if (!measurement.unit.isEmpty()) {
        map.insert("unit", measurement.unit);
    }
    if (measurement.rawValue.isValid()) {
        map.insert("rawValue", measurement.rawValue);
    }
    if (measurement.hasLowerLimit) {
        map.insert("lowerLimit", measurement.lowerLimit);
        map.insert("min", measurement.lowerLimit);
    }
    if (measurement.hasUpperLimit) {
        map.insert("upperLimit", measurement.upperLimit);
        map.insert("max", measurement.upperLimit);
    }
    map.insert("status", measurementStatusName(measurement.status));
    map.insert("passed", measurement.status == MeasurementStatus::Passed);
    if (!measurement.errorCode.isEmpty()) {
        map.insert("errorCode", measurement.errorCode);
    }
    if (!measurement.errorMessage.isEmpty()) {
        map.insert("errorMessage", measurement.errorMessage);
    }
    return map;
}

MeasurementResult measurementFromMap(const QVariantMap& map,
                                     MeasurementStatus defaultStatus)
{
    MeasurementResult measurement;
    measurement.name = map.value("name").toString();
    measurement.value = map.value("value");
    measurement.unit = map.value("unit").toString();
    measurement.rawValue = map.value("rawValue");
    measurement.status = statusFromOutcomeLikeValue(map, defaultStatus);
    measurement.errorCode = map.value("errorCode").toString();
    measurement.errorMessage = map.value("errorMessage").toString();

    measurement.hasLowerLimit = readDoubleLimit(map, {"lowerLimit", "lower", "min"}, measurement.lowerLimit);
    measurement.hasUpperLimit = readDoubleLimit(map, {"upperLimit", "upper", "max"}, measurement.upperLimit);

    measurement.attributes = map;
    measurement.attributes.remove("name");
    measurement.attributes.remove("value");
    measurement.attributes.remove("unit");
    measurement.attributes.remove("rawValue");
    measurement.attributes.remove("lowerLimit");
    measurement.attributes.remove("lower");
    measurement.attributes.remove("min");
    measurement.attributes.remove("upperLimit");
    measurement.attributes.remove("upper");
    measurement.attributes.remove("max");
    measurement.attributes.remove("status");
    measurement.attributes.remove("result");
    measurement.attributes.remove("outcome");
    measurement.attributes.remove("passed");
    measurement.attributes.remove("errorCode");
    measurement.attributes.remove("errorMessage");
    return measurement;
}

QVector<MeasurementResult> measurementsFromVariant(const QVariant& value,
                                                   MeasurementStatus defaultStatus)
{
    QVector<MeasurementResult> measurements;
    if (!value.isValid() || value.isNull()) {
        return measurements;
    }

    if (value.metaType().id() == QMetaType::QVariantList) {
        const auto list = value.toList();
        measurements.reserve(list.size());
        for (const auto& item : list) {
            const auto map = item.toMap();
            if (!map.isEmpty()) {
                measurements.push_back(measurementFromMap(map, defaultStatus));
            }
        }
        return measurements;
    }

    const auto map = value.toMap();
    if (map.isEmpty()) {
        return measurements;
    }
    if (map.value("items").metaType().id() == QMetaType::QVariantList) {
        return measurementsFromVariant(map.value("items"), defaultStatus);
    }

    measurements.push_back(measurementFromMap(map, defaultStatus));
    return measurements;
}

QVariant measurementsToVariant(const QVector<MeasurementResult>& measurements)
{
    if (measurements.isEmpty()) {
        return QVariantMap{};
    }
    if (measurements.size() == 1) {
        return measurementToMap(measurements.first());
    }

    QVariantList list;
    list.reserve(measurements.size());
    for (const auto& measurement : measurements) {
        list.push_back(measurementToMap(measurement));
    }
    return list;
}

bool measurementStatusIsError(MeasurementStatus status)
{
    return status == MeasurementStatus::Failed ||
           status == MeasurementStatus::Error;
}

void applyConfiguredMeasurementLimits(const QVariantMap& configuration,
                                      MeasurementResult& measurement)
{
    const auto comparison = normalizedComparison(
        configuration.value(QStringLiteral("comparison"), QStringLiteral("between")).toString());
    const bool inclusive = configuration.value(QStringLiteral("inclusive"), true).toBool();
    measurement.attributes.insert(QStringLiteral("comparison"), comparison);
    measurement.attributes.insert(QStringLiteral("inclusive"), inclusive);

    QVariant lowerValue;
    QVariant upperValue;
    QVariant expectedValue;
    QVariant toleranceValue;
    const bool hasLower = configuredValue(configuration, QStringLiteral("lower"), lowerValue) ||
                          configuredValue(configuration, QStringLiteral("lowerLimit"), lowerValue);
    const bool hasUpper = configuredValue(configuration, QStringLiteral("upper"), upperValue) ||
                          configuredValue(configuration, QStringLiteral("upperLimit"), upperValue);
    const bool hasExpected = configuredValue(configuration, QStringLiteral("expected"), expectedValue);
    const bool hasTolerance = configuredValue(configuration, QStringLiteral("tolerance"), toleranceValue);

    if (hasExpected) {
        measurement.attributes.insert(QStringLiteral("expected"), expectedValue);
    }
    if (hasTolerance) {
        measurement.attributes.insert(QStringLiteral("tolerance"), toleranceValue);
    }

    double lower = 0.0;
    double upper = 0.0;
    double expected = 0.0;
    double tolerance = 0.0;
    const bool between = comparison == QStringLiteral("between") ||
                         comparison == QStringLiteral("range");
    if (between && hasLower && hasUpper) {
        if (finiteNumber(lowerValue, lower) && finiteNumber(upperValue, upper)) {
            measurement.hasLowerLimit = true;
            measurement.lowerLimit = lower;
            measurement.hasUpperLimit = true;
            measurement.upperLimit = upper;
        } else {
            setDisplayRange(measurement, lowerValue, upperValue);
        }
        return;
    }

    if (between && hasExpected && hasTolerance) {
        if (finiteNumber(expectedValue, expected) && finiteNumber(toleranceValue, tolerance)) {
            measurement.hasLowerLimit = true;
            measurement.lowerLimit = expected - tolerance;
            measurement.hasUpperLimit = true;
            measurement.upperLimit = expected + tolerance;
            measurement.attributes.insert(QStringLiteral("limitsDerived"), true);
        } else {
            const auto expectedText = displayValue(expectedValue);
            const auto toleranceText = displayValue(toleranceValue);
            setDisplayRange(
                measurement,
                QStringLiteral("%1 - %2").arg(expectedText, toleranceText),
                QStringLiteral("%1 + %2").arg(expectedText, toleranceText));
        }
        return;
    }

    const bool equality = comparison == QStringLiteral("==") ||
        comparison == QStringLiteral("eq") || comparison == QStringLiteral("equal") ||
        comparison == QStringLiteral("!=") || comparison == QStringLiteral("ne") ||
        comparison == QStringLiteral("notequal");
    if (equality && hasExpected) {
        if (finiteNumber(expectedValue, expected) &&
            (!hasTolerance || finiteNumber(toleranceValue, tolerance))) {
            measurement.hasLowerLimit = true;
            measurement.lowerLimit = expected - tolerance;
            measurement.hasUpperLimit = true;
            measurement.upperLimit = expected + tolerance;
            measurement.attributes.insert(QStringLiteral("limitsDerived"), true);
        } else {
            setDisplayRange(measurement, expectedValue, expectedValue);
        }
        return;
    }

    const bool lowerBound = comparison == QStringLiteral(">") ||
        comparison == QStringLiteral("gt") || comparison == QStringLiteral("greaterthan") ||
        comparison == QStringLiteral(">=") || comparison == QStringLiteral("ge") ||
        comparison == QStringLiteral("gte") || comparison == QStringLiteral("greaterorequal");
    const bool upperBound = comparison == QStringLiteral("<") ||
        comparison == QStringLiteral("lt") || comparison == QStringLiteral("lessthan") ||
        comparison == QStringLiteral("<=") || comparison == QStringLiteral("le") ||
        comparison == QStringLiteral("lte") || comparison == QStringLiteral("lessorequal");
    const auto thresholdValue = hasExpected
        ? expectedValue
        : (lowerBound ? lowerValue : upperValue);
    if (lowerBound || upperBound) {
        if (finiteNumber(thresholdValue, expected)) {
            measurement.hasLowerLimit = lowerBound;
            measurement.lowerLimit = expected;
            measurement.hasUpperLimit = upperBound;
            measurement.upperLimit = expected;
        } else if (lowerBound) {
            setDisplayRange(measurement, thresholdValue, {});
        } else {
            setDisplayRange(measurement, {}, thresholdValue);
        }
        return;
    }

    if (comparison == QStringLiteral("istrue") || comparison == QStringLiteral("isfalse")) {
        const auto expectedBoolean = comparison == QStringLiteral("istrue");
        setDisplayRange(measurement, expectedBoolean, expectedBoolean);
        return;
    }

    if (hasExpected) {
        setDisplayRange(measurement, expectedValue, expectedValue);
    }
}

MeasurementResult configuredMeasurementPreview(const QVariantMap& configuration,
                                               const QString& defaultName)
{
    MeasurementResult measurement;
    measurement.name = configuration.value(QStringLiteral("measurementName"), defaultName).toString();
    if (measurement.name.trimmed().isEmpty()) {
        measurement.name = defaultName;
    }
    measurement.unit = configuration.value(QStringLiteral("unit")).toString();
    measurement.status = MeasurementStatus::Unknown;
    applyConfiguredMeasurementLimits(configuration, measurement);
    return measurement;
}

} // namespace PicoATE::Core
