#include <QtTest/QtTest>

#include "ExecutionViewModel.h"
#include "BufferedRuntimeEventSink.h"
#include "CoreExecutionService.h"
#include "ReportExporter.h"
#include "ReportHistoryStore.h"
#include "PluginCatalog.h"
#include "PluginFunctionModel.h"
#include "RunnerModels.h"
#include "SequenceDocument.h"
#include "SequenceTreeModel.h"
#include "StationDeviceModel.h"
#include "StationDocument.h"
#include "StartupSupport.h"

#include "PicoATE/Core/ExecutionReportJson.h"
#include "PicoATE/Core/SequenceCompiler.h"
#include "PicoATE/Core/ExecutionSession.h"

#include <QAbstractItemModelTester>
#include <QBrush>
#include <QColor>
#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QFile>
#include <QJsonDocument>
#include <QMimeData>
#include <QSortFilterProxyModel>
#include <QTemporaryDir>
#include <QThread>
#include <QUndoStack>

#include <algorithm>
#include <atomic>
#include <memory>

using namespace PicoATE::Ui;

namespace {

struct FakeServiceControl {
    std::atomic<int> compileCalls{0};
    std::atomic<int> runCalls{0};
    std::atomic<bool> releaseRun{true};
    std::atomic<bool> releaseDeviceTest{true};
    std::atomic<bool> stopObserved{false};
    std::atomic<bool> pauseObserved{false};
    std::atomic<int> stepIntoObserved{0};
    std::atomic<int> stepOverObserved{0};
    std::atomic<int> breakpointCountObserved{0};
    std::atomic<QThread*> compileThread{nullptr};
    std::atomic<QThread*> runThread{nullptr};
    std::atomic<QThread*> deviceTestThread{nullptr};
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
        PicoATE::Core::IRuntimeEventSink* eventSink,
        const std::shared_ptr<PicoATE::Core::ExecutionControl>& executionControl) override
    {
        m_control->runCalls.fetch_add(1);
        m_control->runThread.store(QThread::currentThread());
        if (executionControl) {
            m_control->breakpointCountObserved.store(
                executionControl->breakpoints().size());
        }

        if (eventSink) {
            PicoATE::Core::RuntimeEvent event;
            event.kind = PicoATE::Core::RuntimeEventKind::UutRegistered;
            event.uutId = QStringLiteral("fake-uut");
            eventSink->publish(event);
        }

        auto publishPaused = [&] {
            if (executionControl) {
                PicoATE::Core::ExecutionDebugSnapshot snapshot;
                snapshot.planId = QStringLiteral("fake-plan");
                snapshot.sequenceId = QStringLiteral("fake-sequence");
                snapshot.sequenceVersion = QStringLiteral("1.0");
                snapshot.state = PicoATE::Core::ExecutionState::Paused;
                snapshot.pauseReason = PicoATE::Core::DebugPauseReason::UserPause;
                snapshot.currentUutId = QStringLiteral("fake-uut");
                snapshot.currentNodeId = QStringLiteral("fake-step");
                snapshot.currentLocalPath = QStringLiteral("001");

                PicoATE::Core::DebugNodeSnapshot node;
                node.nodeId = QStringLiteral("fake-step");
                node.localPath = QStringLiteral("001");
                node.displayName = QStringLiteral("Fake Step");
                node.kind = PicoATE::Core::ExecNodeKind::Action;
                node.state = PicoATE::Core::ActivationState::Running;

                PicoATE::Core::DebugUutSnapshot uut;
                uut.uutId = QStringLiteral("fake-uut");
                uut.variables.insert(QStringLiteral("sample"), 42);
                uut.nodes = {node};
                snapshot.uuts = {uut};
                executionControl->setDebugSnapshot(snapshot);
            }
            if (!eventSink) {
                return;
            }
            PicoATE::Core::RuntimeEvent paused;
            paused.kind = PicoATE::Core::RuntimeEventKind::SessionStateChanged;
            paused.executionState = PicoATE::Core::ExecutionState::Paused;
            eventSink->publish(paused);
        };
        auto publishRunning = [&] {
            if (!eventSink || stopToken->isStopRequested()) {
                return;
            }
            PicoATE::Core::RuntimeEvent resumed;
            resumed.kind = PicoATE::Core::RuntimeEventKind::SessionStateChanged;
            resumed.executionState = PicoATE::Core::ExecutionState::Running;
            eventSink->publish(resumed);
        };
        auto consumeStepCommand = [&] {
            const auto stepMode = executionControl->takeStepMode();
            if (stepMode == PicoATE::Core::DebugStepMode::None ||
                stopToken->isStopRequested()) {
                return false;
            }
            if (stepMode == PicoATE::Core::DebugStepMode::Into) {
                m_control->stepIntoObserved.fetch_add(1);
            } else if (stepMode == PicoATE::Core::DebugStepMode::Over) {
                m_control->stepOverObserved.fetch_add(1);
            }
            if (eventSink) {
                PicoATE::Core::RuntimeEvent step;
                step.kind = PicoATE::Core::RuntimeEventKind::DebugStepCompleted;
                step.executionState = PicoATE::Core::ExecutionState::Paused;
                step.nodeId = QStringLiteral("fake-step");
                step.details.insert(
                    QStringLiteral("stepMode"),
                    stepMode == PicoATE::Core::DebugStepMode::Over
                        ? QStringLiteral("over")
                        : QStringLiteral("into"));
                eventSink->publish(step);
            }
            executionControl->requestPause();
            if (!executionControl->enterPausedState()) {
                return false;
            }
            publishPaused();
            executionControl->waitUntilResumedOrStopped(*stopToken);
            return true;
        };

        while (!m_control->releaseRun.load() && !stopToken->isStopRequested()) {
            if (executionControl && executionControl->enterPausedState()) {
                m_control->pauseObserved.store(true);
                publishPaused();
                executionControl->waitUntilResumedOrStopped(*stopToken);
                while (!stopToken->isStopRequested() && consumeStepCommand()) {
                }
                publishRunning();
            }
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

    DeviceConnectionTestResult testDeviceConnection(
        const DeviceConnectionTestRequest& request,
        const std::shared_ptr<PicoATE::Core::StopToken>& stopToken) override
    {
        m_control->deviceTestThread.store(QThread::currentThread());
        while (!m_control->releaseDeviceTest.load() && !stopToken->isStopRequested()) {
            QThread::msleep(5);
        }
        m_control->stopObserved.store(stopToken->isStopRequested());
        DeviceConnectionTestResult result;
        result.requestId = request.requestId;
        result.deviceId = request.deviceId;
        result.outcome = stopToken->isStopRequested()
            ? DeviceConnectionTestOutcome::Cancelled
            : DeviceConnectionTestOutcome::Passed;
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
    attempt.durationMs = 987;
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
    step.phase = ExecutionPhase::Cleanup;
    step.state = ActivationState::Passed;
    step.outcome = NodeOutcome::Passed;
    step.durationMs = 1021;
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
    void pauseAndResumeControlRunningService();
    void stepCommandsControlPausedService();
    void debugSnapshotUpdatesWhenPausedAndClearsWhenResumed();
    void viewModelPassesBreakpointsToExecutionControl();
    void destructionRequestsAbortAndWaitsForWorker();
    void shutdownIsIdempotentAndStopsWorkerCallbacks();
    void deviceConnectionTestRunsOffUiThreadAndCancels();
    void coreServiceCompilesAndRunsSimpleSequence();
    void coreServiceRunsExplicitScannedUut();
    void startupSupportDiscoversSequencesAndValidatesDailyPassword();
    void pluginCatalogParsesGcanManifestAndCreatesSteps();
    void pluginCatalogRejectsDuplicateAndInvalidDefinitions();
    void pluginCatalogParsesCompactDescriptionAndRoundTripsRegistry();
    void pluginCatalogScansNativeDllThroughHost();
    void pluginCatalogValidatesStationBindings();
    void pluginFunctionModelBuildsHierarchyAndDropsGeneratedStep();
    void runnerModelsExposeReportHierarchyAndDetails();
    void uutStepModelUsesProductionStateColors();
    void uutStepModelBuildsSingleUutPhaseLayout();
    void coreServiceRunsBasicAndForLoopExamples();
    void viewModelFlushesWorkerEventsInBatches();
    void corePublishesOrderedBarrierLoopRetryAndStopEvents();
    void runtimeEventsUpdateResultAndDeviceModels();
    void runtimeLogModelKeepsRecentBoundedLogs();
    void runtimeTimelineModelKeepsControlEventsAndSkipsLogs();
    void debugSnapshotModelFlattensRuntimeState();
    void bufferedEventSinkPreservesControlEventsDuringLogFlood();
    void executionReportJsonRoundTripsAndRejectsUnsupportedVersions();
    void reportHistoryPersistsLoadsAndRebuildsIndex();
    void reportExporterWritesJsonAndCsv();
    void testItemReportAndRuntimeEventsPreserveHierarchy();
    void sequenceDocumentPreservesUnknownFieldsAndSnapshots();
    void sequenceDocumentReplacesItemAtomically();
    void sequenceDocumentUndoRedoTracksCleanState();
    void sequenceDocumentRelocatesAcrossShiftedParentPaths();
    void sequenceDocumentWrapsContiguousStepsInTestItem();
    void sequenceDocumentDestructionSilencesUndoStack();
    void sequenceDiagnosticPathsResolveNestedFields();
    void sequenceTreeModelBuildsHierarchyAndEditsSteps();
    void sequenceTreeModelTogglesTransientBreakpoints();
    void sequenceTreeModelMovesAcrossValidParents();
    void stationDocumentPreservesUnknownFieldsAndUndoHistory();
    void stationDeviceModelEditsAndReordersDevices();
    void coreServiceCompilesProvidedSequenceSnapshot();
    void coreServiceTestsDeviceConnectionAndFailurePaths();
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

void ExecutionViewModelTests::pauseAndResumeControlRunningService()
{
    auto control = std::make_shared<FakeServiceControl>();
    control->releaseRun.store(false);
    ExecutionViewModel viewModel(fakeService(control));
    viewModel.setSequencePath(QStringLiteral("sequence.json"));
    viewModel.compile();
    QTRY_COMPARE_WITH_TIMEOUT(viewModel.state(), UiRunState::Ready, 1000);

    viewModel.run();
    QTRY_COMPARE_WITH_TIMEOUT(viewModel.state(), UiRunState::Running, 1000);
    QVERIFY(viewModel.canPause());
    QVERIFY(!viewModel.canResume());

    viewModel.pause();
    QCOMPARE(viewModel.state(), UiRunState::Pausing);
    QTRY_VERIFY_WITH_TIMEOUT(control->pauseObserved.load(), 1000);
    QTRY_COMPARE_WITH_TIMEOUT(viewModel.state(), UiRunState::Paused, 1000);
    QVERIFY(!viewModel.canPause());
    QVERIFY(viewModel.canResume());
    QVERIFY(viewModel.canStop());

    viewModel.resume();
    QCOMPARE(viewModel.state(), UiRunState::Running);
    QVERIFY(viewModel.canPause());
    control->releaseRun.store(true);
    QTRY_COMPARE_WITH_TIMEOUT(viewModel.state(), UiRunState::Completed, 1000);
}

void ExecutionViewModelTests::stepCommandsControlPausedService()
{
    auto control = std::make_shared<FakeServiceControl>();
    control->releaseRun.store(false);
    ExecutionViewModel viewModel(fakeService(control));
    viewModel.setSequencePath(QStringLiteral("sequence.json"));
    viewModel.compile();
    QTRY_COMPARE_WITH_TIMEOUT(viewModel.state(), UiRunState::Ready, 1000);

    viewModel.run();
    QTRY_COMPARE_WITH_TIMEOUT(viewModel.state(), UiRunState::Running, 1000);
    viewModel.pause();
    QTRY_COMPARE_WITH_TIMEOUT(viewModel.state(), UiRunState::Paused, 1000);
    QVERIFY(viewModel.canStepInto());
    QVERIFY(viewModel.canStepOver());

    viewModel.stepInto();
    QTRY_COMPARE_WITH_TIMEOUT(control->stepIntoObserved.load(), 1, 1000);
    QTRY_COMPARE_WITH_TIMEOUT(viewModel.state(), UiRunState::Paused, 1000);
    QVERIFY(viewModel.canStepOver());

    viewModel.stepOver();
    QTRY_COMPARE_WITH_TIMEOUT(control->stepOverObserved.load(), 1, 1000);
    QTRY_COMPARE_WITH_TIMEOUT(viewModel.state(), UiRunState::Paused, 1000);
    QVERIFY(viewModel.canStop());

    viewModel.stop();
    QTRY_COMPARE_WITH_TIMEOUT(viewModel.state(), UiRunState::Completed, 1000);
    QVERIFY(control->stopObserved.load());
}

void ExecutionViewModelTests::debugSnapshotUpdatesWhenPausedAndClearsWhenResumed()
{
    auto control = std::make_shared<FakeServiceControl>();
    control->releaseRun.store(false);
    ExecutionViewModel viewModel(fakeService(control));
    viewModel.setSequencePath(QStringLiteral("sequence.json"));
    viewModel.compile();
    QTRY_COMPARE_WITH_TIMEOUT(viewModel.state(), UiRunState::Ready, 1000);

    QSignalSpy snapshotSpy(&viewModel, &ExecutionViewModel::debugSnapshotChanged);
    viewModel.run();
    QTRY_COMPARE_WITH_TIMEOUT(viewModel.state(), UiRunState::Running, 1000);
    viewModel.pause();
    QTRY_COMPARE_WITH_TIMEOUT(viewModel.state(), UiRunState::Paused, 1000);
    QTRY_VERIFY_WITH_TIMEOUT(viewModel.debugSnapshot().has_value(), 1000);
    QVERIFY(snapshotSpy.count() > 0);
    QCOMPARE(viewModel.debugSnapshot()->currentUutId, QString("fake-uut"));
    QCOMPARE(viewModel.debugSnapshot()->currentNodeId, QString("fake-step"));
    QCOMPARE(viewModel.debugSnapshot()->currentLocalPath, QString("001"));
    QCOMPARE(viewModel.debugSnapshot()->uuts.first().variables.value("sample").toInt(), 42);

    viewModel.resume();
    QCOMPARE(viewModel.state(), UiRunState::Running);
    QVERIFY(!viewModel.debugSnapshot().has_value());

    control->releaseRun.store(true);
    QTRY_COMPARE_WITH_TIMEOUT(viewModel.state(), UiRunState::Completed, 1000);
}

void ExecutionViewModelTests::viewModelPassesBreakpointsToExecutionControl()
{
    auto control = std::make_shared<FakeServiceControl>();
    ExecutionViewModel viewModel(fakeService(control));
    PicoATE::Core::BreakpointSpec breakpoint;
    breakpoint.id = QStringLiteral("bp-001");
    breakpoint.address = PicoATE::Core::BreakpointAddress::nodePath(
        QStringLiteral("open-fixture"));
    viewModel.setBreakpoints({breakpoint});
    QCOMPARE(viewModel.breakpoints().size(), 1);

    viewModel.setSequencePath(QStringLiteral("sequence.json"));
    viewModel.compile();
    QTRY_COMPARE_WITH_TIMEOUT(viewModel.state(), UiRunState::Ready, 1000);
    viewModel.run();
    QTRY_COMPARE_WITH_TIMEOUT(viewModel.state(), UiRunState::Completed, 1000);
    QCOMPARE(control->breakpointCountObserved.load(), 1);
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

void ExecutionViewModelTests::shutdownIsIdempotentAndStopsWorkerCallbacks()
{
    auto control = std::make_shared<FakeServiceControl>();
    control->releaseRun.store(false);
    ExecutionViewModel viewModel(fakeService(control));
    viewModel.setSequencePath(QStringLiteral("sequence.json"));
    viewModel.compile();
    QTRY_COMPARE_WITH_TIMEOUT(viewModel.state(), UiRunState::Ready, 1000);
    viewModel.run();
    QTRY_COMPARE_WITH_TIMEOUT(viewModel.state(), UiRunState::Running, 1000);

    viewModel.shutdown();
    viewModel.shutdown();

    QVERIFY(control->stopObserved.load());
    QVERIFY(!viewModel.canChangeSources());
    QVERIFY(!viewModel.canCompile());
    QVERIFY(!viewModel.canRun());
    QVERIFY(!viewModel.canStop());
}

void ExecutionViewModelTests::deviceConnectionTestRunsOffUiThreadAndCancels()
{
    auto control = std::make_shared<FakeServiceControl>();
    control->releaseDeviceTest.store(false);
    ExecutionViewModel viewModel(fakeService(control));
    viewModel.setSequencePath(QStringLiteral("sequence.json"));
    viewModel.setStationPath(QStringLiteral("station.json"));
    QSignalSpy finishedSpy(
        &viewModel, &ExecutionViewModel::deviceConnectionTestFinished);

    viewModel.testDeviceConnection(QStringLiteral("DMM1"), 500);
    QTRY_COMPARE_WITH_TIMEOUT(viewModel.state(), UiRunState::TestingDevice, 1000);
    QTRY_VERIFY_WITH_TIMEOUT(control->deviceTestThread.load() != nullptr, 1000);
    QVERIFY(control->deviceTestThread.load() != QThread::currentThread());
    QVERIFY(viewModel.canStop());

    viewModel.stop(PicoATE::Core::StopMode::Abort);
    QCOMPARE(viewModel.state(), UiRunState::Stopping);
    QTRY_COMPARE_WITH_TIMEOUT(finishedSpy.count(), 1, 1000);
    QCOMPARE(viewModel.state(), UiRunState::SourceSelected);
    QCOMPARE(viewModel.deviceConnectionTestResult().outcome,
             DeviceConnectionTestOutcome::Cancelled);
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

void ExecutionViewModelTests::coreServiceRunsExplicitScannedUut()
{
    CoreExecutionService service;
    CompileRequest compileRequest;
    compileRequest.requestId = 81;
    compileRequest.sequencePath = QStringLiteral(PICOATE_UI_TEST_PROJECT_DIR)
        + QStringLiteral("/examples/simple_sequence.json");
    const auto compileResult = service.compile(compileRequest);
    QVERIFY(compileResult.success);

    RunRequest request;
    request.requestId = 82;
    RunRequest::UutInput input;
    input.uutId = QStringLiteral("SN-20260710-001");
    input.variables.insert(QStringLiteral("order"), QStringLiteral("ORDER-42"));
    request.uuts.push_back(input);
    const auto runResult = service.run(
        request, std::make_shared<PicoATE::Core::StopToken>());
    QVERIFY(runResult.executed);
    QCOMPARE(runResult.report.uuts.size(), 1);
    QCOMPARE(runResult.report.uuts.first().uutId,
             QStringLiteral("SN-20260710-001"));

    request.requestId = 83;
    request.uuts.push_back(input);
    const auto duplicate = service.run(
        request, std::make_shared<PicoATE::Core::StopToken>());
    QVERIFY(!duplicate.executed);
    QVERIFY(!duplicate.diagnostics.isEmpty());
    QCOMPARE(duplicate.diagnostics.first().path, QStringLiteral("uuts"));
}

void ExecutionViewModelTests::startupSupportDiscoversSequencesAndValidatesDailyPassword()
{
    QCOMPARE(StartupSupport::dailyAdminPassword(QDate(2026, 7, 10)), 40);
    QCOMPARE(StartupSupport::dailyAdminPassword(QDate(2026, 12, 31)), 45);
    QVERIFY(StartupSupport::matchesDailyAdminPassword(
        QStringLiteral("40"), QDate(2026, 7, 10)));
    QVERIFY(!StartupSupport::matchesDailyAdminPassword(
        QStringLiteral("41"), QDate(2026, 7, 10)));

    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const auto sequencePath = directory.filePath(
        QStringLiteral("product_sequence_v1.json"));
    QVERIFY(QFile::copy(QStringLiteral(PICOATE_UI_TEST_PROJECT_DIR)
                           + QStringLiteral("/examples/simple_sequence.json"),
                       sequencePath));

    QFile invalidSequence(directory.filePath(QStringLiteral("not_a_seq.json")));
    QVERIFY(invalidSequence.open(QIODevice::WriteOnly));
    invalidSequence.write(R"({"name":"not a sequence"})");
    invalidSequence.close();

    const auto stationPath = directory.filePath(QStringLiteral("StationSystem.json"));
    QFile station(stationPath);
    QVERIFY(station.open(QIODevice::WriteOnly));
    station.write(R"({
        "stationId": "station-test",
        "name": "Station Test",
        "scanDialogEnabled": false,
        "devices": []
    })");
    station.close();

    const auto discovered = StartupSupport::discoverSequenceFiles(directory.path());
    QCOMPARE(discovered, QStringList({QFileInfo(sequencePath).absoluteFilePath()}));
    QCOMPARE(StartupSupport::stationPathForSequence(sequencePath), stationPath);
    QVERIFY(!StartupSupport::stationScanDialogEnabled(stationPath));

    const auto testValidation = StartupSupport::validateSelection(
        UiMode::Test, sequencePath, stationPath);
    QVERIFY2(testValidation.ok(),
             qPrintable(testValidation.errors.join(QStringLiteral("\n"))));
    QVERIFY(!StartupSupport::validateSelection(
        UiMode::Admin, sequencePath, stationPath, QStringLiteral("39"),
        QDate(2026, 7, 10)).ok());
    QVERIFY(StartupSupport::validateSelection(
        UiMode::Admin, sequencePath, stationPath, QStringLiteral("40"),
        QDate(2026, 7, 10)).ok());
}

void ExecutionViewModelTests::pluginCatalogParsesGcanManifestAndCreatesSteps()
{
    const QString projectDir = QStringLiteral(PICOATE_UI_TEST_PROJECT_DIR);
    const auto manifestPath = projectDir
        + QStringLiteral("/templates/CAN/GCAN/PicoATE.CAN.GCAN.picoate-plugin.json");
    const auto result = PluginCatalog::load(manifestPath);
    QVERIFY2(result.ok(),
             qPrintable(result.errors.isEmpty() ? QStringLiteral("Manifest parse failed")
                                                : result.errors.first().path + QStringLiteral(": ")
                                                      + result.errors.first().message));
    QCOMPARE(result.manifest.pluginId, QStringLiteral("picoate.can.gcan"));
    QCOMPARE(result.manifest.moduleId, QStringLiteral("plugin.can.gcan"));
    QCOMPARE(result.manifest.category, QStringLiteral("CAN"));
    QCOMPARE(result.manifest.functions.size(), 4);

    const auto findFunction = [&result](const QString& id) {
        return std::find_if(result.manifest.functions.cbegin(),
                            result.manifest.functions.cend(),
                            [&id](const auto& function) { return function.id == id; });
    };
    const auto open = findFunction(QStringLiteral("open"));
    const auto write = findFunction(QStringLiteral("write"));
    const auto read = findFunction(QStringLiteral("read"));
    const auto close = findFunction(QStringLiteral("close"));
    QVERIFY(open != result.manifest.functions.cend());
    QVERIFY(write != result.manifest.functions.cend());
    QVERIFY(read != result.manifest.functions.cend());
    QVERIFY(close != result.manifest.functions.cend());
    QCOMPARE(open->inputs.size(), 6);
    QCOMPARE(open->inputs[3].type, PluginParameterType::Enumeration);
    QCOMPARE(open->inputs[3].options.size(), 4);
    QCOMPARE(write->inputs[1].type, PluginParameterType::HexBytes);
    QCOMPARE(read->outputs[2].key, QStringLiteral("dlc"));
    QCOMPARE(close->stepKind, QStringLiteral("cleanup"));

    const auto writeStep = PluginCatalog::createStep(
        result.manifest, *write, QStringLiteral("001"));
    QCOMPARE(writeStep.value(QStringLiteral("id")).toString(), QStringLiteral("001"));
    QCOMPARE(writeStep.value(QStringLiteral("moduleId")).toString(),
             QStringLiteral("plugin.can.gcan"));
    QCOMPARE(writeStep.value(QStringLiteral("function")).toString(),
             QStringLiteral("write"));
    QCOMPARE(writeStep.value(QStringLiteral("inputs")).toObject()
                 .value(QStringLiteral("data")).toString(),
             QStringLiteral("01 02 03 04"));

    const auto openStep = PluginCatalog::createStep(
        result.manifest, *open, QStringLiteral("can-open"));
    QCOMPARE(openStep.value(QStringLiteral("errorPolicy")).toObject()
                 .value(QStringLiteral("onFail")).toString(),
             QStringLiteral("RunCleanup"));

    const auto discovered = PluginCatalog::discoverManifestFiles(
        projectDir + QStringLiteral("/templates"));
    QVERIFY(discovered.contains(QFileInfo(manifestPath).absoluteFilePath()));
}

void ExecutionViewModelTests::pluginCatalogRejectsDuplicateAndInvalidDefinitions()
{
    const QByteArray json = R"({
      "schema": "picoate.plugin",
      "schemaVersion": 1,
      "pluginId": "broken",
      "moduleId": "plugin.broken",
      "name": "Broken",
      "category": "CAN",
      "functions": [
        {
          "id": "send",
          "name": "Send",
          "stepKind": "teleport",
          "inputs": [
            {"key": "mode", "name": "Mode", "type": "enum"},
            {"key": "mode", "name": "Mode 2", "type": "string", "minimum": 5, "maximum": 1}
          ]
        },
        {"id": "send", "name": "Duplicate", "stepKind": "action"}
      ]
    })";
    const auto result = PluginCatalog::parse(json);
    QVERIFY(!result.ok());
    const auto containsPath = [&result](const QString& path) {
        return std::any_of(result.errors.cbegin(), result.errors.cend(),
                           [&path](const auto& error) { return error.path == path; });
    };
    QVERIFY(containsPath(QStringLiteral("functions[0].stepKind")));
    QVERIFY(containsPath(QStringLiteral("functions[0].inputs[0].options")));
    QVERIFY(containsPath(QStringLiteral("functions[0].inputs[1].key")));
    QVERIFY(containsPath(QStringLiteral("functions[0].inputs[1]")));
    QVERIFY(containsPath(QStringLiteral("functions[1].id")));
}

void ExecutionViewModelTests::pluginCatalogParsesCompactDescriptionAndRoundTripsRegistry()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const auto pluginDirectory = directory.filePath(QStringLiteral("plugin/CAN/GCAN"));
    QVERIFY(QDir().mkpath(pluginDirectory));
    const auto dllPath = pluginDirectory
        + QStringLiteral("/PicoATE.CAN.GCAN.dll");
    QFile dll(dllPath);
    QVERIFY(dll.open(QIODevice::WriteOnly));
    dll.write("test");
    dll.close();

    const QByteArray description = R"({
      "name": "GCAN USB-CAN",
      "category": "CAN",
      "functions": [
        {
          "id": "open",
          "name": "Open CAN",
          "inputs": [
            {"key": "deviceIndex", "name": "Device Index", "type": "integer", "required": false, "default": 0}
          ],
          "outputs": []
        },
        {
          "id": "write",
          "name": "Send CAN Frame",
          "inputs": [
            {"key": "data", "name": "Frame Data", "type": "hex-bytes", "required": true}
          ],
          "outputs": []
        }
      ]
    })";
    const auto parsed = PluginCatalog::parseDescription(description, dllPath, 1);
    QVERIFY2(parsed.ok(), qPrintable(parsed.errors.isEmpty()
        ? QStringLiteral("Description parse failed")
        : parsed.errors.first().message));
    QCOMPARE(parsed.manifest.moduleId, QStringLiteral("plugin.can.gcan"));
    QCOMPARE(parsed.manifest.category, QStringLiteral("CAN"));
    QCOMPARE(parsed.manifest.functions[1].inputs[0].required, true);

