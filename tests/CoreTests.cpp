#include <QtTest/QtTest>

#include "PicoATE/Core/BarrierController.h"
#include "PicoATE/Core/DeviceSessionManager.h"
#include "PicoATE/Core/DeviceTransportSession.h"
#include "PicoATE/Core/DllBridgeInvoker.h"
#include "PicoATE/Core/ExecutionGraphScheduler.h"
#include "PicoATE/Core/ExecutionSession.h"
#include "PicoATE/Core/InstrumentAdapterModules.h"
#include "PicoATE/Core/LoopController.h"
#include "PicoATE/Core/ModuleBindingRegistrar.h"
#include "PicoATE/Core/ModuleRuntime.h"
#include "PicoATE/Core/ModuleTransportJson.h"
#include "PicoATE/Core/NativeHostManifest.h"
#include "PicoATE/Core/PlanBuilder.h"
#include "PicoATE/Core/PlanCache.h"
#include "PicoATE/Core/PersistentQProcessTransport.h"
#include "PicoATE/Core/QProcessTransport.h"
#include "PicoATE/Core/ResourceManager.h"
#include "PicoATE/Core/RuntimeVariableResolver.h"
#include "PicoATE/Core/SequenceCompiler.h"
#include "PicoATE/Core/SequenceDef.h"
#include "PicoATE/Core/StationConfig.h"
#include "PicoATE/Core/StationRuntime.h"
#include "PicoATE/Core/VariableResolver.h"

#include <algorithm>
#include <chrono>
#include <memory>
#include <thread>
#include <utility>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonParseError>

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

    bool connect(QString& errorMessage) override
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

    void disconnect() override
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
    void stationRuntimeLoadsStationConfig();
    void resourceManagerSerializesWaiters();
    void barrierControllerReleasesOnlyThroughDecision();
    void planCacheKeepsRunningPlanAlive();
    void nodeRunnerRunsRegisteredModuleAndMapsModuleResult();
    void nodeRunnerReportsMissingModule();
    void moduleTransportJsonSerializesRequestAndResponse();
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
    void executionSessionRunsActionThroughQProcessTransport();
    void dllBridgeInvokerCallsTestDll();
    void dllBridgeInvokerReportsDllErrorCode();
    void dllBridgeInvokerReportsTimeout();
    void nativeHostManifestResolvesVariables();
    void nativeHostManifestReportsUnresolvedVariables();
    void qProcessTransportCallsNativeHostDll();
    void qProcessTransportCallsNativeHostDllManifest();
    void qProcessTransportKillsNativeHostOnDllTimeout();
    void qProcessTransportCallsSimulatedCanDllManifest();
    void qProcessTransportReportsSimulatedCanLimitFail();
    void schedulerRetriesAndRunsCleanup();
    void executionSessionReleasesBarrierAcrossUuts();
    void executionSessionDropsFailedUutBeforeBarrier();
    void executionSessionStopRunsCleanupOnly();
    void stopTokenEscalatesAtomically();
    void executionSessionConsumesCrossThreadStopToken();
    void sequenceDefModelsSetupMainCleanup();
    void sequenceDefDetectsDuplicateStepIds();
    void sequenceDefPreservesBarrierAndResourcePolicies();
    void errorPolicyDefMapsFailureActions();
    void errorPolicyEngineUsesOutcomeSpecificActions();
    void planBuilderBuildsSetupMainCleanupPlan();
    void planBuilderRejectsDuplicateStepIds();
    void planBuilderSkipsDisabledAndBridgesCustomGroups();
    void planBuilderBuildsLoopRegion();
    void planBuilderPlanRunsInExecutionSession();
    void sequenceCompilerCompilesJsonToExecutablePlan();
    void sequenceCompilerReportsUnsupportedStepKind();
    void sequenceCompilerReportsFieldTypeErrors();
    void sequenceCompilerReportsLoopErrors();
    void sequenceCompilerReportsUnknownFieldWarnings();
    void sequenceCompilerParsesModuleBindings();
    void sequenceCompilerReportsModuleBindingErrors();
    void sequenceCompilerRunsSimpleExampleFile();
    void sequenceCompilerRunsBasicExampleFile();
    void sequenceCompilerRunsCustomDisabledExampleFile();
    void sequenceCompilerRunsExternalEchoExampleFile();
    void sequenceCompilerRunsPythonEchoExampleFile();
    void sequenceCompilerRunsNativeHostDllExampleFile();
    void sequenceCompilerRunsSimulatedCanDllExampleFile();
    void sequenceCompilerRunsPersistentInstrumentExampleFile();
    void sequenceCompilerRunsDmmCanAdapterExampleFile();
    void sequenceCompilerRunsForLoopExampleFile();
    void sequenceCompilerRunsTestItemExampleFile();
    void testItemAggregatesFailureAfterRunningAllChildren();
    void testItemAggregatesErrorSeverity();
    void testItemStopSkipsChildrenAndRunsCleanup();
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
    options.variables.insert("DMM1_ADDRESS", "USB0::0x0957::0x0607::MY59001234::INSTR");

    const auto load = loadStationConfigFile(examplePath("stations/basic_station.json"), options);
    QVERIFY(load.ok());
    QCOMPARE(load.config.stationId, QString("bench-01"));
    QCOMPARE(load.config.devices.size(), 2);

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
    QVERIFY(hasErrorAt("devices[0].lifetime"));
    QVERIFY(hasErrorAt("devices[1].driverId"));
    QVERIFY(hasErrorAt("devices[1].deviceId"));
}

