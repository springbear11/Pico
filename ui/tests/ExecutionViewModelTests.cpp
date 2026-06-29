#include <QtTest/QtTest>

#include "ExecutionViewModel.h"
#include "BufferedRuntimeEventSink.h"
#include "CoreExecutionService.h"
#include "ReportExporter.h"
#include "ReportHistoryStore.h"
#include "RunnerModels.h"

#include "PicoATE/Core/ExecutionReportJson.h"
#include "PicoATE/Core/SequenceCompiler.h"
#include "PicoATE/Core/ExecutionSession.h"

#include <QAbstractItemModelTester>
#include <QFileInfo>
#include <QFile>
#include <QJsonDocument>
#include <QSortFilterProxyModel>
#include <QTemporaryDir>
#include <QThread>

#include <algorithm>
#include <atomic>
#include <memory>

using namespace PicoATE::Ui;

namespace {

struct FakeServiceControl {
    std::atomic<int> compileCalls{0};
    std::atomic<int> runCalls{0};
    std::atomic<bool> releaseRun{true};
    std::atomic<bool> stopObserved{false};
    std::atomic<QThread*> compileThread{nullptr};
    std::atomic<QThread*> runThread{nullptr};
    int compileDelayMs = 0;
    bool compileSucceeds = true;
};

class FakeExecutionService final : public IExecutionService
{
public:
    explicit FakeExecutionService(std::shared_ptr<FakeServiceControl> control)
        : m_control(std::move(control))
    {
    }

    CompileServiceResult compile(const CompileRequest& request) override
    {
        m_control->compileCalls.fetch_add(1);
        m_control->compileThread.store(QThread::currentThread());
        if (m_control->compileDelayMs > 0) {
            QThread::msleep(static_cast<unsigned long>(m_control->compileDelayMs));
        }

        CompileServiceResult result;
        result.requestId = request.requestId;
        result.success = m_control->compileSucceeds;
        if (result.success) {
            result.sequenceId = QStringLiteral("fake-sequence");
            result.sequenceName = QStringLiteral("Fake Sequence");
            result.sequenceVersion = QStringLiteral("1.0");
            result.nodeCount = 3;
        } else {
            result.diagnostics.push_back({UiDiagnosticSeverity::Error,
                                          QStringLiteral("groups[0]"),
                                          QStringLiteral("Fake compile error"),
                                          QStringLiteral("Fix the fake input")});
        }
        return result;
    }

    RunServiceResult run(
        const RunRequest& request,
        const std::shared_ptr<PicoATE::Core::StopToken>& stopToken,
        PicoATE::Core::IRuntimeEventSink* eventSink) override
    {
        m_control->runCalls.fetch_add(1);
        m_control->runThread.store(QThread::currentThread());

        if (eventSink) {
            PicoATE::Core::RuntimeEvent event;
            event.kind = PicoATE::Core::RuntimeEventKind::UutRegistered;
            event.uutId = QStringLiteral("fake-uut");
            eventSink->publish(event);
        }

        while (!m_control->releaseRun.load() && !stopToken->isStopRequested()) {
            QThread::msleep(5);
        }
        m_control->stopObserved.store(stopToken->isStopRequested());

        RunServiceResult result;
        result.requestId = request.requestId;
        result.executed = true;
        result.stopRequested = stopToken->isStopRequested();
        result.report.planId = QStringLiteral("fake-plan");
        result.report.sequenceId = QStringLiteral("fake-sequence");
        result.report.state = result.stopRequested &&
                                      stopToken->requestedMode() == PicoATE::Core::StopMode::Abort
            ? PicoATE::Core::ExecutionState::Aborted
            : PicoATE::Core::ExecutionState::Completed;
        result.report.completed = true;
        return result;
    }

private:
    std::shared_ptr<FakeServiceControl> m_control;
};

std::unique_ptr<IExecutionService> fakeService(
    const std::shared_ptr<FakeServiceControl>& control)
{
    return std::make_unique<FakeExecutionService>(control);
}

PicoATE::Core::ExecutionReport sampleReport()
{
    using namespace PicoATE::Core;
    MeasurementResult measurement;
    measurement.name = QStringLiteral("输出电压,\"VOUT\"");
    measurement.value = 4.999;
    measurement.unit = QStringLiteral("V");
    measurement.rawValue = QStringLiteral("4.9990");
    measurement.hasLowerLimit = true;
    measurement.lowerLimit = 4.9;
    measurement.hasUpperLimit = true;
    measurement.upperLimit = 5.1;
    measurement.status = MeasurementStatus::Passed;
    measurement.attributes.insert(QStringLiteral("channel"), 1);

    AttemptReport attempt;
    attempt.index = 2;
    attempt.outcome = NodeOutcome::Passed;
    attempt.loopIteration.active = true;
    attempt.loopIteration.loopId = QStringLiteral("sample-loop");
    attempt.loopIteration.controllerNodeId = QStringLiteral("repeat");
    attempt.loopIteration.variableName = QStringLiteral("sampleIndex");
    attempt.loopIteration.iterationIndex = 1;
    attempt.loopIteration.iterationNumber = 2;
    attempt.loopIteration.value = 1;
    attempt.measurements = {measurement};

    StepReport step;
    step.stepId = QStringLiteral("measure-voltage");
    step.displayName = QStringLiteral("测量,\"输出\"");
    step.kind = ExecNodeKind::Action;
    step.state = ActivationState::Passed;
    step.outcome = NodeOutcome::Passed;
    step.loop.inLoop = true;
    step.loop.loopId = QStringLiteral("sample-loop");
    step.loop.controllerStepId = QStringLiteral("repeat");
    step.loop.variableName = QStringLiteral("sampleIndex");
    step.loop.from = 0;
    step.loop.to = 2;
    step.loop.step = 1;
    step.measurements = {measurement};
    step.attempts = {attempt};

    UutReport uut;
    uut.uutId = QStringLiteral("UUT-中文-01");
    uut.steps = {step};

    ExecutionReport report;
    report.planId = QStringLiteral("sample-plan");
    report.sequenceId = QStringLiteral("sample-sequence");
    report.sequenceVersion = QStringLiteral("1.2.3");
    report.state = ExecutionState::Completed;
    report.completed = true;
    report.uuts = {uut};
    return report;
}

} // namespace