    const auto discovered = PluginCatalog::discoverPluginFiles(
        directory.filePath(QStringLiteral("plugin")));
    QCOMPARE(discovered, QStringList{QFileInfo(dllPath).absoluteFilePath()});

    const auto registryPath = directory.filePath(QStringLiteral("PluginRegistry.json"));
    QString saveError;
    QVERIFY2(PluginCatalog::saveRegistry(
                 registryPath, {parsed.manifest}, &saveError),
             qPrintable(saveError));
    const auto loaded = PluginCatalog::loadRegistry(registryPath);
    QVERIFY2(loaded.ok(), qPrintable(loaded.errors.isEmpty()
        ? QStringLiteral("Registry load failed")
        : loaded.errors.first().message));
    QCOMPARE(loaded.plugins.size(), 1);
    QCOMPARE(loaded.plugins[0].moduleId, QStringLiteral("plugin.can.gcan"));
    QCOMPARE(loaded.plugins[0].functions.size(), 2);
    QCOMPARE(QFileInfo(loaded.plugins[0].dllPath).absoluteFilePath(),
             QFileInfo(dllPath).absoluteFilePath());

    const auto incompatible = PluginCatalog::parseDescription(description, dllPath, 2);
    QVERIFY(!incompatible.ok());
    QCOMPARE(incompatible.errors.first().path, QStringLiteral("abiVersion"));
}

