#include "NativeHostDiagnosticCapture.h"

#include <QDateTime>
#include <QStringDecoder>

#include <chrono>
#include <cstdio>

#if defined(Q_OS_WIN)
#include <Windows.h>
#include <fcntl.h>
#include <io.h>
#endif

using namespace PicoATE::Core;

namespace {

constexpr auto FlushPrefix = "__PICOATE_VENDOR_CAPTURE_FLUSH_";

#if defined(Q_OS_WIN)
HANDLE handleFrom(quintptr value)
{
    return reinterpret_cast<HANDLE>(value);
}
#endif

} // namespace

NativeHostDiagnosticCapture::NativeHostDiagnosticCapture(IModuleLogSink* sink,
                                                           int maximumLineCharacters)
    : m_sink(sink)
    , m_maximumLineCharacters(qMax(64, maximumLineCharacters))
{
    m_active = start();
}

NativeHostDiagnosticCapture::~NativeHostDiagnosticCapture()
{
    restore();
}

bool NativeHostDiagnosticCapture::isActive() const
{
    return m_active;
}

QString NativeHostDiagnosticCapture::errorString() const
{
    return m_error;
}

bool NativeHostDiagnosticCapture::start()
{
#if !defined(Q_OS_WIN)
    return true;
#else
    m_originalStdout = reinterpret_cast<quintptr>(GetStdHandle(STD_OUTPUT_HANDLE));
    m_originalStderr = reinterpret_cast<quintptr>(GetStdHandle(STD_ERROR_HANDLE));

    SECURITY_ATTRIBUTES attributes{};
    attributes.nLength = sizeof(attributes);
    attributes.bInheritHandle = FALSE;
    HANDLE readHandle = nullptr;
    HANDLE writeHandle = nullptr;
    if (!CreatePipe(&readHandle, &writeHandle, &attributes, 0)) {
        m_error = QString("CreatePipe failed: %1").arg(GetLastError());
        return false;
    }
    m_pipeRead = reinterpret_cast<quintptr>(readHandle);
    m_pipeWrite = reinterpret_cast<quintptr>(writeHandle);

    const auto process = GetCurrentProcess();
    HANDLE redirectedStdout = nullptr;
    HANDLE redirectedStderr = nullptr;
    HANDLE crtWrite = nullptr;
    const auto duplicateWrite = [&](HANDLE* target) {
        return DuplicateHandle(process,
                               writeHandle,
                               process,
                               target,
                               0,
                               FALSE,
                               DUPLICATE_SAME_ACCESS) != FALSE;
    };
    if (!duplicateWrite(&redirectedStdout) ||
        !duplicateWrite(&redirectedStderr) ||
        !duplicateWrite(&crtWrite)) {
        m_error = QString("DuplicateHandle failed: %1").arg(GetLastError());
        if (redirectedStdout) CloseHandle(redirectedStdout);
        if (redirectedStderr) CloseHandle(redirectedStderr);
        if (crtWrite) CloseHandle(crtWrite);
        restore();
        return false;
    }
    m_redirectedStdout = reinterpret_cast<quintptr>(redirectedStdout);
    m_redirectedStderr = reinterpret_cast<quintptr>(redirectedStderr);

    m_savedStdoutFd = _dup(_fileno(stdout));
    m_savedStderrFd = _dup(_fileno(stderr));
    if (m_savedStdoutFd < 0 || m_savedStderrFd < 0) {
        m_error = "Failed to duplicate CRT stdout/stderr";
        CloseHandle(crtWrite);
        restore();
        return false;
    }

    std::fflush(stdout);
    std::fflush(stderr);
    if (!SetStdHandle(STD_OUTPUT_HANDLE, redirectedStdout) ||
        !SetStdHandle(STD_ERROR_HANDLE, redirectedStderr)) {
        m_error = QString("SetStdHandle failed: %1").arg(GetLastError());
        CloseHandle(crtWrite);
        restore();
        return false;
    }

    const auto pipeFd = _open_osfhandle(reinterpret_cast<intptr_t>(crtWrite),
                                        _O_WRONLY | _O_BINARY);
    if (pipeFd < 0 ||
        _dup2(pipeFd, _fileno(stdout)) != 0 ||
        _dup2(pipeFd, _fileno(stderr)) != 0) {
        m_error = "Failed to redirect CRT stdout/stderr";
        if (pipeFd >= 0) {
            _close(pipeFd);
        } else {
            CloseHandle(crtWrite);
        }
        restore();
        return false;
    }
    _close(pipeFd);
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    std::setvbuf(stderr, nullptr, _IONBF, 0);

    m_reader = std::thread([this] { readerLoop(); });
    return true;
#endif
}

