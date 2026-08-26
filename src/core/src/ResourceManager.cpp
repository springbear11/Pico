#include "PicoATE/Core/ResourceManager.h"

#include <algorithm>
#include <utility>

namespace PicoATE::Core {

namespace {

bool resourceIdsOverlap(const ResourceId& left, const ResourceId& right)
{
    return left == right ||
           left.startsWith(right + '.') ||
           right.startsWith(left + '.');
}

} // namespace

void ResourceManager::setTransitionHandler(ResourceTransitionHandler handler)
{
    m_transitionHandler = std::move(handler);
}

std::optional<ResourceLease> ResourceManager::tryAcquire(const ResourceRequest& request)
{
    if (!canAcquire(request)) {
        const auto blockers = blockingUutIds(request);
        if (enqueueWaiter(request, blockers)) {
            auto waitingSince = request.enqueuedAt;
            const auto existing = std::find_if(
                m_waiters.cbegin(), m_waiters.cend(),
                [&request](const ResourceRequest& waiter) {
                    return waiter.requestId == request.requestId;
                });
            if (existing != m_waiters.cend()) {
                waitingSince = existing->enqueuedAt;
            }
            ResourceTransition transition;
            transition.kind = ResourceTransitionKind::Waiting;
            transition.requestId = request.requestId;
            transition.uutId = request.uutId;
            transition.frameId = request.frameId;
            transition.nodeId = request.nodeId;
            transition.requirements = request.requirements;
            transition.blockingUutIds = blockers;
            transition.waitingSinceUtc = waitingSince;
            publishTransition(transition);
        }
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
    auto waitingSince = request.enqueuedAt;
    for (qsizetype i = m_waiters.size() - 1; i >= 0; --i) {
        if (m_waiters[i].requestId == request.requestId) {
            waitingSince = m_waiters[i].enqueuedAt;
            m_waiters.removeAt(i);
        }
    }
    m_waiterBlockers.remove(request.requestId);

    ResourceTransition transition;
    transition.kind = ResourceTransitionKind::Acquired;
    transition.requestId = request.requestId;
    transition.leaseId = lease.leaseId;
    transition.uutId = request.uutId;
    transition.frameId = request.frameId;
    transition.nodeId = request.nodeId;
    transition.requirements = request.requirements;
    transition.waitingSinceUtc = waitingSince;
    publishTransition(transition);
    return lease;
}

void ResourceManager::release(const ResourceLeaseId& leaseId)
{
    const auto leaseIt = m_activeLeases.find(leaseId);
    if (leaseIt == m_activeLeases.end()) {
        return;
    }
    const auto lease = leaseIt.value();
    m_activeLeases.erase(leaseIt);

    ResourceTransition transition;
    transition.kind = ResourceTransitionKind::Released;
    transition.requestId = lease.requestId;
    transition.leaseId = lease.leaseId;
    transition.uutId = lease.uutId;
    transition.frameId = lease.frameId;
    transition.nodeId = lease.nodeId;
    transition.requirements = lease.requirements;
    publishTransition(transition);
}

void ResourceManager::cancelRequest(const ResourceRequestId& requestId)
{
    std::optional<ResourceRequest> cancelled;
    for (qsizetype index = m_waiters.size() - 1; index >= 0; --index) {
        if (m_waiters[index].requestId == requestId) {
            cancelled = m_waiters[index];
            m_waiters.removeAt(index);
        }
    }
    const auto blockers = m_waiterBlockers.take(requestId);
    if (!cancelled) {
        return;
    }

    ResourceTransition transition;
    transition.kind = ResourceTransitionKind::Cancelled;
    transition.requestId = cancelled->requestId;
    transition.uutId = cancelled->uutId;
    transition.frameId = cancelled->frameId;
    transition.nodeId = cancelled->nodeId;
    transition.requirements = cancelled->requirements;
    transition.blockingUutIds = blockers;
    transition.waitingSinceUtc = cancelled->enqueuedAt;
    publishTransition(transition);
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
        release(leaseId);
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
    m_waiterBlockers.clear();
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
        const auto blockers = blockingUutIds(request);
        m_waiterBlockers.insert(request.requestId, blockers);

        ResourceTransition transition;
        transition.kind = ResourceTransitionKind::Waiting;
        transition.requestId = request.requestId;
        transition.uutId = request.uutId;
        transition.frameId = request.frameId;
        transition.nodeId = request.nodeId;
        transition.requirements = request.requirements;
        transition.blockingUutIds = blockers;
        transition.waitingSinceUtc = request.enqueuedAt;
        publishTransition(transition);
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

QVector<UutId> ResourceManager::blockingUutIds(
    const ResourceRequest& request) const
{
    QVector<UutId> blockers;
    for (const auto& requested : request.requirements) {
        for (const auto& lease : m_activeLeases) {
            const bool blocked = std::any_of(
                lease.requirements.cbegin(), lease.requirements.cend(),
                [this, &requested](const ResourceRequirement& held) {
                    return conflicts(requested, held);
                });
            if (blocked && !lease.uutId.isEmpty() &&
                !blockers.contains(lease.uutId)) {
                blockers.push_back(lease.uutId);
            }
        }
    }
    std::sort(blockers.begin(), blockers.end());
    return blockers;
}

bool ResourceManager::enqueueWaiter(const ResourceRequest& request,
                                    const QVector<UutId>& blockers)
{
    for (const auto& waiter : m_waiters) {
        if (waiter.requestId == request.requestId) {
            if (m_waiterBlockers.value(request.requestId) == blockers) {
                return false;
            }
            m_waiterBlockers.insert(request.requestId, blockers);
            return true;
        }
    }
    m_waiters.push_back(request);
    m_waiterBlockers.insert(request.requestId, blockers);
    return true;
}

void ResourceManager::publishTransition(
    const ResourceTransition& transition) const
{
    if (m_transitionHandler) {
        m_transitionHandler(transition);
    }
}

bool ResourceManager::conflicts(const ResourceRequirement& requested,
                                const ResourceRequirement& held) const
{
    if (!resourceIdsOverlap(requested.resourceId, held.resourceId)) {
        return false;
    }

    if (requested.mode == ResourceMode::SharedRead && held.mode == ResourceMode::SharedRead) {
        return false;
    }

    return true;
}

} // namespace PicoATE::Core