void ExecutionViewModelTests::pluginCatalogScansNativeDllThroughHost()
{
    const auto buildRoot = QDir(QCoreApplication::applicationDirPath())
        .absoluteFilePath(QStringLiteral("../../.."));
    const auto nativeHost = QDir(buildRoot).absoluteFilePath(
        QStringLiteral("src/nativehost/Debug/PicoATE.NativeHost.exe"));
    const auto testPlugin = QDir(buildRoot).absoluteFilePath(
        QStringLiteral("src/testdllmodule/Debug/PicoATE.TestDllModule.dll"));
    QVERIFY2(QFileInfo::exists(nativeHost), qPrintable(nativeHost));
    QVERIFY2(QFileInfo::exists(testPlugin), qPrintable(testPlugin));

    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const auto pluginDirectory = directory.filePath(QStringLiteral("plugin/Test"));
    QVERIFY(QDir().mkpath(pluginDirectory));
    const auto copiedPlugin = pluginDirectory
        + QStringLiteral("/PicoATE.TestDllModule.dll");
    QVERIFY(QFile::copy(testPlugin, copiedPlugin));

    const auto registryPath = directory.filePath(QStringLiteral("PluginRegistry.json"));
    const auto scan = PluginCatalog::scanPlugins(
        directory.filePath(QStringLiteral("plugin")),
        nativeHost,
        registryPath,
        5000);
    QVERIFY2(scan.ok(), qPrintable(scan.errors.isEmpty()
        ? QStringLiteral("Plugin scan failed")
        : scan.errors.first().path + QStringLiteral(": ")
              + scan.errors.first().message));
    QCOMPARE(scan.discoveredDllCount, 1);
    QCOMPARE(scan.plugins.size(), 1);
    QCOMPARE(scan.plugins[0].name, QStringLiteral("PicoATE Test DLL"));
    QCOMPARE(scan.plugins[0].functions[0].id, QStringLiteral("echo"));
    QVERIFY(QFileInfo::exists(registryPath));

    const auto loaded = PluginCatalog::loadRegistry(registryPath);
    QVERIFY(loaded.ok());
    QCOMPARE(loaded.plugins.size(), 1);
    QCOMPARE(loaded.plugins[0].abiVersion, 1);
}

void ExecutionViewModelTests::pluginCatalogValidatesStationBindings()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const auto firstDll = directory.filePath(QStringLiteral("first.dll"));
    const auto secondDll = directory.filePath(QStringLiteral("second.dll"));
    for (const auto& path : {firstDll, secondDll}) {
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write("test");
    }

    PluginManifest plugin;
    plugin.moduleId = QStringLiteral("plugin.can.gcan");
    plugin.dllPath = firstDll;
    const auto station = QJsonDocument::fromJson(R"json({
      "devices": [
        {"deviceId":"CAN1","driverId":"plugin.can.gcan",
         "pluginPath":"${PROJECT_DIR}/first.dll","enabled":true},
        {"deviceId":"CAN2","driverId":"plugin.can.gcan","enabled":true},
        {"deviceId":"CAN3","driverId":"plugin.unknown",
         "pluginPath":"${PROJECT_DIR}/first.dll","enabled":true},
        {"deviceId":"CAN4","driverId":"plugin.can.gcan",
         "pluginPath":"${PROJECT_DIR}/second.dll","enabled":true},
        {"deviceId":"CAN5","driverId":"plugin.missing",
         "pluginPath":"${PROJECT_DIR}/missing.dll","enabled":true}
      ]
    })json").object();

    const auto diagnostics = PluginCatalog::validateStationBindings(
        station,
        {plugin},
        directory.filePath(QStringLiteral("StationSystem.json")),
        directory.path());
    const auto hasDiagnostic = [&diagnostics](const QString& path,
                                               bool warning) {
        return std::any_of(diagnostics.cbegin(), diagnostics.cend(),
                           [&](const PluginBindingDiagnostic& diagnostic) {
                               return diagnostic.path == path &&
                                      diagnostic.warning == warning;
                           });
    };
    QVERIFY(hasDiagnostic(QStringLiteral("devices[1].pluginPath"), false));
    QVERIFY(hasDiagnostic(QStringLiteral("devices[2].driverId"), true));
    QVERIFY(hasDiagnostic(QStringLiteral("devices[3].pluginPath"), false));
    QVERIFY(hasDiagnostic(QStringLiteral("devices[4].pluginPath"), false));
}

