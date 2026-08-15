#include "PicoATE/Core/ResourceRegionController.h"

#include <algorithm>

namespace PicoATE::Core {

ResourceRegionController::ResourceRegionController(const ExecutionPlan& plan,
                                                    ResourceManager& resources)
    : m_plan(plan)
    , m_resources(resources)
{
}

ResourceRegionAcquireDecision ResourceRegionController::acquireForNode(
    const UutId& uutId,
    const FrameId& frameId,
    const NodeId& nodeId)
{
    ResourceRegionAcquireDecision decision;
    const auto region = m_plan.resourceRegionStartingAt(nodeId);
    if (!region) {
        return decision;
    }

    decision.regionId = region->id;
    const auto key = leaseKey(uutId, frameId, region->id);
    if (m_activeRegions.contains(key)) {
        decision.status = ResourceRegionAcquireStatus::AlreadyHeld;
        return decision;
    }

    ResourceRequest request;
    request.requestId = requestId(uutId, frameId, region->id);
    request.uutId = uutId;
    request.frameId = frameId;
    request.nodeId = QStringLiteral("resource-region:%1").arg(region->id);
    request.requirements = region->requirements;
    const auto lease = m_resources.tryAcquire(request);
    if (!lease) {
        decision.status = ResourceRegionAcquireStatus::Waiting;
        return decision;
    }

    m_activeRegions.insert(
        key, ActiveResourceRegion{region->id, uutId, frameId, *lease});
    decision.status = ResourceRegionAcquireStatus::Acquired;
    return decision;
}

void ResourceRegionController::releaseCompleted(const UutExecution& uut,
                                                const FrameId& frameId)
{
    QVector<QString> completedKeys;
    for (auto it = m_activeRegions.constBegin();
         it != m_activeRegions.constEnd(); ++it) {
        const auto& active = it.value();
        if (active.uutId != uut.uutId || active.frameId != frameId) {
            continue;
        }
        const auto region = std::find_if(
            m_plan.resourceRegions.cbegin(),
            m_plan.resourceRegions.cend(),
            [&active](const ResourceRegion& candidate) {
                return candidate.id == active.regionId;
            });
        if (region != m_plan.resourceRegions.cend() &&
            isTerminalActivation(uut.stateOf(region->exitNodeId))) {
            completedKeys.push_back(it.key());
        }
    }

    for (const auto& key : completedKeys) {
        const auto active = m_activeRegions.take(key);
        m_resources.release(active.lease.leaseId);
        m_resources.cancelRequest(active.lease.requestId);
    }
}

void ResourceRegionController::releaseAll(const UutId& uutId,
                                          const FrameId& frameId)
{
    QVector<QString> keys;
    for (auto it = m_activeRegions.constBegin();
         it != m_activeRegions.constEnd(); ++it) {
        if (it->uutId == uutId && it->frameId == frameId) {
            keys.push_back(it.key());
        }
    }

    for (const auto& key : keys) {
        const auto active = m_activeRegions.take(key);
        m_resources.release(active.lease.leaseId);
        m_resources.cancelRequest(active.lease.requestId);
    }
    for (const auto& region : m_plan.resourceRegions) {
        m_resources.cancelRequest(requestId(uutId, frameId, region.id));
    }
}

QSet<ResourceId> ResourceRegionController::activeResourceIds(
    const UutId& uutId,
    const FrameId& frameId) const
{
    QSet<ResourceId> resourceIds;
    for (const auto& active : m_activeRegions) {
        if (active.uutId != uutId || active.frameId != frameId) {
            continue;
        }
        for (const auto& requirement : active.lease.requirements) {
            resourceIds.insert(requirement.resourceId);
        }
    }
    return resourceIds;
}

QString ResourceRegionController::leaseKey(const UutId& uutId,
                                           const FrameId& frameId,
                                           const ResourceRegionId& regionId) const
{
    return QStringLiteral("%1|%2|%3").arg(uutId, frameId, regionId);
}

ResourceRequestId ResourceRegionController::requestId(
    const UutId& uutId,
    const FrameId& frameId,
    const ResourceRegionId& regionId) const
{
    return QStringLiteral("resource-region:%1:%2:%3")
        .arg(uutId, frameId, regionId);
}

} // namespace PicoATE::Core
