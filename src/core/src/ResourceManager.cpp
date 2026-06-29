#include "PicoATE/Core/ResourceManager.h"

namespace PicoATE::Core {

std::optional<ResourceLease> ResourceManager::tryAcquire(const ResourceRequest& request)
{
    if (!canAcquire(request)) {
        enqueueWaiter(request);
        return std::nullopt;
    }

    ResourceLease lease;
    lease.leaseId = QString("lease-%1").arg(m_nextLease++);
    lease.requestId = request.requestId;
    lease.uutId = request.uutId;
    lease.frameId = request.frameId;
    lease.nodeId = request.nodeId;
    lease.requirements = request.requirements;
    m_activeLeases.insert(lease.leaseId, lease);
    for (qsizetype i = m_waiters.size() - 1; i >= 0; --i) {
        if (m_waiters[i].requestId == request.requestId) {
            m_waiters.removeAt(i);
        }
    }
    return lease;
}

void ResourceManager::release(const ResourceLeaseId& leaseId)
{
    m_activeLeases.remove(leaseId);
}

void ResourceManager::releaseByNode(const UutId& uutId,
                                    const FrameId& frameId,
                                    const NodeId& nodeId)
{
    QVector<ResourceLeaseId> toRemove;
    for (auto it = m_activeLeases.constBegin(); it != m_activeLeases.constEnd(); ++it) {
        const auto& lease = it.value();
        if (lease.uutId == uutId && lease.frameId == frameId && lease.nodeId == nodeId) {
            toRemove.push_back(it.key());
        }
    }

    for (const auto& leaseId : toRemove) {
        m_activeLeases.remove(leaseId);
    }
}

ResourceSnapshot ResourceManager::snapshot() const
{
    ResourceSnapshot snapshot;
    QHash<ResourceId, ResourceStateSnapshot> states;

    for (auto it = m_activeLeases.constBegin(); it != m_activeLeases.constEnd(); ++it) {
        const auto& lease = it.value();
        ResourceLeaseSnapshot leaseSnapshot;
        leaseSnapshot.leaseId = lease.leaseId;
        leaseSnapshot.requestId = lease.requestId;
        leaseSnapshot.uutId = lease.uutId;
        leaseSnapshot.frameId = lease.frameId;
        leaseSnapshot.nodeId = lease.nodeId;
        leaseSnapshot.requirements = lease.requirements;
        snapshot.activeLeases.push_back(leaseSnapshot);

        for (const auto& requirement : lease.requirements) {
            auto& state = states[requirement.resourceId];
            state.resourceId = requirement.resourceId;
            state.activeLeases.push_back(lease.leaseId);
        }
    }

    snapshot.resources = states.values().toVector();

    for (const auto& waiter : m_waiters) {
        ResourceWaiterSnapshot waiterSnapshot;
        waiterSnapshot.requestId = waiter.requestId;
        waiterSnapshot.uutId = waiter.uutId;
        waiterSnapshot.frameId = waiter.frameId;
        waiterSnapshot.nodeId = waiter.nodeId;
        waiterSnapshot.requirements = waiter.requirements;
        waiterSnapshot.enqueuedAt = waiter.enqueuedAt;
        waiterSnapshot.priority = waiter.priority;
        snapshot.waiters.push_back(waiterSnapshot);
    }

    return snapshot;
}

void ResourceManager::restoreWaiters(const ResourceSnapshot& snapshot)
{
    m_waiters.clear();
    for (const auto& waiterSnapshot : snapshot.waiters) {
        ResourceRequest request;
        request.requestId = waiterSnapshot.requestId;
        request.uutId = waiterSnapshot.uutId;
        request.frameId = waiterSnapshot.frameId;
        request.nodeId = waiterSnapshot.nodeId;
        request.requirements = waiterSnapshot.requirements;
        request.enqueuedAt = waiterSnapshot.enqueuedAt;
        request.priority = waiterSnapshot.priority;
        m_waiters.push_back(request);
    }
}

int ResourceManager::activeLeaseCount() const
{
    return m_activeLeases.size();
}

int ResourceManager::waiterCount() const
{
    return m_waiters.size();
}

bool ResourceManager::canAcquire(const ResourceRequest& request) const
{
    for (const auto& requested : request.requirements) {
        for (const auto& lease : m_activeLeases) {
            for (const auto& held : lease.requirements) {
                if (conflicts(requested, held)) {
                    return false;
                }
            }
        }
    }
    return true;
}

void ResourceManager::enqueueWaiter(const ResourceRequest& request)
{
    for (const auto& waiter : m_waiters) {
        if (waiter.requestId == request.requestId) {
            return;
        }
    }
    m_waiters.push_back(request);
}

bool ResourceManager::conflicts(const ResourceRequirement& requested,
                                const ResourceRequirement& held) const
{
    if (requested.resourceId != held.resourceId) {
        return false;
    }

    if (requested.mode == ResourceMode::SharedRead && held.mode == ResourceMode::SharedRead) {
        return false;
    }

    return true;
}

} // namespace PicoATE::Core