void ExecutionViewModelTests::pluginFunctionModelBuildsHierarchyAndDropsGeneratedStep()
{
    const auto projectDir = QStringLiteral(PICOATE_UI_TEST_PROJECT_DIR);
    const auto manifest = PluginCatalog::load(
        projectDir + QStringLiteral(
            "/templates/CAN/GCAN/PicoATE.CAN.GCAN.picoate-plugin.json"));
    QVERIFY(manifest.ok());

    PluginFunctionModel functionModel;
    QAbstractItemModelTester functionTester(
        &functionModel, QAbstractItemModelTester::FailureReportingMode::QtTest);
    functionModel.setPlugins({manifest.manifest});
    functionModel.setDeviceBindings({
        {QStringLiteral("plugin.can.gcan"), QStringList{QStringLiteral("CAN1")}}});
    QCOMPARE(functionModel.rowCount(), 1);
    const auto category = functionModel.index(0, 0);
    QCOMPARE(category.data().toString(), QStringLiteral("CAN"));
    const auto plugin = functionModel.index(0, 0, category);
    QCOMPARE(plugin.data().toString(), QStringLiteral("GCAN USB-CAN"));
    QCOMPARE(functionModel.rowCount(plugin), 4);
    const auto writeFunction = functionModel.index(1, 0, plugin);
    QCOMPARE(writeFunction.data(PluginFunctionModel::FunctionIdRole).toString(),
             QStringLiteral("write"));
    std::unique_ptr<QMimeData> mime(functionModel.mimeData({writeFunction}));
    QVERIFY(mime);
    QVERIFY(mime->hasFormat(PluginFunctionMimeType));

    SequenceDocument document;
    QVERIFY(document.load(
        QDir(projectDir).filePath(QStringLiteral("examples/test_item_sequence.json"))));
    SequenceTreeModel sequenceModel(&document);
    QAbstractItemModelTester sequenceTester(
        &sequenceModel, QAbstractItemModelTester::FailureReportingMode::QtTest);
    const auto mainGroup = sequenceModel.index(0, SequenceTreeModel::NameColumn);
    const int previousCount = sequenceModel.rowCount(mainGroup);
    QSignalSpy inserted(&sequenceModel, &SequenceTreeModel::itemInserted);
    QVERIFY(sequenceModel.dropMimeData(
        mime.get(), Qt::CopyAction, -1, 0, mainGroup));
    QCOMPARE(inserted.count(), 1);
    QCOMPARE(sequenceModel.rowCount(sequenceModel.index(0, 0)), previousCount + 1);

    SequenceItemPath insertedPath;
    insertedPath.groupIndex = 0;
    insertedPath.stepIndices = {previousCount};
    const auto step = document.objectAt(insertedPath);
    QCOMPARE(step.value(QStringLiteral("id")).toString(), QStringLiteral("001"));
    QCOMPARE(step.value(QStringLiteral("moduleId")).toString(),
             QStringLiteral("device"));
    QCOMPARE(step.value(QStringLiteral("function")).toString(),
             QStringLiteral("write"));
    QCOMPARE(step.value(QStringLiteral("inputs")).toObject()
                 .value(QStringLiteral("data")).toString(),
             QStringLiteral("01 02 03 04"));
    QCOMPARE(step.value(QStringLiteral("inputs")).toObject()
                 .value(QStringLiteral("deviceId")).toString(),
             QStringLiteral("CAN1"));

    functionModel.setDeviceBindings({
        {QStringLiteral("plugin.can.gcan"),
         QStringList{QStringLiteral("CAN1"), QStringLiteral("CAN2")}}});
    const auto multiCategory = functionModel.index(0, 0);
    const auto multiPlugin = functionModel.index(0, 0, multiCategory);
    QCOMPARE(functionModel.rowCount(multiPlugin), 8);
    const auto can2Write = functionModel.index(5, 0, multiPlugin);
    QCOMPARE(can2Write.data().toString(), QStringLiteral("Send CAN Frame [CAN2]"));
    QCOMPARE(can2Write.data(PluginFunctionModel::DeviceIdRole).toString(),
             QStringLiteral("CAN2"));
    std::unique_ptr<QMimeData> can2Mime(functionModel.mimeData({can2Write}));
    QVERIFY(can2Mime);
    const auto can2Step = QJsonDocument::fromJson(
        can2Mime->data(PluginFunctionMimeType)).object();
    QCOMPARE(can2Step.value(QStringLiteral("inputs")).toObject()
                 .value(QStringLiteral("deviceId")).toString(),
             QStringLiteral("CAN2"));

    document.undoStack()->undo();
    QCOMPARE(sequenceModel.rowCount(sequenceModel.index(0, 0)), previousCount);
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
    attempt.durationMs = 987;
    attempt.errorCode = QStringLiteral("E001");
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
    step.durationMs = 1021;
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
    QCOMPARE(resultModel.data(stepIndex.siblingAtColumn(UutStepModel::ErrorCodeColumn)).toString(),
             QStringLiteral("E001"));
    QCOMPARE(resultModel.data(stepIndex.siblingAtColumn(UutStepModel::LowerLimitColumn)).toString(),
             QStringLiteral("4.9"));
    QCOMPARE(resultModel.data(stepIndex.siblingAtColumn(UutStepModel::UpperLimitColumn)).toString(),
             QStringLiteral("5.1"));
    QCOMPARE(resultModel.data(stepIndex.siblingAtColumn(UutStepModel::ActualColumn)).toString(),
             QStringLiteral("4.999 V"));
    QCOMPARE(resultModel.data(stepIndex.siblingAtColumn(UutStepModel::OutcomeColumn)).toString(),
             QStringLiteral("Passed"));
    QCOMPARE(resultModel.data(stepIndex.siblingAtColumn(UutStepModel::TimeColumn)).toString(),
             QStringLiteral("1.021 s"));

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

void ExecutionViewModelTests::uutStepModelUsesProductionStateColors()
{
    PicoATE::Core::StepReport step;
    step.stepId = QStringLiteral("running-step");
    step.displayName = QStringLiteral("Running Step");
    step.state = PicoATE::Core::ActivationState::Running;
    PicoATE::Core::UutReport uut;
    uut.uutId = QStringLiteral("SN-001");
    uut.steps = {step};
    PicoATE::Core::ExecutionReport report;
    report.uuts = {uut};

    UutStepModel model;
    model.setReport(report);
    const auto uutIndex = model.index(0, 0);
    const auto stepIndex = model.index(0, UutStepModel::NameColumn, uutIndex);
    QCOMPARE(model.data(
                 uutIndex.siblingAtColumn(UutStepModel::OutcomeColumn),
                 Qt::ForegroundRole).value<QBrush>().color(),
             QColor(QStringLiteral("#62707d")));
    QCOMPARE(model.data(stepIndex.siblingAtColumn(UutStepModel::StateColumn)).toString(),
             QStringLiteral("Running"));
    QCOMPARE(model.data(stepIndex, Qt::BackgroundRole).value<QBrush>().color(),
             QColor(QStringLiteral("#fff0a6")));

    report.uuts.first().steps.first().state = PicoATE::Core::ActivationState::Created;
    model.setReport(report);
    const auto pendingUut = model.index(0, 0);
    const auto pendingStep = model.index(0, UutStepModel::StateColumn, pendingUut);
    QCOMPARE(model.data(pendingStep).toString(), QStringLiteral("Pending"));
}

void ExecutionViewModelTests::uutStepModelBuildsSingleUutPhaseLayout()
{
    using namespace PicoATE::Core;

    const auto makeStep = [](const QString& id,
                             const QString& name,
                             ExecutionPhase phase) {
        StepReport step;
        step.stepId = id;
        step.displayName = name;
        step.phase = phase;
        return step;
    };

    UutReport uut;
    uut.uutId = QStringLiteral("SN-001");
    uut.steps = {
        makeStep(QStringLiteral("open"), QStringLiteral("Open Fixture"), ExecutionPhase::Setup),
        makeStep(QStringLiteral("measure"), QStringLiteral("Measure"), ExecutionPhase::Main),
        makeStep(QStringLiteral("close"), QStringLiteral("Close Fixture"), ExecutionPhase::Cleanup)};
    ExecutionReport report;
    report.uuts = {uut};

    UutStepModel model;
    QAbstractItemModelTester tester(
        &model,
        QAbstractItemModelTester::FailureReportingMode::QtTest);
    model.setSingleUutPhaseLayout(true);
    model.setReport(report);

    QCOMPARE(model.headerData(UutStepModel::NameColumn, Qt::Horizontal).toString(),
             QStringLiteral("Phase / Step"));
    QCOMPARE(model.rowCount(), 3);
    const QStringList phaseNames = {QStringLiteral("SETUP"),
                                    QStringLiteral("MAIN"),
                                    QStringLiteral("CLEANUP")};
    for (int row = 0; row < phaseNames.size(); ++row) {
        const auto phase = model.index(row, UutStepModel::NameColumn);
        QCOMPARE(model.itemType(phase), UutStepModel::PhaseItem);
        QCOMPARE(model.data(phase).toString(), phaseNames[row]);
        QCOMPARE(model.rowCount(phase), 1);
        QCOMPARE(model.parent(model.index(0, 0, phase)), phase);
    }

    const auto measure = model.indexForStep(QStringLiteral("SN-001"),
                                            QStringLiteral("measure"));
    QVERIFY(measure.isValid());
    QCOMPARE(model.data(model.parent(measure)).toString(), QStringLiteral("MAIN"));
    QCOMPARE(model.data(measure).toString(), QStringLiteral("Measure"));
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
    const auto loopController = std::find_if(loopSteps.cbegin(), loopSteps.cend(), [](const auto& item) {
        return item.stepId == QStringLiteral("repeat-measurements");
    });
    QVERIFY(loopController != loopSteps.cend());
    QCOMPARE(loopController->children.size(), 1);
    const auto* loopStep = &loopController->children.first();
    QCOMPARE(loopStep->stepId, QStringLiteral("measure-sample"));
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

void ExecutionViewModelTests::runtimeLogModelKeepsRecentBoundedLogs()
{
    using namespace PicoATE::Core;
    RuntimeLogModel model(nullptr, 3);
    QAbstractItemModelTester tester(
        &model,
        QAbstractItemModelTester::FailureReportingMode::QtTest);

    QVector<RuntimeEvent> events;
    RuntimeEvent unrelated;
    unrelated.kind = RuntimeEventKind::NodeStateChanged;
    events.push_back(unrelated);
    for (int index = 0; index < 5; ++index) {
        RuntimeEvent log;
        log.kind = RuntimeEventKind::ModuleLog;
        log.timestampUtc = QDateTime::currentDateTimeUtc();
        log.uutId = QStringLiteral("UUT-1");
        log.nodeDisplayName = QStringLiteral("CAN Request");
        log.attemptIndex = 1;
        log.message = QStringLiteral("log-%1").arg(index);
        events.push_back(log);
    }

    model.applyRuntimeEvents(events);
    QCOMPARE(model.rowCount(), 3);
    QCOMPARE(model.droppedRowCount(), quint64(2));
    QCOMPARE(model.data(model.index(0, RuntimeLogModel::MessageColumn)).toString(),
             QStringLiteral("log-2"));
    QCOMPARE(model.data(model.index(2, RuntimeLogModel::MessageColumn)).toString(),
             QStringLiteral("log-4"));

    model.clear();
    QCOMPARE(model.rowCount(), 0);
    QCOMPARE(model.droppedRowCount(), quint64(0));
}

void ExecutionViewModelTests::runtimeTimelineModelKeepsControlEventsAndSkipsLogs()
{
    using namespace PicoATE::Core;
    RuntimeTimelineModel model(nullptr, 3);
    QAbstractItemModelTester tester(
        &model,
        QAbstractItemModelTester::FailureReportingMode::QtTest);

    QVector<RuntimeEvent> events;
    RuntimeEvent session;
    session.sequenceNumber = 1;
    session.kind = RuntimeEventKind::SessionStateChanged;
    session.executionState = ExecutionState::Running;
    session.message = QStringLiteral("session running");
    events.push_back(session);

    RuntimeEvent node;
    node.sequenceNumber = 2;
    node.kind = RuntimeEventKind::NodeStateChanged;
    node.uutId = QStringLiteral("UUT-1");
    node.nodeId = QStringLiteral("measure");
    node.nodeDisplayName = QStringLiteral("Measure");
    node.activationState = ActivationState::Running;
    events.push_back(node);

    RuntimeEvent log;
    log.sequenceNumber = 3;
    log.kind = RuntimeEventKind::ModuleLog;
    log.message = QStringLiteral("vendor log should not flood timeline");
    events.push_back(log);

    RuntimeEvent attempt;
    attempt.sequenceNumber = 4;
    attempt.kind = RuntimeEventKind::AttemptCompleted;
    attempt.uutId = QStringLiteral("UUT-1");
    attempt.nodeId = QStringLiteral("measure");
    attempt.nodeDisplayName = QStringLiteral("Measure");
    attempt.attemptIndex = 2;
    attempt.outcome = NodeOutcome::Failed;
    attempt.errorCode = QStringLiteral("LimitFail");
    attempt.details.insert(QStringLiteral("limit"), QStringLiteral("upper"));
    events.push_back(attempt);

    RuntimeEvent loop;
    loop.sequenceNumber = 5;
    loop.kind = RuntimeEventKind::LoopIterationStarted;
    loop.uutId = QStringLiteral("UUT-1");
    loop.nodeId = QStringLiteral("loop");
    loop.loopIteration.active = true;
    loop.loopIteration.loopId = QStringLiteral("repeat");
    loop.loopIteration.variableName = QStringLiteral("i");
    loop.loopIteration.iterationNumber = 2;
    loop.loopIteration.value = 1;
    events.push_back(loop);

    model.applyRuntimeEvents(events);
    QCOMPARE(model.rowCount(), 3);
    QCOMPARE(model.droppedRowCount(), quint64(1));
    QCOMPARE(model.data(model.index(0, RuntimeTimelineModel::EventColumn)).toString(),
             QString("NodeStateChanged"));
    QCOMPARE(model.data(model.index(1, RuntimeTimelineModel::StateColumn)).toString(),
             QString("Failed"));
    QVERIFY(model.data(model.index(1, RuntimeTimelineModel::DetailColumn))
                .toString()
                .contains(QStringLiteral("attempt=2")));
    QVERIFY(model.data(model.index(1, RuntimeTimelineModel::DetailColumn))
                .toString()
                .contains(QStringLiteral("LimitFail")));
    QVERIFY(model.data(model.index(2, RuntimeTimelineModel::StateColumn))
                .toString()
                .contains(QStringLiteral("#2")));
    QVERIFY(model.eventAt(0).has_value());
    QCOMPARE(model.eventAt(0)->kind, RuntimeEventKind::NodeStateChanged);
    QCOMPARE(model.rowForSequenceNumber(2), 0);
    QCOMPARE(model.rowForSequenceNumber(4), 1);
    QCOMPARE(model.rowForSequenceNumber(3), -1);
    QCOMPARE(model.rowForSequenceNumber(1), -1);

    model.clear();
    QCOMPARE(model.rowCount(), 0);
    QCOMPARE(model.droppedRowCount(), quint64(0));
}

void ExecutionViewModelTests::debugSnapshotModelFlattensRuntimeState()
{
    using namespace PicoATE::Core;
    ExecutionDebugSnapshot snapshot;
    snapshot.planId = QStringLiteral("debug-plan");
    snapshot.sequenceId = QStringLiteral("debug-sequence");
    snapshot.sequenceVersion = QStringLiteral("1.0");
    snapshot.state = ExecutionState::Paused;
    snapshot.pauseReason = DebugPauseReason::Breakpoint;
    snapshot.currentUutId = QStringLiteral("UUT-1");
    snapshot.currentNodeId = QStringLiteral("measure-voltage");
    snapshot.currentLocalPath = QStringLiteral("001/02");

    BreakpointHit hit;
    hit.breakpointId = QStringLiteral("bp-1");
    hit.uutId = QStringLiteral("UUT-1");
    hit.nodeId = QStringLiteral("measure-voltage");
    hit.localPath = QStringLiteral("001/02");
    hit.displayName = QStringLiteral("Measure Voltage");
    hit.hitCount = 3;
    snapshot.breakpoint = hit;

    DebugAttemptSnapshot attempt;
    attempt.attemptId = QStringLiteral("attempt-1");
    attempt.attemptIndex = 1;
    attempt.state = AttemptState::Completed;
    attempt.outcome = NodeOutcome::Passed;
    attempt.outputs.insert(QStringLiteral("actualVoltage"), 5.02);

    DebugNodeSnapshot node;
    node.nodeId = QStringLiteral("measure-voltage");
    node.localPath = QStringLiteral("001/02");
    node.displayName = QStringLiteral("Measure Voltage");
    node.kind = ExecNodeKind::Action;
    node.state = ActivationState::Passed;
    node.outcome = NodeOutcome::Passed;
    node.attempts = {attempt};

    DebugUutSnapshot uut;
    uut.uutId = QStringLiteral("UUT-1");
    uut.variables.insert(QStringLiteral("rxFrame"), QStringLiteral("01 02 03"));
    uut.nodes = {node};
    snapshot.uuts = {uut};

    ResourceRequirement requirement;
    requirement.resourceId = QStringLiteral("DMM1");
    requirement.mode = ResourceMode::Exclusive;
    ResourceStateSnapshot resourceState;
    resourceState.resourceId = QStringLiteral("DMM1");
    resourceState.activeLeases = {QStringLiteral("lease-1")};
    ResourceLeaseSnapshot lease;
    lease.leaseId = QStringLiteral("lease-1");
    lease.uutId = QStringLiteral("UUT-1");
    lease.nodeId = QStringLiteral("measure-voltage");
    lease.requirements = {requirement};
    ResourceWaiterSnapshot waiter;
    waiter.requestId = QStringLiteral("request-2");
    waiter.uutId = QStringLiteral("UUT-2");
    waiter.nodeId = QStringLiteral("measure-voltage");
    waiter.requirements = {requirement};
    snapshot.resources.resources = {resourceState};
    snapshot.resources.activeLeases = {lease};
    snapshot.resources.waiters = {waiter};

    BarrierRuntimeState barrier;
    barrier.id = QStringLiteral("barrier-1");
    barrier.barrierName = QStringLiteral("sync-after-measure");
    barrier.state = BarrierState::Waiting;
    barrier.expected.insert(QStringLiteral("UUT-1"));
    barrier.expected.insert(QStringLiteral("UUT-2"));
    barrier.arrived.insert(QStringLiteral("UUT-1"));
    snapshot.barriers = {barrier};

    DebugSnapshotModel model;
    QAbstractItemModelTester tester(
        &model,
        QAbstractItemModelTester::FailureReportingMode::QtTest);
    model.setSnapshot(snapshot);
    QVERIFY(model.rowCount() > 10);

    auto contains = [&model](const QString& needle) {
        for (int row = 0; row < model.rowCount(); ++row) {
            for (int column = 0; column < model.columnCount(); ++column) {
                if (model.data(model.index(row, column)).toString().contains(needle)) {
                    return true;
                }
            }
        }
        return false;
    };

    QVERIFY(contains(QStringLiteral("Breakpoint")));
    QVERIFY(contains(QStringLiteral("rxFrame")));
    QVERIFY(contains(QStringLiteral("actualVoltage")));
    QVERIFY(contains(QStringLiteral("DMM1")));
    QVERIFY(contains(QStringLiteral("request-2")));
    QVERIFY(contains(QStringLiteral("sync-after-measure")));

    model.clear();
    QCOMPARE(model.rowCount(), 0);
}

void ExecutionViewModelTests::bufferedEventSinkPreservesControlEventsDuringLogFlood()
{
    using namespace PicoATE::Core;
    BufferedRuntimeEventSink sink(3);

    for (int index = 0; index < 3; ++index) {
        RuntimeEvent log;
        log.kind = RuntimeEventKind::ModuleLog;
        log.message = QStringLiteral("log-%1").arg(index);
        sink.publish(log);
    }

    RuntimeEvent completed;
    completed.kind = RuntimeEventKind::UutCompleted;
    completed.uutId = QStringLiteral("UUT-1");
    sink.publish(completed);

    const auto events = sink.takeAll();
    QCOMPARE(events.size(), 3);
    QCOMPARE(sink.droppedEventCount(), quint64(1));
    QVERIFY(std::any_of(events.cbegin(), events.cend(), [](const RuntimeEvent& event) {
        return event.kind == RuntimeEventKind::UutCompleted;
    }));
    QCOMPARE(events.first().message, QStringLiteral("log-1"));
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
    QCOMPARE(step.phase, PicoATE::Core::ExecutionPhase::Cleanup);
    QCOMPARE(step.durationMs, 1021);
    QCOMPARE(step.attempts.first().index, 2);
    QCOMPARE(step.attempts.first().durationMs, 987);
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
    QVERIFY(text.contains(QStringLiteral("\"Step Duration Ms\"")));
    QVERIFY(text.contains(QStringLiteral("\"1021\"")));
}

void ExecutionViewModelTests::testItemReportAndRuntimeEventsPreserveHierarchy()
{
    const QString projectDir = QString::fromUtf8(PICOATE_UI_TEST_PROJECT_DIR);
    CoreExecutionService service(projectDir);
    CompileRequest compileRequest;
    compileRequest.requestId = 71;
    compileRequest.sequencePath = QFileInfo(
        projectDir + QStringLiteral("/examples/test_item_sequence.json")).absoluteFilePath();
    const auto compileResult = service.compile(compileRequest);
    QVERIFY2(compileResult.success,
             qPrintable(compileResult.diagnostics.isEmpty()
                            ? QStringLiteral("Compile failed")
                            : compileResult.diagnostics.first().message));

    BufferedRuntimeEventSink sink;
    RunRequest runRequest;
    runRequest.requestId = 72;
    const auto runResult = service.run(
        runRequest,
        std::make_shared<PicoATE::Core::StopToken>(),
        &sink);
    QVERIFY(runResult.executed);
    QVERIFY(runResult.report.completed);
    QCOMPARE(runResult.report.uuts.size(), 1);
    const auto& steps = runResult.report.uuts.first().steps;
    const auto parent = std::find_if(steps.cbegin(), steps.cend(), [](const auto& step) {
        return step.stepId == QStringLiteral("power-rail-check");
    });
    QVERIFY(parent != steps.cend());
    QCOMPARE(parent->kind, PicoATE::Core::ExecNodeKind::TestItem);
    QCOMPARE(parent->phase, PicoATE::Core::ExecutionPhase::Main);
    QCOMPARE(parent->outcome, PicoATE::Core::NodeOutcome::Passed);
    QCOMPARE(parent->children.size(), 2);

    UutStepModel liveModel;
    QAbstractItemModelTester liveTester(
        &liveModel,
        QAbstractItemModelTester::FailureReportingMode::QtTest);
    liveModel.applyRuntimeEvents(sink.takeAll());
    QCOMPARE(liveModel.rowCount(), 1);
    const auto liveUut = liveModel.index(0, 0);
    const auto liveParent = liveModel.indexForStep(
        QStringLiteral("UUT-1"), QStringLiteral("power-rail-check"));
    QVERIFY(liveParent.isValid());
    QCOMPARE(liveModel.parent(liveParent), liveUut);
    QCOMPARE(liveModel.rowCount(liveParent), 2);
    const auto liveChild = liveModel.index(0, 0, liveParent);
    QCOMPARE(liveModel.parent(liveChild), liveParent);
    QCOMPARE(liveModel.stepAt(liveChild)->stepId, QStringLiteral("measure-5v"));

    const auto serialized = PicoATE::Core::serializeExecutionReport(runResult.report);
    const auto document = QJsonDocument::fromJson(serialized);
    QCOMPARE(document.object().value(QStringLiteral("schemaVersion")).toInt(), 3);
    const auto parsed = PicoATE::Core::parseExecutionReport(serialized);
    QVERIFY(parsed.ok());
    QCOMPARE(parsed.report.uuts.first().steps.first().children.size(), 2);
    QCOMPARE(parsed.report.uuts.first().steps.first().phase,
             PicoATE::Core::ExecutionPhase::Main);

    QTemporaryDir exportDirectory;
    QVERIFY(exportDirectory.isValid());
    const auto csvPath = exportDirectory.filePath(QStringLiteral("test-item.csv"));
    const auto exportResult = ReportExporter::saveCsv(csvPath, parsed.report);
    QVERIFY2(exportResult.success, qPrintable(exportResult.errorMessage));
    QFile csvFile(csvPath);
    QVERIFY(csvFile.open(QIODevice::ReadOnly));
    const auto csvText = QString::fromUtf8(csvFile.readAll());
    QVERIFY(csvText.contains(QStringLiteral("power-rail-check")));
    QVERIFY(csvText.contains(QStringLiteral("measure-5v")));
    QVERIFY(csvText.contains(QStringLiteral("measure-3v3")));

    liveModel.setReport(parsed.report);
    const auto finalParent = liveModel.indexForStep(
        QStringLiteral("UUT-1"), QStringLiteral("power-rail-check"));
    QVERIFY(finalParent.isValid());
    QCOMPARE(liveModel.rowCount(finalParent), 2);
}


void ExecutionViewModelTests::sequenceDocumentPreservesUnknownFieldsAndSnapshots()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const auto sourcePath = directory.filePath("source.json");
    QFile source(sourcePath);
    QVERIFY(source.open(QIODevice::WriteOnly));
    source.write(R"({
        "id": "document-sequence",
        "name": "Document Sequence",
        "version": "1.0",
        "x-vendor-root": {"keep": true},
        "groups": [{
            "id": "main",
            "kind": "main",
            "x-vendor-group": 42,
            "steps": [{
                "id": "001",
                "name": "Original",
                "kind": "action",
                "x-vendor-step": "preserve"
            }]
        }]
    })");
    source.close();

    SequenceDocument document;
    QVERIFY(document.load(sourcePath));
    QVERIFY(!document.isModified());
    const auto before = document.snapshot();
    QCOMPARE(before.root.value("id").toString(), QString("document-sequence"));

    SequenceItemPath mainGroup;
    mainGroup.groupIndex = 0;
    QVERIFY(document.insertStep(mainGroup));
    QVERIFY(document.isModified());
    QCOMPARE(document.rootObject().value("groups").toArray()
                 .first().toObject().value("steps").toArray()
                 .last().toObject().value("id").toString(),
             QString("002"));
    QCOMPARE(before.root.value("groups").toArray()
                 .first().toObject().value("steps").toArray().size(),
             1);

    const auto savedPath = directory.filePath("saved.json");
    QString errorMessage;
    QVERIFY2(document.saveAs(savedPath, &errorMessage), qPrintable(errorMessage));
    QVERIFY(!document.isModified());

    QFile saved(savedPath);
    QVERIFY(saved.open(QIODevice::ReadOnly));
    const auto root = QJsonDocument::fromJson(saved.readAll()).object();
    QVERIFY(root.value("x-vendor-root").toObject().value("keep").toBool());
    const auto group = root.value("groups").toArray().first().toObject();
    QCOMPARE(group.value("x-vendor-group").toInt(), 42);
    QCOMPARE(group.value("steps").toArray().first().toObject()
                 .value("x-vendor-step").toString(),
             QString("preserve"));

    const auto badPath = directory.filePath("bad.json");
    QFile bad(badPath);
    QVERIFY(bad.open(QIODevice::WriteOnly));
    bad.write("{not-json");
    bad.close();
    QVERIFY(!document.load(badPath));
    QCOMPARE(document.rootObject().value("id").toString(), QString("document-sequence"));
    QVERIFY(!document.diagnostics().isEmpty());
}

