#include "NativeHostOutputPump.h"

#include "PicoATE/Core/ModuleTransportJson.h"

#include <QDateTime>
#include <QJsonDocument>

#include <cstdio>

using namespace PicoATE::Core;

namespace {

QByteArray compactLine(const QJsonObject& object)
{
    return QJsonDocument(object).toJson(QJsonDocument::Compact) + '\n';
}

} // namespace

NativeHostOutputPump::NativeHostOutputPump(int maximumQueuedMessages,
                                           int maximumMessageCharacters)
    : m_maximumQueuedMessages(static_cast<std::size_t>(qMax(1, maximumQueuedMessages)))
    , m_maximumMessageCharacters(qMax(64, maximumMessageCharacters))
    , m_writer([this] { writerLoop(); })
{
}

NativeHostOutputPump::~NativeHostOutputPump()
{
    {
        std::lock_guard lock(m_mutex);
        m_stopping = true;
    }
    m_ready.notify_all();
    if (m_writer.joinable()) {
        m_writer.join();
    }
}

void NativeHostOutputPump::beginRequest(const QString& traceId)
{
    std::lock_guard lock(m_mutex);
    m_traceId = traceId;
    m_nextSourceSequence = 1;
    m_droppedMessages = 0;
}

void NativeHostOutputPump::publishModuleLog(const ModuleLogRecord& source)
{
    if (source.message.isEmpty()) {
        return;
    }

    std::lock_guard lock(m_mutex);
    ModuleLogRecord record = source;
    record.sourceSequence = m_nextSourceSequence++;
    if (!record.timestampUtc.isValid()) {
        record.timestampUtc = QDateTime::currentDateTimeUtc();
    }
    if (record.message.size() > m_maximumMessageCharacters) {
        record.message = record.message.left(m_maximumMessageCharacters) + "... [truncated]";
    }
    if (static_cast<std::size_t>(m_queue.size()) >= m_maximumQueuedMessages) {
        ++m_droppedMessages;
        return;
    }

    m_queue.enqueue({m_nextTicket++, compactLine(moduleLogMessageToJson(m_traceId, record))});
    m_ready.notify_one();
}

void NativeHostOutputPump::writeResponse(const ModuleTransportResponse& response)
{
    std::unique_lock lock(m_mutex);
    if (m_droppedMessages > 0) {
        ModuleLogRecord dropped;
        dropped.sourceSequence = m_nextSourceSequence++;
        dropped.timestampUtc = QDateTime::currentDateTimeUtc();
        dropped.message = QString("PicoATE log queue dropped %1 message(s)")
                              .arg(m_droppedMessages);
        dropped.droppedBefore = m_droppedMessages;
        enqueueCritical(lock, compactLine(moduleLogMessageToJson(m_traceId, dropped)));
    }

    const auto responseTicket = enqueueCritical(
        lock, compactLine(moduleResponseMessageToJson(m_traceId, response)));
    m_written.wait(lock, [&] { return m_lastWrittenTicket >= responseTicket; });
}

quint64 NativeHostOutputPump::enqueueCritical(std::unique_lock<std::mutex>& lock,
                                              QByteArray line)
{
    m_spaceAvailable.wait(lock, [&] {
        return static_cast<std::size_t>(m_queue.size()) < m_maximumQueuedMessages;
    });
    const auto ticket = m_nextTicket++;
    m_queue.enqueue({ticket, std::move(line)});
    m_ready.notify_one();
    return ticket;
}

void NativeHostOutputPump::writerLoop()
{
    while (true) {
        QueuedLine line;
        {
            std::unique_lock lock(m_mutex);
            m_ready.wait(lock, [&] { return m_stopping || !m_queue.isEmpty(); });
            if (m_queue.isEmpty()) {
                if (m_stopping) {
                    return;
                }
                continue;
            }
            line = m_queue.dequeue();
            m_spaceAvailable.notify_all();
        }

        if (!line.bytes.isEmpty()) {
            std::fwrite(line.bytes.constData(), 1, static_cast<std::size_t>(line.bytes.size()), stdout);
            std::fflush(stdout);
        }

        {
            std::lock_guard lock(m_mutex);
            m_lastWrittenTicket = qMax(m_lastWrittenTicket, line.ticket);
        }
        m_written.notify_all();
    }
}

