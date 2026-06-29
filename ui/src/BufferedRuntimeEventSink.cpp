#include "BufferedRuntimeEventSink.h"

#include <QMutexLocker>

#include <utility>

namespace PicoATE::Ui {

BufferedRuntimeEventSink::BufferedRuntimeEventSink(int maximumBufferedEvents)
    : m_maximumBufferedEvents(qMax(1, maximumBufferedEvents))
{
    m_events.reserve(qMin(m_maximumBufferedEvents, 1024));
}

void BufferedRuntimeEventSink::publish(const PicoATE::Core::RuntimeEvent& event)
{
    QMutexLocker lock(&m_mutex);
    if (m_events.size() >= m_maximumBufferedEvents) {
        ++m_droppedEventCount;
        return;
    }
    m_events.push_back(event);
}

QVector<PicoATE::Core::RuntimeEvent> BufferedRuntimeEventSink::takeAll()
{
    QMutexLocker lock(&m_mutex);
    QVector<PicoATE::Core::RuntimeEvent> events;
    events.swap(m_events);
    return events;
}

void BufferedRuntimeEventSink::clear()
{
    QMutexLocker lock(&m_mutex);
    m_events.clear();
    m_droppedEventCount = 0;
}

quint64 BufferedRuntimeEventSink::droppedEventCount() const
{
    QMutexLocker lock(&m_mutex);
    return m_droppedEventCount;
}

} // namespace PicoATE::Ui
