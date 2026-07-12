#pragma once

#include "PicoATE/Core/ModuleRuntime.h"

#include <QByteArray>
#include <QString>
#include <QtGlobal>

#include <condition_variable>
#include <cstddef>
#include <mutex>
#include <thread>

class NativeHostDiagnosticCapture
{
public:
    explicit NativeHostDiagnosticCapture(PicoATE::Core::IModuleLogSink* sink,
                                         int maximumLineCharacters = 4096);
    ~NativeHostDiagnosticCapture();

    bool isActive() const;
    QString errorString() const;
    bool flush(int timeoutMs = 1000);

private:
    bool start();
    void restore();
    void readerLoop();
    void processBytes(const QByteArray& bytes);
    void processLine(QByteArray line);
    void publishLine(const QByteArray& line, bool continued = false);

    PicoATE::Core::IModuleLogSink* m_sink = nullptr;
    const int m_maximumLineCharacters;
    QString m_error;
    bool m_active = false;
    quintptr m_pipeRead = 0;
    quintptr m_pipeWrite = 0;
    quintptr m_redirectedStdout = 0;
    quintptr m_redirectedStderr = 0;
    quintptr m_originalStdout = 0;
    quintptr m_originalStderr = 0;
    int m_savedStdoutFd = -1;
    int m_savedStderrFd = -1;
    QByteArray m_pending;
    std::thread m_reader;
    std::mutex m_flushMutex;
    std::condition_variable m_flushCompleted;
    quint64 m_nextFlushId = 1;
    quint64 m_lastCompletedFlushId = 0;
};