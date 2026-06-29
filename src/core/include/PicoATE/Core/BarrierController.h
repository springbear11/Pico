#pragma once

#include "PicoATE/Core/RuntimeTypes.h"

#include <QHash>
#include <QSet>

namespace PicoATE::Core {

enum class BarrierArrivalPolicy {
    WaitAll,
    DropFailed,
    CountFailed,
    Quorum,
    BestEffort,
    ManualDecision
};

enum class BarrierReleasePolicy {
    Lockstep,
    Latch,
    Cohort,
    RollingWindow
};

enum class BarrierFailurePolicy {
    FailBarrier,
    RemoveFailedMember,
    HoldFailedMember,
    ContinueWithWarning,
    AbortCohort
};

enum class BarrierTimeoutPolicy {
    FailArrivedAndWaiting,
    ReleaseArrived,
    ReleaseIfQuorumReached,
    AbortCohort,
    RequestOperatorDecision
};

enum class BarrierState {
    Created,
    Waiting,
    Released,
    Failed,
    TimedOut
};

struct BarrierNodePayload {
    QString barrierName;
    QString cohortId;
    int expectedUutCount = -1;
    int quorumCount = -1;
    double quorumRatio = 1.0;
    int arrivalTimeoutMs = 60000;
    int releaseTimeoutMs = 5000;
    BarrierArrivalPolicy arrivalPolicy = BarrierArrivalPolicy::WaitAll;
    BarrierReleasePolicy releasePolicy = BarrierReleasePolicy::Lockstep;
    BarrierFailurePolicy failurePolicy = BarrierFailurePolicy::FailBarrier;
    BarrierTimeoutPolicy timeoutPolicy = BarrierTimeoutPolicy::FailArrivedAndWaiting;
    bool releaseHeldResourcesOnWait = true;
};

struct BarrierRuntimeState {
    BarrierInstanceId id;
    QString barrierName;
    QString cohortId;
    QSet<UutId> expected;
    QSet<UutId> arrived;
    QSet<UutId> failed;
    QSet<UutId> dropped;
    QSet<UutId> released;
    QDateTime openedAt = QDateTime::currentDateTimeUtc();
    QDateTime firstArrivalAt;
    QDateTime releasedAt;
    BarrierState state = BarrierState::Created;
    BarrierNodePayload policy;
};

using BarrierSnapshot = QVector<BarrierRuntimeState>;

struct BarrierArrival {
    BarrierInstanceId barrierId;
    UutId uutId;
    FrameId frameId;
    NodeId barrierNodeId;
    NodeOutcome arrivalOutcome = NodeOutcome::Passed;
    QDateTime arrivedAt = QDateTime::currentDateTimeUtc();
};

struct BarrierReleaseDecision {
    BarrierInstanceId barrierId;
    QSet<UutId> releasedUuts;
    QSet<UutId> droppedUuts;
    BarrierState newState = BarrierState::Waiting;
    QString reason;

    bool released() const { return newState == BarrierState::Released; }
};

class BarrierController {
public:
    BarrierInstanceId createBarrier(const BarrierNodePayload& payload,
                                    const QSet<UutId>& expected);

    BarrierReleaseDecision memberArrived(const BarrierArrival& arrival);
    BarrierReleaseDecision memberFailedBeforeArrival(const UutId& uutId,
                                                     const BarrierInstanceId& barrierId,
                                                     NodeOutcome outcome);
    BarrierReleaseDecision onTimeout(const BarrierInstanceId& barrierId);

    BarrierSnapshot snapshot() const;
    void restore(const BarrierSnapshot& snapshot);

    std::optional<BarrierRuntimeState> state(const BarrierInstanceId& barrierId) const;

private:
    BarrierReleaseDecision evaluateRelease(BarrierRuntimeState& state,
                                           const QString& reason);
    int requiredCount(const BarrierRuntimeState& state) const;

    QHash<BarrierInstanceId, BarrierRuntimeState> m_barriers;
    int m_nextBarrier = 1;
};

} // namespace PicoATE::Core
