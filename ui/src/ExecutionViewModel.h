#pragma once

#include "IExecutionService.h"
#include "UiExecutionTypes.h"

#include <QObject>
#include <QThread>
#include <QTimer>

#include <memory>

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

    bool canChangeSources() const;
    bool canCompile() const;
    bool canRun() const;
    bool canStop() const;

public slots:
    void setSequencePath(const QString& path);
    void setStationPath(const QString& path);
    void compile();
    void run(int uutCount = 1, const QString& uutPrefix = QStringLiteral("UUT"));
    void stop(PicoATE::Core::StopMode mode = PicoATE::Core::StopMode::Graceful);

signals:
    void stateChanged(PicoATE::Ui::UiRunState state);
    void sequencePathChanged(const QString& path);
    void stationPathChanged(const QString& path);
    void diagnosticsChanged();
    void compileSummaryChanged();
    void reportChanged();
    void runtimeEventsReady(const QVector<PicoATE::Core::RuntimeEvent>& events);
    void commandAvailabilityChanged();

private slots:
    void handleCompileFinished(const PicoATE::Ui::CompileServiceResult& result);
    void handleRunStarted(quint64 requestId);
    void handleRunFinished(const PicoATE::Ui::RunServiceResult& result);
    void flushRuntimeEvents();

private:
    void initialize(std::unique_ptr<IExecutionService> service);
    void invalidateCompilation();
    void setState(UiRunState state);
    static QString normalizedPath(const QString& path);

    QThread m_workerThread;
    ExecutionWorker* m_worker = nullptr;
    UiRunState m_state = UiRunState::Empty;
    QString m_sequencePath;
    QString m_stationPath;
    QVector<UiDiagnostic> m_diagnostics;
    CompileServiceResult m_compileSummary;
    PicoATE::Core::ExecutionReport m_report;
    std::shared_ptr<PicoATE::Core::StopToken> m_stopToken;
    std::shared_ptr<BufferedRuntimeEventSink> m_eventSink;
    QTimer* m_eventFlushTimer = nullptr;
    quint64 m_compileRequestId = 0;
    quint64 m_runRequestId = 0;
    bool m_hasCompiledArtifact = false;
};

} // namespace PicoATE::Ui
