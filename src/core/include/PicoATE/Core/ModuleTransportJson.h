#pragma once

#include "PicoATE/Core/ModuleRuntime.h"

#include <QJsonObject>

namespace PicoATE::Core {

QJsonObject moduleTransportRequestToJson(const ModuleTransportRequest& request);
ModuleTransportRequest moduleTransportRequestFromJson(const QJsonObject& json);
QJsonObject moduleTransportResponseToJson(const ModuleTransportResponse& response);
ModuleTransportResponse moduleTransportResponseFromJson(const QJsonObject& json);

} // namespace PicoATE::Core
