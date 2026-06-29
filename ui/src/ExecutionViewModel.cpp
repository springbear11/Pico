#include "ExecutionViewModel.h"

#include "CoreExecutionService.h"
#include "BufferedRuntimeEventSink.h"
#include "ExecutionWorker.h"

#include <QFileInfo>
#include <QMetaObject>
#include <QPointer>

#include <utility>

namespace PicoATE::Ui {

ExecutionViewModel::ExecutionViewModel(QObject* parent)
    : QObject(parent)
{
    initialize(std::make_unique<CoreExecutionService>());
}

ExecutionViewModel::ExecutionViewModel(std::unique_ptr<IExecutionService> service,
                                       QObject* parent)
    : QObject(parent)
{
    initialize(std::move(service));
}

ExecutionViewModel::~ExecutionViewModel()
{
    if (m_stopToken) {
        m_stopToken->requestStop(PicoATE::Core::StopMode::Abort);
    }
    m_workerThread.quit();
    m_workerThread.wait();
}

UiRunState ExecutionViewModel::state() const
{
    return m_state;
}

QString ExecutionViewModel::sequencePath() const
{
    return m_sequencePath;
}

QString ExecutionViewModel::stationPath() const
{
    return m_stationPath;
}

QVector<UiDiagnostic> ExecutionViewModel::diagnostics() const
{
    return m_diagnostics;
}

CompileServiceResult ExecutionViewModel::compileSummary() const
{
    return m_compileSummary;
}

PicoATE::Core::ExecutionReport ExecutionViewModel::report() const
{
    return m_report;
}

bool ExecutionViewModel::canChangeSources() const
{
    return m_state != UiRunState::Starting &&
           m_state != UiRunState::Running &&
           m_state != UiRunState::Stopping;
}

bool ExecutionViewModel::canCompile() const
{
    return canChangeSources() && !m_sequencePath.isEmpty() && m_state != UiRunState::Compiling;
}

bool ExecutionViewModel::canRun() const
{
    return m_hasCompiledArtifact &&
           (m_state == UiRunState::Ready ||
            m_state == UiRunState::Completed ||
            m_state == UiRunState::Failed);
}

bool ExecutionViewModel::canStop() const
{
    return (m_state == UiRunState::Starting || m_state == UiRunState::Running) &&
           m_stopToken != nullptr;
}

void ExecutionViewModel::setSequencePath(const QString& path)
{
    if (!canChangeSources()) {
        return;
    }
    const auto normalized = normalizedPath(path);
    if (m_sequencePath == normalized) {
        return;
    }

    m_sequencePath = normalized;
    emit sequencePathChanged(m_sequencePath);
    invalidateCompilation();
}

void ExecutionViewModel::setStationPath(const QString& path)
{
    if (!canChangeSources()) {
        return;
    }
    const auto normalized = normalizedPath(path);
    if (m_stationPath == normalized) {
        return;
    }

    m_stationPath = normalized;
    emit stationPathChanged(m_stationPath);
    invalidateCompilation();
}

void ExecutionViewModel::compile()
{
    if (!canCompile()) {
        return;
    }

    m_hasCompiledArtifact = false;
    m_diagnostics.clear();
    m_report = {};
    emit diagnosticsChanged();
    emit reportChanged();

    CompileRequest request;
    request.requestId = ++m_compileRequestId;
    request.sequencePath = m_sequencePath;
    request.stationPath = m_stationPath;
    setState(UiRunState::Compiling);

    QPointer<ExecutionWorker> worker(m_worker);
    QMetaObject::invokeMethod(
        m_worker,
        [worker, request] {
            if (worker) {
                worker->compile(request);
            }
        },
        Qt::QueuedConnection);
}

void ExecutionViewModel::run(int uutCount, const QString& uutPrefix)
{
    if (!canRun()) {
        return;
    }

    RunRequest request;
    request.requestId = ++m_runRequestId;
    request.uutCount = uutCount;
    request.uutPrefix = uutPrefix;
    m_stopToken = std::make_shared<PicoATE::Core::StopToken>();
    m_eventSink->clear();
    m_eventFlushTimer->start();
    m_diagnostics.clear();
    m_report = {};
    emit diagnosticsChanged();
    emit reportChanged();
    setState(UiRunState::Starting);

    QPointer<ExecutionWorker> worker(m_worker);
    const auto stopToken = m_stopToken;
    const std::shared_ptr<PicoATE::Core::IRuntimeEventSink> eventSink = m_eventSink;
    QMetaObject::invokeMethod(
        m_worker,
        [worker, request, stopToken, eventSink] {
            if (worker) {
                worker->run(request, stopToken, eventSink);
            }
        },
        Qt::QueuedConnection);
}

void ExecutionViewModel::stop(PicoATE::Core::StopMode mode)
{
    if (!canStop()) {
        return;
    }

    m_stopToken->requestStop(mode);
    setState(UiRunState::Stopping);
}

void ExecutionViewModel::handleCompileFinished(const CompileServiceResult& result)
{
    if (result.requestId != m_compileRequestId || m_state != UiRunState::Compiling) {
        return;
    }

    m_compileSummary = result;
    m_diagnostics = result.diagnostics;
    m_hasCompiledArtifact = result.success;
    emit compileSummaryChanged();
    emit diagnosticsChanged();
    setState(result.success ? UiRunState::Ready : UiRunState::CompileFailed);
}

void ExecutionViewModel::handleRunStarted(quint64 requestId)
{
    if (requestId != m_runRequestId || m_state == UiRunState::Stopping) {
        return;
    }
    setState(UiRunState::Running);
}

void ExecutionViewModel::handleRunFinished(const RunServiceResult& result)
{
    if (result.requestId != m_runRequestId) {
        return;
    }

    flushRuntimeEvents();
    m_eventFlushTimer->stop();
    m_stopToken.reset();
    m_diagnostics = result.diagnostics;
    m_report = result.report;
    emit diagnosticsChanged();
    emit reportChanged();

    const bool aborted = result.report.state == PicoATE::Core::ExecutionState::Aborted;
    const bool failed = !result.executed || aborted || result.report.hasError;
    setState(failed ? UiRunState::Failed : UiRunState::Completed);
}

void ExecutionViewModel::initialize(std::unique_ptr<IExecutionService> service)
{
    qRegisterMetaType<UiRunState>();
    qRegisterMetaType<CompileServiceResult>();
    qRegisterMetaType<RunServiceResult>();

    m_eventSink = std::make_shared<BufferedRuntimeEventSink>();
    m_eventFlushTimer = new QTimer(this);
    m_eventFlushTimer->setInterval(50);
    m_eventFlushTimer->setTimerType(Qt::CoarseTimer);
    connect(m_eventFlushTimer,
            &QTimer::timeout,
            this,
            &ExecutionViewModel::flushRuntimeEvents);

    m_worker = new ExecutionWorker(std::move(service));
    m_worker->moveToThread(&m_workerThread);
    connect(&m_workerThread, &QThread::finished, m_worker, &QObject::deleteLater);
    connect(m_worker,
            &ExecutionWorker::compileFinished,
            this,
            &ExecutionViewModel::handleCompileFinished);
    connect(m_worker,
            &ExecutionWorker::runStarted,
            this,
            &ExecutionViewModel::handleRunStarted);
    connect(m_worker,
            &ExecutionWorker::runFinished,
            this,
            &ExecutionViewModel::handleRunFinished);
    m_workerThread.setObjectName(QStringLiteral("PicoATE.ExecutionWorker"));
    m_workerThread.start();
}

void ExecutionViewModel::flushRuntimeEvents()
{
    if (!m_eventSink) {
        return;
    }
    auto events = m_eventSink->takeAll();
    if (!events.isEmpty()) {
        emit runtimeEventsReady(events);
    }
}

void ExecutionViewModel::invalidateCompilation()
{
    ++m_compileRequestId;
    m_hasCompiledArtifact = false;
    m_compileSummary = {};
    m_diagnostics.clear();
    m_report = {};
    emit compileSummaryChanged();
    emit diagnosticsChanged();
    emit reportChanged();
    setState(m_sequencePath.isEmpty() ? UiRunState::Empty : UiRunState::SourceSelected);
}

void ExecutionViewModel::setState(UiRunState state)
{
    if (m_state == state) {
        emit commandAvailabilityChanged();
        return;
    }
    m_state = state;
    emit stateChanged(m_state);
    emit commandAvailabilityChanged();
}

QString ExecutionViewModel::normalizedPath(const QString& path)
{
    return path.trimmed().isEmpty() ? QString() : QFileInfo(path).absoluteFilePath();
}

} // namespace PicoATE::Ui
