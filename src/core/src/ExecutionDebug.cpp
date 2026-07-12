#include "PicoATE/Core/ExecutionDebug.h"

#include <algorithm>
#include <utility>

namespace PicoATE::Core {

namespace {

QString normalizeLocalPath(QString value)
{
    value = value.trimmed();
    value.replace('\\', '/');
    value.replace('.', '/');
    while (value.contains("//")) {
        value.replace("//", "/");
    }
    if (value.startsWith('/')) {
        value.remove(0, 1);
    }
    if (value.endsWith('/')) {
        value.chop(1);
    }
    return value;
}

QString nodeSegment(const ExecutionPlan& plan, const NodeId& nodeId)
{
    const auto* node = plan.node(nodeId);
    if (!node) {
        return nodeId;
    }
    if (!node->localId.isEmpty()) {
        return node->localId;
    }
    return node->id;
}

QVector<NodeId> sortedNodeIds(const ExecutionPlan& plan)
{
    QVector<NodeId> ids;
    ids.reserve(plan.nodes.size());
    for (auto it = plan.nodes.constBegin(); it != plan.nodes.constEnd(); ++it) {
        ids.push_back(it.key());
    }
    std::sort(ids.begin(), ids.end());
    return ids;
}

NodeOutcome activationOutcome(const NodeActivation& activation)
{
    if (activation.attempts.isEmpty()) {
        return NodeOutcome::Unknown;
    }
    return activation.attempts.last().result.outcome;
}

DebugAttemptSnapshot makeAttemptSnapshot(const NodeAttempt& attempt)
{
    DebugAttemptSnapshot snapshot;
    snapshot.attemptId = attempt.id;
    snapshot.attemptIndex = attempt.attemptIndex + 1;
    snapshot.state = attempt.state;
    snapshot.loopIteration = attempt.loopIteration;
    snapshot.outcome = attempt.result.outcome;
    snapshot.outputs = attempt.result.outputs;
    snapshot.measurements = attempt.result.measurements;
    snapshot.errorCode = attempt.result.errorCode;
    snapshot.errorMessage = attempt.result.errorMessage;
    return snapshot;
}

DebugNodeSnapshot makeNodeSnapshot(const ExecutionPlan& plan,
                                   const NodeId& nodeId,
                                   const UutExecution& uut)
{
    DebugNodeSnapshot snapshot;
    snapshot.nodeId = nodeId;
    snapshot.localPath = debugLocalPathForNode(plan, nodeId);

    const auto* node = plan.node(nodeId);
    if (node) {
        snapshot.localId = node->localId;
        snapshot.displayName = node->displayName;
        snapshot.kind = node->kind;
    }

    const auto activationIt = uut.activations.constFind(nodeId);
    if (activationIt == uut.activations.constEnd()) {
        return snapshot;
    }

    const auto& activation = activationIt.value();
    snapshot.frameId = activation.frameId;
    snapshot.state = activation.state;
    snapshot.outcome = activationOutcome(activation);
    snapshot.attempts.reserve(activation.attempts.size());
    for (const auto& attempt : activation.attempts) {
        snapshot.attempts.push_back(makeAttemptSnapshot(attempt));
    }
    return snapshot;
}

} // namespace

BreakpointAddress BreakpointAddress::nodePath(QString nodePath, UutId uutId)
{
    BreakpointAddress address;
    address.kind = BreakpointAddressKind::NodePath;
    address.value = std::move(nodePath);
    address.uutId = std::move(uutId);
    return address;
}

BreakpointAddress BreakpointAddress::localPath(QString localPath, UutId uutId)
{
    BreakpointAddress address;
    address.kind = BreakpointAddressKind::LocalPath;
    address.value = std::move(localPath);
    address.uutId = std::move(uutId);
    return address;
}

bool BreakpointAddress::hasUutFilter() const
{
    return !uutId.trimmed().isEmpty();
}

QString debugLocalPathForNode(const ExecutionPlan& plan, const NodeId& nodeId)
{
    QVector<QString> segments;
    auto current = nodeId;
    while (!current.isEmpty()) {
        segments.prepend(nodeSegment(plan, current));
        const auto parent = plan.structuralParentOf(current);
        if (!parent) {
            break;
        }
        current = *parent;
    }
    return segments.join('/');
}

std::optional<NodeId> resolveBreakpointAddress(const ExecutionPlan& plan,
                                               const BreakpointAddress& address)
{
    const auto value = address.value.trimmed();
    if (value.isEmpty()) {
        return std::nullopt;
    }
    if (address.kind == BreakpointAddressKind::NodePath) {
        return plan.node(value) ? std::optional<NodeId>(value) : std::nullopt;
    }

    const auto wanted = normalizeLocalPath(value);
    std::optional<NodeId> matched;
    for (const auto& nodeId : sortedNodeIds(plan)) {
        if (normalizeLocalPath(debugLocalPathForNode(plan, nodeId)) != wanted) {
            continue;
        }
        if (matched) {
            return std::nullopt;
        }
        matched = nodeId;
    }
    return matched;
}

bool breakpointAddressMatches(const ExecutionPlan& plan,
                              const BreakpointAddress& address,
                              const UutId& uutId,
                              const ExecNode& node)
{
    if (address.hasUutFilter() && address.uutId != uutId) {
        return false;
    }
    if (address.kind == BreakpointAddressKind::NodePath) {
        return address.value.trimmed() == node.id;
    }
    return normalizeLocalPath(debugLocalPathForNode(plan, node.id)) ==
           normalizeLocalPath(address.value);
}

ExecutionDebugSnapshot makeExecutionDebugSnapshot(
    const ExecutionPlan& plan,
    ExecutionState state,
    const QVector<UutExecution>& uuts,
    const ResourceSnapshot& resources,
    const BarrierSnapshot& barriers,
    DebugPauseReason pauseReason,
    std::optional<BreakpointHit> breakpoint,
    UutId currentUutId,
    NodeId currentNodeId)
{
    ExecutionDebugSnapshot snapshot;
    snapshot.planId = plan.id;
    snapshot.sequenceId = plan.sequenceId;
    snapshot.sequenceVersion = plan.sequenceVersion;
    snapshot.state = state;
    snapshot.pauseReason = pauseReason;
    snapshot.breakpoint = std::move(breakpoint);
    snapshot.resources = resources;
    snapshot.barriers = barriers;
    snapshot.capturedAt = QDateTime::currentDateTimeUtc();
    if (snapshot.breakpoint) {
        snapshot.currentUutId = snapshot.breakpoint->uutId;
        snapshot.currentNodeId = snapshot.breakpoint->nodeId;
        snapshot.currentLocalPath = snapshot.breakpoint->localPath;
    } else if (!currentNodeId.isEmpty()) {
        snapshot.currentUutId = std::move(currentUutId);
        snapshot.currentNodeId = std::move(currentNodeId);
        snapshot.currentLocalPath = debugLocalPathForNode(plan, snapshot.currentNodeId);
    }

    const auto nodeIds = sortedNodeIds(plan);
    snapshot.uuts.reserve(uuts.size());
    for (const auto& uut : uuts) {
        DebugUutSnapshot uutSnapshot;
        uutSnapshot.uutId = uut.uutId;
        uutSnapshot.variables = uut.variables;
        uutSnapshot.nodes.reserve(nodeIds.size());
        for (const auto& nodeId : nodeIds) {
            uutSnapshot.nodes.push_back(makeNodeSnapshot(plan, nodeId, uut));
        }
        snapshot.uuts.push_back(uutSnapshot);
    }
    return snapshot;
}

} // namespace PicoATE::Core
