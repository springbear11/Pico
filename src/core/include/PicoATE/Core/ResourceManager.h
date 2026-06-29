#pragma once

#include "PicoATE/Core/ExecutionPlan.h"

#include <QHash>
#include <QVector>

#include <optional>

namespace PicoATE::Core {

struct ResourceRequest {
    ResourceRequestId requestId;
    UutId uutId;
    FrameId frameId;
    NodeId nodeId;
    QVector<ResourceRequirement> requirements;
    int priority = 0;
    QDateTime enqueuedAt = QDateTime::currentDateTimeUtc();
};

struct ResourceLease {
    ResourceLeaseId leaseId;
    ResourceRequestId requestId;
    UutId uutId;
    FrameId frameId;
    NodeId nodeId;
    QVector<ResourceRequirement> requirements;
    QDateTime acquiredAt = QDateTime::currentDateTimeUtc();
};

struct ResourceStateSnapshot {
    ResourceId resourceId;
    QVector<ResourceLeaseId> activeLeases;
};

struct ResourceLeaseSnapshot {
    ResourceLeaseId leaseId;
    ResourceRequestId requestId;
    UutId uutId;
    FrameId frameId;
    NodeId nodeId;
    QVector<ResourceRequirement> requirements;
};

struct ResourceWaiterSnapshot {
    ResourceRequestId requestId;
    UutId uutId;
    FrameId frameId;
    NodeId nodeId;
    QVector<ResourceRequirement> requirements;
    QDateTime enqueuedAt;
    int priority = 0;
};

struct ResourceSnapshot {
    QVector<ResourceStateSnapshot> resources;
    QVector<ResourceLeaseSnapshot> activeLeases;
    QVector<ResourceWaiterSnapshot> waiters;
};

class ResourceManager {
public:
    std::optional<ResourceLease> tryAcquire(const ResourceRequest& request);
    void release(const ResourceLeaseId& leaseId);
    void releaseByNode(const UutId& uutId, const FrameId& frameId, const NodeId& nodeId);

    ResourceSnapshot snapshot() const;
    void restoreWaiters(const ResourceSnapshot& snapshot);

    int activeLeaseCount() const;
    int waiterCount() const;

private:
    bool canAcquire(const ResourceRequest& request) const;
    void enqueueWaiter(const ResourceRequest& request);
    bool conflicts(const ResourceRequirement& requested,
                   const ResourceRequirement& held) const;

    QHash<ResourceLeaseId, ResourceLease> m_activeLeases;
    QVector<ResourceRequest> m_waiters;
    int m_nextLease = 1;
};

} // namespace PicoATE::Core