void ExecutionViewModelTests::sequenceTreeModelBuildsHierarchyAndEditsSteps()
{
    SequenceDocument document;
    QVERIFY(document.load(
        QDir(QString::fromUtf8(PICOATE_UI_TEST_PROJECT_DIR))
            .filePath("examples/test_item_sequence.json")));

    SequenceTreeModel model(&document);
    QAbstractItemModelTester tester(
        &model, QAbstractItemModelTester::FailureReportingMode::QtTest);

    QCOMPARE(model.rowCount(), 2);
    const auto mainGroup = model.index(0, SequenceTreeModel::NameColumn);
    QCOMPARE(model.itemType(mainGroup), SequenceTreeModel::ItemType::Group);
    QCOMPARE(model.rowCount(mainGroup), 2);

    const auto testItem = model.index(0, SequenceTreeModel::NameColumn, mainGroup);
    QCOMPARE(model.itemType(testItem), SequenceTreeModel::ItemType::Step);
    QCOMPARE(model.rowCount(testItem), 2);
    QCOMPARE(model.data(model.index(0, SequenceTreeModel::KindColumn, mainGroup)).toString(),
             QString("testItem"));

    const auto testItemPath = model.pathForIndex(testItem);
    const auto enabledIndex = model.index(
        0, SequenceTreeModel::EnabledColumn, testItem);
    QVERIFY(model.setData(enabledIndex, Qt::Unchecked, Qt::CheckStateRole));
    const auto refreshedItem = model.indexForPath(testItemPath);
    const auto refreshedEnabled = model.index(
        0, SequenceTreeModel::EnabledColumn, refreshedItem);
    QCOMPARE(model.data(refreshedEnabled, Qt::CheckStateRole).toInt(),
             static_cast<int>(Qt::Unchecked));
    QVERIFY(document.insertStep(testItemPath));
    QCOMPARE(model.rowCount(model.indexForPath(testItemPath)), 3);
    auto addedPath = testItemPath;
    addedPath.stepIndices.push_back(2);
    QCOMPARE(document.objectAt(addedPath).value("id").toString(), QString("01"));

    QVERIFY(document.duplicateStep(addedPath));
    QCOMPARE(model.rowCount(model.indexForPath(testItemPath)), 4);
    auto duplicatePath = testItemPath;
    duplicatePath.stepIndices.push_back(3);
    QCOMPARE(document.objectAt(duplicatePath).value("id").toString(), QString("02"));
    QVERIFY(document.moveStep(duplicatePath, -1));
    duplicatePath.stepIndices.last() = 2;
    QCOMPARE(document.objectAt(duplicatePath).value("id").toString(), QString("02"));
    QVERIFY(document.removeStep(duplicatePath));
    QCOMPARE(model.rowCount(model.indexForPath(testItemPath)), 3);

    SequenceItemPath mainPath;
    mainPath.groupIndex = 0;
    QVERIFY(document.insertStep(mainPath));
    const auto groups = document.rootObject().value("groups").toArray();
    QCOMPARE(groups.first().toObject().value("steps").toArray()
                 .last().toObject().value("id").toString(),
             QString("001"));
}