class ExecutionViewModelTests : public QObject
{
    Q_OBJECT

private slots:
    void sourceSelectionInvalidatesCompiledState();
    void compileAndRunExecuteOffTheUiThread();
    void compileFailurePublishesDiagnostics();
    void staleCompileResultDoesNotOverwriteNewSource();
    void stopSignalsRunningServiceThroughStopToken();
    void destructionRequestsAbortAndWaitsForWorker();
    void coreServiceCompilesAndRunsSimpleSequence();
    void runnerModelsExposeReportHierarchyAndDetails();
    void coreServiceRunsBasicAndForLoopExamples();
    void viewModelFlushesWorkerEventsInBatches();
    void corePublishesOrderedBarrierLoopRetryAndStopEvents();
    void runtimeEventsUpdateResultAndDeviceModels();
    void executionReportJsonRoundTripsAndRejectsUnsupportedVersions();
    void reportHistoryPersistsLoadsAndRebuildsIndex();
    void reportExporterWritesJsonAndCsv();
};

void ExecutionViewModelTests::sourceSelectionInvalidatesCompiledState()
{
    auto control = std::make_shared<FakeServiceControl>();
    ExecutionViewModel viewModel(fakeService(control));

    QCOMPARE(viewModel.state(), UiRunState::Empty);
    QVERIFY(!viewModel.canCompile());

    viewModel.setSequencePath(QStringLiteral("sequence.json"));
    QCOMPARE(viewModel.state(), UiRunState::SourceSelected);
    QVERIFY(viewModel.canCompile());
    QVERIFY(!viewModel.canRun());

    viewModel.compile();
    QTRY_COMPARE_WITH_TIMEOUT(viewModel.state(), UiRunState::Ready, 1000);
    QVERIFY(viewModel.canRun());

    viewModel.setStationPath(QStringLiteral("station.json"));
    QCOMPARE(viewModel.state(), UiRunState::SourceSelected);
    QVERIFY(!viewModel.canRun());
}

void ExecutionViewModelTests::compileAndRunExecuteOffTheUiThread()
{
    auto control = std::make_shared<FakeServiceControl>();
    ExecutionViewModel viewModel(fakeService(control));
    viewModel.setSequencePath(QStringLiteral("sequence.json"));

    viewModel.compile();
    QTRY_COMPARE_WITH_TIMEOUT(viewModel.state(), UiRunState::Ready, 1000);
    QCOMPARE(control->compileCalls.load(), 1);
    QVERIFY(control->compileThread.load() != QThread::currentThread());
    QCOMPARE(viewModel.compileSummary().nodeCount, 3);

    viewModel.run(2, QStringLiteral("DUT"));
    QTRY_COMPARE_WITH_TIMEOUT(viewModel.state(), UiRunState::Completed, 1000);
    QCOMPARE(control->runCalls.load(), 1);
    QVERIFY(control->runThread.load() != QThread::currentThread());
    QCOMPARE(viewModel.report().planId, QStringLiteral("fake-plan"));
}

