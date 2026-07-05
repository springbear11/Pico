#include "NativeHostOutputPump.h"

#include "PicoATE/Core/ModuleTransportJson.h"

#include <QDateTime>
#include <QJsonDocument>

#include <cstdio>
#include <utility>

#if defined(Q_OS_WIN)
#include <Windows.h>
#endif

using namespace PicoATE::Core;

namespace {

QByteArray compactLine(const QJsonObject& object)
{
    return QJsonDocument(object).toJson(QJsonDocument::Compact) + '\n';
}

} // namespace

NativeHostOutputPump::NativeHostOutputPump(int maximumBufferedLogs,
                                           int maximumMessageCharacters,
                                           int maximumBatchRecords,
                                           int maximumBatchBytes,
                                           int batchFlushMs)
    : m_maximumBufferedLogs(static_cast<std::size_t>(qMax(1, maximumBufferedLogs)))
    , m_maximumMessageCharacters(qMax(64, maximumMessageCharacters))
    , m_maximumBatchRecords(qMax(1, maximumBatchRecords))
    , m_maximumBatchBytes(qMax(256, maximumBatchBytes))
    , m_batchFlushInterval(qMax(1, batchFlushMs))
{
#if defined(Q_OS_WIN)
    const auto process = GetCurrentProcess();
    const auto standardOutput = GetStdHandle(STD_OUTPUT_HANDLE);
    HANDLE duplicate = nullptr;
    if (standardOutput && standardOutput != INVALID_HANDLE_VALUE &&
        DuplicateHandle(process,
                        standardOutput,
                        process,
                        &duplicate,
                        0,
                        FALSE,
                        DUPLICATE_SAME_ACCESS)) {
        m_protocolHandle = reinterpret_cast<quintptr>(duplicate);
        m_ownsProtocolHandle = true;
    } else {
        m_protocolHandle = reinterpret_cast<quintptr>(standardOutput);
    }
#endif
    m_writer = std::thread([this] { writerLoop(); });
}

NativeHostOutputPump::~NativeHostOutputPump()
{
    {
        std::lock_guard lock(m_mutex);
        sealPendingLogsLocked();
        m_stopping = true;
    }
    m_ready.notify_all();
    if (m_writer.joinable()) {
        m_writer.join();
    }
#if defined(Q_OS_WIN)
    if (m_ownsProtocolHandle && m_protocolHandle != 0) {
        CloseHandle(reinterpret_cast<HANDLE>(m_protocolHandle));
    }
#endif
}

void NativeHostOutputPump::beginRequest(const QString& traceId)
{
    std::lock_guard lock(m_mutex);
    m_traceId = traceId;
    m_acceptingLogs = true;
    m_nextSourceSequence = 1;
    m_droppedMessages = 0;
}

void NativeHostOutputPump::publishModuleLog(const ModuleLogRecord& source)
{
    if (source.message.isEmpty()) {
        return;
    }

    std::lock_guard lock(m_mutex);
    if (!m_acceptingLogs) {
        return;
    }
    ModuleLogRecord record = source;
    record.sourceSequence = m_nextSourceSequence++;
    if (!record.timestampUtc.isValid()) {
        record.timestampUtc = QDateTime::currentDateTimeUtc();
    }
    if (record.message.size() > m_maximumMessageCharacters) {
        record.message = record.message.left(m_maximumMessageCharacters) + "... [truncated]";
    }
    if (m_bufferedLogCount >= m_maximumBufferedLogs) {
        ++m_droppedMessages;
        return;
    }

    appendPendingLogLocked(std::move(record));
    if (m_pendingLogs.size() >= m_maximumBatchRecords ||
        m_pendingEstimatedBytes >= m_maximumBatchBytes) {
        sealPendingLogsLocked();
    }
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
        appendPendingLogLocked(std::move(dropped));
    }

    m_acceptingLogs = false;

    int pendingLogCount = 0;
    auto frame = takePendingLogsLocked(pendingLogCount);
    frame += compactLine(moduleResponseMessageToJson(m_traceId, response));
    const auto responseTicket = enqueueFrameLocked(std::move(frame), pendingLogCount);
    m_ready.notify_one();
    m_written.wait(lock, [&] { return m_lastWrittenTicket >= responseTicket; });
}

