#include "PicoATE/Core/ModuleTransportJson.h"

#include <QJsonValue>

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

} // namespace

QJsonObject moduleTransportRequestToJson(const ModuleTransportRequest& request)
{
    QJsonObject context;
    context.insert("uutId", request.context.uutId);
    context.insert("frameId", request.context.frameId);
    context.insert("attemptId", request.context.attemptId);
    context.insert("attemptIndex", request.context.attemptIndex);
    context.insert("inputs", mapToJsonObject(request.context.inputs));
    context.insert("parameters", mapToJsonObject(request.context.parameters));
    context.insert("variables", mapToJsonObject(request.context.variables));

    QJsonObject json;
    json.insert("traceId", request.traceId);
    json.insert("moduleId", request.moduleId);
    json.insert("function", request.functionName);
    json.insert("context", context);
    return json;
}

ModuleTransportRequest moduleTransportRequestFromJson(const QJsonObject& json)
{
    ModuleTransportRequest request;
    request.traceId = json.value("traceId").toString();
    request.moduleId = json.value("moduleId").toString();
    request.functionName = json.value("function").toString();

    const auto context = json.value("context").toObject();
    request.context.uutId = context.value("uutId").toString();
    request.context.frameId = context.value("frameId").toString();
    request.context.attemptId = context.value("attemptId").toString();
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
    return response;
}

} // namespace PicoATE::Core