void ExecutionViewModelTests::compileFailurePublishesDiagnostics()
{
    auto control = std::make_shared<FakeServiceControl>();
    control->compileSucceeds = false;
    ExecutionViewModel viewModel(fakeService(control));
    viewModel.setSequencePath(QStringLiteral("broken.json"));

    viewModel.compile();
    QTRY_COMPARE_WITH_TIMEOUT(viewModel.state(), UiRunState::CompileFailed, 1000);
    QCOMPARE(viewModel.diagnostics().size(), 1);
    QCOMPARE(viewModel.diagnostics().first().path, QStringLiteral("groups[0]"));
    QVERIFY(!viewModel.canRun());
}

void ExecutionViewModelTests::staleCompileResultDoesNotOverwriteNewSource()
{
    auto control = std::make_shared<FakeServiceControl>();
    control->compileDelayMs = 80;
    ExecutionViewModel viewModel(fakeService(control));
    viewModel.setSequencePath(QStringLiteral("old.json"));
    viewModel.compile();
    QCOMPARE(viewModel.state(), UiRunState::Compiling);

    viewModel.setSequencePath(QStringLiteral("new.json"));
    QCOMPARE(viewModel.state(), UiRunState::SourceSelected);
    QTest::qWait(150);

    QCOMPARE(viewModel.state(), UiRunState::SourceSelected);
    QVERIFY(!viewModel.canRun());
    QVERIFY(viewModel.compileSummary().sequenceId.isEmpty());
}

void ExecutionViewModelTests::stopSignalsRunningServiceThroughStopToken()
{
    auto control = std::make_shared<FakeServiceControl>();
    control->releaseRun.store(false);
    ExecutionViewModel viewModel(fakeService(control));
    viewModel.setSequencePath(QStringLiteral("sequence.json"));
    viewModel.compile();
    QTRY_COMPARE_WITH_TIMEOUT(viewModel.state(), UiRunState::Ready, 1000);

    viewModel.run();
    QTRY_COMPARE_WITH_TIMEOUT(viewModel.state(), UiRunState::Running, 1000);
    QVERIFY(viewModel.canStop());
    viewModel.stop();
    QCOMPARE(viewModel.state(), UiRunState::Stopping);

    QTRY_VERIFY_WITH_TIMEOUT(control->stopObserved.load(), 1000);
    QTRY_COMPARE_WITH_TIMEOUT(viewModel.state(), UiRunState::Completed, 1000);
    QVERIFY(viewModel.report().completed);
}

void ExecutionViewModelTests::destructionRequestsAbortAndWaitsForWorker()
{
    auto control = std::make_shared<FakeServiceControl>();
    control->releaseRun.store(false);
    auto viewModel = std::make_unique<ExecutionViewModel>(fakeService(control));
    viewModel->setSequencePath(QStringLiteral("sequence.json"));
    viewModel->compile();
    QTRY_COMPARE_WITH_TIMEOUT(viewModel->state(), UiRunState::Ready, 1000);
    viewModel->run();
    QTRY_COMPARE_WITH_TIMEOUT(viewModel->state(), UiRunState::Running, 1000);

    viewModel.reset();
    QVERIFY(control->stopObserved.load());
}

void ExecutionViewModelTests::coreServiceCompilesAndRunsSimpleSequence()
{
    const QString projectDir = QString::fromUtf8(PICOATE_UI_TEST_PROJECT_DIR);
    const QString sequencePath = QFileInfo(
        projectDir + QStringLiteral("/examples/simple_sequence.json"))
                                     .absoluteFilePath();
    QVERIFY2(QFileInfo::exists(sequencePath), qPrintable(sequencePath));

    CoreExecutionService service(projectDir);
    CompileRequest compileRequest;
    compileRequest.requestId = 11;
    compileRequest.sequencePath = sequencePath;
    const auto compileResult = service.compile(compileRequest);
    QVERIFY2(compileResult.success,
             qPrintable(compileResult.diagnostics.isEmpty()
                            ? QStringLiteral("Compile failed without diagnostics")
                            : compileResult.diagnostics.first().message));
    QCOMPARE(compileResult.requestId, quint64(11));
    QVERIFY(compileResult.nodeCount > 0);

    RunRequest runRequest;
    runRequest.requestId = 12;
    runRequest.uutCount = 2;
    runRequest.uutPrefix = QStringLiteral("DUT");
    const auto runResult = service.run(
        runRequest,
        std::make_shared<PicoATE::Core::StopToken>());
    QVERIFY(runResult.executed);
    QCOMPARE(runResult.requestId, quint64(12));
    QVERIFY(runResult.report.completed);
    QVERIFY(!runResult.report.hasError);
    QCOMPARE(runResult.report.uuts.size(), 2);
    QCOMPARE(runResult.report.uuts.first().uutId, QStringLiteral("DUT-1"));
}