void CoreTests::stationRuntimeLoadsStationConfig()
{
    StationRuntime runtime;

    VariableResolverOptions options;
    options.sequenceFilePath = examplePath("stations/basic_station.json");
    options.projectDir = projectRootPath();
    options.variables.insert("DMM1_ADDRESS", "USB0::0x0957::0x0607::MY59001234::INSTR");

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

void CoreTests::moduleTransportJsonSerializesRequestAndResponse()
{
    ModuleTransportRequest request;
    request.traceId = "trace-1";
    request.moduleId = "external.dmm";
    request.functionName = "measureVoltage";
    request.context.uutId = "uut-1";
    request.context.frameId = "root";
    request.context.attemptId = "attempt-2";
    request.context.attemptIndex = 2;
    request.context.inputs.insert("range", "10V");
    request.context.parameters.insert("aperture", "NPLC1");
    request.context.variables.insert("station", "A");

    const auto requestJson = moduleTransportRequestToJson(request);
    QCOMPARE(requestJson.value("traceId").toString(), QString("trace-1"));
    QCOMPARE(requestJson.value("moduleId").toString(), QString("external.dmm"));
    QCOMPARE(requestJson.value("function").toString(), QString("measureVoltage"));

    const auto contextJson = requestJson.value("context").toObject();
    QCOMPARE(contextJson.value("uutId").toString(), QString("uut-1"));
    QCOMPARE(contextJson.value("attemptIndex").toInt(), 2);
    QCOMPARE(contextJson.value("inputs").toObject().value("range").toString(), QString("10V"));
    QCOMPARE(contextJson.value("parameters").toObject().value("aperture").toString(), QString("NPLC1"));
    QCOMPARE(contextJson.value("variables").toObject().value("station").toString(), QString("A"));

    ModuleTransportResponse response;
    response.outcome = ModuleOutcome::Failed;
    response.outputs.insert("actualVoltage", 4.999);
    response.measurements.push_back(makeMeasurement("VOUT", 4.999, "V", MeasurementStatus::Failed));
    response.errorCode = "LimitFail";
    response.errorMessage = "Voltage is out of range";

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
    ExecutionGraphScheduler scheduler(plan, resources, barriers, loops, errorPolicy, runner);

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

void CoreTests::executionSessionStopRunsCleanupOnly()
{
    ExecutionPlan plan;
    plan.id = "plan-stop";

    ExecNode action;
    action.id = "normal-action";
    action.displayName = "Normal Action";
    action.kind = ExecNodeKind::Action;
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
    QCOMPARE(uut.outcomeOf("power-off"), NodeOutcome::Passed);
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
        wait.payload.insert("ms", 100);
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
    std::thread runner([&session, &result] {
        result = session.run();
    });

    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    stopToken->requestStop();
    runner.join();

    QVERIFY(result.completed);
    QCOMPARE(result.state, ExecutionState::Completed);
    const auto& uut = session.uuts().first();
    QCOMPARE(uut.outcomeOf("wait-1"), NodeOutcome::Passed);
    QCOMPARE(uut.outcomeOf("wait-2"), NodeOutcome::Skipped);
    QCOMPARE(uut.outcomeOf("wait-3"), NodeOutcome::Skipped);
    QCOMPARE(uut.outcomeOf("cleanup"), NodeOutcome::Passed);
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
        return error.message.contains("Duplicate step id");
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
    QCOMPARE(region->bodyNodes, QVector<NodeId>{"measure-sample"});
    QCOMPARE(region->entryNodes, QVector<NodeId>{"measure-sample"});
    QCOMPARE(region->exitNodes, QVector<NodeId>{"measure-sample"});
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
    QCOMPARE(uut.outcomeOf("open-fixture"), NodeOutcome::Passed);
    QCOMPARE(uut.outcomeOf("measure"), NodeOutcome::Passed);
    QCOMPARE(uut.outcomeOf("power-off"), NodeOutcome::Passed);
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
                "type": "while",
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
    QCOMPARE(uut.outcomeOf("open-fixture"), NodeOutcome::Passed);
    QCOMPARE(uut.outcomeOf("wait-100ms"), NodeOutcome::Passed);
    QCOMPARE(uut.outcomeOf("measure"), NodeOutcome::Passed);
    QCOMPARE(uut.outcomeOf("power-off"), NodeOutcome::Passed);
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
        QCOMPARE(uut.outcomeOf("power-off"), NodeOutcome::Passed);
        QCOMPARE(uut.outcomeOf("close-fixture"), NodeOutcome::Passed);
        const auto& measureActivation = uut.activations.value("measure-voltage");
        QVERIFY(!measureActivation.attempts.isEmpty());
        const auto outputs = measureActivation.attempts.last().result.outputs;
        QCOMPARE(outputs.value("actualVoltage").toDouble(), 4.999);
        QCOMPARE(outputs.value("measurements").toMap().value("unit").toString(), QString("V"));
    }
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
    QCOMPARE(uut.steps.size(), 4);
    QCOMPARE(uut.steps[0].stepId, QString("open-fixture"));
    QCOMPARE(uut.steps[1].stepId, QString("measure"));
    QCOMPARE(uut.steps[2].stepId, QString("operator-confirm"));
    QCOMPARE(uut.steps[3].stepId, QString("power-off"));
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
    QVERIFY(!run.hasError);
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
    const auto nodeResult = [&](const NodeId& nodeId) -> const NodeResult* {
        const auto it = uut.activations.constFind(nodeId);
        if (it == uut.activations.constEnd() || it.value().attempts.isEmpty()) {
            return nullptr;
        }
        return &it.value().attempts.last().result;
    };

    const auto* open = nodeResult("open-dmm");
    const auto* read1 = nodeResult("read-dmm-1");
    const auto* read2 = nodeResult("read-dmm-2");
    const auto* statusNode = nodeResult("status-dmm");
    const auto* close = nodeResult("close-dmm");
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
    QCOMPARE(result.sequence.moduleBindings.size(), 0);

    ExecutionSession session(result.plan);
    session.addUut("uut-1");

    auto transport = std::make_shared<PersistentQProcessTransport>(host);
    registerFakeInstrumentDeviceFactories(session.devices(), transport);

    VariableResolverOptions stationOptions;
    stationOptions.sequenceFilePath = examplePath("stations/basic_station.json");
    stationOptions.projectDir = projectRootPath();
    stationOptions.variables.insert("DMM1_ADDRESS", "USB0::FAKE::INSTR");
    const auto station = loadStationConfigFile(examplePath("stations/basic_station.json"), stationOptions);
    QVERIFY(station.ok());

    const auto configureErrors = configureDeviceSessions(station.config, session.devices());
    QVERIFY(configureErrors.isEmpty());

    const auto run = session.run();
    QVERIFY(run.completed);
    QVERIFY(!run.hasError);
    QCOMPARE(run.state, ExecutionState::Completed);

    const auto& uut = session.uuts().first();
    const auto nodeResult = [&](const NodeId& nodeId) -> const NodeResult* {
        const auto it = uut.activations.constFind(nodeId);
        if (it == uut.activations.constEnd() || it.value().attempts.isEmpty()) {
            return nullptr;
        }
        return &it.value().attempts.last().result;
    };

    const auto* configureDmm = nodeResult("configure-dmm-dcv");
    const auto* readDmm = nodeResult("read-dmm");
    const auto* readCan = nodeResult("read-can-frame");
    const auto* disconnectDmm = nodeResult("disconnect-dmm");
    const auto* disconnectCan = nodeResult("disconnect-can");
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
    QCOMPARE(uut.outcomeOf("measure-sample"), NodeOutcome::Passed);
    QCOMPARE(uut.outcomeOf("after-loop"), NodeOutcome::Passed);
    QCOMPARE(uut.variables.value("sampleIndex").toInt(), 2);
    QCOMPARE(uut.variables.value("loop.index").toInt(), 2);
    QCOMPARE(uut.variables.value("loop.value").toInt(), 2);
    QCOMPARE(uut.activations.value("measure-sample").attempts.size(), 3);

    const auto& measureAttempts = uut.activations.value("measure-sample").attempts;
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

    const auto lastLoopOutputs = uut.activations.value("measure-sample").attempts.last().result.outputs;
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
    QCOMPARE(steps.size(), 5);
    QCOMPARE(steps[0].stepId, QString("open-fixture"));
    QCOMPARE(steps[1].stepId, QString("repeat-measurements"));
    QCOMPARE(steps[2].stepId, QString("measure-sample"));
    QCOMPARE(steps[3].stepId, QString("after-loop"));
    QCOMPARE(steps[4].stepId, QString("power-off"));

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
    QCOMPARE(uut.outcomeOf("power-off"), NodeOutcome::Passed);
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
    QCOMPARE(uut.outcomeOf("power-off"), NodeOutcome::Passed);
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
    QCOMPARE(uut.steps.size(), 2);
    QCOMPARE(uut.steps[0].stepId, QString("measure"));
    QCOMPARE(uut.steps[1].stepId, QString("power-off"));

    const auto* measure = findStep(uut, "measure");
    QVERIFY(measure != nullptr);
    QCOMPARE(measure->displayName, QString("Measure"));
    QCOMPARE(measure->kind, ExecNodeKind::Action);
    QCOMPARE(measure->state, ActivationState::Passed);
    QCOMPARE(measure->outcome, NodeOutcome::Passed);
    QVERIFY(!measure->wasError);
    QCOMPARE(measure->attempts.size(), 2);
    QCOMPARE(measure->attempts[0].index, 1);
    QCOMPARE(measure->attempts[0].outcome, NodeOutcome::Failed);
    QCOMPARE(measure->attempts[1].index, 2);
    QCOMPARE(measure->attempts[1].outcome, NodeOutcome::Passed);

    const auto* powerOff = findStep(uut, "power-off");
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

    const auto* cleanup = findStep(uut, "power-off");
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

void CoreTests::testItemAggregatesFailureAfterRunningAllChildren()
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
    QCOMPARE(parent->children[1].outcome, NodeOutcome::Passed);
    QCOMPARE(findStep(uut, "after-parent")->outcome, NodeOutcome::Skipped);
    QCOMPARE(findStep(uut, "cleanup-step")->outcome, NodeOutcome::Passed);
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
    QCOMPARE(findStep(uut, "cleanup")->outcome, NodeOutcome::Passed);
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
    QCOMPARE(parent->children[1].outcome, NodeOutcome::Passed);
}

QTEST_MAIN(CoreTests)
#include "CoreTests.moc"
