#include "PicoATE/Core/ExecutionRequest.h"

#include <QUuid>

namespace PicoATE::Core {

RequestId createRequestId(const QString& scope)
{
    const auto value = QUuid::createUuid().toString(QUuid::WithoutBraces);
    const auto normalizedScope = scope.trimmed();
    return normalizedScope.isEmpty()
        ? value
        : QStringLiteral("%1:%2").arg(normalizedScope, value);
}

} // namespace PicoATE::Core