void ExecutionViewModelTests::runnerModelsExposeReportHierarchyAndDetails()
{
    PicoATE::Core::MeasurementResult measurement;
    measurement.name = QStringLiteral("VOUT");
    measurement.value = 4.999;
    measurement.unit = QStringLiteral("V");
    measurement.hasLowerLimit = true;
    measurement.lowerLimit = 4.9;
    measurement.hasUpperLimit = true;
    measurement.upperLimit = 5.1;
    measurement.status = PicoATE::Core::MeasurementStatus::Passed;

    PicoATE::Core::AttemptReport attempt;
    attempt.index = 1;
    attempt.outcome = PicoATE::Core::NodeOutcome::Passed;
    attempt.loopIteration.active = true;
    attempt.loopIteration.iterationNumber = 2;
    attempt.loopIteration.variableName = QStringLiteral("sampleIndex");
    attempt.loopIteration.value = 1;
    attempt.measurements = {measurement};

    PicoATE::Core::StepReport step;
    step.stepId = QStringLiteral("measure-voltage");
    step.displayName = QStringLiteral("Measure Voltage");
    step.state = PicoATE::Core::ActivationState::Passed;
    step.outcome = PicoATE::Core::NodeOutcome::Passed;
    step.loop.inLoop = true;
    step.loop.loopId = QStringLiteral("sample-loop");
    step.loop.variableName = QStringLiteral("sampleIndex");
    step.loop.from = 0;
    step.loop.to = 2;
    step.loop.step = 1;
    step.attempts = {attempt};
    step.measurements = {measurement};

    PicoATE::Core::UutReport uut;
    uut.uutId = QStringLiteral("UUT-01");
    uut.steps = {step};

    PicoATE::Core::ExecutionReport report;
    report.completed = true;
    report.uuts = {uut};

    UutStepModel resultModel;
    QAbstractItemModelTester resultTester(
        &resultModel,
        QAbstractItemModelTester::FailureReportingMode::QtTest);
    resultModel.setReport(report);
    QCOMPARE(resultModel.rowCount(), 1);
    const auto uutIndex = resultModel.index(0, UutStepModel::NameColumn);
    QCOMPARE(resultModel.data(uutIndex).toString(), QStringLiteral("UUT-01"));
    QCOMPARE(resultModel.rowCount(uutIndex), 1);
    const auto stepIndex = resultModel.index(0, UutStepModel::NameColumn, uutIndex);
    QCOMPARE(resultModel.data(stepIndex).toString(), QStringLiteral("Measure Voltage"));
    QCOMPARE(resultModel.itemType(stepIndex), UutStepModel::StepItem);
    QVERIFY(resultModel.stepAt(stepIndex).has_value());

    AttemptModel attemptModel;
    QAbstractItemModelTester attemptTester(
        &attemptModel,
        QAbstractItemModelTester::FailureReportingMode::QtTest);
    attemptModel.setStep(resultModel.stepAt(stepIndex));
    QCOMPARE(attemptModel.rowCount(), 1);
    QCOMPARE(attemptModel.data(attemptModel.index(0, AttemptModel::LoopColumn)).toString(),
             QStringLiteral("#2 / sampleIndex=1"));
    QVERIFY(attemptModel.attemptAt(0).has_value());

    MeasurementModel measurementModel;
    QAbstractItemModelTester measurementTester(
        &measurementModel,
        QAbstractItemModelTester::FailureReportingMode::QtTest);
    measurementModel.setMeasurements(attemptModel.attemptAt(0)->measurements);
    QCOMPARE(measurementModel.rowCount(), 1);
    QCOMPARE(measurementModel.data(
                 measurementModel.index(0, MeasurementModel::NameColumn)).toString(),
             QStringLiteral("VOUT"));
    QCOMPARE(measurementModel.data(
                 measurementModel.index(0, MeasurementModel::LimitsColumn)).toString(),
             QStringLiteral("[4.9, 5.1]"));

    DiagnosticModel diagnosticModel;
    QAbstractItemModelTester diagnosticTester(
        &diagnosticModel,
        QAbstractItemModelTester::FailureReportingMode::QtTest);
    diagnosticModel.setDiagnostics({{UiDiagnosticSeverity::Warning,
                                     QStringLiteral("groups[0]"),
                                     QStringLiteral("Unknown field"),
                                     QStringLiteral("Use x-* for extensions")}});
    QCOMPARE(diagnosticModel.rowCount(), 1);
    QCOMPARE(diagnosticModel.data(
                 diagnosticModel.index(0, DiagnosticModel::SeverityColumn)).toString(),
             QStringLiteral("Warning"));
}

