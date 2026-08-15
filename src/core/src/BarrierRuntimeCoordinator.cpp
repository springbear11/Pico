#include "PicoATE/Core/BarrierRuntimeCoordinator.h"

#include <utility>

namespace PicoATE::Core {

BarrierRuntimeCoordinator::BarrierRuntimeCoordinator(
    const ExecutionPlan& plan,
    BarrierController& barriers)
    : m_plan(plan)
    , m_barriers(barriers)
{
}

void BarrierRuntimeCoordinator::setCohortUuts(const QSet<UutId>& uutIds)
{
    m_cohortUuts = uutIds;
}

BarrierRuntimeTransition BarrierRuntimeCoordinator::memberArrived(
    const ExecNode& barrierNode,
    const UutId& uutId,
    const FrameId& frameId)
{
    BarrierArrival arrival;
    arrival.barrierId = instanceForNode(barrierNode, uutId);
    arrival.uutId = uutId;
    arrival.frameId = frameId;
    arrival.barrierNodeId = barrierNode.id;
    arrival.arrivalOutcome = NodeOutcome::Passed;

    auto decision = m_barriers.memberArrived(arrival);
    if (decision.released()) {
        queueRelease(decision);
    }
    return {barrierNode.id, std::move(decision)};
}

QVector<BarrierRuntimeTransition>
BarrierRuntimeCoordinator::memberFailedBeforeReachableBarriers(
    const ExecNode& failedNode,
    const UutId& uutId,
    NodeOutcome outcome)
{
    QVector<BarrierRuntimeTransition> transitions;
    if (failedNode.kind == ExecNodeKind::Barrier) {
        return transitions;
    }

    for (auto it = m_plan.nodes.constBegin(); it != m_plan.nodes.constEnd(); ++it) {
        const auto& barrierNode = it.value();
        if (barrierNode.kind != ExecNodeKind::Barrier ||
            !hasPath(failedNode.id, barrierNode.id)) {
            continue;
        }

        const auto barrierId = instanceForNode(barrierNode, uutId);
        auto decision = m_barriers.memberFailedBeforeArrival(
            uutId, barrierId, outcome);
        if (decision.released()) {
            queueRelease(decision);
        }
        transitions.push_back({barrierNode.id, std::move(decision)});
    }
    return transitions;
}

void BarrierRuntimeCoordinator::queueRelease(
    const BarrierReleaseDecision& decision)
{
    m_pendingReleases.insert(decision.barrierId, decision);
}

QVector<BarrierRuntimeTransition>
BarrierRuntimeCoordinator::takePendingReleases()
{
    QVector<BarrierRuntimeTransition> transitions;
    QVector<BarrierInstanceId> applied;
    for (auto it = m_pendingReleases.constBegin();
         it != m_pendingReleases.constEnd(); ++it) {
        const auto node = m_nodeByBarrier.constFind(it.key());
        if (node == m_nodeByBarrier.constEnd()) {
            continue;
        }
        transitions.push_back({node.value(), it.value()});
        applied.push_back(it.key());
    }
    for (const auto& barrierId : applied) {
        m_pendingReleases.remove(barrierId);
    }
    return transitions;
}

BarrierNodePayload BarrierRuntimeCoordinator::payloadFromNode(
    const ExecNode& node) const
{
    BarrierNodePayload payload;
    payload.barrierName = node.payload.value(
        QStringLiteral("barrierName"), node.id).toString();
    payload.cohortId = node.payload.value(
        QStringLiteral("cohortId"), QStringLiteral("default")).toString();
    payload.expectedUutCount = node.payload.value(
        QStringLiteral("expectedUutCount"), -1).toInt();

    const auto arrivalPolicy = node.payload.value(
        QStringLiteral("arrivalPolicy"), QStringLiteral("WaitAll")).toString();
    if (arrivalPolicy.compare(QStringLiteral("DropFailed"),
                              Qt::CaseInsensitive) == 0) {
        payload.arrivalPolicy = BarrierArrivalPolicy::DropFailed;
    } else if (arrivalPolicy.compare(QStringLiteral("Quorum"),
                                     Qt::CaseInsensitive) == 0) {
        payload.arrivalPolicy = BarrierArrivalPolicy::Quorum;
    } else if (arrivalPolicy.compare(QStringLiteral("BestEffort"),
                                     Qt::CaseInsensitive) == 0) {
        payload.arrivalPolicy = BarrierArrivalPolicy::BestEffort;
    } else {
        payload.arrivalPolicy = BarrierArrivalPolicy::WaitAll;
    }

    const auto releasePolicy = node.payload.value(
        QStringLiteral("releasePolicy"), QStringLiteral("Lockstep")).toString();
    if (releasePolicy.compare(QStringLiteral("Latch"),
                              Qt::CaseInsensitive) == 0) {
        payload.releasePolicy = BarrierReleasePolicy::Latch;
    } else if (releasePolicy.compare(QStringLiteral("Cohort"),
                                     Qt::CaseInsensitive) == 0) {
        payload.releasePolicy = BarrierReleasePolicy::Cohort;
    } else if (releasePolicy.compare(QStringLiteral("RollingWindow"),
                                     Qt::CaseInsensitive) == 0) {
        payload.releasePolicy = BarrierReleasePolicy::RollingWindow;
    } else {
        payload.releasePolicy = BarrierReleasePolicy::Lockstep;
    }

    const auto failurePolicy = node.payload.value(
        QStringLiteral("failurePolicy"),
        QStringLiteral("FailBarrier")).toString();
    if (failurePolicy.compare(QStringLiteral("RemoveFailedMember"),
                              Qt::CaseInsensitive) == 0) {
        payload.failurePolicy = BarrierFailurePolicy::RemoveFailedMember;
    } else if (failurePolicy.compare(QStringLiteral("HoldFailedMember"),
                                     Qt::CaseInsensitive) == 0) {
        payload.failurePolicy = BarrierFailurePolicy::HoldFailedMember;
    } else if (failurePolicy.compare(QStringLiteral("ContinueWithWarning"),
                                     Qt::CaseInsensitive) == 0) {
        payload.failurePolicy = BarrierFailurePolicy::ContinueWithWarning;
    } else if (failurePolicy.compare(QStringLiteral("AbortCohort"),
                                     Qt::CaseInsensitive) == 0) {
        payload.failurePolicy = BarrierFailurePolicy::AbortCohort;
    } else {
        payload.failurePolicy = BarrierFailurePolicy::FailBarrier;
    }

    const auto timeoutPolicy = node.payload.value(
        QStringLiteral("timeoutPolicy"),
        QStringLiteral("FailArrivedAndWaiting")).toString();
    if (timeoutPolicy.compare(QStringLiteral("ReleaseArrived"),
                              Qt::CaseInsensitive) == 0) {
        payload.timeoutPolicy = BarrierTimeoutPolicy::ReleaseArrived;
    } else if (timeoutPolicy.compare(QStringLiteral("ReleaseIfQuorumReached"),
                                     Qt::CaseInsensitive) == 0) {
        payload.timeoutPolicy = BarrierTimeoutPolicy::ReleaseIfQuorumReached;
    } else if (timeoutPolicy.compare(QStringLiteral("AbortCohort"),
                                     Qt::CaseInsensitive) == 0) {
        payload.timeoutPolicy = BarrierTimeoutPolicy::AbortCohort;
    } else if (timeoutPolicy.compare(QStringLiteral("RequestOperatorDecision"),
                                     Qt::CaseInsensitive) == 0) {
        payload.timeoutPolicy = BarrierTimeoutPolicy::RequestOperatorDecision;
    } else {
        payload.timeoutPolicy = BarrierTimeoutPolicy::FailArrivedAndWaiting;
    }
    return payload;
}

BarrierInstanceId BarrierRuntimeCoordinator::instanceForNode(
    const ExecNode& node,
    const UutId& uutId)
{
    const auto existing = m_barrierByNode.constFind(node.id);
    if (existing != m_barrierByNode.constEnd()) {
        return existing.value();
    }

    auto expected = m_cohortUuts;
    if (expected.isEmpty()) {
        expected.insert(uutId);
    }
    const auto barrierId = m_barriers.createBarrier(
        payloadFromNode(node), expected);
    m_barrierByNode.insert(node.id, barrierId);
    m_nodeByBarrier.insert(barrierId, node.id);
    return barrierId;
}

bool BarrierRuntimeCoordinator::hasPath(const NodeId& from,
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