void ExecutionViewModelTests::sequenceTreeModelTogglesTransientBreakpoints()
{
    SequenceDocument document;
    QVERIFY(document.load(
        QDir(QString::fromUtf8(PICOATE_UI_TEST_PROJECT_DIR))
            .filePath("examples/test_item_sequence.json")));

    SequenceTreeModel model(&document);
    QAbstractItemModelTester tester(
        &model, QAbstractItemModelTester::FailureReportingMode::QtTest);

    const auto mainGroup = model.index(0, SequenceTreeModel::NameColumn);
    const auto testItem = model.index(0, SequenceTreeModel::NameColumn, mainGroup);
    const auto firstChild = model.index(0, SequenceTreeModel::NameColumn, testItem);
    const auto breakpointIndex = firstChild.siblingAtColumn(
        SequenceTreeModel::BreakpointColumn);
    QVERIFY(breakpointIndex.isValid());
    QCOMPARE(model.nodePathForIndex(firstChild),
             QString("power-rail-check.measure-5v"));
    QCOMPARE(model.localPathForIndex(firstChild),
             QString("power-rail-check/measure-5v"));
    QCOMPARE(model.data(breakpointIndex, Qt::CheckStateRole).toInt(),
             static_cast<int>(Qt::Unchecked));

    QSignalSpy changedSpy(&model, &SequenceTreeModel::breakpointsChanged);
    QVERIFY(model.setData(breakpointIndex, Qt::Checked, Qt::CheckStateRole));
    QCOMPARE(changedSpy.count(), 1);
    QCOMPARE(model.data(breakpointIndex, Qt::CheckStateRole).toInt(),
             static_cast<int>(Qt::Checked));
    QCOMPARE(model.data(breakpointIndex).toString(), QString("On"));
    QCOMPARE(model.breakpointSpecs().size(), 1);
    QCOMPARE(model.breakpointSpecs().first().id, QString("ui-bp-1"));
    QCOMPARE(model.breakpointSpecs().first().address.kind,
             PicoATE::Core::BreakpointAddressKind::NodePath);
    QCOMPARE(model.breakpointSpecs().first().address.value,
             QString("power-rail-check.measure-5v"));

    model.setCurrentDebugNodePath(QStringLiteral("power-rail-check.measure-5v"));
    QCOMPARE(model.data(breakpointIndex).toString(), QString("Hit"));
    QCOMPARE(model.indexForNodePath(QStringLiteral("power-rail-check.measure-5v")),
             firstChild);

    QVERIFY(model.setData(breakpointIndex, Qt::Unchecked, Qt::CheckStateRole));
    QCOMPARE(model.breakpointSpecs().size(), 0);
}

void ExecutionViewModelTests::sequenceDocumentReplacesItemAtomically()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const auto path = directory.filePath("properties.json");
    QFile source(path);
    QVERIFY(source.open(QIODevice::WriteOnly));
    source.write(R"({
        "id": "property-sequence",
        "name": "Property Sequence",
        "x-root": {"keep": true},
        "groups": [{
            "id": "main",
            "kind": "main",
            "steps": [{
                "id": "001",
                "name": "Original",
                "kind": "action",
                "moduleId": "mock.action",
                "x-step": "keep",
                "retry": {"maxAttempts": 1, "x-policy": 17}
            }]
        }]
    })");
    source.close();

    SequenceDocument document;
    QVERIFY(document.load(path));
    SequenceItemPath stepPath;
    stepPath.groupIndex = 0;
    stepPath.stepIndices = {0};

    auto replacement = document.objectAt(stepPath);
    replacement.insert("name", "Edited Action");
    replacement.insert("inputs", QJsonObject{{"channel", 2}});
    auto retry = replacement.value("retry").toObject();
    retry.insert("maxAttempts", 3);
    replacement.insert("retry", retry);

    const auto revision = document.revision();
    QSignalSpy changedSpy(&document, &SequenceDocument::documentChanged);
    QVERIFY(document.replaceItemObject(stepPath, replacement));
    QCOMPARE(document.revision(), revision + 1);
    QCOMPARE(changedSpy.count(), 1);
    QVERIFY(document.isModified());

    const auto edited = document.objectAt(stepPath);
    QCOMPARE(edited.value("name").toString(), QString("Edited Action"));
    QCOMPARE(edited.value("x-step").toString(), QString("keep"));
    QCOMPARE(edited.value("retry").toObject().value("x-policy").toInt(), 17);
    QCOMPARE(edited.value("retry").toObject().value("maxAttempts").toInt(), 3);
    QVERIFY(document.rootObject().value("x-root").toObject().value("keep").toBool());

    PicoATE::Core::SequenceCompiler compiler;
    const auto compiled = compiler.compileJson(document.snapshot().root);
    QVERIFY2(compiled.ok(),
             qPrintable(compiled.errors.isEmpty()
                            ? QStringLiteral("Compilation failed")
                            : compiled.errors.first().message));

    auto invalidPath = stepPath;
    invalidPath.stepIndices = {9};
    QVERIFY(!document.replaceItemObject(invalidPath, replacement));
    QCOMPARE(document.revision(), revision + 1);
}

void ExecutionViewModelTests::sequenceDocumentUndoRedoTracksCleanState()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const auto sourcePath = directory.filePath("undo-source.json");
    QFile source(sourcePath);
    QVERIFY(source.open(QIODevice::WriteOnly));
    source.write(R"({
        "id": "undo-sequence",
        "name": "Undo Sequence",
        "groups": [{"id": "main", "kind": "main", "steps": [
            {"id": "001", "name": "Original", "kind": "noop", "x-step": 9}
        ]}]
    })");
    source.close();

    SequenceDocument document;
    QVERIFY(document.load(sourcePath));
    QVERIFY(document.undoStack()->isClean());
    QVERIFY(!document.isModified());
    QVERIFY(!document.undoStack()->canUndo());

    SequenceItemPath stepPath;
    stepPath.groupIndex = 0;
    stepPath.stepIndices = {0};
    auto edited = document.objectAt(stepPath);
    edited.insert("name", "Edited");
    QVERIFY(document.replaceItemObject(stepPath, edited));
    QVERIFY(document.isModified());
    QVERIFY(document.undoStack()->canUndo());
    QCOMPARE(document.undoStack()->undoText(), QString("Apply Properties"));
    QCOMPARE(document.objectAt(stepPath).value("name").toString(), QString("Edited"));

    const auto editedRevision = document.revision();
    document.undoStack()->undo();
    QCOMPARE(document.revision(), editedRevision + 1);
    QCOMPARE(document.objectAt(stepPath).value("name").toString(), QString("Original"));
    QCOMPARE(document.objectAt(stepPath).value("x-step").toInt(), 9);
    QVERIFY(!document.isModified());
    QVERIFY(document.undoStack()->canRedo());

    document.undoStack()->redo();
    QCOMPARE(document.objectAt(stepPath).value("name").toString(), QString("Edited"));
    QVERIFY(document.isModified());

    const auto savedPath = directory.filePath("undo-saved.json");
    QString errorMessage;
    QVERIFY2(document.saveAs(savedPath, &errorMessage), qPrintable(errorMessage));
    QVERIFY(document.undoStack()->isClean());
    QVERIFY(!document.isModified());

    QVERIFY(document.setItemValue(stepPath, "enabled", false));
    QVERIFY(document.isModified());
    document.undoStack()->undo();
    QVERIFY(document.undoStack()->isClean());
    QVERIFY(!document.isModified());
    QCOMPARE(document.objectAt(stepPath).value("enabled").toBool(true), true);

    SequenceItemPath groupPath;
    groupPath.groupIndex = 0;
    QVERIFY(document.insertStep(groupPath));
    QCOMPARE(document.objectAt(groupPath).value("steps").toArray().size(), 2);
    document.undoStack()->undo();
    QCOMPARE(document.objectAt(groupPath).value("steps").toArray().size(), 1);
    document.undoStack()->redo();
    QCOMPARE(document.objectAt(groupPath).value("steps").toArray().size(), 2);
}

