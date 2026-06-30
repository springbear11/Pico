#include "PicoATE/Core/ExecutionPlan.h"

namespace PicoATE::Core {

bool ExecutionPlan::addNode(const ExecNode& execNode)
{
    if (execNode.id.isEmpty() || nodes.contains(execNode.id)) {
        return false;
    }
    nodes.insert(execNode.id, execNode);
    return true;
}

void ExecutionPlan::addEdge(const ExecEdge& edge)
{
    edges.push_back(edge);
}

const ExecNode* ExecutionPlan::node(const NodeId& nodeId) const
{
    auto it = nodes.constFind(nodeId);
    if (it == nodes.constEnd()) {
        return nullptr;
    }
    return &it.value();
}

QVector<ExecEdge> ExecutionPlan::incomingEdges(const NodeId& nodeId) const
{
    QVector<ExecEdge> result;
    for (const auto& edge : edges) {
        if (edge.to == nodeId) {
            result.push_back(edge);
        }
    }
    return result;
}

QVector<ExecEdge> ExecutionPlan::outgoingEdges(const NodeId& nodeId) const
{
    QVector<ExecEdge> result;
    for (const auto& edge : edges) {
        if (edge.from == nodeId) {
            result.push_back(edge);
        }
    }
    return result;
}

std::optional<CleanupRegion> ExecutionPlan::cleanupRegion(const CleanupRegionId& id) const
{
    for (const auto& region : cleanupRegions) {
        if (region.id == id) {
            return region;
        }
    }
    return std::nullopt;
}

std::optional<LoopRegion> ExecutionPlan::loopRegionForController(const NodeId& nodeId) const
{
    for (const auto& region : loopRegions) {
        if (region.controllerNodeId == nodeId) {
            return region;
        }
    }
    return std::nullopt;
}

std::optional<LoopRegion> ExecutionPlan::loopRegionForBodyNode(const NodeId& nodeId) const
{
    for (const auto& region : loopRegions) {
        if (region.bodyNodes.contains(nodeId)) {
            return region;
        }
    }
    return std::nullopt;
}

std::optional<TestItemRegion> ExecutionPlan::testItemRegionForController(const NodeId& nodeId) const
{
    for (const auto& region : testItemRegions) {
        if (region.controllerNodeId == nodeId) {
            return region;
        }
    }
    return std::nullopt;
}

std::optional<TestItemRegion> ExecutionPlan::testItemRegionForChild(const NodeId& nodeId) const
{
    for (const auto& region : testItemRegions) {
        if (region.childNodeIds.contains(nodeId)) {
            return region;
        }
    }
    return std::nullopt;
}

bool isTerminalOutcome(NodeOutcome outcome)
{
    return outcome != NodeOutcome::Unknown;
}

bool triggerMatchesOutcome(EdgeTrigger trigger, NodeOutcome outcome)
{
    switch (trigger) {
    case EdgeTrigger::OnSuccess:
        return outcome == NodeOutcome::Passed;
    case EdgeTrigger::OnFail:
        return outcome == NodeOutcome::Failed;
    case EdgeTrigger::OnError:
        return outcome == NodeOutcome::Error;
    case EdgeTrigger::OnTimeout:
        return outcome == NodeOutcome::Timeout;
    case EdgeTrigger::OnCancelled:
        return outcome == NodeOutcome::Cancelled;
    case EdgeTrigger::OnSkipped:
        return outcome == NodeOutcome::Skipped;
    case EdgeTrigger::Always:
    case EdgeTrigger::Finally:
        return isTerminalOutcome(outcome);
    case EdgeTrigger::OnStopRequested:
    case EdgeTrigger::OnAbortRequested:
        return false;
    }
    return false;
}

QString nodeOutcomeName(NodeOutcome outcome)
{
    switch (outcome) {
    case NodeOutcome::Unknown:
        return "Unknown";
    case NodeOutcome::Passed:
        return "Passed";
    case NodeOutcome::Failed:
        return "Failed";
    case NodeOutcome::Error:
        return "Error";
    case NodeOutcome::Timeout:
        return "Timeout";
    case NodeOutcome::Cancelled:
        return "Cancelled";
    case NodeOutcome::Skipped:
        return "Skipped";
    }
    return "Unknown";
}

} // namespace PicoATE::Core
