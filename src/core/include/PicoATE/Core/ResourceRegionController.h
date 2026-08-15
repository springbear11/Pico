#pragma once

#include "PicoATE/Core/ResourceManager.h"
#include "PicoATE/Core/RuntimeTypes.h"

#include <QHash>
#include <QSet>

namespace PicoATE::Core {

enum class ResourceRegionAcquireStatus {
    NotApplicable,
    Acquired,
    AlreadyHeld,
    Waiting
};

struct ResourceRegionAcquireDecision {
    ResourceRegionAcquireStatus status = ResourceRegionAcquireStatus::NotApplicable;
    ResourceRegionId regionId;

    bool canExecute() const
    {
        return status != ResourceRegionAcquireStatus::Waiting;
    }
};

class ResourceRegionController {
public:
    ResourceRegionController(const ExecutionPlan& plan, ResourceManager& resources);

    ResourceRegionAcquireDecision acquireForNode(const UutId& uutId,
                                                  const FrameId& frameId,
                                                  const NodeId& nodeId);
    void releaseCompleted(const UutExecution& uut, const FrameId& frameId);
    void releaseAll(const UutId& uutId, const FrameId& frameId);
    QSet<ResourceId> activeResourceIds(const UutId& uutId,
                                       const FrameId& frameId) const;

private:
    struct ActiveResourceRegion {
        ResourceRegionId regionId;
        UutId uutId;
        FrameId frameId;
        ResourceLease lease;
    };

    QString leaseKey(const UutId& uutId,
                     const FrameId& frameId,
                     const ResourceRegionId& regionId) const;
    ResourceRequestId requestId(const UutId& uutId,
                                const FrameId& frameId,
                                const ResourceRegionId& regionId) const;

    const ExecutionPlan& m_plan;
    ResourceManager& m_resources;
    QHash<QString, ActiveResourceRegion> m_activeRegions;
};

} // namespace PicoATE::Core
