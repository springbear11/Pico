#include "PicoATE/Core/RuntimeEvent.h"

#include <utility>

namespace PicoATE::Core {

RuntimeEventEmitter::RuntimeEventEmitter(PlanId planId, IRuntimeEventSink* sink)
    : m_planId(std::move(planId))
    , m_sink(sink)
{
}

void RuntimeEventEmitter::setSink(IRuntimeEventSink* sink)
{
    m_sink = sink;
}

bool RuntimeEventEmitter::hasSink() const
{
    return m_sink != nullptr;
}

void RuntimeEventEmitter::publish(RuntimeEvent event)
{
    if (!m_sink) {
        return;
    }
    event.sequenceNumber = m_nextSequence.fetch_add(1, std::memory_order_relaxed);
    event.timestampUtc = QDateTime::currentDateTimeUtc();
    if (event.planId.isEmpty()) {
        event.planId = m_planId;
    }
    m_sink->publish(event);
}

QString runtimeEventKindName(RuntimeEventKind kind)
{
    switch (kind) {
    case RuntimeEventKind::SessionStateChanged:
        return "SessionStateChanged";
    case RuntimeEventKind::UutRegistered:
        return "UutRegistered";
    case RuntimeEventKind::UutCompleted:
        return "UutCompleted";
    case RuntimeEventKind::NodeStateChanged:
        return "NodeStateChanged";
    case RuntimeEventKind::AttemptStarted:
        return "AttemptStarted";
    case RuntimeEventKind::AttemptCompleted:
        return "AttemptCompleted";
    case RuntimeEventKind::RetryScheduled:
        return "RetryScheduled";
    case RuntimeEventKind::LoopIterationStarted:
        return "LoopIterationStarted";
    case RuntimeEventKind::LoopCompleted:
        return "LoopCompleted";
    case RuntimeEventKind::BarrierWaiting:
        return "BarrierWaiting";
    case RuntimeEventKind::BarrierReleased:
        return "BarrierReleased";
    case RuntimeEventKind::CleanupActivated:
        return "CleanupActivated";
    case RuntimeEventKind::DeviceStateChanged:
        return "DeviceStateChanged";
    }
    return "Unknown";
}

} // namespace PicoATE::Core