void ExecutionViewModelTests::coreServiceRunsBasicAndForLoopExamples()
{
    const QString projectDir = QString::fromUtf8(PICOATE_UI_TEST_PROJECT_DIR);
    CoreExecutionService service(projectDir);

    auto runExample = [&](const QString& fileName, int uutCount, quint64 requestId) {
        CompileRequest compileRequest;
        compileRequest.requestId = requestId;
        compileRequest.sequencePath = QFileInfo(
            projectDir + QStringLiteral("/examples/") + fileName).absoluteFilePath();
        const auto compileResult = service.compile(compileRequest);
        if (!compileResult.success) {
            RunServiceResult failed;
            failed.requestId = requestId;
            failed.diagnostics = compileResult.diagnostics;
            return failed;
        }

        RunRequest runRequest;
        runRequest.requestId = requestId;
        runRequest.uutCount = uutCount;
        return service.run(runRequest, std::make_shared<PicoATE::Core::StopToken>());
    };

    const auto basic = runExample(QStringLiteral("basic_sequence.json"), 2, 21);
    QVERIFY2(basic.executed,
             qPrintable(basic.diagnostics.isEmpty()
                            ? QStringLiteral("Basic example did not execute")
                            : basic.diagnostics.first().message));
    QVERIFY(basic.report.completed);
    QVERIFY(!basic.report.hasError);
    QCOMPARE(basic.report.uuts.size(), 2);
    for (const auto& uut : basic.report.uuts) {
        const auto step = std::find_if(uut.steps.cbegin(), uut.steps.cend(), [](const auto& item) {
            return item.stepId == QStringLiteral("measure-voltage");
        });
        QVERIFY(step != uut.steps.cend());
        QCOMPARE(step->measurements.size(), 1);
        QCOMPARE(step->measurements.first().name, QStringLiteral("VOUT"));
    }

    const auto loop = runExample(QStringLiteral("for_loop_sequence.json"), 1, 22);
    QVERIFY2(loop.executed,
             qPrintable(loop.diagnostics.isEmpty()
                            ? QStringLiteral("For-loop example did not execute")
                            : loop.diagnostics.first().message));
    QVERIFY(loop.report.completed);
    QVERIFY(!loop.report.hasError);
    QCOMPARE(loop.report.uuts.size(), 1);
    const auto& loopSteps = loop.report.uuts.first().steps;
    const auto loopStep = std::find_if(loopSteps.cbegin(), loopSteps.cend(), [](const auto& item) {
        return item.stepId == QStringLiteral("measure-sample");
    });
    QVERIFY(loopStep != loopSteps.cend());
    QVERIFY(loopStep->loop.inLoop);
    QCOMPARE(loopStep->attempts.size(), 3);
    for (int index = 0; index < loopStep->attempts.size(); ++index) {
        const auto& loopAttempt = loopStep->attempts[index];
        QVERIFY(loopAttempt.loopIteration.active);
        QCOMPARE(loopAttempt.loopIteration.iterationNumber, index + 1);
        QCOMPARE(loopAttempt.loopIteration.value, index);
        QCOMPARE(loopAttempt.measurements.size(), 1);
    }
}

void ExecutionViewModelTests::viewModelFlushesWorkerEventsInBatches()
{
    auto control = std::make_shared<FakeServiceControl>();
    ExecutionViewModel viewModel(fakeService(control));
    QVector<PicoATE::Core::RuntimeEvent> received;
    connect(&viewModel,
            &ExecutionViewModel::runtimeEventsReady,
            this,
            [&received](const auto& events) { received += events; });

    viewModel.setSequencePath(QStringLiteral("sequence.json"));
    viewModel.compile();
    QTRY_COMPARE_WITH_TIMEOUT(viewModel.state(), UiRunState::Ready, 1000);
    viewModel.run();
    QTRY_COMPARE_WITH_TIMEOUT(viewModel.state(), UiRunState::Completed, 1000);

    QCOMPARE(received.size(), 1);
    QCOMPARE(received.first().kind, PicoATE::Core::RuntimeEventKind::UutRegistered);
    QCOMPARE(received.first().uutId, QStringLiteral("fake-uut"));
}

