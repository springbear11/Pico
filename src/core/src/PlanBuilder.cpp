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

QVector<NodeId> structuralLineage(const ExecutionPlan& plan, NodeId nodeId)
{
    QVector<NodeId> lineage{nodeId};
    QSet<NodeId> visited{nodeId};
    auto parent = plan.structuralParentOf(nodeId);
    while (parent && !visited.contains(*parent)) {
        nodeId = *parent;
        lineage.push_back(nodeId);
        visited.insert(nodeId);
        parent = plan.structuralParentOf(nodeId);
    }
    return lineage;
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

    const auto sourceLineage = structuralLineage(plan, source);
    const auto consumerLineage = structuralLineage(plan, consumer);
    int sourceIndex = sourceLineage.size() - 1;
    int consumerIndex = consumerLineage.size() - 1;
    while (sourceIndex >= 0 && consumerIndex >= 0 &&
           sourceLineage[sourceIndex] == consumerLineage[consumerIndex]) {
        --sourceIndex;
        --consumerIndex;
    }

    // Compare the two branches directly below their nearest common structural
    // parent. A completed container guarantees that all of its descendants ran.
    if (sourceIndex < 0 || consumerIndex < 0) {
        return false;
    }
    return hasPlanPath(plan,
                       sourceLineage[sourceIndex],
                       consumerLineage[consumerIndex]);
}

std::optional<NodeId> resolveOperatorPromptCloseTarget(const ExecutionPlan& plan,
                                                       const ExecNode& prompt,
                                                       QString requested,
                                                       bool& ambiguous)
{
    ambiguous = false;
    requested = requested.trimmed();
    if (requested.startsWith(QStringLiteral("step:"), Qt::CaseInsensitive)) {
        requested = requested.mid(5).trimmed();
    }
    if (requested.isEmpty()) {
        return std::nullopt;
    }
    if (plan.node(requested)) {
        return requested;
    }

    QVector<NodeId> matches;
    const auto parent = plan.structuralParentOf(prompt.id);
    for (auto it = plan.nodes.constBegin(); it != plan.nodes.constEnd(); ++it) {
        const auto& candidate = it.value();
        if (candidate.localId != requested && candidate.key != requested) {
            continue;
        }
        if (parent == plan.structuralParentOf(candidate.id)) {
            matches.push_back(candidate.id);
        }
    }
    ambiguous = matches.size() > 1;
    return matches.size() == 1
        ? std::optional<NodeId>{matches.first()}
        : std::nullopt;
}

