#include "PicoATE/Core/BarrierController.h"

#include <cmath>

namespace PicoATE::Core {

BarrierInstanceId BarrierController::createBarrier(const BarrierNodePayload& payload,
                                                   const QSet<UutId>& expected)
{
    BarrierRuntimeState state;
    state.id = QString("barrier-%1").arg(m_nextBarrier++);
    state.barrierName = payload.barrierName;
    state.cohortId = payload.cohortId;
    state.expected = expected;
    state.policy = payload;
    state.state = BarrierState::Waiting;
    m_barriers.insert(state.id, state);
    return state.id;
}

BarrierReleaseDecision BarrierController::memberArrived(const BarrierArrival& arrival)
{
    auto it = m_barriers.find(arrival.barrierId);
    if (it == m_barriers.end()) {
        return {arrival.barrierId, {}, {}, BarrierState::Failed, "unknown barrier"};
    }

    auto& state = it.value();
    if (!state.firstArrivalAt.isValid()) {
        state.firstArrivalAt = arrival.arrivedAt;
    }

    if (arrival.arrivalOutcome == NodeOutcome::Passed) {
        state.arrived.insert(arrival.uutId);
    } else {
        state.failed.insert(arrival.uutId);
        if (state.policy.arrivalPolicy == BarrierArrivalPolicy::CountFailed) {
            state.arrived.insert(arrival.uutId);
        }
        if (state.policy.arrivalPolicy == BarrierArrivalPolicy::DropFailed ||
            state.policy.failurePolicy == BarrierFailurePolicy::RemoveFailedMember) {
            state.dropped.insert(arrival.uutId);
        }
    }

    return evaluateRelease(state, "member arrived");
}

BarrierReleaseDecision BarrierController::memberFailedBeforeArrival(
    const UutId& uutId,
    const BarrierInstanceId& barrierId,
    NodeOutcome outcome)
{
    auto it = m_barriers.find(barrierId);
    if (it == m_barriers.end()) {
        return {barrierId, {}, {}, BarrierState::Failed, "unknown barrier"};
    }

    auto& state = it.value();
    state.failed.insert(uutId);

    if (outcome == NodeOutcome::Failed &&
        (state.policy.failurePolicy == BarrierFailurePolicy::RemoveFailedMember ||
         state.policy.arrivalPolicy == BarrierArrivalPolicy::DropFailed)) {
        state.dropped.insert(uutId);
    }

    if (state.policy.failurePolicy == BarrierFailurePolicy::FailBarrier) {
        state.state = BarrierState::Failed;
        return {barrierId, {}, {}, BarrierState::Failed, "member failed before arrival"};
    }

    return evaluateRelease(state, "member failed before arrival");
}

BarrierReleaseDecision BarrierController::onTimeout(const BarrierInstanceId& barrierId)
{
    auto it = m_barriers.find(barrierId);
    if (it == m_barriers.end()) {
        return {barrierId, {}, {}, BarrierState::Failed, "unknown barrier"};
    }

    auto& state = it.value();
    switch (state.policy.timeoutPolicy) {
    case BarrierTimeoutPolicy::ReleaseArrived:
        state.released = state.arrived;
        state.state = BarrierState::Released;
        state.releasedAt = QDateTime::currentDateTimeUtc();
        return {barrierId, state.released, state.dropped, BarrierState::Released, "timeout release arrived"};
    case BarrierTimeoutPolicy::ReleaseIfQuorumReached:
        return evaluateRelease(state, "timeout quorum check");
    case BarrierTimeoutPolicy::AbortCohort:
    case BarrierTimeoutPolicy::FailArrivedAndWaiting:
    case BarrierTimeoutPolicy::RequestOperatorDecision:
        state.state = BarrierState::TimedOut;
        return {barrierId, {}, state.dropped, BarrierState::TimedOut, "barrier timeout"};
    }
    return {barrierId, {}, state.dropped, BarrierState::TimedOut, "barrier timeout"};
}

BarrierSnapshot BarrierController::snapshot() const
{
    return m_barriers.values().toVector();
}

void BarrierController::restore(const BarrierSnapshot& snapshot)
{
    m_barriers.clear();
    for (const auto& state : snapshot) {
        m_barriers.insert(state.id, state);
    }
}

std::optional<BarrierRuntimeState> BarrierController::state(
    const BarrierInstanceId& barrierId) const
{
    auto it = m_barriers.constFind(barrierId);
    if (it == m_barriers.constEnd()) {
        return std::nullopt;
    }
    return it.value();
}

BarrierReleaseDecision BarrierController::evaluateRelease(BarrierRuntimeState& state,
                                                          const QString& reason)
{
    if (state.state == BarrierState::Released) {
        return {state.id, state.released, state.dropped, state.state, "already released"};
    }

    if (state.policy.failurePolicy == BarrierFailurePolicy::FailBarrier && !state.failed.isEmpty()) {
        state.state = BarrierState::Failed;
        return {state.id, {}, state.dropped, BarrierState::Failed, "fail barrier"};
    }

    const int arrivedCount = state.arrived.size();
    const int required = requiredCount(state);

    if (arrivedCount >= required) {
        state.released = state.arrived;
        state.state = BarrierState::Released;
        state.releasedAt = QDateTime::currentDateTimeUtc();
        return {state.id, state.released, state.dropped, BarrierState::Released, reason};
    }

    return {state.id, {}, state.dropped, BarrierState::Waiting, "waiting"};
}

int BarrierController::requiredCount(const BarrierRuntimeState& state) const
{
    if (state.policy.arrivalPolicy == BarrierArrivalPolicy::Quorum) {
        if (state.policy.quorumCount > 0) {
            return state.policy.quorumCount;
        }
        return qMax(1, static_cast<int>(std::ceil(state.expected.size() * state.policy.quorumRatio)));
    }

    return qMax(0, state.expected.size() - state.dropped.size());
}

} // namespace PicoATE::Core
