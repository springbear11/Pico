#pragma once

#include "PicoATE/Core/ModuleRuntime.h"

#include <QJsonObject>

namespace PicoATE::Core {

enum class ModuleProtocolMessageKind {
    Invalid,
    Log,
    Response
};

struct ModuleProtocolMessage {
    ModuleProtocolMessageKind kind = ModuleProtocolMessageKind::Invalid;
    QString traceId;
    ModuleLogRecord log;
    ModuleTransportResponse response;
    QString errorMessage;
};

QJsonObject moduleTransportRequestToJson(const ModuleTransportRequest& request);
ModuleTransportRequest moduleTransportRequestFromJson(const QJsonObject& json);
QJsonObject moduleTransportResponseToJson(const ModuleTransportResponse& response);
ModuleTransportResponse moduleTransportResponseFromJson(const QJsonObject& json);
QJsonObject moduleLogMessageToJson(const QString& traceId, const ModuleLogRecord& record);
QJsonObject moduleResponseMessageToJson(const QString& traceId,
                                        const ModuleTransportResponse& response);
ModuleProtocolMessage moduleProtocolMessageFromJson(const QJsonObject& json);

} // namespace PicoATE::Core
