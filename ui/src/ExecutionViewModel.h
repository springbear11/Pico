#pragma once

#include "IExecutionService.h"
#include "UiExecutionTypes.h"
#include "PicoATE/Core/ExecutionDebug.h"
#include "PicoATE/Core/OperatorPrompt.h"

#include <QObject>
#include <QThread>
#include <QTimer>

#include <memory>
#include <optional>

namespace PicoATE::Ui {

class ExecutionWorker;
class BufferedRuntimeEventSink;

class ExecutionViewModel final : public QObject
{
    Q_OBJECT

public:
    explicit ExecutionViewModel(QObject* parent = nullptr);
    explicit ExecutionViewModel(std::unique_ptr<IExecutionService> service,
                                QObject* parent = nullptr);
    ~ExecutionViewModel() override;

    UiRunState state() const;
    QString sequencePath() const;
    QString stationPath() const;
    QVector<UiDiagnostic> diagnostics() const;
    CompileServiceResult compileSummary() const;
    PicoATE::Core::ExecutionReport report() const;
    std::optional<PicoATE::Core::ExecutionDebugSnapshot> debugSnapshot() const;
    QVector<PicoATE::Core::BreakpointSpec> breakpoints() const;
    DeviceConnectionTestResult deviceConnectionTestResult() const;

    bool canChangeSources() const;
    bool canCompile() const;
    bool canRun() const;
    bool canPause() const;
    bool canResume() const;
    bool canStepInto() const;
    bool canStepOver() const;
    bool canStop() const;
    bool canTestDeviceConnection() const;
    void shutdown();

public slots:
    void setSequencePath(const QString& path);
    void setSequenceDocument(const QString& path, const QByteArray& jsonSnapshot);
    void invalidateSequenceDocument();
    void setStationPath(const QString& path);
    void setStationDocument(const QString& path, const QByteArray& jsonSnapshot);
    void compile();
    void run(int uutCount = 1, const QString& uutPrefix = QStringLiteral("UUT"));
    void runUut(const QString& uutId, const QVariantMap& variables = {});
    void pause();
    void resume();
    void stepInto();
    void stepOver();
    void stop(PicoATE::Core::StopMode mode = PicoATE::Core::StopMode::Graceful);
    void setBreakpoints(QVector<PicoATE::Core::BreakpointSpec> breakpoints);
    bool respondToOperatorPrompt(const QString& instanceId,
                                 PicoATE::Core::OperatorPromptResponse response);
    void testDeviceConnection(const QString& deviceId, int timeoutMs = 5000);

signals:
    void stateChanged(PicoATE::Ui::UiRunState state);
    void sequencePathChanged(const QString& path);
    void stationPathChanged(const QString& path);
    void diagnosticsChanged();
    void compileSummaryChanged();
    void reportChanged();
    void runIterationStarted(int iteration, int totalIterations);
    void debugSnapshotChanged();
    void runtimeEventsReady(const QVector<PicoATE::Core::RuntimeEvent>& events);
    void commandAvailabilityChanged();
    void deviceConnectionTestStarted(const QString& deviceId);
    void deviceConnectionTestFinished(
        const PicoATE::Ui::DeviceConnectionTestResult& result);

private slots:
    void handleCompileFinished(const PicoATE::Ui::CompileServiceResult& result);
    void handleRunStarted(quint64 requestId);
    void handleRunFinished(const PicoATE::Ui::RunServiceResult& result);
    void handleDeviceConnectionTestStarted(quint64 requestId,
                                           const QString& deviceId);
    void handleDeviceConnectionTestFinished(
        const PicoATE::Ui::DeviceConnectionTestResult& result);
    void flushRuntimeEvents();

private:
    void initialize(std::unique_ptr<IExecutionService> service);
    void invalidateCompilation();
    void startRun(RunRequest request);
    void startNextRunIteration();
    void setState(UiRunState state);
    void setDebugSnapshot(std::optional<PicoATE::Core::ExecutionDebugSnapshot> snapshot);
    void clearDebugSnapshot();
    static QString normalizedPath(const QString& path);

    QThread m_workerThread;
    ExecutionWorker* m_worker = nullptr;
    UiRunState m_state = UiRunState::Empty;
    QString m_sequencePath;
    QByteArray m_sequenceJson;
    QString m_stationPath;
    QByteArray m_stationJson;
    QVector<UiDiagnostic> m_diagnostics;
    CompileServiceResult m_compileSummary;
    PicoATE::Core::ExecutionReport m_report;
    std::optional<PicoATE::Core::ExecutionDebugSnapshot> m_debugSnapshot;
    QVector<PicoATE::Core::BreakpointSpec> m_breakpoints;
    DeviceConnectionTestResult m_deviceConnectionTestResult;
    std::shared_ptr<PicoATE::Core::StopToken> m_stopToken;
    std::shared_ptr<PicoATE::Core::ExecutionControl> m_executionControl;
    std::shared_ptr<BufferedRuntimeEventSink> m_eventSink;
    QTimer* m_eventFlushTimer = nullptr;
    quint64 m_compileRequestId = 0;
    quint64 m_runRequestId = 0;
    quint64 m_deviceConnectionTestRequestId = 0;
    bool m_hasCompiledArtifact = false;
    bool m_shuttingDown = false;
    RunRequest m_activeRunRequest;
    int m_runIteration = 0;
    int m_runIterationCount = 1;
    UiRunState m_stateBeforeDeviceTest = UiRunState::SourceSelected;
};

} // namespace PicoATE::Ui
