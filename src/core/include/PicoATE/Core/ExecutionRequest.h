#pragma once

#include "PicoATE/Core/ExecutionPlan.h"

namespace PicoATE::Core {

RequestId createRequestId(const QString& scope = {});

} // namespace PicoATE::Core
