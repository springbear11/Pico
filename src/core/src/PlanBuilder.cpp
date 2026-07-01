#include "PicoATE/Core/PlanBuilder.h"
#include "PicoATE/Core/ExecutionResultStore.h"

#include <QMetaType>
#include <QRegularExpression>

namespace PicoATE::Core {

namespace {

void collectStepExpressions(const QVariant& value, QVector<QString>& expressions)
{
    if (value.metaType().id() == QMetaType::QString) {
        static const QRegularExpression pattern(R"(\$\{(step:[^}]+)\})");
        auto matches = pattern.globalMatch(value.toString());
        while (matches.hasNext()) {
            expressions.push_back(matches.next().captured(1).trimmed());
        }
        return;
    }
    if (value.metaType().id() == QMetaType::QVariantMap) {
        const auto map = value.toMap();
        for (auto it = map.constBegin(); it != map.constEnd(); ++it) {
            collectStepExpressions(it.value(), expressions);
        }
        return;
    }
    if (value.metaType().id() == QMetaType::QVariantList) {
        for (const auto& item : value.toList()) {
            collectStepExpressions(item, expressions);
        }
    }
}

bool hasPlanPath(const ExecutionPlan& plan, const NodeId& from, const NodeId& to)
{
    QSet<NodeId> visited;
    QVector<NodeId> pending{from};
    while (!pending.isEmpty()) {
        const auto current = pending.takeLast();
        if (current == to) {
            return true;
        }
        if (visited.contains(current)) {
            continue;
        }
        visited.insert(current);
        for (const auto& edge : plan.outgoingEdges(current)) {
            pending.push_back(edge.to);
        }
    }
    return false;
}

NodeId structuralRoot(const ExecutionPlan& plan, NodeId nodeId)
{
    auto parent = plan.structuralParentOf(nodeId);
    while (parent) {
        nodeId = *parent;
        parent = plan.structuralParentOf(nodeId);
    }
    return nodeId;
}

bool sourceRunsBeforeConsumer(const ExecutionPlan& plan,
                              const NodeId& source,
                              const NodeId& consumer)
{
    if (source == consumer) {
        return false;
    }
    if (hasPlanPath(plan, source, consumer)) {
        return true;
    }
    const auto sourceRoot = structuralRoot(plan, source);
    const auto consumerRoot = structuralRoot(plan, consumer);
    return sourceRoot != consumerRoot && hasPlanPath(plan, sourceRoot, consumerRoot);
}

} // namespace

PlanBuildResult PlanBuilder::build(const SequenceDef& sequence) const
{
    PlanBuildResult result;
    if (!validateSequence(sequence, result)) {
        return result;
    }

    result.plan.id = QString("plan:%1:%2").arg(sequence.id, sequence.version);
    result.plan.sequenceId = sequence.id;
    result.plan.sequenceVersion = sequence.version;

    const CleanupRegionId cleanupRegionId = "main-cleanup";
    QVector<GroupBuildInfo> setupGroups;
    QVector<GroupBuildInfo> bodyGroups;
    QVector<GroupBuildInfo> cleanupGroups;

    for (const auto& group : sequence.groups) {
        auto built = buildGroup(group, cleanupRegionId, result.plan);
        switch (group.kind) {
        case StepGroupKind::Setup:
            setupGroups.push_back(built);
            break;
        case StepGroupKind::Main:
            bodyGroups.push_back(built);
            break;
        case StepGroupKind::Cleanup:
            cleanupGroups.push_back(built);
            break;
        case StepGroupKind::Custom:
            bodyGroups.push_back(built);
            break;
        }
    }

    auto bridgeGroups = [&](const QVector<GroupBuildInfo>& groups, const QString& prefix) {
        for (int i = 0; i + 1 < groups.size(); ++i) {
            addGroupBridge(groups[i], groups[i + 1], result.plan, QString("%1-%2").arg(prefix).arg(i));
        }
    };

    auto nonEmptyGroups = [](const QVector<GroupBuildInfo>& groups) {
        QVector<GroupBuildInfo> result;
        for (const auto& group : groups) {
            if (!group.nodeIds.isEmpty()) {
                result.push_back(group);
            }
        }
        return result;
    };

    QVector<GroupBuildInfo> normalGroups;
    normalGroups.reserve(setupGroups.size() + bodyGroups.size());
    normalGroups += setupGroups;
    normalGroups += bodyGroups;

    const auto bridgeableNormalGroups = nonEmptyGroups(normalGroups);
    const auto bridgeableCleanupGroups = nonEmptyGroups(cleanupGroups);

    bridgeGroups(bridgeableNormalGroups, "flow");
    bridgeGroups(bridgeableCleanupGroups, "cleanup");

    if (!bridgeableNormalGroups.isEmpty() && !bridgeableCleanupGroups.isEmpty()) {
        addGroupBridge(bridgeableNormalGroups.last(),
                       bridgeableCleanupGroups.first(),
                       result.plan,
                       "normal-cleanup",
                       EdgeKind::Finally,
                       EdgeTrigger::Finally);
    }

    addCleanupRegion(cleanupGroups, cleanupRegionId, result.plan);

    auto firstNonEmpty = [](const QVector<GroupBuildInfo>& groups) -> NodeId {
        for (const auto& group : groups) {
            if (!group.nodeIds.isEmpty()) {
                return group.nodeIds.first();
            }
        }
        return {};
    };

    auto lastNonEmpty = [](const QVector<GroupBuildInfo>& groups) -> NodeId {
        for (auto it = groups.crbegin(); it != groups.crend(); ++it) {
            if (!it->nodeIds.isEmpty()) {
                return it->nodeIds.last();
            }
        }
        return {};
    };

    result.plan.entryNodeId = firstNonEmpty(setupGroups);
    if (result.plan.entryNodeId.isEmpty()) {
        result.plan.entryNodeId = firstNonEmpty(bodyGroups);
    }
    if (result.plan.entryNodeId.isEmpty()) {
        result.plan.entryNodeId = firstNonEmpty(cleanupGroups);
    }

    result.plan.exitNodeId = lastNonEmpty(cleanupGroups);
    if (result.plan.exitNodeId.isEmpty()) {
        result.plan.exitNodeId = lastNonEmpty(bodyGroups);
    }
    if (result.plan.exitNodeId.isEmpty()) {
        result.plan.exitNodeId = lastNonEmpty(setupGroups);
    }

    addDataReferenceEdges(result.plan, result);
    validatePlanReferences(result.plan, result);
    return result;
}

bool PlanBuilder::validateSequence(const SequenceDef& sequence, PlanBuildResult& result) const
{
    if (sequence.id.trimmed().isEmpty()) {
        result.errors.push_back({"Sequence id is required", "Set SequenceDef::id before building"});
    }

    if (sequence.name.trimmed().isEmpty()) {
        result.errors.push_back({"Sequence name is required", "Set SequenceDef::name before building"});
    }

    if (sequence.groups.isEmpty()) {
        result.errors.push_back({"At least one step group is required", "Add a Main group"});
    }

    bool hasEnabledStep = false;
    QHash<NodeId, int> activeNodePathCounts;
    QHash<NodeId, QSet<QString>> idsByScope;
    QHash<NodeId, QSet<QString>> keysByScope;
    const auto collectStep = [&](const StepDef& step,
                                 const NodeId& parentPath,
                                 bool insideLoop,
                                 bool,
                                 const auto& collectRef) -> void {
        if (!step.enabled) {
            return;
        }
        hasEnabledStep = true;
        if (step.id.trimmed().isEmpty()) {
            result.errors.push_back({"Step id is required", "Every enabled StepDef must have a stable id"});
        } else {
            if (step.id.contains('.')) {
                result.errors.push_back({"Step id must not contain '.'",
                                         QString("Change step id %1; dots separate scoped node paths").arg(step.id)});
            }
            if (idsByScope[parentPath].contains(step.id)) {
                result.errors.push_back({"Duplicate sibling step id found",
                                         QString("Duplicate id %1 under %2")
                                             .arg(step.id, parentPath.isEmpty() ? QString("<root>") : parentPath)});
            }
            idsByScope[parentPath].insert(step.id);
        }

        if (step.key.contains('.')) {
            result.errors.push_back({"Step key must not contain '.'",
                                     QString("Change step key %1; dots separate scoped node paths").arg(step.key)});
        }
        static const QSet<QString> reservedKeys = {"outputs", "measurements", "outcome"};
        if (!step.key.isEmpty() && reservedKeys.contains(step.key)) {
            result.errors.push_back({"Step key uses a reserved result field",
                                     QString("Change key %1").arg(step.key)});
        }
        if (!step.key.isEmpty() && keysByScope[parentPath].contains(step.key)) {
            result.errors.push_back({"Duplicate sibling step key found",
                                     QString("Duplicate key %1 under %2")
                                         .arg(step.key, parentPath.isEmpty() ? QString("<root>") : parentPath)});
        }
        if (!step.key.isEmpty()) {
            keysByScope[parentPath].insert(step.key);
        }

        const auto segment = parentPath.isEmpty() || step.key.isEmpty() ? step.id : step.key;
        const NodeId nodePath = parentPath.isEmpty()
            ? segment
            : QString("%1.%2").arg(parentPath, segment);
        activeNodePathCounts[nodePath] += 1;

        if (step.kind == StepKind::Loop) {
            if (insideLoop) {
                result.errors.push_back({"Nested loops are not supported yet",
                                         QString("Move nested loop %1 to a separate sequence or unroll it for now").arg(step.id)});
            }
            if (step.loop.step == 0) {
                result.errors.push_back({"Loop step must not be zero",
                                         QString("Set a non-zero loop.step for %1").arg(step.id)});
            }
            if (step.steps.isEmpty()) {
                result.errors.push_back({"Loop body is required",
                                         QString("Add at least one child step to loop %1").arg(step.id)});
            }
        }

        if (step.kind == StepKind::TestItem) {
            if (step.steps.isEmpty()) {
                result.errors.push_back({"Test item children are required",
                                         QString("Add at least one child step to test item %1").arg(step.id)});
            }
            if (step.retry.maxAttempts > 1) {
                result.errors.push_back({"Test item retry is not supported yet",
                                         QString("Put retry on child steps of %1 instead").arg(step.id)});
            }
        }

        for (const auto& child : step.steps) {
            collectRef(child,
                       nodePath,
                       step.kind == StepKind::Loop || insideLoop,
                       false,
                       collectRef);
        }
    };

    for (const auto& group : sequence.groups) {
        if (!group.enabled) {
            continue;
        }
        for (const auto& step : group.steps) {
            collectStep(step, {}, false, false, collectStep);
        }
    }

    if (!hasEnabledStep) {
        result.errors.push_back({"At least one enabled step is required", "Enable at least one step or add a Main group"});
    }

    QVector<QString> duplicates;
    for (auto it = activeNodePathCounts.constBegin(); it != activeNodePathCounts.constEnd(); ++it) {
        if (it.value() > 1) {
            duplicates.push_back(it.key());
        }
    }

    if (!duplicates.isEmpty()) {
        result.errors.push_back({"Duplicate scoped node path found",
                                 QString("Duplicate paths: %1").arg(duplicates.join(", "))});
    }

    return result.errors.isEmpty();
}

PlanBuilder::GroupBuildInfo PlanBuilder::buildGroup(const StepGroupDef& group,
                                                    const CleanupRegionId& cleanupRegionId,
                                                    ExecutionPlan& plan) const
{
    GroupBuildInfo info;
    info.kind = group.kind;
    if (!group.enabled) {
        return info;
    }

    for (const auto& step : group.steps) {
        if (!step.enabled) {
            continue;
        }

        auto built = buildStep(step, cleanupRegionId, group.kind, plan, {});
        if (!built.controlNodeId.isEmpty()) {
            info.nodeIds.push_back(built.controlNodeId);
        }
    }

    addSerialEdges(info.nodeIds, plan, group.id.isEmpty() ? stepGroupKindName(group.kind) : group.id);
    return info;
}

PlanBuilder::StepBuildInfo PlanBuilder::buildStep(const StepDef& step,
                                                  const CleanupRegionId& cleanupRegionId,
                                                  StepGroupKind groupKind,
                                                  ExecutionPlan& plan,
                                                  const NodeId& parentPath) const
{
    if (step.kind == StepKind::Loop) {
        return buildLoopStep(step, cleanupRegionId, groupKind, plan, parentPath);
    }
    if (step.kind == StepKind::TestItem) {
        return buildTestItemStep(step, cleanupRegionId, groupKind, plan, parentPath);
    }

    StepBuildInfo info;
    const auto segment = parentPath.isEmpty() || step.key.isEmpty() ? step.id : step.key;
    const auto nodeId = parentPath.isEmpty() ? segment : QString("%1.%2").arg(parentPath, segment);
    auto node = buildNode(step, cleanupRegionId, groupKind, nodeId);
    if (plan.addNode(node)) {
        info.controlNodeId = node.id;
        info.allNodeIds.push_back(node.id);
    }
    return info;
}

PlanBuilder::StepBuildInfo PlanBuilder::buildLoopStep(const StepDef& step,
                                                      const CleanupRegionId& cleanupRegionId,
                                                      StepGroupKind groupKind,
                                                      ExecutionPlan& plan,
                                                      const NodeId& parentPath) const
{
    StepBuildInfo info;
    const auto segment = parentPath.isEmpty() || step.key.isEmpty() ? step.id : step.key;
    const auto controllerId = parentPath.isEmpty() ? segment : QString("%1.%2").arg(parentPath, segment);
    auto controller = buildNode(step, cleanupRegionId, groupKind, controllerId);
    if (!plan.addNode(controller)) {
        return info;
    }

    info.controlNodeId = controller.id;
    info.allNodeIds.push_back(controller.id);

    QVector<NodeId> bodyControlNodeIds;
    QVector<NodeId> bodyNodeIds;
    for (const auto& child : step.steps) {
        if (!child.enabled) {
            continue;
        }

        auto built = buildStep(child, cleanupRegionId, groupKind, plan, controller.id);
        if (!built.controlNodeId.isEmpty()) {
            bodyControlNodeIds.push_back(built.controlNodeId);
        }
        bodyNodeIds += built.allNodeIds;
    }

    addSerialEdges(bodyControlNodeIds,
                   plan,
                   QString("loop:%1").arg(step.id),
                   EdgeTrigger::Finally);

    LoopRegion region;
    region.id = controller.id;
    region.controllerNodeId = controller.id;
    region.bodyNodes = bodyNodeIds;
    region.childNodeIds = bodyControlNodeIds;
    if (!bodyControlNodeIds.isEmpty()) {
        region.entryNodes = {bodyControlNodeIds.first()};
        region.exitNodes = {bodyControlNodeIds.last()};
    }
    region.forLoop = step.loop.toRuntimeSpec();
    plan.loopRegions.push_back(region);

    info.allNodeIds += bodyNodeIds;
    return info;
}

PlanBuilder::StepBuildInfo PlanBuilder::buildTestItemStep(
    const StepDef& step,
    const CleanupRegionId& cleanupRegionId,
    StepGroupKind groupKind,
    ExecutionPlan& plan,
    const NodeId& parentPath) const
{
    StepBuildInfo info;
    const auto segment = parentPath.isEmpty() || step.key.isEmpty() ? step.id : step.key;
    const auto controllerId = parentPath.isEmpty() ? segment : QString("%1.%2").arg(parentPath, segment);
    auto controller = buildNode(step, cleanupRegionId, groupKind, controllerId);
    if (!plan.addNode(controller)) {
        return info;
    }

    info.controlNodeId = controller.id;
    info.allNodeIds.push_back(controller.id);

    QVector<NodeId> childControlNodeIds;
    QVector<NodeId> descendantNodeIds;
    for (const auto& child : step.steps) {
        if (!child.enabled) {
            continue;
        }
        auto built = buildStep(child, cleanupRegionId, groupKind, plan, controller.id);
        if (!built.controlNodeId.isEmpty()) {
            childControlNodeIds.push_back(built.controlNodeId);
        }
        descendantNodeIds += built.allNodeIds;
    }

    addSerialEdges(childControlNodeIds,
                   plan,
                   QString("test-item:%1").arg(step.id),
                   EdgeTrigger::Finally);

    TestItemRegion region;
    region.controllerNodeId = controller.id;
    region.childNodeIds = childControlNodeIds;
    plan.testItemRegions.push_back(region);
    info.allNodeIds += descendantNodeIds;
    return info;
}

ExecNode PlanBuilder::buildNode(const StepDef& step,
                                const CleanupRegionId& cleanupRegionId,
                                StepGroupKind groupKind,
                                const NodeId& nodeId) const
{
    ExecNode node;
    node.id = nodeId;
    node.localId = step.id;
    node.key = step.key;
    node.displayName = step.name.isEmpty() ? step.id : step.name;
    node.kind = toExecNodeKind(step.kind);
    node.payload = step.parameters;
    if (!step.moduleId.isEmpty()) {
        node.payload.insert("moduleId", step.moduleId);
    }
    if (!step.functionName.isEmpty()) {
        node.payload.insert("function", step.functionName);
    }
    if (!step.inputs.isEmpty()) {
        node.payload.insert("inputs", step.inputs);
    }
    node.retry = step.retry.toRuntimePolicy();
    node.timeout = step.timeout.toRuntimePolicy();
    node.errorPolicy = step.errorPolicy.toRuntimePolicy();
    node.alwaysRun = step.alwaysRun || groupKind == StepGroupKind::Cleanup || step.kind == StepKind::Cleanup;
    node.resultRecording = step.resultRecording;
    node.checkpointBefore = step.checkpointBefore;
    node.checkpointAfter = step.checkpointAfter;
    node.tags = step.tags;

    if (step.kind == StepKind::Barrier) {
        node.payload = step.barrier.toPayload();
    } else if (step.kind == StepKind::Loop) {
        node.payload = step.loop.toPayload();
    }

    for (const auto& resource : step.resources) {
        node.resources.push_back(resource.toRuntimeRequirement());
    }

    if (groupKind != StepGroupKind::Cleanup &&
        step.kind != StepKind::Cleanup &&
        node.errorPolicy.cleanupRegionId.isEmpty()) {
        node.errorPolicy.cleanupRegionId = cleanupRegionId;
    }

    return node;
}

void PlanBuilder::addSerialEdges(const QVector<NodeId>& nodeIds,
                                 ExecutionPlan& plan,
                                 const QString& edgePrefix,
                                 EdgeTrigger trigger) const
{
    for (int i = 0; i + 1 < nodeIds.size(); ++i) {
        plan.addEdge({QString("%1:%2:%3").arg(edgePrefix, nodeIds[i], nodeIds[i + 1]),
                      nodeIds[i],
                      nodeIds[i + 1],
                      EdgeKind::Control,
                      trigger,
                      {},
                      0});
    }
}

void PlanBuilder::addGroupBridge(const GroupBuildInfo& from,
                                 const GroupBuildInfo& to,
                                 ExecutionPlan& plan,
                                 const QString& edgePrefix,
                                 EdgeKind kind,
                                 EdgeTrigger trigger) const
{
    if (from.nodeIds.isEmpty() || to.nodeIds.isEmpty()) {
        return;
    }

    plan.addEdge({QString("%1:%2:%3").arg(edgePrefix, from.nodeIds.last(), to.nodeIds.first()),
                  from.nodeIds.last(),
                  to.nodeIds.first(),
                  kind,
                  trigger,
                  {},
                  0});
}

void PlanBuilder::addCleanupRegion(const QVector<GroupBuildInfo>& cleanupGroups,
                                   const CleanupRegionId& cleanupRegionId,
                                   ExecutionPlan& plan) const
{
    NodeId entryNode;
    NodeId exitNode;
    for (const auto& cleanup : cleanupGroups) {
        if (cleanup.nodeIds.isEmpty()) {
            continue;
        }
        if (entryNode.isEmpty()) {
            entryNode = cleanup.nodeIds.first();
        }
        exitNode = cleanup.nodeIds.last();
    }

    if (entryNode.isEmpty()) {
        return;
    }

    CleanupRegion region;
    region.id = cleanupRegionId;
    region.entryNodes = {entryNode};
    region.exitNodes = {exitNode};
    region.triggers = {
        CleanupReason::NormalCompletion,
        CleanupReason::StepFailed,
        CleanupReason::ModuleError,
        CleanupReason::Timeout,
        CleanupReason::UserStop,
        CleanupReason::UserAbort,
    };
    plan.cleanupRegions.push_back(region);
}

void PlanBuilder::addDataReferenceEdges(ExecutionPlan& plan, PlanBuildResult& result) const
{
    QVector<ExecEdge> dataEdges;
    for (auto it = plan.nodes.constBegin(); it != plan.nodes.constEnd(); ++it) {
        const auto& consumer = it.value();
        QVector<QString> expressions;
        collectStepExpressions(consumer.payload, expressions);
        for (const auto& expression : expressions) {
            const auto reference = parseStepResultReference(expression);
            if (!reference) {
                result.errors.push_back({QString("Invalid step result expression in %1: %2")
                                             .arg(consumer.id, expression),
                                         "Use ${step:<node-path>.outputs.<field>}, measurements, or outcome"});
                continue;
            }
            const auto source = resolveStepReferenceNode(plan, consumer.id, reference->nodeAddress);
            if (!source) {
                result.errors.push_back({QString("Step result reference in %1 points to missing node: %2")
                                             .arg(consumer.id, reference->nodeAddress),
                                         "Use a sibling key or a complete node path such as 001.rx"});
                continue;
            }
            if (!sourceRunsBeforeConsumer(plan, *source, consumer.id)) {
                result.errors.push_back({QString("Step result source is not guaranteed to run before consumer: %1 -> %2")
                                             .arg(*source, consumer.id),
                                         "Move the source earlier or remove the forward/circular reference"});
                continue;
            }

            const auto isSameDataEdge = [&](const ExecEdge& edge) {
                return edge.from == *source && edge.to == consumer.id &&
                       edge.condition == "step-result";
            };
            const bool exists =
                std::any_of(plan.edges.cbegin(), plan.edges.cend(), isSameDataEdge) ||
                std::any_of(dataEdges.cbegin(), dataEdges.cend(), isSameDataEdge);
            if (!exists) {
                dataEdges.push_back({QString("data:%1:%2").arg(*source, consumer.id),
                                     *source,
                                     consumer.id,
                                     EdgeKind::Dependency,
                                     EdgeTrigger::Finally,
                                     "step-result",
                                     100});
            }
        }
    }
    for (const auto& edge : dataEdges) {
        plan.addEdge(edge);
    }
}

bool PlanBuilder::validatePlanReferences(const ExecutionPlan& plan, PlanBuildResult& result) const
{
    for (const auto& edge : plan.edges) {
        if (!plan.node(edge.from)) {
            result.errors.push_back({QString("Edge references missing source node: %1").arg(edge.from), {}});
        }
        if (!plan.node(edge.to)) {
            result.errors.push_back({QString("Edge references missing target node: %1").arg(edge.to), {}});
        }
    }

    for (const auto& region : plan.cleanupRegions) {
        for (const auto& entry : region.entryNodes) {
            if (!plan.node(entry)) {
                result.errors.push_back({QString("Cleanup region references missing entry node: %1").arg(entry), {}});
            }
        }
    }

    for (const auto& region : plan.loopRegions) {
        if (!plan.node(region.controllerNodeId)) {
            result.errors.push_back({QString("Loop region references missing controller node: %1").arg(region.controllerNodeId), {}});
        }
        if (region.bodyNodes.isEmpty()) {
            result.errors.push_back({QString("Loop region has no body nodes: %1").arg(region.id), {}});
        }
        for (const auto& bodyNode : region.bodyNodes) {
            if (!plan.node(bodyNode)) {
                result.errors.push_back({QString("Loop region references missing body node: %1").arg(bodyNode), {}});
            }
        }
    }

    for (const auto& region : plan.testItemRegions) {
        if (!plan.node(region.controllerNodeId)) {
            result.errors.push_back({QString("Test item references missing controller node: %1")
                                         .arg(region.controllerNodeId), {}});
        }
        if (region.childNodeIds.isEmpty()) {
            result.errors.push_back({QString("Test item has no child nodes: %1")
                                         .arg(region.controllerNodeId), {}});
        }
        for (const auto& childNode : region.childNodeIds) {
            if (!plan.node(childNode)) {
                result.errors.push_back({QString("Test item references missing child node: %1")
                                             .arg(childNode), {}});
            }
        }
    }

    if (!plan.entryNodeId.isEmpty() && !plan.node(plan.entryNodeId)) {
        result.errors.push_back({QString("Entry node does not exist: %1").arg(plan.entryNodeId), {}});
    }
    if (!plan.exitNodeId.isEmpty() && !plan.node(plan.exitNodeId)) {
        result.errors.push_back({QString("Exit node does not exist: %1").arg(plan.exitNodeId), {}});
    }

    return result.errors.isEmpty();
}

} // namespace PicoATE::Core