void NativeHostDiagnosticCapture::restore()
{
#if defined(Q_OS_WIN)
    std::fflush(stdout);
    std::fflush(stderr);

    if (m_savedStdoutFd >= 0) {
        _dup2(m_savedStdoutFd, _fileno(stdout));
        _close(m_savedStdoutFd);
        m_savedStdoutFd = -1;
    }
    if (m_savedStderrFd >= 0) {
        _dup2(m_savedStderrFd, _fileno(stderr));
        _close(m_savedStderrFd);
        m_savedStderrFd = -1;
    }

    if (m_originalStdout != 0) {
        SetStdHandle(STD_OUTPUT_HANDLE, handleFrom(m_originalStdout));
    }
    if (m_originalStderr != 0) {
        SetStdHandle(STD_ERROR_HANDLE, handleFrom(m_originalStderr));
    }
    if (m_redirectedStdout != 0) {
        CloseHandle(handleFrom(m_redirectedStdout));
        m_redirectedStdout = 0;
    }
    if (m_redirectedStderr != 0) {
        CloseHandle(handleFrom(m_redirectedStderr));
        m_redirectedStderr = 0;
    }
    if (m_pipeWrite != 0) {
        CloseHandle(handleFrom(m_pipeWrite));
        m_pipeWrite = 0;
    }
    if (m_reader.joinable()) {
        m_reader.join();
    }
    if (m_pipeRead != 0) {
        CloseHandle(handleFrom(m_pipeRead));
        m_pipeRead = 0;
    }
#endif
    m_active = false;
}

bool NativeHostDiagnosticCapture::flush(int timeoutMs)
{
#if !defined(Q_OS_WIN)
    Q_UNUSED(timeoutMs);
    return true;
#else
    if (!m_active || m_pipeWrite == 0) {
        return false;
    }
    std::fflush(stdout);
    std::fflush(stderr);

    quint64 flushId = 0;
    {
        std::lock_guard lock(m_flushMutex);
        flushId = m_nextFlushId++;
    }
    const auto marker = QByteArray("\n") + FlushPrefix + QByteArray::number(flushId) + "__\n";
    DWORD written = 0;
    if (!WriteFile(handleFrom(m_pipeWrite),
                   marker.constData(),
                   static_cast<DWORD>(marker.size()),
                   &written,
                   nullptr) || written != static_cast<DWORD>(marker.size())) {
        return false;
    }

    std::unique_lock lock(m_flushMutex);
    return m_flushCompleted.wait_for(
        lock,
        std::chrono::milliseconds(qMax(1, timeoutMs)),
        [&] { return m_lastCompletedFlushId >= flushId; });
#endif
}

void NativeHostDiagnosticCapture::readerLoop()
{
#if defined(Q_OS_WIN)
    QByteArray chunk(4096, '\0');
    while (true) {
        DWORD bytesRead = 0;
        if (!ReadFile(handleFrom(m_pipeRead),
                      chunk.data(),
                      static_cast<DWORD>(chunk.size()),
                      &bytesRead,
                      nullptr) || bytesRead == 0) {
            break;
        }
        processBytes(QByteArray(chunk.constData(), static_cast<qsizetype>(bytesRead)));
    }
    if (!m_pending.isEmpty()) {
        publishLine(m_pending);
        m_pending.clear();
    }
#endif
}

void NativeHostDiagnosticCapture::processBytes(const QByteArray& bytes)
{
    m_pending += bytes;
    while (true) {
        const auto newline = m_pending.indexOf('\n');
        if (newline < 0) {
            break;
        }
        auto line = m_pending.left(newline);
        m_pending.remove(0, newline + 1);
        processLine(std::move(line));
    }

    while (m_pending.size() > m_maximumLineCharacters) {
        const auto part = m_pending.left(m_maximumLineCharacters);
        m_pending.remove(0, m_maximumLineCharacters);
        publishLine(part, true);
    }
}

void NativeHostDiagnosticCapture::processLine(QByteArray line)
{
    if (line.endsWith('\r')) {
        line.chop(1);
    }
    const QByteArray prefix(FlushPrefix);
    if (line.startsWith(prefix) && line.endsWith("__")) {
        bool ok = false;
        const auto value = line.mid(prefix.size(), line.size() - prefix.size() - 2)
                               .toULongLong(&ok);
        if (ok) {
            {
                std::lock_guard lock(m_flushMutex);
                m_lastCompletedFlushId = qMax(m_lastCompletedFlushId, value);
            }
            m_flushCompleted.notify_all();
            return;
        }
    }
    publishLine(line);
}

void NativeHostDiagnosticCapture::publishLine(const QByteArray& line, bool continued)
{
    if (!m_sink || line.isEmpty()) {
        return;
    }
    QStringDecoder utf8(QStringDecoder::Utf8);
    QString message = utf8.decode(line);
    if (utf8.hasError()) {
        message = QString::fromLocal8Bit(line);
    }
    if (message.trimmed().isEmpty()) {
        return;
    }

    ModuleLogRecord record;
    record.timestampUtc = QDateTime::currentDateTimeUtc();
    record.message = QString("[vendor] %1%2")
                         .arg(message,
                              continued ? QString("... [continued]") : QString());
    m_sink->publishModuleLog(record);
}