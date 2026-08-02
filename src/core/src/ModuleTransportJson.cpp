#include "PicoATE/Core/ModuleTransportJson.h"

#include <QJsonArray>
#include <QJsonValue>

#include <utility>

namespace PicoATE::Core {

namespace {

QString outcomeToString(ModuleOutcome outcome)
{
    switch (outcome) {
    case ModuleOutcome::Passed:
        return "Passed";
    case ModuleOutcome::Failed:
        return "Failed";
    case ModuleOutcome::Error:
        return "Error";
    case ModuleOutcome::Timeout:
        return "Timeout";
    }
    return "Error";
}

ModuleOutcome outcomeFromString(const QString& value)
{
    if (value.compare("Passed", Qt::CaseInsensitive) == 0) {
        return ModuleOutcome::Passed;
    }
    if (value.compare("Failed", Qt::CaseInsensitive) == 0) {
        return ModuleOutcome::Failed;
    }
    if (value.compare("Timeout", Qt::CaseInsensitive) == 0) {
        return ModuleOutcome::Timeout;
    }
    return ModuleOutcome::Error;
}

QJsonObject mapToJsonObject(const QVariantMap& map)
{
    return QJsonObject::fromVariantMap(map);
}

QVariantMap mapFromJsonObject(const QJsonObject& object)
{
    return object.toVariantMap();
}

QJsonObject logRecordToJson(const ModuleLogRecord& record)
{
    QJsonObject log;
    log.insert("sourceSequence", static_cast<double>(record.sourceSequence));
    log.insert("timestampUtc", record.timestampUtc.toString(Qt::ISODateWithMs));
    log.insert("message", record.message);
    log.insert("droppedBefore", static_cast<double>(record.droppedBefore));
    return log;
}

bool logRecordFromJson(const QJsonValue& value,
                       ModuleLogRecord& record,
                       QString& errorMessage)
{
    if (!value.isObject()) {
        errorMessage = "Module log entry must be an object";
        return false;
    }

    const auto log = value.toObject();
    record.sourceSequence = static_cast<quint64>(
        log.value("sourceSequence").toDouble());
    record.timestampUtc = QDateTime::fromString(
        log.value("timestampUtc").toString(), Qt::ISODateWithMs);
    record.message = log.value("message").toString();
    record.droppedBefore = static_cast<quint64>(
        log.value("droppedBefore").toDouble());
    if (record.message.isEmpty()) {
        errorMessage = "Module log message is empty";
        return false;
    }
    return true;
}

} // namespace

QJsonObject moduleTransportRequestToJson(const ModuleTransportRequest& request)
{
    QJsonObject context;
    context.insert("uutId", request.context.uutId);
    context.insert("frameId", request.context.frameId);
    context.insert("attemptId", request.context.attemptId);
    context.insert("requestId", request.context.requestId);
    context.insert("attemptIndex", request.context.attemptIndex);
    context.insert("inputs", mapToJsonObject(request.context.inputs));
    context.insert("parameters", mapToJsonObject(request.context.parameters));
    context.insert("variables", mapToJsonObject(request.context.variables));

    QJsonObject json;
    json.insert("requestId", request.requestId);
    json.insert("traceId", request.traceId);
    json.insert("moduleId", request.moduleId);
    json.insert("function", request.functionName);
    json.insert("context", context);
    return json;
}

ModuleTransportRequest moduleTransportRequestFromJson(const QJsonObject& json)
{
    ModuleTransportRequest request;
    request.requestId = json.value("requestId").toString();
    request.traceId = json.value("traceId").toString();
    request.moduleId = json.value("moduleId").toString();
    request.functionName = json.value("function").toString();

    const auto context = json.value("context").toObject();
    request.context.uutId = context.value("uutId").toString();
    request.context.frameId = context.value("frameId").toString();
    request.context.attemptId = context.value("attemptId").toString();
    request.context.requestId = context.value("requestId").toString(request.requestId);
    request.context.attemptIndex = context.value("attemptIndex").toInt();
    request.context.inputs = mapFromJsonObject(context.value("inputs").toObject());
    request.context.parameters = mapFromJsonObject(context.value("parameters").toObject());
    request.context.variables = mapFromJsonObject(context.value("variables").toObject());
    return request;
}

QJsonObject moduleTransportResponseToJson(const ModuleTransportResponse& response)
{
    QJsonObject json;
    json.insert("outcome", outcomeToString(response.outcome));
    json.insert("outputs", mapToJsonObject(response.outputs));
    json.insert("measurements", QJsonValue::fromVariant(measurementsToVariant(response.measurements)));
    json.insert("errorCode", response.errorCode);
    json.insert("errorMessage", response.errorMessage);
    if (!response.diagnostics.isEmpty()) {
        json.insert("diagnostics", mapToJsonObject(response.diagnostics));
    }
    return json;
}

ModuleTransportResponse moduleTransportResponseFromJson(const QJsonObject& json)
{
    ModuleTransportResponse response;
    response.outcome = outcomeFromString(json.value("outcome").toString("Error"));
    response.outputs = mapFromJsonObject(json.value("outputs").toObject());
    response.measurements = measurementsFromVariant(
        json.value("measurements").toVariant(),
        toMeasurementStatus(response.outcome));
    response.errorCode = json.value("errorCode").toString();
    response.errorMessage = json.value("errorMessage").toString();
    response.diagnostics = mapFromJsonObject(json.value("diagnostics").toObject());
    return response;
}

QJsonObject moduleLogMessageToJson(const QString& traceId, const ModuleLogRecord& record)
{
    QJsonObject json;
    json.insert("type", "moduleLog");
    json.insert("traceId", traceId);
    json.insert("log", logRecordToJson(record));
    return json;
}

QJsonObject moduleLogBatchMessageToJson(const QString& traceId,
                                        const QVector<ModuleLogRecord>& records)
{
    QJsonArray logs;
    for (const auto& record : records) {
        logs.push_back(logRecordToJson(record));
    }

    QJsonObject json;
    json.insert("type", "moduleLogBatch");
    json.insert("traceId", traceId);
    json.insert("logs", logs);
    return json;
}

QJsonObject moduleResponseMessageToJson(const QString& traceId,
                                        const ModuleTransportResponse& response)
{
    QJsonObject json;
    json.insert("type", "moduleResponse");
    json.insert("traceId", traceId);
    json.insert("response", moduleTransportResponseToJson(response));
    return json;
}

ModuleProtocolMessage moduleProtocolMessageFromJson(const QJsonObject& json)
{
    ModuleProtocolMessage message;
    const auto type = json.value("type").toString();
    if (type.isEmpty()) {
        message.kind = ModuleProtocolMessageKind::Response;
        message.response = moduleTransportResponseFromJson(json);
        return message;
    }

    message.traceId = json.value("traceId").toString();
    if (type == "moduleLog") {
        message.kind = ModuleProtocolMessageKind::Log;
        if (!logRecordFromJson(json.value("log"), message.log, message.errorMessage)) {
            message.kind = ModuleProtocolMessageKind::Invalid;
        }
        return message;
    }
    if (type == "moduleLogBatch") {
        const auto values = json.value("logs");
        if (!values.isArray() || values.toArray().isEmpty()) {
            message.errorMessage = "Module log batch must contain at least one log entry";
            return message;
        }

        for (const auto& value : values.toArray()) {
            ModuleLogRecord record;
            if (!logRecordFromJson(value, record, message.errorMessage)) {
                message.logs.clear();
                return message;
            }
            message.logs.push_back(std::move(record));
        }
        message.kind = ModuleProtocolMessageKind::LogBatch;
        return message;
    }
    if (type == "moduleResponse") {
        if (!json.value("response").isObject()) {
            message.errorMessage = "Module response envelope is missing response object";
            return message;
        }
        message.kind = ModuleProtocolMessageKind::Response;
        message.response = moduleTransportResponseFromJson(json.value("response").toObject());
        return message;
    }

    message.errorMessage = QString("Unknown module protocol message type: %1").arg(type);
    return message;
}

} // namespace PicoATE::Core
