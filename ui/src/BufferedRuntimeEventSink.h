#pragma once

#include "PicoATE/Core/RuntimeEvent.h"

#include <QMutex>
#include <QVector>

namespace PicoATE::Ui {

class BufferedRuntimeEventSink final : public PicoATE::Core::IRuntimeEventSink
{
public:
    explicit BufferedRuntimeEventSink(int maximumBufferedEvents = 20000);

    void publish(const PicoATE::Core::RuntimeEvent& event) override;
    QVector<PicoATE::Core::RuntimeEvent> takeAll();
    void clear();
    quint64 droppedEventCount() const;

private:
    const int m_maximumBufferedEvents;
    mutable QMutex m_mutex;
    QVector<PicoATE::Core::RuntimeEvent> m_events;
    quint64 m_droppedEventCount = 0;
};

} // namespace PicoATE::Ui