void ExecutionViewModelTests::sequenceDocumentWrapsContiguousStepsInTestItem()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const auto path = directory.filePath(QStringLiteral("wrap.json"));
    QFile source(path);
    QVERIFY(source.open(QIODevice::WriteOnly));
    source.write(R"json({
      "id":"wrap","name":"Wrap","groups":[{
        "id":"main","kind":"main","steps":[
          {"id":"alpha","name":"Alpha","kind":"noop"},
          {"id":"beta","name":"Beta","kind":"noop"},
          {"id":"gamma","name":"Gamma","kind":"noop"}
        ]
      }]
    })json");
    source.close();

    SequenceDocument document;
    QVERIFY(document.load(path));
    const QVector<SequenceItemPath> selected = {
        SequenceItemPath{0, {0}}, SequenceItemPath{0, {1}}};
    QVERIFY(document.canWrapStepsInTestItem(selected));
    QVERIFY(!document.canWrapStepsInTestItem(
        {SequenceItemPath{0, {0}}, SequenceItemPath{0, {2}}}));

    SequenceItemPath testItemPath;
    QVERIFY(document.wrapStepsInTestItem(selected, &testItemPath));
    QVERIFY(testItemPath == (SequenceItemPath{0, {0}}));
    const auto steps = document.rootObject().value("groups").toArray().at(0)
                           .toObject().value("steps").toArray();
    QCOMPARE(steps.size(), 2);
    const auto testItem = steps.at(0).toObject();
    QCOMPARE(testItem.value("id").toString(), QString("001"));
    QCOMPARE(testItem.value("kind").toString(), QString("testItem"));
    QCOMPARE(testItem.value("steps").toArray().at(0)
                 .toObject().value("id").toString(),
             QString("alpha"));
    QCOMPARE(testItem.value("steps").toArray().at(1)
                 .toObject().value("id").toString(),
             QString("beta"));
    QCOMPARE(steps.at(1).toObject().value("id").toString(), QString("gamma"));
    QCOMPARE(document.undoStack()->undoText(), QString("Wrap Steps in TestItem"));
    document.undoStack()->undo();
    QCOMPARE(document.rootObject().value("groups").toArray().at(0)
                 .toObject().value("steps").toArray().size(),
             3);
}

void ExecutionViewModelTests::sequenceDocumentRelocatesAcrossShiftedParentPaths()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const auto path = directory.filePath(QStringLiteral("relocate.json"));
    QFile source(path);
    QVERIFY(source.open(QIODevice::WriteOnly));
    source.write(R"json({
      "id":"relocate","name":"Relocate","groups":[{
        "id":"main","kind":"main","steps":[
          {"id":"leaf","name":"Leaf","kind":"noop"},
          {"id":"first","name":"First","kind":"testItem","steps":[]},
          {"id":"second","name":"Second","kind":"testItem","steps":[
            {"id":"child","name":"Child","kind":"noop"}
          ]}
        ]
      }]
    })json");
    source.close();

    SequenceDocument document;
    QVERIFY(document.load(path));
    SequenceItemPath sourcePath{0, {0}};
    SequenceItemPath destinationParent{0, {2}};
    SequenceItemPath relocated;
    QVERIFY(document.relocateStep(
        sourcePath, destinationParent, -1, &relocated));
    QVERIFY(relocated == (SequenceItemPath{0, {1, 1}}));
    const auto topSteps = document.rootObject().value("groups").toArray().at(0)
                              .toObject().value("steps").toArray();
    QCOMPARE(topSteps.size(), 2);
    QCOMPARE(topSteps.at(1).toObject().value("id").toString(), QString("second"));
    QCOMPARE(topSteps.at(1).toObject().value("steps").toArray().at(1)
                 .toObject().value("id").toString(),
             QString("leaf"));
    document.undoStack()->undo();
    QCOMPARE(document.rootObject().value("groups").toArray().at(0)
                 .toObject().value("steps").toArray().at(0)
                 .toObject().value("id").toString(),
             QString("leaf"));

    QVERIFY(!document.relocateStep(
        SequenceItemPath{0, {2}}, SequenceItemPath{0, {2, 0}}, -1));
}

void ExecutionViewModelTests::sequenceDocumentDestructionSilencesUndoStack()
{
    QTemporaryDir temporaryDirectory;
    QVERIFY(temporaryDirectory.isValid());
    const auto path = temporaryDirectory.filePath(QStringLiteral("sequence.json"));
    QFile source(path);
    QVERIFY(source.open(QIODevice::WriteOnly));
    source.write(R"({
        "id": "lifecycle",
        "name": "Lifecycle",
        "groups": [{
            "id": "main",
            "kind": "main",
            "steps": [{"id": "001", "kind": "noop", "enabled": true}]
        }]
    })");
    source.close();

    auto document = std::make_unique<SequenceDocument>();
    QVERIFY(document->load(path));
    SequenceItemPath stepPath;
    stepPath.groupIndex = 0;
    stepPath.stepIndices = {0};
    QVERIFY(document->setItemValue(stepPath, QStringLiteral("enabled"), false));
    document->undoStack()->setClean();

    int undoNotificationsDuringDestruction = 0;
    QObject observer;
    QObject::connect(document->undoStack(), &QUndoStack::canUndoChanged,
                     &observer, [&](bool) { ++undoNotificationsDuringDestruction; });
    QObject::connect(document->undoStack(), &QUndoStack::cleanChanged,
                     &observer, [&](bool) { ++undoNotificationsDuringDestruction; });

    document.reset();
    QCOMPARE(undoNotificationsDuringDestruction, 0);
}

void ExecutionViewModelTests::sequenceDiagnosticPathsResolveNestedFields()
{
    auto target = parseSequenceDiagnosticTarget(
        "groups[2].steps[4].steps[1].errorPolicy.onFail");
    QVERIFY(target.isValid());
    QCOMPARE(target.itemPath.groupIndex, 2);
    QCOMPARE(target.itemPath.stepIndices, QVector<int>({4, 1}));
    QCOMPARE(target.fieldPath, QString("errorPolicy.onFail"));

    target = parseSequenceDiagnosticTarget(
        "groups[0].steps[3].resources[1].acquireTimeoutMs");
    QVERIFY(target.isValid());
    QCOMPARE(target.itemPath.groupIndex, 0);
    QCOMPARE(target.itemPath.stepIndices, QVector<int>({3}));
    QCOMPARE(target.fieldPath, QString("resources[1].acquireTimeoutMs"));

    target = parseSequenceDiagnosticTarget("groups[1].steps[0].loop.step");
    QVERIFY(target.isValid());
    QCOMPARE(target.itemPath.stepIndices, QVector<int>({0}));
    QCOMPARE(target.fieldPath, QString("loop.step"));

    target = parseSequenceDiagnosticTarget("groups[1].kind");
    QVERIFY(target.isValid());
    QVERIFY(target.itemPath.isGroup());
    QCOMPARE(target.fieldPath, QString("kind"));

    target = parseSequenceDiagnosticTarget("groups[1].steps[2]");
    QVERIFY(target.isValid());
    QCOMPARE(target.itemPath.stepIndices, QVector<int>({2}));
    QVERIFY(target.fieldPath.isEmpty());

    QVERIFY(!parseSequenceDiagnosticTarget("groups").isValid());
    QVERIFY(!parseSequenceDiagnosticTarget("groups[x].steps[0].id").isValid());
    QVERIFY(!parseSequenceDiagnosticTarget("groups[0].steps[-1].id").isValid());
    QVERIFY(!parseSequenceDiagnosticTarget("groups[0].steps[1").isValid());
    QVERIFY(!parseSequenceDiagnosticTarget("moduleBindings[0].program").isValid());
}

void ExecutionViewModelTests::sequenceTreeModelMovesAcrossValidParents()
{
    SequenceDocument document;
    QVERIFY(document.load(
        QDir(QString::fromUtf8(PICOATE_UI_TEST_PROJECT_DIR))
            .filePath("examples/test_item_sequence.json")));
    SequenceTreeModel model(&document);
    QAbstractItemModelTester tester(
        &model, QAbstractItemModelTester::FailureReportingMode::QtTest);

    auto mainGroup = model.index(0, SequenceTreeModel::NameColumn);
    QCOMPARE(model.rowCount(mainGroup), 2);
    auto secondTopLevel = model.index(1, SequenceTreeModel::NameColumn, mainGroup);
    std::unique_ptr<QMimeData> topLevelData(
        model.mimeData({secondTopLevel}));
    QVERIFY(topLevelData);
    QSignalSpy movedSpy(&model, &SequenceTreeModel::itemMoved);
    QVERIFY(model.dropMimeData(topLevelData.get(), Qt::MoveAction, 0, 0, mainGroup));
    QCOMPARE(movedSpy.count(), 1);

    SequenceItemPath mainPath;
    mainPath.groupIndex = 0;
    auto topLevelSteps = document.objectAt(mainPath).value("steps").toArray();
    QCOMPARE(topLevelSteps.at(0).toObject().value("id").toString(),
             QString("after-power-check"));
    QVERIFY(document.undoStack()->canUndo());
    document.undoStack()->undo();
    topLevelSteps = document.objectAt(mainPath).value("steps").toArray();
    QCOMPARE(topLevelSteps.at(0).toObject().value("id").toString(),
             QString("power-rail-check"));

    mainGroup = model.indexForPath(mainPath);
    auto testItem = model.index(0, SequenceTreeModel::NameColumn, mainGroup);
    auto child = model.index(0, SequenceTreeModel::NameColumn, testItem);
    std::unique_ptr<QMimeData> childData(model.mimeData({child}));
    QVERIFY(childData);
    QVERIFY(model.dropMimeData(
        childData.get(), Qt::MoveAction, 1, 0, mainGroup));
    auto movedTopLevel = document.objectAt(mainPath).value("steps").toArray();
    QCOMPARE(movedTopLevel.size(), 3);
    QCOMPARE(movedTopLevel.at(1).toObject().value("id").toString(),
             QString("measure-5v"));
    QCOMPARE(movedTopLevel.at(0).toObject().value("steps").toArray().size(), 1);
    document.undoStack()->undo();

    mainGroup = model.indexForPath(mainPath);
    testItem = model.index(0, SequenceTreeModel::NameColumn, mainGroup);
    std::unique_ptr<QMimeData> parentData(model.mimeData({testItem}));
    QVERIFY(parentData);
    QVERIFY(!model.dropMimeData(
        parentData.get(), Qt::MoveAction, -1, 0, testItem));

    auto ordinaryStep = model.index(1, SequenceTreeModel::NameColumn, mainGroup);
    std::unique_ptr<QMimeData> ordinaryData(model.mimeData({ordinaryStep}));
    QVERIFY(ordinaryData);
    QVERIFY(model.dropMimeData(
        ordinaryData.get(), Qt::MoveAction, -1, 0, testItem));
    auto nestedSteps = document.objectAt(mainPath).value("steps").toArray();
    QCOMPARE(nestedSteps.size(), 1);
    QCOMPARE(nestedSteps.at(0).toObject().value("steps").toArray().size(), 3);
    QCOMPARE(nestedSteps.at(0).toObject().value("steps").toArray().last()
                 .toObject().value("id").toString(),
             QString("after-power-check"));
    document.undoStack()->undo();

    mainGroup = model.indexForPath(mainPath);
    testItem = model.index(0, SequenceTreeModel::NameColumn, mainGroup);
    QVERIFY(testItem.data(Qt::ToolTipRole).toString().contains(
        QStringLiteral("inside")));

    auto secondChild = model.index(1, SequenceTreeModel::NameColumn, testItem);
    std::unique_ptr<QMimeData> siblingData(model.mimeData({secondChild}));
    QVERIFY(siblingData);
    QVERIFY(model.dropMimeData(
        siblingData.get(), Qt::MoveAction, 0, 0, testItem));
    SequenceItemPath testItemPath = model.pathForIndex(
        model.index(0, SequenceTreeModel::NameColumn,
                    model.indexForPath(mainPath)));
    const auto children = document.objectAt(testItemPath).value("steps").toArray();
    QCOMPARE(children.at(0).toObject().value("id").toString(),
             QString("measure-3v3"));
}

