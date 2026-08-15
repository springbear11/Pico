#include "PicoATE/Core/CleanupRuntimeCoordinator.h"

#include <QSet>

#include <algorithm>

namespace PicoATE::Core {

namespace {

const QString& blockedCleanupMessage()
{
    static const QString message = QStringLiteral(
        "cleanup could not continue after a prior cleanup error");
    return message;
}

} // namespace

CleanupRuntimeCoordinator::CleanupRuntimeCoordinator(
    const ExecutionPlan& plan)
    : m_plan(plan)
{
}

bool CleanupRuntimeCoordinator::requestSessionCleanup(
    const UutId& uutId,
    const NodeId& nodeId,
    const QString& reason)
{
    if (m_sessionCleanupRequested) {
        return false;
    }
    m_sessionCleanupRequested = true;
    m_sessionCleanupReason = QStringLiteral("%1 requested cleanup after %2: %3")
                                 .arg(uutId, nodeId, reason);
    return true;
}

bool CleanupRuntimeCoordinator::sessionCleanupRequested() const
{
    return m_sessionCleanupRequested;
}

QString CleanupRuntimeCoordinator::sessionCleanupReason() const
{
    return m_sessionCleanupReason;
}

QVector<CleanupActivationRequest>
CleanupRuntimeCoordinator::activationRequests(const UutExecution& uut) const
{
    QVector<CleanupActivationRequest> requests;
    for (const auto& region : m_plan.cleanupRegions) {
        for (const auto& entryNodeId : region.entryNodes) {
            if (isTerminalActivation(uut.stateOf(entryNodeId))) {
                continue;
            }
            requests.push_back({
                region.id,
                entryNodeId,
                QStringLiteral("cleanup region activated: %1").arg(region.id)});
        }
    }
    return requests;
}

bool CleanupRuntimeCoordinator::bestEffortApplies(
    const UutExecution& uut,
    const NodeId& nodeId) const
{
    return std::any_of(
        m_plan.cleanupRegions.cbegin(),
        m_plan.cleanupRegions.cend(),
        [this, &uut, &nodeId](const CleanupRegion& region) {
            return region.bestEffort && regionIsActive(region, uut) &&
                   regionContainsNode(region, nodeId);
        });
}

bool CleanupRuntimeCoordinator::bestEffortEdgeActive(
    const UutExecution& uut,
    const NodeId& from,
    const NodeId& to) const
{
    return std::any_of(
        m_plan.cleanupRegions.cbegin(),
        m_plan.cleanupRegions.cend(),
        [this, &uut, &from, &to](const CleanupRegion& region) {
            return region.bestEffort && regionIsActive(region, uut) &&
                   regionContainsNode(region, from) &&
                   regionContainsNode(region, to);
        });
}

QVector<CleanupBlockedNode> CleanupRuntimeCoordinator::blockedNodes(
    const UutExecution& uut) const
{
    QVector<CleanupBlockedNode> blocked;
    QSet<NodeId> seen;
    for (const auto& region : m_plan.cleanupRegions) {
        if (!regionIsActive(region, uut)) {
            continue;
        }
        for (auto it = m_plan.nodes.constBegin();
             it != m_plan.nodes.constEnd(); ++it) {
            const auto& node = it.value();
            if (seen.contains(node.id) || !regionContainsNode(region, node.id)) {
                continue;
            }
            const auto state = uut.stateOf(node.id);
            if (isTerminalActivation(state) ||
                state == ActivationState::Running ||
                state == ActivationState::WaitingForResource ||
                state == ActivationState::WaitingForTimer ||
                state == ActivationState::WaitingAtBarrier) {
                continue;
            }
            seen.insert(node.id);
            blocked.push_back({node.id, blockedCleanupMessage()});
        }
    }
    return blocked;
}

bool CleanupRuntimeCoordinator::regionContainsNode(
    const CleanupRegion& region,
    const NodeId& nodeId) const
{
    const auto* node = m_plan.node(nodeId);
    if (!node || executionPhaseOf(*node) != ExecutionPhase::Cleanup) {
        return false;
    }

    NodeId controlNodeId = nodeId;
    while (const auto parent = m_plan.structuralParentOf(controlNodeId)) {
        controlNodeId = *parent;
    }

    const bool reachableFromEntry = std::any_of(
        region.entryNodes.cbegin(),
        region.entryNodes.cend(),
        [this, &controlNodeId](const NodeId& entry) {
            return hasPath(entry, controlNodeId);
        });
    if (!reachableFromEntry) {
        return false;
    }

    return region.exitNodes.isEmpty() || std::any_of(
        region.exitNodes.cbegin(),
        region.exitNodes.cend(),
        [this, &controlNodeId](const NodeId& exit) {
            return hasPath(controlNodeId, exit);
        });
}

bool CleanupRuntimeCoordinator::regionIsActive(
    const CleanupRegion& region,
    const UutExecution& uut) const
{
    return std::any_of(
        region.entryNodes.cbegin(),
        region.entryNodes.cend(),
        [&uut](const NodeId& entry) {
            return uut.activations.contains(entry);
        });
}

bool CleanupRuntimeCoordinator::hasPath(const NodeId& from,
                                        const NodeId& to) const
{
    QSet<NodeId> visited;
    QVector<NodeId> stack{from};
    while (!stack.isEmpty()) {
        const auto current = stack.takeLast();
        if (current == to) {
            return true;
        }
        if (visited.contains(current)) {
            continue;
        }
        visited.insert(current);
        for (const auto& edge : m_plan.outgoingEdges(current)) {
            stack.push_back(edge.to);
        }
    }
    return false;
}

} // namespace PicoATE::Core