void NativeHostOutputPump::appendPendingLogLocked(ModuleLogRecord record)
{
    if (m_pendingLogs.isEmpty()) {
        m_batchDeadline = std::chrono::steady_clock::now() + m_batchFlushInterval;
    }
    m_pendingEstimatedBytes += record.message.toUtf8().size() + 128;
    m_pendingLogs.push_back(std::move(record));
    ++m_bufferedLogCount;
}

void NativeHostOutputPump::sealPendingLogsLocked()
{
    int logCount = 0;
    auto bytes = takePendingLogsLocked(logCount);
    if (!bytes.isEmpty()) {
        enqueueFrameLocked(std::move(bytes), logCount);
    }
}

QByteArray NativeHostOutputPump::takePendingLogsLocked(int& logCount)
{
    logCount = m_pendingLogs.size();
    if (m_pendingLogs.isEmpty()) {
        return {};
    }

    auto records = std::move(m_pendingLogs);
    m_pendingLogs.clear();
    m_pendingEstimatedBytes = 0;
    return compactLine(moduleLogBatchMessageToJson(m_traceId, records));
}

quint64 NativeHostOutputPump::enqueueFrameLocked(QByteArray bytes, int logCount)
{
    const auto ticket = m_nextTicket++;
    m_queue.enqueue({ticket, std::move(bytes), logCount});
    return ticket;
}

void NativeHostOutputPump::writerLoop()
{
    while (true) {
        QueuedFrame frame;
        {
            std::unique_lock lock(m_mutex);
            while (m_queue.isEmpty()) {
                if (m_stopping) {
                    return;
                }
                if (m_pendingLogs.isEmpty()) {
                    m_ready.wait(lock, [&] {
                        return m_stopping || !m_queue.isEmpty() || !m_pendingLogs.isEmpty();
                    });
                    continue;
                }

                if (!m_ready.wait_until(lock, m_batchDeadline, [&] {
                        return m_stopping || !m_queue.isEmpty();
                    })) {
                    sealPendingLogsLocked();
                }
            }

            frame = m_queue.dequeue();
            const auto frameLogCount = static_cast<std::size_t>(qMax(0, frame.logCount));
            m_bufferedLogCount = frameLogCount > m_bufferedLogCount
                ? 0
                : m_bufferedLogCount - frameLogCount;
            m_spaceAvailable.notify_all();
        }

        writeProtocolFrame(frame.bytes);

        {
            std::lock_guard lock(m_mutex);
            m_lastWrittenTicket = qMax(m_lastWrittenTicket, frame.ticket);
        }
        m_written.notify_all();
    }
}

void NativeHostOutputPump::writeProtocolFrame(const QByteArray& bytes)
{
    if (bytes.isEmpty()) {
        return;
    }
#if defined(Q_OS_WIN)
    const auto handle = reinterpret_cast<HANDLE>(m_protocolHandle);
    if (!handle || handle == INVALID_HANDLE_VALUE) {
        return;
    }
    qsizetype offset = 0;
    while (offset < bytes.size()) {
        DWORD written = 0;
        const auto remaining = static_cast<DWORD>(
            qMin<qsizetype>(bytes.size() - offset, static_cast<qsizetype>(MAXDWORD)));
        if (!WriteFile(handle,
                       bytes.constData() + offset,
                       remaining,
                       &written,
                       nullptr) || written == 0) {
            return;
        }
        offset += static_cast<qsizetype>(written);
    }
#else
    std::fwrite(bytes.constData(), 1, static_cast<std::size_t>(bytes.size()), stdout);
    std::fflush(stdout);
#endif
}