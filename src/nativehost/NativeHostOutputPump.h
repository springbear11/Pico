#pragma once

#include "PicoATE/Core/ModuleRuntime.h"

#include <QByteArray>
#include <QQueue>

#include <condition_variable>
#include <cstddef>
#include <mutex>
#include <thread>

class NativeHostOutputPump final : public PicoATE::Core::IModuleLogSink {
public:
    explicit NativeHostOutputPump(int maximumQueuedMessages = 1024,
                                  int maximumMessageCharacters = 4096);
    ~NativeHostOutputPump() override;

    void beginRequest(const QString& traceId);
    void publishModuleLog(const PicoATE::Core::ModuleLogRecord& record) override;
    void writeResponse(const PicoATE::Core::ModuleTransportResponse& response);

private:
    struct QueuedLine {
        quint64 ticket = 0;
        QByteArray bytes;
    };

    quint64 enqueueCritical(std::unique_lock<std::mutex>& lock, QByteArray line);
    void writerLoop();

    const std::size_t m_maximumQueuedMessages;
    const int m_maximumMessageCharacters;
    std::mutex m_mutex;
    std::condition_variable m_ready;
    std::condition_variable m_spaceAvailable;
    std::condition_variable m_written;
    QQueue<QueuedLine> m_queue;
    std::thread m_writer;
    bool m_stopping = false;
    QString m_traceId;
    quint64 m_nextSourceSequence = 1;
    quint64 m_nextTicket = 1;
    quint64 m_lastWrittenTicket = 0;
    quint64 m_droppedMessages = 0;
};

