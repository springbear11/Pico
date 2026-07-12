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
    shutdown();
}

void ExecutionViewModel::shutdown()
{
    if (m_shuttingDown) {
        return;
    }
    m_shuttingDown = true;
    if (m_eventFlushTimer) {
        m_eventFlushTimer->stop();
    }
    if (m_stopToken) {
        m_stopToken->requestStop(PicoATE::Core::StopMode::Abort);
    }
    if (m_executionControl) {
        m_executionControl->resume();
    }
    if (m_worker) {
        disconnect(m_worker, nullptr, this, nullptr);
    }
    if (m_eventSink) {
        m_eventSink->clear();
    }
    m_workerThread.quit();
    m_workerThread.wait();
    m_worker = nullptr;
    m_stopToken.reset();
    m_executionControl.reset();
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

std::optional<PicoATE::Core::ExecutionDebugSnapshot> ExecutionViewModel::debugSnapshot() const
{
    return m_debugSnapshot;
}

QVector<PicoATE::Core::BreakpointSpec> ExecutionViewModel::breakpoints() const
{
    return m_breakpoints;
}

DeviceConnectionTestResult ExecutionViewModel::deviceConnectionTestResult() const
{
    return m_deviceConnectionTestResult;
}

bool ExecutionViewModel::canChangeSources() const
{
    return !m_shuttingDown &&
           m_state != UiRunState::Starting &&
           m_state != UiRunState::Running &&
           m_state != UiRunState::Pausing &&
           m_state != UiRunState::Paused &&
           m_state != UiRunState::TestingDevice &&
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
    return (m_state == UiRunState::Starting ||
            m_state == UiRunState::Running ||
            m_state == UiRunState::Pausing ||
            m_state == UiRunState::Paused ||
            m_state == UiRunState::TestingDevice) &&
           m_stopToken != nullptr;
}

bool ExecutionViewModel::canPause() const
{
    return m_state == UiRunState::Running && m_executionControl != nullptr;
}

bool ExecutionViewModel::canResume() const
{
    return (m_state == UiRunState::Pausing || m_state == UiRunState::Paused) &&
           m_executionControl != nullptr;
}

bool ExecutionViewModel::canStepInto() const
{
    return m_state == UiRunState::Paused && m_executionControl != nullptr;
}

bool ExecutionViewModel::canStepOver() const
{
    return m_state == UiRunState::Paused && m_executionControl != nullptr;
}

bool ExecutionViewModel::canTestDeviceConnection() const
{
    return canChangeSources() &&
           m_state != UiRunState::Compiling &&
           !m_sequencePath.isEmpty() &&
           !m_stationPath.isEmpty();
}

void ExecutionViewModel::setSequencePath(const QString& path)
{
    if (!canChangeSources()) {
        return;
    }
    const auto normalized = normalizedPath(path);
    if (m_sequencePath == normalized && m_sequenceJson.isEmpty()) {
        return;
    }

    m_sequencePath = normalized;
    m_sequenceJson.clear();
    emit sequencePathChanged(m_sequencePath);
    invalidateCompilation();
}
void ExecutionViewModel::setSequenceDocument(const QString& path, const QByteArray& jsonSnapshot)
{
    if (!canChangeSources()) {
        return;
    }
    const auto normalized = normalizedPath(path);
    if (m_sequencePath == normalized && m_sequenceJson == jsonSnapshot) {
        return;
    }

    const bool pathChanged = m_sequencePath != normalized;
    m_sequencePath = normalized;
    m_sequenceJson = jsonSnapshot;
    if (pathChanged) {
        emit sequencePathChanged(m_sequencePath);
    }
    invalidateCompilation();
}

void ExecutionViewModel::setStationPath(const QString& path)
{
    if (!canChangeSources()) {
        return;
    }
    const auto normalized = normalizedPath(path);
    if (m_stationPath == normalized && m_stationJson.isEmpty()) {
        return;
    }

    m_stationPath = normalized;
    m_stationJson.clear();
    emit stationPathChanged(m_stationPath);
    invalidateCompilation();
}

void ExecutionViewModel::setStationDocument(const QString& path,
                                            const QByteArray& jsonSnapshot)
{
    if (!canChangeSources()) {
        return;
    }
    const auto normalized = normalizedPath(path);
    if (m_stationPath == normalized && m_stationJson == jsonSnapshot) {
        return;
    }

    const bool pathChanged = m_stationPath != normalized;
    m_stationPath = normalized;
    m_stationJson = jsonSnapshot;
    if (pathChanged) {
        emit stationPathChanged(m_stationPath);
    }
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
    clearDebugSnapshot();
    emit diagnosticsChanged();
    emit reportChanged();

    CompileRequest request;
    request.requestId = ++m_compileRequestId;
    request.sequencePath = m_sequencePath;
    request.sequenceJson = m_sequenceJson;
    request.stationPath = m_stationPath;
    request.stationJson = m_stationJson;
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
    RunRequest request;
    request.uutCount = uutCount;
    request.uutPrefix = uutPrefix;
    startRun(std::move(request));
}

void ExecutionViewModel::runUut(const QString& uutId, const QVariantMap& variables)
{
    RunRequest request;
    RunRequest::UutInput input;
    input.uutId = uutId.trimmed();
    input.variables = variables;
    request.uuts.push_back(std::move(input));
    startRun(std::move(request));
}

void ExecutionViewModel::startRun(RunRequest request)
{
    if (!canRun()) {
        return;
    }

    request.requestId = ++m_runRequestId;
    m_stopToken = std::make_shared<PicoATE::Core::StopToken>();
    m_executionControl = std::make_shared<PicoATE::Core::ExecutionControl>();
    m_executionControl->setBreakpoints(m_breakpoints);
    m_eventSink->clear();
    m_eventFlushTimer->start();
    m_diagnostics.clear();
    m_report = {};
    clearDebugSnapshot();
    emit diagnosticsChanged();
    emit reportChanged();
    setState(UiRunState::Starting);

    QPointer<ExecutionWorker> worker(m_worker);
    const auto stopToken = m_stopToken;
    const auto executionControl = m_executionControl;
    const std::shared_ptr<PicoATE::Core::IRuntimeEventSink> eventSink = m_eventSink;
    QMetaObject::invokeMethod(
        m_worker,
        [worker, request, stopToken, eventSink, executionControl] {
            if (worker) {
                worker->run(request, stopToken, eventSink, executionControl);
            }
        },
        Qt::QueuedConnection);
}

void ExecutionViewModel::pause()
{
    if (!canPause() || !m_executionControl->requestPause()) {
        return;
    }
    setState(UiRunState::Pausing);
}

void ExecutionViewModel::resume()
{
    if (!canResume()) {
        return;
    }
    m_executionControl->resume();
    clearDebugSnapshot();
    setState(UiRunState::Running);
}

void ExecutionViewModel::stepInto()
{
    if (!canStepInto() || !m_executionControl->stepInto()) {
        return;
    }
    clearDebugSnapshot();
    setState(UiRunState::Running);
}

void ExecutionViewModel::stepOver()
{
    if (!canStepOver() || !m_executionControl->stepOver()) {
        return;
    }
    clearDebugSnapshot();
    setState(UiRunState::Running);
}

void ExecutionViewModel::stop(PicoATE::Core::StopMode mode)
{
    if (!canStop()) {
        return;
    }

    m_stopToken->requestStop(mode);
    if (m_executionControl) {
        m_executionControl->resume();
    }
    clearDebugSnapshot();
    setState(UiRunState::Stopping);
}

void ExecutionViewModel::setBreakpoints(
    QVector<PicoATE::Core::BreakpointSpec> breakpoints)
{
    m_breakpoints = std::move(breakpoints);
    if (m_executionControl) {
        m_executionControl->setBreakpoints(m_breakpoints);
    }
}

void ExecutionViewModel::testDeviceConnection(const QString& deviceId, int timeoutMs)
{
    if (!canTestDeviceConnection() || deviceId.trimmed().isEmpty()) {
        return;
    }

    DeviceConnectionTestRequest request;
    request.requestId = ++m_deviceConnectionTestRequestId;
    request.sequencePath = m_sequencePath;
    request.sequenceJson = m_sequenceJson;
    request.stationPath = m_stationPath;
    request.stationJson = m_stationJson;
    request.deviceId = deviceId.trimmed();
    request.timeoutMs = qBound(100, timeoutMs, 60000);
    m_stopToken = std::make_shared<PicoATE::Core::StopToken>();
    m_stateBeforeDeviceTest = m_state;
    setState(UiRunState::TestingDevice);

    QPointer<ExecutionWorker> worker(m_worker);
    const auto stopToken = m_stopToken;
    QMetaObject::invokeMethod(
        m_worker,
        [worker, request, stopToken] {
            if (worker) {
                worker->testDeviceConnection(request, stopToken);
            }
        },
        Qt::QueuedConnection);
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
    m_executionControl.reset();
    m_diagnostics = result.diagnostics;
    m_report = result.report;
    clearDebugSnapshot();
    emit diagnosticsChanged();
    emit reportChanged();

    const bool aborted = result.report.state == PicoATE::Core::ExecutionState::Aborted;
    const bool failed = !result.executed || aborted || result.report.hasError;
    setState(failed ? UiRunState::Failed : UiRunState::Completed);
}

void ExecutionViewModel::handleDeviceConnectionTestStarted(
    quint64 requestId,
    const QString& deviceId)
{
    if (requestId != m_deviceConnectionTestRequestId) {
        return;
    }
    emit deviceConnectionTestStarted(deviceId);
}

void ExecutionViewModel::handleDeviceConnectionTestFinished(
    const DeviceConnectionTestResult& result)
{
    if (result.requestId != m_deviceConnectionTestRequestId) {
        return;
    }
    m_stopToken.reset();
    m_deviceConnectionTestResult = result;
    setState(m_stateBeforeDeviceTest);
    emit deviceConnectionTestFinished(result);
}

void ExecutionViewModel::initialize(std::unique_ptr<IExecutionService> service)
{
    qRegisterMetaType<UiRunState>();
    qRegisterMetaType<CompileServiceResult>();
    qRegisterMetaType<RunServiceResult>();
    qRegisterMetaType<DeviceConnectionTestResult>();

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
    connect(m_worker,
            &ExecutionWorker::deviceConnectionTestStarted,
            this,
            &ExecutionViewModel::handleDeviceConnectionTestStarted);
    connect(m_worker,
            &ExecutionWorker::deviceConnectionTestFinished,
            this,
            &ExecutionViewModel::handleDeviceConnectionTestFinished);
    m_workerThread.setObjectName(QStringLiteral("PicoATE.ExecutionWorker"));
    m_workerThread.start();
}

void ExecutionViewModel::flushRuntimeEvents()
{
    if (m_shuttingDown || !m_eventSink) {
        return;
    }
    auto events = m_eventSink->takeAll();
    if (!events.isEmpty()) {
        enum class DebugSnapshotAction {
            None,
            Capture,
            Clear
        };
        auto debugSnapshotAction = DebugSnapshotAction::None;
        for (const auto& event : events) {
            if (event.kind == PicoATE::Core::RuntimeEventKind::BreakpointHit ||
                event.kind == PicoATE::Core::RuntimeEventKind::DebugStepCompleted) {
                debugSnapshotAction = DebugSnapshotAction::Capture;
            }

            if (event.kind != PicoATE::Core::RuntimeEventKind::SessionStateChanged ||
                m_state == UiRunState::Stopping) {
                continue;
            }
            if (event.executionState == PicoATE::Core::ExecutionState::Paused) {
                setState(UiRunState::Paused);
                debugSnapshotAction = DebugSnapshotAction::Capture;
            } else if (event.executionState == PicoATE::Core::ExecutionState::Running) {
                if (m_state == UiRunState::Pausing || m_state == UiRunState::Paused) {
                    setState(UiRunState::Running);
                }
                debugSnapshotAction = DebugSnapshotAction::Clear;
            }
        }
        if (debugSnapshotAction == DebugSnapshotAction::Capture && m_executionControl) {
            setDebugSnapshot(m_executionControl->debugSnapshot());
        } else if (debugSnapshotAction == DebugSnapshotAction::Clear) {
            clearDebugSnapshot();
        }
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
    clearDebugSnapshot();
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

void ExecutionViewModel::setDebugSnapshot(
    std::optional<PicoATE::Core::ExecutionDebugSnapshot> snapshot)
{
    const bool hadSnapshot = m_debugSnapshot.has_value();
    const bool hasSnapshot = snapshot.has_value();
    m_debugSnapshot = std::move(snapshot);
    if (hadSnapshot || hasSnapshot) {
        emit debugSnapshotChanged();
    }
}

void ExecutionViewModel::clearDebugSnapshot()
{
    setDebugSnapshot(std::nullopt);
}

QString ExecutionViewModel::normalizedPath(const QString& path)
{
    return path.trimmed().isEmpty() ? QString() : QFileInfo(path).absoluteFilePath();
}

} // namespace PicoATE::Ui
