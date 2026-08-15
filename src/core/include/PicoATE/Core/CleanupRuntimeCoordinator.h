#pragma once

#include "PicoATE/Core/RuntimeTypes.h"

#include <QVector>

namespace PicoATE::Core {

struct CleanupActivationRequest {
    CleanupRegionId regionId;
    NodeId nodeId;
    QString message;
};

struct CleanupBlockedNode {
    NodeId nodeId;
    QString message;
};

class CleanupRuntimeCoordinator {
public:
    explicit CleanupRuntimeCoordinator(const ExecutionPlan& plan);

    bool requestSessionCleanup(const UutId& uutId,
                               const NodeId& nodeId,
                               const QString& reason);
    bool sessionCleanupRequested() const;
    QString sessionCleanupReason() const;

    QVector<CleanupActivationRequest> activationRequests(
        const UutExecution& uut) const;
    bool bestEffortApplies(const UutExecution& uut,
                           const NodeId& nodeId) const;
    bool bestEffortEdgeActive(const UutExecution& uut,
                              const NodeId& from,
                              const NodeId& to) const;
    QVector<CleanupBlockedNode> blockedNodes(
        const UutExecution& uut) const;

private:
    bool regionContainsNode(const CleanupRegion& region,
                            const NodeId& nodeId) const;
    bool regionIsActive(const CleanupRegion& region,
                        const UutExecution& uut) const;
    bool hasPath(const NodeId& from, const NodeId& to) const;

    const ExecutionPlan& m_plan;
    bool m_sessionCleanupRequested = false;
    QString m_sessionCleanupReason;
};

} // namespace PicoATE::Core