void ExecutionViewModelTests::corePublishesOrderedBarrierLoopRetryAndStopEvents()
{
    const QString projectDir = QString::fromUtf8(PICOATE_UI_TEST_PROJECT_DIR);
    CoreExecutionService service(projectDir);
    BufferedRuntimeEventSink sink;

    auto compileAndRun = [&](const QString& fileName,
                             int uutCount,
                             quint64 requestId) -> RunServiceResult {
        CompileRequest compileRequest;
        compileRequest.requestId = requestId;
        compileRequest.sequencePath = QFileInfo(
            projectDir + QStringLiteral("/examples/") + fileName).absoluteFilePath();
        const auto compileResult = service.compile(compileRequest);
        if (!compileResult.success) {
            RunServiceResult failed;
            failed.requestId = requestId;
            failed.diagnostics = compileResult.diagnostics;
            return failed;
        }

        RunRequest runRequest;
        runRequest.requestId = requestId;
        runRequest.uutCount = uutCount;
        return service.run(
            runRequest,
            std::make_shared<PicoATE::Core::StopToken>(),
            &sink);
    };

    const auto basic = compileAndRun(QStringLiteral("basic_sequence.json"), 2, 31);
    QVERIFY(basic.executed);
    auto events = sink.takeAll();
    QVERIFY(!events.isEmpty());
    for (int index = 1; index < events.size(); ++index) {
        QVERIFY(events[index - 1].sequenceNumber < events[index].sequenceNumber);
    }
    QCOMPARE(std::count_if(events.cbegin(), events.cend(), [](const auto& event) {
                 return event.kind == PicoATE::Core::RuntimeEventKind::BarrierWaiting;
             }),
             2);
    QCOMPARE(std::count_if(events.cbegin(), events.cend(), [](const auto& event) {
                 return event.kind == PicoATE::Core::RuntimeEventKind::BarrierReleased;
             }),
             2);

    sink.clear();
    const auto loop = compileAndRun(QStringLiteral("for_loop_sequence.json"), 1, 32);
    QVERIFY(loop.executed);
    events = sink.takeAll();
    QCOMPARE(std::count_if(events.cbegin(), events.cend(), [](const auto& event) {
                 return event.kind == PicoATE::Core::RuntimeEventKind::LoopIterationStarted;
             }),
             3);
    QCOMPARE(std::count_if(events.cbegin(), events.cend(), [](const auto& event) {
                 return event.kind == PicoATE::Core::RuntimeEventKind::LoopCompleted;
             }),
             1);

    const auto retryDocument = QJsonDocument::fromJson(R"json(
        {
          "id": "runtime-event-retry",
          "name": "Runtime Event Retry",
          "groups": [{
            "id": "main",
            "kind": "main",
            "steps": [{
              "id": "measure",
              "kind": "action",
              "parameters": { "failUntilAttempt": 0 },
              "retry": { "maxAttempts": 2 }
            }]
          }]
        })json");
    PicoATE::Core::SequenceCompiler compiler;
    const auto retryCompile = compiler.compileJson(retryDocument.object());
    QVERIFY(retryCompile.ok());
    sink.clear();
    PicoATE::Core::ExecutionSession retrySession(
        retryCompile.plan,
        std::make_shared<PicoATE::Core::StopToken>(),
        &sink);
    retrySession.addUut(QStringLiteral("UUT-1"));
    retrySession.run();
    events = sink.takeAll();
    QCOMPARE(std::count_if(events.cbegin(), events.cend(), [](const auto& event) {
                 return event.kind == PicoATE::Core::RuntimeEventKind::RetryScheduled;
             }),
             1);
    QCOMPARE(std::count_if(events.cbegin(), events.cend(), [](const auto& event) {
                 return event.kind == PicoATE::Core::RuntimeEventKind::AttemptStarted;
             }),
             2);

    const auto stopDocument = QJsonDocument::fromJson(R"json(
        {
          "id": "runtime-event-stop",
          "name": "Runtime Event Stop",
          "groups": [
            { "id": "main", "kind": "main", "steps": [
              { "id": "work", "kind": "action" }
            ]},
            { "id": "cleanup", "kind": "cleanup", "steps": [
              { "id": "power-off", "kind": "cleanup" }
            ]}
          ]
        })json");
    const auto stopCompile = compiler.compileJson(stopDocument.object());
    QVERIFY(stopCompile.ok());
    sink.clear();
    PicoATE::Core::ExecutionSession stopSession(
        stopCompile.plan,
        std::make_shared<PicoATE::Core::StopToken>(),
        &sink);
    stopSession.addUut(QStringLiteral("UUT-1"));
    stopSession.requestStop();
    stopSession.run();
    events = sink.takeAll();
    QVERIFY(std::any_of(events.cbegin(), events.cend(), [](const auto& event) {
        return event.kind == PicoATE::Core::RuntimeEventKind::SessionStateChanged &&
               event.executionState == PicoATE::Core::ExecutionState::Stopping;
    }));
    QVERIFY(std::any_of(events.cbegin(), events.cend(), [](const auto& event) {
        return event.kind == PicoATE::Core::RuntimeEventKind::CleanupActivated;
    }));
}

