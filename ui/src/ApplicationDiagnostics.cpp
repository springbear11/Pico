#include "ApplicationDiagnostics.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QMutex>
#include <QMutexLocker>
#include <QStandardPaths>
#include <QThread>

#include <array>
#include <cstdio>

#ifdef Q_OS_WIN
#include <Windows.h>
#include <DbgHelp.h>
#endif

namespace PicoATE::Ui {

namespace {

struct DiagnosticsState {
    QMutex mutex;
    QFile actionsFile;
    QString directory;
    bool installed = false;
#ifdef Q_OS_WIN
    std::array<wchar_t, 32768> nativeDirectory{};
    LPTOP_LEVEL_EXCEPTION_FILTER previousExceptionFilter = nullptr;
#endif
};

DiagnosticsState& diagnosticsState()
{
    // Intentionally retained until process exit so the exception filter never
    // observes a partially destroyed diagnostics object.
    static auto* state = new DiagnosticsState;
    return *state;
}

QString diagnosticsDirectory()
{
    const auto besideApplication = QDir(QCoreApplication::applicationDirPath())
                                       .filePath(QStringLiteral("diagnostics"));
    if (QDir().mkpath(besideApplication)) {
        return QDir(besideApplication).absolutePath();
    }

    const auto writableRoot = QStandardPaths::writableLocation(
        QStandardPaths::AppLocalDataLocation);
    const auto fallback = QDir(writableRoot).filePath(
        QStringLiteral("diagnostics"));
    QDir().mkpath(fallback);
    return QDir(fallback).absolutePath();
}

QByteArray actionLine(const QString& action, const QString& detail)
{
    const auto timestamp = QDateTime::currentDateTime().toString(
        QStringLiteral("yyyy-MM-dd HH:mm:ss.zzz"));
    const auto threadId = quintptr(QThread::currentThreadId());
    auto line = QStringLiteral("[%1] [T%2] %3")
                    .arg(timestamp)
                    .arg(threadId, 0, 16)
                    .arg(action.trimmed());
    if (!detail.trimmed().isEmpty()) {
        line += QStringLiteral(" | ") + detail.trimmed();
    }
    line += QLatin1Char('\n');
    return line.toUtf8();
}

#ifdef Q_OS_WIN

LONG WINAPI writeUnhandledExceptionDump(EXCEPTION_POINTERS* exceptionInfo)
{
    auto& state = diagnosticsState();
    SYSTEMTIME time{};
    GetLocalTime(&time);

    wchar_t dumpPath[32768]{};
    _snwprintf_s(dumpPath, _countof(dumpPath), _TRUNCATE,
                 L"%ls\\PicoATE.UI_%04u%02u%02u_%02u%02u%02u_%03u_%lu.dmp",
                 state.nativeDirectory.data(),
                 time.wYear, time.wMonth, time.wDay,
                 time.wHour, time.wMinute, time.wSecond, time.wMilliseconds,
                 GetCurrentProcessId());

    const HANDLE file = CreateFileW(
        dumpPath, GENERIC_WRITE, FILE_SHARE_READ, nullptr, CREATE_ALWAYS,
        FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file != INVALID_HANDLE_VALUE) {
        MINIDUMP_EXCEPTION_INFORMATION information{};
        information.ThreadId = GetCurrentThreadId();
        information.ExceptionPointers = exceptionInfo;
        information.ClientPointers = FALSE;
        MiniDumpWriteDump(
            GetCurrentProcess(), GetCurrentProcessId(), file,
            static_cast<MINIDUMP_TYPE>(MiniDumpNormal | MiniDumpWithThreadInfo),
            exceptionInfo ? &information : nullptr, nullptr, nullptr);
        CloseHandle(file);
    }

    if (state.previousExceptionFilter &&
        state.previousExceptionFilter != writeUnhandledExceptionDump) {
        return state.previousExceptionFilter(exceptionInfo);
    }
    return EXCEPTION_CONTINUE_SEARCH;
}

#endif

} // namespace

void ApplicationDiagnostics::install()
{
    auto& state = diagnosticsState();
    QMutexLocker locker(&state.mutex);
    if (state.installed) {
        return;
    }

    state.directory = diagnosticsDirectory();
    const auto fileName = QStringLiteral("PicoATE.UI_%1_%2.actions.log")
                              .arg(QDateTime::currentDateTime().toString(
                                       QStringLiteral("yyyyMMdd_HHmmss_zzz")))
                              .arg(QCoreApplication::applicationPid());
    state.actionsFile.setFileName(QDir(state.directory).filePath(fileName));
    state.actionsFile.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text);
    if (state.actionsFile.isOpen()) {
        state.actionsFile.write(actionLine(
            QStringLiteral("APPLICATION_START"),
            QStringLiteral("version=%1")
                .arg(QCoreApplication::applicationVersion())));
        state.actionsFile.flush();
    }

#ifdef Q_OS_WIN
    const auto nativeDirectory = QDir::toNativeSeparators(state.directory)
                                     .toStdWString();
    wcsncpy_s(state.nativeDirectory.data(), state.nativeDirectory.size(),
              nativeDirectory.c_str(), _TRUNCATE);
    state.previousExceptionFilter = SetUnhandledExceptionFilter(
        writeUnhandledExceptionDump);
#endif
    state.installed = true;
}

void ApplicationDiagnostics::recordAction(const QString& action,
                                           const QString& detail)
{
    auto& state = diagnosticsState();
    QMutexLocker locker(&state.mutex);
    if (!state.installed || !state.actionsFile.isOpen()) {
        return;
    }
    state.actionsFile.write(actionLine(action, detail));
}

void ApplicationDiagnostics::recordSlowOperation(const QString& operation,
                                                  qint64 elapsedMs,
                                                  qint64 thresholdMs)
{
    if (elapsedMs < thresholdMs) {
        return;
    }
    recordAction(QStringLiteral("SLOW_OPERATION"),
                 QStringLiteral("%1=%2ms").arg(operation).arg(elapsedMs));
}

QString ApplicationDiagnostics::outputDirectory()
{
    auto& state = diagnosticsState();
    QMutexLocker locker(&state.mutex);
    return state.directory;
}

ScopedOperationTimer::ScopedOperationTimer(QString operation,
                                           qint64 thresholdMs)
    : m_operation(std::move(operation))
    , m_thresholdMs(thresholdMs)
{
    m_timer.start();
}

ScopedOperationTimer::~ScopedOperationTimer()
{
    ApplicationDiagnostics::recordSlowOperation(
        m_operation, m_timer.elapsed(), m_thresholdMs);
}

} // namespace PicoATE::Ui
