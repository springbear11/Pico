#pragma once

#include "PicoATE/Core/BarrierController.h"
#include "PicoATE/Core/ExecutionPlan.h"

#include <QHash>
#include <QSet>
#include <QVector>

namespace PicoATE::Core {

struct BarrierRuntimeTransition {
    NodeId barrierNodeId;
    BarrierReleaseDecision decision;
};

class BarrierRuntimeCoordinator {
public:
    BarrierRuntimeCoordinator(const ExecutionPlan& plan,
                              BarrierController& barriers);

    void setCohortUuts(const QSet<UutId>& uutIds);
    BarrierRuntimeTransition memberArrived(const ExecNode& barrierNode,
                                           const UutId& uutId,
                                           const FrameId& frameId);
    QVector<BarrierRuntimeTransition> memberFailedBeforeReachableBarriers(
        const ExecNode& failedNode,
        const UutId& uutId,
        NodeOutcome outcome);
    void queueRelease(const BarrierReleaseDecision& decision);
    QVector<BarrierRuntimeTransition> takePendingReleases();

private:
    BarrierNodePayload payloadFromNode(const ExecNode& node) const;
    BarrierInstanceId instanceForNode(const ExecNode& node,
                                      const UutId& uutId);
    bool hasPath(const NodeId& from, const NodeId& to) const;

    const ExecutionPlan& m_plan;
    BarrierController& m_barriers;
    QSet<UutId> m_cohortUuts;
    QHash<BarrierInstanceId, BarrierReleaseDecision> m_pendingReleases;
    QHash<NodeId, BarrierInstanceId> m_barrierByNode;
    QHash<BarrierInstanceId, NodeId> m_nodeByBarrier;
};

} // namespace PicoATE::Core
