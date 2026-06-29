#include "PicoATE/Core/MeasurementTypes.h"

#include <QMetaType>

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

} // namespace PicoATE::Core
