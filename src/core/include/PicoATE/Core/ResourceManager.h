#pragma once

#include "PicoATE/Core/ExecutionPlan.h"

#include <QHash>
#include <QVector>

#include <functional>
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

enum class ResourceTransitionKind {
    Waiting,
    Acquired,
    Released,
    Cancelled
};

struct ResourceTransition {
    ResourceTransitionKind kind = ResourceTransitionKind::Waiting;
    ResourceRequestId requestId;
    ResourceLeaseId leaseId;
    UutId uutId;
    FrameId frameId;
    NodeId nodeId;
    QVector<ResourceRequirement> requirements;
    QVector<UutId> blockingUutIds;
    QDateTime waitingSinceUtc;
    QDateTime occurredAtUtc = QDateTime::currentDateTimeUtc();
};

using ResourceTransitionHandler =
    std::function<void(const ResourceTransition& transition)>;

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
    void setTransitionHandler(ResourceTransitionHandler handler);
    std::optional<ResourceLease> tryAcquire(const ResourceRequest& request);
    void release(const ResourceLeaseId& leaseId);
    void cancelRequest(const ResourceRequestId& requestId);
    void releaseByNode(const UutId& uutId, const FrameId& frameId, const NodeId& nodeId);

    ResourceSnapshot snapshot() const;
    void restoreWaiters(const ResourceSnapshot& snapshot);

    int activeLeaseCount() const;
    int waiterCount() const;

private:
    bool canAcquire(const ResourceRequest& request) const;
    QVector<UutId> blockingUutIds(const ResourceRequest& request) const;
    bool enqueueWaiter(const ResourceRequest& request,
                       const QVector<UutId>& blockers);
    void publishTransition(const ResourceTransition& transition) const;
    bool conflicts(const ResourceRequirement& requested,
                   const ResourceRequirement& held) const;

    QHash<ResourceLeaseId, ResourceLease> m_activeLeases;
    QVector<ResourceRequest> m_waiters;
    QHash<ResourceRequestId, QVector<UutId>> m_waiterBlockers;
    ResourceTransitionHandler m_transitionHandler;
    int m_nextLease = 1;
};

} // namespace PicoATE::Core
