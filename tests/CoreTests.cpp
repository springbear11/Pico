#include <QtTest/QtTest>

#include "PicoATE/Core/BarrierController.h"
#include "PicoATE/Core/DataParserModule.h"
#include "PicoATE/Core/DeviceSessionManager.h"
#include "PicoATE/Core/DeviceDiscovery.h"
#include "PicoATE/Core/DeviceTransportSession.h"
#include "PicoATE/Core/DllBridgeInvoker.h"
#include "PicoATE/Core/ExecutionGraphScheduler.h"
#include "PicoATE/Core/ExecutionControl.h"
#include "PicoATE/Core/ExecutionDebug.h"
#include "PicoATE/Core/ExecutionSession.h"
#include "PicoATE/Core/ExecutionReportJson.h"
#include "PicoATE/Core/InstrumentAdapterModules.h"
#include "PicoATE/Core/LoopController.h"
#include "PicoATE/Core/ModuleBindingRegistrar.h"
#include "PicoATE/Core/ModuleRuntime.h"
#include "PicoATE/Core/ModuleTransportJson.h"
#include "PicoATE/Core/NativeHostManifest.h"
#include "PicoATE/Core/PlanBuilder.h"
#include "PicoATE/Core/PlanCache.h"
#include "PicoATE/Core/PluginLog.h"
#include "PicoATE/Core/PersistentQProcessTransport.h"
#include "PicoATE/Core/QProcessTransport.h"
#include "PicoATE/Core/ResourceManager.h"
#include "PicoATE/Core/RuntimeVariableResolver.h"
#include "PicoATE/Core/SequenceCompiler.h"
#include "PicoATE/Core/SequenceDef.h"
#include "PicoATE/Core/StationConfig.h"
#include "PicoATE/Core/StationRunPreparation.h"
#include "PicoATE/Core/StationRuntime.h"
#include "PicoATE/Core/ValueToolsModule.h"
#include "PicoATE/Core/VariableResolver.h"

#include <algorithm>
#include <chrono>
#include <memory>
#include <thread>
#include <tuple>
#include <utility>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QProcess>
#include <QTemporaryDir>

using namespace PicoATE::Core;

namespace {

QString examplePath(const QString& fileName)
{
    QDir dir(QFileInfo(QString::fromUtf8(__FILE__)).absoluteDir());
    dir.cdUp();
    return dir.filePath(QString("examples/%1").arg(fileName));
}

QString projectRootPath()
{
    QDir dir(QFileInfo(QString::fromUtf8(__FILE__)).absoluteDir());
    dir.cdUp();
    return dir.absolutePath();
}

const StepReport* findStep(const UutReport& uut, const NodeId& stepId)
{
    const auto findRecursive = [&](const StepReport& step, const auto& findRef) -> const StepReport* {
        if (step.stepId == stepId) {
            return &step;
        }
        for (const auto& child : step.children) {
            if (const auto* found = findRef(child, findRef)) {
                return found;
            }
        }
        return nullptr;
    };
    for (const auto& step : uut.steps) {
        if (const auto* found = findRecursive(step, findRecursive)) {
            return found;
        }
    }
    return nullptr;
}

const StepReport* findStep(const QVector<StepReport>& steps, const NodeId& stepId)
{
    const auto findRecursive = [&](const StepReport& step,
                                   const auto& findRef) -> const StepReport* {
        if (step.stepId == stepId) {
            return &step;
        }
        for (const auto& child : step.children) {
            if (const auto* found = findRef(child, findRef)) {
                return found;
            }
        }
        return nullptr;
    };
    for (const auto& step : steps) {
        if (const auto* found = findRecursive(step, findRecursive)) {
            return found;
        }
    }
    return nullptr;
}

QString mockHostPath()
{
#ifdef PICOATE_MOCK_HOST_PATH
    return QFileInfo(QString::fromUtf8(PICOATE_MOCK_HOST_PATH)).absoluteFilePath();
#else
    return {};
#endif
}

QString fakeInstrumentHostPath()
{
#ifdef PICOATE_FAKE_INSTRUMENT_HOST_PATH
    return QFileInfo(QString::fromUtf8(PICOATE_FAKE_INSTRUMENT_HOST_PATH)).absoluteFilePath();
#else
    return {};
#endif
}

QString nativeHostPath()
{
#ifdef PICOATE_NATIVE_HOST_PATH
    return QFileInfo(QString::fromUtf8(PICOATE_NATIVE_HOST_PATH)).absoluteFilePath();
#else
    return {};
#endif
}

QString testDllPath()
{
#ifdef PICOATE_TEST_DLL_PATH
    return QFileInfo(QString::fromUtf8(PICOATE_TEST_DLL_PATH)).absoluteFilePath();
#else
    return {};
#endif
}

QString canDllPath()
{
#ifdef PICOATE_CAN_DLL_PATH
    return QFileInfo(QString::fromUtf8(PICOATE_CAN_DLL_PATH)).absoluteFilePath();
#else
    return {};
#endif
}

QString pythonExePath()
{
#ifdef PICOATE_PYTHON_EXE
    return QFileInfo(QString::fromUtf8(PICOATE_PYTHON_EXE)).absoluteFilePath();
#else
    return {};
#endif
}

ModuleBindingRegistrationOptions testBindingOptions(const QString& sequencePath = {})
{
    ModuleBindingRegistrationOptions options;
    options.sequenceFilePath = sequencePath;
    options.projectDir = projectRootPath();
    options.variables.insert("PICOATE_MOCK_HOST", mockHostPath());
    options.variables.insert("PICOATE_FAKE_INSTRUMENT_HOST", fakeInstrumentHostPath());
    options.variables.insert("PICOATE_NATIVE_HOST", nativeHostPath());
    options.variables.insert("PICOATE_TEST_DLL", testDllPath());
    options.variables.insert("PICOATE_CAN_DLL", canDllPath());
    if (!pythonExePath().isEmpty()) {
        options.variables.insert("PYTHON_EXE", pythonExePath());
    }
    return options;
}

class FakeDeviceDiscoveryService final : public IDeviceDiscoveryService {
public:
    DeviceDiscoveryResult discover(const DeviceDiscoveryRequest& request) override
    {
        requests.push_back(request);
        return result;
    }

    DeviceDiscoveryResult result;
    QVector<DeviceDiscoveryRequest> requests;
};

MeasurementResult makeMeasurement(const QString& name,
                                  const QVariant& value,
                                  const QString& unit,
                                  MeasurementStatus status = MeasurementStatus::Passed)
{
    MeasurementResult measurement;
    measurement.name = name;
    measurement.value = value;
    measurement.unit = unit;
    measurement.status = status;
    return measurement;
}

class EchoModule final : public IModule {
public:
    ModuleId moduleId() const override
    {
        return "test.echo";
    }

    ModuleResult execute(const ModuleFunction& functionName,
                         const ModuleExecutionContext& context) override
    {
        ModuleResult result;
        result.outputs.insert("function", functionName);
        result.outputs.insert("uutId", context.uutId);
        result.outputs.insert("attemptIndex", context.attemptIndex);
        result.outputs.insert("inputValue", context.inputs.value("value"));
        result.measurements.push_back(makeMeasurement("ECHO_VALUE",
                                                      context.inputs.value("value"),
                                                      context.inputs.value("unit").toString()));
        return result;
    }
};

class PeriodicRecordingModule final : public IModule {
public:
    ModuleId moduleId() const override
    {
        return QStringLiteral("test.periodic");
    }

    ModuleResult execute(const ModuleFunction&,
                         const ModuleExecutionContext& context) override
    {
        requestIds.push_back(context.requestId);
        callTimesMs.push_back(clock.isValid() ? clock.elapsed() : 0);
        heartbeatValues.push_back(context.inputs.value(QStringLiteral("heartbeat")));
        ModuleResult result;
        result.outputs.insert(QStringLiteral("callCount"), requestIds.size());
        if (failFirst && requestIds.size() == 1) {
            result.outcome = ModuleOutcome::Failed;
            result.errorCode = QStringLiteral("HeartbeatRejected");
            result.errorMessage = QStringLiteral("intentional periodic failure");
        }
        return result;
    }

    bool failFirst = false;
    QElapsedTimer clock;
    QVector<RequestId> requestIds;
    QVector<qint64> callTimesMs;
    QVector<QVariant> heartbeatValues;
};

class MultiUutLifecycleModule final : public IModule {
public:
    ModuleId moduleId() const override
    {
        return "test.multi-uut-lifecycle";
    }

    ModuleResult execute(const ModuleFunction& functionName,
                         const ModuleExecutionContext& context) override
    {
        calls.push_back(QString("%1:%2").arg(functionName, context.uutId));
        if (context.logSink) {
            ModuleLogRecord log;
            log.timestampUtc = QDateTime::currentDateTimeUtc();
            log.message = calls.last();
            context.logSink->publishModuleLog(log);
        }

        ModuleResult result;
        if (functionName == "fail-uut-2" && context.uutId == "UUT-2") {
            result.outcome = ModuleOutcome::Failed;
            result.errorCode = "IntentionalUutFailure";
            result.errorMessage = "UUT-2 failed for isolation verification";
        }
        return result;
    }

    QVector<QString> calls;
};

class TestItemRetryLockModule final : public IModule {
public:
    ModuleId moduleId() const override
    {
        return "test.test-item-retry-lock";
    }

    ModuleResult execute(const ModuleFunction&,
                         const ModuleExecutionContext& context) override
    {
        auto& attempt = attemptsByUut[context.uutId];
        ++attempt;
        calls.push_back(QString("%1:%2").arg(context.uutId).arg(attempt));

        ModuleResult result;
        result.outcome = ModuleOutcome::Passed;
        if (context.uutId == QStringLiteral("UUT-1") && attempt == 1) {
            result.outcome = ModuleOutcome::Failed;
            result.errorCode = QStringLiteral("IntentionalFirstAttemptFailure");
            result.errorMessage = QStringLiteral("UUT-1 fails its first TestItem attempt");
        }
        return result;
    }

    QVector<QString> calls;
    QHash<UutId, int> attemptsByUut;
};

class FakeModuleTransport final : public IModuleTransport {
public:
    ModuleTransportStatus status = ModuleTransportStatus::Ok;
    ModuleTransportResponse response;
    ModuleTransportRequest lastRequest;
    int lastTimeoutMs = 0;
    int callCount = 0;

    ModuleTransportStatus call(const ModuleTransportRequest& request,
                               ModuleTransportResponse& output,
                               int timeoutMs) override
    {
        ++callCount;
        lastRequest = request;
        lastTimeoutMs = timeoutMs;
        output = response;
        return status;
    }
};

class CollectingModuleLogSink final : public IModuleLogSink {
public:
    void publishModuleLog(const ModuleLogRecord& record) override
    {
        QMutexLocker lock(&mutex);
        logs.push_back(record);
    }

    QVector<ModuleLogRecord> records() const
    {
        QMutexLocker lock(&mutex);
        return logs;
    }

private:
    mutable QMutex mutex;
    QVector<ModuleLogRecord> logs;
};

class CollectingRuntimeEventSink final : public IRuntimeEventSink {
public:
    void publish(const RuntimeEvent& event) override
    {
        QMutexLocker lock(&mutex);
        events.push_back(event);
    }

    QVector<RuntimeEvent> records() const
    {
        QMutexLocker lock(&mutex);
        return events;
    }

private:
    mutable QMutex mutex;
    QVector<RuntimeEvent> events;
};

class OperatorPromptResponderSink final : public IRuntimeEventSink {
public:
    explicit OperatorPromptResponderSink(
        std::shared_ptr<ExecutionControl> control,
        OperatorPromptResponse judgmentResponse = OperatorPromptResponse::Passed)
        : m_control(std::move(control))
        , m_judgmentResponse(judgmentResponse)
    {
    }

    void publish(const RuntimeEvent& event) override
    {
        {
            QMutexLocker lock(&m_mutex);
            m_events.push_back(event);
        }
        if (event.kind != RuntimeEventKind::OperatorPromptRequested || !m_control) {
            return;
        }
        const auto mode = event.details.value("mode").toString();
        const auto response = mode == QStringLiteral("notice")
            ? OperatorPromptResponse::Shown
            : (mode == QStringLiteral("judgment")
                   ? m_judgmentResponse
                   : OperatorPromptResponse::Confirmed);
        m_control->operatorPrompts().respond(
            event.details.value("promptInstanceId").toString(), response);
    }

    QVector<RuntimeEvent> records() const
    {
        QMutexLocker lock(&m_mutex);
        return m_events;
    }

private:
    std::shared_ptr<ExecutionControl> m_control;
    OperatorPromptResponse m_judgmentResponse;
    mutable QMutex m_mutex;
    QVector<RuntimeEvent> m_events;
};

class PauseOnBarrierEventSink final : public IRuntimeEventSink {
public:
    explicit PauseOnBarrierEventSink(std::shared_ptr<ExecutionControl> control)
        : m_control(std::move(control))
    {
    }

    void publish(const RuntimeEvent& event) override
    {
        if (event.kind == RuntimeEventKind::BarrierWaiting && m_control && !m_requested) {
            m_requested = true;
            m_control->requestPause();
        }
    }

private:
    std::shared_ptr<ExecutionControl> m_control;
    bool m_requested = false;
};

void PICOATE_PLUGIN_CALL collectPluginLog(void* userData, const char* messageUtf8)
{
    auto* messages = static_cast<QStringList*>(userData);
    if (messages && messageUtf8) {
        messages->push_back(QString::fromUtf8(messageUtf8));
    }
}

class FakeDeviceSession final : public IDeviceSession {
public:
    explicit FakeDeviceSession(DeviceSessionConfig config)
        : m_config(std::move(config))
    {
    }

    bool failConnect = false;
    bool healthy = true;
    int connectCount = 0;
    int disconnectCount = 0;

    DeviceId deviceId() const override
    {
        return m_config.deviceId;
    }

    QString deviceType() const override
    {
        return m_config.deviceType;
    }

    DeviceConnectionState state() const override
    {
        return m_state;
    }

    bool connect(QString& errorMessage,
                 const ModuleExecutionContext* = nullptr) override
    {
        ++connectCount;
        m_state = DeviceConnectionState::Connecting;
        if (failConnect) {
            m_state = DeviceConnectionState::Error;
            errorMessage = "fake device connect failed";
            return false;
        }

        errorMessage.clear();
        m_state = DeviceConnectionState::Connected;
        return true;
    }

    void disconnect(const ModuleExecutionContext* = nullptr) override
    {
        ++disconnectCount;
        m_state = DeviceConnectionState::Disconnected;
    }

    bool isHealthy(QString& errorMessage) const override
    {
        if (m_state == DeviceConnectionState::Connected && healthy) {
            errorMessage.clear();
            return true;
        }

        errorMessage = "fake device unhealthy";
        return false;
    }

    QVariantMap metadata() const override
    {
        return {
            {"address", m_config.address},
            {"lifetime", deviceSessionLifetimeName(m_config.lifetime)},
        };
    }

private:
    DeviceSessionConfig m_config;
    DeviceConnectionState m_state = DeviceConnectionState::Disconnected;
};

class FakeDeviceSessionFactory final : public IDeviceSessionFactory {
public:
    DeviceDriverId id = "fake.dmm";
    bool failCreate = false;
    bool failConnect = false;
    int createCount = 0;
    QVector<std::shared_ptr<FakeDeviceSession>> createdSessions;

    DeviceDriverId driverId() const override
    {
        return id;
    }

    std::shared_ptr<IDeviceSession> createSession(const DeviceSessionConfig& config,
                                                  DeviceSessionError& error) override
    {
        ++createCount;
        if (failCreate) {
            error.deviceId = config.deviceId;
            error.errorCode = "FakeCreateFailed";
            error.message = "fake session creation failed";
            return {};
        }

        auto session = std::make_shared<FakeDeviceSession>(config);
        session->failConnect = failConnect;
        createdSessions.push_back(session);
        return session;
    }
};

ModuleTransportRequest makeCanDecodeRequest(double maxValue = 105.0)
{
    ModuleTransportRequest request;
    request.traceId = "trace-can-decode";
    request.moduleId = "project.can.decode";
    request.functionName = "decodeSignal";
    request.context.uutId = "uut-1";
    request.context.inputs.insert("frameId", "0x321");
    request.context.inputs.insert("rawBytes", "10 27 00 00 00 00 00 00");
    request.context.inputs.insert("signal",
                                  QVariantMap{
                                      {"name", "PackVoltage"},
                                      {"startByte", 0},
                                      {"byteLength", 2},
                                      {"byteOrder", "littleEndian"},
                                      {"signed", false},
                                      {"scale", 0.01},
                                      {"offset", 0.0},
                                      {"unit", "V"},
                                      {"min", 95.0},
                                      {"max", maxValue},
                                  });
    return request;
}

ModuleTransportRequest makeFakeInstrumentRequest(const QString& function,
                                                 const QVariantMap& inputs,
                                                 const QString& moduleId = "fake.instrument")
{
    ModuleTransportRequest request;
    request.traceId = QString("trace-%1").arg(function);
    request.moduleId = moduleId;
    request.functionName = function;
    request.context.uutId = "uut-1";
    request.context.inputs = inputs;
    return request;
}

void registerFakeInstrumentDeviceFactories(DeviceSessionManager& manager,
                                           const std::shared_ptr<IModuleTransport>& transport)
{
    manager.registerFactory(std::make_shared<TransportDeviceSessionFactory>("fake.dmm", transport, 3000));
    manager.registerFactory(std::make_shared<TransportDeviceSessionFactory>("fake.can", transport, 3000));
    manager.registerFactory(std::make_shared<TransportDeviceSessionFactory>("fake.instrument", transport, 3000));
}

} // namespace

class CoreTests : public QObject {
    Q_OBJECT

private slots:
    void deviceSessionManagerReusesConnectedSession();
    void deviceSessionManagerReportsMissingFactoryAndConnectFailure();
    void stationConfigParsesDevicesAndConfiguresSessionManager();
    void stationConfigReportsDeviceErrors();
    void stationFieldBindingPersistsResourcesAndKeepsRuntimeIndexTransient();
    void stationRunPreparationResolvesStableCanBindingIntoEffectiveSnapshot();
    void stationRunPreparationMarksUnavailableBindings();
    void deviceSessionManagerRejectsUnavailableDeviceAtOpen();
    void stationRuntimeLoadsStationConfig();
    void resourceManagerSerializesWaiters();
    void resourceManagerTreatsDeviceAndChannelAsOneHierarchy();
    void barrierControllerReleasesOnlyThroughDecision();
    void planCacheKeepsRunningPlanAlive();
    void nodeRunnerRunsRegisteredModuleAndMapsModuleResult();
    void nodeRunnerReportsMissingModule();
    void dataParserDecodesBinaryAndModbusValues();
    void dataParserDecodesRegisterText();
    void dataParserExtractsStructuredTextAndReportsFailures();
    void dataParserExtractsMultipleNamedFields();
    void valueToolsCalculateStatisticsAndConvertNumbers();
    void moduleTransportJsonSerializesRequestAndResponse();
    void pluginLogHandlesEmptyCallbackAndMixedValues();
    void variableResolverResolvesBuiltInsExplicitVariablesAndEnvironment();
    void variableResolverRecursivelyResolvesVariantContainers();
    void runtimeVariableResolverPreservesTypesAndInterpolatesStrings();
    void nodeRunnerResolvesRuntimeVariablesBeforeModuleExecution();
    void moduleBindingRegistrarReportsVariableResolutionErrors();
    void transportModuleAdapterMapsSuccessfulResponse();
    void transportModuleAdapterMapsTimeout();
    void transportModuleAdapterMapsTransportError();
    void qProcessTransportCallsMockHost();
    void qProcessTransportTimesOutMockHost();
    void qProcessTransportReportsHostExitError();
    void persistentQProcessTransportReusesHostStateAcrossCalls();
    void persistentInstrumentHostReportsHealthReconnectAndShutdown();
    void moduleRuntimeServicesInvokesTransportDeviceSession();
    void deviceSessionOpenAndCloseForwardAttemptLogContext();
    void stationPluginBindingRunsLogicalDeviceThroughNativeHost();
    void executionSessionRunsActionThroughQProcessTransport();
    void dllBridgeInvokerCallsTestDll();
    void dllBridgeInvokerStreamsPluginLogs();
    void dllBridgeInvokerReportsDllErrorCode();
    void dllBridgeInvokerReportsTimeout();
    void nativeHostManifestResolvesVariables();
    void nativeHostManifestReportsUnresolvedVariables();
    void nativeHostManifestRejectsInvalidDiagnostics();
    void qProcessTransportCallsNativeHostDll();
    void qProcessTransportStreamsOrderedNativeHostLogs();
    void qProcessTransportCapturesVendorStdoutAndStderr();
    void qProcessTransportBoundsRawVendorOutput();
    void nativeHostBatchesProtocolLogFrames();
    void qProcessTransportDiscardsVendorStdoutWhenConfigured();
    void persistentNativeHostHandlesManyShortCalls();
    void qProcessTransportDropsHighFrequencyLogsWithoutBlocking();
    void executionSessionPublishesPluginLogsWithAttemptContext();
    void qProcessTransportCallsNativeHostDllManifest();
    void qProcessTransportKillsNativeHostOnDllTimeout();
    void qProcessTransportCallsSimulatedCanDllManifest();
    void qProcessTransportReportsSimulatedCanLimitFail();
    void schedulerRetriesAndRunsCleanup();
    void executionSessionReleasesBarrierAcrossUuts();
    void executionSessionDropsFailedUutBeforeBarrier();
    void executionSessionRunsSetupCleanupOnceAndIsolatesFailedUut();
    void executionSessionKeepsResourceAcrossUutTransaction();
    void executionSessionReleasesResourceRegionAfterUutFailure();
    void sequenceCompilerRunsNestedResourceRegionAcrossUuts();
    void sequenceCompilerRunsSingleItemResourceRegionAcrossUuts();
    void singleItemTestItemResourceRegionStaysLockedAcrossRetry();
    void sequenceCompilerRejectsCrossParentResourceRegion();
    void sequenceCompilerRejectsIncompleteResourceRegion();
    void executionSessionStopRunsCleanupOnly();
    void stopTokenEscalatesAtomically();
    void executionSessionConsumesCrossThreadStopToken();
    void executionSessionWaitDoesNotBlockOtherUuts();
    void sequenceCompilerRunsCooperativePeriodicAction();
    void periodicActionWaitsForResourceRegionAndStopsWithSession();
    void periodicActionFailureMarksSessionButDoesNotStopMain();
    void sequenceCompilerRejectsUnsupportedPeriodicTaskShapes();
    void executionSessionPausesAtNodeBoundaryAndResumes();
    void executionSessionPausePreservesMultiUutBarrierState();
    void executionSessionStopWakesPausedRunAndRunsCleanup();
    void breakpointAddressResolvesNestedLocalPaths();
    void executionSessionBreakpointPausesBeforeNodeAndExposesDebugSnapshot();
    void disabledBreakpointDoesNotPause();
    void executionSessionStepIntoRunsOneNodeAndPausesAgain();
    void executionSessionStepOverLoopRunsWholeLoopBeforePausing();
    void executionSessionStepOverSuppressesBreakpointsInsideLoop();
    void sequenceDefModelsSetupMainCleanup();
    void sequenceDefDetectsDuplicateStepIds();
    void sequenceDefPreservesBarrierAndResourcePolicies();
    void errorPolicyDefMapsFailureActions();
    void errorPolicyEngineUsesOutcomeSpecificActions();
    void stationFailureHandlingOverridesNodePolicies();
    void planBuilderBuildsSetupMainCleanupPlan();
    void planBuilderRejectsDuplicateStepIds();
    void planBuilderSkipsDisabledAndBridgesCustomGroups();
    void planBuilderBuildsLoopRegion();
    void planBuilderPlanRunsInExecutionSession();
    void sequenceCompilerCompilesJsonToExecutablePlan();
    void sequenceCompilerBindsTypedVariablesPerUut();
    void sequenceCompilerRejectsInvalidSequenceVariables();
    void operatorPromptsConfirmAndCloseOnCompletedStep();
    void operatorPromptJudgmentMapsPassAndFail();
    void operatorPromptInterpolatesRuntimeValues();
    void operatorPromptWaitsForFinalRetryAttemptBeforeClosing();
    void operatorPromptWithoutResponderFailsWithoutBlocking();
    void sequenceCompilerRejectsInvalidOperatorPrompt();
    void sequenceCompilerRejectsInvalidOperatorPromptCloseTarget();
    void sequenceCompilerReportsUnsupportedStepKind();
    void sequenceCompilerReportsFieldTypeErrors();
    void sequenceCompilerReportsLoopErrors();
    void sequenceCompilerReportsUnknownFieldWarnings();
    void sequenceCompilerParsesModuleBindings();
    void sequenceCompilerReportsModuleBindingErrors();
    void sequenceCompilerRunsSimpleExampleFile();
    void sequenceCompilerRunsBasicExampleFile();
    void sequenceCompilerRunsDataParserExampleFile();
    void sequenceCompilerCompilesModbusRegisterTextReadbackExampleFile();
    void sequenceCompilerRunsCustomDisabledExampleFile();
    void sequenceCompilerRunsExternalEchoExampleFile();
    void sequenceCompilerRunsPythonEchoExampleFile();
    void sequenceCompilerRunsNativeHostDllExampleFile();
    void sequenceCompilerRunsSimulatedCanDllExampleFile();
    void sequenceCompilerRunsPersistentInstrumentExampleFile();
    void sequenceCompilerRunsDmmCanAdapterExampleFile();
    void sequenceCompilerRunsForLoopExampleFile();
    void whileLoopBreakCounterAndAggregateWorkTogether();
    void whileLoopRunsInsideTestItem();
    void whileLoopResultFeedsLaterSiblingInsideTestItem();
    void compilerRejectsForwardLoopResultInsideTestItem();
    void whileLoopBreakSkipsRemainingBody();
    void whileLoopStopsAtFiniteGuard();
    void forLoopSupportsBreakIf();
    void compilerRejectsInvalidWhileLoopAndBreakPlacement();
    void sequenceCompilerRunsTestItemExampleFile();
    void testItemStopsRemainingChildrenAfterFailure();
    void testItemChildContinueRunsRemainingChildren();
    void stationFailureHandlingControlsTestItemChildren();
    void stationContinueEvaluatesFailedDataDependency();
    void testItemRetriesWholeSubtreeAndEventuallyPasses();
    void testItemRetryResetsChildRetryBudget();
    void testItemRetryResetsNestedLoopState();
    void testItemRetryExhaustionKeepsFinalFailure();
    void compilerRejectsBarrierInsideRetryingTestItem();
    void testItemAggregatesErrorSeverity();
    void testItemStopSkipsChildrenAndRunsCleanup();
    void cleanupTestItemRunsAllChildren();
    void cleanupTestItemContinuesAfterChildError();
    void cleanupFailureContinuesBestEffortAndCompletesSession();
    void incompleteCleanupReturnsCompletedWithError();
    void nestedTestItemsAggregateDirectChildrenRecursively();
    void testItemContainingLoopAggregatesIterationFailures();
    void loopTestItemChildrenKeepSerialOrderAcrossIterations();
    void continuePolicyAdvancesAfterOrdinaryStepFailure();
    void continuePolicyAdvancesAfterTestItemFailure();
    void statementAndSequenceCallKeepDistinctRuntimeKinds();
    void reportOrdersTestItemChildrenByTopology();
    void testItemIgnoresSkippedChildrenWhenAggregating();
    void scopedStepResultsFlowAcrossTestItemsPerUut();
    void resultStoreTracksRetryAndLoopHistory();
    void compilerRejectsInvalidStepResultReferencesAndScopedKeys();
    void compilerDeduplicatesMultipleReferencesToSameStep();
    void runtimeResultLookupReportsMissingAndNonPassedSources();
    void limitNodeSupportsNumericStringAndBooleanComparisons();
    void limitSpecificationsAppearInReportBeforeExecution();
    void limitNodeDistinguishesFailuresFromConfigurationErrors();
    void limitNodePreservesConfiguredLimitsWhenVariableResolutionFails();
    void limitStepFailsReferencedParsedValueOutsideRange();
    void executionSessionJsonFailureRunsCleanup();
    void executionSessionJsonRetryAttemptsAreRecorded();
    void executionSessionReportCapturesRetryAttempts();
    void executionSessionReportFlagsErrorsWithoutTreatingSkippedAsError();
};

void CoreTests::deviceSessionManagerReusesConnectedSession()
{
    DeviceSessionManager manager;
    auto factory = std::make_shared<FakeDeviceSessionFactory>();
    QVERIFY(manager.registerFactory(factory));

    DeviceSessionConfig config;
    config.deviceId = "DMM1";
    config.deviceType = "DMM";
    config.driverId = factory->driverId();
    config.address = "USB0::0x0957::0x0607::MY59001234::INSTR";
    config.lifetime = DeviceSessionLifetime::Station;

    QVERIFY(manager.configureDevice(config));
    QCOMPARE(manager.configuredDeviceIds(), QVector<DeviceId>{"DMM1"});

    const auto first = manager.openSession("DMM1");
    QVERIFY(first.ok());
    QVERIFY(!first.reusedExisting);
    QCOMPARE(factory->createCount, 1);
    QCOMPARE(manager.stateOf("DMM1"), DeviceConnectionState::Connected);

    auto fakeSession = std::static_pointer_cast<FakeDeviceSession>(first.session);
    QCOMPARE(fakeSession->connectCount, 1);
    QCOMPARE(fakeSession->metadata().value("address").toString(), config.address);

    const auto second = manager.openSession("DMM1");
    QVERIFY(second.ok());
    QVERIFY(second.reusedExisting);
    QVERIFY(first.session == second.session);
    QCOMPARE(factory->createCount, 1);
    QCOMPARE(fakeSession->connectCount, 1);

    const auto closeError = manager.closeSession("DMM1");
    QVERIFY(!closeError.hasError());
    QCOMPARE(fakeSession->disconnectCount, 1);
    QCOMPARE(manager.stateOf("DMM1"), DeviceConnectionState::Disconnected);

    const auto third = manager.openSession("DMM1");
    QVERIFY(third.ok());
    QVERIFY(!third.reusedExisting);
    QVERIFY(third.session == first.session);
    QCOMPARE(factory->createCount, 1);
    QCOMPARE(fakeSession->connectCount, 2);

    manager.closeAll();
    QCOMPARE(fakeSession->disconnectCount, 2);
}

void CoreTests::deviceSessionManagerReportsMissingFactoryAndConnectFailure()
{
    DeviceSessionManager manager;

    DeviceSessionConfig missingFactory;
    missingFactory.deviceId = "DMM1";
    missingFactory.deviceType = "DMM";
    missingFactory.driverId = "missing.driver";
    QVERIFY(manager.configureDevice(missingFactory));

    const auto missingFactoryResult = manager.openSession("DMM1");
    QVERIFY(!missingFactoryResult.ok());
    QCOMPARE(missingFactoryResult.error.deviceId, QString("DMM1"));
    QCOMPARE(missingFactoryResult.error.errorCode, QString("DeviceDriverNotRegistered"));

    auto factory = std::make_shared<FakeDeviceSessionFactory>();
    factory->id = "fake.failing";
    factory->failConnect = true;
    QVERIFY(manager.registerFactory(factory));

    DeviceSessionConfig failingDevice;
    failingDevice.deviceId = "DMM2";
    failingDevice.deviceType = "DMM";
    failingDevice.driverId = factory->driverId();
    QVERIFY(manager.configureDevice(failingDevice));

    const auto connectResult = manager.openSession("DMM2");
    QVERIFY(!connectResult.ok());
    QCOMPARE(connectResult.error.deviceId, QString("DMM2"));
    QCOMPARE(connectResult.error.errorCode, QString("DeviceConnectFailed"));
    QCOMPARE(factory->createCount, 1);
    QCOMPARE(manager.stateOf("DMM2"), DeviceConnectionState::Error);
}

void CoreTests::stationConfigParsesDevicesAndConfiguresSessionManager()
{
    VariableResolverOptions options;
    options.sequenceFilePath = examplePath("stations/basic_station.json");
    options.projectDir = projectRootPath();
    options.variables.insert("DMM1_RESOURCE", "USB0::0x0957::0x0607::MY59001234::INSTR");

    const auto load = loadStationConfigFile(examplePath("stations/basic_station.json"), options);
    QVERIFY(load.ok());
    QCOMPARE(load.config.stationId, QString("bench-01"));
    QVERIFY(load.config.stopOnFailure);
    QVERIFY(!load.config.scanDialogEnabled);
    QVERIFY(!load.config.txtLogEnabled);
    QVERIFY(!load.config.csvReportEnabled);
    QVERIFY(!load.config.xlsxReportEnabled);
    QVERIFY(load.config.reportOutputDirectory.isEmpty());
    QCOMPARE(load.config.snLength, 12);
    QCOMPARE(load.config.snPattern, QString("BTSN*"));
    QCOMPARE(load.config.snAllowedRegex, QString("^[A-Z0-9]+$"));
    QCOMPARE(load.config.devices.size(), 2);

    const auto reportSettings = parseStationConfigJson({
        {QStringLiteral("stationId"), QStringLiteral("report-station")},
        {QStringLiteral("xlsxReportEnabled"), true},
        {QStringLiteral("devices"), QJsonArray{}}
    });
    QVERIFY(reportSettings.ok());
    QVERIFY(reportSettings.config.xlsxReportEnabled);

    const auto dmm = load.config.devices[0];
    QCOMPARE(dmm.deviceId, QString("DMM1"));
    QCOMPARE(dmm.deviceType, QString("DMM"));
    QCOMPARE(dmm.driverId, QString("fake.dmm"));
    QCOMPARE(dmm.address, QString("USB0::0x0957::0x0607::MY59001234::INSTR"));
    QCOMPARE(dmm.lifetime, DeviceSessionLifetime::Station);
    QCOMPARE(dmm.options.value("defaultFunction").toString(), QString("DCV"));
    QCOMPARE(dmm.options.value("nplc").toInt(), 10);

    const auto can = load.config.devices[1];
    QCOMPARE(can.deviceId, QString("CAN1"));
    QCOMPARE(can.deviceType, QString("CAN"));
    QCOMPARE(can.driverId, QString("fake.can"));
    QCOMPARE(can.lifetime, DeviceSessionLifetime::Run);
    QVERIFY(can.address.endsWith("virtual/can1"));
    QCOMPARE(can.options.value("channel").toInt(), 0);
    QCOMPARE(can.options.value("bitrate").toInt(), 500000);

    DeviceSessionManager manager;
    auto dmmFactory = std::make_shared<FakeDeviceSessionFactory>();
    dmmFactory->id = "fake.dmm";
    auto canFactory = std::make_shared<FakeDeviceSessionFactory>();
    canFactory->id = "fake.can";
    QVERIFY(manager.registerFactory(dmmFactory));
    QVERIFY(manager.registerFactory(canFactory));

    const auto configureErrors = configureDeviceSessions(load.config, manager);
    QVERIFY(configureErrors.isEmpty());
    QCOMPARE(manager.configuredDeviceIds(), QVector<DeviceId>({"CAN1", "DMM1"}));

    const auto dmmOpen = manager.openSession("DMM1");
    QVERIFY(dmmOpen.ok());
    auto fakeDmm = std::static_pointer_cast<FakeDeviceSession>(dmmOpen.session);
    QCOMPARE(fakeDmm->metadata().value("address").toString(), dmm.address);

    const auto canOpen = manager.openSession("CAN1");
    QVERIFY(canOpen.ok());
    QCOMPARE(canFactory->createCount, 1);
}

void CoreTests::stationConfigReportsDeviceErrors()
{
    const auto json = R"json(
    {
      "stationId": "bad-station",
      "snLength": -1,
      "snAllowedRegex": "[",
      "devices": [
        {
          "deviceId": "DMM1",
          "deviceType": "DMM",
          "driverId": "fake.dmm",
          "address": "${MISSING_DMM}",
          "lifetime": "forever"
        },
        {
          "deviceId": "DMM1",
          "deviceType": "DMM",
          "driverId": 42,
          "enabled": true
        },
        {
          "deviceId": "IGNORED",
          "driverId": "fake.disabled",
          "enabled": false
        }
      ]
    }
    )json";

    QJsonParseError parseError;
    const auto document = QJsonDocument::fromJson(QByteArray(json), &parseError);
    QCOMPARE(parseError.error, QJsonParseError::NoError);

    VariableResolverOptions options;
    options.useEnvironment = false;
    const auto result = parseStationConfigJson(document.object(), options);
    QVERIFY(!result.ok());
    QCOMPARE(result.config.devices.size(), 2);

    const auto hasErrorAt = [&](const QString& path) {
        return std::any_of(result.errors.cbegin(), result.errors.cend(), [&](const StationConfigDiagnostic& error) {
            return error.path == path;
        });
    };

    QVERIFY(hasErrorAt("devices[0].address"));
    QVERIFY(hasErrorAt("snLength"));
    QVERIFY(hasErrorAt("snAllowedRegex"));
    QVERIFY(hasErrorAt("devices[0].lifetime"));
    QVERIFY(hasErrorAt("devices[1].driverId"));
    QVERIFY(hasErrorAt("devices[1].deviceId"));
}

void CoreTests::stationFieldBindingPersistsResourcesAndKeepsRuntimeIndexTransient()
{
    QJsonObject station = QJsonDocument::fromJson(R"json({
      "devices": [
        {"deviceId":"CAN1.CH1","deviceType":"CAN","driverId":"plugin.can.gcan",
         "enabled":true,"options":{"channelIndex":0,"deviceIndex":3,"bitrate":500000}},
        {"deviceId":"CAN1.CH2","deviceType":"CAN","driverId":"plugin.can.gcan",
         "enabled":true,"options":{"channelIndex":1,"deviceIndex":3,"bitrate":1000000}},
        {"deviceId":"DMM1","deviceType":"DMM","driverId":"plugin.dmm.demo",
         "enabled":true,"address":"OLD"}
      ]
    })json").object();

    const auto devices = stationFieldDevices(station);
    QCOMPARE(devices.size(), 2);
    QCOMPARE(devices[0].logicalId, QStringLiteral("CAN1"));
    QCOMPARE(devices[0].memberDeviceIds,
             QStringList({QStringLiteral("CAN1.CH1"), QStringLiteral("CAN1.CH2")}));

    QString error;
    QVERIFY(applyStationFieldBinding(station, QStringLiteral("CAN1"),
                                     QStringLiteral("canSerial"),
                                     QStringLiteral("GCAN-SN-001"), &error));
    QVERIFY(applyStationFieldBinding(station, QStringLiteral("DMM1"),
                                     QStringLiteral("visa"),
                                     QStringLiteral("USB0::1::INSTR"), &error));
    const auto persisted = station.value(QStringLiteral("devices")).toArray();
    for (int index = 0; index < 2; ++index) {
        const auto device = persisted[index].toObject();
        const auto options = device.value(QStringLiteral("options")).toObject();
        QCOMPARE(device.value(QStringLiteral("connectionKind")).toString(),
                 QStringLiteral("canSerial"));
        QCOMPARE(device.value(QStringLiteral("resource")).toString(),
                 QStringLiteral("GCAN-SN-001"));
        QVERIFY(!options.contains(QStringLiteral("serialNumber")));
        QVERIFY(!options.contains(QStringLiteral("deviceIndex")));
    }
    QCOMPARE(persisted[2].toObject().value(QStringLiteral("connectionKind")).toString(),
             QStringLiteral("visa"));
    QCOMPARE(persisted[2].toObject().value(QStringLiteral("resource")).toString(),
             QStringLiteral("USB0::1::INSTR"));
    QVERIFY(!persisted[2].toObject().contains(QStringLiteral("address")));

    const auto effective = effectiveStationSnapshot(
        station, {{QStringLiteral("CAN1"), 7}});
    const auto effectiveDevices = effective.value(QStringLiteral("devices")).toArray();
    QCOMPARE(effectiveDevices[0].toObject().value(QStringLiteral("options"))
                 .toObject().value(QStringLiteral("deviceIndex")).toInt(), 7);
    QCOMPARE(effectiveDevices[1].toObject().value(QStringLiteral("options"))
                 .toObject().value(QStringLiteral("deviceIndex")).toInt(), 7);
    QVERIFY(!station.value(QStringLiteral("devices")).toArray()[0].toObject()
                 .value(QStringLiteral("options")).toObject()
                 .contains(QStringLiteral("deviceIndex")));
}

void CoreTests::stationRunPreparationResolvesStableCanBindingIntoEffectiveSnapshot()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    QVERIFY(QDir(directory.path()).mkpath(QStringLiteral("plugins")));
    const auto dllPath = directory.filePath(QStringLiteral("plugins/PicoATE.CAN.Test.dll"));
    QFile dll(dllPath);
    QVERIFY(dll.open(QIODevice::WriteOnly));
    dll.close();
    QFile registry(directory.filePath(QStringLiteral("plugins/PluginRegistry.json")));
    QVERIFY(registry.open(QIODevice::WriteOnly));
    registry.write(R"json({"plugins":[
      {"moduleId":"plugin.can.test","dll":"PicoATE.CAN.Test.dll"}
    ]})json");
    registry.close();

    const auto station = QJsonDocument::fromJson(R"json({
      "stationId":"station-prepare",
      "pluginRegistry":"plugins/PluginRegistry.json",
      "devices":[
        {"deviceId":"CAN1.CH1","deviceType":"CAN","driverId":"plugin.can.test",
         "enabled":true,"connectionKind":"canSerial","resource":"CAN-SN-001",
         "options":{"channelIndex":0}},
        {"deviceId":"CAN1.CH2","deviceType":"CAN","driverId":"plugin.can.test",
         "enabled":true,"connectionKind":"canSerial","resource":"CAN-SN-001",
         "options":{"channelIndex":1}}
      ]
    })json").object();

    FakeDeviceDiscoveryService discovery;
    DiscoveredDeviceResource resource;
    resource.resourceId = QStringLiteral("CAN-SN-001");
    resource.serialNumber = QStringLiteral("can-sn-001");
    resource.runtimeLocator.insert(QStringLiteral("deviceIndex"), 4);
    discovery.result.resources.push_back(resource);

    StationRunPreparationOptions options;
    options.stationFilePath = directory.filePath(QStringLiteral("StationSystem.json"));
    options.projectDir = directory.path();
    options.nativeHostProgram = directory.filePath(QStringLiteral("PicoATE.NativeHost.exe"));
    const auto prepared = StationRunPreparationService(&discovery).prepare(station, options);

    QVERIFY2(prepared.ok(), prepared.errors.isEmpty()
        ? ""
        : qPrintable(prepared.errors.first().message));
    QCOMPARE(discovery.requests.size(), 1);
    QCOMPARE(discovery.requests.first().driverId, QStringLiteral("plugin.can.test"));
    QCOMPARE(discovery.requests.first().pluginDllPath, QFileInfo(dllPath).absoluteFilePath());
    const auto effectiveDevices = prepared.effectiveStation.value(QStringLiteral("devices"))
                                      .toArray();
    QCOMPARE(effectiveDevices[0].toObject().value(QStringLiteral("options"))
                 .toObject().value(QStringLiteral("deviceIndex")).toInt(), 4);
    QCOMPARE(effectiveDevices[1].toObject().value(QStringLiteral("options"))
                 .toObject().value(QStringLiteral("deviceIndex")).toInt(), 4);
    QCOMPARE(effectiveDevices[0].toObject().value(QStringLiteral("resource")).toString(),
             QStringLiteral("CAN-SN-001"));
    QVERIFY(!effectiveDevices[0].toObject().contains(QStringLiteral("address")));
    QVERIFY(!station.value(QStringLiteral("devices")).toArray()[0].toObject()
                 .value(QStringLiteral("options")).toObject()
                 .contains(QStringLiteral("deviceIndex")));
    QCOMPARE(prepared.stationConfig.devices[0].resource,
             QStringLiteral("CAN-SN-001"));
    QCOMPARE(prepared.stationConfig.devices[0].connectionKind,
             QStringLiteral("canSerial"));
}

void CoreTests::stationRunPreparationMarksUnavailableBindings()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    QVERIFY(QDir(directory.path()).mkpath(QStringLiteral("plugins")));
    QFile dll(directory.filePath(QStringLiteral("plugins/PicoATE.CAN.Test.dll")));
    QVERIFY(dll.open(QIODevice::WriteOnly));
    dll.close();
    QFile registry(directory.filePath(QStringLiteral("plugins/PluginRegistry.json")));
    QVERIFY(registry.open(QIODevice::WriteOnly));
    registry.write(R"json({"plugins":[
      {"moduleId":"plugin.can.test","dll":"PicoATE.CAN.Test.dll"}
    ]})json");
    registry.close();

    auto station = QJsonDocument::fromJson(R"json({
      "stationId":"station-prepare",
      "pluginRegistry":"plugins/PluginRegistry.json",
      "devices":[
        {"deviceId":"CAN1.CH1","deviceType":"CAN","driverId":"plugin.can.test",
         "enabled":true,"options":{"channelIndex":0}}
      ]
    })json").object();
    StationRunPreparationOptions options;
    options.stationFilePath = directory.filePath(QStringLiteral("StationSystem.json"));
    options.projectDir = directory.path();

    FakeDeviceDiscoveryService discovery;
    auto prepared = StationRunPreparationService(&discovery).prepare(station, options);
    QVERIFY2(prepared.ok(), prepared.errors.isEmpty()
        ? ""
        : qPrintable(prepared.errors.first().message));
    QVERIFY(!prepared.warnings.isEmpty());
    auto runtimeOptions = prepared.effectiveStation.value(QStringLiteral("devices"))
                              .toArray().first().toObject()
                              .value(QStringLiteral("options")).toObject();
    QVERIFY(runtimeOptions.value(QStringLiteral("__picoateUnavailable")).toBool());
    QVERIFY(!runtimeOptions.contains(QStringLiteral("deviceIndex")));
    QVERIFY(discovery.requests.isEmpty());

    auto devices = station.value(QStringLiteral("devices")).toArray();
    auto can = devices[0].toObject();
    can.insert(QStringLiteral("connectionKind"), QStringLiteral("canSerial"));
    can.insert(QStringLiteral("resource"), QStringLiteral("MISSING-SN"));
    devices[0] = can;
    station.insert(QStringLiteral("devices"), devices);
    prepared = StationRunPreparationService(&discovery).prepare(station, options);
    QVERIFY(prepared.ok());
    QVERIFY(std::any_of(prepared.warnings.cbegin(), prepared.warnings.cend(), [](const auto& warning) {
        return warning.message.contains(QStringLiteral("was not found"));
    }));

    devices = station.value(QStringLiteral("devices")).toArray();
    auto secondChannel = devices[0].toObject();
    secondChannel.insert(QStringLiteral("deviceId"), QStringLiteral("CAN1.CH2"));
    secondChannel.insert(QStringLiteral("resource"), QStringLiteral("OTHER-SN"));
    auto secondOptions = secondChannel.value(QStringLiteral("options")).toObject();
    secondOptions.insert(QStringLiteral("channelIndex"), 1);
    secondChannel.insert(QStringLiteral("options"), secondOptions);
    devices.push_back(secondChannel);
    station.insert(QStringLiteral("devices"), devices);
    prepared = StationRunPreparationService(&discovery).prepare(station, options);
    QVERIFY(!prepared.ok());
    QVERIFY(std::any_of(prepared.errors.cbegin(), prepared.errors.cend(), [](const auto& error) {
        return error.message.contains(QStringLiteral("do not share"));
    }));

    station = QJsonDocument::fromJson(R"json({
      "stationId":"station-prepare",
      "devices":[
        {"deviceId":"DMM1","deviceType":"DMM","driverId":"test.visa",
         "connectionKind":"visa","resource":"","enabled":true}
      ]
    })json").object();
    prepared = StationRunPreparationService(&discovery).prepare(station, options);
    QVERIFY(prepared.ok());
    QCOMPARE(prepared.warnings.first().path, QStringLiteral("devices.DMM1.resource"));
    QVERIFY(prepared.stationConfig.devices.first().options
                .value(QStringLiteral("__picoateUnavailable")).toBool());
}

void CoreTests::deviceSessionManagerRejectsUnavailableDeviceAtOpen()
{
    DeviceSessionManager manager;
    DeviceSessionConfig config;
    config.deviceId = QStringLiteral("DMM1");
    config.deviceType = QStringLiteral("DMM");
    config.driverId = QStringLiteral("test.visa");
    config.options.insert(QStringLiteral("__picoateUnavailable"), true);
    config.options.insert(
        QStringLiteral("__picoateUnavailableReason"),
        QStringLiteral("Device DMM1 has no VISA resource"));

    QVERIFY(manager.configureDevice(config));
    const auto opened = manager.openSession(QStringLiteral("DMM1"));
    QVERIFY(!opened.ok());
    QCOMPARE(opened.error.errorCode, QStringLiteral("DeviceUnavailable"));
    QCOMPARE(opened.error.message,
             QStringLiteral("Device DMM1 has no VISA resource"));
}

void CoreTests::stationRuntimeLoadsStationConfig()
{
    StationRuntime runtime;

    VariableResolverOptions options;
    options.sequenceFilePath = examplePath("stations/basic_station.json");
    options.projectDir = projectRootPath();
    options.variables.insert("DMM1_RESOURCE", "USB0::0x0957::0x0607::MY59001234::INSTR");

    const auto result = runtime.loadStationConfigFile(examplePath("stations/basic_station.json"), options);
    QVERIFY(result.ok());
    QVERIFY(runtime.hasStationConfig());
    QCOMPARE(runtime.stationConfig().stationId, QString("bench-01"));
    QCOMPARE(runtime.stationConfig().devices.size(), 2);
    QCOMPARE(runtime.devices().configuredDeviceIds(), QVector<DeviceId>({"CAN1", "DMM1"}));

    const auto dmmConfig = runtime.devices().deviceConfig("DMM1");
    QVERIFY(dmmConfig.has_value());
    QCOMPARE(dmmConfig->driverId, QString("fake.dmm"));
    QCOMPARE(dmmConfig->address, QString("USB0::0x0957::0x0607::MY59001234::INSTR"));
    QCOMPARE(dmmConfig->lifetime, DeviceSessionLifetime::Station);
}

void CoreTests::resourceManagerSerializesWaiters()
{
    ResourceManager resources;

    ResourceRequirement dmm;
    dmm.resourceId = "Instrument.DMM1";
    dmm.mode = ResourceMode::Exclusive;

    ResourceRequest first;
    first.requestId = "req-1";
    first.uutId = "uut-1";
    first.frameId = "root";
    first.nodeId = "measure";
    first.requirements = {dmm};

    ResourceRequest second = first;
    second.requestId = "req-2";
    second.uutId = "uut-2";

    auto firstLease = resources.tryAcquire(first);
    QVERIFY(firstLease.has_value());

    auto secondLease = resources.tryAcquire(second);
    QVERIFY(!secondLease.has_value());
    QCOMPARE(resources.waiterCount(), 1);

    const auto snapshot = resources.snapshot();
    QCOMPARE(snapshot.waiters.size(), 1);
    QCOMPARE(snapshot.waiters.first().requestId, QString("req-2"));

    resources.release(firstLease->leaseId);
    secondLease = resources.tryAcquire(second);
    QVERIFY(secondLease.has_value());
    QCOMPARE(resources.waiterCount(), 0);
}

void CoreTests::resourceManagerTreatsDeviceAndChannelAsOneHierarchy()
{
    ResourceManager resources;
    ResourceRequirement wholeDevice;
    wholeDevice.resourceId = QStringLiteral("CAN1");
    wholeDevice.mode = ResourceMode::Exclusive;
    ResourceRequirement channel;
    channel.resourceId = QStringLiteral("CAN1.CH1");
    channel.mode = ResourceMode::Exclusive;

    ResourceRequest deviceRequest;
    deviceRequest.requestId = QStringLiteral("device-lock");
    deviceRequest.uutId = QStringLiteral("UUT-1");
    deviceRequest.frameId = QStringLiteral("root");
    deviceRequest.nodeId = QStringLiteral("heartbeat");
    deviceRequest.requirements = {wholeDevice};
    const auto deviceLease = resources.tryAcquire(deviceRequest);
    QVERIFY(deviceLease.has_value());

    ResourceRequest channelRequest = deviceRequest;
    channelRequest.requestId = QStringLiteral("channel-lock");
    channelRequest.uutId = QStringLiteral("UUT-2");
    channelRequest.nodeId = QStringLiteral("read-can");
    channelRequest.requirements = {channel};
    QVERIFY(!resources.tryAcquire(channelRequest).has_value());

    resources.release(deviceLease->leaseId);
    QVERIFY(resources.tryAcquire(channelRequest).has_value());
}

void CoreTests::barrierControllerReleasesOnlyThroughDecision()
{
    BarrierController barriers;

    BarrierNodePayload payload;
    payload.barrierName = "batch-ready";
    payload.cohortId = "batch-1";
    payload.arrivalPolicy = BarrierArrivalPolicy::WaitAll;

    const auto barrierId = barriers.createBarrier(payload, {"uut-1", "uut-2"});

    BarrierArrival first;
    first.barrierId = barrierId;
    first.uutId = "uut-1";

    auto decision = barriers.memberArrived(first);
    QVERIFY(!decision.released());
    QVERIFY(decision.releasedUuts.isEmpty());

    BarrierArrival second;
    second.barrierId = barrierId;
    second.uutId = "uut-2";

    decision = barriers.memberArrived(second);
    QVERIFY(decision.released());
    QCOMPARE(decision.releasedUuts.size(), 2);
}

void CoreTests::planCacheKeepsRunningPlanAlive()
{
    PlanCache cache;
    CompileOptions options;

    std::shared_ptr<const ExecutionPlan> runningPlan;
    {
        auto plan = cache.getOrCompile("child-sequence", options, [](const SequenceId& id,
                                                                      const CompileOptions&) {
            auto compiled = std::make_shared<ExecutionPlan>();
            compiled->id = "plan-child";
            compiled->sequenceId = id;
            return compiled;
        });
        runningPlan = plan;
    }

    cache.purgeUnused();
    QVERIFY(runningPlan != nullptr);
    QCOMPARE(runningPlan->id, QString("plan-child"));
}

void CoreTests::nodeRunnerRunsRegisteredModuleAndMapsModuleResult()
{
    NodeRunner runner;
    QVERIFY(runner.registerModule(std::make_shared<EchoModule>()));

    ExecNode node;
    node.id = "measure";
    node.kind = ExecNodeKind::Action;
    node.payload.insert("moduleId", "test.echo");
    node.payload.insert("function", "measureVoltage");
    node.payload.insert("inputs", QVariantMap{
                                      {"value", 4.999},
                                      {"unit", "V"},
                                  });

    NodeExecutionContext context;
    context.uutId = "uut-1";
    context.frameId = "root";
    context.attemptId = "attempt-1";
    context.attemptIndex = 1;

    const auto result = runner.run(node, context);
    QCOMPARE(result.nodeId, QString("measure"));
    QCOMPARE(result.outcome, NodeOutcome::Passed);
    QCOMPARE(result.outputs.value("function").toString(), QString("measureVoltage"));
    QCOMPARE(result.outputs.value("uutId").toString(), QString("uut-1"));
    QCOMPARE(result.outputs.value("attemptIndex").toInt(), 1);
    QCOMPARE(result.outputs.value("inputValue").toDouble(), 4.999);

    QCOMPARE(result.measurements.size(), 1);
    QCOMPARE(result.measurements.first().name, QString("ECHO_VALUE"));
    QCOMPARE(result.measurements.first().value.toDouble(), 4.999);
    QCOMPARE(result.measurements.first().unit, QString("V"));

    const auto measurements = result.outputs.value("measurements").toMap();
    QCOMPARE(measurements.value("name").toString(), QString("ECHO_VALUE"));
    QCOMPARE(measurements.value("value").toDouble(), 4.999);
    QCOMPARE(measurements.value("unit").toString(), QString("V"));
}

void CoreTests::nodeRunnerReportsMissingModule()
{
    NodeRunner runner;

    ExecNode node;
    node.id = "measure";
    node.kind = ExecNodeKind::Action;
    node.payload.insert("moduleId", "missing.module");

    NodeExecutionContext context;
    context.uutId = "uut-1";

    const auto result = runner.run(node, context);
    QCOMPARE(result.outcome, NodeOutcome::Error);
    QCOMPARE(result.errorCode, QString("ModuleNotFound"));
    QVERIFY(result.errorMessage.contains("missing.module"));
}

void CoreTests::dataParserDecodesBinaryAndModbusValues()
{
    DataParserModule parser;
    CollectingModuleLogSink logs;
    ModuleExecutionContext context;
    context.logSink = &logs;

    context.inputs = {
        {QStringLiteral("source"), QVariantList{0x41, 0x48, 0x00, 0x00}},
        {QStringLiteral("sourceFormat"), QStringLiteral("auto")},
        {QStringLiteral("offset"), 0},
        {QStringLiteral("length"), 4},
        {QStringLiteral("unit"), QStringLiteral("byte")},
        {QStringLiteral("dataType"), QStringLiteral("float32")},
        {QStringLiteral("byteOrder"), QStringLiteral("big")}
    };
    auto result = parser.execute(QStringLiteral("decodeBinary"), context);
    QCOMPARE(result.outcome, ModuleOutcome::Passed);
    QVERIFY(qAbs(result.outputs.value(QStringLiteral("value")).toDouble() - 12.5) < 0.0001);
    QCOMPARE(result.outputs.value(QStringLiteral("rawHex")).toString(),
             QStringLiteral("41 48 00 00"));

    context.inputs = {
        {QStringLiteral("source"), QVariantList{0x9C, 0xFF}},
        {QStringLiteral("offset"), 0},
        {QStringLiteral("length"), 2},
        {QStringLiteral("unit"), QStringLiteral("byte")},
        {QStringLiteral("dataType"), QStringLiteral("signed")},
        {QStringLiteral("byteOrder"), QStringLiteral("little")}
    };
    result = parser.execute(QStringLiteral("decodeBinary"), context);
    QCOMPARE(result.outcome, ModuleOutcome::Passed);
    QCOMPARE(result.outputs.value(QStringLiteral("value")).toLongLong(), qint64(-100));

    context.inputs = {
        {QStringLiteral("source"), QVariantList{0xB6}},
        {QStringLiteral("offset"), 1},
        {QStringLiteral("length"), 3},
        {QStringLiteral("unit"), QStringLiteral("bit")},
        {QStringLiteral("bitOrder"), QStringLiteral("lsb0")},
        {QStringLiteral("dataType"), QStringLiteral("unsigned")}
    };
    result = parser.execute(QStringLiteral("decodeBinary"), context);
    QCOMPARE(result.outcome, ModuleOutcome::Passed);
    QCOMPARE(result.outputs.value(QStringLiteral("value")).toULongLong(), quint64(3));

    context.inputs = {
        {QStringLiteral("source"), QVariantList{0x4148, 0x0000}},
        {QStringLiteral("registerOffset"), 0},
        {QStringLiteral("dataType"), QStringLiteral("float32")},
        {QStringLiteral("layout"), QStringLiteral("normal")}
    };
    result = parser.execute(QStringLiteral("decodeRegisters"), context);
    QCOMPARE(result.outcome, ModuleOutcome::Passed);
    QVERIFY(qAbs(result.outputs.value(QStringLiteral("value")).toDouble() - 12.5) < 0.0001);

    context.inputs.insert(QStringLiteral("registerOffset"), 1);
    result = parser.execute(QStringLiteral("decodeRegisters"), context);
    QCOMPARE(result.outcome, ModuleOutcome::Error);
    QCOMPARE(result.errorCode, QStringLiteral("ParserRangeError"));
    QVERIFY(logs.records().size() >= 5);
}

void CoreTests::dataParserDecodesRegisterText()
{
    DataParserModule parser;
    CollectingModuleLogSink logs;
    ModuleExecutionContext context;
    context.logSink = &logs;

    const auto toRegisters = [](QByteArray bytes) {
        if ((bytes.size() % 2) != 0) {
            bytes.push_back('\0');
        }
        QVariantList registers;
        registers.reserve(bytes.size() / 2);
        for (int index = 0; index < bytes.size(); index += 2) {
            registers.push_back(
                (static_cast<quint16>(static_cast<uchar>(bytes[index])) << 8) |
                static_cast<quint16>(static_cast<uchar>(bytes[index + 1])));
        }
        return registers;
    };

    const auto serialNumber = QStringLiteral("BTSN1234567890");
    auto padded = serialNumber.toUtf8();
    padded.append(QByteArray(48 - padded.size(), '\0'));
    context.inputs = {
        {QStringLiteral("source"), toRegisters(padded)},
        {QStringLiteral("registerOffset"), 0},
        {QStringLiteral("registerCount"), 24},
        {QStringLiteral("dataType"), QStringLiteral("asciiText")},
        {QStringLiteral("byteOrder"), QStringLiteral("highByteFirst")},
        {QStringLiteral("padding"), QStringLiteral("trimTrailingNulls")}
    };
    auto result = parser.execute(QStringLiteral("decodeRegisters"), context);
    QCOMPARE(result.outcome, ModuleOutcome::Passed);
    QCOMPARE(result.outputs.value(QStringLiteral("text")).toString(), serialNumber);
    QCOMPARE(result.outputs.value(QStringLiteral("value")).toString(), serialNumber);
    QCOMPARE(result.outputs.value(QStringLiteral("rawBytes")).toList().size(), 48);
    QCOMPARE(result.outputs.value(QStringLiteral("parsedLength")).toInt(),
             serialNumber.toUtf8().size());
    QCOMPARE(result.outputs.value(QStringLiteral("characterCount")).toInt(),
             serialNumber.size());
    QCOMPARE(result.outputs.value(QStringLiteral("registerCount")).toInt(), 24);
    QVERIFY(result.outputs.value(QStringLiteral("rawHex")).toString()
                .startsWith(QStringLiteral("42 54 53 4E")));
    QCOMPARE(result.outputs.value(QStringLiteral("dataType")).toString(),
             QStringLiteral("asciiText"));

    const auto jsonRegisterSource = QString::fromUtf8(
        QJsonDocument(QJsonArray::fromVariantList(toRegisters(padded)))
            .toJson(QJsonDocument::Compact));
    context.inputs.insert(QStringLiteral("source"), jsonRegisterSource);
    result = parser.execute(QStringLiteral("decodeRegisters"), context);
    QCOMPARE(result.outcome, ModuleOutcome::Passed);
    QCOMPARE(result.outputs.value(QStringLiteral("text")).toString(), serialNumber);
    QCOMPARE(result.outputs.value(QStringLiteral("rawRegisters")).toList().size(), 24);

    auto legacyContext = context;
    legacyContext.inputs.remove(QStringLiteral("dataType"));
    legacyContext.inputs.insert(QStringLiteral("encoding"),
                                QStringLiteral("ascii"));
    const auto legacyResult = parser.execute(
        QStringLiteral("decodeRegisterText"), legacyContext);
    QCOMPARE(legacyResult.outcome, ModuleOutcome::Passed);
    QCOMPARE(legacyResult.outputs.value(QStringLiteral("text")).toString(),
             serialNumber);

    context.inputs = {
        {QStringLiteral("source"), QVariantList{0x4241, 0x4443}},
        {QStringLiteral("registerCount"), 2},
        {QStringLiteral("byteOrder"), QStringLiteral("lowByteFirst")},
        {QStringLiteral("encoding"), QStringLiteral("ascii")},
        {QStringLiteral("padding"), QStringLiteral("keep")}
    };
    result = parser.execute(QStringLiteral("decodeRegisterText"), context);
    QCOMPARE(result.outcome, ModuleOutcome::Passed);
    QCOMPARE(result.outputs.value(QStringLiteral("text")).toString(),
             QStringLiteral("ABCD"));

    const auto utf8Text = QStringLiteral("充电枪");
    context.inputs = {
        {QStringLiteral("source"), toRegisters(utf8Text.toUtf8())},
        {QStringLiteral("registerCount"), 0},
        {QStringLiteral("dataType"), QStringLiteral("utf8Text")}
    };
    result = parser.execute(QStringLiteral("decodeRegisters"), context);
    QCOMPARE(result.outcome, ModuleOutcome::Passed);
    QCOMPARE(result.outputs.value(QStringLiteral("text")).toString(), utf8Text);
    QCOMPARE(result.outputs.value(QStringLiteral("characterCount")).toInt(),
             utf8Text.size());

    context.inputs = {
        {QStringLiteral("source"), QVariantList{0x4180}},
        {QStringLiteral("registerCount"), 1},
        {QStringLiteral("encoding"), QStringLiteral("ascii")}
    };
    result = parser.execute(QStringLiteral("decodeRegisterText"), context);
    QCOMPARE(result.outcome, ModuleOutcome::Error);
    QCOMPARE(result.errorCode, QStringLiteral("ParserConversionError"));

    context.inputs = {
        {QStringLiteral("source"), QVariantList{0xC328}},
        {QStringLiteral("registerCount"), 1},
        {QStringLiteral("encoding"), QStringLiteral("utf8")}
    };
    result = parser.execute(QStringLiteral("decodeRegisterText"), context);
    QCOMPARE(result.outcome, ModuleOutcome::Error);
    QCOMPARE(result.errorCode, QStringLiteral("ParserConversionError"));

    context.inputs = {
        {QStringLiteral("source"), QVariantList{0x4142}},
        {QStringLiteral("registerCount"), 2}
    };
    result = parser.execute(QStringLiteral("decodeRegisterText"), context);
    QCOMPARE(result.outcome, ModuleOutcome::Error);
    QCOMPARE(result.errorCode, QStringLiteral("ParserRangeError"));

    context.inputs = {
        {QStringLiteral("source"), QVariantList{0x4142}},
        {QStringLiteral("padding"), QStringLiteral("removeEveryNull")}
    };
    result = parser.execute(QStringLiteral("decodeRegisterText"), context);
    QCOMPARE(result.outcome, ModuleOutcome::Error);
    QCOMPARE(result.errorCode, QStringLiteral("ParserConfigurationError"));
    QVERIFY(logs.records().size() >= 8);
}

void CoreTests::dataParserExtractsStructuredTextAndReportsFailures()
{
    DataParserModule parser;
    CollectingModuleLogSink logs;
    ModuleExecutionContext context;
    context.logSink = &logs;

    context.inputs = {
        {QStringLiteral("source"), QStringLiteral("SN:1234567890\r\n")},
        {QStringLiteral("startMarker"), QStringLiteral("SN:")},
        {QStringLiteral("endMarker"), QStringLiteral("\\r\\n")},
        {QStringLiteral("outputType"), QStringLiteral("string")}
    };
    auto result = parser.execute(QStringLiteral("extractBetween"), context);
    QCOMPARE(result.outcome, ModuleOutcome::Passed);
    QCOMPARE(result.outputs.value(QStringLiteral("value")).toString(),
             QStringLiteral("1234567890"));

    context.inputs = {
        {QStringLiteral("source"), QStringLiteral("OK,25.75,V")},
        {QStringLiteral("delimiter"), QStringLiteral(",")},
        {QStringLiteral("fieldIndex"), 1},
        {QStringLiteral("outputType"), QStringLiteral("number")}
    };
    result = parser.execute(QStringLiteral("splitText"), context);
    QCOMPARE(result.outcome, ModuleOutcome::Passed);
    QCOMPARE(result.outputs.value(QStringLiteral("value")).toDouble(), 25.75);
    QCOMPARE(result.outputs.value(QStringLiteral("fieldCount")).toInt(), 3);

    context.inputs = {
        {QStringLiteral("source"), QStringLiteral("ID=10;ID=20")},
        {QStringLiteral("pattern"), QStringLiteral("ID=(\\d+)")},
        {QStringLiteral("captureGroup"), 1},
        {QStringLiteral("occurrence"), 2},
        {QStringLiteral("outputType"), QStringLiteral("unsigned")}
    };
    result = parser.execute(QStringLiteral("regexCapture"), context);
    QCOMPARE(result.outcome, ModuleOutcome::Passed);
    QCOMPARE(result.outputs.value(QStringLiteral("value")).toULongLong(), quint64(20));

    context.inputs.insert(QStringLiteral("occurrence"), 3);
    result = parser.execute(QStringLiteral("regexCapture"), context);
    QCOMPARE(result.outcome, ModuleOutcome::Error);
    QCOMPARE(result.errorCode, QStringLiteral("ParserPatternNotFound"));

    context.inputs = {
        {QStringLiteral("source"), QStringLiteral("NO_SERIAL_HERE")},
        {QStringLiteral("startMarker"), QStringLiteral("SN:")},
        {QStringLiteral("endMarker"), QStringLiteral("\\r\\n")}
    };
    result = parser.execute(QStringLiteral("extractBetween"), context);
    QCOMPARE(result.outcome, ModuleOutcome::Error);
    QCOMPARE(result.errorCode, QStringLiteral("ParserMarkerNotFound"));
    const auto records = logs.records();
    QVERIFY(std::any_of(records.cbegin(), records.cend(),
                        [](const ModuleLogRecord& record) {
        return record.message.contains(QStringLiteral("PARSER_ERROR"));
    }));
}

void CoreTests::dataParserExtractsMultipleNamedFields()
{
    DataParserModule parser;
    ModuleExecutionContext context;
    context.inputs = {
        {QStringLiteral("source"),
         QStringLiteral("BTSN001, 812.5, 0x1A")},
        {QStringLiteral("delimiter"), QStringLiteral(",")},
        {QStringLiteral("resultMode"), QStringLiteral("multiple")},
        {QStringLiteral("fields"), QVariantList{
             QVariantMap{{QStringLiteral("index"), 0},
                         {QStringLiteral("name"), QStringLiteral("SN1")},
                         {QStringLiteral("type"), QStringLiteral("string")}},
             QVariantMap{{QStringLiteral("index"), 1},
                         {QStringLiteral("name"), QStringLiteral("voltage")},
                         {QStringLiteral("type"), QStringLiteral("number")}},
             QVariantMap{{QStringLiteral("index"), -1},
                         {QStringLiteral("name"), QStringLiteral("status")},
                         {QStringLiteral("type"), QStringLiteral("hex")}}
         }}
    };
    auto result = parser.execute(QStringLiteral("splitText"), context);
    QCOMPARE(result.outcome, ModuleOutcome::Passed);
    const auto splitFields = result.outputs.value(QStringLiteral("fields")).toMap();
    QCOMPARE(splitFields.value(QStringLiteral("SN1")).toString(),
             QStringLiteral("BTSN001"));
    QCOMPARE(splitFields.value(QStringLiteral("voltage")).toDouble(), 812.5);
    QCOMPARE(splitFields.value(QStringLiteral("status")).toString(),
             QStringLiteral("0x1A"));
    QCOMPARE(result.outputs.value(QStringLiteral("fieldCount")).toInt(), 3);
    QCOMPARE(result.outputs.value(QStringLiteral("namedFieldCount")).toInt(), 3);

    auto outOfRangeFields = context.inputs.value(QStringLiteral("fields")).toList();
    auto outOfRange = outOfRangeFields[0].toMap();
    outOfRange.insert(QStringLiteral("index"), 3);
    outOfRangeFields[0] = outOfRange;
    const auto validSplitInputs = context.inputs;
    context.inputs.insert(QStringLiteral("fields"), outOfRangeFields);
    result = parser.execute(QStringLiteral("splitText"), context);
    QCOMPARE(result.outcome, ModuleOutcome::Error);
    QCOMPARE(result.errorCode, QStringLiteral("ParserRangeError"));
    context.inputs = validSplitInputs;

    context.inputs = {
        {QStringLiteral("source"),
         QStringLiteral("SN=BTSN009,V=799.5,OK=1")},
        {QStringLiteral("pattern"),
         QStringLiteral("SN=([^,]+),V=([0-9.]+),OK=(\\d+)")},
        {QStringLiteral("resultMode"), QStringLiteral("multiple")},
        {QStringLiteral("fields"), QVariantList{
             QVariantMap{{QStringLiteral("group"), 1},
                         {QStringLiteral("name"), QStringLiteral("serialNumber")},
                         {QStringLiteral("type"), QStringLiteral("string")}},
             QVariantMap{{QStringLiteral("group"), 2},
                         {QStringLiteral("name"), QStringLiteral("voltage")},
                         {QStringLiteral("type"), QStringLiteral("number")}},
             QVariantMap{{QStringLiteral("group"), 3},
                         {QStringLiteral("name"), QStringLiteral("ok")},
                         {QStringLiteral("type"), QStringLiteral("boolean")}}
         }}
    };
    result = parser.execute(QStringLiteral("regexCapture"), context);
    QCOMPARE(result.outcome, ModuleOutcome::Passed);
    const auto regexFields = result.outputs.value(QStringLiteral("fields")).toMap();
    QCOMPARE(regexFields.value(QStringLiteral("serialNumber")).toString(),
             QStringLiteral("BTSN009"));
    QCOMPARE(regexFields.value(QStringLiteral("voltage")).toDouble(), 799.5);
    QCOMPARE(regexFields.value(QStringLiteral("ok")).toBool(), true);

    auto invalidFields = context.inputs.value(QStringLiteral("fields")).toList();
    auto duplicate = invalidFields[1].toMap();
    duplicate.insert(QStringLiteral("name"), QStringLiteral("SERIALNUMBER"));
    invalidFields[1] = duplicate;
    context.inputs.insert(QStringLiteral("fields"), invalidFields);
    result = parser.execute(QStringLiteral("regexCapture"), context);
    QCOMPARE(result.outcome, ModuleOutcome::Error);
    QCOMPARE(result.errorCode, QStringLiteral("ParserConfigurationError"));
    QVERIFY(result.errorMessage.contains(QStringLiteral("duplicates")));

    const auto sequence = QJsonDocument::fromJson(R"json({
      "id":"named-parser","name":"Named Parser","groups":[
        {"id":"main","kind":"main","steps":[
          {"id":"parse","kind":"action","moduleId":"builtin.data-parser",
           "function":"splitText","inputs":{
             "source":"BTSN100,800.0","delimiter":",","resultMode":"multiple",
             "fields":[
               {"index":0,"name":"SN1","type":"string"},
               {"index":1,"name":"voltage","type":"number"}
             ]}},
          {"id":"check-sn","kind":"limit",
           "inputs":{"actual":"${step:parse.outputs.fields.SN1}"},
           "parameters":{"comparison":"equal","expected":"BTSN100"}},
          {"id":"check-voltage","kind":"limit",
           "inputs":{"actual":"${step:parse.outputs.fields.voltage}"},
           "parameters":{"comparison":"equal","expected":800.0}}
        ]}
      ]
    })json").object();
    SequenceCompiler compiler;
    const auto compiled = compiler.compileJson(sequence);
    QVERIFY2(compiled.ok(), qPrintable(compiled.errors.isEmpty()
        ? QStringLiteral("compile failed") : compiled.errors.first().message));
    ExecutionSession session(compiled.plan);
    session.addUut(QStringLiteral("uut-1"));
    const auto run = session.run();
    QVERIFY(run.completed);
    QCOMPARE(run.state, ExecutionState::Completed);
    QCOMPARE(run.nodeResults.size(), 3);
    QVERIFY(std::all_of(run.nodeResults.cbegin(), run.nodeResults.cend(),
                        [](const NodeResult& node) {
        return node.outcome == NodeOutcome::Passed;
    }));
}

void CoreTests::valueToolsCalculateStatisticsAndConvertNumbers()
{
    ValueToolsModule module;
    CollectingModuleLogSink logs;
    ModuleExecutionContext context;
    context.logSink = &logs;
    context.inputs.insert(QStringLiteral("values"), QVariantList{
        QVariantMap{{QStringLiteral("name"), QStringLiteral("Gun 1")},
                    {QStringLiteral("value"), 40.0}},
        QVariantMap{{QStringLiteral("name"), QStringLiteral("Gun 2")},
                    {QStringLiteral("value"), 44.5}},
        QVariantMap{{QStringLiteral("name"), QStringLiteral("Gun 3")},
                    {QStringLiteral("value"), 38.0}},
        QVariantMap{{QStringLiteral("name"), QStringLiteral("Gun 4")},
                    {QStringLiteral("value"), 41.0}}
    });

    auto result = module.execute(QStringLiteral("statistics"), context);
    QCOMPARE(result.outcome, ModuleOutcome::Passed);
    QCOMPARE(result.outputs.value(QStringLiteral("minimum")).toDouble(), 38.0);
    QCOMPARE(result.outputs.value(QStringLiteral("maximum")).toDouble(), 44.5);
    QCOMPARE(result.outputs.value(QStringLiteral("range")).toDouble(), 6.5);
    QCOMPARE(result.outputs.value(QStringLiteral("minimumName")).toString(),
             QStringLiteral("Gun 3"));
    QCOMPARE(result.outputs.value(QStringLiteral("maximumName")).toString(),
             QStringLiteral("Gun 2"));

    context.inputs = {{QStringLiteral("operation"), QStringLiteral("absoluteDifference")},
                      {QStringLiteral("a"), 44.5},
                      {QStringLiteral("b"), 38.0}};
    result = module.execute(QStringLiteral("calculate"), context);
    QCOMPARE(result.outcome, ModuleOutcome::Passed);
    QCOMPARE(result.outputs.value(QStringLiteral("value")).toDouble(), 6.5);

    context.inputs = {{QStringLiteral("text"), QStringLiteral("0xC8")},
                      {QStringLiteral("base"), 0}};
    result = module.execute(QStringLiteral("textToNumber"), context);
    QCOMPARE(result.outcome, ModuleOutcome::Passed);
    QCOMPARE(result.outputs.value(QStringLiteral("number")).toLongLong(), qint64(200));
    QCOMPARE(result.outputs.value(QStringLiteral("hexText")).toString(),
             QStringLiteral("0xC8"));

    context.inputs = {{QStringLiteral("value"), 200},
                      {QStringLiteral("base"), 16},
                      {QStringLiteral("width"), 4},
                      {QStringLiteral("prefix"), false},
                      {QStringLiteral("uppercase"), true}};
    result = module.execute(QStringLiteral("numberToText"), context);
    QCOMPARE(result.outcome, ModuleOutcome::Passed);
    QCOMPARE(result.outputs.value(QStringLiteral("text")).toString(),
             QStringLiteral("00C8"));
    QVERIFY(logs.records().size() >= 4);
}

void CoreTests::moduleTransportJsonSerializesRequestAndResponse()
{
    ModuleTransportRequest request;
    request.requestId = "request-17";
    request.traceId = "trace-1";
    request.moduleId = "external.dmm";
    request.functionName = "measureVoltage";
    request.context.uutId = "uut-1";
    request.context.frameId = "root";
    request.context.attemptId = "attempt-2";
    request.context.requestId = request.requestId;
    request.context.attemptIndex = 2;
    request.context.inputs.insert("range", "10V");
    request.context.parameters.insert("aperture", "NPLC1");
    request.context.variables.insert("station", "A");

    const auto requestJson = moduleTransportRequestToJson(request);
    QCOMPARE(requestJson.value("requestId").toString(), QString("request-17"));
    QCOMPARE(requestJson.value("traceId").toString(), QString("trace-1"));
    QCOMPARE(requestJson.value("moduleId").toString(), QString("external.dmm"));
    QCOMPARE(requestJson.value("function").toString(), QString("measureVoltage"));

    const auto contextJson = requestJson.value("context").toObject();
    QCOMPARE(contextJson.value("requestId").toString(), QString("request-17"));
    QCOMPARE(contextJson.value("uutId").toString(), QString("uut-1"));
    QCOMPARE(contextJson.value("attemptIndex").toInt(), 2);
    QCOMPARE(contextJson.value("inputs").toObject().value("range").toString(), QString("10V"));
    QCOMPARE(contextJson.value("parameters").toObject().value("aperture").toString(), QString("NPLC1"));
    QCOMPARE(contextJson.value("variables").toObject().value("station").toString(), QString("A"));

    const auto parsedRequest = moduleTransportRequestFromJson(requestJson);
    QCOMPARE(parsedRequest.requestId, request.requestId);
    QCOMPARE(parsedRequest.context.requestId, request.requestId);

    ModuleTransportResponse response;
    response.outcome = ModuleOutcome::Failed;
    response.outputs.insert("actualVoltage", 4.999);
    response.measurements.push_back(makeMeasurement("VOUT", 4.999, "V", MeasurementStatus::Failed));
    response.errorCode = "LimitFail";
    response.errorMessage = "Voltage is out of range";
    response.diagnostics.insert("fixture", QVariantMap{{"elapsedMs", 12}});

    const auto responseJson = moduleTransportResponseToJson(response);
    QCOMPARE(responseJson.value("outcome").toString(), QString("Failed"));

    const auto parsed = moduleTransportResponseFromJson(responseJson);
    QCOMPARE(parsed.outcome, ModuleOutcome::Failed);
    QCOMPARE(parsed.outputs.value("actualVoltage").toDouble(), 4.999);
    QCOMPARE(parsed.measurements.size(), 1);
    QCOMPARE(parsed.measurements.first().name, QString("VOUT"));
    QCOMPARE(parsed.measurements.first().value.toDouble(), 4.999);
    QCOMPARE(parsed.measurements.first().unit, QString("V"));
    QCOMPARE(parsed.measurements.first().status, MeasurementStatus::Failed);
    QCOMPARE(parsed.errorCode, QString("LimitFail"));
    QCOMPARE(parsed.errorMessage, QString("Voltage is out of range"));
    QCOMPARE(parsed.diagnostics.value("fixture").toMap().value("elapsedMs").toInt(), 12);

    ModuleLogRecord firstLog;
    firstLog.sourceSequence = 1;
    firstLog.timestampUtc = QDateTime::currentDateTimeUtc();
    firstLog.message = "first";
    auto secondLog = firstLog;
    secondLog.sourceSequence = 2;
    secondLog.message = "second";
    const auto batch = moduleProtocolMessageFromJson(
        moduleLogBatchMessageToJson("trace-1", {firstLog, secondLog}));
    QCOMPARE(batch.kind, ModuleProtocolMessageKind::LogBatch);
    QCOMPARE(batch.logs.size(), 2);
    QCOMPARE(batch.logs[1].message, QString("second"));

    QJsonObject emptyBatchJson;
    emptyBatchJson.insert("type", "moduleLogBatch");
    emptyBatchJson.insert("logs", QJsonArray{});
    const auto emptyBatch = moduleProtocolMessageFromJson(emptyBatchJson);
    QCOMPARE(emptyBatch.kind, ModuleProtocolMessageKind::Invalid);
    QVERIFY(emptyBatch.errorMessage.contains("at least one"));
}

void CoreTests::pluginLogHandlesEmptyCallbackAndMixedValues()
{
    PicoATE::Plugin::setLogSink(nullptr, nullptr);
    PicoATE_Log("callback is optional");
    PicoATE_Log("ignored value={}", 42);

    QStringList messages;
    PicoATE::Plugin::setLogSink(&collectPluginLog, &messages);
    PicoATE_Log("CAN connected");
    PicoATE_Log("send={} count={} voltage={:.2f}",
                std::string("01 02 03"),
                3,
                5.0123);
    PicoATE::Plugin::setLogSink(nullptr, nullptr);

    QCOMPARE(messages.size(), 2);
    QCOMPARE(messages[0], QString("CAN connected"));
    QCOMPARE(messages[1], QString("send=01 02 03 count=3 voltage=5.01"));
}

void CoreTests::variableResolverResolvesBuiltInsExplicitVariablesAndEnvironment()
{
    qputenv("PICOATE_RESOLVER_TEST", "env-value");

    VariableResolverOptions options;
    options.sequenceFilePath = examplePath("simple_sequence.json");
    options.projectDir = projectRootPath();
    options.variables.insert("BIN_DIR", "${PROJECT_DIR}/bin");
    options.variables.insert("EMPTY_VALUE", "");

    VariableResolver resolver(options);
    QVector<VariableResolutionError> errors;
    const auto resolved = resolver.resolveString(
        "${PROJECT_DIR}|${SEQUENCE_DIR}|${BIN_DIR}|${PICOATE_RESOLVER_TEST}|${EMPTY_VALUE}",
        errors,
        "config.path");

    QVERIFY(errors.isEmpty());
    QVERIFY(resolved.contains(projectRootPath()));
    QVERIFY(resolved.contains(QFileInfo(examplePath("simple_sequence.json")).absoluteDir().absolutePath()));
    QVERIFY(resolved.contains(projectRootPath() + "/bin"));
    QVERIFY(resolved.contains("env-value"));

    qunsetenv("PICOATE_RESOLVER_TEST");
}

void CoreTests::variableResolverRecursivelyResolvesVariantContainers()
{
    VariableResolverOptions options;
    options.sequenceFilePath = examplePath("simple_sequence.json");
    options.projectDir = projectRootPath();
    options.variables.insert("DLL_NAME", "ProjectCan.dll");
    options.variables.insert("DLL_PATH", "${PROJECT_DIR}/modules/${DLL_NAME}");

    QVariantMap nested;
    nested.insert("sequenceDir", "${SEQUENCE_DIR}");
    nested.insert("unchangedNumber", 42);

    QVariantList arguments;
    arguments.push_back("--dll");
    arguments.push_back("${DLL_PATH}");
    arguments.push_back(nested);

    QVariantMap config;
    config.insert("program", "${PROJECT_DIR}/bin/PicoATE.NativeHost.exe");
    config.insert("arguments", arguments);
    config.insert("timeoutMs", 3000);

    VariableResolver resolver(options);
    QVector<VariableResolutionError> errors;
    const auto resolved = resolver.resolveMap(config, errors, "nativeHost");

    QVERIFY(errors.isEmpty());
    QCOMPARE(resolved.value("program").toString(), projectRootPath() + "/bin/PicoATE.NativeHost.exe");

    const auto resolvedArguments = resolved.value("arguments").toList();
    QCOMPARE(resolvedArguments.size(), 3);
    QCOMPARE(resolvedArguments[0].toString(), QString("--dll"));
    QCOMPARE(resolvedArguments[1].toString(), projectRootPath() + "/modules/ProjectCan.dll");

    const auto resolvedNested = resolvedArguments[2].toMap();
    QCOMPARE(resolvedNested.value("sequenceDir").toString(),
             QFileInfo(examplePath("simple_sequence.json")).absoluteDir().absolutePath());
    QCOMPARE(resolvedNested.value("unchangedNumber").toInt(), 42);
}

void CoreTests::runtimeVariableResolverPreservesTypesAndInterpolatesStrings()
{
    RuntimeVariableContext context;
    context.uutId = "uut-1";
    context.frameId = "root";
    context.attemptId = "attempt-2";
    context.attemptIndex = 1;
    context.requestId = QStringLiteral("periodic-request-8");
    context.periodicInvocation = true;
    context.periodicIndex = 7;
    context.periodicCounter = 200;
    context.variables.insert("channelIndex", 3);
    context.variables.insert("loop.index", 2);
    context.variables.insert("loop.value", 42);

    QVariantMap limits;
    limits.insert("enabled", true);
    context.variables.insert("limits", limits);

    QVariantMap input;
    input.insert("channel", "${var.channelIndex}");
    input.insert("label", "CH${var.channelIndex}-${uut.id}-${attempt.number}");
    input.insert("loopIndex", "${loop.index}");
    input.insert("loopValue", "${loop.value}");
    input.insert("limitEnabled", "${var.limits.enabled}");
    input.insert("periodicIndex", "${periodic.index}");
    input.insert("periodicNumber", "${periodic.number}");
    input.insert("periodicCounter", "${periodic.counter}");
    input.insert("periodicRequestId", "${periodic.requestId}");

    RuntimeVariableResolver resolver(context);
    QVector<VariableResolutionError> errors;
    const auto resolved = resolver.resolveMap(input, errors, "inputs");

    QVERIFY(errors.isEmpty());
    QCOMPARE(resolved.value("channel").metaType().id(), QMetaType::Int);
    QCOMPARE(resolved.value("channel").toInt(), 3);
    QCOMPARE(resolved.value("label").toString(), QString("CH3-uut-1-2"));
    QCOMPARE(resolved.value("loopIndex").toInt(), 2);
    QCOMPARE(resolved.value("loopValue").toInt(), 42);
    QCOMPARE(resolved.value("limitEnabled").metaType().id(), QMetaType::Bool);
    QCOMPARE(resolved.value("limitEnabled").toBool(), true);
    QCOMPARE(resolved.value("periodicIndex").toInt(), 7);
    QCOMPARE(resolved.value("periodicNumber").toInt(), 8);
    QCOMPARE(resolved.value("periodicCounter").toLongLong(), qint64(200));
    QCOMPARE(resolved.value("periodicRequestId").toString(),
             QStringLiteral("periodic-request-8"));
}

void CoreTests::nodeRunnerResolvesRuntimeVariablesBeforeModuleExecution()
{
    NodeRunner runner;

    ExecNode node;
    node.id = "measure-channel";
    node.kind = ExecNodeKind::Action;
    node.payload.insert("moduleId", "mock.measurement");
    node.payload.insert("function", "measureVoltage");
    node.payload.insert("inputs",
                        QVariantMap{
                            {"outputs",
                             QVariantMap{
                                 {"channel", "${var.channelIndex}"},
                                 {"label", "CH${var.channelIndex}"},
                                 {"uut", "${uut.id}"},
                                 {"attemptNumber", "${attempt.number}"},
                             }},
                            {"measurements",
                             QVariantMap{
                                 {"name", "CH${var.channelIndex}_VOLTAGE"},
                                 {"value", "${var.channelIndex}"},
                                 {"unit", "V"},
                             }},
                        });

    NodeExecutionContext context;
    context.uutId = "uut-1";
    context.frameId = "root";
    context.attemptId = "attempt-2";
    context.attemptIndex = 1;
    context.variables.insert("channelIndex", 3);

    const auto result = runner.run(node, context);

    QCOMPARE(result.outcome, NodeOutcome::Passed);
    QCOMPARE(result.outputs.value("channel").toInt(), 3);
    QCOMPARE(result.outputs.value("label").toString(), QString("CH3"));
    QCOMPARE(result.outputs.value("uut").toString(), QString("uut-1"));
    QCOMPARE(result.outputs.value("attemptNumber").toInt(), 2);

    const auto measurements = result.outputs.value("measurements").toMap();
    QCOMPARE(measurements.value("name").toString(), QString("CH3_VOLTAGE"));
    QCOMPARE(measurements.value("value").toInt(), 3);
    QCOMPARE(measurements.value("unit").toString(), QString("V"));
}

void CoreTests::moduleBindingRegistrarReportsVariableResolutionErrors()
{
    SequenceDef sequence;
    sequence.id = "binding-errors";
    sequence.name = "Binding Errors";

    ModuleBindingDef binding;
    binding.moduleId = "external.missing";
    binding.program = "${MISSING_HOST}";
    binding.arguments = {"--dll", "${MISSING_DLL}"};
    sequence.moduleBindings.push_back(binding);

    ExecutionPlan plan;
    plan.id = "plan-binding-errors";
    ExecutionSession session(plan);

    const auto result = registerConfiguredModules(session, sequence, testBindingOptions());
    QVERIFY(!result.ok());
    QCOMPARE(result.errors.size(), 2);

    const auto hasError = [&](const QString& variableName, const QString& path) {
        return std::any_of(result.errors.cbegin(), result.errors.cend(), [&](const ModuleBindingRegistrationError& error) {
            return error.moduleId == "external.missing" &&
                   error.message.contains(variableName) &&
                   error.message.contains(path);
        });
    };

    QVERIFY(hasError("MISSING_HOST", "moduleBindings[0].program"));
    QVERIFY(hasError("MISSING_DLL", "moduleBindings[0].arguments[1]"));
}

void CoreTests::transportModuleAdapterMapsSuccessfulResponse()
{
    auto transport = std::make_shared<FakeModuleTransport>();
    transport->response.outcome = ModuleOutcome::Passed;
    transport->response.outputs.insert("actualVoltage", 4.999);
    transport->response.measurements.push_back(makeMeasurement("VOUT", 4.999, "V"));

    TransportModuleAdapter adapter("external.dmm", transport, 1234);

    ModuleExecutionContext context;
    context.uutId = "uut-1";
    context.frameId = "root";
    context.attemptId = "attempt-2";
    context.attemptIndex = 2;
    context.inputs.insert("range", "10V");
    context.parameters.insert("aperture", "NPLC1");

    const auto result = adapter.execute("measureVoltage", context);

    QCOMPARE(result.outcome, ModuleOutcome::Passed);
    QCOMPARE(result.outputs.value("actualVoltage").toDouble(), 4.999);
    QCOMPARE(result.measurements.size(), 1);
    QCOMPARE(result.measurements.first().name, QString("VOUT"));
    QCOMPARE(result.measurements.first().unit, QString("V"));
    QCOMPARE(transport->callCount, 1);
    QCOMPARE(transport->lastTimeoutMs, 1234);
    QVERIFY(!transport->lastRequest.traceId.isEmpty());
    QVERIFY(transport->lastRequest.traceId.startsWith("external.dmm:"));
    QCOMPARE(transport->lastRequest.moduleId, QString("external.dmm"));
    QCOMPARE(transport->lastRequest.functionName, QString("measureVoltage"));
    QCOMPARE(transport->lastRequest.context.uutId, QString("uut-1"));
    QCOMPARE(transport->lastRequest.context.attemptIndex, 2);
    QCOMPARE(transport->lastRequest.context.inputs.value("range").toString(), QString("10V"));
    QCOMPARE(transport->lastRequest.context.parameters.value("aperture").toString(), QString("NPLC1"));
}

void CoreTests::transportModuleAdapterMapsTimeout()
{
    auto transport = std::make_shared<FakeModuleTransport>();
    transport->status = ModuleTransportStatus::Timeout;

    TransportModuleAdapter adapter("external.dmm", transport, 100);
    const auto result = adapter.execute("measureVoltage", {});

    QCOMPARE(result.outcome, ModuleOutcome::Timeout);
    QCOMPARE(result.errorCode, QString("TransportTimeout"));
    QVERIFY(result.errorMessage.contains("timed out"));
}

void CoreTests::transportModuleAdapterMapsTransportError()
{
    auto transport = std::make_shared<FakeModuleTransport>();
    transport->status = ModuleTransportStatus::TransportError;
    transport->response.errorCode = "HostCrashed";
    transport->response.errorMessage = "Native host crashed";

    TransportModuleAdapter adapter("external.dmm", transport, 100);
    const auto result = adapter.execute("measureVoltage", {});

    QCOMPARE(result.outcome, ModuleOutcome::Error);
    QCOMPARE(result.errorCode, QString("HostCrashed"));
    QCOMPARE(result.errorMessage, QString("Native host crashed"));
}

void CoreTests::qProcessTransportCallsMockHost()
{
    const auto host = mockHostPath();
    QVERIFY2(QFileInfo::exists(host), qPrintable(host));

    QProcessTransport transport(host);

    ModuleTransportRequest request;
    request.traceId = "trace-1";
    request.moduleId = "external.echo";
    request.functionName = "echo";
    request.context.uutId = "uut-1";
    request.context.inputs.insert("value", "hello");

    ModuleTransportResponse response;
    const auto status = transport.call(request, response, 3000);

    QCOMPARE(status, ModuleTransportStatus::Ok);
    QCOMPARE(response.outcome, ModuleOutcome::Passed);
    QCOMPARE(response.outputs.value("value").toString(), QString("hello"));
}

void CoreTests::qProcessTransportTimesOutMockHost()
{
    const auto host = mockHostPath();
    QVERIFY2(QFileInfo::exists(host), qPrintable(host));

    QProcessTransport transport(host);

    ModuleTransportRequest request;
    request.traceId = "trace-timeout";
    request.moduleId = "external.echo";
    request.functionName = "echo";
    request.context.inputs.insert("mockDelayMs", 1000);

    ModuleTransportResponse response;
    const auto status = transport.call(request, response, 100);

    QCOMPARE(status, ModuleTransportStatus::Timeout);
    QCOMPARE(response.outcome, ModuleOutcome::Timeout);
    QCOMPARE(response.errorCode, QString("ProcessTimeout"));
}

void CoreTests::qProcessTransportReportsHostExitError()
{
    const auto host = mockHostPath();
    QVERIFY2(QFileInfo::exists(host), qPrintable(host));

    QProcessTransport transport(host);

    ModuleTransportRequest request;
    request.traceId = "trace-exit";
    request.moduleId = "external.echo";
    request.functionName = "echo";
    request.context.inputs.insert("mockExitCode", 7);

    ModuleTransportResponse response;
    const auto status = transport.call(request, response, 3000);

    QCOMPARE(status, ModuleTransportStatus::TransportError);
    QCOMPARE(response.outcome, ModuleOutcome::Error);
    QCOMPARE(response.errorCode, QString("ProcessExitError"));
}

void CoreTests::persistentQProcessTransportReusesHostStateAcrossCalls()
{
    const auto host = fakeInstrumentHostPath();
    QVERIFY2(QFileInfo::exists(host), qPrintable(host));

    PersistentQProcessTransport transport(host);

    auto makeRequest = [](const QString& function, const QVariantMap& inputs) {
        ModuleTransportRequest request;
        request.traceId = QString("trace-%1").arg(function);
        request.moduleId = "fake.instrument";
        request.functionName = function;
        request.context.uutId = "uut-1";
        request.context.inputs = inputs;
        return request;
    };

    ModuleTransportResponse openResponse;
    auto status = transport.call(makeRequest("open",
                                             {{"deviceId", "DMM1"},
                                              {"deviceType", "DMM"},
                                              {"address", "USB0::FAKE::INSTR"}}),
                                 openResponse,
                                 3000);
    QCOMPARE(status, ModuleTransportStatus::Ok);
    QCOMPARE(openResponse.outcome, ModuleOutcome::Passed);
    QCOMPARE(openResponse.outputs.value("openCount").toInt(), 1);
    QCOMPARE(openResponse.outputs.value("readCount").toInt(), 0);
    QVERIFY(openResponse.outputs.value("connected").toBool());
    QVERIFY(transport.isRunning());

    ModuleTransportResponse firstRead;
    status = transport.call(makeRequest("read", {{"deviceId", "DMM1"}}), firstRead, 3000);
    QCOMPARE(status, ModuleTransportStatus::Ok);
    QCOMPARE(firstRead.outputs.value("openCount").toInt(), 1);
    QCOMPARE(firstRead.outputs.value("readCount").toInt(), 1);

    ModuleTransportResponse secondRead;
    status = transport.call(makeRequest("read", {{"deviceId", "DMM1"}}), secondRead, 3000);
    QCOMPARE(status, ModuleTransportStatus::Ok);
    QCOMPARE(secondRead.outputs.value("openCount").toInt(), 1);
    QCOMPARE(secondRead.outputs.value("readCount").toInt(), 2);
    QCOMPARE(secondRead.measurements.size(), 1);
    QCOMPARE(secondRead.measurements.first().name, QString("FAKE_INSTRUMENT_READ"));

    ModuleTransportResponse closeResponse;
    status = transport.call(makeRequest("close", {{"deviceId", "DMM1"}}), closeResponse, 3000);
    QCOMPARE(status, ModuleTransportStatus::Ok);
    QCOMPARE(closeResponse.outputs.value("openCount").toInt(), 1);
    QCOMPARE(closeResponse.outputs.value("readCount").toInt(), 2);
    QVERIFY(!closeResponse.outputs.value("connected").toBool());

    transport.shutdown();
    QVERIFY(!transport.isRunning());
}

void CoreTests::persistentInstrumentHostReportsHealthReconnectAndShutdown()
{
    const auto host = fakeInstrumentHostPath();
    QVERIFY2(QFileInfo::exists(host), qPrintable(host));

    PersistentQProcessTransport transport(host);

    ModuleTransportResponse openResponse;
    auto status = transport.call(makeFakeInstrumentRequest("open",
                                                           {{"deviceId", "DMM1"},
                                                            {"deviceType", "DMM"},
                                                            {"address", "USB0::FAKE::INSTR"}}),
                                 openResponse,
                                 3000);
    QCOMPARE(status, ModuleTransportStatus::Ok);
    QCOMPARE(openResponse.outcome, ModuleOutcome::Passed);
    QVERIFY(openResponse.outputs.value("connected").toBool());
    QVERIFY(openResponse.outputs.value("healthy").toBool());

    ModuleTransportResponse healthResponse;
    status = transport.call(makeFakeInstrumentRequest("health", {{"deviceId", "DMM1"}}),
                            healthResponse,
                            3000);
    QCOMPARE(status, ModuleTransportStatus::Ok);
    QCOMPARE(healthResponse.outcome, ModuleOutcome::Passed);
    QVERIFY(healthResponse.outputs.value("healthy").toBool());
    QVERIFY(healthResponse.outputs.value("connected").toBool());
    QCOMPARE(healthResponse.outputs.value("openCount").toInt(), 1);

    ModuleTransportResponse reconnectResponse;
    status = transport.call(makeFakeInstrumentRequest("reconnect", {{"deviceId", "DMM1"}}),
                            reconnectResponse,
                            3000);
    QCOMPARE(status, ModuleTransportStatus::Ok);
    QCOMPARE(reconnectResponse.outcome, ModuleOutcome::Passed);
    QCOMPARE(reconnectResponse.outputs.value("reconnectCount").toInt(), 1);
    QCOMPARE(reconnectResponse.outputs.value("openCount").toInt(), 1);
    QVERIFY(reconnectResponse.outputs.value("connected").toBool());

    ModuleTransportResponse shutdownResponse;
    status = transport.call(makeFakeInstrumentRequest("shutdown", {}), shutdownResponse, 3000);
    QCOMPARE(status, ModuleTransportStatus::Ok);
    QCOMPARE(shutdownResponse.outcome, ModuleOutcome::Passed);
    QVERIFY(shutdownResponse.outputs.value("shutdown").toBool());

    transport.shutdown();
    QVERIFY(!transport.isRunning());
}

void CoreTests::moduleRuntimeServicesInvokesTransportDeviceSession()
{
    const auto host = fakeInstrumentHostPath();
    QVERIFY2(QFileInfo::exists(host), qPrintable(host));

    DeviceSessionManager manager;
    auto transport = std::make_shared<PersistentQProcessTransport>(host);
    registerFakeInstrumentDeviceFactories(manager, transport);

    DeviceSessionConfig config;
    config.deviceId = "DMM1";
    config.deviceType = "DMM";
    config.driverId = "fake.dmm";
    config.address = "USB0::FAKE::INSTR";
    config.lifetime = DeviceSessionLifetime::Run;
    config.options.insert("nplc", 10);
    QVERIFY(manager.configureDevice(config));

    ModuleRuntimeServices services(manager);
    ModuleExecutionContext context;
    context.uutId = "uut-1";

    const auto open = services.openDeviceSession("DMM1");
    QVERIFY(open.ok());
    QVERIFY(!open.reusedExisting);

    const auto configure = services.invokeDevice("DMM1",
                                                 "configureDcv",
                                                 {{"range", 10.0}, {"nplc", 1.0}},
                                                 context);
    QCOMPARE(configure.outcome, ModuleOutcome::Passed);
    QCOMPARE(configure.outputs.value("lastMode").toString(), QString("DCV"));
    QCOMPARE(configure.outputs.value("configureCount").toInt(), 1);

    auto readContext = context;
    readContext.inputs.insert("measurementName", "DMM_DCV");
    const auto read = services.invokeDevice("DMM1",
                                            "read",
                                            {{"measurementName", "DMM_DCV"}, {"unit", "V"}},
                                            readContext);
    QCOMPARE(read.outcome, ModuleOutcome::Passed);
    QCOMPARE(read.outputs.value("readCount").toInt(), 1);
    QCOMPARE(read.measurements.size(), 1);
    QCOMPARE(read.measurements.first().name, QString("DMM_DCV"));
    QCOMPARE(read.measurements.first().unit, QString("V"));

    const auto close = services.closeDeviceSession("DMM1");
    QVERIFY(!close.hasError());
    QVERIFY(manager.session("DMM1"));
    QVERIFY(manager.session("DMM1")->state() != DeviceConnectionState::Connected);
    transport->shutdown();
}

void CoreTests::deviceSessionOpenAndCloseForwardAttemptLogContext()
{
    DeviceSessionManager manager;
    auto transport = std::make_shared<FakeModuleTransport>();
    transport->response.outcome = ModuleOutcome::Passed;
    transport->response.outputs.insert("connected", true);
    QVERIFY(manager.registerFactory(
        std::make_shared<TransportDeviceSessionFactory>("fake.can", transport, 3000)));

    DeviceSessionConfig config;
    config.deviceId = "CAN1.CH1";
    config.deviceType = "CAN";
    config.driverId = "fake.can";
    config.options.insert("deviceIndex", 0);
    config.options.insert("channelIndex", 0);
    QVERIFY(manager.configureDevice(config));

    ModuleRuntimeServices services(manager);
    CollectingModuleLogSink logs;
    ModuleExecutionContext context;
    context.uutId = "uut-1";
    context.frameId = "root";
    context.attemptId = "open-attempt";
    context.attemptIndex = 1;
    context.logSink = &logs;

    const auto opened = services.openDeviceSession(config.deviceId, &context);
    QVERIFY(opened.ok());
    QCOMPARE(transport->lastRequest.functionName, QString("open"));
    QCOMPARE(transport->lastRequest.context.logSink, &logs);
    QCOMPARE(transport->lastRequest.context.attemptId, QString("open-attempt"));
    QCOMPARE(transport->lastRequest.context.inputs.value("channelIndex").toInt(), 0);

    context.attemptId = "close-attempt";
    const auto closed = services.closeDeviceSession(config.deviceId, &context);
    QVERIFY(!closed.hasError());
    QCOMPARE(transport->lastRequest.functionName, QString("close"));
    QCOMPARE(transport->lastRequest.context.logSink, &logs);
    QCOMPARE(transport->lastRequest.context.attemptId, QString("close-attempt"));
}

void CoreTests::stationPluginBindingRunsLogicalDeviceThroughNativeHost()
{
    const auto host = nativeHostPath();
    const auto dll = testDllPath();
    QVERIFY2(QFileInfo::exists(host), qPrintable(host));
    QVERIFY2(QFileInfo::exists(dll), qPrintable(dll));

    const auto document = QJsonDocument::fromJson(R"json(
    {
      "id": "logical-device-test",
      "name": "Logical Device Test",
      "groups": [{
        "id": "main",
        "kind": "main",
        "steps": [{
          "id": "echo",
          "name": "Echo through CAN1",
          "kind": "action",
          "moduleId": "device",
          "function": "echo",
          "inputs": {"deviceId": "CAN1", "value": "logical-device"}
        }]
      }]
    })json");
    SequenceCompiler compiler;
    const auto compiled = compiler.compileJson(document.object());
    QVERIFY(compiled.ok());

    ExecutionSession session(compiled.plan);
    session.addUut(QStringLiteral("uut-1"));

    StationConfig station;
    station.stationId = QStringLiteral("station-1");
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const auto registryPath = directory.filePath(
        QStringLiteral("plugins/PluginRegistry.json"));
    QVERIFY(QDir().mkpath(QFileInfo(registryPath).absolutePath()));
    QFile registryFile(registryPath);
    QVERIFY(registryFile.open(QIODevice::WriteOnly));
    registryFile.write(QJsonDocument(QJsonObject{
        {QStringLiteral("plugins"), QJsonArray{QJsonObject{
             {QStringLiteral("moduleId"), QStringLiteral("plugin.test.dll")},
             {QStringLiteral("dll"), dll}}}}})
                           .toJson(QJsonDocument::Compact));
    registryFile.close();
    station.pluginRegistryPath = QStringLiteral("plugins/PluginRegistry.json");
    DeviceSessionConfig device;
    device.deviceId = QStringLiteral("CAN1");
    device.deviceType = QStringLiteral("CAN");
    device.driverId = QStringLiteral("plugin.test.dll");
    device.lifetime = DeviceSessionLifetime::Run;
    station.devices.push_back(device);
    QVERIFY(configureDeviceSessions(station, session.devices()).isEmpty());

    StationPluginRegistrationOptions options;
    options.nativeHostProgram = host;
    options.projectDir = projectRootPath();
    options.stationFilePath = directory.filePath(QStringLiteral("StationSystem.json"));
    const auto registration = registerStationPluginModules(session, station, options);
    QVERIFY(registration.ok());
    QVERIFY(registration.registeredModuleIds.contains(QStringLiteral("device")));

    const auto result = session.run();
    QVERIFY(result.completed);
    QCOMPARE(result.nodeResults.size(), 1);
    QCOMPARE(result.nodeResults.first().outcome, NodeOutcome::Passed);
    QCOMPARE(result.nodeResults.first().outputs.value(QStringLiteral("value")).toString(),
             QStringLiteral("logical-device"));
}

void CoreTests::executionSessionRunsActionThroughQProcessTransport()
{
    const auto host = mockHostPath();
    QVERIFY2(QFileInfo::exists(host), qPrintable(host));

    ExecutionPlan plan;
    plan.id = "plan-external-module";

    ExecNode action;
    action.id = "external-echo";
    action.displayName = "External Echo";
    action.kind = ExecNodeKind::Action;
    action.payload.insert("moduleId", "external.echo");
    action.payload.insert("function", "echo");
    action.payload.insert("inputs", QVariantMap{
                                      {"value", "from-host"},
                                      {"numeric", 42},
                                  });
    QVERIFY(plan.addNode(action));

    ExecutionSession session(plan);
    session.addUut("uut-1");

    auto transport = std::make_shared<QProcessTransport>(host);
    QVERIFY(session.registerModule(
        std::make_shared<TransportModuleAdapter>("external.echo", transport, 3000)));

    const auto result = session.run();
    QVERIFY(result.completed);
    QVERIFY(!result.hasError);
    QCOMPARE(result.state, ExecutionState::Completed);
    QCOMPARE(result.nodeResults.size(), 1);
    QCOMPARE(result.nodeResults.first().outcome, NodeOutcome::Passed);
    QCOMPARE(result.nodeResults.first().outputs.value("value").toString(), QString("from-host"));
    QCOMPARE(result.nodeResults.first().outputs.value("numeric").toInt(), 42);
}

void CoreTests::dllBridgeInvokerCallsTestDll()
{
    const auto dllPath = testDllPath();
    QVERIFY2(QFileInfo::exists(dllPath), qPrintable(dllPath));

    DllBridgeInvoker invoker(dllPath);

    ModuleTransportRequest request;
    request.traceId = "trace-dll";
    request.moduleId = "dll.echo";
    request.functionName = "echo";
    request.context.uutId = "uut-1";
    request.context.inputs.insert("value", "from-dll");
    request.context.inputs.insert("numeric", 42);
    request.context.inputs.insert("measurements", QVariantMap{
                                                       {"name", "DLL_ECHO"},
                                                       {"value", 42},
                                                       {"unit", "count"},
                                                   });

    ModuleTransportResponse response;
    const auto status = invoker.call(request, response, 3000);

    QCOMPARE(status, ModuleTransportStatus::Ok);
    QCOMPARE(response.outcome, ModuleOutcome::Passed);
    QCOMPARE(response.outputs.value("value").toString(), QString("from-dll"));
    QCOMPARE(response.outputs.value("numeric").toInt(), 42);
    QCOMPARE(response.measurements.size(), 1);
    QCOMPARE(response.measurements.first().name, QString("DLL_ECHO"));
    QCOMPARE(response.measurements.first().value.toInt(), 42);
    QCOMPARE(response.measurements.first().unit, QString("count"));
}

void CoreTests::dllBridgeInvokerStreamsPluginLogs()
{
    const auto dllPath = testDllPath();
    QVERIFY2(QFileInfo::exists(dllPath), qPrintable(dllPath));

    DllBridgeInvoker invoker(dllPath);
    CollectingModuleLogSink sink;

    ModuleTransportRequest request;
    request.traceId = "trace-dll-log";
    request.moduleId = "dll.echo";
    request.functionName = "echo";
    request.context.logSink = &sink;
    request.context.inputs.insert("logMessages",
                                  QVariantList{"send: 01 02 03 04",
                                               "sleep: 2000 ms",
                                               "recv: 01 02 03",
                                               "parse: voltage=5.01 V"});

    ModuleTransportResponse response;
    const auto status = invoker.call(request, response, 3000);

    QCOMPARE(status, ModuleTransportStatus::Ok);
    const auto logs = sink.records();
    QCOMPARE(logs.size(), 4);
    QCOMPARE(logs[0].message, QString("send: 01 02 03 04"));
    QCOMPARE(logs[1].message, QString("sleep: 2000 ms"));
    QCOMPARE(logs[2].message, QString("recv: 01 02 03"));
    QCOMPARE(logs[3].message, QString("parse: voltage=5.01 V"));
}

void CoreTests::dllBridgeInvokerReportsDllErrorCode()
{
    const auto dllPath = testDllPath();
    QVERIFY2(QFileInfo::exists(dllPath), qPrintable(dllPath));

    DllBridgeInvoker invoker(dllPath);

    ModuleTransportRequest request;
    request.traceId = "trace-dll-error";
    request.moduleId = "dll.echo";
    request.functionName = "echo";
    request.context.inputs.insert("dllReturnCode", 17);

    ModuleTransportResponse response;
    const auto status = invoker.call(request, response, 3000);

    QCOMPARE(status, ModuleTransportStatus::TransportError);
    QCOMPARE(response.outcome, ModuleOutcome::Error);
    QCOMPARE(response.errorCode, QString("DllExecuteFailed"));
    QVERIFY(response.errorMessage.contains("17"));
}

void CoreTests::dllBridgeInvokerReportsTimeout()
{
    const auto dllPath = testDllPath();
    QVERIFY2(QFileInfo::exists(dllPath), qPrintable(dllPath));

    DllBridgeInvoker invoker(dllPath);

    ModuleTransportRequest request;
    request.traceId = "trace-dll-timeout";
    request.moduleId = "dll.echo";
    request.functionName = "echo";
    request.context.inputs.insert("dllSleepMs", 1000);

    ModuleTransportResponse response;
    const auto status = invoker.call(request, response, 50);

    QCOMPARE(status, ModuleTransportStatus::Timeout);
    QCOMPARE(response.outcome, ModuleOutcome::Timeout);
    QCOMPARE(response.errorCode, QString("DllExecuteTimeout"));
    QVERIFY(response.errorMessage.contains("cannot be safely terminated"));
}

void CoreTests::nativeHostManifestResolvesVariables()
{
    const auto manifestPath = examplePath("nativehost/test_dll_manifest.json");
    const auto dllPath = testDllPath();
    QVERIFY2(QFileInfo::exists(manifestPath), qPrintable(manifestPath));
    QVERIFY2(QFileInfo::exists(dllPath), qPrintable(dllPath));

    VariableResolverOptions options;
    options.sequenceFilePath = manifestPath;
    options.projectDir = projectRootPath();
    options.useEnvironment = false;
    options.variables.insert("PICOATE_TEST_DLL", dllPath);

    const auto result = loadNativeHostManifest(manifestPath, options);
    QVERIFY(result.ok());
    QCOMPARE(result.manifest.dllPath, dllPath);
    QCOMPARE(result.manifest.symbol, QString("PicoATE_Execute"));
    QCOMPARE(result.manifest.bufferSize, 65536);
    QCOMPARE(result.manifest.dllTimeoutMs, 30000);
    QCOMPARE(result.manifest.diagnostics.vendorStdioMode, NativeHostVendorStdioMode::Strict);
    QCOMPARE(result.manifest.diagnostics.maximumBatchRecords, 64);
    QCOMPARE(result.manifest.diagnostics.maximumBatchBytes, 16384);
    QCOMPARE(result.manifest.diagnostics.batchFlushMs, 20);
    QCOMPARE(result.manifest.metadata.value("source").toString(),
             QFileInfo(manifestPath).absoluteDir().absolutePath());
}

void CoreTests::nativeHostManifestReportsUnresolvedVariables()
{
    const auto manifestPath = examplePath("nativehost/test_dll_manifest.json");
    QVERIFY2(QFileInfo::exists(manifestPath), qPrintable(manifestPath));

    VariableResolverOptions options;
    options.sequenceFilePath = manifestPath;
    options.useEnvironment = false;

    const auto result = loadNativeHostManifest(manifestPath, options);
    QVERIFY(!result.ok());
    QVERIFY(std::any_of(result.errors.cbegin(), result.errors.cend(), [](const NativeHostManifestError& error) {
        return error.path == "dll" && error.message.contains("PICOATE_TEST_DLL");
    }));
}


void CoreTests::nativeHostManifestRejectsInvalidDiagnostics()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const auto path = directory.filePath("invalid_manifest.json");
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write(R"({
        "dll": "dummy.dll",
        "diagnostics": {
            "vendorStdio": "guess",
            "maximumBatchRecords": 0,
            "batchFlushMs": "fast"
        }
    })");
    file.close();

    VariableResolverOptions options;
    options.sequenceFilePath = path;
    options.useEnvironment = false;
    const auto result = loadNativeHostManifest(path, options);
    QVERIFY(!result.ok());

    const auto hasPath = [&](const QString& pathValue) {
        return std::any_of(result.errors.cbegin(), result.errors.cend(),
                           [&](const NativeHostManifestError& error) {
                               return error.path == pathValue;
                           });
    };
    QVERIFY(hasPath("diagnostics.vendorStdio"));
    QVERIFY(hasPath("diagnostics.maximumBatchRecords"));
    QVERIFY(hasPath("diagnostics.batchFlushMs"));
}
void CoreTests::qProcessTransportCallsNativeHostDll()
{
    const auto host = nativeHostPath();
    const auto dllPath = testDllPath();
    QVERIFY2(QFileInfo::exists(host), qPrintable(host));
    QVERIFY2(QFileInfo::exists(dllPath), qPrintable(dllPath));

    QProcessTransport transport(host, {"--dll", dllPath});

    ModuleTransportRequest request;
    request.traceId = "trace-nativehost-dll";
    request.moduleId = "native.dll.echo";
    request.functionName = "echo";
    request.context.uutId = "uut-1";
    request.context.inputs.insert("value", "from-nativehost");
    request.context.inputs.insert("numeric", 42);

    ModuleTransportResponse response;
    const auto status = transport.call(request, response, 3000);

    QCOMPARE(status, ModuleTransportStatus::Ok);
    QCOMPARE(response.outcome, ModuleOutcome::Passed);
    QCOMPARE(response.outputs.value("value").toString(), QString("from-nativehost"));
    QCOMPARE(response.outputs.value("numeric").toInt(), 42);
}

void CoreTests::qProcessTransportStreamsOrderedNativeHostLogs()
{
    const auto host = nativeHostPath();
    const auto dllPath = testDllPath();
    QVERIFY2(QFileInfo::exists(host), qPrintable(host));
    QVERIFY2(QFileInfo::exists(dllPath), qPrintable(dllPath));

    QProcessTransport transport(host, {"--dll", dllPath});
    CollectingModuleLogSink sink;

    ModuleTransportRequest request;
    request.traceId = "trace-nativehost-log";
    request.moduleId = "native.dll.echo";
    request.functionName = "echo";
    request.context.logSink = &sink;
    request.context.inputs.insert("logMessages",
                                  QVariantList{"send: 01 02 03 04",
                                               "sleep: 2000 ms",
                                               "recv: 01 02 03",
                                               "parse: voltage=5.01 V"});

    ModuleTransportResponse response;
    const auto status = transport.call(request, response, 3000);

    QCOMPARE(status, ModuleTransportStatus::Ok);
    QCOMPARE(response.outcome, ModuleOutcome::Passed);
    const auto logs = sink.records();
    QCOMPARE(logs.size(), 4);
    for (int index = 0; index < logs.size(); ++index) {
        QCOMPARE(logs[index].sourceSequence, static_cast<quint64>(index + 1));
    }
    QCOMPARE(logs[0].message, QString("send: 01 02 03 04"));
    QCOMPARE(logs[3].message, QString("parse: voltage=5.01 V"));
}

void CoreTests::qProcessTransportCapturesVendorStdoutAndStderr()
{
    const auto host = nativeHostPath();
    const auto dllPath = testDllPath();
    QVERIFY2(QFileInfo::exists(host), qPrintable(host));
    QVERIFY2(QFileInfo::exists(dllPath), qPrintable(dllPath));

    QProcessTransport transport(host, {"--dll", dllPath});
    CollectingModuleLogSink sink;

    ModuleTransportRequest request;
    request.traceId = "trace-vendor-stdio";
    request.moduleId = "native.dll.echo";
    request.functionName = "echo";
    request.context.logSink = &sink;
    request.context.inputs.insert("stdoutMessages", QVariantList{"stdout-one"});
    request.context.inputs.insert("stderrMessages", QVariantList{"stderr-two"});
    request.context.inputs.insert("numeric", 42);

    ModuleTransportResponse response;
    const auto status = transport.call(request, response, 3000);

    QCOMPARE(status, ModuleTransportStatus::Ok);
    QCOMPARE(response.outcome, ModuleOutcome::Passed);
    QCOMPARE(response.outputs.value("numeric").toInt(), 42);
    const auto logs = sink.records();
    QCOMPARE(logs.size(), 2);
    QCOMPARE(logs[0].message, QString("[vendor] stdout-one"));
    QCOMPARE(logs[1].message, QString("[vendor] stderr-two"));
    QCOMPARE(logs[0].sourceSequence, quint64(1));
    QCOMPARE(logs[1].sourceSequence, quint64(2));
}

void CoreTests::qProcessTransportBoundsRawVendorOutput()
{
    const auto host = nativeHostPath();
    const auto dllPath = testDllPath();
    QVERIFY2(QFileInfo::exists(host), qPrintable(host));
    QVERIFY2(QFileInfo::exists(dllPath), qPrintable(dllPath));

    QProcessTransport transport(host,
                                {"--dll", dllPath,
                                 "--log-queue-capacity", "1",
                                 "--log-message-characters", "128"});
    CollectingModuleLogSink sink;

    ModuleTransportRequest request;
    request.traceId = "trace-vendor-stdio-burst";
    request.moduleId = "native.dll.echo";
    request.functionName = "echo";
    request.context.logSink = &sink;
    request.context.inputs.insert("rawStdoutCount", 10000);
    request.context.inputs.insert("rawNoNewlineCharacters", 100000);

    ModuleTransportResponse response;
    const auto status = transport.call(request, response, 10000);

    QCOMPARE(status, ModuleTransportStatus::Ok);
    QCOMPARE(response.outcome, ModuleOutcome::Passed);
    const auto logs = sink.records();
    QVERIFY(!logs.isEmpty());
    QVERIFY(logs.size() < 10000);
    QVERIFY(std::any_of(logs.cbegin(), logs.cend(), [](const ModuleLogRecord& record) {
        return record.droppedBefore > 0 && record.message.contains("dropped");
    }));
    QVERIFY(std::all_of(logs.cbegin(), logs.cend(), [](const ModuleLogRecord& record) {
        return record.message.size() <= 160;
    }));
}

void CoreTests::nativeHostBatchesProtocolLogFrames()
{
    const auto host = nativeHostPath();
    const auto dllPath = testDllPath();
    QVERIFY2(QFileInfo::exists(host), qPrintable(host));
    QVERIFY2(QFileInfo::exists(dllPath), qPrintable(dllPath));

    ModuleTransportRequest request;
    request.traceId = "trace-batched-wire-logs";
    request.moduleId = "native.dll.echo";
    request.functionName = "echo";
    request.context.inputs.insert("logCount", 1000);

    QProcess process;
    process.setProgram(host);
    process.setArguments({"--dll", dllPath,
                          "--vendor-stdio", "discard",
                          "--log-batch-records", "64",
                          "--log-batch-bytes", "1048576",
                          "--log-flush-ms", "1000"});
    process.setProcessChannelMode(QProcess::SeparateChannels);
    process.start();
    QVERIFY2(process.waitForStarted(3000), qPrintable(process.errorString()));
    process.write(QJsonDocument(moduleTransportRequestToJson(request))
                      .toJson(QJsonDocument::Compact) + '\n');
    QVERIFY(process.waitForBytesWritten(3000));
    process.closeWriteChannel();
    QVERIFY2(process.waitForFinished(10000), qPrintable(process.errorString()));
    QCOMPARE(process.exitStatus(), QProcess::NormalExit);
    QCOMPARE(process.exitCode(), 0);

    const auto lines = process.readAllStandardOutput().split('\n');
    int protocolLines = 0;
    int batchLines = 0;
    int responseLines = 0;
    int deliveredLogs = 0;
    for (const auto& rawLine : lines) {
        if (rawLine.trimmed().isEmpty()) {
            continue;
        }
        ++protocolLines;
        QJsonParseError error;
        const auto document = QJsonDocument::fromJson(rawLine, &error);
        QCOMPARE(error.error, QJsonParseError::NoError);
        QVERIFY(document.isObject());
        const auto message = moduleProtocolMessageFromJson(document.object());
        if (message.kind == ModuleProtocolMessageKind::LogBatch) {
            ++batchLines;
            deliveredLogs += message.logs.size();
        } else if (message.kind == ModuleProtocolMessageKind::Response) {
            ++responseLines;
        } else {
            QFAIL("NativeHost emitted an unexpected protocol frame");
        }
    }

    QCOMPARE(deliveredLogs, 1000);
    QCOMPARE(batchLines, 16);
    QCOMPARE(responseLines, 1);
    QCOMPARE(protocolLines, 17);
}

void CoreTests::qProcessTransportDiscardsVendorStdoutWhenConfigured()
{
    const auto host = nativeHostPath();
    const auto dllPath = testDllPath();
    QVERIFY2(QFileInfo::exists(host), qPrintable(host));
    QVERIFY2(QFileInfo::exists(dllPath), qPrintable(dllPath));

    QProcessTransport transport(host, {"--dll", dllPath,
                                       "--vendor-stdio", "discard"});
    CollectingModuleLogSink sink;

    ModuleTransportRequest request;
    request.traceId = "trace-discard-vendor-stdio";
    request.moduleId = "native.dll.echo";
    request.functionName = "echo";
    request.context.logSink = &sink;
    request.context.inputs.insert("logMessages", QVariantList{"callback-visible"});
    request.context.inputs.insert("stdoutMessages", QVariantList{"vendor-hidden"});
    request.context.inputs.insert("stderrMessages", QVariantList{"vendor-error-hidden"});

    ModuleTransportResponse response;
    const auto status = transport.call(request, response, 3000);

    QCOMPARE(status, ModuleTransportStatus::Ok);
    QCOMPARE(response.outcome, ModuleOutcome::Passed);
    const auto logs = sink.records();
    QCOMPARE(logs.size(), 1);
    QCOMPARE(logs.first().message, QString("callback-visible"));

    const auto hostTiming = response.diagnostics.value("nativeHost").toMap();
    QCOMPARE(hostTiming.value("vendorStdioMode").toString(), QString("discard"));
    QCOMPARE(hostTiming.value("vendorFlushMs").toLongLong(), 0);
    QVERIFY(response.diagnostics.value("qprocess").toMap().value("totalMs").toLongLong() >= 0);
}

void CoreTests::persistentNativeHostHandlesManyShortCalls()
{
    const auto host = nativeHostPath();
    const auto dllPath = testDllPath();
    QVERIFY2(QFileInfo::exists(host), qPrintable(host));
    QVERIFY2(QFileInfo::exists(dllPath), qPrintable(dllPath));

    PersistentQProcessTransport transport(host, {"--dll", dllPath,
                                                  "--vendor-stdio", "discard"});
    QElapsedTimer timer;
    timer.start();

    constexpr int CallCount = 200;
    for (int index = 0; index < CallCount; ++index) {
        ModuleTransportRequest request;
        request.traceId = QString("trace-short-%1").arg(index);
        request.moduleId = "native.dll.echo";
        request.functionName = "echo";
        request.context.inputs.insert("numeric", index);

        ModuleTransportResponse response;
        const auto status = transport.call(request, response, 3000);
        QCOMPARE(status, ModuleTransportStatus::Ok);
        QCOMPARE(response.outcome, ModuleOutcome::Passed);
        QCOMPARE(response.outputs.value("numeric").toInt(), index);
        const auto timing = response.diagnostics.value("persistentQprocess").toMap();
        QCOMPARE(timing.value("reusedProcess").toBool(), index > 0);
        QVERIFY(timing.value("totalMs").toLongLong() >= 0);
    }

    transport.shutdown();
    QVERIFY2(timer.elapsed() < 10000,
             qPrintable(QString("200 short calls took %1 ms").arg(timer.elapsed())));
}
void CoreTests::qProcessTransportDropsHighFrequencyLogsWithoutBlocking()
{
    const auto host = nativeHostPath();
    const auto dllPath = testDllPath();
    QVERIFY2(QFileInfo::exists(host), qPrintable(host));
    QVERIFY2(QFileInfo::exists(dllPath), qPrintable(dllPath));

    QProcessTransport transport(host,
                                {"--dll", dllPath, "--log-queue-capacity", "1"});
    CollectingModuleLogSink sink;

    ModuleTransportRequest request;
    request.traceId = "trace-nativehost-log-burst";
    request.moduleId = "native.dll.echo";
    request.functionName = "echo";
    request.context.logSink = &sink;
    request.context.inputs.insert("logCount", 5000);

    ModuleTransportResponse response;
    const auto status = transport.call(request, response, 10000);

    QCOMPARE(status, ModuleTransportStatus::Ok);
    QCOMPARE(response.outcome, ModuleOutcome::Passed);
    const auto logs = sink.records();
    QVERIFY(!logs.isEmpty());
    QVERIFY(logs.size() < 5001);
    QVERIFY(std::any_of(logs.cbegin(), logs.cend(), [](const ModuleLogRecord& record) {
        return record.droppedBefore > 0 && record.message.contains("dropped");
    }));
    for (int index = 1; index < logs.size(); ++index) {
        QVERIFY(logs[index].sourceSequence > logs[index - 1].sourceSequence);
    }
}

void CoreTests::executionSessionPublishesPluginLogsWithAttemptContext()
{
    const auto host = nativeHostPath();
    const auto dllPath = testDllPath();
    QVERIFY2(QFileInfo::exists(host), qPrintable(host));
    QVERIFY2(QFileInfo::exists(dllPath), qPrintable(dllPath));

    ExecutionPlan plan;
    plan.id = "plugin-log-plan";
    ExecNode action;
    action.id = "001";
    action.localId = "001";
    action.displayName = "CAN Request";
    action.kind = ExecNodeKind::Action;
    action.payload.insert("moduleId", "native.dll.echo");
    action.payload.insert("function", "echo");
    action.payload.insert("inputs", QVariantMap{{"logMessages",
                                                  QVariantList{"send: 01 02 03 04",
                                                               "recv: 01 02 03"}}});
    QVERIFY(plan.addNode(action));

    CollectingRuntimeEventSink eventSink;
    ExecutionSession session(plan, {}, &eventSink);
    session.addUut("uut-1");
    auto transport = std::make_shared<QProcessTransport>(host, QStringList{"--dll", dllPath});
    QVERIFY(session.registerModule(
        std::make_shared<TransportModuleAdapter>("native.dll.echo", transport, 3000)));

    const auto run = session.run();
    QVERIFY(run.completed);
    QVERIFY(!run.hasError);

    QVector<RuntimeEvent> logs;
    for (const auto& event : eventSink.records()) {
        if (event.kind == RuntimeEventKind::ModuleLog) {
            logs.push_back(event);
        }
    }
    QCOMPARE(logs.size(), 2);
    QCOMPARE(logs[0].uutId, QString("uut-1"));
    QCOMPARE(logs[0].nodeId, QString("001"));
    QCOMPARE(logs[0].nodeDisplayName, QString("CAN Request"));
    QVERIFY(!logs[0].attemptId.isEmpty());
    QVERIFY(!logs[0].requestId.isEmpty());
    QCOMPARE(logs[1].requestId, logs[0].requestId);
    QCOMPARE(logs[0].attemptIndex, 1);
    QCOMPARE(logs[0].message, QString("send: 01 02 03 04"));
    QVERIFY(logs[0].sequenceNumber < logs[1].sequenceNumber);
}

void CoreTests::qProcessTransportCallsNativeHostDllManifest()
{
    const auto host = nativeHostPath();
    const auto dllPath = testDllPath();
    const auto manifestPath = examplePath("nativehost/test_dll_manifest.json");
    QVERIFY2(QFileInfo::exists(host), qPrintable(host));
    QVERIFY2(QFileInfo::exists(dllPath), qPrintable(dllPath));
    QVERIFY2(QFileInfo::exists(manifestPath), qPrintable(manifestPath));

    QProcessTransport transport(host, {
                                          "--manifest",
                                          manifestPath,
                                          "--project-dir",
                                          projectRootPath(),
                                          "--var",
                                          QString("PICOATE_TEST_DLL=%1").arg(dllPath),
                                      });

    ModuleTransportRequest request;
    request.traceId = "trace-nativehost-manifest";
    request.moduleId = "native.dll.echo";
    request.functionName = "echo";
    request.context.uutId = "uut-1";
    request.context.inputs.insert("value", "from-nativehost-manifest");
    request.context.inputs.insert("numeric", 42);

    ModuleTransportResponse response;
    const auto status = transport.call(request, response, 3000);

    QCOMPARE(status, ModuleTransportStatus::Ok);
    QCOMPARE(response.outcome, ModuleOutcome::Passed);
    QCOMPARE(response.outputs.value("value").toString(), QString("from-nativehost-manifest"));
    QCOMPARE(response.outputs.value("numeric").toInt(), 42);
}

void CoreTests::qProcessTransportKillsNativeHostOnDllTimeout()
{
    const auto host = nativeHostPath();
    const auto dllPath = testDllPath();
    QVERIFY2(QFileInfo::exists(host), qPrintable(host));
    QVERIFY2(QFileInfo::exists(dllPath), qPrintable(dllPath));

    QProcessTransport transport(host, {"--dll", dllPath});

    ModuleTransportRequest request;
    request.traceId = "trace-nativehost-timeout";
    request.moduleId = "native.dll.echo";
    request.functionName = "echo";
    request.context.inputs.insert("dllSleepMs", 1000);

    ModuleTransportResponse response;
    const auto status = transport.call(request, response, 100);

    QCOMPARE(status, ModuleTransportStatus::Timeout);
    QCOMPARE(response.outcome, ModuleOutcome::Timeout);
    QCOMPARE(response.errorCode, QString("ProcessTimeout"));
}

void CoreTests::qProcessTransportCallsSimulatedCanDllManifest()
{
    const auto host = nativeHostPath();
    const auto dllPath = canDllPath();
    const auto manifestPath = examplePath("nativehost/can_decode_manifest.json");
    QVERIFY2(QFileInfo::exists(host), qPrintable(host));
    QVERIFY2(QFileInfo::exists(dllPath), qPrintable(dllPath));
    QVERIFY2(QFileInfo::exists(manifestPath), qPrintable(manifestPath));

    QProcessTransport transport(host, {
                                          "--manifest",
                                          manifestPath,
                                          "--project-dir",
                                          projectRootPath(),
                                          "--var",
                                          QString("PICOATE_CAN_DLL=%1").arg(dllPath),
                                      });

    ModuleTransportResponse response;
    const auto status = transport.call(makeCanDecodeRequest(), response, 3000);

    QCOMPARE(status, ModuleTransportStatus::Ok);
    QCOMPARE(response.outcome, ModuleOutcome::Passed);
    QCOMPARE(response.outputs.value("signalName").toString(), QString("PackVoltage"));
    QCOMPARE(response.outputs.value("rawValue").toDouble(), 10000.0);
    QCOMPARE(response.outputs.value("physicalValue").toDouble(), 100.0);
    QCOMPARE(response.measurements.size(), 1);
    QCOMPARE(response.measurements.first().name, QString("PackVoltage"));
    QCOMPARE(response.measurements.first().value.toDouble(), 100.0);
    QCOMPARE(response.measurements.first().unit, QString("V"));
    QCOMPARE(response.measurements.first().lowerLimit, 95.0);
    QCOMPARE(response.measurements.first().upperLimit, 105.0);
    QCOMPARE(response.measurements.first().status, MeasurementStatus::Passed);
}

void CoreTests::qProcessTransportReportsSimulatedCanLimitFail()
{
    const auto host = nativeHostPath();
    const auto dllPath = canDllPath();
    const auto manifestPath = examplePath("nativehost/can_decode_manifest.json");
    QVERIFY2(QFileInfo::exists(host), qPrintable(host));
    QVERIFY2(QFileInfo::exists(dllPath), qPrintable(dllPath));
    QVERIFY2(QFileInfo::exists(manifestPath), qPrintable(manifestPath));

    QProcessTransport transport(host, {
                                          "--manifest",
                                          manifestPath,
                                          "--project-dir",
                                          projectRootPath(),
                                          "--var",
                                          QString("PICOATE_CAN_DLL=%1").arg(dllPath),
                                      });

    ModuleTransportResponse response;
    const auto status = transport.call(makeCanDecodeRequest(99.0), response, 3000);

    QCOMPARE(status, ModuleTransportStatus::Ok);
    QCOMPARE(response.outcome, ModuleOutcome::Failed);
    QCOMPARE(response.errorCode, QString("LimitFail"));
    QVERIFY(response.errorMessage.contains("PackVoltage"));
    QCOMPARE(response.outputs.value("passed").toBool(), false);
    QCOMPARE(response.measurements.size(), 1);
    QCOMPARE(response.measurements.first().upperLimit, 99.0);
    QCOMPARE(response.measurements.first().status, MeasurementStatus::Failed);
}

void CoreTests::schedulerRetriesAndRunsCleanup()
{
    ExecutionPlan plan;
    plan.id = "plan-main";

    ExecNode action;
    action.id = "measure";
    action.displayName = "Measure";
    action.kind = ExecNodeKind::Action;
    action.payload.insert("failUntilAttempt", 0);
    action.retry.maxAttempts = 2;
    action.errorPolicy.cleanupRegionId = "main-cleanup";
    QVERIFY(plan.addNode(action));

    ExecNode cleanup;
    cleanup.id = "power-off";
    cleanup.displayName = "Power Off";
    cleanup.kind = ExecNodeKind::Cleanup;
    cleanup.alwaysRun = true;
    QVERIFY(plan.addNode(cleanup));

    CleanupRegion region;
    region.id = "main-cleanup";
    region.entryNodes = {"power-off"};
    region.triggers = {CleanupReason::StepFailed};
    plan.cleanupRegions.push_back(region);
    plan.addEdge({"finally-measure-cleanup",
                  "measure",
                  "power-off",
                  EdgeKind::Finally,
                  EdgeTrigger::Finally,
                  {},
                  0});

    ResourceManager resources;
    BarrierController barriers;
    LoopController loops;
    ErrorPolicyEngine errorPolicy;
    NodeRunner runner;
    ExecutionResultStore results(plan);
    ExecutionGraphScheduler scheduler(plan, resources, barriers, loops, errorPolicy, runner, results);

    UutExecution uut;
    uut.uutId = "uut-1";

    const auto result = scheduler.run(uut);
    QVERIFY(result.completed);
    QCOMPARE(uut.activations["measure"].attempts.size(), 2);
    QCOMPARE(uut.outcomeOf("measure"), NodeOutcome::Passed);
    QCOMPARE(uut.outcomeOf("power-off"), NodeOutcome::Passed);
}

void CoreTests::executionSessionReleasesBarrierAcrossUuts()
{
    ExecutionPlan plan;
    plan.id = "plan-batch";

    ExecNode barrier;
    barrier.id = "batch-ready";
    barrier.displayName = "Batch Ready";
    barrier.kind = ExecNodeKind::Barrier;
    barrier.payload.insert("barrierName", "batch-ready");
    barrier.payload.insert("cohortId", "batch-1");
    QVERIFY(plan.addNode(barrier));

    ExecNode after;
    after.id = "after-barrier";
    after.displayName = "After Barrier";
    after.kind = ExecNodeKind::Action;
    QVERIFY(plan.addNode(after));

    plan.addEdge({"barrier-after",
                  "batch-ready",
                  "after-barrier",
                  EdgeKind::Control,
                  EdgeTrigger::OnSuccess,
                  {},
                  0});

    ExecutionSession session(plan);
    session.addUut("uut-1");
    session.addUut("uut-2");

    const auto result = session.run();
    QVERIFY(result.completed);
    QCOMPARE(result.state, ExecutionState::Completed);

    const auto& uuts = session.uuts();
    QCOMPARE(uuts.size(), 2);
    for (const auto& uut : uuts) {
        QCOMPARE(uut.outcomeOf("batch-ready"), NodeOutcome::Passed);
        QCOMPARE(uut.outcomeOf("after-barrier"), NodeOutcome::Passed);
    }
}

void CoreTests::executionSessionDropsFailedUutBeforeBarrier()
{
    ExecutionPlan plan;
    plan.id = "plan-drop-failed";

    ExecNode precheck;
    precheck.id = "precheck";
    precheck.displayName = "Precheck";
    precheck.kind = ExecNodeKind::Action;
    precheck.payload.insert("failForUut", "uut-1");
    QVERIFY(plan.addNode(precheck));

    ExecNode barrier;
    barrier.id = "batch-ready";
    barrier.displayName = "Batch Ready";
    barrier.kind = ExecNodeKind::Barrier;
    barrier.payload.insert("barrierName", "batch-ready");
    barrier.payload.insert("cohortId", "batch-1");
    barrier.payload.insert("arrivalPolicy", "DropFailed");
    barrier.payload.insert("failurePolicy", "RemoveFailedMember");
    QVERIFY(plan.addNode(barrier));

    ExecNode after;
    after.id = "after-barrier";
    after.displayName = "After Barrier";
    after.kind = ExecNodeKind::Action;
    QVERIFY(plan.addNode(after));

    plan.addEdge({"precheck-barrier",
                  "precheck",
                  "batch-ready",
                  EdgeKind::Control,
                  EdgeTrigger::OnSuccess,
                  {},
                  0});
    plan.addEdge({"barrier-after",
                  "batch-ready",
                  "after-barrier",
                  EdgeKind::Control,
                  EdgeTrigger::OnSuccess,
                  {},
                  0});

    ExecutionSession session(plan);
    session.addUut("uut-1");
    session.addUut("uut-2");

    const auto result = session.run();
    QVERIFY(result.completed);
    QCOMPARE(result.state, ExecutionState::CompletedWithError);

    const auto& uuts = session.uuts();
    const auto failed = std::find_if(uuts.cbegin(), uuts.cend(), [](const UutExecution& uut) {
        return uut.uutId == "uut-1";
    });
    QVERIFY(failed != uuts.cend());
    QCOMPARE(failed->outcomeOf("precheck"), NodeOutcome::Failed);
    QCOMPARE(failed->outcomeOf("batch-ready"), NodeOutcome::Skipped);
    QCOMPARE(failed->outcomeOf("after-barrier"), NodeOutcome::Skipped);

    const auto passed = std::find_if(uuts.cbegin(), uuts.cend(), [](const UutExecution& uut) {
        return uut.uutId == "uut-2";
    });
    QVERIFY(passed != uuts.cend());
    QCOMPARE(passed->outcomeOf("precheck"), NodeOutcome::Passed);
    QCOMPARE(passed->outcomeOf("batch-ready"), NodeOutcome::Passed);
    QCOMPARE(passed->outcomeOf("after-barrier"), NodeOutcome::Passed);
}

void CoreTests::executionSessionRunsSetupCleanupOnceAndIsolatesFailedUut()
{
    ExecutionPlan plan;
    plan.id = "plan-four-uut-session-lifecycle";

    auto action = [](const NodeId& id,
                     const QString& function,
                     ExecutionPhase phase) {
        ExecNode node;
        node.id = id;
        node.localId = id;
        node.displayName = id;
        node.kind = ExecNodeKind::Action;
        node.phase = phase;
        node.payload.insert("moduleId", "test.multi-uut-lifecycle");
        node.payload.insert("function", function);
        return node;
    };

    auto setup = action("session-setup", "setup", ExecutionPhase::Setup);
    auto first = action("first", "fail-uut-2", ExecutionPhase::Main);
    first.errorPolicy.onFail = ErrorAction::StopUut;
    first.errorPolicy.cleanupRegionId = "session-cleanup";
    auto second = action("second", "after-failure", ExecutionPhase::Main);
    auto cleanup = action("session-cleanup", "cleanup", ExecutionPhase::Cleanup);
    cleanup.alwaysRun = true;

    QVERIFY(plan.addNode(setup));
    QVERIFY(plan.addNode(first));
    QVERIFY(plan.addNode(second));
    QVERIFY(plan.addNode(cleanup));
    plan.addEdge({"setup-main", "session-setup", "first", EdgeKind::Control,
                  EdgeTrigger::OnSuccess, {}, 0});
    plan.addEdge({"main-next", "first", "second", EdgeKind::Control,
                  EdgeTrigger::OnSuccess, {}, 0});
    plan.addEdge({"main-cleanup", "second", "session-cleanup", EdgeKind::Finally,
                  EdgeTrigger::Finally, {}, 0});

    CleanupRegion cleanupRegion;
    cleanupRegion.id = "session-cleanup";
    cleanupRegion.entryNodes = {"session-cleanup"};
    cleanupRegion.exitNodes = {"session-cleanup"};
    cleanupRegion.bestEffort = true;
    plan.cleanupRegions.push_back(cleanupRegion);

    CollectingRuntimeEventSink events;
    ExecutionSession session(plan, {}, &events);
    auto module = std::make_shared<MultiUutLifecycleModule>();
    QVERIFY(session.registerModule(module));
    for (int index = 1; index <= 4; ++index) {
        session.addUut(QString("UUT-%1").arg(index));
    }

    const auto result = session.run();
    QVERIFY(result.completed);
    QVERIFY(result.hasError);
    QCOMPARE(result.state, ExecutionState::CompletedWithError);
    QCOMPARE(result.uutResults.size(), 4);
    QCOMPARE(result.sessionNodeResults.size(), 2);

    QCOMPARE(module->calls.count("setup:"), 1);
    QCOMPARE(module->calls.count("cleanup:"), 1);
    QCOMPARE(module->calls.count("fail-uut-2:UUT-1"), 1);
    QCOMPARE(module->calls.count("fail-uut-2:UUT-2"), 1);
    QCOMPARE(module->calls.count("fail-uut-2:UUT-3"), 1);
    QCOMPARE(module->calls.count("fail-uut-2:UUT-4"), 1);
    QVERIFY(!module->calls.contains("after-failure:UUT-2"));
    QVERIFY(module->calls.contains("after-failure:UUT-1"));
    QVERIFY(module->calls.contains("after-failure:UUT-3"));
    QVERIFY(module->calls.contains("after-failure:UUT-4"));
    QCOMPARE(module->calls.last(), QString("cleanup:"));

    for (const auto& uutResult : result.uutResults) {
        QVERIFY(uutResult.completed);
        QCOMPARE(uutResult.hasError, uutResult.uutId == "UUT-2");
    }

    const auto report = session.report();
    QCOMPARE(report.sessionSteps.size(), 2);
    QCOMPARE(report.uuts.size(), 4);
    for (const auto& uut : report.uuts) {
        QCOMPARE(uut.steps.size(), 2);
        QVERIFY(uut.completed);
        QCOMPARE(uut.hasError, uut.uutId == "UUT-2");
        QCOMPARE(uut.outcome,
                 uut.uutId == "UUT-2" ? NodeOutcome::Failed : NodeOutcome::Passed);
    }

    QHash<UutId, int> logsByUut;
    for (const auto& event : events.records()) {
        if (event.kind == RuntimeEventKind::ModuleLog) {
            logsByUut[event.uutId] += 1;
        }
    }
    QCOMPARE(logsByUut.value({}), 2);
    QCOMPARE(logsByUut.value("UUT-1"), 2);
    QCOMPARE(logsByUut.value("UUT-2"), 1);
    QCOMPARE(logsByUut.value("UUT-3"), 2);
    QCOMPARE(logsByUut.value("UUT-4"), 2);
}

void CoreTests::executionSessionKeepsResourceAcrossUutTransaction()
{
    ExecutionPlan plan;
    plan.id = "plan-resource-region-order";

    auto action = [](const NodeId& id, const QString& function) {
        ExecNode node;
        node.id = id;
        node.localId = id;
        node.displayName = id;
        node.kind = ExecNodeKind::Action;
        node.phase = ExecutionPhase::Main;
        node.payload.insert("moduleId", "test.multi-uut-lifecycle");
        node.payload.insert("function", function);
        node.resources.push_back({"CAN1", ResourceMode::Exclusive, 1, 0, 30000});
        return node;
    };

    QVERIFY(plan.addNode(action("transaction-start", "start")));
    QVERIFY(plan.addNode(action("transaction-body", "body")));
    QVERIFY(plan.addNode(action("transaction-end", "end")));
    plan.addEdge({"region-1", "transaction-start", "transaction-body",
                  EdgeKind::Control, EdgeTrigger::OnSuccess, {}, 0});
    plan.addEdge({"region-2", "transaction-body", "transaction-end",
                  EdgeKind::Control, EdgeTrigger::OnSuccess, {}, 0});
    plan.resourceRegions.push_back({"can-transaction",
                                    "transaction-start",
                                    "transaction-end",
                                    {{"CAN1", ResourceMode::Exclusive, 1, 0, 30000}}});
    plan.entryNodeId = "transaction-start";
    plan.exitNodeId = "transaction-end";

    ExecutionSession session(plan);
    auto module = std::make_shared<MultiUutLifecycleModule>();
    QVERIFY(session.registerModule(module));
    for (int index = 1; index <= 4; ++index) {
        session.addUut(QString("UUT-%1").arg(index));
    }

    const auto result = session.run();
    QVERIFY(result.completed);
    QVERIFY(!result.hasError);
    QCOMPARE(module->calls,
             QVector<QString>({"start:UUT-1", "body:UUT-1", "end:UUT-1",
                               "start:UUT-2", "body:UUT-2", "end:UUT-2",
                               "start:UUT-3", "body:UUT-3", "end:UUT-3",
                               "start:UUT-4", "body:UUT-4", "end:UUT-4"}));
}

void CoreTests::executionSessionReleasesResourceRegionAfterUutFailure()
{
    ExecutionPlan plan;
    plan.id = "plan-resource-region-failure";

    auto action = [](const NodeId& id, const QString& function) {
        ExecNode node;
        node.id = id;
        node.localId = id;
        node.displayName = id;
        node.kind = ExecNodeKind::Action;
        node.phase = ExecutionPhase::Main;
        node.payload.insert("moduleId", "test.multi-uut-lifecycle");
        node.payload.insert("function", function);
        return node;
    };

    auto begin = action("begin", "begin");
    auto check = action("check", "fail-uut-2");
    check.errorPolicy.onFail = ErrorAction::StopUut;
    auto finish = action("finish", "finish");
    QVERIFY(plan.addNode(begin));
    QVERIFY(plan.addNode(check));
    QVERIFY(plan.addNode(finish));
    plan.addEdge({"failure-region-1", "begin", "check", EdgeKind::Control,
                  EdgeTrigger::OnSuccess, {}, 0});
    plan.addEdge({"failure-region-2", "check", "finish", EdgeKind::Control,
                  EdgeTrigger::OnSuccess, {}, 0});
    plan.resourceRegions.push_back({"failure-region", "begin", "finish",
                                    {{"CAN1", ResourceMode::Exclusive, 1, 0, 30000}}});
    plan.entryNodeId = "begin";
    plan.exitNodeId = "finish";

    ExecutionSession session(plan);
    auto module = std::make_shared<MultiUutLifecycleModule>();
    QVERIFY(session.registerModule(module));
    session.addUut("UUT-1");
    session.addUut("UUT-2");
    session.addUut("UUT-3");

    const auto result = session.run();
    QVERIFY(result.completed);
    QVERIFY(result.hasError);
    QCOMPARE(module->calls,
             QVector<QString>({"begin:UUT-1", "fail-uut-2:UUT-1", "finish:UUT-1",
                               "begin:UUT-2", "fail-uut-2:UUT-2",
                               "begin:UUT-3", "fail-uut-2:UUT-3", "finish:UUT-3"}));
    QCOMPARE(session.uuts()[1].stateOf("finish"), ActivationState::Skipped);
}

void CoreTests::sequenceCompilerRunsNestedResourceRegionAcrossUuts()
{
    const QByteArray json = R"json({
        "id": "nested-resource-region",
        "name": "Nested Resource Region",
        "groups": [{
            "id": "main",
            "kind": "main",
            "steps": [{
                "id": "001",
                "key": "transaction",
                "name": "CAN Transaction",
                "kind": "testItem",
                "steps": [{
                    "id": "01",
                    "key": "send",
                    "name": "Send",
                    "kind": "action",
                    "moduleId": "test.multi-uut-lifecycle",
                    "function": "start",
                    "resourceRegionStart": {
                        "id": "can-transaction",
                        "resources": [{
                            "resourceId": "CAN1",
                            "mode": "exclusive"
                        }]
                    }
                }, {
                    "id": "02",
                    "key": "read",
                    "name": "Read",
                    "kind": "action",
                    "moduleId": "test.multi-uut-lifecycle",
                    "function": "body"
                }, {
                    "id": "03",
                    "key": "check",
                    "name": "Check",
                    "kind": "action",
                    "moduleId": "test.multi-uut-lifecycle",
                    "function": "end",
                    "resourceRegionEnd": "can-transaction"
                }]
            }]
        }]
    })json";

    SequenceCompiler compiler;
    const auto compiled = compiler.compileJson(
        QJsonDocument::fromJson(json).object());
    QVERIFY2(compiled.ok(), qPrintable(compiled.errors.isEmpty()
        ? QStringLiteral("unknown compile error")
        : compiled.errors.first().message));
    QCOMPARE(compiled.plan.resourceRegions.size(), 1);
    QCOMPARE(compiled.plan.resourceRegions.first().entryNodeId,
             QStringLiteral("001.send"));
    QCOMPARE(compiled.plan.resourceRegions.first().exitNodeId,
             QStringLiteral("001.check"));
    QCOMPARE(compiled.plan.resourceRegions.first().requirements.size(), 1);
    QCOMPARE(compiled.plan.resourceRegions.first().requirements.first().resourceId,
             QStringLiteral("CAN1"));

    ExecutionSession session(compiled.plan);
    auto module = std::make_shared<MultiUutLifecycleModule>();
    QVERIFY(session.registerModule(module));
    session.addUut(QStringLiteral("UUT-1"));
    session.addUut(QStringLiteral("UUT-2"));

    const auto result = session.run();
    QVERIFY(result.completed);
    QVERIFY(!result.hasError);
    QCOMPARE(module->calls,
             QVector<QString>({QStringLiteral("start:UUT-1"),
                               QStringLiteral("body:UUT-1"),
                               QStringLiteral("end:UUT-1"),
                               QStringLiteral("start:UUT-2"),
                               QStringLiteral("body:UUT-2"),
                               QStringLiteral("end:UUT-2")}));
}

void CoreTests::sequenceCompilerRunsSingleItemResourceRegionAcrossUuts()
{
    const QByteArray json = R"json({
        "id": "single-item-resource-region",
        "name": "Single TestItem Resource Region",
        "groups": [{
            "id": "main",
            "kind": "main",
            "steps": [{
                "id": "001",
                "key": "transaction",
                "name": "CAN Transaction",
                "kind": "testItem",
                "resourceRegionStart": {
                    "id": "single-can-transaction",
                    "resources": [{
                        "resourceId": "CAN1",
                        "mode": "exclusive"
                    }]
                },
                "resourceRegionEnd": "single-can-transaction",
                "steps": [{
                    "id": "01",
                    "key": "send",
                    "name": "Send",
                    "kind": "action",
                    "moduleId": "test.multi-uut-lifecycle",
                    "function": "start"
                }, {
                    "id": "02",
                    "key": "settle",
                    "name": "Settle",
                    "kind": "wait",
                    "ms": 5
                }, {
                    "id": "03",
                    "key": "read",
                    "name": "Read",
                    "kind": "action",
                    "moduleId": "test.multi-uut-lifecycle",
                    "function": "end"
                }]
            }]
        }]
    })json";

    SequenceCompiler compiler;
    const auto compiled = compiler.compileJson(
        QJsonDocument::fromJson(json).object());
    QVERIFY2(compiled.ok(), qPrintable(compiled.errors.isEmpty()
        ? QStringLiteral("unknown compile error")
        : compiled.errors.first().message));
    QCOMPARE(compiled.plan.resourceRegions.size(), 1);
    QCOMPARE(compiled.plan.resourceRegions.first().entryNodeId,
             QStringLiteral("001"));
    QCOMPARE(compiled.plan.resourceRegions.first().exitNodeId,
             QStringLiteral("001"));

    ExecutionSession session(compiled.plan);
    auto module = std::make_shared<MultiUutLifecycleModule>();
    QVERIFY(session.registerModule(module));
    session.addUut(QStringLiteral("UUT-1"));
    session.addUut(QStringLiteral("UUT-2"));

    const auto result = session.run();
    QVERIFY(result.completed);
    QVERIFY(!result.hasError);
    QCOMPARE(module->calls,
             QVector<QString>({QStringLiteral("start:UUT-1"),
                               QStringLiteral("end:UUT-1"),
                               QStringLiteral("start:UUT-2"),
                               QStringLiteral("end:UUT-2")}));
}

void CoreTests::singleItemTestItemResourceRegionStaysLockedAcrossRetry()
{
    const QByteArray json = R"json({
        "id": "single-test-item-lock-retry",
        "name": "Single TestItem Lock Retry",
        "groups": [{
            "id": "main",
            "kind": "main",
            "steps": [{
                "id": "001",
                "key": "transaction",
                "name": "Retried CAN Transaction",
                "kind": "testItem",
                "retry": { "maxAttempts": 2 },
                "resourceRegionStart": {
                    "id": "single-test-item-can-lock",
                    "resources": [{
                        "resourceId": "CAN1",
                        "mode": "exclusive"
                    }]
                },
                "resourceRegionEnd": "single-test-item-can-lock",
                "steps": [{
                    "id": "01",
                    "key": "work",
                    "name": "CAN Work",
                    "kind": "action",
                    "moduleId": "test.test-item-retry-lock",
                    "function": "run"
                }]
            }]
        }]
    })json";

    SequenceCompiler compiler;
    const auto compiled = compiler.compileJson(
        QJsonDocument::fromJson(json).object());
    QVERIFY2(compiled.ok(), qPrintable(compiled.errors.isEmpty()
        ? QStringLiteral("unknown compile error")
        : compiled.errors.first().message));
    QCOMPARE(compiled.plan.resourceRegions.size(), 1);
    QCOMPARE(compiled.plan.resourceRegions.first().entryNodeId,
             QStringLiteral("001"));
    QCOMPARE(compiled.plan.resourceRegions.first().exitNodeId,
             QStringLiteral("001"));

    ExecutionSession session(compiled.plan);
    auto module = std::make_shared<TestItemRetryLockModule>();
    QVERIFY(session.registerModule(module));
    session.addUut(QStringLiteral("UUT-1"));
    session.addUut(QStringLiteral("UUT-2"));

    const auto result = session.run();
    QVERIFY(result.completed);
    QVERIFY(!result.hasError);
    QCOMPARE(module->calls,
             QVector<QString>({QStringLiteral("UUT-1:1"),
                               QStringLiteral("UUT-1:2"),
                               QStringLiteral("UUT-2:1")}));
    QCOMPARE(session.uuts()[0].activations.value(QStringLiteral("001")).attempts.size(), 2);
    QCOMPARE(session.uuts()[1].activations.value(QStringLiteral("001")).attempts.size(), 1);
}

void CoreTests::sequenceCompilerRejectsCrossParentResourceRegion()
{
    const QByteArray json = R"json({
        "id": "cross-parent-resource-region",
        "name": "Cross Parent Resource Region",
        "groups": [{
            "id": "main",
            "kind": "main",
            "steps": [{
                "id": "001",
                "name": "Transaction",
                "kind": "testItem",
                "steps": [{
                    "id": "01",
                    "name": "Nested Start",
                    "kind": "noop",
                    "resourceRegionStart": {
                        "id": "bad-region",
                        "resources": [{"resourceId": "CAN1"}]
                    }
                }, {
                    "id": "02",
                    "name": "Nested Body",
                    "kind": "noop"
                }]
            }, {
                "id": "002",
                "name": "Top Level End",
                "kind": "noop",
                "resourceRegionEnd": "bad-region"
            }]
        }]
    })json";

    SequenceCompiler compiler;
    const auto compiled = compiler.compileJson(
        QJsonDocument::fromJson(json).object());
    QVERIFY(!compiled.ok());
    QVERIFY(std::any_of(compiled.errors.cbegin(), compiled.errors.cend(),
                        [](const CompileError& error) {
                            return error.message.contains("no exit marker",
                                                          Qt::CaseInsensitive) ||
                                   error.message.contains("no matching entry",
                                                          Qt::CaseInsensitive);
                        }));
}

void CoreTests::sequenceCompilerRejectsIncompleteResourceRegion()
{
    const QByteArray json = R"json({
        "id": "incomplete-resource-region",
        "name": "Incomplete Resource Region",
        "groups": [{
            "id": "main",
            "kind": "main",
            "steps": [{
                "id": "001",
                "name": "Start",
                "kind": "noop",
                "resourceRegionStart": {
                    "id": "region-001",
                    "resources": [{"resourceId": "CAN1", "mode": "exclusive"}]
                }
            }, {
                "id": "002",
                "name": "Still Running",
                "kind": "noop"
            }]
        }]
    })json";

    SequenceCompiler compiler;
    const auto result = compiler.compileJson(QJsonDocument::fromJson(json).object());
    QVERIFY(!result.ok());
    QVERIFY(std::any_of(result.errors.cbegin(), result.errors.cend(),
                        [](const CompileError& error) {
                            return error.message.contains("no exit marker",
                                                          Qt::CaseInsensitive);
                        }));
}

void CoreTests::executionSessionStopRunsCleanupOnly()
{
    ExecutionPlan plan;
    plan.id = "plan-stop";

    ExecNode action;
    action.id = "normal-action";
    action.displayName = "Normal Action";
    action.kind = ExecNodeKind::Action;
    action.alwaysRun = true;
    QVERIFY(plan.addNode(action));

    ExecNode cleanup;
    cleanup.id = "power-off";
    cleanup.displayName = "Power Off";
    cleanup.kind = ExecNodeKind::Cleanup;
    cleanup.alwaysRun = true;
    QVERIFY(plan.addNode(cleanup));

    CleanupRegion region;
    region.id = "main-cleanup";
    region.entryNodes = {"power-off"};
    region.triggers = {CleanupReason::UserStop};
    plan.cleanupRegions.push_back(region);

    ExecutionSession session(plan);
    session.addUut("uut-1");
    session.requestStop();

    const auto result = session.run();
    QVERIFY(result.completed);
    QCOMPARE(result.state, ExecutionState::Completed);

    const auto& uut = session.uuts().first();
    QCOMPARE(uut.outcomeOf("normal-action"), NodeOutcome::Skipped);
    QCOMPARE(session.report().sessionSteps.first().outcome, NodeOutcome::Passed);
}

void CoreTests::stopTokenEscalatesAtomically()
{
    StopToken token;
    QVERIFY(!token.isStopRequested());
    QCOMPARE(token.requestedMode(), StopMode::Graceful);

    token.requestStop(StopMode::Graceful);
    QVERIFY(token.isStopRequested());
    QCOMPARE(token.requestedMode(), StopMode::Graceful);

    token.requestStop(StopMode::Abort);
    QCOMPARE(token.requestedMode(), StopMode::Abort);

    token.requestStop(StopMode::Graceful);
    QCOMPARE(token.requestedMode(), StopMode::Abort);
}

void CoreTests::executionSessionConsumesCrossThreadStopToken()
{
    ExecutionPlan plan;
    plan.id = "plan-cross-thread-stop";

    for (int index = 1; index <= 3; ++index) {
        ExecNode wait;
        wait.id = QString("wait-%1").arg(index);
        wait.displayName = wait.id;
        wait.kind = ExecNodeKind::Wait;
        wait.payload.insert("ms", 5000);
        QVERIFY(plan.addNode(wait));
        if (index > 1) {
            plan.addEdge({QString("edge-%1").arg(index),
                          QString("wait-%1").arg(index - 1),
                          wait.id,
                          EdgeKind::Dependency,
                          EdgeTrigger::OnSuccess,
                          {},
                          0});
        }
    }

    ExecNode cleanup;
    cleanup.id = "cleanup";
    cleanup.displayName = "Cleanup";
    cleanup.kind = ExecNodeKind::Cleanup;
    cleanup.alwaysRun = true;
    QVERIFY(plan.addNode(cleanup));

    CleanupRegion region;
    region.id = "stop-cleanup";
    region.entryNodes = {"cleanup"};
    region.triggers = {CleanupReason::UserStop, CleanupReason::UserAbort};
    plan.cleanupRegions.push_back(region);

    auto stopToken = std::make_shared<StopToken>();
    ExecutionSession session(plan, stopToken);
    session.addUut("uut-1");

    ExecutionSessionResult result;
    QElapsedTimer elapsed;
    elapsed.start();
    std::thread runner([&session, &result] {
        result = session.run();
    });

    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    stopToken->requestStop();
    runner.join();

    QVERIFY2(elapsed.elapsed() < 1500, "Stop should cancel a pending timer without waiting for its deadline");
    QVERIFY(result.completed);
    QCOMPARE(result.state, ExecutionState::Completed);
    const auto& uut = session.uuts().first();
    QCOMPARE(uut.outcomeOf("wait-1"), NodeOutcome::Skipped);
    QCOMPARE(uut.outcomeOf("wait-2"), NodeOutcome::Skipped);
    QCOMPARE(uut.outcomeOf("wait-3"), NodeOutcome::Skipped);
    QCOMPARE(session.snapshot().sessionExecution.outcomeOf("cleanup"), NodeOutcome::Passed);
}

void CoreTests::executionSessionWaitDoesNotBlockOtherUuts()
{
    ExecutionPlan plan;
    plan.id = "plan-nonblocking-wait";

    ExecNode wait;
    wait.id = "settle";
    wait.displayName = "Settle";
    wait.kind = ExecNodeKind::Wait;
    wait.payload.insert("ms", 100);
    QVERIFY(plan.addNode(wait));

    ExecNode after;
    after.id = "after";
    after.displayName = "After";
    after.kind = ExecNodeKind::Noop;
    QVERIFY(plan.addNode(after));
    plan.addEdge({"settle-after",
                  "settle",
                  "after",
                  EdgeKind::Dependency,
                  EdgeTrigger::OnSuccess,
                  {},
                  0});

    CollectingRuntimeEventSink events;
    ExecutionSession session(plan, {}, &events);
    session.addUut("uut-1");
    session.addUut("uut-2");

    const auto result = session.run();
    QVERIFY(result.completed);
    QCOMPARE(result.state, ExecutionState::Completed);
    QCOMPARE(session.uuts()[0].outcomeOf("settle"), NodeOutcome::Passed);
    QCOMPARE(session.uuts()[1].outcomeOf("settle"), NodeOutcome::Passed);

    int uut1Started = -1;
    int uut2Started = -1;
    int uut1Completed = -1;
    RequestId uut1RequestId;
    RequestId uut2RequestId;
    const auto records = events.records();
    for (int index = 0; index < records.size(); ++index) {
        const auto& event = records[index];
        if (event.nodeId != "settle") {
            continue;
        }
        if (event.kind == RuntimeEventKind::AttemptStarted && event.uutId == "uut-1") {
            uut1Started = index;
            uut1RequestId = event.requestId;
        } else if (event.kind == RuntimeEventKind::AttemptStarted && event.uutId == "uut-2") {
            uut2Started = index;
            uut2RequestId = event.requestId;
        } else if (event.kind == RuntimeEventKind::AttemptCompleted && event.uutId == "uut-1") {
            uut1Completed = index;
            QCOMPARE(event.requestId, uut1RequestId);
        }
    }

    QVERIFY(uut1Started >= 0);
    QVERIFY(uut2Started > uut1Started);
    QVERIFY(uut1Completed > uut2Started);
    QVERIFY(!uut1RequestId.isEmpty());
    QVERIFY(!uut2RequestId.isEmpty());
    QVERIFY(uut1RequestId != uut2RequestId);

    const auto report = session.report();
    QCOMPARE(report.uuts.size(), 2);
    const auto* uut1Wait = findStep(report.uuts[0], "settle");
    const auto* uut2Wait = findStep(report.uuts[1], "settle");
    QVERIFY(uut1Wait);
    QVERIFY(uut2Wait);
    QCOMPARE(uut1Wait->attempts.size(), 1);
    QCOMPARE(uut2Wait->attempts.size(), 1);
    QCOMPARE(uut1Wait->attempts.first().requestId, uut1RequestId);
    QCOMPARE(uut2Wait->attempts.first().requestId, uut2RequestId);
}

void CoreTests::executionSessionPausesAtNodeBoundaryAndResumes()
{
    ExecutionPlan plan;
    plan.id = "plan-pause-resume";

    ExecNode first;
    first.id = "first";
    first.displayName = "First";
    first.kind = ExecNodeKind::Wait;
    first.payload.insert("ms", 100);
    first.resources.push_back({"DMM1", ResourceMode::Exclusive, 1, 0});
    QVERIFY(plan.addNode(first));

    ExecNode second;
    second.id = "second";
    second.displayName = "Second";
    second.kind = ExecNodeKind::Noop;
    QVERIFY(plan.addNode(second));
    plan.addEdge({"first-to-second",
                  "first",
                  "second",
                  EdgeKind::Dependency,
                  EdgeTrigger::OnSuccess,
                  {},
                  0});

    auto stopToken = std::make_shared<StopToken>();
    auto control = std::make_shared<ExecutionControl>();
    CollectingRuntimeEventSink events;
    ExecutionSession session(plan, stopToken, &events, control);
    session.addUut("uut-1");

    ExecutionSessionResult result;
    std::thread runner([&] { result = session.run(); });
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    QVERIFY(control->requestPause());

    QTRY_VERIFY_WITH_TIMEOUT(control->state() == ExecutionControlState::Paused, 1000);
    QCOMPARE(session.uuts().first().outcomeOf("first"), NodeOutcome::Passed);
    QCOMPARE(session.uuts().first().stateOf("second"), ActivationState::Created);
    QVERIFY(session.snapshot().resources.activeLeases.isEmpty());

    bool sawPausedEvent = false;
    for (const auto& event : events.records()) {
        sawPausedEvent = sawPausedEvent ||
                         (event.kind == RuntimeEventKind::SessionStateChanged &&
                          event.executionState == ExecutionState::Paused);
    }
    QVERIFY(sawPausedEvent);

    control->resume();
    runner.join();

    QVERIFY(result.completed);
    QCOMPARE(result.state, ExecutionState::Completed);
    QCOMPARE(session.uuts().first().outcomeOf("second"), NodeOutcome::Passed);
}

void CoreTests::executionSessionPausePreservesMultiUutBarrierState()
{
    ExecutionPlan plan;
    plan.id = "plan-pause-barrier";

    ExecNode barrier;
    barrier.id = "batch-ready";
    barrier.kind = ExecNodeKind::Barrier;
    barrier.payload.insert("barrierName", "batch-ready");
    barrier.payload.insert("cohortId", "batch-1");
    QVERIFY(plan.addNode(barrier));

    ExecNode after;
    after.id = "after-barrier";
    after.kind = ExecNodeKind::Noop;
    QVERIFY(plan.addNode(after));
    plan.addEdge({"barrier-after",
                  "batch-ready",
                  "after-barrier",
                  EdgeKind::Control,
                  EdgeTrigger::OnSuccess,
                  {},
                  0});

    auto control = std::make_shared<ExecutionControl>();
    PauseOnBarrierEventSink events(control);
    ExecutionSession session(plan, {}, &events, control);
    session.addUut("uut-1");
    session.addUut("uut-2");

    ExecutionSessionResult result;
    std::thread runner([&] { result = session.run(); });
    QTRY_VERIFY_WITH_TIMEOUT(control->state() == ExecutionControlState::Paused, 1000);

    QCOMPARE(session.uuts()[0].stateOf("batch-ready"), ActivationState::WaitingAtBarrier);
    QCOMPARE(session.uuts()[1].stateOf("batch-ready"), ActivationState::Created);
    const auto barriers = session.snapshot().barriers;
    QCOMPARE(barriers.size(), 1);
    QCOMPARE(barriers.first().state, BarrierState::Waiting);
    QVERIFY(barriers.first().arrived.contains("uut-1"));
    QVERIFY(!barriers.first().arrived.contains("uut-2"));

    control->resume();
    runner.join();

    QVERIFY(result.completed);
    QCOMPARE(result.state, ExecutionState::Completed);
    for (const auto& uut : session.uuts()) {
        QCOMPARE(uut.outcomeOf("batch-ready"), NodeOutcome::Passed);
        QCOMPARE(uut.outcomeOf("after-barrier"), NodeOutcome::Passed);
    }
}

void CoreTests::executionSessionStopWakesPausedRunAndRunsCleanup()
{
    ExecutionPlan plan;
    plan.id = "plan-stop-paused";

    ExecNode first;
    first.id = "first";
    first.kind = ExecNodeKind::Wait;
    first.payload.insert("ms", 100);
    QVERIFY(plan.addNode(first));

    ExecNode second;
    second.id = "second";
    second.kind = ExecNodeKind::Noop;
    QVERIFY(plan.addNode(second));
    plan.addEdge({"first-to-second",
                  "first",
                  "second",
                  EdgeKind::Dependency,
                  EdgeTrigger::OnSuccess,
                  {},
                  0});

    ExecNode cleanup;
    cleanup.id = "cleanup";
    cleanup.kind = ExecNodeKind::Cleanup;
    cleanup.alwaysRun = true;
    QVERIFY(plan.addNode(cleanup));

    CleanupRegion region;
    region.id = "pause-stop-cleanup";
    region.entryNodes = {"cleanup"};
    region.triggers = {CleanupReason::UserStop, CleanupReason::UserAbort};
    plan.cleanupRegions.push_back(region);

    auto stopToken = std::make_shared<StopToken>();
    auto control = std::make_shared<ExecutionControl>();
    ExecutionSession session(plan, stopToken, nullptr, control);
    session.addUut("uut-1");

    ExecutionSessionResult result;
    std::thread runner([&] { result = session.run(); });
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    QVERIFY(control->requestPause());
    QTRY_VERIFY_WITH_TIMEOUT(control->state() == ExecutionControlState::Paused, 1000);

    // Exercise callers that own StopToken directly; the paused wait must still wake.
    stopToken->requestStop(StopMode::Graceful);
    runner.join();

    QVERIFY(result.completed);
    QCOMPARE(result.state, ExecutionState::Completed);
    QCOMPARE(control->state(), ExecutionControlState::Running);
    const auto& uut = session.uuts().first();
    QCOMPARE(uut.outcomeOf("first"), NodeOutcome::Passed);
    QCOMPARE(uut.outcomeOf("second"), NodeOutcome::Skipped);
    QCOMPARE(session.snapshot().sessionExecution.outcomeOf("cleanup"), NodeOutcome::Passed);
}

void CoreTests::breakpointAddressResolvesNestedLocalPaths()
{
    const auto json = R"json({
      "id": "debug-addresses",
      "name": "Debug Addresses",
      "groups": [{
        "id": "main",
        "kind": "main",
        "steps": [
          {
            "id": "001",
            "kind": "testItem",
            "steps": [
              { "id": "01", "kind": "action" },
              { "id": "03", "kind": "action" }
            ]
          },
          {
            "id": "002",
            "kind": "testItem",
            "steps": [
              { "id": "03", "kind": "action" }
            ]
          }
        ]
      }]
    })json";

    SequenceCompiler compiler;
    const auto compile = compiler.compileJson(QJsonDocument::fromJson(json).object());
    QVERIFY2(compile.ok(), qPrintable(compile.errors.isEmpty() ? QString() : compile.errors.first().message));

    QCOMPARE(debugLocalPathForNode(compile.plan, "001.03"), QString("001/03"));
    QCOMPARE(debugLocalPathForNode(compile.plan, "002.03"), QString("002/03"));

    const auto first = resolveBreakpointAddress(
        compile.plan, BreakpointAddress::localPath("001/03"));
    QVERIFY(first.has_value());
    QCOMPARE(*first, NodeId("001.03"));

    const auto firstWithDots = resolveBreakpointAddress(
        compile.plan, BreakpointAddress::localPath("001.03"));
    QVERIFY(firstWithDots.has_value());
    QCOMPARE(*firstWithDots, NodeId("001.03"));

    const auto second = resolveBreakpointAddress(
        compile.plan, BreakpointAddress::nodePath("002.03"));
    QVERIFY(second.has_value());
    QCOMPARE(*second, NodeId("002.03"));
}

void CoreTests::executionSessionBreakpointPausesBeforeNodeAndExposesDebugSnapshot()
{
    ExecutionPlan plan;
    plan.id = "plan-breakpoint";

    ExecNode first;
    first.id = "first";
    first.localId = "001";
    first.displayName = "First";
    first.kind = ExecNodeKind::Noop;
    QVERIFY(plan.addNode(first));

    ExecNode second;
    second.id = "second";
    second.localId = "002";
    second.displayName = "Second";
    second.kind = ExecNodeKind::Noop;
    QVERIFY(plan.addNode(second));
    plan.addEdge({"first-to-second",
                  "first",
                  "second",
                  EdgeKind::Dependency,
                  EdgeTrigger::OnSuccess,
                  {},
                  0});

    auto control = std::make_shared<ExecutionControl>();
    BreakpointSpec breakpoint;
    breakpoint.id = "bp-second";
    breakpoint.address = BreakpointAddress::nodePath("second");
    control->setBreakpoints({breakpoint});

    CollectingRuntimeEventSink events;
    ExecutionSession session(plan, {}, &events, control);
    session.addUut("uut-1");

    ExecutionSessionResult result;
    std::thread runner([&] { result = session.run(); });

    QTRY_VERIFY_WITH_TIMEOUT(control->state() == ExecutionControlState::Paused, 1000);
    QCOMPARE(session.uuts().first().outcomeOf("first"), NodeOutcome::Passed);
    QCOMPARE(session.uuts().first().stateOf("second"), ActivationState::Created);

    const auto snapshot = control->debugSnapshot();
    QVERIFY(snapshot.has_value());
    QCOMPARE(snapshot->pauseReason, DebugPauseReason::Breakpoint);
    QCOMPARE(snapshot->currentUutId, UutId("uut-1"));
    QCOMPARE(snapshot->currentNodeId, NodeId("second"));
    QCOMPARE(snapshot->currentLocalPath, QString("002"));
    QCOMPARE(snapshot->uuts.size(), 1);

    const auto& uutSnapshot = snapshot->uuts.first();
    const auto findNode = [&uutSnapshot](const NodeId& nodeId) -> const DebugNodeSnapshot* {
        for (const auto& node : uutSnapshot.nodes) {
            if (node.nodeId == nodeId) {
                return &node;
            }
        }
        return nullptr;
    };
    const auto* firstSnapshot = findNode("first");
    const auto* secondSnapshot = findNode("second");
    QVERIFY(firstSnapshot != nullptr);
    QVERIFY(secondSnapshot != nullptr);
    QCOMPARE(firstSnapshot->state, ActivationState::Passed);
    QCOMPARE(secondSnapshot->state, ActivationState::Created);

    bool sawBreakpointEvent = false;
    for (const auto& event : events.records()) {
        sawBreakpointEvent = sawBreakpointEvent ||
                             (event.kind == RuntimeEventKind::BreakpointHit &&
                              event.nodeId == "second" &&
                              event.details.value("breakpointId").toString() == "bp-second");
    }
    QVERIFY(sawBreakpointEvent);
    QCOMPARE(control->breakpoints().first().hitCount, 1);

    control->resume();
    runner.join();

    QVERIFY(result.completed);
    QCOMPARE(result.state, ExecutionState::Completed);
    QCOMPARE(session.uuts().first().outcomeOf("second"), NodeOutcome::Passed);
    QCOMPARE(control->breakpoints().first().hitCount, 1);
}

void CoreTests::disabledBreakpointDoesNotPause()
{
    ExecutionPlan plan;
    plan.id = "plan-disabled-breakpoint";

    ExecNode node;
    node.id = "only-step";
    node.kind = ExecNodeKind::Noop;
    QVERIFY(plan.addNode(node));

    auto control = std::make_shared<ExecutionControl>();
    BreakpointSpec breakpoint;
    breakpoint.id = "bp-disabled";
    breakpoint.address = BreakpointAddress::nodePath("only-step");
    breakpoint.enabled = false;
    control->setBreakpoints({breakpoint});

    ExecutionSession session(plan, {}, nullptr, control);
    session.addUut("uut-1");
    const auto result = session.run();

    QVERIFY(result.completed);
    QCOMPARE(session.uuts().first().outcomeOf("only-step"), NodeOutcome::Passed);
    QCOMPARE(control->state(), ExecutionControlState::Running);
    QCOMPARE(control->breakpoints().first().hitCount, 0);
    QVERIFY(!control->debugSnapshot().has_value());
}

void CoreTests::executionSessionStepIntoRunsOneNodeAndPausesAgain()
{
    ExecutionPlan plan;
    plan.id = "plan-step-into";

    ExecNode first;
    first.id = "first";
    first.displayName = "First";
    first.kind = ExecNodeKind::Wait;
    first.payload.insert("ms", 50);
    QVERIFY(plan.addNode(first));

    ExecNode second;
    second.id = "second";
    second.displayName = "Second";
    second.kind = ExecNodeKind::Noop;
    QVERIFY(plan.addNode(second));
    plan.addEdge({"first-to-second",
                  "first",
                  "second",
                  EdgeKind::Dependency,
                  EdgeTrigger::OnSuccess,
                  {},
                  0});

    auto control = std::make_shared<ExecutionControl>();
    BreakpointSpec breakpoint;
    breakpoint.id = "bp-first";
    breakpoint.address = BreakpointAddress::nodePath("first");
    control->setBreakpoints({breakpoint});

    CollectingRuntimeEventSink events;
    ExecutionSession session(plan, {}, &events, control);
    session.addUut("uut-1");

    ExecutionSessionResult result;
    std::thread runner([&] { result = session.run(); });
    QTRY_VERIFY_WITH_TIMEOUT(control->state() == ExecutionControlState::Paused, 1000);
    QCOMPARE(session.uuts().first().stateOf("first"), ActivationState::Created);

    QVERIFY(control->stepInto());
    QTRY_VERIFY_WITH_TIMEOUT(control->state() == ExecutionControlState::Paused, 1000);
    QCOMPARE(session.uuts().first().outcomeOf("first"), NodeOutcome::Passed);
    QCOMPARE(session.uuts().first().stateOf("second"), ActivationState::Created);

    const auto snapshot = control->debugSnapshot();
    QVERIFY(snapshot.has_value());
    QCOMPARE(snapshot->pauseReason, DebugPauseReason::StepInto);
    QCOMPARE(snapshot->currentNodeId, NodeId("first"));

    bool sawStepEvent = false;
    for (const auto& event : events.records()) {
        sawStepEvent = sawStepEvent ||
                       (event.kind == RuntimeEventKind::DebugStepCompleted &&
                        event.nodeId == "first" &&
                        event.details.value("stepMode").toString() == "into");
    }
    QVERIFY(sawStepEvent);

    control->resume();
    runner.join();
    QVERIFY(result.completed);
    QCOMPARE(session.uuts().first().outcomeOf("second"), NodeOutcome::Passed);
}

void CoreTests::executionSessionStepOverLoopRunsWholeLoopBeforePausing()
{
    const auto json = R"json({
      "id": "debug-step-over-loop",
      "name": "Debug Step Over Loop",
      "groups": [{
        "id": "main",
        "kind": "main",
        "steps": [
          {
            "id": "repeat",
            "kind": "loop",
            "loop": { "variable": "sample", "from": 0, "to": 2, "step": 1 },
            "steps": [
              { "id": "sample", "kind": "action",
                "parameters": { "outputs": { "value": "${loop.value}" } } }
            ]
          },
          { "id": "after", "kind": "action" }
        ]
      }]
    })json";

    SequenceCompiler compiler;
    const auto compile = compiler.compileJson(QJsonDocument::fromJson(json).object());
    QVERIFY2(compile.ok(), qPrintable(compile.errors.isEmpty() ? QString() : compile.errors.first().message));

    auto control = std::make_shared<ExecutionControl>();
    BreakpointSpec breakpoint;
    breakpoint.id = "bp-loop";
    breakpoint.address = BreakpointAddress::nodePath("repeat");
    control->setBreakpoints({breakpoint});

    ExecutionSession session(compile.plan, {}, nullptr, control);
    session.addUut("uut-1");

    ExecutionSessionResult result;
    std::thread runner([&] { result = session.run(); });
    QTRY_VERIFY_WITH_TIMEOUT(control->state() == ExecutionControlState::Paused, 1000);
    QCOMPARE(session.uuts().first().stateOf("repeat"), ActivationState::Created);

    QVERIFY(control->stepOver());
    QTRY_VERIFY_WITH_TIMEOUT(control->state() == ExecutionControlState::Paused, 1000);

    const auto& uut = session.uuts().first();
    QCOMPARE(uut.outcomeOf("repeat"), NodeOutcome::Passed);
    QCOMPARE(uut.stateOf("after"), ActivationState::Created);
    QCOMPARE(session.results().history("uut-1", "root", "repeat.sample").size(), 3);
    const auto snapshot = control->debugSnapshot();
    QVERIFY(snapshot.has_value());
    QCOMPARE(snapshot->pauseReason, DebugPauseReason::StepOver);
    QCOMPARE(snapshot->currentNodeId, NodeId("repeat"));

    control->resume();
    runner.join();
    QVERIFY(result.completed);
    QCOMPARE(session.uuts().first().outcomeOf("after"), NodeOutcome::Passed);
}

void CoreTests::executionSessionStepOverSuppressesBreakpointsInsideLoop()
{
    const auto json = R"json({
      "id": "debug-step-over-loop-breakpoints",
      "name": "Debug Step Over Loop Breakpoints",
      "groups": [{
        "id": "main",
        "kind": "main",
        "steps": [{
          "id": "repeat",
          "kind": "loop",
          "loop": { "variable": "sample", "from": 0, "to": 2, "step": 1 },
          "steps": [{ "id": "sample", "kind": "action" }]
        }]
      }]
    })json";

    SequenceCompiler compiler;
    const auto compile = compiler.compileJson(QJsonDocument::fromJson(json).object());
    QVERIFY(compile.ok());

    auto control = std::make_shared<ExecutionControl>();
    BreakpointSpec loopBreakpoint;
    loopBreakpoint.id = "bp-loop";
    loopBreakpoint.address = BreakpointAddress::nodePath("repeat");
    BreakpointSpec bodyBreakpoint;
    bodyBreakpoint.id = "bp-body";
    bodyBreakpoint.address = BreakpointAddress::nodePath("repeat.sample");
    control->setBreakpoints({loopBreakpoint, bodyBreakpoint});

    ExecutionSession session(compile.plan, {}, nullptr, control);
    session.addUut("uut-1");

    ExecutionSessionResult result;
    std::thread runner([&] { result = session.run(); });
    QTRY_VERIFY_WITH_TIMEOUT(control->state() == ExecutionControlState::Paused, 1000);
    QVERIFY(control->stepOver());
    QTRY_VERIFY_WITH_TIMEOUT(control->state() == ExecutionControlState::Paused, 1000);

    const auto breakpoints = control->breakpoints();
    QCOMPARE(breakpoints[0].hitCount, 1);
    QCOMPARE(breakpoints[1].hitCount, 0);
    QCOMPARE(session.results().history("uut-1", "root", "repeat.sample").size(), 3);

    control->resume();
    runner.join();
    QVERIFY(result.completed);
}

void CoreTests::sequenceDefModelsSetupMainCleanup()
{
    SequenceDef sequence;
    sequence.id = "seq-power-on";
    sequence.name = "Power On";
    sequence.version = "0.1.0";

    StepGroupDef setup;
    setup.id = "setup";
    setup.name = "Setup";
    setup.kind = StepGroupKind::Setup;
    setup.addStep({"open-fixture", "Open Fixture", StepKind::Action});

    StepGroupDef main;
    main.id = "main";
    main.name = "Main";
    main.kind = StepGroupKind::Main;
    main.addStep({"measure-voltage", "Measure Voltage", StepKind::Action});

    StepGroupDef cleanup;
    cleanup.id = "cleanup";
    cleanup.name = "Cleanup";
    cleanup.kind = StepGroupKind::Cleanup;
    cleanup.addStep({"power-off", "Power Off", StepKind::Cleanup});

    sequence.addGroup(setup);
    sequence.addGroup(main);
    sequence.addGroup(cleanup);

    QCOMPARE(sequence.groups.size(), 3);
    QCOMPARE(sequence.allSteps().size(), 3);
    QCOMPARE(stepGroupKindName(sequence.groups[0].kind), QString("Setup"));
    QCOMPARE(stepKindName(sequence.groups[2].steps[0].kind), QString("Cleanup"));

    const auto step = sequence.stepById("measure-voltage");
    QVERIFY(step.has_value());
    QCOMPARE(step->name, QString("Measure Voltage"));
}

void CoreTests::sequenceDefDetectsDuplicateStepIds()
{
    SequenceDef sequence;
    sequence.id = "seq-duplicates";

    StepGroupDef main;
    main.id = "main";
    main.kind = StepGroupKind::Main;
    main.addStep({"same-id", "A", StepKind::Noop});
    main.addStep({"same-id", "B", StepKind::Wait});

    sequence.addGroup(main);

    QVERIFY(sequence.hasDuplicateStepIds());
    QCOMPARE(sequence.duplicateStepIds(), QVector<QString>{"same-id"});
}

void CoreTests::sequenceDefPreservesBarrierAndResourcePolicies()
{
    StepDef barrier;
    barrier.id = "batch-ready";
    barrier.name = "Batch Ready";
    barrier.kind = StepKind::Barrier;
    barrier.barrier.barrierName = "batch-ready";
    barrier.barrier.cohortId = "batch-1";
    barrier.barrier.arrivalPolicy = BarrierArrivalPolicy::DropFailed;
    barrier.barrier.failurePolicy = BarrierFailurePolicy::RemoveFailedMember;
    barrier.barrier.timeoutPolicy = BarrierTimeoutPolicy::ReleaseArrived;

    ResourceRequirementDef dmm;
    dmm.resourceId = "Instrument.DMM1";
    dmm.mode = ResourceMode::Exclusive;
    dmm.priority = 7;
    barrier.resources.push_back(dmm);

    barrier.retry.maxAttempts = 3;
    barrier.timeout.timeoutMs = 5000;
    barrier.errorPolicy.cleanupRegionId = "main-cleanup";
    barrier.errorPolicy.onError = OnFailureAction::Abort;
    barrier.errorPolicy.onTimeout = OnFailureAction::RunCleanup;

    const auto payload = barrier.barrier.toPayload();
    QCOMPARE(payload.value("arrivalPolicy").toString(), QString("DropFailed"));
    QCOMPARE(payload.value("failurePolicy").toString(), QString("RemoveFailedMember"));
    QCOMPARE(payload.value("timeoutPolicy").toString(), QString("ReleaseArrived"));

    const auto runtimeResource = barrier.resources.first().toRuntimeRequirement();
    QCOMPARE(runtimeResource.resourceId, QString("Instrument.DMM1"));
    QCOMPARE(runtimeResource.mode, ResourceMode::Exclusive);
    QCOMPARE(runtimeResource.priority, 7);

    QCOMPARE(barrier.retry.toRuntimePolicy().maxAttempts, 3);
    QCOMPARE(barrier.timeout.toRuntimePolicy().timeoutMs, 5000);
    const auto runtimeError = barrier.errorPolicy.toRuntimePolicy();
    QCOMPARE(runtimeError.cleanupRegionId, QString("main-cleanup"));
    QCOMPARE(runtimeError.onFail, ErrorAction::StopUut);
    QCOMPARE(runtimeError.onError, ErrorAction::Abort);
    QCOMPARE(runtimeError.onTimeout, ErrorAction::RunCleanup);
    QCOMPARE(toExecNodeKind(barrier.kind), ExecNodeKind::Barrier);
}

void CoreTests::errorPolicyDefMapsFailureActions()
{
    QCOMPARE(toErrorAction(OnFailureAction::Continue), ErrorAction::Continue);
    QCOMPARE(toErrorAction(OnFailureAction::StopUut), ErrorAction::StopUut);
    QCOMPARE(toErrorAction(OnFailureAction::Retry), ErrorAction::Retry);
    QCOMPARE(toErrorAction(OnFailureAction::RunCleanup), ErrorAction::RunCleanup);
    QCOMPARE(toErrorAction(OnFailureAction::Abort), ErrorAction::Abort);
    QCOMPARE(errorActionName(ErrorAction::RunCleanup), QString("RunCleanup"));
}

void CoreTests::errorPolicyEngineUsesOutcomeSpecificActions()
{
    ErrorPolicyEngine engine;
    ExecNode node;
    node.id = "measure";
    node.retry.maxAttempts = 1;
    node.errorPolicy.onFail = ErrorAction::Continue;
    node.errorPolicy.onError = ErrorAction::Abort;
    node.errorPolicy.onTimeout = ErrorAction::RunCleanup;
    node.errorPolicy.cleanupRegionId = "main-cleanup";

    NodeResult failed;
    failed.nodeId = node.id;
    failed.outcome = NodeOutcome::Failed;
    auto decision = engine.decide(node, failed, 1);
    QCOMPARE(decision.action, ErrorAction::Continue);

    NodeResult error;
    error.nodeId = node.id;
    error.outcome = NodeOutcome::Error;
    decision = engine.decide(node, error, 1);
    QCOMPARE(decision.action, ErrorAction::RunCleanup);
    QCOMPARE(decision.cleanupRegionId, QString("main-cleanup"));
    QCOMPARE(decision.cleanupReason, CleanupReason::ModuleError);

    NodeResult timeout;
    timeout.nodeId = node.id;
    timeout.outcome = NodeOutcome::Timeout;
    decision = engine.decide(node, timeout, 1);
    QCOMPARE(decision.action, ErrorAction::RunCleanup);
    QCOMPARE(decision.cleanupReason, CleanupReason::Timeout);
}

void CoreTests::stationFailureHandlingOverridesNodePolicies()
{
    ExecNode node;
    node.id = "measure";
    node.retry.maxAttempts = 1;
    node.errorPolicy.onFail = ErrorAction::Continue;
    node.errorPolicy.onError = ErrorAction::Continue;
    node.errorPolicy.onTimeout = ErrorAction::Continue;
    node.errorPolicy.stopUutOnFailure = false;

    ErrorPolicyEngine stopEngine(FailureHandlingMode::Stop);
    for (const auto outcome : {NodeOutcome::Failed,
                               NodeOutcome::Error,
                               NodeOutcome::Timeout}) {
        NodeResult result;
        result.nodeId = node.id;
        result.outcome = outcome;
        QCOMPARE(stopEngine.decide(node, result, 1).action,
                 ErrorAction::StopUut);
    }

    node.errorPolicy.onFail = ErrorAction::StopUut;
    node.errorPolicy.onError = ErrorAction::Abort;
    node.errorPolicy.onTimeout = ErrorAction::RunCleanup;
    node.errorPolicy.stopUutOnFailure = true;
    ErrorPolicyEngine continueEngine(FailureHandlingMode::Continue);
    for (const auto outcome : {NodeOutcome::Failed,
                               NodeOutcome::Error,
                               NodeOutcome::Timeout}) {
        NodeResult result;
        result.nodeId = node.id;
        result.outcome = outcome;
        QCOMPARE(continueEngine.decide(node, result, 1).action,
                 ErrorAction::Continue);
    }

    NodeResult cancelled;
    cancelled.nodeId = node.id;
    cancelled.outcome = NodeOutcome::Cancelled;
    QCOMPARE(continueEngine.decide(node, cancelled, 1).action,
             ErrorAction::StopUut);

    node.retry.maxAttempts = 2;
    NodeResult failed;
    failed.nodeId = node.id;
    failed.outcome = NodeOutcome::Failed;
    QCOMPARE(stopEngine.decide(node, failed, 1).action, ErrorAction::Retry);
    QCOMPARE(continueEngine.decide(node, failed, 1).action, ErrorAction::Retry);
    QCOMPARE(stopEngine.decide(node, failed, 2).action, ErrorAction::StopUut);
    QCOMPARE(continueEngine.decide(node, failed, 2).action, ErrorAction::Continue);

    StationConfig station;
    station.stopOnFailure = true;
    QCOMPARE(failureHandlingMode(station), FailureHandlingMode::Stop);
    station.stopOnFailure = false;
    QCOMPARE(failureHandlingMode(station), FailureHandlingMode::Continue);
}

void CoreTests::planBuilderBuildsSetupMainCleanupPlan()
{
    SequenceDef sequence;
    sequence.id = "seq-builder";
    sequence.name = "Builder Sequence";
    sequence.version = "0.1.0";

    StepGroupDef setup;
    setup.id = "setup";
    setup.kind = StepGroupKind::Setup;
    setup.addStep({"open-fixture", "Open Fixture", StepKind::Action});

    StepGroupDef main;
    main.id = "main";
    main.kind = StepGroupKind::Main;

    StepDef barrier;
    barrier.id = "batch-ready";
    barrier.name = "Batch Ready";
    barrier.kind = StepKind::Barrier;
    barrier.barrier.barrierName = "batch-ready";
    barrier.barrier.arrivalPolicy = BarrierArrivalPolicy::DropFailed;
    barrier.barrier.failurePolicy = BarrierFailurePolicy::RemoveFailedMember;

    StepDef measure;
    measure.id = "measure";
    measure.name = "Measure";
    measure.kind = StepKind::Action;
    measure.resources.push_back({"Instrument.DMM1", ResourceMode::Exclusive, 1, 5, 30000});
    measure.retry.maxAttempts = 2;
    measure.errorPolicy.onError = OnFailureAction::Abort;

    main.addStep(barrier);
    main.addStep(measure);

    StepGroupDef cleanup;
    cleanup.id = "cleanup";
    cleanup.kind = StepGroupKind::Cleanup;
    cleanup.addStep({"power-off", "Power Off", StepKind::Cleanup});

    sequence.addGroup(setup);
    sequence.addGroup(main);
    sequence.addGroup(cleanup);

    PlanBuilder builder;
    const auto result = builder.build(sequence);
    QVERIFY(result.ok());
    QCOMPARE(result.plan.nodes.size(), 4);
    QCOMPARE(result.plan.cleanupRegions.size(), 1);
    QCOMPARE(result.plan.entryNodeId, QString("open-fixture"));
    QCOMPARE(result.plan.exitNodeId, QString("power-off"));

    const auto* measureNode = result.plan.node("measure");
    QVERIFY(measureNode != nullptr);
    QCOMPARE(measureNode->resources.size(), 1);
    QCOMPARE(measureNode->resources.first().resourceId, QString("Instrument.DMM1"));
    QCOMPARE(measureNode->retry.maxAttempts, 2);
    QCOMPARE(measureNode->errorPolicy.cleanupRegionId, QString("main-cleanup"));
    QCOMPARE(measureNode->errorPolicy.onError, ErrorAction::Abort);

    const auto* barrierNode = result.plan.node("batch-ready");
    QVERIFY(barrierNode != nullptr);
    QCOMPARE(barrierNode->kind, ExecNodeKind::Barrier);
    QCOMPARE(barrierNode->payload.value("arrivalPolicy").toString(), QString("DropFailed"));
    QCOMPARE(barrierNode->payload.value("failurePolicy").toString(), QString("RemoveFailedMember"));

    const auto* cleanupNode = result.plan.node("power-off");
    QVERIFY(cleanupNode != nullptr);
    QVERIFY(cleanupNode->alwaysRun);

    const auto finallyIt = std::find_if(result.plan.edges.cbegin(),
                                        result.plan.edges.cend(),
                                        [](const ExecEdge& edge) {
                                            return edge.kind == EdgeKind::Finally &&
                                                   edge.to == "power-off";
                                        });
    QVERIFY(finallyIt != result.plan.edges.cend());
}

void CoreTests::planBuilderRejectsDuplicateStepIds()
{
    SequenceDef sequence;
    sequence.id = "seq-invalid";
    sequence.name = "Invalid";

    StepGroupDef main;
    main.id = "main";
    main.kind = StepGroupKind::Main;
    main.addStep({"same", "A", StepKind::Noop});
    main.addStep({"same", "B", StepKind::Noop});
    sequence.addGroup(main);

    PlanBuilder builder;
    const auto result = builder.build(sequence);
    QVERIFY(!result.ok());
    QVERIFY(std::any_of(result.errors.cbegin(), result.errors.cend(), [](const PlanBuildError& error) {
        return error.message.contains("Duplicate sibling step id");
    }));
}

void CoreTests::planBuilderSkipsDisabledAndBridgesCustomGroups()
{
    SequenceDef sequence;
    sequence.id = "seq-disabled-custom";
    sequence.name = "Disabled Custom";

    StepGroupDef setup;
    setup.id = "setup";
    setup.kind = StepGroupKind::Setup;
    setup.addStep({"open-fixture", "Open Fixture", StepKind::Action});

    StepGroupDef custom;
    custom.id = "operator-checks";
    custom.kind = StepGroupKind::Custom;
    custom.addStep({"operator-check", "Operator Check", StepKind::Action});

    StepGroupDef main;
    main.id = "main";
    main.kind = StepGroupKind::Main;

    StepDef disabledStep;
    disabledStep.id = "disabled-measure";
    disabledStep.name = "Disabled Measure";
    disabledStep.kind = StepKind::Action;
    disabledStep.enabled = false;
    main.addStep(disabledStep);
    main.addStep({"measure", "Measure", StepKind::Action});

    StepGroupDef disabledGroup;
    disabledGroup.id = "disabled-group";
    disabledGroup.kind = StepGroupKind::Main;
    disabledGroup.enabled = false;
    disabledGroup.addStep({"disabled-group-step", "Disabled Group Step", StepKind::Action});

    StepGroupDef cleanup;
    cleanup.id = "cleanup";
    cleanup.kind = StepGroupKind::Cleanup;
    cleanup.addStep({"power-off", "Power Off", StepKind::Cleanup});

    sequence.addGroup(setup);
    sequence.addGroup(custom);
    sequence.addGroup(main);
    sequence.addGroup(disabledGroup);
    sequence.addGroup(cleanup);

    PlanBuilder builder;
    const auto result = builder.build(sequence);
    QVERIFY(result.ok());
    QCOMPARE(result.plan.nodes.size(), 4);
    QVERIFY(result.plan.node("disabled-measure") == nullptr);
    QVERIFY(result.plan.node("disabled-group-step") == nullptr);
    QCOMPARE(result.plan.node("open-fixture")->phase, ExecutionPhase::Setup);
    QCOMPARE(result.plan.node("operator-check")->phase, ExecutionPhase::Main);
    QCOMPARE(result.plan.node("measure")->phase, ExecutionPhase::Main);
    QCOMPARE(result.plan.node("power-off")->phase, ExecutionPhase::Cleanup);

    const auto hasEdge = [&](const NodeId& from, const NodeId& to, EdgeKind kind, EdgeTrigger trigger) {
        return std::any_of(result.plan.edges.cbegin(), result.plan.edges.cend(), [&](const ExecEdge& edge) {
            return edge.from == from && edge.to == to && edge.kind == kind && edge.trigger == trigger;
        });
    };

    QVERIFY(hasEdge("open-fixture", "operator-check", EdgeKind::Control, EdgeTrigger::OnSuccess));
    QVERIFY(hasEdge("operator-check", "measure", EdgeKind::Control, EdgeTrigger::OnSuccess));
    QVERIFY(hasEdge("measure", "power-off", EdgeKind::Finally, EdgeTrigger::Finally));
    QCOMPARE(result.plan.entryNodeId, QString("open-fixture"));
    QCOMPARE(result.plan.exitNodeId, QString("power-off"));
}

void CoreTests::planBuilderBuildsLoopRegion()
{
    SequenceDef sequence;
    sequence.id = "seq-loop";
    sequence.name = "Loop Sequence";

    StepGroupDef main;
    main.id = "main";
    main.kind = StepGroupKind::Main;

    StepDef loop;
    loop.id = "repeat-measurements";
    loop.name = "Repeat Measurements";
    loop.kind = StepKind::Loop;
    loop.loop.variableName = "sampleIndex";
    loop.loop.from = 0;
    loop.loop.to = 2;
    loop.loop.step = 1;
    loop.steps.push_back({"measure-sample", "Measure Sample", StepKind::Action});
    main.addStep(loop);
    main.addStep({"after-loop", "After Loop", StepKind::Action});

    sequence.addGroup(main);

    PlanBuilder builder;
    const auto result = builder.build(sequence);
    QVERIFY(result.ok());
    QCOMPARE(result.plan.nodes.size(), 3);
    QCOMPARE(result.plan.loopRegions.size(), 1);

    const auto* loopNode = result.plan.node("repeat-measurements");
    QVERIFY(loopNode != nullptr);
    QCOMPARE(loopNode->kind, ExecNodeKind::Loop);
    QCOMPARE(loopNode->payload.value("variable").toString(), QString("sampleIndex"));

    const auto region = result.plan.loopRegionForController("repeat-measurements");
    QVERIFY(region.has_value());
    QCOMPARE(region->bodyNodes, QVector<NodeId>{"repeat-measurements.measure-sample"});
    QCOMPARE(region->entryNodes, QVector<NodeId>{"repeat-measurements.measure-sample"});
    QCOMPARE(region->exitNodes, QVector<NodeId>{"repeat-measurements.measure-sample"});
    QCOMPARE(region->forLoop.variableName, QString("sampleIndex"));
    QCOMPARE(region->forLoop.from, 0);
    QCOMPARE(region->forLoop.to, 2);
    QCOMPARE(region->forLoop.step, 1);

    const auto hasEdge = [&](const NodeId& from, const NodeId& to) {
        return std::any_of(result.plan.edges.cbegin(), result.plan.edges.cend(), [&](const ExecEdge& edge) {
            return edge.from == from && edge.to == to;
        });
    };
    QVERIFY(hasEdge("repeat-measurements", "after-loop"));
    QVERIFY(!hasEdge("repeat-measurements", "measure-sample"));
}

void CoreTests::planBuilderPlanRunsInExecutionSession()
{
    SequenceDef sequence;
    sequence.id = "seq-run";
    sequence.name = "Run Sequence";

    StepGroupDef setup;
    setup.id = "setup";
    setup.kind = StepGroupKind::Setup;
    setup.addStep({"open-fixture", "Open Fixture", StepKind::Action});

    StepGroupDef main;
    main.id = "main";
    main.kind = StepGroupKind::Main;
    main.addStep({"measure", "Measure", StepKind::Action});

    StepGroupDef cleanup;
    cleanup.id = "cleanup";
    cleanup.kind = StepGroupKind::Cleanup;
    cleanup.addStep({"power-off", "Power Off", StepKind::Cleanup});

    sequence.addGroup(setup);
    sequence.addGroup(main);
    sequence.addGroup(cleanup);

    PlanBuilder builder;
    const auto build = builder.build(sequence);
    QVERIFY(build.ok());

    ExecutionSession session(build.plan);
    session.addUut("uut-1");

    const auto run = session.run();
    QVERIFY(run.completed);
    QCOMPARE(run.state, ExecutionState::Completed);
    const auto& uut = session.uuts().first();
    QCOMPARE(uut.outcomeOf("measure"), NodeOutcome::Passed);
    const auto& sessionExecution = session.snapshot().sessionExecution;
    QCOMPARE(sessionExecution.outcomeOf("open-fixture"), NodeOutcome::Passed);
    QCOMPARE(sessionExecution.outcomeOf("power-off"), NodeOutcome::Passed);
}

void CoreTests::sequenceCompilerCompilesJsonToExecutablePlan()
{
    const auto json = R"json(
    {
      "id": "json-sequence",
      "name": "JSON Sequence",
      "version": "0.1.0",
      "groups": [
        {
          "id": "setup",
          "kind": "setup",
          "steps": [
            { "id": "open-fixture", "name": "Open Fixture", "kind": "action" }
          ]
        },
        {
          "id": "main",
          "kind": "main",
          "steps": [
            {
              "id": "batch-ready",
              "name": "Batch Ready",
              "kind": "barrier",
              "barrier": {
                "barrierName": "batch-ready",
                "cohortId": "batch-1",
                "arrivalPolicy": "DropFailed",
                "failurePolicy": "RemoveFailedMember",
                "timeoutPolicy": "ReleaseArrived"
              }
            },
            {
              "id": "measure-voltage",
              "name": "Measure Voltage",
              "kind": "action",
              "resources": [
                { "resourceId": "Instrument.DMM1", "mode": "exclusive", "priority": 5 }
              ],
              "retry": { "maxAttempts": 2 },
              "timeoutMs": 5000,
              "errorPolicy": {
                "onFail": "RunCleanup",
                "onError": "Abort",
                "onTimeout": "RunCleanup"
              }
            }
          ]
        },
        {
          "id": "cleanup",
          "kind": "cleanup",
          "steps": [
            { "id": "power-off", "name": "Power Off", "kind": "cleanup" }
          ]
        }
      ]
    }
    )json";

    const auto document = QJsonDocument::fromJson(json);
    QVERIFY(document.isObject());

    SequenceCompiler compiler;
    const auto result = compiler.compileJson(document.object());
    QVERIFY(result.ok());
    QCOMPARE(result.sequence.id, QString("json-sequence"));
    QCOMPARE(result.sequence.allSteps().size(), 4);
    QCOMPARE(result.plan.nodes.size(), 4);
    QCOMPARE(result.plan.cleanupRegions.size(), 1);

    const auto* barrier = result.plan.node("batch-ready");
    QVERIFY(barrier != nullptr);
    QCOMPARE(barrier->payload.value("arrivalPolicy").toString(), QString("DropFailed"));
    QCOMPARE(barrier->payload.value("failurePolicy").toString(), QString("RemoveFailedMember"));

    const auto* measure = result.plan.node("measure-voltage");
    QVERIFY(measure != nullptr);
    QCOMPARE(measure->resources.first().resourceId, QString("Instrument.DMM1"));
    QCOMPARE(measure->resources.first().priority, 5);
    QCOMPARE(measure->retry.maxAttempts, 2);
    QCOMPARE(measure->timeout.timeoutMs, 5000);
    QCOMPARE(measure->errorPolicy.onFail, ErrorAction::RunCleanup);
    QCOMPARE(measure->errorPolicy.onError, ErrorAction::Abort);

    ExecutionSession session(result.plan);
    session.addUut("uut-1");
    session.addUut("uut-2");
    const auto run = session.run();
    QVERIFY(run.completed);
    QCOMPARE(run.state, ExecutionState::Completed);
}

void CoreTests::sequenceCompilerBindsTypedVariablesPerUut()
{
    const auto document = QJsonDocument::fromJson(R"json(
    {
      "id": "per-uut-variables",
      "name": "Per UUT Variables",
      "variables": [
        {
          "name": "CAN_ID",
          "type": "hex",
          "scope": "perUut",
          "values": ["0x101", "0x102", null, "0x104"]
        },
        {
          "name": "MODBUS_SLAVE_ID",
          "type": "integer",
          "scope": "perUut",
          "values": [1, 2, 3, 4]
        },
        {
          "name": "TIMEOUT_MS",
          "type": "integer",
          "scope": "shared",
          "value": 1500
        }
      ],
      "groups": [{
        "id": "main",
        "kind": "main",
        "steps": [{
          "id": "echo",
          "name": "Echo CAN ID",
          "kind": "action",
          "moduleId": "test.echo",
          "function": "echo",
          "inputs": {
            "value": "${var.CAN_ID}",
            "unit": "id"
          }
        }]
      }]
    }
    )json");
    QVERIFY(document.isObject());

    SequenceCompiler compiler;
    const auto compiled = compiler.compileJson(document.object());
    QVERIFY2(compiled.ok(),
             compiled.errors.isEmpty() ? "compile failed"
                                       : qPrintable(compiled.errors.first().message));
    QCOMPARE(compiled.sequence.variables.size(), 3);
    QCOMPARE(compiled.plan.variables.size(), 3);

    const auto uut1 = bindSequenceVariablesForUut(
        compiled.plan.variables, 0, QStringLiteral("SN-A"));
    const auto uut2 = bindSequenceVariablesForUut(
        compiled.plan.variables, 1, QStringLiteral("SN-B"));
    QVERIFY(uut1.ok());
    QVERIFY(uut2.ok());
    QCOMPARE(uut1.variables.value("CAN_ID").toULongLong(), qulonglong(0x101));
    QCOMPARE(uut2.variables.value("CAN_ID").toULongLong(), qulonglong(0x102));
    QCOMPARE(uut2.variables.value("MODBUS_SLAVE_ID").toLongLong(), qlonglong(2));
    QCOMPARE(uut2.variables.value("TIMEOUT_MS").toLongLong(), qlonglong(1500));
    QCOMPARE(uut2.variables.value("uut").toMap().value("index").toInt(), 1);
    QCOMPARE(uut2.variables.value("uut").toMap().value("slot").toInt(), 2);
    QCOMPARE(uut2.variables.value("sn").toString(), QString("SN-B"));

    const auto missing = bindSequenceVariablesForUut(
        compiled.plan.variables, 2, QStringLiteral("SN-C"));
    QVERIFY(!missing.ok());
    QVERIFY(std::any_of(missing.errors.cbegin(), missing.errors.cend(),
                        [](const UutVariableBindingDiagnostic& diagnostic) {
                            return diagnostic.variableName == "CAN_ID";
                        }));

    QVariantMap overrides;
    overrides.insert(QStringLiteral("CAN_ID"), 0x555);
    const auto overridden = bindSequenceVariablesForUut(
        compiled.plan.variables, 0, QStringLiteral("SN-A"), overrides);
    QVERIFY(overridden.ok());
    QCOMPARE(overridden.variables.value("CAN_ID").toInt(), 0x555);

    ExecutionSession session(compiled.plan);
    auto& first = session.addUut(QStringLiteral("SN-A"));
    first.variables = uut1.variables;
    auto& second = session.addUut(QStringLiteral("SN-B"));
    second.variables = uut2.variables;
    QVERIFY(session.registerModule(std::make_shared<EchoModule>()));
    const auto run = session.run();
    QVERIFY(run.completed);
    const auto firstResult = session.results().latest("SN-A", "root", "echo");
    const auto secondResult = session.results().latest("SN-B", "root", "echo");
    QVERIFY(firstResult.has_value());
    QVERIFY(secondResult.has_value());
    QCOMPARE(firstResult->result.outputs.value("inputValue").toULongLong(),
             qulonglong(0x101));
    QCOMPARE(secondResult->result.outputs.value("inputValue").toULongLong(),
             qulonglong(0x102));
}

void CoreTests::sequenceCompilerRejectsInvalidSequenceVariables()
{
    const auto document = QJsonDocument::fromJson(R"json(
    {
      "id": "invalid-variables",
      "name": "Invalid Variables",
      "variables": [
        {"name":"9BAD", "type":"integer", "scope":"shared", "value":1},
        {"name":"DUP", "type":"hex", "scope":"perUut", "values":[true]},
        {"name":"DUP", "type":"bool", "scope":"shared", "value":"yes"}
      ],
      "groups":[{"id":"main","kind":"main","steps":[{"id":"noop","kind":"noop"}]}]
    }
    )json");
    QVERIFY(document.isObject());

    SequenceCompiler compiler;
    const auto compiled = compiler.compileJson(document.object());
    QVERIFY(!compiled.ok());
    const auto hasPath = [&compiled](const QString& path) {
        return std::any_of(compiled.errors.cbegin(), compiled.errors.cend(),
                           [&path](const CompileError& error) {
                               return error.path == path;
                           });
    };
    QVERIFY(hasPath(QStringLiteral("variables[0].name")));
    QVERIFY(hasPath(QStringLiteral("variables[1].values[0]")));
    QVERIFY(hasPath(QStringLiteral("variables[2].name")));
    QVERIFY(hasPath(QStringLiteral("variables[2].value")));
}

void CoreTests::sequenceCompilerReportsUnsupportedStepKind()
{
    const auto json = R"json(
    {
      "id": "bad-sequence",
      "name": "Bad Sequence",
      "groups": [
        {
          "id": "main",
          "kind": "main",
          "steps": [
            { "id": "bad-step", "kind": "teleport" }
          ]
        }
      ]
    }
    )json";

    const auto document = QJsonDocument::fromJson(json);
    QVERIFY(document.isObject());

    SequenceCompiler compiler;
    const auto result = compiler.compileJson(document.object());
    QVERIFY(!result.ok());
    QVERIFY(std::any_of(result.errors.cbegin(), result.errors.cend(), [](const CompileError& error) {
        return error.path.contains("kind") && error.message.contains("Unsupported step kind");
    }));
}

void CoreTests::sequenceCompilerReportsFieldTypeErrors()
{
    const auto json = R"json(
    {
      "id": 42,
      "name": "Bad Types",
      "groups": [
        {
          "id": "main",
          "kind": "main",
          "enabled": "yes",
          "steps": [
            {
              "id": "measure",
              "kind": "action",
              "enabled": "true",
              "parameters": [],
              "inputs": [],
              "checkpointBefore": "yes",
              "checkpointAfter": "no",
              "resources": [
                { "resourceId": "Instrument.DMM1", "mode": "teleport", "count": "one" }
              ],
              "retry": { "maxAttempts": "two" },
              "timeoutMs": "fast",
              "errorPolicy": { "onFail": "Explode" },
              "tags": [123]
            }
          ]
        }
      ]
    }
    )json";

    const auto document = QJsonDocument::fromJson(json);
    QVERIFY(document.isObject());

    SequenceCompiler compiler;
    const auto result = compiler.compileJson(document.object());
    QVERIFY(!result.ok());

    const auto hasError = [&](const QString& path, const QString& text) {
        return std::any_of(result.errors.cbegin(), result.errors.cend(), [&](const CompileError& error) {
            return error.path == path && error.message.contains(text);
        });
    };

    QVERIFY(hasError("id", "Expected string"));
    QVERIFY(hasError("groups[0].enabled", "Expected bool"));
    QVERIFY(hasError("groups[0].steps[0].enabled", "Expected bool"));
    QVERIFY(hasError("groups[0].steps[0].parameters", "Expected object"));
    QVERIFY(hasError("groups[0].steps[0].inputs", "Expected object"));
    QVERIFY(hasError("groups[0].steps[0].checkpointBefore", "Expected bool"));
    QVERIFY(hasError("groups[0].steps[0].checkpointAfter", "Expected bool"));
    QVERIFY(hasError("groups[0].steps[0].resources[0].mode", "Unsupported resource mode"));
    QVERIFY(hasError("groups[0].steps[0].resources[0].count", "Expected number"));
    QVERIFY(hasError("groups[0].steps[0].retry.maxAttempts", "Expected number"));
    QVERIFY(hasError("groups[0].steps[0].timeoutMs", "Expected number"));
    QVERIFY(hasError("groups[0].steps[0].errorPolicy.onFail", "Unsupported error action"));
    QVERIFY(hasError("groups[0].steps[0].tags[0]", "Expected string"));
}

void CoreTests::sequenceCompilerReportsLoopErrors()
{
    const auto json = R"json(
    {
      "id": "bad-loop",
      "name": "Bad Loop",
      "groups": [
        {
          "id": "main",
          "kind": "main",
          "steps": [
            {
              "id": "repeat",
              "kind": "loop",
              "loop": {
                "type": "teleport",
                "variable": "",
                "from": 0,
                "to": 2,
                "step": 0
              },
              "steps": "not-an-array"
            }
          ]
        }
      ]
    }
    )json";

    const auto document = QJsonDocument::fromJson(json);
    QVERIFY(document.isObject());

    SequenceCompiler compiler;
    const auto result = compiler.compileJson(document.object());
    QVERIFY(!result.ok());

    const auto hasError = [&](const QString& path, const QString& text) {
        return std::any_of(result.errors.cbegin(), result.errors.cend(), [&](const CompileError& error) {
            return error.path == path && error.message.contains(text);
        });
    };

    QVERIFY(hasError("groups[0].steps[0].loop.type", "Unsupported loop type"));
    QVERIFY(hasError("groups[0].steps[0].loop.variable", "Loop variable is required"));
    QVERIFY(hasError("groups[0].steps[0].loop.step", "Loop step must not be zero"));
    QVERIFY(hasError("groups[0].steps[0].steps", "Expected array"));
}

void CoreTests::sequenceCompilerReportsUnknownFieldWarnings()
{
    const auto json = R"json(
    {
      "id": "warn-sequence",
      "name": "Warn Sequence",
      "rootTypo": true,
      "x-root-extension": true,
      "vendor": { "station": "A1" },
      "groups": [
        {
          "id": "main",
          "kind": "main",
          "groupTypo": 1,
          "x-group-extension": true,
          "vendor": { "owner": "test" },
          "steps": [
            {
              "id": "measure",
              "kind": "action",
              "stepTypo": true,
              "x-step-extension": true,
              "vendor": { "driver": "mock" },
              "parameters": { "openPayloadField": true },
              "resources": [
                {
                  "resourceId": "Instrument.DMM1",
                  "resourceTypo": true
                }
              ],
              "retry": {
                "maxAttempts": 1,
                "retryTypo": true
              },
              "timeout": {
                "timeoutMs": 10,
                "timeoutTypo": true
              },
              "errorPolicy": {
                "onFail": "Continue",
                "errorTypo": true
              }
            },
            {
              "id": "sync",
              "kind": "barrier",
              "barrier": {
                "barrierName": "sync",
                "barrierTypo": true
              }
            }
          ]
        }
      ]
    }
    )json";

    const auto document = QJsonDocument::fromJson(json);
    QVERIFY(document.isObject());

    SequenceCompiler compiler;
    const auto result = compiler.compileJson(document.object());
    QVERIFY(result.ok());
    QVERIFY(result.errors.isEmpty());

    const auto hasWarning = [&](const QString& path) {
        return std::any_of(result.warnings.cbegin(), result.warnings.cend(), [&](const CompileWarning& warning) {
            return warning.path == path && warning.message.contains("Unknown field");
        });
    };

    QCOMPARE(result.warnings.size(), 8);
    QVERIFY(hasWarning("rootTypo"));
    QVERIFY(hasWarning("groups[0].groupTypo"));
    QVERIFY(hasWarning("groups[0].steps[0].stepTypo"));
    QVERIFY(hasWarning("groups[0].steps[0].resources[0].resourceTypo"));
    QVERIFY(hasWarning("groups[0].steps[0].retry.retryTypo"));
    QVERIFY(hasWarning("groups[0].steps[0].timeout.timeoutTypo"));
    QVERIFY(hasWarning("groups[0].steps[0].errorPolicy.errorTypo"));
    QVERIFY(hasWarning("groups[0].steps[1].barrier.barrierTypo"));
}

void CoreTests::sequenceCompilerParsesModuleBindings()
{
    const auto json = R"json(
    {
      "id": "external-binding",
      "name": "External Binding",
      "moduleBindings": [
        {
          "moduleId": "external.echo",
          "transport": "qprocess",
          "program": "${PICOATE_MOCK_HOST}",
          "arguments": ["--mode", "echo"],
          "timeoutMs": 1234
        }
      ],
      "groups": [
        {
          "id": "main",
          "kind": "main",
          "steps": [
            {
              "id": "external-echo",
              "kind": "action",
              "moduleId": "external.echo",
              "function": "echo"
            }
          ]
        }
      ]
    }
    )json";

    const auto document = QJsonDocument::fromJson(json);
    QVERIFY(document.isObject());

    SequenceCompiler compiler;
    const auto result = compiler.compileJson(document.object());
    QVERIFY(result.ok());
    QCOMPARE(result.sequence.moduleBindings.size(), 1);

    const auto& binding = result.sequence.moduleBindings.first();
    QCOMPARE(binding.moduleId, QString("external.echo"));
    QCOMPARE(binding.transport, QString("qprocess"));
    QCOMPARE(binding.program, QString("${PICOATE_MOCK_HOST}"));
    QCOMPARE(binding.arguments, QStringList({"--mode", "echo"}));
    QCOMPARE(binding.timeoutMs, 1234);
    QVERIFY(binding.enabled);
}

void CoreTests::sequenceCompilerReportsModuleBindingErrors()
{
    const auto json = R"json(
    {
      "id": "bad-binding",
      "name": "Bad Binding",
      "moduleBindings": [
        {
          "moduleId": 42,
          "transport": "teleport",
          "program": 7,
          "arguments": [123],
          "timeoutMs": "fast",
          "enabled": "yes"
        }
      ],
      "groups": [
        {
          "id": "main",
          "kind": "main",
          "steps": [
            { "id": "step", "kind": "action" }
          ]
        }
      ]
    }
    )json";

    const auto document = QJsonDocument::fromJson(json);
    QVERIFY(document.isObject());

    SequenceCompiler compiler;
    const auto result = compiler.compileJson(document.object());
    QVERIFY(!result.ok());

    const auto hasError = [&](const QString& path, const QString& text) {
        return std::any_of(result.errors.cbegin(), result.errors.cend(), [&](const CompileError& error) {
            return error.path == path && error.message.contains(text);
        });
    };

    QVERIFY(hasError("moduleBindings[0].moduleId", "Expected string"));
    QVERIFY(hasError("moduleBindings[0].transport", "Unsupported module transport"));
    QVERIFY(hasError("moduleBindings[0].program", "Expected string"));
    QVERIFY(hasError("moduleBindings[0].arguments[0]", "Expected string"));
    QVERIFY(hasError("moduleBindings[0].timeoutMs", "Expected number"));
    QVERIFY(hasError("moduleBindings[0].enabled", "Expected bool"));
}

void CoreTests::sequenceCompilerRunsSimpleExampleFile()
{
    QFile file(examplePath("simple_sequence.json"));
    QVERIFY2(file.open(QIODevice::ReadOnly), qPrintable(file.errorString()));

    QJsonParseError parseError;
    const auto document = QJsonDocument::fromJson(file.readAll(), &parseError);
    QCOMPARE(parseError.error, QJsonParseError::NoError);
    QVERIFY(document.isObject());

    SequenceCompiler compiler;
    const auto result = compiler.compileJson(document.object());
    QVERIFY(result.ok());
    QCOMPARE(result.sequence.id, QString("simple-sequence"));

    ExecutionSession session(result.plan);
    session.addUut("uut-1");

    const auto run = session.run();
    QVERIFY(run.completed);
    QCOMPARE(run.state, ExecutionState::Completed);
    const auto& uut = session.uuts().first();
    QCOMPARE(uut.outcomeOf("measure"), NodeOutcome::Passed);
    QCOMPARE(uut.outcomeOf("wait-100ms"), NodeOutcome::Passed);
    const auto& sessionExecution = session.snapshot().sessionExecution;
    QCOMPARE(sessionExecution.outcomeOf("open-fixture"), NodeOutcome::Passed);
    QCOMPARE(sessionExecution.outcomeOf("power-off"), NodeOutcome::Passed);
}

void CoreTests::sequenceCompilerRunsBasicExampleFile()
{
    QFile file(examplePath("basic_sequence.json"));
    QVERIFY2(file.open(QIODevice::ReadOnly), qPrintable(file.errorString()));

    QJsonParseError parseError;
    const auto document = QJsonDocument::fromJson(file.readAll(), &parseError);
    QCOMPARE(parseError.error, QJsonParseError::NoError);
    QVERIFY(document.isObject());

    SequenceCompiler compiler;
    const auto result = compiler.compileJson(document.object());
    QVERIFY(result.ok());
    QCOMPARE(result.sequence.id, QString("basic-sequence"));

    const auto* measureNode = result.plan.node("measure-voltage");
    QVERIFY(measureNode != nullptr);
    QCOMPARE(measureNode->payload.value("moduleId").toString(), QString("mock.measurement"));
    QCOMPARE(measureNode->payload.value("function").toString(), QString("measureVoltage"));
    QCOMPARE(measureNode->payload.value("inputs").toMap().value("outputs").toMap().value("actualVoltage").toDouble(), 4.999);

    ExecutionSession session(result.plan);
    session.addUut("uut-1");
    session.addUut("uut-2");

    const auto run = session.run();
    QVERIFY(run.completed);
    QCOMPARE(run.state, ExecutionState::Completed);

    for (const auto& uut : session.uuts()) {
        QCOMPARE(uut.outcomeOf("batch-ready"), NodeOutcome::Passed);
        QCOMPARE(uut.outcomeOf("measure-voltage"), NodeOutcome::Passed);
        const auto& measureActivation = uut.activations.value("measure-voltage");
        QVERIFY(!measureActivation.attempts.isEmpty());
        const auto outputs = measureActivation.attempts.last().result.outputs;
        QCOMPARE(outputs.value("actualVoltage").toDouble(), 4.999);
        QCOMPARE(outputs.value("measurements").toMap().value("unit").toString(), QString("V"));
    }
    const auto& sessionExecution = session.snapshot().sessionExecution;
    QCOMPARE(sessionExecution.outcomeOf("power-off"), NodeOutcome::Passed);
    QCOMPARE(sessionExecution.outcomeOf("close-fixture"), NodeOutcome::Passed);
}

void CoreTests::sequenceCompilerRunsDataParserExampleFile()
{
    QFile file(examplePath(QStringLiteral("data_parser_sequence.json")));
    QVERIFY2(file.open(QIODevice::ReadOnly), qPrintable(file.errorString()));

    QJsonParseError parseError;
    const auto document = QJsonDocument::fromJson(file.readAll(), &parseError);
    QCOMPARE(parseError.error, QJsonParseError::NoError);
    QVERIFY(document.isObject());

    SequenceCompiler compiler;
    const auto compiled = compiler.compileJson(document.object());
    QVERIFY2(compiled.ok(),
             qPrintable(compiled.errors.isEmpty()
                            ? QStringLiteral("Compilation failed without diagnostics")
                            : compiled.errors.first().message));
    const auto* parserNode = compiled.plan.node(QStringLiteral("decode-can-voltage"));
    QVERIFY(parserNode != nullptr);
    QCOMPARE(parserNode->payload.value(QStringLiteral("moduleId")).toString(),
             QStringLiteral("builtin.data-parser"));

    ExecutionSession session(compiled.plan);
    const auto uutId = QStringLiteral("UUT-1");
    const auto variableBinding = bindSequenceVariablesForUut(
        compiled.plan.variables, 0, uutId);
    QVERIFY(variableBinding.ok());
    auto& uut = session.addUut(uutId);
    uut.variables = variableBinding.variables;
    const auto run = session.run();
    QVERIFY(run.completed);
    QVERIFY(!run.hasError);
    QCOMPARE(run.state, ExecutionState::Completed);

    const auto canValue = session.results().latest(
        uutId, QStringLiteral("root"),
        QStringLiteral("decode-can-voltage"));
    QVERIFY(canValue.has_value());
    QVERIFY(qAbs(canValue->result.outputs.value(QStringLiteral("value")).toDouble()
                 - 12.5) < 0.0001);

    const auto serialNumber = session.results().latest(
        uutId, QStringLiteral("root"),
        QStringLiteral("extract-serial-number"));
    QVERIFY(serialNumber.has_value());
    QCOMPARE(serialNumber->result.outputs.value(QStringLiteral("value")).toString(),
             QStringLiteral("1234567890"));

    const auto registerText = session.results().latest(
        uutId, QStringLiteral("root"),
        QStringLiteral("decode-modbus-register-text"));
    QVERIFY(registerText.has_value());
    QCOMPARE(registerText->result.outputs.value(QStringLiteral("text")).toString(),
             QStringLiteral("UUT-1"));
    QCOMPARE(registerText->result.outputs.value(
                 QStringLiteral("parsedLength")).toInt(), 5);

    for (const auto& stepId : {
             QStringLiteral("check-can-voltage"),
             QStringLiteral("check-serial-number"),
             QStringLiteral("check-modbus-power"),
             QStringLiteral("check-modbus-register-text"),
             QStringLiteral("check-visa-value"),
             QStringLiteral("check-second-temperature")}) {
        const auto checked = session.results().latest(
            uutId, QStringLiteral("root"), stepId);
        QVERIFY2(checked.has_value(), qPrintable(stepId));
        QCOMPARE(checked->result.outcome, NodeOutcome::Passed);
    }
}

void CoreTests::sequenceCompilerCompilesModbusRegisterTextReadbackExampleFile()
{
    const auto sequencePath = QDir(projectRootPath()).filePath(
        QStringLiteral("templates/Modbus/Tcp/sinexcel_charger_protocol_sequence.json"));
    QFile file(sequencePath);
    QVERIFY2(file.open(QIODevice::ReadOnly), qPrintable(file.errorString()));

    QJsonParseError parseError;
    const auto document = QJsonDocument::fromJson(file.readAll(), &parseError);
    QCOMPARE(parseError.error, QJsonParseError::NoError);
    QVERIFY(document.isObject());

    SequenceCompiler compiler;
    const auto compiled = compiler.compileJson(document.object());
    QVERIFY2(compiled.ok(),
             qPrintable(compiled.errors.isEmpty()
                            ? QStringLiteral("Compilation failed without diagnostics")
                            : compiled.errors.first().message));

    const auto* write = compiled.plan.node(QStringLiteral("write-asset-code"));
    const auto* read = compiled.plan.node(QStringLiteral("read-asset-code"));
    const auto* decode = compiled.plan.node(QStringLiteral("decode-asset-code"));
    const auto* check = compiled.plan.node(QStringLiteral("check-asset-code"));
    QVERIFY(write && read && decode && check);

    const auto writeInputs = write->payload.value(QStringLiteral("inputs")).toMap();
    QCOMPARE(write->payload.value(QStringLiteral("function")).toString(),
             QStringLiteral("writeMultipleRegisters"));
    QCOMPARE(writeInputs.value(QStringLiteral("text")).toString(),
             QStringLiteral("${var.serialNumber}"));
    QCOMPARE(writeInputs.value(QStringLiteral("registerCount")).toInt(), 24);

    const auto readInputs = read->payload.value(QStringLiteral("inputs")).toMap();
    QCOMPARE(read->payload.value(QStringLiteral("function")).toString(),
             QStringLiteral("readHoldingRegisters"));
    QCOMPARE(readInputs.value(QStringLiteral("count")).toInt(), 24);

    const auto decodeInputs = decode->payload.value(QStringLiteral("inputs")).toMap();
    QCOMPARE(decode->payload.value(QStringLiteral("moduleId")).toString(),
             QStringLiteral("builtin.data-parser"));
    QCOMPARE(decode->payload.value(QStringLiteral("function")).toString(),
             QStringLiteral("decodeRegisters"));
    QCOMPARE(decodeInputs.value(QStringLiteral("source")).toString(),
             QStringLiteral("${step:read-asset-code.outputs.registers}"));
    QCOMPARE(decodeInputs.value(QStringLiteral("dataType")).toString(),
             QStringLiteral("utf8Text"));

    const auto checkInputs = check->payload.value(QStringLiteral("inputs")).toMap();
    QCOMPARE(checkInputs.value(QStringLiteral("actual")).toString(),
             QStringLiteral("${step:decode-asset-code.outputs.text}"));
    QCOMPARE(check->payload.value(QStringLiteral("expected")).toString(),
             QStringLiteral("${var.serialNumber}"));
}

void CoreTests::sequenceCompilerRunsCustomDisabledExampleFile()
{
    QFile file(examplePath("custom_disabled_sequence.json"));
    QVERIFY2(file.open(QIODevice::ReadOnly), qPrintable(file.errorString()));

    QJsonParseError parseError;
    const auto document = QJsonDocument::fromJson(file.readAll(), &parseError);
    QCOMPARE(parseError.error, QJsonParseError::NoError);
    QVERIFY(document.isObject());

    SequenceCompiler compiler;
    const auto result = compiler.compileJson(document.object());
    QVERIFY(result.ok());
    QCOMPARE(result.sequence.id, QString("custom-disabled-sequence"));

    const auto measureDef = result.sequence.stepById("measure");
    QVERIFY(measureDef.has_value());
    QVERIFY(measureDef->checkpointBefore);
    QVERIFY(measureDef->checkpointAfter);

    QVERIFY(result.plan.node("warmup-wait") == nullptr);
    QVERIFY(result.plan.node("disabled-diagnostic") == nullptr);
    const auto* measureNode = result.plan.node("measure");
    QVERIFY(measureNode != nullptr);
    QVERIFY(measureNode->checkpointBefore);
    QVERIFY(measureNode->checkpointAfter);
    QVERIFY(result.plan.node("operator-confirm") != nullptr);

    ExecutionSession session(result.plan);
    session.addUut("uut-1");

    const auto run = session.run();
    QVERIFY(run.completed);
    QCOMPARE(run.state, ExecutionState::Completed);

    const auto report = session.report();
    QCOMPARE(report.uuts.size(), 1);
    const auto& uut = report.uuts.first();
    QCOMPARE(uut.steps.size(), 2);
    QCOMPARE(uut.steps[0].stepId, QString("measure"));
    QCOMPARE(uut.steps[1].stepId, QString("operator-confirm"));
    QCOMPARE(report.sessionSteps.size(), 2);
    QCOMPARE(report.sessionSteps[0].stepId, QString("open-fixture"));
    QCOMPARE(report.sessionSteps[1].stepId, QString("power-off"));
}

void CoreTests::sequenceCompilerRunsExternalEchoExampleFile()
{
    const auto sequencePath = examplePath("external_echo_sequence.json");
    QFile file(sequencePath);
    QVERIFY2(file.open(QIODevice::ReadOnly), qPrintable(file.errorString()));

    QJsonParseError parseError;
    const auto document = QJsonDocument::fromJson(file.readAll(), &parseError);
    QCOMPARE(parseError.error, QJsonParseError::NoError);
    QVERIFY(document.isObject());

    SequenceCompiler compiler;
    const auto result = compiler.compileJson(document.object());
    QVERIFY(result.ok());
    QCOMPARE(result.sequence.id, QString("external-echo-sequence"));
    QCOMPARE(result.sequence.moduleBindings.size(), 1);

    ExecutionSession session(result.plan);
    session.addUut("uut-1");

    const auto registration = registerConfiguredModules(session, result.sequence, testBindingOptions(sequencePath));
    QVERIFY(registration.ok());
    QCOMPARE(registration.registeredModuleIds, QVector<ModuleId>{"external.echo"});

    const auto run = session.run();
    QVERIFY(run.completed);
    QVERIFY(!run.hasError);
    QCOMPARE(run.state, ExecutionState::Completed);
    QCOMPARE(run.nodeResults.size(), 1);
    QCOMPARE(run.nodeResults.first().outcome, NodeOutcome::Passed);
    QCOMPARE(run.nodeResults.first().outputs.value("value").toString(), QString("from-configured-host"));
    QCOMPARE(run.nodeResults.first().outputs.value("numeric").toInt(), 42);
}

void CoreTests::sequenceCompilerRunsPythonEchoExampleFile()
{
    const auto python = pythonExePath();
    if (python.isEmpty() || !QFileInfo::exists(python)) {
        QSKIP("Python interpreter was not found by CMake.");
    }

    const auto sequencePath = examplePath("python_echo_sequence.json");
    QFile file(sequencePath);
    QVERIFY2(file.open(QIODevice::ReadOnly), qPrintable(file.errorString()));

    QJsonParseError parseError;
    const auto document = QJsonDocument::fromJson(file.readAll(), &parseError);
    QCOMPARE(parseError.error, QJsonParseError::NoError);
    QVERIFY(document.isObject());

    SequenceCompiler compiler;
    const auto result = compiler.compileJson(document.object());
    QVERIFY(result.ok());
    QCOMPARE(result.sequence.id, QString("python-echo-sequence"));
    QCOMPARE(result.sequence.moduleBindings.size(), 1);

    ExecutionSession session(result.plan);
    session.addUut("uut-1");

    const auto registration = registerConfiguredModules(session, result.sequence, testBindingOptions(sequencePath));
    QVERIFY(registration.ok());
    QCOMPARE(registration.registeredModuleIds, QVector<ModuleId>{"python.echo"});

    const auto run = session.run();
    QVERIFY(run.completed);
    const auto runError = run.nodeResults.isEmpty()
        ? QStringLiteral("Python echo produced no node result")
        : QStringLiteral("%1: %2")
              .arg(run.nodeResults.first().errorCode,
                   run.nodeResults.first().errorMessage);
    QVERIFY2(!run.hasError, qPrintable(runError));
    QCOMPARE(run.state, ExecutionState::Completed);
    QCOMPARE(run.nodeResults.size(), 1);
    QCOMPARE(run.nodeResults.first().outcome, NodeOutcome::Passed);
    QCOMPARE(run.nodeResults.first().outputs.value("value").toString(), QString("from-python-host"));
    QCOMPARE(run.nodeResults.first().outputs.value("numeric").toInt(), 42);

    QCOMPARE(run.nodeResults.first().measurements.size(), 1);
    QCOMPARE(run.nodeResults.first().measurements.first().name, QString("PY_ECHO"));
    QCOMPARE(run.nodeResults.first().measurements.first().value.toInt(), 42);
    QCOMPARE(run.nodeResults.first().measurements.first().unit, QString("count"));

    const auto measurements = run.nodeResults.first().outputs.value("measurements").toMap();
    QCOMPARE(measurements.value("name").toString(), QString("PY_ECHO"));
    QCOMPARE(measurements.value("value").toInt(), 42);
    QCOMPARE(measurements.value("unit").toString(), QString("count"));
}

void CoreTests::sequenceCompilerRunsNativeHostDllExampleFile()
{
    const auto host = nativeHostPath();
    const auto dllPath = testDllPath();
    QVERIFY2(QFileInfo::exists(host), qPrintable(host));
    QVERIFY2(QFileInfo::exists(dllPath), qPrintable(dllPath));

    const auto sequencePath = examplePath("nativehost_dll_sequence.json");
    QFile file(sequencePath);
    QVERIFY2(file.open(QIODevice::ReadOnly), qPrintable(file.errorString()));

    QJsonParseError parseError;
    const auto document = QJsonDocument::fromJson(file.readAll(), &parseError);
    QCOMPARE(parseError.error, QJsonParseError::NoError);
    QVERIFY(document.isObject());

    SequenceCompiler compiler;
    const auto result = compiler.compileJson(document.object());
    QVERIFY(result.ok());
    QCOMPARE(result.sequence.id, QString("nativehost-dll-sequence"));
    QCOMPARE(result.sequence.moduleBindings.size(), 1);

    ExecutionSession session(result.plan);
    session.addUut("uut-1");

    const auto registration = registerConfiguredModules(session, result.sequence, testBindingOptions(sequencePath));
    QVERIFY(registration.ok());
    QCOMPARE(registration.registeredModuleIds, QVector<ModuleId>{"native.dll.echo"});

    const auto run = session.run();
    QVERIFY(run.completed);
    QVERIFY(!run.hasError);
    QCOMPARE(run.state, ExecutionState::Completed);
    QCOMPARE(run.nodeResults.size(), 1);
    QCOMPARE(run.nodeResults.first().outcome, NodeOutcome::Passed);
    QCOMPARE(run.nodeResults.first().outputs.value("value").toString(), QString("from-nativehost-dll"));
    QCOMPARE(run.nodeResults.first().outputs.value("numeric").toInt(), 42);

    QCOMPARE(run.nodeResults.first().measurements.size(), 1);
    QCOMPARE(run.nodeResults.first().measurements.first().name, QString("NATIVE_DLL_ECHO"));
    QCOMPARE(run.nodeResults.first().measurements.first().value.toInt(), 42);
    QCOMPARE(run.nodeResults.first().measurements.first().unit, QString("count"));

    const auto measurements = run.nodeResults.first().outputs.value("measurements").toMap();
    QCOMPARE(measurements.value("name").toString(), QString("NATIVE_DLL_ECHO"));
    QCOMPARE(measurements.value("value").toInt(), 42);
    QCOMPARE(measurements.value("unit").toString(), QString("count"));
}

void CoreTests::sequenceCompilerRunsSimulatedCanDllExampleFile()
{
    const auto host = nativeHostPath();
    const auto dllPath = canDllPath();
    QVERIFY2(QFileInfo::exists(host), qPrintable(host));
    QVERIFY2(QFileInfo::exists(dllPath), qPrintable(dllPath));

    const auto sequencePath = examplePath("can_dll_sequence.json");
    QFile file(sequencePath);
    QVERIFY2(file.open(QIODevice::ReadOnly), qPrintable(file.errorString()));

    QJsonParseError parseError;
    const auto document = QJsonDocument::fromJson(file.readAll(), &parseError);
    QCOMPARE(parseError.error, QJsonParseError::NoError);
    QVERIFY(document.isObject());

    SequenceCompiler compiler;
    const auto result = compiler.compileJson(document.object());
    QVERIFY(result.ok());
    QCOMPARE(result.sequence.id, QString("simulated-can-dll-sequence"));
    QCOMPARE(result.sequence.moduleBindings.size(), 1);

    ExecutionSession session(result.plan);
    session.addUut("uut-1");

    const auto registration = registerConfiguredModules(session, result.sequence, testBindingOptions(sequencePath));
    QVERIFY(registration.ok());
    QCOMPARE(registration.registeredModuleIds, QVector<ModuleId>{"project.can.decode"});

    const auto run = session.run();
    QVERIFY(run.completed);
    QVERIFY(!run.hasError);
    QCOMPARE(run.state, ExecutionState::Completed);
    QCOMPARE(run.nodeResults.size(), 1);
    QCOMPARE(run.nodeResults.first().outcome, NodeOutcome::Passed);
    QCOMPARE(run.nodeResults.first().outputs.value("signalName").toString(), QString("PackVoltage"));
    QCOMPARE(run.nodeResults.first().outputs.value("physicalValue").toDouble(), 100.0);

    QCOMPARE(run.nodeResults.first().measurements.size(), 1);
    QCOMPARE(run.nodeResults.first().measurements.first().name, QString("PackVoltage"));
    QCOMPARE(run.nodeResults.first().measurements.first().value.toDouble(), 100.0);
    QCOMPARE(run.nodeResults.first().measurements.first().unit, QString("V"));
    QCOMPARE(run.nodeResults.first().measurements.first().lowerLimit, 95.0);
    QCOMPARE(run.nodeResults.first().measurements.first().upperLimit, 105.0);

    const auto measurements = run.nodeResults.first().outputs.value("measurements").toMap();
    QCOMPARE(measurements.value("name").toString(), QString("PackVoltage"));
    QCOMPARE(measurements.value("value").toDouble(), 100.0);
    QCOMPARE(measurements.value("unit").toString(), QString("V"));

    const auto report = session.report();
    QCOMPARE(report.uuts.size(), 1);
    const auto* step = findStep(report.uuts.first(), "decode-pack-voltage");
    QVERIFY(step != nullptr);
    QCOMPARE(step->measurements.size(), 1);
    QCOMPARE(step->measurements.first().name, QString("PackVoltage"));
    QCOMPARE(step->measurements.first().value.toDouble(), 100.0);
    QCOMPARE(step->measurements.first().unit, QString("V"));
    QCOMPARE(step->attempts.size(), 1);
    QCOMPARE(step->attempts.first().measurements.size(), 1);
}

void CoreTests::sequenceCompilerRunsPersistentInstrumentExampleFile()
{
    const auto host = fakeInstrumentHostPath();
    QVERIFY2(QFileInfo::exists(host), qPrintable(host));

    const auto sequencePath = examplePath("persistent_instrument_sequence.json");
    QFile file(sequencePath);
    QVERIFY2(file.open(QIODevice::ReadOnly), qPrintable(file.errorString()));

    QJsonParseError parseError;
    const auto document = QJsonDocument::fromJson(file.readAll(), &parseError);
    QCOMPARE(parseError.error, QJsonParseError::NoError);
    QVERIFY(document.isObject());

    SequenceCompiler compiler;
    const auto result = compiler.compileJson(document.object());
    QVERIFY(result.ok());
    QCOMPARE(result.sequence.id, QString("persistent-instrument-sequence"));
    QCOMPARE(result.sequence.moduleBindings.size(), 1);
    QCOMPARE(result.sequence.moduleBindings.first().transport, QString("persistent-qprocess"));

    ExecutionSession session(result.plan);
    session.addUut("uut-1");

    const auto registration = registerConfiguredModules(session, result.sequence, testBindingOptions(sequencePath));
    QVERIFY(registration.ok());
    QCOMPARE(registration.registeredModuleIds, QVector<ModuleId>{"fake.instrument"});

    const auto run = session.run();
    QVERIFY(run.completed);
    QVERIFY(!run.hasError);
    QCOMPARE(run.state, ExecutionState::Completed);

    const auto& uut = session.uuts().first();
    const auto snapshot = session.snapshot();
    const auto nodeResult = [](const UutExecution& execution,
                               const NodeId& nodeId) -> const NodeResult* {
        const auto it = execution.activations.constFind(nodeId);
        if (it == execution.activations.constEnd() || it.value().attempts.isEmpty()) {
            return nullptr;
        }
        return &it.value().attempts.last().result;
    };

    const auto* open = nodeResult(uut, "open-dmm");
    const auto* read1 = nodeResult(uut, "read-dmm-1");
    const auto* read2 = nodeResult(uut, "read-dmm-2");
    const auto* statusNode = nodeResult(uut, "status-dmm");
    const auto* close = nodeResult(snapshot.sessionExecution, "close-dmm");
    QVERIFY(open != nullptr);
    QVERIFY(read1 != nullptr);
    QVERIFY(read2 != nullptr);
    QVERIFY(statusNode != nullptr);
    QVERIFY(close != nullptr);

    QCOMPARE(open->outputs.value("openCount").toInt(), 1);
    QCOMPARE(read1->outputs.value("readCount").toInt(), 1);
    QCOMPARE(read2->outputs.value("readCount").toInt(), 2);
    QCOMPARE(statusNode->outputs.value("readCount").toInt(), 2);
    QVERIFY(statusNode->outputs.value("connected").toBool());
    QVERIFY(!close->outputs.value("connected").toBool());
    QCOMPARE(close->outputs.value("readCount").toInt(), 2);
    QCOMPARE(read2->measurements.size(), 1);
    QCOMPARE(read2->measurements.first().name, QString("DMM_READ_2"));
}

void CoreTests::sequenceCompilerRunsDmmCanAdapterExampleFile()
{
    const auto host = fakeInstrumentHostPath();
    QVERIFY2(QFileInfo::exists(host), qPrintable(host));

    const auto sequencePath = examplePath("dmm_can_adapter_sequence.json");
    QFile file(sequencePath);
    QVERIFY2(file.open(QIODevice::ReadOnly), qPrintable(file.errorString()));

    QJsonParseError parseError;
    const auto document = QJsonDocument::fromJson(file.readAll(), &parseError);
    QCOMPARE(parseError.error, QJsonParseError::NoError);
    QVERIFY(document.isObject());

    SequenceCompiler compiler;
    const auto result = compiler.compileJson(document.object());
    QVERIFY(result.ok());
    QCOMPARE(result.sequence.id, QString("dmm-can-adapter-sequence"));
    QCOMPARE(result.sequence.moduleBindings.size(), 2);
    QCOMPARE(result.sequence.moduleBindings[0].moduleId, QString("fake.dmm"));
    QCOMPARE(result.sequence.moduleBindings[1].moduleId, QString("fake.can"));

    ExecutionSession session(result.plan);
    session.addUut("uut-1");

    auto transport = std::make_shared<PersistentQProcessTransport>(host);
    registerFakeInstrumentDeviceFactories(session.devices(), transport);

    VariableResolverOptions stationOptions;
    stationOptions.sequenceFilePath = examplePath("stations/basic_station.json");
    stationOptions.projectDir = projectRootPath();
    stationOptions.variables.insert("DMM1_RESOURCE", "USB0::FAKE::INSTR");
    const auto station = loadStationConfigFile(examplePath("stations/basic_station.json"), stationOptions);
    QVERIFY(station.ok());

    const auto configureErrors = configureDeviceSessions(station.config, session.devices());
    QVERIFY(configureErrors.isEmpty());

    const auto run = session.run();
    QVERIFY(run.completed);
    QVERIFY(!run.hasError);
    QCOMPARE(run.state, ExecutionState::Completed);

    const auto& uut = session.uuts().first();
    const auto snapshot = session.snapshot();
    const auto nodeResult = [](const UutExecution& execution,
                               const NodeId& nodeId) -> const NodeResult* {
        const auto it = execution.activations.constFind(nodeId);
        if (it == execution.activations.constEnd() || it.value().attempts.isEmpty()) {
            return nullptr;
        }
        return &it.value().attempts.last().result;
    };

    const auto* configureDmm = nodeResult(uut, "configure-dmm-dcv");
    const auto* readDmm = nodeResult(uut, "read-dmm");
    const auto* readCan = nodeResult(uut, "read-can-frame");
    const auto* disconnectDmm = nodeResult(snapshot.sessionExecution, "disconnect-dmm");
    const auto* disconnectCan = nodeResult(snapshot.sessionExecution, "disconnect-can");
    QVERIFY(configureDmm != nullptr);
    QVERIFY(readDmm != nullptr);
    QVERIFY(readCan != nullptr);
    QVERIFY(disconnectDmm != nullptr);
    QVERIFY(disconnectCan != nullptr);

    QCOMPARE(configureDmm->outputs.value("lastMode").toString(), QString("DCV"));
    QCOMPARE(configureDmm->outputs.value("configureCount").toInt(), 1);
    QCOMPARE(readDmm->outputs.value("readCount").toInt(), 1);
    QCOMPARE(readDmm->measurements.size(), 1);
    QCOMPARE(readDmm->measurements.first().name, QString("DMM_DCV"));
    QCOMPARE(readDmm->measurements.first().unit, QString("V"));
    QCOMPARE(readDmm->measurements.first().status, MeasurementStatus::Passed);

    QCOMPARE(readCan->outputs.value("frameId").toString(), QString("0x123"));
    QCOMPARE(readCan->outputs.value("data").toString(), QString("01 02 03 04 05 06 07 08"));
    QCOMPARE(readCan->measurements.size(), 1);
    QCOMPARE(readCan->measurements.first().name, QString("CAN_FRAME_READ"));

    QCOMPARE(disconnectDmm->outcome, NodeOutcome::Passed);
    QCOMPARE(disconnectCan->outcome, NodeOutcome::Passed);
    QCOMPARE(session.devices().stateOf("DMM1"), DeviceConnectionState::Disconnected);
    QCOMPARE(session.devices().stateOf("CAN1"), DeviceConnectionState::Disconnected);

    const auto report = session.report();
    QCOMPARE(report.uuts.size(), 1);
    QVERIFY(!report.uuts.first().hasError);
    QVERIFY(findStep(report.uuts.first(), "read-dmm") != nullptr);
    QVERIFY(findStep(report.uuts.first(), "read-can-frame") != nullptr);
    transport->shutdown();
}

void CoreTests::sequenceCompilerRunsForLoopExampleFile()
{
    QFile file(examplePath("for_loop_sequence.json"));
    QVERIFY2(file.open(QIODevice::ReadOnly), qPrintable(file.errorString()));

    QJsonParseError parseError;
    const auto document = QJsonDocument::fromJson(file.readAll(), &parseError);
    QCOMPARE(parseError.error, QJsonParseError::NoError);
    QVERIFY(document.isObject());

    SequenceCompiler compiler;
    const auto result = compiler.compileJson(document.object());
    QVERIFY(result.ok());
    QCOMPARE(result.sequence.id, QString("for-loop-sequence"));
    QCOMPARE(result.plan.loopRegions.size(), 1);

    ExecutionSession session(result.plan);
    session.addUut("uut-1");

    const auto run = session.run();
    QVERIFY(run.completed);
    QVERIFY(!run.hasError);
    QCOMPARE(run.state, ExecutionState::Completed);

    const auto& uut = session.uuts().first();
    QCOMPARE(uut.outcomeOf("repeat-measurements"), NodeOutcome::Passed);
    QCOMPARE(uut.outcomeOf("repeat-measurements.measure-sample"), NodeOutcome::Passed);
    QCOMPARE(uut.outcomeOf("after-loop"), NodeOutcome::Passed);
    QCOMPARE(uut.variables.value("sampleIndex").toInt(), 2);
    QCOMPARE(uut.variables.value("loop.index").toInt(), 2);
    QCOMPARE(uut.variables.value("loop.value").toInt(), 2);
    QCOMPARE(uut.activations.value("repeat-measurements.measure-sample").attempts.size(), 3);

    const auto& measureAttempts = uut.activations.value("repeat-measurements.measure-sample").attempts;
    for (int i = 0; i < measureAttempts.size(); ++i) {
        const auto& loopIteration = measureAttempts[i].loopIteration;
        QVERIFY(loopIteration.active);
        QCOMPARE(loopIteration.loopId, QString("repeat-measurements"));
        QCOMPARE(loopIteration.controllerNodeId, QString("repeat-measurements"));
        QCOMPARE(loopIteration.variableName, QString("sampleIndex"));
        QCOMPARE(loopIteration.iterationIndex, i);
        QCOMPARE(loopIteration.iterationNumber, i + 1);
        QCOMPARE(loopIteration.value, i);
    }

    const auto lastLoopOutputs = uut.activations.value("repeat-measurements.measure-sample").attempts.last().result.outputs;
    QCOMPARE(lastLoopOutputs.value("sampleIndex").toInt(), 2);
    QCOMPARE(lastLoopOutputs.value("sampleLabel").toString(), QString("sample-2"));
    QCOMPARE(lastLoopOutputs.value("uutId").toString(), QString("uut-1"));
    QCOMPARE(lastLoopOutputs.value("attemptNumber").toInt(), 3);

    const auto lastLoopMeasurements = lastLoopOutputs.value("measurements").toMap();
    QCOMPARE(lastLoopMeasurements.value("name").toString(), QString("LOOP_SAMPLE_2"));
    QCOMPARE(lastLoopMeasurements.value("value").toInt(), 2);
    QCOMPARE(lastLoopMeasurements.value("loopIndex").toInt(), 2);

    const auto report = session.report();
    QCOMPARE(report.uuts.size(), 1);
    const auto& steps = report.uuts.first().steps;
    QCOMPARE(steps.size(), 2);
    QCOMPARE(steps[0].stepId, QString("repeat-measurements"));
    QCOMPARE(steps[0].children.size(), 1);
    QCOMPARE(steps[0].children[0].stepId, QString("measure-sample"));
    QCOMPARE(steps[1].stepId, QString("after-loop"));
    QCOMPARE(report.sessionSteps.size(), 2);
    QCOMPARE(report.sessionSteps[0].stepId, QString("open-fixture"));
    QCOMPARE(report.sessionSteps[1].stepId, QString("power-off"));

    const auto* measure = findStep(report.uuts.first(), "measure-sample");
    QVERIFY(measure != nullptr);
    QVERIFY(measure->loop.inLoop);
    QCOMPARE(measure->loop.loopId, QString("repeat-measurements"));
    QCOMPARE(measure->loop.controllerStepId, QString("repeat-measurements"));
    QCOMPARE(measure->loop.variableName, QString("sampleIndex"));
    QCOMPARE(measure->loop.from, 0);
    QCOMPARE(measure->loop.to, 2);
    QCOMPARE(measure->loop.step, 1);
    QCOMPARE(measure->attempts.size(), 3);
    for (int i = 0; i < measure->attempts.size(); ++i) {
        const auto& loopIteration = measure->attempts[i].loopIteration;
        QVERIFY(loopIteration.active);
        QCOMPARE(loopIteration.iterationIndex, i);
        QCOMPARE(loopIteration.iterationNumber, i + 1);
        QCOMPARE(loopIteration.value, i);
    }
    QCOMPARE(measure->measurements.size(), 1);
    QCOMPARE(measure->measurements.first().name, QString("LOOP_SAMPLE_2"));
    QCOMPARE(measure->measurements.first().value.toInt(), 2);
    QCOMPARE(measure->attempts.last().measurements.size(), 1);
    QCOMPARE(measure->attempts.last().measurements.first().name, QString("LOOP_SAMPLE_2"));
    QVERIFY(!measure->wasError);
}

void CoreTests::whileLoopBreakCounterAndAggregateWorkTogether()
{
    const auto document = QJsonDocument::fromJson(R"json({
      "id":"while-counter","name":"While counter","groups":[
        {"id":"setup","kind":"setup","steps":[{"id":"prepare","kind":"noop"}]},
        {"id":"main","kind":"main","steps":[{
          "id":"poll","kind":"loop",
          "loop":{"type":"while","intervalMs":0,"maxIterations":10,"timeoutMs":1000},
          "steps":[
            {"id":"sample","kind":"action",
             "parameters":{"outputs":{"ready":true,"voltage":"${loop.number}"}}},
            {"id":"count","kind":"counter",
             "inputs":{"condition":"${step:sample.outputs.ready}"},
             "parameters":{"mode":"consecutive","start":0,"increment":1}},
            {"id":"stats","kind":"aggregate",
             "inputs":{"value":"${step:sample.outputs.voltage}"}},
            {"id":"done","kind":"break",
             "inputs":{"actual":"${step:count.outputs.value}"},
             "parameters":{"comparison":"greaterOrEqual","expected":3}}
          ]
        },{"id":"after","kind":"noop"}]},
        {"id":"cleanup","kind":"cleanup","steps":[{"id":"close","kind":"cleanup"}]}
      ]
    })json");
    QVERIFY(document.isObject());

    SequenceCompiler compiler;
    const auto compiled = compiler.compileJson(document.object());
    QVERIFY2(compiled.ok(), qPrintable(compiled.errors.isEmpty()
        ? QString() : compiled.errors.first().message));
    QCOMPARE(compiled.plan.loopRegions.size(), 1);
    QCOMPARE(compiled.plan.loopRegions.first().type, LoopType::While);

    class BreakEventSink final : public IRuntimeEventSink {
    public:
        void publish(const RuntimeEvent& event) override
        {
            if (event.kind == RuntimeEventKind::AttemptCompleted &&
                event.nodeLocalId == QStringLiteral("done")) {
                breakRequests.push_back(
                    event.details.value(QStringLiteral("breakRequested")).toBool());
            }
        }

        QVector<bool> breakRequests;
    } events;

    ExecutionSession session(compiled.plan, {}, &events);
    session.addUut("uut-1");
    const auto run = session.run();
    QVERIFY(run.completed);
    QVERIFY(!run.hasError);
    QCOMPARE(run.state, ExecutionState::Completed);

    const auto& uut = session.uuts().first();
    QCOMPARE(uut.outcomeOf("poll"), NodeOutcome::Passed);
    QCOMPARE(uut.outcomeOf("after"), NodeOutcome::Passed);
    QCOMPARE(uut.activations.value("poll.count").attempts.size(), 3);
    QCOMPARE(uut.activations.value("poll.stats").attempts.size(), 3);
    QCOMPARE(uut.activations.value("poll.done").attempts.size(), 3);
    QCOMPARE(uut.activations.value("poll.count").attempts.last()
                 .result.outputs.value("value").toDouble(), 3.0);
    const auto stats = uut.activations.value("poll.stats").attempts.last().result.outputs;
    QCOMPARE(stats.value("count").toInt(), 3);
    QCOMPARE(stats.value("minimum").toDouble(), 1.0);
    QCOMPARE(stats.value("maximum").toDouble(), 3.0);
    QCOMPARE(stats.value("average").toDouble(), 2.0);
    const auto outputs = uut.activations.value("poll").attempts.last().result.outputs;
    QCOMPARE(outputs.value("iterations").toInt(), 3);
    QCOMPARE(outputs.value("exitReason").toString(), QString("break"));
    QCOMPARE(outputs.value("breakNodeId").toString(), QString("poll.done"));
    QCOMPARE(events.breakRequests, QVector<bool>({false, false, true}));
}

void CoreTests::whileLoopRunsInsideTestItem()
{
    const auto document = QJsonDocument::fromJson(R"json({
      "id":"while-test-item","name":"While in TestItem","groups":[
        {"id":"setup","kind":"setup","steps":[{"id":"prepare","kind":"noop"}]},
        {"id":"main","kind":"main","steps":[{
          "id":"voltage-item","kind":"testItem","steps":[{
            "id":"poll","kind":"loop",
            "loop":{"type":"while","maxIterations":10,"timeoutMs":1000},
            "steps":[{"id":"done","kind":"break",
                      "inputs":{"actual":"${loop.number}"},
                      "parameters":{"comparison":"greaterOrEqual","expected":2}}]
          }]
        }]},
        {"id":"cleanup","kind":"cleanup","steps":[{"id":"close","kind":"cleanup"}]}
      ]
    })json");
    QVERIFY(document.isObject());

    SequenceCompiler compiler;
    const auto compiled = compiler.compileJson(document.object());
    QVERIFY(compiled.ok());
    ExecutionSession session(compiled.plan);
    session.addUut("uut-1");
    const auto run = session.run();
    QVERIFY(run.completed);
    QVERIFY(!run.hasError);

    const auto& uut = session.uuts().first();
    QCOMPARE(uut.outcomeOf("voltage-item"), NodeOutcome::Passed);
    QCOMPARE(uut.outcomeOf("voltage-item.poll"), NodeOutcome::Passed);
    QCOMPARE(uut.activations.value("voltage-item.poll.done").attempts.size(), 2);
}

void CoreTests::whileLoopResultFeedsLaterSiblingInsideTestItem()
{
    const auto document = QJsonDocument::fromJson(R"json({
      "id":"while-result-test-item","name":"While result in TestItem","groups":[
        {"id":"main","kind":"main","steps":[{
          "id":"voltage-item","kind":"testItem","steps":[
            {
              "id":"poll","kind":"loop",
              "loop":{"type":"while","maxIterations":10,"timeoutMs":1000},
              "steps":[
                {"id":"sample","kind":"action",
                 "parameters":{"outputs":{"voltage":"${loop.number}"}}},
                {"id":"done","kind":"break",
                 "inputs":{"actual":"${loop.number}"},
                 "parameters":{"comparison":"greaterOrEqual","expected":2}}
              ]
            },
            {"id":"parse","kind":"action",
             "inputs":{"voltage":"${step:poll.sample.outputs.voltage}"},
             "parameters":{"echoInputs":true}}
          ]
        }]}
      ]
    })json");
    QVERIFY(document.isObject());

    SequenceCompiler compiler;
    const auto compiled = compiler.compileJson(document.object());
    QVERIFY2(compiled.ok(), qPrintable(compiled.errors.isEmpty()
        ? QString() : compiled.errors.first().message));

    ExecutionSession session(compiled.plan);
    session.addUut("uut-1");
    const auto run = session.run();
    QVERIFY(run.completed);
    QVERIFY(!run.hasError);
    const auto parsed = session.results().latest("uut-1", "root", "voltage-item.parse");
    QVERIFY(parsed.has_value());
    QCOMPARE(parsed->result.outputs.value("voltage").toInt(), 2);
}

void CoreTests::compilerRejectsForwardLoopResultInsideTestItem()
{
    const auto document = QJsonDocument::fromJson(R"json({
      "id":"forward-while-result","name":"Forward While Result","groups":[
        {"id":"main","kind":"main","steps":[{
          "id":"item","kind":"testItem","steps":[
            {"id":"consumer","kind":"action",
             "inputs":{"value":"${step:poll.sample.outputs.value}"}},
            {"id":"poll","kind":"loop",
             "loop":{"type":"while","maxIterations":1,"timeoutMs":1000},
             "steps":[
               {"id":"sample","kind":"action","parameters":{"outputs":{"value":1}}},
               {"id":"done","kind":"break","inputs":{"actual":true},
                "parameters":{"comparison":"isTrue"}}
             ]}
          ]
        }]}
      ]
    })json");
    QVERIFY(document.isObject());

    SequenceCompiler compiler;
    const auto compiled = compiler.compileJson(document.object());
    QVERIFY(!compiled.ok());
    QVERIFY(std::any_of(compiled.errors.cbegin(), compiled.errors.cend(),
                        [](const CompileError& error) {
                            return error.message.contains("not guaranteed",
                                                          Qt::CaseInsensitive);
                        }));
}

void CoreTests::whileLoopBreakSkipsRemainingBody()
{
    const auto document = QJsonDocument::fromJson(R"json({
      "id":"while-skip","name":"While skips tail","groups":[
        {"id":"setup","kind":"setup","steps":[{"id":"prepare","kind":"noop"}]},
        {"id":"main","kind":"main","steps":[{
          "id":"poll","kind":"loop",
          "loop":{"type":"while","maxIterations":10,"timeoutMs":1000},
          "steps":[
            {"id":"done","kind":"break","inputs":{"actual":true},
             "parameters":{"comparison":"isTrue"}},
            {"id":"must-not-run","kind":"action","parameters":{"outcome":"error"}}
          ]
        }]},
        {"id":"cleanup","kind":"cleanup","steps":[{"id":"close","kind":"cleanup"}]}
      ]
    })json");
    QVERIFY(document.isObject());

    SequenceCompiler compiler;
    const auto compiled = compiler.compileJson(document.object());
    QVERIFY(compiled.ok());
    ExecutionSession session(compiled.plan);
    session.addUut("uut-1");
    const auto run = session.run();
    QVERIFY(run.completed);
    QVERIFY(!run.hasError);

    const auto& uut = session.uuts().first();
    QCOMPARE(uut.outcomeOf("poll"), NodeOutcome::Passed);
    QCOMPARE(uut.outcomeOf("poll.done"), NodeOutcome::Passed);
    QCOMPARE(uut.outcomeOf("poll.must-not-run"), NodeOutcome::Skipped);
    QCOMPARE(uut.activations.value("poll.must-not-run").attempts.size(), 1);
}

void CoreTests::whileLoopStopsAtFiniteGuard()
{
    const auto document = QJsonDocument::fromJson(R"json({
      "id":"while-guard","name":"While guard","groups":[
        {"id":"setup","kind":"setup","steps":[{"id":"prepare","kind":"noop"}]},
        {"id":"main","kind":"main","steps":[{
          "id":"never-ready","kind":"loop",
          "loop":{"type":"while","maxIterations":3,"timeoutMs":1000},
          "errorPolicy":{"onFail":"Continue","stopUutOnFailure":false},
          "steps":[{"id":"done","kind":"break","inputs":{"actual":false},
                    "parameters":{"comparison":"isTrue"}}]
        },{"id":"after","kind":"noop"}]},
        {"id":"cleanup","kind":"cleanup","steps":[{"id":"close","kind":"cleanup"}]}
      ]
    })json");
    QVERIFY(document.isObject());

    SequenceCompiler compiler;
    const auto compiled = compiler.compileJson(document.object());
    QVERIFY(compiled.ok());
    ExecutionSession session(compiled.plan);
    session.addUut("uut-1");
    const auto run = session.run();
    QVERIFY(run.completed);
    QVERIFY(run.hasError);
    QCOMPARE(run.state, ExecutionState::CompletedWithError);

    const auto& uut = session.uuts().first();
    QCOMPARE(uut.outcomeOf("never-ready"), NodeOutcome::Failed);
    QCOMPARE(uut.outcomeOf("after"), NodeOutcome::Passed);
    QCOMPARE(uut.activations.value("never-ready.done").attempts.size(), 3);
    const auto parentResult = uut.activations.value("never-ready").attempts.last().result;
    QCOMPARE(parentResult.errorCode, QString("WhileLoopMaxIterations"));
    QCOMPARE(parentResult.outputs.value("exitReason").toString(),
             QString("maximum-iterations"));
}

void CoreTests::forLoopSupportsBreakIf()
{
    const auto document = QJsonDocument::fromJson(R"json({
      "id":"for-break","name":"For break","groups":[
        {"id":"setup","kind":"setup","steps":[{"id":"prepare","kind":"noop"}]},
        {"id":"main","kind":"main","steps":[{
          "id":"scan","kind":"loop",
          "loop":{"type":"for","variable":"i","from":0,"to":9,"step":1},
          "steps":[{"id":"done","kind":"break",
                    "inputs":{"actual":"${loop.value}"},
                    "parameters":{"comparison":"greaterOrEqual","expected":2}}]
        }]},
        {"id":"cleanup","kind":"cleanup","steps":[{"id":"close","kind":"cleanup"}]}
      ]
    })json");
    QVERIFY(document.isObject());

    SequenceCompiler compiler;
    const auto compiled = compiler.compileJson(document.object());
    QVERIFY2(compiled.ok(), qPrintable(compiled.errors.isEmpty()
        ? QString() : compiled.errors.first().message));
    ExecutionSession session(compiled.plan);
    session.addUut("uut-1");
    const auto run = session.run();
    QVERIFY(run.completed);
    QVERIFY(!run.hasError);

    const auto& uut = session.uuts().first();
    QCOMPARE(uut.outcomeOf("scan"), NodeOutcome::Passed);
    QCOMPARE(uut.activations.value("scan.done").attempts.size(), 3);
    const auto outputs = uut.activations.value("scan").attempts.last().result.outputs;
    QCOMPARE(outputs.value("iterations").toInt(), 3);
    QCOMPARE(outputs.value("exitReason").toString(), QString("break"));
}

void CoreTests::compilerRejectsInvalidWhileLoopAndBreakPlacement()
{
    const auto document = QJsonDocument::fromJson(R"json({
      "id":"bad-while","name":"Bad while","groups":[
        {"id":"main","kind":"main","steps":[{
          "id":"poll","kind":"loop",
          "loop":{"type":"while","intervalMs":-1,
                  "maxIterations":0,"timeoutMs":0},
          "steps":[{"id":"sync","kind":"barrier"}]
        },{"id":"outside-break","kind":"break","inputs":{"actual":true},
            "parameters":{"comparison":"isTrue"}}]}
      ]
    })json");
    QVERIFY(document.isObject());

    SequenceCompiler compiler;
    const auto compiled = compiler.compileJson(document.object());
    QVERIFY(!compiled.ok());
    const auto hasMessage = [&](const QString& text) {
        return std::any_of(compiled.errors.cbegin(), compiled.errors.cend(),
                           [&](const CompileError& error) {
                               return error.message.contains(text, Qt::CaseInsensitive);
                           });
    };
    QVERIFY(hasMessage("intervalMs"));
    QVERIFY(hasMessage("finite guard"));
    QVERIFY(hasMessage("requires a Break If"));
    QVERIFY(hasMessage("Barrier inside a While Loop"));
    QVERIFY(hasMessage("Break If must be inside a Loop"));
}

void CoreTests::executionSessionJsonFailureRunsCleanup()
{
    const auto json = R"json(
    {
      "id": "json-fail-cleanup",
      "name": "JSON Fail Cleanup",
      "groups": [
        {
          "id": "main",
          "kind": "main",
          "steps": [
            {
              "id": "measure",
              "kind": "action",
              "parameters": { "outcome": "Failed" },
              "errorPolicy": {
                "onFail": "RunCleanup"
              }
            },
            {
              "id": "after-fail",
              "kind": "action"
            }
          ]
        },
        {
          "id": "cleanup",
          "kind": "cleanup",
          "steps": [
            { "id": "power-off", "kind": "cleanup" }
          ]
        }
      ]
    }
    )json";

    const auto document = QJsonDocument::fromJson(json);
    QVERIFY(document.isObject());

    SequenceCompiler compiler;
    const auto result = compiler.compileJson(document.object());
    QVERIFY(result.ok());

    ExecutionSession session(result.plan);
    session.addUut("uut-1");

    const auto run = session.run();
    QVERIFY(run.completed);
    QCOMPARE(run.state, ExecutionState::CompletedWithError);

    const auto& uut = session.uuts().first();
    QCOMPARE(uut.outcomeOf("measure"), NodeOutcome::Failed);
    QCOMPARE(uut.outcomeOf("after-fail"), NodeOutcome::Skipped);
    QCOMPARE(session.snapshot().sessionExecution.outcomeOf("power-off"), NodeOutcome::Passed);
}

void CoreTests::executionSessionJsonRetryAttemptsAreRecorded()
{
    const auto json = R"json(
    {
      "id": "json-retry",
      "name": "JSON Retry",
      "groups": [
        {
          "id": "main",
          "kind": "main",
          "steps": [
            {
              "id": "measure",
              "kind": "action",
              "parameters": { "failUntilAttempt": 0 },
              "retry": { "maxAttempts": 2 }
            }
          ]
        },
        {
          "id": "cleanup",
          "kind": "cleanup",
          "steps": [
            { "id": "power-off", "kind": "cleanup" }
          ]
        }
      ]
    }
    )json";

    const auto document = QJsonDocument::fromJson(json);
    QVERIFY(document.isObject());

    SequenceCompiler compiler;
    const auto result = compiler.compileJson(document.object());
    QVERIFY(result.ok());

    ExecutionSession session(result.plan);
    session.addUut("uut-1");

    const auto run = session.run();
    QVERIFY(run.completed);
    QCOMPARE(run.state, ExecutionState::Completed);

    const auto& uut = session.uuts().first();
    QCOMPARE(uut.outcomeOf("measure"), NodeOutcome::Passed);
    QCOMPARE(uut.activations.value("measure").attempts.size(), 2);
    QCOMPARE(uut.activations.value("measure").attempts[0].result.outcome, NodeOutcome::Failed);
    QCOMPARE(uut.activations.value("measure").attempts[1].result.outcome, NodeOutcome::Passed);
    QCOMPARE(session.snapshot().sessionExecution.outcomeOf("power-off"), NodeOutcome::Passed);
}

void CoreTests::executionSessionReportCapturesRetryAttempts()
{
    const auto json = R"json(
    {
      "id": "json-retry-report",
      "name": "JSON Retry Report",
      "groups": [
        {
          "id": "main",
          "kind": "main",
          "steps": [
            {
              "id": "measure",
              "name": "Measure",
              "kind": "action",
              "parameters": { "failUntilAttempt": 0 },
              "retry": { "maxAttempts": 2 }
            }
          ]
        },
        {
          "id": "cleanup",
          "kind": "cleanup",
          "steps": [
            { "id": "power-off", "name": "Power Off", "kind": "cleanup" }
          ]
        }
      ]
    }
    )json";

    SequenceCompiler compiler;
    const auto compile = compiler.compileJson(QJsonDocument::fromJson(json).object());
    QVERIFY(compile.ok());

    ExecutionSession session(compile.plan);
    session.addUut("uut-1");

    const auto result = session.run();
    QVERIFY(result.completed);
    QVERIFY(!result.hasError);

    const auto report = session.report();
    QCOMPARE(report.planId, compile.plan.id);
    QCOMPARE(report.sequenceId, QString("json-retry-report"));
    QCOMPARE(report.state, ExecutionState::Completed);
    QVERIFY(report.completed);
    QVERIFY(!report.hasError);
    QCOMPARE(report.uuts.size(), 1);

    const auto& uut = report.uuts.first();
    QVERIFY(!uut.hasError);
    QCOMPARE(uut.steps.size(), 1);
    QCOMPARE(uut.steps[0].stepId, QString("measure"));
    QCOMPARE(report.sessionSteps.size(), 1);

    const auto* measure = findStep(uut, "measure");
    QVERIFY(measure != nullptr);
    QCOMPARE(measure->displayName, QString("Measure"));
    QCOMPARE(measure->kind, ExecNodeKind::Action);
    QCOMPARE(measure->state, ActivationState::Passed);
    QCOMPARE(measure->outcome, NodeOutcome::Passed);
    QVERIFY(!measure->wasError);
    QVERIFY(measure->durationMs >= 0);
    QCOMPARE(measure->attempts.size(), 2);
    QCOMPARE(measure->attempts[0].index, 1);
    QCOMPARE(measure->attempts[0].outcome, NodeOutcome::Failed);
    QVERIFY(measure->attempts[0].durationMs >= 0);
    QCOMPARE(measure->attempts[1].index, 2);
    QCOMPARE(measure->attempts[1].outcome, NodeOutcome::Passed);
    QVERIFY(measure->attempts[1].durationMs >= 0);

    const auto* powerOff = findStep(report.sessionSteps, "power-off");
    QVERIFY(powerOff != nullptr);
    QCOMPARE(powerOff->kind, ExecNodeKind::Cleanup);
    QCOMPARE(powerOff->outcome, NodeOutcome::Passed);
    QVERIFY(!powerOff->wasError);
}

void CoreTests::executionSessionReportFlagsErrorsWithoutTreatingSkippedAsError()
{
    const auto json = R"json(
    {
      "id": "json-fail-report",
      "name": "JSON Fail Report",
      "groups": [
        {
          "id": "main",
          "kind": "main",
          "steps": [
            {
              "id": "measure",
              "name": "Measure",
              "kind": "action",
              "parameters": { "outcome": "Failed" },
              "errorPolicy": { "onFail": "RunCleanup" }
            },
            { "id": "after-fail", "name": "After Fail", "kind": "action" }
          ]
        },
        {
          "id": "cleanup",
          "kind": "cleanup",
          "steps": [
            { "id": "power-off", "name": "Power Off", "kind": "cleanup" }
          ]
        }
      ]
    }
    )json";

    SequenceCompiler compiler;
    const auto compile = compiler.compileJson(QJsonDocument::fromJson(json).object());
    QVERIFY(compile.ok());

    ExecutionSession session(compile.plan);
    session.addUut("uut-1");

    const auto result = session.run();
    QVERIFY(result.completed);
    QVERIFY(result.hasError);

    const auto report = session.report();
    QCOMPARE(report.state, ExecutionState::CompletedWithError);
    QVERIFY(report.completed);
    QVERIFY(report.hasError);
    QCOMPARE(report.uuts.size(), 1);

    const auto& uut = report.uuts.first();
    QVERIFY(uut.hasError);

    const auto* measure = findStep(uut, "measure");
    QVERIFY(measure != nullptr);
    QCOMPARE(measure->state, ActivationState::Failed);
    QCOMPARE(measure->outcome, NodeOutcome::Failed);
    QVERIFY(measure->wasError);
    QCOMPARE(measure->attempts.size(), 1);

    const auto* skipped = findStep(uut, "after-fail");
    QVERIFY(skipped != nullptr);
    QCOMPARE(skipped->state, ActivationState::Skipped);
    QCOMPARE(skipped->outcome, NodeOutcome::Skipped);
    QVERIFY(!skipped->wasError);

    const auto* cleanup = findStep(report.sessionSteps, "power-off");
    QVERIFY(cleanup != nullptr);
    QCOMPARE(cleanup->state, ActivationState::Passed);
    QCOMPARE(cleanup->outcome, NodeOutcome::Passed);
    QVERIFY(!cleanup->wasError);
}

void CoreTests::sequenceCompilerRunsTestItemExampleFile()
{
    QFile file(examplePath("test_item_sequence.json"));
    QVERIFY2(file.open(QIODevice::ReadOnly), qPrintable(file.errorString()));
    const auto document = QJsonDocument::fromJson(file.readAll());
    QVERIFY(document.isObject());

    SequenceCompiler compiler;
    const auto compile = compiler.compileJson(document.object());
    QVERIFY2(compile.ok(),
             qPrintable(compile.errors.isEmpty() ? QString() : compile.errors.first().message));
    QCOMPARE(compile.plan.testItemRegions.size(), 1);
    QCOMPARE(compile.plan.testItemRegions.first().controllerNodeId,
             QString("power-rail-check"));
    QCOMPARE(compile.plan.testItemRegions.first().childNodeIds.size(), 2);

    ExecutionSession session(compile.plan);
    session.addUut("uut-1");
    const auto result = session.run();
    QVERIFY(result.completed);
    QVERIFY(!result.hasError);

    const auto report = session.report();
    QCOMPARE(report.uuts.size(), 1);
    const auto& uut = report.uuts.first();
    const auto* parent = findStep(uut, "power-rail-check");
    QVERIFY(parent != nullptr);
    QCOMPARE(parent->kind, ExecNodeKind::TestItem);
    QCOMPARE(parent->outcome, NodeOutcome::Passed);
    QCOMPARE(parent->children.size(), 2);
    QCOMPARE(parent->children[0].outcome, NodeOutcome::Passed);
    QCOMPARE(parent->children[1].outcome, NodeOutcome::Passed);
    QVERIFY(findStep(uut, "after-power-check") != nullptr);
}

void CoreTests::testItemStopsRemainingChildrenAfterFailure()
{
    const auto json = R"json(
    {
      "id": "test-item-failure",
      "name": "Test Item Failure",
      "groups": [
        {
          "id": "main",
          "kind": "main",
          "steps": [
            {
              "id": "parent-check",
              "name": "Parent Check",
              "kind": "testItem",
              "steps": [
                {
                  "id": "child-fail",
                  "kind": "action",
                  "parameters": { "outcome": "Failed" }
                },
                {
                  "id": "child-pass",
                  "kind": "action"
                }
              ]
            },
            { "id": "after-parent", "kind": "action" }
          ]
        },
        {
          "id": "cleanup",
          "kind": "cleanup",
          "steps": [{ "id": "cleanup-step", "kind": "cleanup" }]
        }
      ]
    })json";

    SequenceCompiler compiler;
    const auto compile = compiler.compileJson(QJsonDocument::fromJson(json).object());
    QVERIFY(compile.ok());
    ExecutionSession session(compile.plan);
    session.addUut("uut-1");
    const auto result = session.run();
    QVERIFY(result.completed);
    QVERIFY(result.hasError);

    const auto report = session.report();
    const auto& uut = report.uuts.first();
    const auto* parent = findStep(uut, "parent-check");
    QVERIFY(parent != nullptr);
    QCOMPARE(parent->outcome, NodeOutcome::Failed);
    QCOMPARE(parent->state, ActivationState::Failed);
    QCOMPARE(parent->children.size(), 2);
    QCOMPARE(parent->children[0].outcome, NodeOutcome::Failed);
    QCOMPARE(parent->children[1].outcome, NodeOutcome::Skipped);
    QCOMPARE(findStep(uut, "after-parent")->outcome, NodeOutcome::Skipped);
    const auto* cleanup = findStep(report.sessionSteps, "cleanup-step");
    QVERIFY(cleanup != nullptr);
    QCOMPARE(cleanup->outcome, NodeOutcome::Passed);
}

void CoreTests::testItemChildContinueRunsRemainingChildren()
{
    const auto json = R"json({
      "id": "test-item-child-continue",
      "name": "Test Item Child Continue",
      "groups": [{
        "id": "main",
        "kind": "main",
        "steps": [{
          "id": "parent",
          "kind": "testItem",
          "steps": [
            {
              "id": "allowed-failure",
              "kind": "action",
              "parameters": { "outcome": "Failed" },
              "errorPolicy": { "onFail": "Continue" }
            },
            { "id": "still-runs", "kind": "action" }
          ]
        }]
      }]
    })json";

    SequenceCompiler compiler;
    const auto compile = compiler.compileJson(QJsonDocument::fromJson(json).object());
    QVERIFY(compile.ok());
    ExecutionSession session(compile.plan);
    session.addUut("uut-1");
    const auto run = session.run();
    QVERIFY(run.completed);
    QVERIFY(run.hasError);
    QCOMPARE(session.uuts().first().outcomeOf("parent.allowed-failure"), NodeOutcome::Failed);
    QCOMPARE(session.uuts().first().outcomeOf("parent.still-runs"), NodeOutcome::Passed);
    QCOMPARE(session.uuts().first().outcomeOf("parent"), NodeOutcome::Failed);
}

void CoreTests::stationFailureHandlingControlsTestItemChildren()
{
    const auto json = R"json({
      "id": "station-test-item-policy",
      "name": "Station TestItem Policy",
      "groups": [{
        "id": "main",
        "kind": "main",
        "steps": [
          {
            "id": "parent",
            "kind": "testItem",
            "errorPolicy": {
              "onFail": "StopUut",
              "onError": "StopUut",
              "onTimeout": "StopUut"
            },
            "steps": [
              {
                "id": "failed",
                "kind": "action",
                "parameters": { "outcome": "Failed" },
                "errorPolicy": { "onFail": "Continue" }
              },
              { "id": "after-fail", "kind": "noop" },
              { "id": "error", "kind": "action", "parameters": { "outcome": "Error" } },
              { "id": "after-error", "kind": "noop" },
              { "id": "timeout", "kind": "action", "parameters": { "outcome": "Timeout" } },
              { "id": "after-timeout", "kind": "noop" }
            ]
          },
          { "id": "after-parent", "kind": "noop" }
        ]
      }]
    })json";

    SequenceCompiler compiler;
    const auto compile = compiler.compileJson(QJsonDocument::fromJson(json).object());
    QVERIFY(compile.ok());

    ExecutionSession continueSession(compile.plan,
                                     {},
                                     nullptr,
                                     {},
                                     FailureHandlingMode::Continue);
    continueSession.addUut("continue-uut");
    const auto continueRun = continueSession.run();
    QVERIFY(continueRun.completed);
    QVERIFY(continueRun.hasError);
    const auto& continued = continueSession.uuts().first();
    QCOMPARE(continued.outcomeOf("parent.failed"), NodeOutcome::Failed);
    QCOMPARE(continued.outcomeOf("parent.after-fail"), NodeOutcome::Passed);
    QCOMPARE(continued.outcomeOf("parent.error"), NodeOutcome::Error);
    QCOMPARE(continued.outcomeOf("parent.after-error"), NodeOutcome::Passed);
    QCOMPARE(continued.outcomeOf("parent.timeout"), NodeOutcome::Timeout);
    QCOMPARE(continued.outcomeOf("parent.after-timeout"), NodeOutcome::Passed);
    QCOMPARE(continued.outcomeOf("parent"), NodeOutcome::Error);
    QCOMPARE(continued.outcomeOf("after-parent"), NodeOutcome::Passed);

    ExecutionSession stopSession(compile.plan,
                                 {},
                                 nullptr,
                                 {},
                                 FailureHandlingMode::Stop);
    stopSession.addUut("stop-uut");
    const auto stopRun = stopSession.run();
    QVERIFY(stopRun.completed);
    QVERIFY(stopRun.hasError);
    const auto& stopped = stopSession.uuts().first();
    QCOMPARE(stopped.outcomeOf("parent.failed"), NodeOutcome::Failed);
    QCOMPARE(stopped.outcomeOf("parent.after-fail"), NodeOutcome::Skipped);
    QCOMPARE(stopped.outcomeOf("parent.error"), NodeOutcome::Skipped);
    QCOMPARE(stopped.outcomeOf("parent.after-timeout"), NodeOutcome::Skipped);
    QCOMPARE(stopped.outcomeOf("parent"), NodeOutcome::Failed);
    QCOMPARE(stopped.outcomeOf("after-parent"), NodeOutcome::Skipped);
}

void CoreTests::stationContinueEvaluatesFailedDataDependency()
{
    const auto json = R"json({
      "id": "station-dependent-continue",
      "name": "Station Dependent Continue",
      "groups": [{
        "id": "main",
        "kind": "main",
        "steps": [{
          "id": "parent",
          "kind": "testItem",
          "steps": [
            {
              "id": "source",
              "kind": "action",
              "parameters": {
                "outcome": "Failed",
                "outputs": { "value": 42 }
              }
            },
            {
              "id": "dependent",
              "kind": "action",
              "inputs": { "copy": "${step:source.outputs.value}" }
            },
            { "id": "unrelated", "kind": "noop" }
          ]
        }]
      }]
    })json";

    SequenceCompiler compiler;
    const auto compile = compiler.compileJson(QJsonDocument::fromJson(json).object());
    QVERIFY2(compile.ok(), qPrintable(compile.errors.isEmpty()
                                          ? QString()
                                          : compile.errors.first().message));
    ExecutionSession session(compile.plan,
                             {},
                             nullptr,
                             {},
                             FailureHandlingMode::Continue);
    session.addUut("uut-1");
    const auto run = session.run();
    QVERIFY(run.completed);
    QVERIFY(run.hasError);

    const auto& uut = session.uuts().first();
    QCOMPARE(uut.outcomeOf("parent.source"), NodeOutcome::Failed);
    QCOMPARE(uut.outcomeOf("parent.dependent"), NodeOutcome::Error);
    QCOMPARE(uut.outcomeOf("parent.unrelated"), NodeOutcome::Passed);
    QCOMPARE(uut.outcomeOf("parent"), NodeOutcome::Error);
    const auto dependent = uut.activations.constFind("parent.dependent");
    QVERIFY(dependent != uut.activations.constEnd());
    QVERIFY(!dependent->attempts.isEmpty());
    QCOMPARE(dependent->attempts.last().result.errorCode,
             QString("RuntimeVariableResolutionError"));
}

void CoreTests::testItemRetriesWholeSubtreeAndEventuallyPasses()
{
    const auto json = R"json({
      "id":"test-item-retry-pass","name":"TestItem Retry Pass","groups":[{
        "id":"main","kind":"main","steps":[
          {"id":"parent","kind":"testItem","retry":{"maxAttempts":2},"steps":[
            {"id":"first","kind":"action","parameters":{"failUntilAttempt":0}},
            {"id":"second","kind":"action"}
          ]},
          {"id":"after","kind":"action"}
        ]
      }]
    })json";

    SequenceCompiler compiler;
    const auto compile = compiler.compileJson(QJsonDocument::fromJson(json).object());
    QVERIFY2(compile.ok(), qPrintable(compile.errors.isEmpty()
                                          ? QString()
                                          : compile.errors.first().message));
    CollectingRuntimeEventSink events;
    ExecutionSession session(compile.plan, {}, &events);
    session.addUut("uut-1");
    const auto run = session.run();
    QVERIFY(run.completed);
    QVERIFY(!run.hasError);

    const auto& uut = session.uuts().first();
    QCOMPARE(uut.outcomeOf("parent"), NodeOutcome::Passed);
    QCOMPARE(uut.outcomeOf("parent.first"), NodeOutcome::Passed);
    QCOMPARE(uut.outcomeOf("parent.second"), NodeOutcome::Passed);
    QCOMPARE(uut.outcomeOf("after"), NodeOutcome::Passed);
    QCOMPARE(uut.activations.value("parent").attempts.size(), 2);
    QCOMPARE(uut.activations.value("parent").attempts[0].result.outcome,
             NodeOutcome::Failed);
    QCOMPARE(uut.activations.value("parent").attempts[1].result.outcome,
             NodeOutcome::Passed);
    QCOMPARE(uut.activations.value("parent.first").attempts.size(), 2);
    QCOMPARE(uut.activations.value("parent.second").attempts.size(), 2);
    QCOMPARE(uut.activations.value("parent.second").attempts[0].result.outcome,
             NodeOutcome::Skipped);
    QCOMPARE(uut.activations.value("parent.second").attempts[1].result.outcome,
             NodeOutcome::Passed);
    const auto runtimeEvents = events.records();
    QVERIFY(std::any_of(runtimeEvents.cbegin(), runtimeEvents.cend(), [](const auto& event) {
        return event.kind == RuntimeEventKind::RetryScheduled &&
               event.nodeId == QStringLiteral("parent");
    }));
    QVERIFY(!session.report().hasError);
}

void CoreTests::testItemRetryResetsChildRetryBudget()
{
    const auto json = R"json({
      "id":"test-item-child-budget","name":"TestItem Child Budget","groups":[{
        "id":"main","kind":"main","steps":[{
          "id":"parent","kind":"testItem","retry":{"maxAttempts":2},"steps":[{
            "id":"sample","kind":"action",
            "parameters":{"failUntilAttempt":2},
            "retry":{"maxAttempts":2}
          }]
        }]
      }]
    })json";

    SequenceCompiler compiler;
    const auto compile = compiler.compileJson(QJsonDocument::fromJson(json).object());
    QVERIFY(compile.ok());
    ExecutionSession session(compile.plan);
    session.addUut("uut-1");
    const auto run = session.run();
    QVERIFY(run.completed);
    QVERIFY(!run.hasError);
    const auto& uut = session.uuts().first();
    QCOMPARE(uut.outcomeOf("parent"), NodeOutcome::Passed);
    QCOMPARE(uut.activations.value("parent").attempts.size(), 2);
    QCOMPARE(uut.activations.value("parent.sample").attempts.size(), 4);
    QCOMPARE(uut.activations.value("parent.sample").attempts.last().result.outcome,
             NodeOutcome::Passed);
}

void CoreTests::testItemRetryResetsNestedLoopState()
{
    const auto json = R"json({
      "id":"test-item-loop-retry","name":"TestItem Loop Retry","groups":[{
        "id":"main","kind":"main","steps":[{
          "id":"parent","kind":"testItem","retry":{"maxAttempts":2},"steps":[{
            "id":"loop","kind":"loop",
            "loop":{"variable":"i","from":0,"to":0,"step":1},
            "steps":[{
              "id":"sample","kind":"action","parameters":{"failUntilAttempt":0}
            }]
          }]
        }]
      }]
    })json";

    SequenceCompiler compiler;
    const auto compile = compiler.compileJson(QJsonDocument::fromJson(json).object());
    QVERIFY(compile.ok());
    ExecutionSession session(compile.plan);
    session.addUut("uut-1");
    const auto run = session.run();
    QVERIFY(run.completed);
    QVERIFY(!run.hasError);
    const auto& uut = session.uuts().first();
    QCOMPARE(uut.outcomeOf("parent"), NodeOutcome::Passed);
    QCOMPARE(uut.outcomeOf("parent.loop"), NodeOutcome::Passed);
    QCOMPARE(uut.activations.value("parent.loop.sample").attempts.size(), 2);
}

void CoreTests::testItemRetryExhaustionKeepsFinalFailure()
{
    const auto json = R"json({
      "id":"test-item-retry-fail","name":"TestItem Retry Fail","groups":[
        {"id":"main","kind":"main","steps":[
          {"id":"parent","kind":"testItem","retry":{"maxAttempts":2},"steps":[
            {"id":"always-fails","kind":"action","parameters":{"outcome":"Failed"}}
          ]},
          {"id":"after","kind":"action"}
        ]},
        {"id":"cleanup","kind":"cleanup","steps":[
          {"id":"cleanup","kind":"cleanup"}
        ]}
      ]
    })json";

    SequenceCompiler compiler;
    const auto compile = compiler.compileJson(QJsonDocument::fromJson(json).object());
    QVERIFY(compile.ok());
    ExecutionSession session(compile.plan);
    session.addUut("uut-1");
    const auto run = session.run();
    QVERIFY(run.completed);
    QVERIFY(run.hasError);
    const auto& uut = session.uuts().first();
    QCOMPARE(uut.outcomeOf("parent"), NodeOutcome::Failed);
    QCOMPARE(uut.activations.value("parent").attempts.size(), 2);
    QCOMPARE(uut.activations.value("parent.always-fails").attempts.size(), 2);
    QCOMPARE(uut.outcomeOf("after"), NodeOutcome::Skipped);
    QCOMPARE(session.snapshot().sessionExecution.outcomeOf("cleanup"), NodeOutcome::Passed);
}

void CoreTests::compilerRejectsBarrierInsideRetryingTestItem()
{
    const auto json = R"json({
      "id":"retry-barrier","name":"Retry Barrier","groups":[{
        "id":"main","kind":"main","steps":[{
          "id":"parent","kind":"testItem","retry":{"maxAttempts":2},"steps":[{
            "id":"join","kind":"barrier"
          }]
        }]
      }]
    })json";
    SequenceCompiler compiler;
    const auto compile = compiler.compileJson(QJsonDocument::fromJson(json).object());
    QVERIFY(!compile.ok());
    QVERIFY(std::any_of(compile.errors.cbegin(), compile.errors.cend(), [](const auto& error) {
        return error.message.contains(QStringLiteral("Barrier inside a retrying TestItem"));
    }));
}

void CoreTests::testItemStopSkipsChildrenAndRunsCleanup()
{
    const auto json = R"json(
    {
      "id": "test-item-stop",
      "name": "Test Item Stop",
      "groups": [
        { "id": "main", "kind": "main", "steps": [
          { "id": "parent", "kind": "testItem", "steps": [
            { "id": "child-a", "kind": "action" },
            { "id": "child-b", "kind": "action" }
          ]}
        ]},
        { "id": "cleanup", "kind": "cleanup", "steps": [
          { "id": "cleanup", "kind": "cleanup" }
        ]}
      ]
    })json";
    SequenceCompiler compiler;
    const auto compile = compiler.compileJson(QJsonDocument::fromJson(json).object());
    QVERIFY(compile.ok());
    ExecutionSession session(compile.plan);
    session.addUut("uut-1");
    session.requestStop();
    const auto result = session.run();
    QVERIFY(result.completed);
    const auto report = session.report();
    const auto& uut = report.uuts.first();
    QCOMPARE(findStep(uut, "parent")->outcome, NodeOutcome::Skipped);
    QCOMPARE(findStep(uut, "child-a")->outcome, NodeOutcome::Skipped);
    QCOMPARE(findStep(uut, "child-b")->outcome, NodeOutcome::Skipped);
    const auto* cleanup = findStep(report.sessionSteps, "cleanup");
    QVERIFY(cleanup != nullptr);
    QCOMPARE(cleanup->outcome, NodeOutcome::Passed);
}

void CoreTests::cleanupTestItemRunsAllChildren()
{
    const auto json = R"json({
      "id":"cleanup-test-item","name":"Cleanup TestItem","groups":[
        {"id":"main","kind":"main","steps":[
          {"id":"run","kind":"noop"}
        ]},
        {"id":"cleanup","kind":"cleanup","steps":[
          {"id":"close-all","name":"Close All Devices","kind":"testItem","steps":[
            {"id":"01","key":"close-can","name":"Close CAN","kind":"cleanup"},
            {"id":"02","key":"close-power","name":"Close Power","kind":"cleanup"}
          ]}
        ]}
      ]
    })json";

    SequenceCompiler compiler;
    const auto compile = compiler.compileJson(QJsonDocument::fromJson(json).object());
    QVERIFY2(compile.ok(), qPrintable(compile.errors.isEmpty()
                                          ? QString()
                                          : compile.errors.first().message));
    ExecutionSession session(compile.plan);
    session.addUut("uut-1");
    const auto run = session.run();
    QVERIFY(run.completed);
    QVERIFY(!run.hasError);

    const auto& execution = session.snapshot().sessionExecution;
    QCOMPARE(execution.outcomeOf("close-all"), NodeOutcome::Passed);
    QCOMPARE(execution.outcomeOf("close-all.close-can"), NodeOutcome::Passed);
    QCOMPARE(execution.outcomeOf("close-all.close-power"), NodeOutcome::Passed);

    const auto report = session.report();
    const auto* cleanup = findStep(report.sessionSteps, "close-all");
    QVERIFY(cleanup != nullptr);
    QCOMPARE(cleanup->children.size(), 2);
    QCOMPARE(cleanup->children[0].outcome, NodeOutcome::Passed);
    QCOMPARE(cleanup->children[1].outcome, NodeOutcome::Passed);
}

void CoreTests::cleanupTestItemContinuesAfterChildError()
{
    const auto json = R"json({
      "id":"cleanup-test-item-error","name":"Cleanup TestItem Error","groups":[
        {"id":"setup","kind":"setup","steps":[
          {"id":"open-psu","kind":"action",
           "parameters":{"outcome":"Error"},
           "errorPolicy":{"onError":"RunCleanup"}}
        ]},
        {"id":"cleanup","kind":"cleanup","steps":[
          {"id":"close-all","kind":"testItem","steps":[
            {"id":"01","key":"output-off","kind":"action",
             "parameters":{"outcome":"Error"}},
            {"id":"02","key":"close-psu","kind":"cleanup"}
          ]},
          {"id":"close-fixture","kind":"cleanup"}
        ]}
      ]
    })json";

    SequenceCompiler compiler;
    const auto compile = compiler.compileJson(QJsonDocument::fromJson(json).object());
    QVERIFY2(compile.ok(), qPrintable(compile.errors.isEmpty()
                                          ? QString()
                                          : compile.errors.first().message));

    ExecutionSession session(compile.plan);
    session.addUut("uut-1");
    const auto run = session.run();

    QVERIFY(run.completed);
    QVERIFY(run.hasError);
    QCOMPARE(run.state, ExecutionState::CompletedWithError);
    const auto& sessionExecution = session.snapshot().sessionExecution;
    QCOMPARE(sessionExecution.outcomeOf("close-all.output-off"), NodeOutcome::Error);
    QCOMPARE(sessionExecution.outcomeOf("close-all.close-psu"), NodeOutcome::Passed);
    QCOMPARE(sessionExecution.outcomeOf("close-all"), NodeOutcome::Error);
    QCOMPARE(sessionExecution.outcomeOf("close-fixture"), NodeOutcome::Passed);
}

void CoreTests::cleanupFailureContinuesBestEffortAndCompletesSession()
{
    const auto json = R"json({
      "id":"cleanup-error-terminal","name":"Cleanup Error Terminal","groups":[
        {"id":"setup","kind":"setup","steps":[
          {"id":"open-psu","name":"Open PSU","kind":"action",
           "parameters":{"outcome":"Error","errorCode":"DeviceConnectFailed",
                         "errorMessage":"resource not found"},
           "errorPolicy":{"onError":"RunCleanup"}}
        ]},
        {"id":"main","kind":"main","steps":[
          {"id":"measure","name":"Measure","kind":"action"}
        ]},
        {"id":"cleanup","kind":"cleanup","steps":[
          {"id":"output-off","name":"Set Output OFF","kind":"action",
           "parameters":{"outcome":"Error","errorCode":"DeviceConnectFailed",
                         "errorMessage":"resource not found"}},
          {"id":"set-ovp","name":"Set OVP","kind":"action"},
          {"id":"set-ocp","name":"Set OCP","kind":"action"},
          {"id":"close-psu","name":"Close PSU","kind":"cleanup"}
        ]}
      ]
    })json";

    SequenceCompiler compiler;
    const auto compile = compiler.compileJson(QJsonDocument::fromJson(json).object());
    QVERIFY2(compile.ok(), qPrintable(compile.errors.isEmpty()
                                          ? QString()
                                          : compile.errors.first().message));
    QVERIFY(compile.plan.cleanupRegions.first().bestEffort);

    CollectingRuntimeEventSink events;
    ExecutionSession session(compile.plan, {}, &events);
    session.addUut("uut-1");
    const auto run = session.run();

    QVERIFY(run.completed);
    QVERIFY(run.hasError);
    QCOMPARE(run.state, ExecutionState::CompletedWithError);
    QCOMPARE(session.state(), ExecutionState::CompletedWithError);

    const auto& sessionExecution = session.snapshot().sessionExecution;
    QCOMPARE(sessionExecution.outcomeOf("open-psu"), NodeOutcome::Error);
    const auto& uut = session.uuts().first();
    QCOMPARE(uut.outcomeOf("measure"), NodeOutcome::Skipped);
    QCOMPARE(sessionExecution.outcomeOf("output-off"), NodeOutcome::Error);
    QCOMPARE(sessionExecution.outcomeOf("set-ovp"), NodeOutcome::Passed);
    QCOMPARE(sessionExecution.outcomeOf("set-ocp"), NodeOutcome::Passed);
    QCOMPARE(sessionExecution.outcomeOf("close-psu"), NodeOutcome::Passed);

    const auto report = session.report();
    QVERIFY(report.completed);
    QVERIFY(report.hasError);
    QCOMPARE(report.state, ExecutionState::CompletedWithError);

    bool sawCompletedState = false;
    bool sawUutCompleted = false;
    for (const auto& event : events.records()) {
        sawCompletedState = sawCompletedState ||
            (event.kind == RuntimeEventKind::SessionStateChanged &&
             event.executionState == ExecutionState::CompletedWithError);
        sawUutCompleted = sawUutCompleted ||
            event.kind == RuntimeEventKind::UutCompleted;
    }
    QVERIFY(sawCompletedState);
    QVERIFY(sawUutCompleted);

    auto strictCompile = compiler.compileJson(QJsonDocument::fromJson(json).object());
    QVERIFY(strictCompile.ok());
    QVERIFY(!strictCompile.plan.cleanupRegions.isEmpty());
    strictCompile.plan.cleanupRegions.first().bestEffort = false;

    ExecutionSession strictSession(strictCompile.plan);
    strictSession.addUut("uut-strict");
    const auto strictRun = strictSession.run();
    QVERIFY(strictRun.completed);
    QVERIFY(strictRun.hasError);
    QCOMPARE(strictRun.state, ExecutionState::CompletedWithError);

    const auto& strictExecution = strictSession.snapshot().sessionExecution;
    QCOMPARE(strictExecution.outcomeOf("output-off"), NodeOutcome::Error);
    QCOMPARE(strictExecution.outcomeOf("set-ovp"), NodeOutcome::Skipped);
    QCOMPARE(strictExecution.outcomeOf("set-ocp"), NodeOutcome::Skipped);
    QCOMPARE(strictExecution.outcomeOf("close-psu"), NodeOutcome::Skipped);
}

void CoreTests::incompleteCleanupReturnsCompletedWithError()
{
    const auto json = R"json({
      "id":"incomplete-cleanup-terminal","name":"Incomplete Cleanup Terminal","groups":[
        {"id":"main","kind":"main","steps":[
          {"id":"measure","name":"Measure","kind":"noop"}
        ]},
        {"id":"cleanup","kind":"cleanup","steps":[
          {"id":"cleanup-barrier","name":"Cleanup Barrier","kind":"barrier",
           "barrier":{"barrierName":"cleanup-terminal","expectedUutCount":2}}
        ]}
      ]
    })json";

    SequenceCompiler compiler;
    const auto compile = compiler.compileJson(QJsonDocument::fromJson(json).object());
    QVERIFY2(compile.ok(), qPrintable(compile.errors.isEmpty()
                                          ? QString()
                                          : compile.errors.first().message));

    CollectingRuntimeEventSink events;
    ExecutionSession session(compile.plan, {}, &events);
    session.addUut("uut-1");
    session.addUut("uut-2");
    const auto run = session.run();

    QVERIFY(!run.completed);
    QVERIFY(run.hasError);
    QCOMPARE(run.state, ExecutionState::CompletedWithError);
    QCOMPARE(session.state(), ExecutionState::CompletedWithError);
    QCOMPARE(session.snapshot().sessionExecution.stateOf("cleanup-barrier"),
             ActivationState::WaitingAtBarrier);

    const auto report = session.report();
    QVERIFY(report.completed);
    QVERIFY(report.hasError);
    QCOMPARE(report.state, ExecutionState::CompletedWithError);

    const auto runtimeEvents = events.records();
    const bool sawTerminalState = std::any_of(
        runtimeEvents.cbegin(),
        runtimeEvents.cend(),
        [](const RuntimeEvent& event) {
            return event.kind == RuntimeEventKind::SessionStateChanged &&
                   event.executionState == ExecutionState::CompletedWithError;
        });
    QVERIFY(sawTerminalState);
}

void CoreTests::testItemAggregatesErrorSeverity()
{
    const auto json = R"json(
    {
      "id": "test-item-error",
      "name": "Test Item Error",
      "groups": [{
        "id": "main",
        "kind": "main",
        "steps": [{
          "id": "parent",
          "kind": "testItem",
          "steps": [
            { "id": "child-error", "kind": "action", "parameters": { "outcome": "Error" } },
            { "id": "child-pass", "kind": "action" }
          ]
        }]
      }]
    })json";
    SequenceCompiler compiler;
    const auto compile = compiler.compileJson(QJsonDocument::fromJson(json).object());
    QVERIFY(compile.ok());
    ExecutionSession session(compile.plan);
    session.addUut("uut-1");
    const auto result = session.run();
    QVERIFY(result.completed);
    QVERIFY(result.hasError);
    const auto report = session.report();
    const auto* parent = findStep(report.uuts.first(), "parent");
    QVERIFY(parent != nullptr);
    QCOMPARE(parent->outcome, NodeOutcome::Error);
    QCOMPARE(parent->children[0].outcome, NodeOutcome::Error);
    QCOMPARE(parent->children[1].outcome, NodeOutcome::Skipped);
}

void CoreTests::nestedTestItemsAggregateDirectChildrenRecursively()
{
    const auto json = R"json({
      "id": "nested-test-items",
      "name": "Nested Test Items",
      "groups": [{
        "id": "main",
        "kind": "main",
        "steps": [{
          "id": "outer",
          "kind": "testItem",
          "steps": [
            { "id": "outer-first", "kind": "action" },
            {
              "id": "inner",
              "kind": "testItem",
              "steps": [
                { "id": "inner-pass", "kind": "action" },
                { "id": "inner-fail", "kind": "action", "parameters": { "outcome": "Failed" } }
              ]
            },
            { "id": "outer-last", "kind": "action" }
          ]
        }]
      }]
    })json";

    SequenceCompiler compiler;
    const auto compile = compiler.compileJson(QJsonDocument::fromJson(json).object());
    QVERIFY2(compile.ok(), qPrintable(compile.errors.isEmpty() ? QString() : compile.errors.first().message));
    QCOMPARE(compile.plan.testItemRegions.size(), 2);
    QVERIFY(compile.plan.structuralParentOf("outer.inner") == std::optional<NodeId>("outer"));
    QVERIFY(compile.plan.structuralParentOf("outer.inner.inner-fail") == std::optional<NodeId>("outer.inner"));

    ExecutionSession session(compile.plan);
    session.addUut("uut-1");
    const auto run = session.run();
    QVERIFY(run.completed);
    QVERIFY(run.hasError);

    const auto report = session.report();
    const auto* outer = findStep(report.uuts.first(), "outer");
    const auto* inner = findStep(report.uuts.first(), "inner");
    QVERIFY(outer != nullptr);
    QVERIFY(inner != nullptr);
    QCOMPARE(outer->outcome, NodeOutcome::Failed);
    QCOMPARE(outer->children.size(), 3);
    QCOMPARE(inner->outcome, NodeOutcome::Failed);
    QCOMPARE(inner->children.size(), 2);
    QCOMPARE(findStep(report.uuts.first(), "inner-pass")->outcome, NodeOutcome::Passed);
    QCOMPARE(findStep(report.uuts.first(), "inner-fail")->outcome, NodeOutcome::Failed);
    QCOMPARE(findStep(report.uuts.first(), "outer-last")->outcome, NodeOutcome::Skipped);
}

void CoreTests::testItemContainingLoopAggregatesIterationFailures()
{
    const auto json = R"json({
      "id": "test-item-loop",
      "name": "Test Item Loop",
      "groups": [{
        "id": "main",
        "kind": "main",
        "steps": [{
          "id": "parent",
          "kind": "testItem",
          "steps": [
            {
              "id": "repeat-check",
              "kind": "loop",
              "loop": { "variable": "sample", "from": 0, "to": 2, "step": 1 },
              "steps": [
                { "id": "sample-check", "kind": "action", "parameters": { "outcome": "Failed" } }
              ]
            },
            { "id": "after-repeat", "kind": "action" }
          ]
        }]
      }]
    })json";

    SequenceCompiler compiler;
    const auto compile = compiler.compileJson(QJsonDocument::fromJson(json).object());
    QVERIFY2(compile.ok(), qPrintable(compile.errors.isEmpty() ? QString() : compile.errors.first().message));
    QCOMPARE(compile.plan.loopRegions.size(), 1);
    QCOMPARE(compile.plan.loopRegions.first().childNodeIds, QVector<NodeId>{"parent.repeat-check.sample-check"});
    QVERIFY(compile.plan.isInsideTestItem("parent.repeat-check.sample-check"));

    ExecutionSession session(compile.plan);
    session.addUut("uut-1");
    const auto run = session.run();
    QVERIFY(run.completed);
    QVERIFY(run.hasError);
    QCOMPARE(session.uuts().first().activations.value("parent.repeat-check.sample-check").attempts.size(), 3);

    const auto report = session.report();
    const auto& uut = report.uuts.first();
    const auto* parent = findStep(uut, "parent");
    const auto* loop = findStep(uut, "repeat-check");
    QVERIFY(parent != nullptr);
    QVERIFY(loop != nullptr);
    QCOMPARE(parent->outcome, NodeOutcome::Failed);
    QCOMPARE(loop->outcome, NodeOutcome::Failed);
    QCOMPARE(loop->children.size(), 1);
    QCOMPARE(loop->children.first().attempts.size(), 3);
    QCOMPARE(findStep(uut, "after-repeat")->outcome, NodeOutcome::Skipped);
}

void CoreTests::loopTestItemChildrenKeepSerialOrderAcrossIterations()
{
    const auto json = R"json({
      "id": "loop-test-item-order",
      "name": "Loop Test Item Order",
      "groups": [{
        "id": "main",
        "kind": "main",
        "steps": [{
          "id": "repeat",
          "kind": "loop",
          "loop": { "variable": "index", "from": 0, "to": 3, "step": 1 },
          "steps": [{
            "id": "item",
            "kind": "testItem",
            "steps": [
              { "id": "first", "kind": "action" },
              { "id": "second", "kind": "action" },
              { "id": "third", "kind": "action" }
            ]
          }]
        }]
      }]
    })json";

    class EventSink final : public IRuntimeEventSink {
    public:
        void publish(const RuntimeEvent& event) override
        {
            if (event.kind == RuntimeEventKind::AttemptCompleted &&
                (event.nodeLocalId == "first" || event.nodeLocalId == "second" || event.nodeLocalId == "third")) {
                completedNodes.push_back(event.nodeLocalId);
            }
        }
        QVector<NodeId> completedNodes;
    } sink;

    SequenceCompiler compiler;
    const auto compile = compiler.compileJson(QJsonDocument::fromJson(json).object());
    QVERIFY2(compile.ok(), qPrintable(compile.errors.isEmpty() ? QString() : compile.errors.first().message));

    ExecutionSession session(compile.plan, {}, &sink);
    session.addUut("uut-1");
    const auto run = session.run();
    QVERIFY(run.completed);
    QVERIFY(!run.hasError);

    const QVector<NodeId> expected = {
        "first", "second", "third",
        "first", "second", "third",
        "first", "second", "third",
        "first", "second", "third",
    };
    QCOMPARE(sink.completedNodes, expected);
}

void CoreTests::continuePolicyAdvancesAfterOrdinaryStepFailure()
{
    const auto json = R"json({
      "id": "continue-step",
      "name": "Continue Step",
      "groups": [{
        "id": "main",
        "kind": "main",
        "steps": [
          {
            "id": "allowed-failure",
            "kind": "action",
            "parameters": { "outcome": "Failed" },
            "errorPolicy": { "onFail": "Continue" }
          },
          { "id": "after-failure", "kind": "action" }
        ]
      }]
    })json";

    SequenceCompiler compiler;
    const auto compile = compiler.compileJson(QJsonDocument::fromJson(json).object());
    QVERIFY(compile.ok());
    ExecutionSession session(compile.plan);
    session.addUut("uut-1");
    const auto run = session.run();
    QVERIFY(run.completed);
    QVERIFY(run.hasError);
    QCOMPARE(session.uuts().first().outcomeOf("allowed-failure"), NodeOutcome::Failed);
    QCOMPARE(session.uuts().first().outcomeOf("after-failure"), NodeOutcome::Passed);
}

void CoreTests::continuePolicyAdvancesAfterTestItemFailure()
{
    const auto json = R"json({
      "id": "continue-test-item",
      "name": "Continue Test Item",
      "groups": [{
        "id": "main",
        "kind": "main",
        "steps": [
          {
            "id": "failed-item",
            "kind": "testItem",
            "errorPolicy": { "onFail": "Continue" },
            "steps": [
              { "id": "failed-child", "kind": "action", "parameters": { "outcome": "Failed" } },
              { "id": "remaining-child", "kind": "action" }
            ]
          },
          { "id": "after-item", "kind": "action" }
        ]
      }]
    })json";

    SequenceCompiler compiler;
    const auto compile = compiler.compileJson(QJsonDocument::fromJson(json).object());
    QVERIFY(compile.ok());
    ExecutionSession session(compile.plan);
    session.addUut("uut-1");
    const auto run = session.run();
    QVERIFY(run.completed);
    QVERIFY(run.hasError);
    QCOMPARE(session.uuts().first().outcomeOf("failed-item"), NodeOutcome::Failed);
    QCOMPARE(session.uuts().first().outcomeOf("failed-item.remaining-child"), NodeOutcome::Skipped);
    QCOMPARE(session.uuts().first().outcomeOf("after-item"), NodeOutcome::Passed);
}

void CoreTests::statementAndSequenceCallKeepDistinctRuntimeKinds()
{
    QCOMPARE(toExecNodeKind(StepKind::Statement), ExecNodeKind::Statement);
    QCOMPARE(toExecNodeKind(StepKind::SequenceCall), ExecNodeKind::SequenceCall);

    NodeRunner runner;
    NodeExecutionContext context;
    context.uutId = "uut-1";
    context.frameId = "root";

    ExecNode statement;
    statement.id = "statement";
    statement.kind = ExecNodeKind::Statement;
    const auto statementResult = runner.run(statement, context);
    QCOMPARE(statementResult.outcome, NodeOutcome::Error);
    QCOMPARE(statementResult.errorCode, QString("StatementNotImplemented"));

    ExecNode sequenceCall;
    sequenceCall.id = "sequence-call";
    sequenceCall.kind = ExecNodeKind::SequenceCall;
    const auto sequenceCallResult = runner.run(sequenceCall, context);
    QCOMPARE(sequenceCallResult.outcome, NodeOutcome::Error);
    QCOMPARE(sequenceCallResult.errorCode, QString("SequenceCallNotImplemented"));

    ExecutionReport report;
    UutReport uut;
    uut.uutId = "uut-1";
    StepReport statementReport;
    statementReport.stepId = statement.id;
    statementReport.kind = statement.kind;
    StepReport sequenceCallReport;
    sequenceCallReport.stepId = sequenceCall.id;
    sequenceCallReport.kind = sequenceCall.kind;
    uut.steps = {statementReport, sequenceCallReport};
    report.uuts = {uut};

    const auto parsed = parseExecutionReport(serializeExecutionReport(report));
    QVERIFY(parsed.ok());
    QCOMPARE(parsed.report.uuts.first().steps[0].kind, ExecNodeKind::Statement);
    QCOMPARE(parsed.report.uuts.first().steps[1].kind, ExecNodeKind::SequenceCall);
}

void CoreTests::reportOrdersTestItemChildrenByTopology()
{
    const auto json = R"json({
      "id": "test-item-report-order",
      "name": "Test Item Report Order",
      "groups": [{
        "id": "main",
        "kind": "main",
        "steps": [{
          "id": "item",
          "kind": "testItem",
          "steps": [
            { "id": "first", "kind": "action" },
            { "id": "second", "kind": "action" },
            { "id": "third", "kind": "action" }
          ]
        }]
      }]
    })json";

    SequenceCompiler compiler;
    auto compile = compiler.compileJson(QJsonDocument::fromJson(json).object());
    QVERIFY(compile.ok());
    auto& region = compile.plan.testItemRegions.first();
    std::reverse(region.childNodeIds.begin(), region.childNodeIds.end());
    QCOMPARE(region.childNodeIds, QVector<NodeId>({"item.third", "item.second", "item.first"}));

    ExecutionSession session(compile.plan);
    session.addUut("uut-1");
    const auto run = session.run();
    QVERIFY(run.completed);

    const auto report = session.report();
    const auto* item = findStep(report.uuts.first(), "item");
    QVERIFY(item != nullptr);
    QCOMPARE(item->children.size(), 3);
    QCOMPARE(item->children[0].stepId, QString("first"));
    QCOMPARE(item->children[1].stepId, QString("second"));
    QCOMPARE(item->children[2].stepId, QString("third"));
}

void CoreTests::testItemIgnoresSkippedChildrenWhenAggregating()
{
    ExecutionPlan plan;
    plan.id = "plan:test-item-skipped";

    ExecNode parent;
    parent.id = "parent";
    parent.kind = ExecNodeKind::TestItem;
    QVERIFY(plan.addNode(parent));

    ExecNode passedChild;
    passedChild.id = "passed-child";
    passedChild.kind = ExecNodeKind::Action;
    QVERIFY(plan.addNode(passedChild));

    ExecNode skippedChild;
    skippedChild.id = "skipped-child";
    skippedChild.kind = ExecNodeKind::Action;
    QVERIFY(plan.addNode(skippedChild));
    plan.testItemRegions.push_back({parent.id, {passedChild.id, skippedChild.id}});

    ExecutionSession session(plan);
    auto& uut = session.addUut("uut-1");
    auto& parentActivation = uut.ensureActivation(parent.id, "root");
    parentActivation.state = ActivationState::WaitingForDependency;

    const auto setTerminal = [&uut](const NodeId& nodeId,
                                    ActivationState state,
                                    NodeOutcome outcome) {
        auto& activation = uut.ensureActivation(nodeId, "root");
        activation.state = state;
        NodeAttempt attempt;
        attempt.activationId = activation.id;
        attempt.attemptIndex = 0;
        attempt.state = AttemptState::Completed;
        attempt.result.nodeId = nodeId;
        attempt.result.outcome = outcome;
        activation.attempts.push_back(attempt);
    };
    setTerminal(passedChild.id, ActivationState::Passed, NodeOutcome::Passed);
    setTerminal(skippedChild.id, ActivationState::Skipped, NodeOutcome::Skipped);

    const auto run = session.run();
    QVERIFY(run.completed);
    QVERIFY(!run.hasError);
    QCOMPARE(session.uuts().first().outcomeOf(parent.id), NodeOutcome::Passed);

    const auto report = session.report();
    const auto* parentReport = findStep(report.uuts.first(), parent.id);
    QVERIFY(parentReport != nullptr);
    QCOMPARE(parentReport->outcome, NodeOutcome::Passed);
    QCOMPARE(parentReport->children[1].outcome, NodeOutcome::Skipped);
}

void CoreTests::scopedStepResultsFlowAcrossTestItemsPerUut()
{
    QFile file(examplePath("scoped_result_sequence.json"));
    QVERIFY2(file.open(QIODevice::ReadOnly), qPrintable(file.errorString()));
    const auto document = QJsonDocument::fromJson(file.readAll());
    QVERIFY(document.isObject());

    SequenceCompiler compiler;
    const auto compile = compiler.compileJson(document.object());
    QVERIFY2(compile.ok(), qPrintable(compile.errors.isEmpty() ? QString() : compile.errors.first().message));
    QVERIFY(compile.plan.node("001.tx") != nullptr);
    QVERIFY(compile.plan.node("001.rx") != nullptr);
    QVERIFY(compile.plan.node("002.parse") != nullptr);
    QVERIFY(compile.plan.node("002.limit") != nullptr);
    QVERIFY(!compile.plan.node("rx"));

    const auto dataEdgeExists = [&](const NodeId& from, const NodeId& to) {
        return std::any_of(compile.plan.edges.cbegin(), compile.plan.edges.cend(), [&](const ExecEdge& edge) {
            return edge.from == from && edge.to == to && edge.condition == "step-result";
        });
    };
    QVERIFY(dataEdgeExists("001.rx", "002.parse"));
    QVERIFY(dataEdgeExists("002.parse", "002.limit"));

    ExecutionSession session(compile.plan);
    session.addUut("uut-A");
    session.addUut("uut-B");
    const auto run = session.run();
    QVERIFY(run.completed);
    QVERIFY(!run.hasError);

    for (const auto& uutId : {QString("uut-A"), QString("uut-B")}) {
        const auto parsed = session.results().latest(uutId, "root", "002.parse");
        const auto limited = session.results().latest(uutId, "root", "002.limit");
        QVERIFY(parsed.has_value());
        QVERIFY(limited.has_value());
        QCOMPARE(parsed->result.outputs.value("frame").toMap().value("owner").toString(), uutId);
        QCOMPARE(limited->result.outputs.value("actual").toDouble(), 5.01);
        QCOMPARE(limited->result.outputs.value("passed").toBool(), true);
        QCOMPARE(limited->result.measurements.size(), 1);
        QCOMPARE(limited->result.measurements.first().name, QString("CAN_VOLTAGE"));
        QCOMPARE(limited->result.measurements.first().status, MeasurementStatus::Passed);
    }

    const auto report = session.report();
    const auto* rx = findStep(report.uuts.first(), "03");
    QVERIFY(rx != nullptr);
    QCOMPARE(rx->nodePath, QString("001.rx"));
}

void CoreTests::resultStoreTracksRetryAndLoopHistory()
{
    const auto json = R"json({
      "id": "result-history",
      "name": "Result History",
      "groups": [{
        "id": "main",
        "kind": "main",
        "steps": [
          {
            "id": "001",
            "kind": "action",
            "retry": { "maxAttempts": 2 },
            "parameters": {
              "failUntilAttempt": 0,
              "outputs": { "token": 42 }
            }
          },
          {
            "id": "002",
            "kind": "action",
            "inputs": { "token": "${step:001.outputs.token}" },
            "parameters": { "echoInputs": true }
          },
          {
            "id": "003",
            "kind": "loop",
            "loop": { "variable": "sample", "from": 0, "to": 2, "step": 1 },
            "steps": [{
              "id": "01",
              "key": "sample",
              "kind": "action",
              "parameters": { "outputs": { "value": "${loop.value}" } }
            }]
          },
          {
            "id": "004",
            "kind": "action",
            "inputs": { "latest": "${step:003.sample.outputs.value}" },
            "parameters": { "echoInputs": true }
          }
        ]
      }]
    })json";

    SequenceCompiler compiler;
    const auto compile = compiler.compileJson(QJsonDocument::fromJson(json).object());
    QVERIFY2(compile.ok(), qPrintable(compile.errors.isEmpty() ? QString() : compile.errors.first().message));
    ExecutionSession session(compile.plan);
    session.addUut("uut-1");
    const auto run = session.run();
    QVERIFY(run.completed);
    QVERIFY(!run.hasError);

    const auto retryHistory = session.results().history("uut-1", "root", "001");
    QCOMPARE(retryHistory.size(), 2);
    QCOMPARE(retryHistory[0].result.outcome, NodeOutcome::Failed);
    QCOMPARE(retryHistory[1].result.outcome, NodeOutcome::Passed);
    QCOMPARE(session.results().latest("uut-1", "root", "002")->result.outputs.value("token").toInt(), 42);

    const auto loopHistory = session.results().history("uut-1", "root", "003.sample");
    QCOMPARE(loopHistory.size(), 3);
    QCOMPARE(loopHistory[0].result.outputs.value("value").toInt(), 0);
    QCOMPARE(loopHistory[2].result.outputs.value("value").toInt(), 2);
    QCOMPARE(session.results().latest("uut-1", "root", "004")->result.outputs.value("latest").toInt(), 2);
}

void CoreTests::compilerRejectsInvalidStepResultReferencesAndScopedKeys()
{
    SequenceCompiler compiler;
    const auto compileText = [&compiler](const QByteArray& json) {
        return compiler.compileJson(QJsonDocument::fromJson(json).object());
    };
    const auto hasError = [](const CompileResult& result, const QString& text) {
        return std::any_of(result.errors.cbegin(), result.errors.cend(), [&](const CompileError& error) {
            return error.message.contains(text, Qt::CaseInsensitive);
        });
    };

    const auto missing = compileText(R"json({"id":"missing","name":"missing","groups":[{"id":"main","kind":"main","steps":[{"id":"001","kind":"action","inputs":{"x":"${step:999.outputs.x}"}}]}]})json");
    QVERIFY(!missing.ok());
    QVERIFY(hasError(missing, "missing node"));

    const auto forward = compileText(R"json({"id":"forward","name":"forward","groups":[{"id":"main","kind":"main","steps":[{"id":"001","kind":"action","inputs":{"x":"${step:002.outputs.x}"}},{"id":"002","kind":"action"}]}]})json");
    QVERIFY(!forward.ok());
    QVERIFY(hasError(forward, "not guaranteed"));

    const auto self = compileText(R"json({"id":"self","name":"self","groups":[{"id":"main","kind":"main","steps":[{"id":"001","kind":"action","inputs":{"x":"${step:001.outputs.x}"}}]}]})json");
    QVERIFY(!self.ok());
    QVERIFY(hasError(self, "not guaranteed"));

    const auto duplicateKey = compileText(R"json({"id":"keys","name":"keys","groups":[{"id":"main","kind":"main","steps":[{"id":"001","kind":"testItem","steps":[{"id":"01","key":"rx","kind":"action"},{"id":"02","key":"rx","kind":"action"}]}]}]})json");
    QVERIFY(!duplicateKey.ok());
    QVERIFY(hasError(duplicateKey, "Duplicate sibling step key"));

    const auto reservedKey = compileText(R"json({"id":"reserved","name":"reserved","groups":[{"id":"main","kind":"main","steps":[{"id":"001","kind":"testItem","steps":[{"id":"01","key":"outputs","kind":"action"}]}]}]})json");
    QVERIFY(!reservedKey.ok());
    QVERIFY(hasError(reservedKey, "reserved"));
}

void CoreTests::compilerDeduplicatesMultipleReferencesToSameStep()
{
    const auto json = R"json({
      "id": "deduplicate-data-edges",
      "name": "Deduplicate Data Edges",
      "groups": [{
        "id": "main",
        "kind": "main",
        "steps": [
          {"id":"001","kind":"action","parameters":{"outputs":{"a":1,"b":2}}},
          {"id":"002","kind":"action","inputs":{
            "first":"${step:001.outputs.a}",
            "second":"${step:001.outputs.b}"
          }}
        ]
      }]
    })json";

    SequenceCompiler compiler;
    const auto compile = compiler.compileJson(QJsonDocument::fromJson(json).object());
    QVERIFY(compile.ok());

    const auto dataEdgeCount = std::count_if(
        compile.plan.edges.cbegin(), compile.plan.edges.cend(), [](const ExecEdge& edge) {
            return edge.from == "001" && edge.to == "002" &&
                   edge.condition == "step-result";
        });
    QCOMPARE(dataEdgeCount, 1);
}

void CoreTests::runtimeResultLookupReportsMissingAndNonPassedSources()
{
    const auto runCase = [](const QString& sourceParameters, const QString& reference) {
        const auto json = QString(R"json({
          "id": "runtime-lookup",
          "name": "Runtime Lookup",
          "groups": [{"id":"main","kind":"main","steps":[
            {"id":"001","kind":"action","errorPolicy":{"onFail":"Continue"},"parameters":%1},
            {"id":"002","kind":"action","inputs":{"value":"%2"},"parameters":{"echoInputs":true}}
          ]}]
        })json").arg(sourceParameters, reference).toUtf8();
        SequenceCompiler compiler;
        const auto compile = compiler.compileJson(QJsonDocument::fromJson(json).object());
        ExecutionSession session(compile.plan);
        session.addUut("uut-1");
        return std::pair<CompileResult, ExecutionSessionResult>{compile, session.run()};
    };

    const auto missingField = runCase(R"({"outputs":{"present":1}})",
                                      R"(${step:001.outputs.absent})");
    QVERIFY(missingField.first.ok());
    QVERIFY(missingField.second.completed);
    QVERIFY(missingField.second.hasError);
    QVERIFY(std::any_of(missingField.second.nodeResults.cbegin(),
                        missingField.second.nodeResults.cend(),
                        [](const NodeResult& result) {
                            return result.nodeId == "002" &&
                                   result.errorMessage.contains("StepResultValueNotFound");
                        }));

    const auto failedSource = runCase(R"({"outcome":"Failed","outputs":{"value":1}})",
                                      R"(${step:001.outputs.value})");
    QVERIFY(failedSource.first.ok());
    QVERIFY(failedSource.second.completed);
    QVERIFY(failedSource.second.hasError);
    QVERIFY(std::any_of(failedSource.second.nodeResults.cbegin(),
                        failedSource.second.nodeResults.cend(),
                        [](const NodeResult& result) {
                            return result.nodeId == "002" &&
                                   result.errorMessage.contains("StepResultNotPassed");
                        }));

    ExecutionPlan plan;
    plan.addNode({"001"});
    ExecutionResultStore store(plan);
    NodeResult skipped;
    skipped.nodeId = "001";
    skipped.outcome = NodeOutcome::Skipped;
    store.commit("uut-1", "root", "001", 0, skipped);
    const auto reference = parseStepResultReference("step:001.outputs.value");
    QVERIFY(reference.has_value());
    const auto lookup = store.lookup("uut-1", "root", "002", *reference);
    QVERIFY(!lookup.found);
    QCOMPARE(lookup.errorCode, QString("StepResultNotPassed"));
}

void CoreTests::limitNodeSupportsNumericStringAndBooleanComparisons()
{
    NodeRunner runner;
    NodeExecutionContext context;
    context.uutId = "uut-1";
    context.frameId = "root";
    CollectingModuleLogSink limitLogs;
    context.logSink = &limitLogs;

    const auto runLimit = [&](const QVariantMap& inputs, const QVariantMap& parameters) {
        ExecNode node;
        node.id = "limit";
        node.displayName = "Limit";
        node.kind = ExecNodeKind::Limit;
        node.payload = parameters;
        node.payload.insert("inputs", inputs);
        return runner.run(node, context);
    };

    auto result = runLimit({{"actual", 4.8}},
                           {{"comparison", "between"}, {"lower", 4.8}, {"upper", 5.2}, {"unit", "V"}});
    QCOMPARE(result.outcome, NodeOutcome::Passed);
    QCOMPARE(result.measurements.size(), 1);
    QVERIFY(result.measurements.first().hasLowerLimit);
    QVERIFY(result.measurements.first().hasUpperLimit);
    QCOMPARE(result.measurements.first().status, MeasurementStatus::Passed);
    const auto firstLogs = limitLogs.records();
    QCOMPARE(firstLogs.size(), 2);
    QVERIFY(firstLogs[0].message.contains(QStringLiteral("LIMIT_CHECK actual=4.8")));
    QVERIFY(firstLogs[1].message.contains(QStringLiteral("LIMIT_RESULT PASS")));
    QVERIFY(firstLogs[1].message.contains(QStringLiteral("lower=4.8")));
    QVERIFY(firstLogs[1].message.contains(QStringLiteral("upper=5.2")));

    result = runLimit({{"actual", 5.2}},
                      {{"comparison", "between"}, {"expected", 5.0}, {"tolerance", 0.2}});
    QCOMPARE(result.outcome, NodeOutcome::Passed);
    QCOMPARE(result.measurements.first().lowerLimit, 4.8);
    QCOMPARE(result.measurements.first().upperLimit, 5.2);
    QCOMPARE(result.measurements.first().attributes.value("limitsDerived").toBool(), true);

    result = runLimit({{"actual", 5.21}},
                      {{"comparison", "between"}, {"expected", 5.0}, {"tolerance", 0.2}});
    QCOMPARE(result.outcome, NodeOutcome::Failed);

    result = runLimit({{"actual", 4.8}},
                      {{"comparison", "between"}, {"lower", 4.8}, {"upper", 5.2}, {"inclusive", false}});
    QCOMPARE(result.outcome, NodeOutcome::Failed);

    result = runLimit({{"actual", 5.0}, {"expected", 4.9}}, {{"comparison", ">"}});
    QCOMPARE(result.outcome, NodeOutcome::Passed);
    QVERIFY(result.measurements.first().hasLowerLimit);
    QCOMPARE(result.measurements.first().lowerLimit, 4.9);

    result = runLimit({{"actual", 5.001}},
                      {{"comparison", "equal"}, {"expected", 5.0}, {"tolerance", 0.01}});
    QCOMPARE(result.outcome, NodeOutcome::Passed);
    QVERIFY(result.measurements.first().hasLowerLimit);
    QVERIFY(result.measurements.first().hasUpperLimit);
    QCOMPARE(result.measurements.first().lowerLimit, 4.99);
    QCOMPARE(result.measurements.first().upperLimit, 5.01);

    result = runLimit({{"actual", 8.0}},
                      {{"comparison", "equal"}, {"expected", 8.0}});
    QCOMPARE(result.outcome, NodeOutcome::Passed);
    QCOMPARE(result.measurements.first().lowerLimit, 8.0);
    QCOMPARE(result.measurements.first().upperLimit, 8.0);

    result = runLimit({{"actual", "62 F1 90"}},
                      {{"comparison", "contains"}, {"expected", "F1"}});
    QCOMPARE(result.outcome, NodeOutcome::Passed);

    result = runLimit({{"actual", true}}, {{"comparison", "isTrue"}});
    QCOMPARE(result.outcome, NodeOutcome::Passed);
}

void CoreTests::limitSpecificationsAppearInReportBeforeExecution()
{
    ExecNode limit;
    limit.id = QStringLiteral("check-voltage");
    limit.displayName = QStringLiteral("Check Voltage");
    limit.kind = ExecNodeKind::Limit;
    limit.payload.insert(QStringLiteral("comparison"), QStringLiteral("between"));
    limit.payload.insert(QStringLiteral("expected"), 5.0);
    limit.payload.insert(QStringLiteral("tolerance"), 0.2);
    limit.payload.insert(QStringLiteral("measurementName"), QStringLiteral("VOUT"));
    limit.payload.insert(QStringLiteral("unit"), QStringLiteral("V"));
    limit.payload.insert(
        QStringLiteral("inputs"),
        QVariantMap{{QStringLiteral("actual"),
                     QStringLiteral("${step:read.outputs.value}")}});

    ExecutionPlan plan;
    plan.id = QStringLiteral("limit-preview-plan");
    QVERIFY(plan.addNode(limit));
    plan.entryNodeId = limit.id;
    plan.exitNodeId = limit.id;

    ExecutionSession session(plan);
    session.addUut(QStringLiteral("uut-1"));
    const auto report = session.report();
    QCOMPARE(report.uuts.size(), 1);
    const auto* step = findStep(report.uuts.first(), limit.id);
    QVERIFY(step != nullptr);
    QCOMPARE(step->attempts.size(), 0);
    QCOMPARE(step->measurements.size(), 1);
    const auto& measurement = step->measurements.first();
    QCOMPARE(measurement.name, QStringLiteral("VOUT"));
    QCOMPARE(measurement.unit, QStringLiteral("V"));
    QCOMPARE(measurement.status, MeasurementStatus::Unknown);
    QVERIFY(!measurement.value.isValid());
    QVERIFY(measurement.hasLowerLimit);
    QCOMPARE(measurement.lowerLimit, 4.8);
    QVERIFY(measurement.hasUpperLimit);
    QCOMPARE(measurement.upperLimit, 5.2);

    const auto stringPreview = configuredMeasurementPreview(
        {{QStringLiteral("comparison"), QStringLiteral("equal")},
         {QStringLiteral("expected"), QStringLiteral("READY")}},
        QStringLiteral("State"));
    QCOMPARE(stringPreview.attributes.value(QStringLiteral("displayLower")).toString(),
             QStringLiteral("READY"));
    QCOMPARE(stringPreview.attributes.value(QStringLiteral("displayUpper")).toString(),
             QStringLiteral("READY"));

    const auto expressionPreview = configuredMeasurementPreview(
        {{QStringLiteral("comparison"), QStringLiteral("between")},
         {QStringLiteral("expected"), QStringLiteral("${globals.targetVoltage}")},
         {QStringLiteral("tolerance"), 0.1}},
        QStringLiteral("Dynamic Voltage"));
    QCOMPARE(expressionPreview.attributes.value(QStringLiteral("displayLower")).toString(),
             QStringLiteral("${globals.targetVoltage} - 0.1"));
    QCOMPARE(expressionPreview.attributes.value(QStringLiteral("displayUpper")).toString(),
             QStringLiteral("${globals.targetVoltage} + 0.1"));
}

void CoreTests::limitNodeDistinguishesFailuresFromConfigurationErrors()
{
    NodeRunner runner;
    NodeExecutionContext context;
    context.uutId = "uut-1";
    context.frameId = "root";

    const auto runLimit = [&](const QVariantMap& inputs, const QVariantMap& parameters) {
        ExecNode node;
        node.id = "limit";
        node.displayName = "Limit";
        node.kind = ExecNodeKind::Limit;
        node.payload = parameters;
        node.payload.insert("inputs", inputs);
        return runner.run(node, context);
    };

    auto result = runLimit({{"actual", 5.3}},
                           {{"comparison", "between"}, {"lower", 4.8}, {"upper", 5.2}});
    QCOMPARE(result.outcome, NodeOutcome::Failed);
    QCOMPARE(result.errorCode, QString("LimitFailed"));
    QCOMPARE(result.measurements.first().status, MeasurementStatus::Failed);

    result = runLimit({}, {{"comparison", "between"}, {"lower", 4.8}, {"upper", 5.2}});
    QCOMPARE(result.outcome, NodeOutcome::Error);
    QCOMPARE(result.errorCode, QString("LimitActualMissing"));

    result = runLimit({{"actual", "not-a-number"}},
                      {{"comparison", "between"}, {"lower", 4.8}, {"upper", 5.2}});
    QCOMPARE(result.outcome, NodeOutcome::Error);
    QCOMPARE(result.errorCode, QString("LimitTypeError"));

    result = runLimit({{"actual", 5.0}},
                      {{"comparison", "between"}, {"lower", 5.2}, {"upper", 4.8}});
    QCOMPARE(result.outcome, NodeOutcome::Error);
    QCOMPARE(result.errorCode, QString("LimitConfigurationError"));

    result = runLimit({{"actual", 5.0}}, {{"comparison", "teleport"}});
    QCOMPARE(result.outcome, NodeOutcome::Error);
    QCOMPARE(result.errorCode, QString("UnsupportedLimitComparison"));
}

void CoreTests::limitNodePreservesConfiguredLimitsWhenVariableResolutionFails()
{
    NodeRunner runner;
    ExecutionPlan plan;
    plan.addNode({"missing"});
    plan.addNode({"check-dlc"});
    ExecutionResultStore resultStore(plan);
    NodeExecutionContext context;
    context.uutId = "uut-1";
    context.frameId = "root";
    context.resultStore = &resultStore;

    ExecNode node;
    node.id = "check-dlc";
    node.displayName = "Check DLC";
    node.kind = ExecNodeKind::Limit;
    node.payload = {
        {"comparison", "equal"},
        {"expected", 8},
        {"measurementName", "GCAN_ECHO_DLC"},
        {"unit", "byte"},
        {"inputs", QVariantMap{{"actual", "${step:missing.outputs.dlc}"}}}
    };

    const auto result = runner.run(node, context);
    QCOMPARE(result.outcome, NodeOutcome::Error);
    QCOMPARE(result.errorCode, QString("RuntimeVariableResolutionError"));
    QCOMPARE(result.measurements.size(), 1);

    const auto& measurement = result.measurements.first();
    QCOMPARE(measurement.name, QString("GCAN_ECHO_DLC"));
    QCOMPARE(measurement.unit, QString("byte"));
    QVERIFY(!measurement.value.isValid());
    QCOMPARE(measurement.status, MeasurementStatus::Error);
    QCOMPARE(measurement.errorCode, QString("RuntimeVariableResolutionError"));
    QVERIFY(measurement.hasLowerLimit);
    QCOMPARE(measurement.lowerLimit, 8.0);
    QVERIFY(measurement.hasUpperLimit);
    QCOMPARE(measurement.upperLimit, 8.0);

    node.id = "check-payload";
    node.displayName = "Check Payload";
    node.payload.insert("expected", "43 58 31 2D 47 43 41 4E");
    node.payload.insert("measurementName", "GCAN_ECHO_PAYLOAD");
    const auto stringResult = runner.run(node, context);
    QCOMPARE(stringResult.outcome, NodeOutcome::Error);
    QCOMPARE(stringResult.measurements.size(), 1);
    QCOMPARE(stringResult.measurements.first().attributes.value("comparison").toString(),
             QString("equal"));
    QCOMPARE(stringResult.measurements.first().attributes.value("expected").toString(),
             QString("43 58 31 2D 47 43 41 4E"));
}

void CoreTests::limitStepFailsReferencedParsedValueOutsideRange()
{
    const auto json = R"json({
      "id": "limit-reference-fail",
      "name": "Limit Reference Fail",
      "groups": [{
        "id": "main",
        "kind": "main",
        "steps": [
          {
            "id": "001",
            "kind": "action",
            "parameters": { "outputs": { "decoded": { "voltage": 5.3 } } }
          },
          {
            "id": "002",
            "kind": "limit",
            "inputs": { "actual": "${step:001.outputs.decoded.voltage}" },
            "parameters": {
              "comparison": "between",
              "expected": 5.0,
              "tolerance": 0.2,
              "unit": "V",
              "measurementName": "PARSED_VOLTAGE"
            }
          }
        ]
      }]
    })json";

    SequenceCompiler compiler;
    const auto compile = compiler.compileJson(QJsonDocument::fromJson(json).object());
    QVERIFY2(compile.ok(), qPrintable(compile.errors.isEmpty() ? QString() : compile.errors.first().message));
    QCOMPARE(compile.plan.node("002")->kind, ExecNodeKind::Limit);

    ExecutionSession session(compile.plan);
    session.addUut("uut-1");
    const auto run = session.run();
    QVERIFY(run.completed);
    QVERIFY(run.hasError);

    const auto limited = session.results().latest("uut-1", "root", "002");
    QVERIFY(limited.has_value());
    QCOMPARE(limited->result.outcome, NodeOutcome::Failed);
    QCOMPARE(limited->result.errorCode, QString("LimitFailed"));
    QCOMPARE(limited->result.measurements.first().name, QString("PARSED_VOLTAGE"));
    QCOMPARE(limited->result.measurements.first().value.toDouble(), 5.3);
    QCOMPARE(limited->result.measurements.first().lowerLimit, 4.8);
    QCOMPARE(limited->result.measurements.first().upperLimit, 5.2);
}

void CoreTests::operatorPromptsConfirmAndCloseOnCompletedStep()
{
    const auto json = R"json({
      "id": "operator-prompt-flow",
      "name": "Operator Prompt Flow",
      "groups": [
        {
          "id": "setup",
          "kind": "setup",
          "steps": [{
            "id": "001",
            "kind": "operatorPrompt",
            "prompt": {
              "mode": "confirm",
              "title": "Connect fixture",
              "message": "Connect the fixture, then click Continue.",
              "image": "fixture_connection.png",
              "confirmText": "Continue",
              "timeoutMs": 1000
            }
          }]
        },
        {
          "id": "main",
          "kind": "main",
          "steps": [
            {
              "id": "002",
              "kind": "operatorPrompt",
              "prompt": {
                "mode": "notice",
                "message": "Press the product button.",
                "closeOnStep": "003",
                "timeoutMs": 1000
              }
            },
            { "id": "003", "kind": "wait", "name": "Button detected", "ms": 10 }
          ]
        },
        {
          "id": "cleanup",
          "kind": "cleanup",
          "steps": [{ "id": "004", "kind": "noop" }]
        }
      ]
    })json";

    SequenceCompiler compiler;
    const auto compiled = compiler.compileJson(QJsonDocument::fromJson(json).object());
    QVERIFY2(compiled.ok(), qPrintable(compiled.errors.isEmpty()
                                           ? QString()
                                           : compiled.errors.first().message));
    QCOMPARE(compiled.plan.node("001")->kind, ExecNodeKind::OperatorPrompt);
    QCOMPARE(compiled.plan.node("002")->kind, ExecNodeKind::OperatorPrompt);
    QCOMPARE(compiled.plan.node("001")->payload.value("image").toString(),
             QString("fixture_connection.png"));

    auto control = std::make_shared<ExecutionControl>();
    control->operatorPrompts().setResponderAvailable(true);
    OperatorPromptResponderSink events(control);
    ExecutionSession session(compiled.plan, {}, &events, control);
    session.addUut("uut-1");
    const auto run = session.run();
    QVERIFY(run.completed);
    QVERIFY(!run.hasError);

    int requested = 0;
    int closed = 0;
    bool conditionClosedNotice = false;
    bool imageForwarded = false;
    for (const auto& event : events.records()) {
        if (event.kind == RuntimeEventKind::OperatorPromptRequested) {
            ++requested;
            imageForwarded = imageForwarded ||
                (event.nodeId == "001" &&
                 event.details.value("image").toString() ==
                     QStringLiteral("fixture_connection.png"));
        } else if (event.kind == RuntimeEventKind::OperatorPromptClosed) {
            ++closed;
            conditionClosedNotice = conditionClosedNotice ||
                (event.nodeId == "002" &&
                 event.details.value("closedByStep").toString() == "003" &&
                 event.details.value("reason").toString() == "target-completed");
        }
    }
    QCOMPARE(requested, 2);
    QCOMPARE(closed, 2);
    QVERIFY(imageForwarded);
    QVERIFY(conditionClosedNotice);
}

void CoreTests::operatorPromptJudgmentMapsPassAndFail()
{
    const auto json = R"json({
      "id":"operator-judgment","name":"Operator Judgment","groups":[
        {"id":"main","kind":"main","steps":[
          {"id":"observe","kind":"operatorPrompt","prompt":{
            "mode":"notice","title":"RGB Lamp","message":"Observe the RGB lamp cycle.",
            "dialogKey":"rgb-lamp","timeoutMs":1000
          }},
          {"id":"exercise","kind":"wait","ms":1},
          {"id":"judge","kind":"operatorPrompt","prompt":{
            "mode":"judgment","title":"RGB Lamp Result",
            "message":"Did red, green, and blue all illuminate correctly?",
            "dialogKey":"rgb-lamp","passText":"PASS","failText":"FAIL",
            "failureCode":"RgbLampOperatorFail","timeoutMs":1000
          }}
        ]},
        {"id":"cleanup","kind":"cleanup","steps":[{"id":"close","kind":"noop"}]}
      ]
    })json";

    SequenceCompiler compiler;
    const auto compiled = compiler.compileJson(QJsonDocument::fromJson(json).object());
    QVERIFY2(compiled.ok(), qPrintable(compiled.errors.isEmpty()
        ? QString() : compiled.errors.first().message));

    const auto runWithResponse = [&](OperatorPromptResponse response) {
        auto control = std::make_shared<ExecutionControl>();
        control->operatorPrompts().setResponderAvailable(true);
        OperatorPromptResponderSink events(control, response);
        ExecutionSession session(compiled.plan, {}, &events, control);
        session.addUut("uut-1");
        const auto run = session.run();
        const auto judgment = session.results().latest("uut-1", "root", "judge");
        return std::tuple<ExecutionSessionResult, std::optional<StoredStepResult>,
                          QVector<RuntimeEvent>>{run, judgment, events.records()};
    };

    const auto [passRun, passJudgment, passEvents] =
        runWithResponse(OperatorPromptResponse::Passed);
    QVERIFY(passRun.completed);
    QVERIFY(!passRun.hasError);
    QVERIFY(passJudgment.has_value());
    QCOMPARE(passJudgment->result.outcome, NodeOutcome::Passed);
    QCOMPARE(passJudgment->result.outputs.value("response").toString(),
             QStringLiteral("pass"));
    QVERIFY(std::any_of(passEvents.cbegin(), passEvents.cend(),
                        [](const RuntimeEvent& event) {
                            return event.kind == RuntimeEventKind::OperatorPromptRequested &&
                                   event.nodeId == QStringLiteral("judge") &&
                                   event.details.value("dialogKey").toString() ==
                                       QStringLiteral("rgb-lamp");
                        }));

    const auto [failRun, failJudgment, failEvents] =
        runWithResponse(OperatorPromptResponse::Failed);
    Q_UNUSED(failEvents);
    QVERIFY(failRun.completed);
    QVERIFY(failRun.hasError);
    QVERIFY(failJudgment.has_value());
    QCOMPARE(failJudgment->result.outcome, NodeOutcome::Failed);
    QCOMPARE(failJudgment->result.errorCode, QStringLiteral("RgbLampOperatorFail"));
    QCOMPARE(failJudgment->result.outputs.value("response").toString(),
             QStringLiteral("fail"));
}

void CoreTests::operatorPromptInterpolatesRuntimeValues()
{
    const auto json = R"json({
      "id":"operator-prompt-values","name":"Operator Prompt Values","groups":[
        {"id":"setup","kind":"setup","steps":[
          {"id":"open","kind":"noop"}
        ]},
        {"id":"main","kind":"main","steps":[
          {"id":"001","name":"Read Voltage","kind":"action",
           "moduleId":"mock.action","parameters":{"outputs":{
             "voltage":812.5,"bytes":[1,2,3]
           }}},
          {"id":"002","name":"Show Values","kind":"operatorPrompt","prompt":{
            "mode":"confirm",
            "title":"Result for ${uut.id}",
            "message":"Voltage=${step:001.outputs.voltage} V; UUT=${uut.id}; Data=${step:001.outputs.bytes}",
            "confirmText":"OK","timeoutMs":1000
          }}
        ]},
        {"id":"cleanup","kind":"cleanup","steps":[
          {"id":"close","kind":"noop"}
        ]}
      ]
    })json";

    SequenceCompiler compiler;
    const auto compiled = compiler.compileJson(QJsonDocument::fromJson(json).object());
    QVERIFY2(compiled.ok(), qPrintable(compiled.errors.isEmpty()
                                           ? QString()
                                           : compiled.errors.first().message));

    auto control = std::make_shared<ExecutionControl>();
    control->operatorPrompts().setResponderAvailable(true);
    OperatorPromptResponderSink events(control);
    ExecutionSession session(compiled.plan, {}, &events, control);
    session.addUut(QStringLiteral("uut-1"));
    const auto run = session.run();
    QVERIFY(run.completed);
    QVERIFY(!run.hasError);

    const auto records = events.records();
    const auto prompt = std::find_if(
        records.cbegin(), records.cend(), [](const RuntimeEvent& event) {
            return event.kind == RuntimeEventKind::OperatorPromptRequested &&
                   event.nodeId == QStringLiteral("002");
        });
    QVERIFY(prompt != records.cend());
    QCOMPARE(prompt->details.value(QStringLiteral("title")).toString(),
             QStringLiteral("Result for uut-1"));
    QCOMPARE(prompt->details.value(QStringLiteral("message")).toString(),
             QStringLiteral("Voltage=812.5 V; UUT=uut-1; Data=[1,2,3]"));
}

void CoreTests::operatorPromptWaitsForFinalRetryAttemptBeforeClosing()
{
    const auto json = R"json({
      "id":"prompt-retry-target","name":"Prompt Retry Target","groups":[{
        "id":"main","kind":"main","steps":[
          {"id":"001","kind":"operatorPrompt","prompt":{
            "mode":"notice","message":"Operate the product","closeOnStep":"002"}},
          {"id":"002","kind":"action",
           "parameters":{"failUntilAttempt":99},
           "retry":{"maxAttempts":2},
           "errorPolicy":{"onFail":"Continue","stopUutOnFailure":false}},
          {"id":"003","kind":"noop"}
        ]
      }]
    })json";

    SequenceCompiler compiler;
    const auto compiled = compiler.compileJson(QJsonDocument::fromJson(json).object());
    QVERIFY2(compiled.ok(), qPrintable(compiled.errors.isEmpty()
                                           ? QString()
                                           : compiled.errors.first().message));

    auto control = std::make_shared<ExecutionControl>();
    control->operatorPrompts().setResponderAvailable(true);
    OperatorPromptResponderSink events(control);
    ExecutionSession session(compiled.plan, {}, &events, control);
    session.addUut("uut-1");
    const auto run = session.run();
    QVERIFY(run.completed);
    QVERIFY(run.hasError);
    const auto& target = session.uuts().first().activations.value("002");
    QCOMPARE(target.attempts.size(), 2);
    QCOMPARE(target.attempts[0].result.outcome, NodeOutcome::Failed);
    QCOMPARE(target.attempts[1].result.outcome, NodeOutcome::Failed);

    int finalAttemptCompletedIndex = -1;
    int promptClosedIndex = -1;
    const auto records = events.records();
    for (int index = 0; index < records.size(); ++index) {
        const auto& event = records[index];
        if (event.kind == RuntimeEventKind::AttemptCompleted &&
            event.nodeId == QStringLiteral("002") &&
            event.attemptIndex == 1) {
            finalAttemptCompletedIndex = index;
        }
        if (event.kind == RuntimeEventKind::OperatorPromptClosed &&
            event.nodeId == QStringLiteral("001") &&
            event.details.value("closedByStep").toString() == QStringLiteral("002")) {
            promptClosedIndex = index;
            QCOMPARE(event.details.value("reason").toString(),
                     QStringLiteral("target-completed"));
        }
    }
    QVERIFY(finalAttemptCompletedIndex >= 0);
    QVERIFY(promptClosedIndex > finalAttemptCompletedIndex);
}

void CoreTests::operatorPromptWithoutResponderFailsWithoutBlocking()
{
    const auto json = R"json({
      "id": "unavailable-prompt",
      "name": "Unavailable Prompt",
      "groups": [{
        "id": "main",
        "kind": "main",
        "steps": [{
          "id": "001",
          "kind": "operatorPrompt",
          "prompt": { "mode": "confirm", "message": "Click OK", "timeoutMs": 10000 }
        }]
      }]
    })json";
    SequenceCompiler compiler;
    const auto compiled = compiler.compileJson(QJsonDocument::fromJson(json).object());
    QVERIFY(compiled.ok());

    QElapsedTimer elapsed;
    elapsed.start();
    ExecutionSession session(compiled.plan);
    session.addUut("uut-1");
    const auto run = session.run();
    QVERIFY(run.completed);
    QVERIFY(run.hasError);
    QVERIFY2(elapsed.elapsed() < 500, "A missing UI responder must not block the scheduler");
    const auto result = session.results().latest("uut-1", "root", "001");
    QVERIFY(result.has_value());
    QCOMPARE(result->result.outcome, NodeOutcome::Error);
    QCOMPARE(result->result.errorCode, QString("OperatorPromptResponderUnavailable"));
}

void CoreTests::sequenceCompilerRejectsInvalidOperatorPrompt()
{
    const auto json = R"json({
      "id": "invalid-prompt",
      "name": "Invalid Prompt",
      "groups": [{
        "id": "main",
        "kind": "main",
        "steps": [{
          "id": "001",
          "kind": "operatorPrompt",
          "prompt": {
            "mode": "teleport",
            "message": "",
            "image": "instruction.gif",
            "timeoutMs": -1
          }
        }]
      }]
    })json";
    SequenceCompiler compiler;
    const auto compiled = compiler.compileJson(QJsonDocument::fromJson(json).object());
    QVERIFY(!compiled.ok());
    const auto paths = [&] {
        QStringList result;
        for (const auto& error : compiled.errors) result.push_back(error.path);
        return result;
    }();
    QVERIFY(paths.contains("groups[0].steps[0].prompt.mode"));
    QVERIFY(paths.contains("groups[0].steps[0].prompt.message"));
    QVERIFY(paths.contains("groups[0].steps[0].prompt.image"));
    QVERIFY(paths.contains("groups[0].steps[0].prompt.timeoutMs"));
}

void CoreTests::sequenceCompilerRejectsInvalidOperatorPromptCloseTarget()
{
    const auto compile = [](const QByteArray& json) {
        SequenceCompiler compiler;
        return compiler.compileJson(QJsonDocument::fromJson(json).object());
    };
    const auto hasMessage = [](const CompileResult& result, const QString& text) {
        return std::any_of(result.errors.cbegin(), result.errors.cend(),
                           [&](const CompileError& error) {
            return error.message.contains(text, Qt::CaseInsensitive);
        });
    };

    const auto missing = compile(R"json({
      "id":"missing-prompt-target","name":"Missing Prompt Target","groups":[{
        "id":"main","kind":"main","steps":[
          {"id":"001","kind":"operatorPrompt","prompt":{
            "mode":"notice","message":"Press button","closeOnStep":"999"}},
          {"id":"002","kind":"noop"}
        ]
      }]
    })json");
    QVERIFY(!missing.ok());
    QVERIFY(hasMessage(missing, QStringLiteral("missing or disabled node")));

    const auto earlier = compile(R"json({
      "id":"earlier-prompt-target","name":"Earlier Prompt Target","groups":[{
        "id":"main","kind":"main","steps":[
          {"id":"001","kind":"noop"},
          {"id":"002","kind":"operatorPrompt","prompt":{
            "mode":"notice","message":"Press button","closeOnStep":"001"}}
        ]
      }]
    })json");
    QVERIFY(!earlier.ok());
    QVERIFY(hasMessage(earlier, QStringLiteral("later node")));
}

void CoreTests::sequenceCompilerRunsCooperativePeriodicAction()
{
    const auto json = R"json({
      "id":"periodic-action","name":"Periodic Action","groups":[
        {"id":"setup","kind":"setup","steps":[{
          "id":"heartbeat","name":"Heartbeat","kind":"action",
          "moduleId":"test.periodic","function":"send",
          "inputs":{"deviceId":"DEVICE1","heartbeat":"${periodic.counter}"},
          "periodic":{"intervalMs":10,"runImmediately":true,
                      "counter":{"start":1,"increment":1,"wrapAt":3}}
        }]},
        {"id":"main","kind":"main","steps":[
          {"id":"wait-main","kind":"wait","ms":75}
        ]},
        {"id":"cleanup","kind":"cleanup","steps":[
          {"id":"cleanup-done","kind":"cleanup"}
        ]}
      ]
    })json";

    SequenceCompiler compiler;
    const auto compiled = compiler.compileJson(QJsonDocument::fromJson(json).object());
    QVERIFY2(compiled.ok(), compiled.errors.isEmpty()
                                ? "compile failed"
                                : qPrintable(compiled.errors.first().message));
    const auto* heartbeat = compiled.plan.node(QStringLiteral("heartbeat"));
    QVERIFY(heartbeat);
    QVERIFY(heartbeat->periodic.enabled);
    QCOMPARE(heartbeat->periodic.intervalMs, 10);
    QCOMPARE(heartbeat->periodic.counterStart, 1);
    QCOMPARE(heartbeat->periodic.counterIncrement, 1);
    QCOMPARE(heartbeat->periodic.counterWrapAt, 3);
    QCOMPARE(heartbeat->resources.size(), 1);
    QCOMPARE(heartbeat->resources.first().resourceId, QStringLiteral("DEVICE1"));

    auto module = std::make_shared<PeriodicRecordingModule>();
    module->clock.start();
    ExecutionSession session(compiled.plan);
    QVERIFY(session.registerModule(module));
    session.addUut(QStringLiteral("UUT-1"));
    const auto result = session.run();

    QVERIFY(result.completed);
    QVERIFY(module->requestIds.size() >= 4);
    QCOMPARE(module->heartbeatValues[0].toLongLong(), qint64(1));
    QCOMPARE(module->heartbeatValues[1].toLongLong(), qint64(2));
    QCOMPARE(module->heartbeatValues[2].toLongLong(), qint64(3));
    QCOMPARE(module->heartbeatValues[3].toLongLong(), qint64(1));
    QSet<RequestId> uniqueRequests(module->requestIds.cbegin(), module->requestIds.cend());
    QCOMPARE(uniqueRequests.size(), module->requestIds.size());
    const auto countAtCompletion = module->requestIds.size();
    QTest::qWait(40);
    QCOMPARE(module->requestIds.size(), countAtCompletion);
}

void CoreTests::periodicActionWaitsForResourceRegionAndStopsWithSession()
{
    const auto json = R"json({
      "id":"periodic-resource","name":"Periodic Resource","groups":[
        {"id":"setup","kind":"setup","steps":[{
          "id":"heartbeat","kind":"action","moduleId":"test.periodic","function":"send",
          "inputs":{"deviceId":"DEVICE1.CH1"},
          "periodic":{"intervalMs":30,"runImmediately":false}
        }]},
        {"id":"main","kind":"main","steps":[
          {"id":"locked-wait","kind":"wait","ms":90,
           "resourceRegionStart":{"id":"device-transaction","resources":[
             {"resourceId":"DEVICE1","mode":"exclusive"}
           ]},"resourceRegionEnd":"device-transaction"},
          {"id":"unlocked-wait","kind":"wait","ms":70}
        ]},
        {"id":"cleanup","kind":"cleanup","steps":[
          {"id":"cleanup-done","kind":"cleanup"}
        ]}
      ]
    })json";

    SequenceCompiler compiler;
    const auto compiled = compiler.compileJson(QJsonDocument::fromJson(json).object());
    QVERIFY2(compiled.ok(), compiled.errors.isEmpty()
                                ? "compile failed"
                                : qPrintable(compiled.errors.first().message));

    auto module = std::make_shared<PeriodicRecordingModule>();
    module->clock.start();
    ExecutionSession session(compiled.plan);
    QVERIFY(session.registerModule(module));
    session.addUut(QStringLiteral("UUT-1"));
    const auto result = session.run();

    QVERIFY(result.completed);
    QVERIFY(!module->callTimesMs.isEmpty());
    QVERIFY2(module->callTimesMs.first() >= 70,
             qPrintable(QStringLiteral("periodic action ran while resource region was locked at %1 ms")
                            .arg(module->callTimesMs.first())));
}

void CoreTests::periodicActionFailureMarksSessionButDoesNotStopMain()
{
    const auto json = R"json({
      "id":"periodic-failure","name":"Periodic Failure","groups":[
        {"id":"setup","kind":"setup","steps":[{
          "id":"heartbeat","kind":"action","moduleId":"test.periodic","function":"send",
          "inputs":{"deviceId":"DEVICE1"},
          "periodic":{"intervalMs":10,"runImmediately":true}
        }]},
        {"id":"main","kind":"main","steps":[
          {"id":"wait-main","kind":"wait","ms":55},
          {"id":"main-finished","kind":"noop"}
        ]},
        {"id":"cleanup","kind":"cleanup","steps":[
          {"id":"cleanup-done","kind":"cleanup"}
        ]}
      ]
    })json";

    SequenceCompiler compiler;
    const auto compiled = compiler.compileJson(QJsonDocument::fromJson(json).object());
    QVERIFY(compiled.ok());
    auto module = std::make_shared<PeriodicRecordingModule>();
    module->failFirst = true;
    module->clock.start();
    ExecutionSession session(compiled.plan);
    QVERIFY(session.registerModule(module));
    session.addUut(QStringLiteral("UUT-1"));

    const auto result = session.run();
    QVERIFY(result.completed);
    QVERIFY(result.hasError);
    QCOMPARE(result.state, ExecutionState::CompletedWithError);
    QVERIFY(module->requestIds.size() >= 2);
    const auto executionReport = session.report();
    QVERIFY(executionReport.sessionHasError);
    const auto* heartbeat = findStep(executionReport.sessionSteps,
                                     QStringLiteral("heartbeat"));
    QVERIFY(heartbeat);
    QVERIFY(heartbeat->wasError);
}

void CoreTests::sequenceCompilerRejectsUnsupportedPeriodicTaskShapes()
{
    const auto json = R"json({
      "id":"invalid-periodic","name":"Invalid Periodic","groups":[{
        "id":"main","kind":"main","steps":[{
          "id":"bad-heartbeat","kind":"action","moduleId":"test.periodic","function":"send",
          "inputs":{"deviceId":"${variables.deviceId}"},
          "retry":{"maxAttempts":2},
          "periodic":{"intervalMs":0,"runImmediately":true,
                      "counter":{"start":5,"increment":0,"wrapAt":3}}
        }]
      }]
    })json";

    SequenceCompiler compiler;
    const auto compiled = compiler.compileJson(QJsonDocument::fromJson(json).object());
    QVERIFY(!compiled.ok());
    const auto hasMessage = [&compiled](const QString& text) {
        return std::any_of(compiled.errors.cbegin(), compiled.errors.cend(),
                           [&text](const CompileError& error) {
            return error.message.contains(text, Qt::CaseInsensitive);
        });
    };
    QVERIFY(hasMessage(QStringLiteral("top-level Setup")));
    QVERIFY(hasMessage(QStringLiteral("interval")));
    QVERIFY(hasMessage(QStringLiteral("retry")));
    QVERIFY(hasMessage(QStringLiteral("exclusive resource")));
    QVERIFY(hasMessage(QStringLiteral("counter increment")));
    QVERIFY(hasMessage(QStringLiteral("wrapAt")));
}

QTEST_MAIN(CoreTests)
#include "CoreTests.moc"
