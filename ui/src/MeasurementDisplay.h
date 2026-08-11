#pragma once

#include <QString>

namespace PicoATE::Core {
struct MeasurementResult;
}

namespace PicoATE::Ui {

QString measurementActualDisplay(
    const PicoATE::Core::MeasurementResult& measurement);
QString measurementLowerLimitDisplay(
    const PicoATE::Core::MeasurementResult& measurement);
QString measurementUpperLimitDisplay(
    const PicoATE::Core::MeasurementResult& measurement);
QString measurementLimitsDisplay(
    const PicoATE::Core::MeasurementResult& measurement);

} // namespace PicoATE::Ui
