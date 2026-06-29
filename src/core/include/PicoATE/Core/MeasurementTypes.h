#pragma once

#include <QString>
#include <QVariant>
#include <QVariantMap>
#include <QVector>

namespace PicoATE::Core {

enum class MeasurementStatus {
    Unknown,
    Passed,
    Failed,
    Error,
    Skipped
};

struct MeasurementResult {
    QString name;
    QVariant value;
    QString unit;
    QVariant rawValue;
    bool hasLowerLimit = false;
    double lowerLimit = 0.0;
    bool hasUpperLimit = false;
    double upperLimit = 0.0;
    MeasurementStatus status = MeasurementStatus::Unknown;
    QString errorCode;
    QString errorMessage;
    QVariantMap attributes;
};

QString measurementStatusName(MeasurementStatus status);
MeasurementStatus measurementStatusFromString(const QString& value);
QVariantMap measurementToMap(const MeasurementResult& measurement);
MeasurementResult measurementFromMap(const QVariantMap& map,
                                     MeasurementStatus defaultStatus = MeasurementStatus::Unknown);
QVector<MeasurementResult> measurementsFromVariant(const QVariant& value,
                                                   MeasurementStatus defaultStatus = MeasurementStatus::Unknown);
QVariant measurementsToVariant(const QVector<MeasurementResult>& measurements);
bool measurementStatusIsError(MeasurementStatus status);

} // namespace PicoATE::Core