void ExecutionViewModelTests::stationDocumentPreservesUnknownFieldsAndUndoHistory()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const auto sourcePath = directory.filePath(QStringLiteral("station.json"));
    QFile source(sourcePath);
    QVERIFY(source.open(QIODevice::WriteOnly));
    source.write(R"({
        "stationId": "bench-01",
        "name": "Bench 01",
        "x-root": {"keep": true},
        "devices": [
            {
                "deviceId": "DMM1",
                "deviceType": "DMM",
                "driverId": "fake.dmm",
                "address": "USB::1",
                "lifetime": "Station",
                "options": {"nplc": 10},
                "x-device": 17
            },
            {
                "deviceId": "CAN1",
                "deviceType": "CAN",
                "driverId": "fake.can",
                "address": "CAN::0",
                "lifetime": "Run"
            }
        ]
    })");
    source.close();

    StationDocument document;
    QVERIFY(document.load(sourcePath));
    QCOMPARE(document.deviceCount(), 2);
    QVERIFY(document.undoStack()->isClean());
    QVERIFY(!document.isModified());
    QVERIFY(document.diagnostics().isEmpty());

    auto edited = document.deviceAt(0);
    edited.insert("driverId", "vendor.dmm");
    edited.insert("options", QJsonObject{{"nplc", 1}, {"range", 10}});
    QVERIFY(document.replaceDevice(0, edited));
    QVERIFY(document.isModified());
    QCOMPARE(document.deviceAt(0).value("x-device").toInt(), 17);
    QCOMPARE(document.rootObject().value("x-root").toObject().value("keep").toBool(), true);

    document.undoStack()->undo();
    QCOMPARE(document.deviceAt(0).value("driverId").toString(), QString("fake.dmm"));
    QVERIFY(!document.isModified());
    document.undoStack()->redo();
    QCOMPARE(document.deviceAt(0).value("driverId").toString(), QString("vendor.dmm"));

    QVERIFY(document.setDeviceValue(1, "deviceId", "DMM1"));
    QVERIFY(std::any_of(document.diagnostics().cbegin(),
                        document.diagnostics().cend(),
                        [](const UiDiagnostic& diagnostic) {
                            return diagnostic.path == QStringLiteral("devices[1].deviceId") &&
                                   diagnostic.message.contains(QStringLiteral("Duplicate"));
                        }));
    document.undoStack()->undo();
    QVERIFY(document.diagnostics().isEmpty());

    const auto savedPath = directory.filePath(QStringLiteral("saved-station.json"));
    QString errorMessage;
    QVERIFY2(document.saveAs(savedPath, &errorMessage), qPrintable(errorMessage));
    QVERIFY(document.undoStack()->isClean());
    QVERIFY(!document.isModified());
    const auto snapshot = document.snapshot();
    QVERIFY(snapshot.isValid());
    QCOMPARE(snapshot.filePath, QFileInfo(savedPath).absoluteFilePath());
    QVERIFY(snapshot.json.contains("x-device"));
}

void ExecutionViewModelTests::stationDeviceModelEditsAndReordersDevices()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const auto sourcePath = directory.filePath(QStringLiteral("station-model.json"));
    QFile source(sourcePath);
    QVERIFY(source.open(QIODevice::WriteOnly));
    source.write(R"({
        "stationId": "model-bench",
        "devices": [
            {"deviceId": "DMM1", "deviceType": "DMM", "driverId": "dmm", "enabled": true},
            {"deviceId": "CAN1", "deviceType": "CAN", "driverId": "can", "enabled": true}
        ]
    })");
    source.close();

    StationDocument document;
    QVERIFY(document.load(sourcePath));
    StationDeviceModel model(&document);
    QAbstractItemModelTester tester(&model,
        QAbstractItemModelTester::FailureReportingMode::QtTest);
    QCOMPARE(model.rowCount(), 2);
    QCOMPARE(model.data(model.index(0, StationDeviceModel::DeviceIdColumn)).toString(),
             QString("DMM1"));
    QVERIFY(model.setData(model.index(0, StationDeviceModel::EnabledColumn),
                          Qt::Unchecked,
                          Qt::CheckStateRole));
    QCOMPARE(document.deviceAt(0).value("enabled").toBool(true), false);

    QVERIFY(document.duplicateDevice(1));
    QCOMPARE(model.rowCount(), 3);
    QCOMPARE(model.data(model.index(2, StationDeviceModel::DeviceIdColumn)).toString(),
             QString("DEVICE1"));
    QVERIFY(document.moveDevice(2, -1));
    QCOMPARE(model.data(model.index(1, StationDeviceModel::DeviceIdColumn)).toString(),
             QString("DEVICE1"));
    QVERIFY(document.removeDevice(1));
    QCOMPARE(model.rowCount(), 2);
}

void ExecutionViewModelTests::coreServiceCompilesProvidedSequenceSnapshot()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const auto path = directory.filePath("sequence.json");
    QFile diskFile(path);
    QVERIFY(diskFile.open(QIODevice::WriteOnly));
    diskFile.write(R"({
        "id": "disk-sequence",
        "name": "Disk Sequence",
        "groups": [{"id": "main", "kind": "main", "steps": [
            {"id": "001", "kind": "noop"}
        ]}]
    })");
    diskFile.close();

    const QByteArray snapshot = R"({
        "id": "snapshot-sequence",
        "name": "Snapshot Sequence",
        "version": "2.0",
        "groups": [{"id": "main", "kind": "main", "steps": [
            {"id": "001", "kind": "noop"},
            {"id": "002", "kind": "noop"}
        ]}]
    })";

    const auto stationPath = directory.filePath("station.json");
    QFile stationFile(stationPath);
    QVERIFY(stationFile.open(QIODevice::WriteOnly));
    stationFile.write(R"({"stationId":"disk-station","devices":"invalid"})");
    stationFile.close();
    const QByteArray stationSnapshot = R"({
        "stationId": "snapshot-station",
        "name": "Snapshot Station",
        "devices": []
    })";

    CoreExecutionService service(directory.path());
    CompileRequest request;
    request.requestId = 77;
    request.sequencePath = path;
    request.sequenceJson = snapshot;
    request.stationPath = stationPath;
    request.stationJson = stationSnapshot;
    const auto result = service.compile(request);

    QVERIFY(result.success);
    QCOMPARE(result.requestId, quint64(77));
    QCOMPARE(result.sequenceId, QString("snapshot-sequence"));
    QCOMPARE(result.sequenceName, QString("Snapshot Sequence"));
    QCOMPARE(result.sequenceVersion, QString("2.0"));
    QCOMPARE(result.nodeCount, 2);
}

void ExecutionViewModelTests::coreServiceTestsDeviceConnectionAndFailurePaths()
{
    const QString projectDir = QString::fromUtf8(PICOATE_UI_TEST_PROJECT_DIR);
    const auto fakeHost = QFileInfo(
        projectDir + QStringLiteral(
            "/out/build/vs2022-qt6-all/src/fakeinstrumenthost/Debug/"
            "PicoATE.FakeInstrumentHost.exe")).absoluteFilePath();
    const auto mockHost = QFileInfo(
        projectDir + QStringLiteral(
            "/out/build/vs2022-qt6-all/src/mockhost/Debug/"
            "PicoATE.MockHost.exe")).absoluteFilePath();
    QVERIFY2(QFileInfo::exists(fakeHost), qPrintable(fakeHost));
    QVERIFY2(QFileInfo::exists(mockHost), qPrintable(mockHost));

    auto sequenceJson = [](const QString& driverId, const QString& program) {
        const QJsonObject root{
            {"id", "connection-test"},
            {"name", "Connection Test"},
            {"moduleBindings", QJsonArray{QJsonObject{
                 {"moduleId", driverId},
                 {"transport", "persistent-qprocess"},
                 {"program", program},
                 {"timeoutMs", 3000}}}},
            {"groups", QJsonArray{QJsonObject{
                 {"id", "main"},
                 {"kind", "main"},
                 {"steps", QJsonArray{QJsonObject{{"id", "001"}, {"kind", "noop"}}}}}}}
        };
        return QJsonDocument(root).toJson(QJsonDocument::Compact);
    };
    auto stationJson = [](const QString& driverId,
                          int delayMs = 0,
                          int exitCode = -1) {
        QJsonObject options;
        if (delayMs > 0) {
            options.insert("mockDelayMs", delayMs);
        }
        if (exitCode >= 0) {
            options.insert("mockExitCode", exitCode);
        }
        const QJsonObject root{
            {"stationId", "connection-station"},
            {"devices", QJsonArray{QJsonObject{
                 {"deviceId", "DMM1"},
                 {"deviceType", "DMM"},
                 {"driverId", driverId},
                 {"address", "USB::1"},
                 {"lifetime", "Station"},
                 {"options", options}}}}
        };
        return QJsonDocument(root).toJson(QJsonDocument::Compact);
    };

    CoreExecutionService service(projectDir);
    DeviceConnectionTestRequest request;
    request.requestId = 91;
    request.sequencePath = projectDir + QStringLiteral("/connection-sequence.json");
    request.sequenceJson = sequenceJson(QStringLiteral("fake.instrument"), fakeHost);
    request.stationPath = projectDir + QStringLiteral("/connection-station.json");
    request.stationJson = stationJson(QStringLiteral("fake.instrument"));
    request.deviceId = QStringLiteral("DMM1");
    request.timeoutMs = 1000;

    const auto passed = service.testDeviceConnection(
        request, std::make_shared<PicoATE::Core::StopToken>());
    QCOMPARE(passed.outcome, DeviceConnectionTestOutcome::Passed);
    QCOMPARE(passed.metadata.value("address").toString(), QString("USB::1"));

    request.requestId = 92;
    request.stationJson = stationJson(QStringLiteral("missing.driver"));
    const auto missingDriver = service.testDeviceConnection(
        request, std::make_shared<PicoATE::Core::StopToken>());
    QCOMPARE(missingDriver.outcome, DeviceConnectionTestOutcome::Failed);
    QCOMPARE(missingDriver.errorCode, QString("DeviceDriverNotRegistered"));

    request.requestId = 93;
    request.sequenceJson = sequenceJson(QStringLiteral("slow.driver"), mockHost);
    request.stationJson = stationJson(QStringLiteral("slow.driver"), 500);
    request.timeoutMs = 100;
    const auto timedOut = service.testDeviceConnection(
        request, std::make_shared<PicoATE::Core::StopToken>());
    QCOMPARE(timedOut.outcome, DeviceConnectionTestOutcome::TimedOut);

    request.requestId = 94;
    request.sequenceJson = sequenceJson(QStringLiteral("crash.driver"), mockHost);
    request.stationJson = stationJson(QStringLiteral("crash.driver"), 0, 7);
    request.timeoutMs = 1000;
    const auto crashed = service.testDeviceConnection(
        request, std::make_shared<PicoATE::Core::StopToken>());
    QCOMPARE(crashed.outcome, DeviceConnectionTestOutcome::Failed);
    QVERIFY(!crashed.errorMessage.isEmpty());
}
QTEST_GUILESS_MAIN(ExecutionViewModelTests)
#include "ExecutionViewModelTests.moc"