QString staticDeviceResourceId(const StepDef& step)
{
    auto deviceId = step.inputs.value(QStringLiteral("deviceId")).toString().trimmed();
    if (deviceId.isEmpty() || deviceId.contains(QStringLiteral("${"))) {
        return {};
    }
    const auto channelSeparator = deviceId.indexOf('.');
    if (channelSeparator > 0) {
        deviceId = deviceId.left(channelSeparator);
    }
    return deviceId.trimmed();
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
    result.plan.variables = sequence.variables;

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
    addResourceRegions(sequence, result.plan, result);

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

    QSet<QString> variableNames;
    static const QRegularExpression variableNamePattern(
        QStringLiteral("^[A-Za-z_][A-Za-z0-9_]*$"));
    for (const auto& variable : sequence.variables) {
        const auto name = variable.name.trimmed();
        if (!variableNamePattern.match(name).hasMatch()) {
            result.errors.push_back({
                "Invalid sequence variable name",
                QString("Change variable '%1' to letters, numbers, and underscores")
                    .arg(name)});
            continue;
        }
        if (variableNames.contains(name)) {
            result.errors.push_back({
                "Duplicate sequence variable name",
                QString("Keep only one definition for %1").arg(name)});
        }
        variableNames.insert(name);
        if (variable.scope == SequenceVariableScope::Shared &&
            (!variable.value.isValid() || variable.value.isNull())) {
            result.errors.push_back({
                "Shared sequence variable has no value",
                QString("Set a value for %1").arg(name)});
        }
        if (variable.scope == SequenceVariableScope::PerUut &&
            variable.values.isEmpty()) {
            result.errors.push_back({
                "Per-UUT sequence variable has no values",
                QString("Set at least the UUT1 value for %1").arg(name)});
        }
    }

    bool hasEnabledStep = false;
    QHash<NodeId, int> activeNodePathCounts;
    QHash<NodeId, QSet<QString>> idsByScope;
    QHash<NodeId, QSet<QString>> keysByScope;
    const auto containsEnabledBreak = [](const StepDef& parent, const auto& self) -> bool {
        for (const auto& child : parent.steps) {
            if (!child.enabled) {
                continue;
            }
            if (child.kind == StepKind::Break || self(child, self)) {
                return true;
            }
        }
        return false;
    };
    const auto collectStep = [&](const StepDef& step,
                                 const NodeId& parentPath,
                                 StepGroupKind groupKind,
                                 bool insideLoop,
                                 bool insideWhileLoop,
                                 bool insideRetryingTestItem,
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
            if (step.loop.type == LoopType::For && step.loop.step == 0) {
                result.errors.push_back({"Loop step must not be zero",
                                         QString("Set a non-zero loop.step for %1").arg(step.id)});
            }
            if (step.loop.type == LoopType::While) {
                if (step.loop.intervalMs < 0) {
                    result.errors.push_back({
                        "While loop intervalMs must not be negative",
                        QString("Set loop.intervalMs to 0 or greater for %1").arg(step.id)});
                }
                if (step.loop.maxIterations <= 0 && step.loop.timeoutMs <= 0) {
                    result.errors.push_back({
                        "While loop requires a finite guard",
                        QString("Set loop.maxIterations or loop.timeoutMs for %1").arg(step.id)});
                }
                if (!containsEnabledBreak(step, containsEnabledBreak)) {
                    result.errors.push_back({
                        "While loop requires a Break If step",
                        QString("Add an enabled break child to while loop %1").arg(step.id)});
                }
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
        }

        if (step.kind == StepKind::Barrier && insideRetryingTestItem) {
            result.errors.push_back({
                "Barrier inside a retrying TestItem is not supported",
                QString("Move barrier %1 outside the retrying TestItem; coordinated multi-UUT reset is undefined")
                    .arg(step.id)});
        }
        if (step.kind == StepKind::Barrier && insideWhileLoop) {
            result.errors.push_back({
                "Barrier inside a While Loop is not supported",
                QString("Move barrier %1 outside the While Loop; repeated multi-UUT generations are undefined")
                    .arg(step.id)});
        }
        if (step.kind == StepKind::Break && !insideLoop) {
            result.errors.push_back({
                "Break If must be inside a Loop",
                QString("Move break step %1 into a For or While loop").arg(step.id)});
        }


        if (step.periodic.enabled) {
            if (groupKind != StepGroupKind::Setup || !parentPath.isEmpty()) {
                result.errors.push_back({
                    "Periodic task must be a top-level Setup step",
                    QString("Move %1 directly into the Setup group").arg(step.id)});
            }
            if (step.kind != StepKind::Action) {
                result.errors.push_back({
                    "Periodic task supports Action steps only",
                    QString("Change %1 to an action step").arg(step.id)});
            }
            if (step.periodic.intervalMs <= 0) {
                result.errors.push_back({
                    "Periodic task interval must be positive",
                    QString("Set periodic.intervalMs to at least 1 for %1").arg(step.id)});
            }
            if (step.periodic.counterIncrement <= 0) {
                result.errors.push_back({
                    "Periodic counter increment must be positive",
                    QString("Set periodic.counter.increment to at least 1 for %1")
                        .arg(step.id)});
            }
            if (step.periodic.counterWrapAt > 0 &&
                step.periodic.counterWrapAt < step.periodic.counterStart) {
                result.errors.push_back({
                    "Periodic counter wrapAt must not be less than start",
                    QString("Increase wrapAt or reduce counter.start for %1")
                        .arg(step.id)});
            }
            if (step.retry.maxAttempts != 1) {
                result.errors.push_back({
                    "Periodic task does not support Step retry",
                    QString("Set retry.maxAttempts to 1 for %1; the next timer tick is the retry boundary")
                        .arg(step.id)});
            }
            if (step.resourceRegionStart || !step.resourceRegionEnd.isEmpty()) {
                result.errors.push_back({
                    "Periodic task cannot define a resource region boundary",
                    QString("Use the periodic Step resources list for %1").arg(step.id)});
            }
            if (step.resources.isEmpty() && staticDeviceResourceId(step).isEmpty()) {
                result.errors.push_back({
                    "Periodic task requires an exclusive resource",
                    QString("Select a fixed deviceId or add resources to %1").arg(step.id)});
            }
        }

        const bool retryingTestItem = insideRetryingTestItem ||
            (step.kind == StepKind::TestItem && step.retry.maxAttempts > 1);
        const bool whileLoop = insideWhileLoop ||
            (step.kind == StepKind::Loop && step.loop.type == LoopType::While);
        for (const auto& child : step.steps) {
            collectRef(child,
                       nodePath,
                       groupKind,
                       step.kind == StepKind::Loop || insideLoop,
                       whileLoop,
                       retryingTestItem,
                       collectRef);
        }
    };

    for (const auto& group : sequence.groups) {
        if (!group.enabled) {
            continue;
        }
        for (const auto& step : group.steps) {
            collectStep(step, {}, group.kind, false, false, false, collectStep);
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
    region.type = step.loop.type;
    region.forLoop = step.loop.toRuntimeSpec();
    region.whileLoop = step.loop.toRuntimeWhileSpec();
    if (region.type == LoopType::While) {
        region.forLoop.variableName = "iteration";
        region.forLoop.from = 1;
        region.forLoop.to = qMax(1, region.whileLoop.maxIterations);
        region.forLoop.step = 1;
    }
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
    node.phase = toExecutionPhase(groupKind);
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
    node.periodic = step.periodic.toRuntimePolicy();
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
    } else if (step.kind == StepKind::OperatorPrompt) {
        node.payload = step.prompt.toPayload();
    }

    for (const auto& resource : step.resources) {
        node.resources.push_back(resource.toRuntimeRequirement());
    }
    if (node.periodic.enabled && node.resources.isEmpty()) {
        ResourceRequirement requirement;
        requirement.resourceId = staticDeviceResourceId(step);
        requirement.mode = ResourceMode::Exclusive;
        node.resources.push_back(requirement);
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

void PlanBuilder::addResourceRegions(const SequenceDef& sequence,
                                     ExecutionPlan& plan,
                                     PlanBuildResult& result) const
{
    QSet<ResourceRegionId> regionIds;
    const auto containsBarrier = [](const StepDef& step, const auto& self) -> bool {
        if (step.kind == StepKind::Barrier) {
            return true;
        }
        for (const auto& child : step.steps) {
            if (child.enabled && self(child, self)) {
                return true;
            }
        }
        return false;
    };
    const auto nodeIdForStep = [](const StepDef& step, const NodeId& parentPath) {
        const auto segment = parentPath.isEmpty() || step.key.isEmpty()
            ? step.id
            : step.key;
        return parentPath.isEmpty()
            ? segment
            : QString("%1.%2").arg(parentPath, segment);
    };
    const auto rejectDisabledMarkers = [&](const StepDef& step,
                                           const auto& self) -> void {
        if (step.resourceRegionStart || !step.resourceRegionEnd.isEmpty()) {
            result.errors.push_back({
                QString("Disabled step contains a resource region marker: %1")
                    .arg(step.id),
                "Enable the step or remove its resource region marker"});
        }
        for (const auto& child : step.steps) {
            self(child, self);
        }
    };

    struct OpenRegion {
        ResourceRegionStartDef definition;
        NodeId entryNodeId;
        bool containsBarrier = false;
    };

    const auto processSiblings = [&](const QVector<StepDef>& steps,
                                     const NodeId& parentPath,
                                     bool insideAncestorRegion,
                                     const auto& self) -> void {
        std::optional<OpenRegion> open;
        for (const auto& step : steps) {
            if (!step.enabled) {
                rejectDisabledMarkers(step, rejectDisabledMarkers);
                continue;
            }

            const auto nodeId = nodeIdForStep(step, parentPath);
            const bool hasStart = step.resourceRegionStart.has_value();
            const bool hasEnd = !step.resourceRegionEnd.isEmpty();
            const bool nestedMarker = insideAncestorRegion && (hasStart || hasEnd);
            if (nestedMarker) {
                result.errors.push_back({
                    QString("Nested resource region marker is not supported: %1")
                        .arg(nodeId),
                    "Place LOCK/UNLOCK on siblings outside the enclosing interval"});
            } else if (hasStart) {
                const auto& definition = *step.resourceRegionStart;
                if (open) {
                    result.errors.push_back({
                        QString("Resource region %1 starts before %2 is closed")
                            .arg(definition.id, open->definition.id),
                        "Place the pending exit marker before starting another region"});
                } else if (definition.id.trimmed().isEmpty()) {
                    result.errors.push_back({
                        QString("Resource region entry on %1 has no id").arg(nodeId),
                        "Assign a stable resource region id"});
                } else if (definition.resources.isEmpty()) {
                    result.errors.push_back({
                        QString("Resource region %1 has no resource").arg(definition.id),
                        "Select at least one shared resource for the interval"});
                } else if (regionIds.contains(definition.id)) {
                    result.errors.push_back({
                        QString("Duplicate resource region id: %1").arg(definition.id),
                        "Use a unique id for every resource interval"});
                } else {
                    regionIds.insert(definition.id);
                    open = OpenRegion{definition, nodeId, containsBarrier(step, containsBarrier)};
                }
            } else if (!insideAncestorRegion && open &&
                       containsBarrier(step, containsBarrier)) {
                open->containsBarrier = true;
            }

            bool singleNodeRegion = false;
            if (!nestedMarker && hasEnd) {
                if (!open) {
                    result.errors.push_back({
                        QString("Resource region exit on %1 has no matching entry")
                            .arg(nodeId),
                        "Place an entry marker before this exit under the same parent"});
                } else if (step.resourceRegionEnd != open->definition.id) {
                    result.errors.push_back({
                        QString("Resource region exit %1 does not match open region %2")
                            .arg(step.resourceRegionEnd, open->definition.id),
                        "Use the same region id on the entry and exit markers"});
                    open.reset();
                } else {
                    singleNodeRegion = nodeId == open->entryNodeId;
                    open->containsBarrier = open->containsBarrier ||
                        containsBarrier(step, containsBarrier);
                    if (open->containsBarrier) {
                        result.errors.push_back({
                            QString("Resource region %1 contains a Barrier")
                                .arg(open->definition.id),
                            "Move the Barrier outside the locked interval to avoid multi-UUT deadlock"});
                    } else {
                        ResourceRegion region;
                        region.id = open->definition.id;
                        region.entryNodeId = open->entryNodeId;
                        region.exitNodeId = nodeId;
                        for (const auto& resource : open->definition.resources) {
                            region.requirements.push_back(
                                resource.toRuntimeRequirement());
                        }
                        plan.resourceRegions.push_back(std::move(region));
                    }
                    open.reset();
                }
            }

            // Children below an UNLOCK row are outside the completed interval.
            self(step.steps,
                 nodeId,
                 insideAncestorRegion || open.has_value() || singleNodeRegion,
                 self);
        }

        if (open) {
            result.errors.push_back({
                QString("Resource region %1 has no exit marker")
                    .arg(open->definition.id),
                "Place UNLOCK on the same step or a later sibling under the same parent"});
        }
    };

    for (const auto& group : sequence.groups) {
        if (!group.enabled) {
            for (const auto& step : group.steps) {
                rejectDisabledMarkers(step, rejectDisabledMarkers);
            }
            continue;
        }
        processSiblings(group.steps, {}, false, processSiblings);
    }
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

    for (const auto& region : plan.resourceRegions) {
        if (!plan.node(region.entryNodeId) || !plan.node(region.exitNodeId)) {
            result.errors.push_back({
                QString("Resource region references a missing boundary node: %1")
                    .arg(region.id),
                "Recreate the entry and exit markers"});
        }
        if (region.requirements.isEmpty()) {
            result.errors.push_back({
                QString("Resource region has no requirements: %1").arg(region.id),
                "Select at least one resource"});
        }
    }

    for (auto it = plan.nodes.constBegin(); it != plan.nodes.constEnd(); ++it) {
        const auto& node = it.value();
        if (node.kind != ExecNodeKind::OperatorPrompt ||
            node.payload.value(QStringLiteral("mode")).toString() != QStringLiteral("notice")) {
            continue;
        }
        const auto requested = node.payload.value(QStringLiteral("closeOnStep"))
                                   .toString().trimmed();
        if (requested.isEmpty()) {
            continue;
        }
        bool ambiguous = false;
        const auto target = resolveOperatorPromptCloseTarget(
            plan, node, requested, ambiguous);
        if (!target) {
            result.errors.push_back({
                ambiguous
                    ? QString("Operator prompt closeOnStep is ambiguous: %1").arg(requested)
                    : QString("Operator prompt closeOnStep references missing or disabled node: %1")
                          .arg(requested),
                QString("Select an enabled step after %1").arg(node.id)});
            continue;
        }
        if (!sourceRunsBeforeConsumer(plan, node.id, *target)) {
            result.errors.push_back({
                QString("Operator prompt closeOnStep must reference a later node: %1")
                    .arg(requested),
                QString("Select an enabled step after %1").arg(node.id)});
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
