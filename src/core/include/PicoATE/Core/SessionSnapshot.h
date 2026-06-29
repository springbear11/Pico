#pragma once

#include "PicoATE/Core/BarrierController.h"
#include "PicoATE/Core/ResourceManager.h"

namespace PicoATE::Core {

struct ExecutionSessionSnapshot {
    QString executionId;
    PlanId rootPlanId;
    ExecutionState state = ExecutionState::Idle;
    QVector<UutExecution> uuts;
    QVector<ExecutionFrame> frames;
    ResourceSnapshot resources;
    BarrierSnapshot barriers;
    QDateTime savedAt = QDateTime::currentDateTimeUtc();
    QString runtimeVersion;
};

class ISessionPersistence {
public:
    virtual ~ISessionPersistence() = default;
    virtual QString saveCheckpoint(const ExecutionSessionSnapshot& snapshot) = 0;
    virtual std::optional<ExecutionSessionSnapshot> restore(const QString& checkpointId) = 0;
};

} // namespace PicoATE::Core
