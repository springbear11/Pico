#include "PicoATE/Core/PlanBuilder.h"

namespace PicoATE::Core {

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

    QHash<QString, int> activeStepIdCounts;
    bool hasEnabledStep = false;
    const auto collectStep = [&](const StepDef& step,
                                 bool insideLoop,
                                 bool insideTestItem,
                                 const auto& collectRef) -> void {
        if (!step.enabled) {
            return;
        }
        hasEnabledStep = true;
        if (step.id.trimmed().isEmpty()) {
            result.errors.push_back({"Step id is required", "Every enabled StepDef must have a stable id"});
        } else {
            activeStepIdCounts[step.id] += 1;
        }

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
            if (insideTestItem) {
                result.errors.push_back({"Nested test items are not supported yet",
                                         QString("Move nested test item %1 to the same level").arg(step.id)});
            }
            if (step.steps.isEmpty()) {
                result.errors.push_back({"Test item children are required",
                                         QString("Add at least one child step to test item %1").arg(step.id)});
            }
            if (step.retry.maxAttempts > 1) {
                result.errors.push_back({"Test item retry is not supported yet",
                                         QString("Put retry on child steps of %1 instead").arg(step.id)});
            }
            for (const auto& child : step.steps) {
                if (child.enabled && child.kind == StepKind::Loop) {
                    result.errors.push_back({"Loop directly inside a test item is not supported yet",
                                             QString("Move loop %1 outside test item %2").arg(child.id, step.id)});
                }
            }
        }

        for (const auto& child : step.steps) {
            collectRef(child,
                       step.kind == StepKind::Loop || insideLoop,
                       step.kind == StepKind::TestItem || insideTestItem,
                       collectRef);
        }
    };

    for (const auto& group : sequence.groups) {
        if (!group.enabled) {
            continue;
        }
        for (const auto& step : group.steps) {
            collectStep(step, false, false, collectStep);
        }
    }

    if (!hasEnabledStep) {
        result.errors.push_back({"At least one enabled step is required", "Enable at least one step or add a Main group"});
    }

    QVector<QString> duplicates;
    for (auto it = activeStepIdCounts.constBegin(); it != activeStepIdCounts.constEnd(); ++it) {
        if (it.value() > 1) {
            duplicates.push_back(it.key());
        }
    }

    if (!duplicates.isEmpty()) {
        result.errors.push_back({"Duplicate step id found",
                                 QString("Duplicate ids: %1").arg(duplicates.join(", "))});
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

        auto built = buildStep(step, cleanupRegionId, group.kind, plan);
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
                                                  ExecutionPlan& plan) const
{
    if (step.kind == StepKind::Loop) {
        return buildLoopStep(step, cleanupRegionId, groupKind, plan);
    }
    if (step.kind == StepKind::TestItem) {
        return buildTestItemStep(step, cleanupRegionId, groupKind, plan);
    }

    StepBuildInfo info;
    auto node = buildNode(step, cleanupRegionId, groupKind);
    if (plan.addNode(node)) {
        info.controlNodeId = node.id;
        info.allNodeIds.push_back(node.id);
    }
    return info;
}

PlanBuilder::StepBuildInfo PlanBuilder::buildLoopStep(const StepDef& step,
                                                      const CleanupRegionId& cleanupRegionId,
                                                      StepGroupKind groupKind,
                                                      ExecutionPlan& plan) const
{
    StepBuildInfo info;
    auto controller = buildNode(step, cleanupRegionId, groupKind);
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

        auto built = buildStep(child, cleanupRegionId, groupKind, plan);
        if (!built.controlNodeId.isEmpty()) {
            bodyControlNodeIds.push_back(built.controlNodeId);
        }
        bodyNodeIds += built.allNodeIds;
    }

    addSerialEdges(bodyControlNodeIds, plan, QString("loop:%1").arg(step.id));

    LoopRegion region;
    region.id = step.id;
    region.controllerNodeId = controller.id;
    region.bodyNodes = bodyNodeIds;
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
    ExecutionPlan& plan) const
{
    StepBuildInfo info;
    auto controller = buildNode(step, cleanupRegionId, groupKind);
    if (!plan.addNode(controller)) {
        return info;
    }

    info.controlNodeId = controller.id;
    info.allNodeIds.push_back(controller.id);

    QVector<NodeId> childControlNodeIds;
    QVector<NodeId> childNodeIds;
    for (const auto& child : step.steps) {
        if (!child.enabled) {
            continue;
        }
        auto built = buildStep(child, cleanupRegionId, groupKind, plan);
        if (!built.controlNodeId.isEmpty()) {
            childControlNodeIds.push_back(built.controlNodeId);
        }
        childNodeIds += built.allNodeIds;
    }

    addSerialEdges(childControlNodeIds,
                   plan,
                   QString("test-item:%1").arg(step.id),
                   EdgeTrigger::Finally);

    TestItemRegion region;
    region.controllerNodeId = controller.id;
    region.childNodeIds = childNodeIds;
    plan.testItemRegions.push_back(region);
    info.allNodeIds += childNodeIds;
    return info;
}

ExecNode PlanBuilder::buildNode(const StepDef& step,
                                const CleanupRegionId& cleanupRegionId,
                                StepGroupKind groupKind) const
{
    ExecNode node;
    node.id = step.id;
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
