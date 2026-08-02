#include "PicoATE/Core/ExecutionPlan.h"

namespace PicoATE::Core {

UutVariableBindingResult bindSequenceVariablesForUut(
    const QVector<SequenceVariableDefinition>& definitions,
    int uutIndex,
    const UutId& uutId,
    const QVariantMap& overrides)
{
    UutVariableBindingResult result;
    if (uutIndex < 0) {
        result.errors.push_back(
            {QStringLiteral("uut.index"), QStringLiteral("UUT index must not be negative")});
        return result;
    }

    QSet<QString> boundNames;
    for (const auto& definition : definitions) {
        const auto name = definition.name.trimmed();
        if (name.isEmpty()) {
            result.errors.push_back(
                {{}, QStringLiteral("Sequence variable name must not be empty")});
            continue;
        }
        if (boundNames.contains(name)) {
            result.errors.push_back(
                {name, QStringLiteral("Sequence variable is defined more than once")});
            continue;
        }
        boundNames.insert(name);

        QVariant value;
        if (definition.scope == SequenceVariableScope::Shared) {
            value = definition.value;
        } else if (uutIndex < definition.values.size()) {
            value = definition.values[uutIndex];
        }
        if (!value.isValid() || value.isNull()) {
            result.errors.push_back(
                {name,
                 QStringLiteral("No value is configured for UUT%1").arg(uutIndex + 1)});
            continue;
        }
        result.variables.insert(name, value);
    }

    for (auto it = overrides.constBegin(); it != overrides.constEnd(); ++it) {
        result.variables.insert(it.key(), it.value());
    }

    if (!result.variables.contains(QStringLiteral("sn"))) {
        result.variables.insert(QStringLiteral("sn"), uutId);
    }
    if (!result.variables.contains(QStringLiteral("serialNumber"))) {
        result.variables.insert(QStringLiteral("serialNumber"), uutId);
    }
    auto uut = result.variables.value(QStringLiteral("uut")).toMap();
    uut.insert(QStringLiteral("id"), uutId);
    uut.insert(QStringLiteral("index"), uutIndex);
    uut.insert(QStringLiteral("number"), uutIndex + 1);
    uut.insert(QStringLiteral("slot"), uutIndex + 1);
    if (!uut.contains(QStringLiteral("sn"))) {
        uut.insert(QStringLiteral("sn"), result.variables.value(QStringLiteral("sn")));
    }
    if (!uut.contains(QStringLiteral("serialNumber"))) {
        uut.insert(QStringLiteral("serialNumber"),
                   result.variables.value(QStringLiteral("serialNumber")));
    }
    result.variables.insert(QStringLiteral("uut"), uut);
    return result;
}

QString sequenceVariableTypeName(SequenceVariableType type)
{
    switch (type) {
    case SequenceVariableType::String:
        return QStringLiteral("string");
    case SequenceVariableType::Integer:
        return QStringLiteral("integer");
    case SequenceVariableType::Hex:
        return QStringLiteral("hex");
    case SequenceVariableType::Double:
        return QStringLiteral("double");
    case SequenceVariableType::Boolean:
        return QStringLiteral("bool");
    }
    return QStringLiteral("string");
}

QString sequenceVariableScopeName(SequenceVariableScope scope)
{
    return scope == SequenceVariableScope::PerUut
        ? QStringLiteral("perUut")
        : QStringLiteral("shared");
}

ExecutionPhase executionPhaseOf(const ExecNode& node)
{
    return node.kind == ExecNodeKind::Cleanup
        ? ExecutionPhase::Cleanup
        : node.phase;
}

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

std::optional<ResourceRegion> ExecutionPlan::resourceRegionStartingAt(const NodeId& nodeId) const
{
    for (const auto& region : resourceRegions) {
        if (region.entryNodeId == nodeId) {
            return region;
        }
    }
    return std::nullopt;
}

std::optional<ResourceRegion> ExecutionPlan::resourceRegionEndingAt(const NodeId& nodeId) const
{
    for (const auto& region : resourceRegions) {
        if (region.exitNodeId == nodeId) {
            return region;
        }
    }
    return std::nullopt;
}

std::optional<NodeId> ExecutionPlan::structuralParentOf(const NodeId& nodeId) const
{
    for (const auto& region : testItemRegions) {
        if (region.childNodeIds.contains(nodeId)) {
            return region.controllerNodeId;
        }
    }
    for (const auto& region : loopRegions) {
        if (region.childNodeIds.contains(nodeId)) {
            return region.controllerNodeId;
        }
    }
    return std::nullopt;
}

bool ExecutionPlan::isInsideTestItem(const NodeId& nodeId) const
{
    auto current = structuralParentOf(nodeId);
    QSet<NodeId> visited;
    while (current && !visited.contains(*current)) {
        visited.insert(*current);
        if (testItemRegionForController(*current)) {
            return true;
        }
        current = structuralParentOf(*current);
    }
    return false;
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

QString executionPhaseName(ExecutionPhase phase)
{
    switch (phase) {
    case ExecutionPhase::Setup:
        return "Setup";
    case ExecutionPhase::Main:
        return "Main";
    case ExecutionPhase::Cleanup:
        return "Cleanup";
    }
    return "Main";
}

} // namespace PicoATE::Core
