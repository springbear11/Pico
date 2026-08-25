#pragma once

#include "PicoATE/Core/ExecutionPlan.h"
#include "PicoATE/Core/RuntimeTypes.h"

#include <QHash>
#include <QSet>
#include <QVector>

namespace PicoATE::Core {

struct SharedExecutionArrivalDecision {
    bool applies = false;
    bool execute = false;
    bool waiting = false;
    UutId leaderUutId;
};

struct SharedExecutionLeaderReady {
    NodeId rootNodeId;
    FrameId frameId;
    UutExecution* leader = nullptr;
};

struct SharedExecutionCompletion {
    NodeId rootNodeId;
    FrameId frameId;
    UutExecution* leader = nullptr;
    QVector<UutExecution*> recipients;
};

struct SharedExecutionUpdate {
    QVector<SharedExecutionLeaderReady> readyLeaders;
    QVector<SharedExecutionCompletion> completions;
};

class SharedExecutionController {
public:
    explicit SharedExecutionController(const ExecutionPlan& plan);

    void setCohortUuts(const QSet<UutId>& uutIds);
    SharedExecutionArrivalDecision arrive(const ExecNode& node,
                                          UutExecution& uut,
                                          const FrameId& frameId);
    SharedExecutionUpdate synchronize(const QVector<UutExecution*>& uuts = {});

private:
    struct Instance {
        NodeId rootNodeId;
        FrameId frameId;
        QSet<UutId> expectedUuts;
        QSet<UutId> droppedUuts;
        QVector<UutId> arrivalOrder;
        QHash<UutId, UutExecution*> executions;
        UutId leaderUutId;
        bool ready = false;
        bool completed = false;
    };

    QString instanceKey(const NodeId& nodeId, const FrameId& frameId) const;
    bool releaseIfReady(Instance& instance,
                        QVector<SharedExecutionLeaderReady>& readyLeaders);
    std::optional<SharedExecutionCompletion> completionIfReady(Instance& instance);

    QSet<UutId> m_cohortUuts;
    QHash<QString, Instance> m_instances;
};

} // namespace PicoATE::Core
