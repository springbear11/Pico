#pragma once

#include "PicoATE/Core/RuntimeTypes.h"
#include "PicoATE/Core/TimerService.h"

#include <QHash>
#include <QVector>

#include <optional>

namespace PicoATE::Core {

struct PeriodicTaskRegistration {
    QString taskId;
    NodeId nodeId;
    UutExecution* execution = nullptr;
    FrameId frameId;
    ActivationId activationId;
    int intervalMs = 5000;
    bool runImmediately = true;
};

struct PeriodicTaskInvocation {
    QString taskId;
    RequestId requestId;
    NodeId nodeId;
    UutExecution* execution = nullptr;
    FrameId frameId;
    int invocationIndex = 0;
};

struct PeriodicTaskSummary {
    QString taskId;
    NodeId nodeId;
    UutExecution* execution = nullptr;
    FrameId frameId;
    int executionCount = 0;
    int failureCount = 0;
};

class PeriodicTaskController {
public:
    bool registerTask(PeriodicTaskRegistration registration,
                      QString* errorMessage = nullptr);
    std::optional<PeriodicTaskInvocation> takeReady();
    void complete(const PeriodicTaskInvocation& invocation,
                  const NodeResult& result);
    void defer(const PeriodicTaskInvocation& invocation,
               int delayMs = 20);

    QVector<PeriodicTaskSummary> stopAll();
    bool hasActiveTasks() const;
    int activeTaskCount() const;

private:
    struct ActiveTask {
        PeriodicTaskRegistration registration;
        RequestId pendingRequestId;
        bool inFlight = false;
        int executionCount = 0;
        int failureCount = 0;
    };

    bool schedule(ActiveTask& task, int delayMs);

    TimerService m_timers;
    QHash<QString, ActiveTask> m_tasks;
    QHash<RequestId, QString> m_taskByRequest;
};

} // namespace PicoATE::Core
