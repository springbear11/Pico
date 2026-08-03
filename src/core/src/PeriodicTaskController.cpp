#include "PicoATE/Core/PeriodicTaskController.h"

#include "PicoATE/Core/ExecutionRequest.h"

#include <algorithm>
#include <utility>

namespace PicoATE::Core {

namespace {

bool periodicOutcomeFailed(NodeOutcome outcome)
{
    return outcome == NodeOutcome::Failed ||
           outcome == NodeOutcome::Error ||
           outcome == NodeOutcome::Timeout;
}

} // namespace

bool PeriodicTaskController::registerTask(PeriodicTaskRegistration registration,
                                          QString* errorMessage)
{
    registration.taskId = registration.taskId.trimmed();
    if (registration.taskId.isEmpty() || registration.nodeId.trimmed().isEmpty() ||
        !registration.execution || registration.intervalMs <= 0) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("Periodic task registration is invalid");
        }
        return false;
    }
    if (m_tasks.contains(registration.taskId)) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("Periodic task is already registered: %1")
                                .arg(registration.taskId);
        }
        return false;
    }

    ActiveTask task;
    task.registration = std::move(registration);
    const auto taskId = task.registration.taskId;
    if (!schedule(task, task.registration.runImmediately ? 0 : task.registration.intervalMs)) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("Periodic task timer could not be scheduled: %1")
                                .arg(taskId);
        }
        return false;
    }
    m_tasks.insert(taskId, std::move(task));
    return true;
}

std::optional<PeriodicTaskInvocation> PeriodicTaskController::takeReady()
{
    while (const auto completion = m_timers.takeAnyReady()) {
        const auto taskId = m_taskByRequest.take(completion->requestId);
        auto taskIt = m_tasks.find(taskId);
        if (taskIt == m_tasks.end() || taskIt->inFlight ||
            taskIt->pendingRequestId != completion->requestId) {
            continue;
        }

        taskIt->pendingRequestId.clear();
        taskIt->inFlight = true;
        PeriodicTaskInvocation invocation;
        invocation.taskId = taskId;
        invocation.requestId = completion->requestId;
        invocation.nodeId = taskIt->registration.nodeId;
        invocation.execution = taskIt->registration.execution;
        invocation.frameId = taskIt->registration.frameId;
        invocation.invocationIndex = taskIt->executionCount;
        return invocation;
    }
    return std::nullopt;
}

void PeriodicTaskController::complete(const PeriodicTaskInvocation& invocation,
                                      const NodeResult& result)
{
    auto taskIt = m_tasks.find(invocation.taskId);
    if (taskIt == m_tasks.end() || !taskIt->inFlight) {
        return;
    }

    taskIt->inFlight = false;
    taskIt->executionCount += 1;
    if (periodicOutcomeFailed(result.outcome)) {
        taskIt->failureCount += 1;
    }
    schedule(*taskIt, taskIt->registration.intervalMs);
}

void PeriodicTaskController::defer(const PeriodicTaskInvocation& invocation,
                                   int delayMs)
{
    auto taskIt = m_tasks.find(invocation.taskId);
    if (taskIt == m_tasks.end() || !taskIt->inFlight) {
        return;
    }
    taskIt->inFlight = false;
    schedule(*taskIt, std::max(1, delayMs));
}

QVector<PeriodicTaskSummary> PeriodicTaskController::stopAll()
{
    QVector<PeriodicTaskSummary> summaries;
    summaries.reserve(m_tasks.size());
    for (auto it = m_tasks.begin(); it != m_tasks.end(); ++it) {
        if (!it->pendingRequestId.isEmpty()) {
            m_timers.cancel(it->pendingRequestId);
            m_taskByRequest.remove(it->pendingRequestId);
        }
        summaries.push_back({it->registration.taskId,
                             it->registration.nodeId,
                             it->registration.execution,
                             it->registration.frameId,
                             it->executionCount,
                             it->failureCount});
    }
    m_tasks.clear();
    m_taskByRequest.clear();
    return summaries;
}

bool PeriodicTaskController::hasActiveTasks() const
{
    return !m_tasks.isEmpty();
}

int PeriodicTaskController::activeTaskCount() const
{
    return m_tasks.size();
}

bool PeriodicTaskController::schedule(ActiveTask& task, int delayMs)
{
    TimerRequest timer;
    timer.requestId = createRequestId(QStringLiteral("periodic"));
    timer.uutId = task.registration.execution
        ? task.registration.execution->uutId
        : UutId{};
    timer.frameId = task.registration.frameId;
    timer.nodeId = task.registration.nodeId;
    timer.activationId = task.registration.activationId;
    timer.attemptId = timer.requestId;
    timer.durationMs = std::max(0, delayMs);
    timer.startedAt = QDateTime::currentDateTimeUtc();
    if (!m_timers.schedule(timer)) {
        return false;
    }
    task.pendingRequestId = timer.requestId;
    m_taskByRequest.insert(timer.requestId, task.registration.taskId);
    return true;
}

} // namespace PicoATE::Core
