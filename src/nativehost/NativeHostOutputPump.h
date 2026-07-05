#pragma once

#include "PicoATE/Core/ModuleRuntime.h"

#include <QByteArray>
#include <QQueue>
#include <QVector>
#include <QtGlobal>

#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <mutex>
#include <thread>

class NativeHostOutputPump final : public PicoATE::Core::IModuleLogSink {
public:
    explicit NativeHostOutputPump(int maximumBufferedLogs = 1024,
                                  int maximumMessageCharacters = 4096,
                                  int maximumBatchRecords = 64,
                                  int maximumBatchBytes = 16384,
                                  int batchFlushMs = 20);
    ~NativeHostOutputPump() override;

    void beginRequest(const QString& traceId);
    void publishModuleLog(const PicoATE::Core::ModuleLogRecord& record) override;
    void writeResponse(const PicoATE::Core::ModuleTransportResponse& response);

private:
    struct QueuedFrame {
        quint64 ticket = 0;
        QByteArray bytes;
        int logCount = 0;
    };

    void appendPendingLogLocked(PicoATE::Core::ModuleLogRecord record);
    void sealPendingLogsLocked();
    QByteArray takePendingLogsLocked(int& logCount);
    quint64 enqueueFrameLocked(QByteArray bytes, int logCount = 0);
    void writeProtocolFrame(const QByteArray& bytes);
    void writerLoop();

    const std::size_t m_maximumBufferedLogs;
    const int m_maximumMessageCharacters;
    const int m_maximumBatchRecords;
    const int m_maximumBatchBytes;
    const std::chrono::milliseconds m_batchFlushInterval;
    std::mutex m_mutex;
    std::condition_variable m_ready;
    std::condition_variable m_spaceAvailable;
    std::condition_variable m_written;
    QQueue<QueuedFrame> m_queue;
    QVector<PicoATE::Core::ModuleLogRecord> m_pendingLogs;
    qsizetype m_pendingEstimatedBytes = 0;
    std::size_t m_bufferedLogCount = 0;
    std::chrono::steady_clock::time_point m_batchDeadline;
    std::thread m_writer;
    quintptr m_protocolHandle = 0;
    bool m_ownsProtocolHandle = false;
    bool m_stopping = false;
    bool m_acceptingLogs = false;
    QString m_traceId;
    quint64 m_nextSourceSequence = 1;
    quint64 m_nextTicket = 1;
    quint64 m_lastWrittenTicket = 0;
    quint64 m_droppedMessages = 0;
};