void ExecutionViewModelTests::runtimeEventsUpdateResultAndDeviceModels()
{
    using namespace PicoATE::Core;
    QVector<RuntimeEvent> events;

    RuntimeEvent registered;
    registered.kind = RuntimeEventKind::UutRegistered;
    registered.uutId = QStringLiteral("UUT-1");
    events.push_back(registered);

    RuntimeEvent running;
    running.kind = RuntimeEventKind::NodeStateChanged;
    running.uutId = QStringLiteral("UUT-1");
    running.nodeId = QStringLiteral("measure");
    running.nodeDisplayName = QStringLiteral("Measure Voltage");
    running.nodeKind = ExecNodeKind::Action;
    running.activationState = ActivationState::Running;
    events.push_back(running);

    RuntimeEvent completed = running;
    completed.kind = RuntimeEventKind::AttemptCompleted;
    completed.attemptIndex = 1;
    completed.attemptState = AttemptState::Completed;
    completed.outcome = NodeOutcome::Passed;
    MeasurementResult measurement;
    measurement.name = QStringLiteral("VOUT");
    measurement.value = 5.0;
    measurement.unit = QStringLiteral("V");
    measurement.status = MeasurementStatus::Passed;
    completed.measurements = {measurement};
    events.push_back(completed);

    RuntimeEvent device;
    device.kind = RuntimeEventKind::DeviceStateChanged;
    device.deviceId = QStringLiteral("DMM1");
    device.deviceState = DeviceConnectionState::Connected;
    device.message = QStringLiteral("connected");
    device.details.insert(QStringLiteral("deviceType"), QStringLiteral("DMM"));
    device.details.insert(QStringLiteral("driverId"), QStringLiteral("fake.scpi"));
    events.push_back(device);

    UutStepModel resultModel;
    resultModel.applyRuntimeEvents(events);
    QCOMPARE(resultModel.rowCount(), 1);
    const auto uutIndex = resultModel.index(0, 0);
    QCOMPARE(resultModel.rowCount(uutIndex), 1);
    const auto stepIndex = resultModel.index(0, 0, uutIndex);
    const auto step = resultModel.stepAt(stepIndex);
    QVERIFY(step.has_value());
    QCOMPARE(step->attempts.size(), 1);
    QCOMPARE(step->measurements.size(), 1);

    StepReport reconciledStep = *step;
    reconciledStep.displayName = QStringLiteral("Measure Voltage (final)");
    reconciledStep.state = ActivationState::Passed;
    UutReport reconciledUut;
    reconciledUut.uutId = QStringLiteral("UUT-1");
    reconciledUut.steps = {reconciledStep};
    ExecutionReport finalReport;
    finalReport.completed = true;
    finalReport.state = ExecutionState::Completed;
    finalReport.uuts = {reconciledUut};
    resultModel.setReport(finalReport);
    const auto reconciledIndex = resultModel.index(0, 0, resultModel.index(0, 0));
    QCOMPARE(resultModel.stepAt(reconciledIndex)->displayName,
             QStringLiteral("Measure Voltage (final)"));

    DeviceStatusModel deviceModel;
    deviceModel.applyRuntimeEvents(events);
    QCOMPARE(deviceModel.rowCount(), 1);
    QCOMPARE(deviceModel.data(deviceModel.index(0, DeviceStatusModel::DeviceColumn)).toString(),
             QStringLiteral("DMM1"));
    QCOMPARE(deviceModel.data(deviceModel.index(0, DeviceStatusModel::StateColumn)).toString(),
             QStringLiteral("Connected"));
}

void ExecutionViewModelTests::executionReportJsonRoundTripsAndRejectsUnsupportedVersions()
{
    const auto report = sampleReport();
    const auto bytes = PicoATE::Core::serializeExecutionReport(report);
    const auto parsed = PicoATE::Core::parseExecutionReport(bytes);
    QVERIFY2(parsed.ok(),
             qPrintable(parsed.errors.isEmpty() ? QString() : parsed.errors.first().message));
    QCOMPARE(parsed.report.planId, report.planId);
    QCOMPARE(parsed.report.sequenceId, report.sequenceId);
    QCOMPARE(parsed.report.state, PicoATE::Core::ExecutionState::Completed);
    QCOMPARE(parsed.report.uuts.size(), 1);
    const auto& step = parsed.report.uuts.first().steps.first();
    QCOMPARE(step.displayName, QStringLiteral("测量,\"输出\""));
    QCOMPARE(step.kind, PicoATE::Core::ExecNodeKind::Action);
    QCOMPARE(step.attempts.first().index, 2);
    QCOMPARE(step.attempts.first().loopIteration.iterationNumber, 2);
    QCOMPARE(step.measurements.first().lowerLimit, 4.9);
    QCOMPARE(step.measurements.first().attributes.value(QStringLiteral("channel")).toInt(), 1);

    auto future = PicoATE::Core::executionReportToJson(report);
    future.insert(QStringLiteral("schemaVersion"), 99);
    const auto futureResult = PicoATE::Core::executionReportFromJson(future);
    QVERIFY(!futureResult.ok());
    QCOMPARE(futureResult.errors.first().path, QStringLiteral("schemaVersion"));

    const QJsonObject minimal{
        {QStringLiteral("schema"), QStringLiteral("picoate.execution-report")},
        {QStringLiteral("schemaVersion"), 1},
        {QStringLiteral("report"), QJsonObject{
             {QStringLiteral("state"), QStringLiteral("Completed")}}},
    };
    const auto minimalResult = PicoATE::Core::executionReportFromJson(minimal);
    QVERIFY(minimalResult.ok());
    QCOMPARE(minimalResult.report.state, PicoATE::Core::ExecutionState::Completed);

    const auto broken = PicoATE::Core::parseExecutionReport("{ broken json");
    QVERIFY(!broken.ok());
    QVERIFY(!broken.errors.first().path.isEmpty());
}

