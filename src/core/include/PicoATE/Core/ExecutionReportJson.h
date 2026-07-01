#pragma once

#include "PicoATE/Core/ExecutionReport.h"

#include <QByteArray>
#include <QJsonObject>

namespace PicoATE::Core {

inline constexpr int ExecutionReportSchemaVersion = 3;

struct ExecutionReportJsonError {
    QString path;
    QString message;
};

struct ExecutionReportJsonResult {
    ExecutionReport report;
    QVector<ExecutionReportJsonError> errors;

    bool ok() const { return errors.isEmpty(); }
};

QJsonObject executionReportToJson(const ExecutionReport& report);
QByteArray serializeExecutionReport(const ExecutionReport& report);
ExecutionReportJsonResult executionReportFromJson(const QJsonObject& object);
ExecutionReportJsonResult parseExecutionReport(const QByteArray& json);

} // namespace PicoATE::Core