void ExecutionViewModelTests::reportHistoryPersistsLoadsAndRebuildsIndex()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    ReportHistoryStore store(directory.path());

    auto firstReport = sampleReport();
    const auto first = store.save(firstReport);
    QVERIFY2(first.success, qPrintable(first.errorMessage));
    QTest::qWait(2);
    auto secondReport = sampleReport();
    secondReport.planId = QStringLiteral("second-plan");
    secondReport.sequenceId = QStringLiteral("other-sequence");
    secondReport.hasError = true;
    secondReport.state = PicoATE::Core::ExecutionState::CompletedWithError;
    const auto second = store.save(secondReport);
    QVERIFY2(second.success, qPrintable(second.errorMessage));

    QString errorMessage;
    auto entries = store.entries(&errorMessage);
    QVERIFY2(errorMessage.isEmpty(), qPrintable(errorMessage));
    QCOMPARE(entries.size(), 2);
    QCOMPARE(entries.first().planId, QStringLiteral("second-plan"));
    QCOMPARE(entries.first().uutIds.first(), QStringLiteral("UUT-中文-01"));

    const auto loaded = store.load(first.entry.id);
    QVERIFY2(loaded.ok(), qPrintable(loaded.errorMessage));
    QCOMPARE(loaded.report.planId, QStringLiteral("sample-plan"));

    QFile brokenIndex(directory.filePath(QStringLiteral("index.json")));
    QVERIFY(brokenIndex.open(QIODevice::WriteOnly | QIODevice::Truncate));
    brokenIndex.write("{ not valid");
    brokenIndex.close();
    entries = store.entries(&errorMessage);
    QVERIFY2(errorMessage.isEmpty(), qPrintable(errorMessage));
    QCOMPARE(entries.size(), 2);

    HistoryModel model;
    model.setEntries(entries);
    QSortFilterProxyModel proxy;
    proxy.setSourceModel(&model);
    proxy.setFilterKeyColumn(-1);
    proxy.setFilterCaseSensitivity(Qt::CaseInsensitive);
    proxy.setFilterFixedString(QStringLiteral("other-sequence"));
    QCOMPARE(proxy.rowCount(), 1);
    proxy.setFilterFixedString(QStringLiteral("UUT-中文-01"));
    QCOMPARE(proxy.rowCount(), 2);
}

void ExecutionViewModelTests::reportExporterWritesJsonAndCsv()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const auto report = sampleReport();
    const auto jsonPath = directory.filePath(QStringLiteral("report.json"));
    const auto csvPath = directory.filePath(QStringLiteral("report.csv"));

    const auto jsonResult = ReportExporter::saveJson(jsonPath, report);
    QVERIFY2(jsonResult.success, qPrintable(jsonResult.errorMessage));
    QFile jsonFile(jsonPath);
    QVERIFY(jsonFile.open(QIODevice::ReadOnly));
    const auto parsed = PicoATE::Core::parseExecutionReport(jsonFile.readAll());
    QVERIFY(parsed.ok());

    const auto csvResult = ReportExporter::saveCsv(csvPath, report);
    QVERIFY2(csvResult.success, qPrintable(csvResult.errorMessage));
    QFile csvFile(csvPath);
    QVERIFY(csvFile.open(QIODevice::ReadOnly));
    const auto csv = csvFile.readAll();
    QVERIFY(csv.startsWith("\xEF\xBB\xBF"));
    const auto text = QString::fromUtf8(csv);
    QVERIFY(text.contains(QStringLiteral("UUT-中文-01")));
    QVERIFY(text.contains(QStringLiteral("\"测量,\"\"输出\"\"\"")));
    QVERIFY(text.contains(QStringLiteral("\"输出电压,\"\"VOUT\"\"\"")));
    QVERIFY(text.contains(QStringLiteral("\"4.9\"")));
}

QTEST_GUILESS_MAIN(ExecutionViewModelTests)
#include "ExecutionViewModelTests.moc"
