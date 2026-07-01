#include "PicoATE/Core/DeviceTransportSession.h"
#include "PicoATE/Core/ExecutionSession.h"
#include "PicoATE/Core/ModuleBindingRegistrar.h"
#include "PicoATE/Core/PersistentQProcessTransport.h"
#include "PicoATE/Core/SequenceCompiler.h"
#include "PicoATE/Core/StationRuntime.h"

#include <QCommandLineOption>
#include <QCommandLineParser>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QMetaType>
#include <QRegularExpression>
#include <QTextStream>

using namespace PicoATE::Core;

namespace {

QString executionStateName(ExecutionState state)
{
    switch (state) {
    case ExecutionState::Idle:
        return "Idle";
    case ExecutionState::Starting:
        return "Starting";
    case ExecutionState::Running:
        return "Running";
    case ExecutionState::Paused:
        return "Paused";
    case ExecutionState::Stopping:
        return "Stopping";
    case ExecutionState::CleaningUp:
        return "CleaningUp";
    case ExecutionState::Completed:
        return "Completed";
    case ExecutionState::CompletedWithError:
        return "CompletedWithError";
    case ExecutionState::Aborted:
        return "Aborted";
    }
    return "Unknown";
}

QString activationStateName(ActivationState state)
{
    switch (state) {
    case ActivationState::Created:
        return "Created";
    case ActivationState::WaitingForDependency:
        return "WaitingForDependency";
    case ActivationState::WaitingForResource:
        return "WaitingForResource";
    case ActivationState::WaitingAtBarrier:
        return "WaitingAtBarrier";
    case ActivationState::Ready:
        return "Ready";
    case ActivationState::Running:
        return "Running";
    case ActivationState::Passed:
        return "Passed";
    case ActivationState::Failed:
        return "Failed";
    case ActivationState::Error:
        return "Error";
    case ActivationState::Timeout:
        return "Timeout";
    case ActivationState::Cancelled:
        return "Cancelled";
    case ActivationState::Skipped:
        return "Skipped";
    }
    return "Unknown";
}

QString nodeKindName(ExecNodeKind kind)
{
    switch (kind) {
    case ExecNodeKind::Noop:
        return "Noop";
    case ExecNodeKind::Wait:
        return "Wait";
    case ExecNodeKind::Action:
        return "Action";
    case ExecNodeKind::Barrier:
        return "Barrier";
    case ExecNodeKind::Cleanup:
        return "Cleanup";
    case ExecNodeKind::Loop:
        return "Loop";
    case ExecNodeKind::TestItem:
        return "TestItem";
    case ExecNodeKind::Limit:
        return "Limit";
    case ExecNodeKind::Statement:
        return "Statement";
    case ExecNodeKind::SequenceCall:
        return "SequenceCall";
    }
    return "Unknown";
}

QString variantDisplay(const QVariant& value)
{
    if (!value.isValid() || value.isNull()) {
        return {};
    }
    if (value.metaType().id() == QMetaType::Double || value.metaType().id() == QMetaType::Float) {
        return QString::number(value.toDouble(), 'f', 6).remove(QRegularExpression("0+$")).remove(QRegularExpression("\\.$"));
    }
    return value.toString();
}

QString defaultExamplePath()
{
    const auto portablePath = QCoreApplication::applicationDirPath() + "/examples/simple_sequence.json";
    if (QFileInfo::exists(portablePath)) {
        return portablePath;
    }
    return QString::fromUtf8(PICOATE_SOURCE_DIR) + "/examples/simple_sequence.json";
}

bool readJsonObject(const QString& path, QJsonObject& object, QTextStream& err)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        err << "Failed to open sequence file: " << QFileInfo(path).absoluteFilePath()
            << "\n  " << file.errorString() << '\n';
        return false;
    }

    QJsonParseError parseError;
    const auto document = QJsonDocument::fromJson(file.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError) {
        err << "Failed to parse JSON: " << QFileInfo(path).absoluteFilePath()
            << "\n  offset " << parseError.offset << ": " << parseError.errorString() << '\n';
        return false;
    }

    if (!document.isObject()) {
        err << "Sequence JSON must be an object: " << QFileInfo(path).absoluteFilePath() << '\n';
        return false;
    }

    object = document.object();
    return true;
}

QHash<QString, QString> defaultRuntimeVariables()
{
    QHash<QString, QString> variables;
    const QDir applicationDirectory(QCoreApplication::applicationDirPath());
    const auto preferPortable = [&applicationDirectory](const QString& fileName,
                                                        const QString& buildPath) {
        const auto portablePath = applicationDirectory.absoluteFilePath(fileName);
        return QFileInfo::exists(portablePath) ? portablePath : buildPath;
    };
#ifdef PICOATE_MOCK_HOST_PATH
    variables.insert("PICOATE_MOCK_HOST",
                     preferPortable("PicoATE.MockHost.exe", QFileInfo(QString::fromUtf8(PICOATE_MOCK_HOST_PATH)).absoluteFilePath()));
#endif
#ifdef PICOATE_FAKE_INSTRUMENT_HOST_PATH
    variables.insert("PICOATE_FAKE_INSTRUMENT_HOST",
                     preferPortable("PicoATE.FakeInstrumentHost.exe", QFileInfo(QString::fromUtf8(PICOATE_FAKE_INSTRUMENT_HOST_PATH)).absoluteFilePath()));
#endif
#ifdef PICOATE_NATIVE_HOST_PATH
    variables.insert("PICOATE_NATIVE_HOST",
                     preferPortable("PicoATE.NativeHost.exe", QFileInfo(QString::fromUtf8(PICOATE_NATIVE_HOST_PATH)).absoluteFilePath()));
#endif
#ifdef PICOATE_TEST_DLL_PATH
    variables.insert("PICOATE_TEST_DLL",
                     preferPortable("PicoATE.TestDllModule.dll", QFileInfo(QString::fromUtf8(PICOATE_TEST_DLL_PATH)).absoluteFilePath()));
#endif
#ifdef PICOATE_CAN_DLL_PATH
    variables.insert("PICOATE_CAN_DLL",
                     preferPortable("PicoATE.CanExampleModule.dll", QFileInfo(QString::fromUtf8(PICOATE_CAN_DLL_PATH)).absoluteFilePath()));
#endif
#ifdef PICOATE_PYTHON_EXE
    variables.insert("PYTHON_EXE",
                     QFileInfo(QString::fromUtf8(PICOATE_PYTHON_EXE)).absoluteFilePath());
#endif
    return variables;
}

void registerFakeInstrumentDeviceFactories(DeviceSessionManager& devices)
{
    const auto host = defaultRuntimeVariables().value("PICOATE_FAKE_INSTRUMENT_HOST").trimmed();
    if (host.isEmpty()) {
        return;
    }

    auto transport = std::make_shared<PersistentQProcessTransport>(host);
    devices.registerFactory(std::make_shared<TransportDeviceSessionFactory>("fake.dmm", transport, 3000));
    devices.registerFactory(std::make_shared<TransportDeviceSessionFactory>("fake.can", transport, 3000));
    devices.registerFactory(std::make_shared<TransportDeviceSessionFactory>("fake.instrument", transport, 3000));
}

void printCompileErrors(const QVector<CompileError>& errors, QTextStream& err)
{
    err << "Compile failed with " << errors.size() << " error(s):\n";
    for (const auto& error : errors) {
        err << "  - " << (error.path.isEmpty() ? "<root>" : error.path)
            << ": " << error.message;
        if (!error.suggestion.isEmpty()) {
            err << " (" << error.suggestion << ')';
        }
        err << '\n';
    }
}

void printCompileWarnings(const QVector<CompileWarning>& warnings, QTextStream& err)
{
    err << "Compile warning(s):\n";
    for (const auto& warning : warnings) {
        err << "  - " << (warning.path.isEmpty() ? "<root>" : warning.path)
            << ": " << warning.message;
        if (!warning.suggestion.isEmpty()) {
            err << " (" << warning.suggestion << ')';
        }
        err << '\n';
    }
}

void printStationRuntimeErrors(const QVector<StationConfigDiagnostic>& errors, QTextStream& err)
{
    err << "Station config failed with " << errors.size() << " error(s):\n";
    for (const auto& error : errors) {
        err << "  - " << (error.path.isEmpty() ? "<root>" : error.path)
            << ": " << error.message;
        if (!error.suggestion.isEmpty()) {
            err << " (" << error.suggestion << ')';
        }
        err << '\n';
    }
}

void printModuleBindingErrors(const ModuleBindingRegistrationResult& result, QTextStream& err)
{
    err << "Module binding registration failed with " << result.errors.size() << " error(s):\n";
    for (const auto& error : result.errors) {
        err << "  - " << (error.moduleId.isEmpty() ? "<unknown module>" : error.moduleId)
            << ": " << error.message;
        if (!error.suggestion.isEmpty()) {
            err << " (" << error.suggestion << ')';
        }
        err << '\n';
    }
}

void printStationSummary(const StationRuntime& runtime, QTextStream& out)
{
    if (!runtime.hasStationConfig()) {
        return;
    }

    const auto& station = runtime.stationConfig();
    out << "Station: " << (station.name.isEmpty() ? QString("<unnamed>") : station.name);
    if (!station.stationId.isEmpty()) {
        out << " [" << station.stationId << ']';
    }
    out << '\n';
    out << "Devices: " << station.devices.size() << " configured\n";
    for (const auto& device : station.devices) {
        out << "  - " << device.deviceId
            << " [" << (device.deviceType.isEmpty() ? QString("<type>") : device.deviceType) << "] "
            << device.driverId
            << " lifetime=" << deviceSessionLifetimeName(device.lifetime);
        if (!device.address.isEmpty()) {
            out << " address=" << device.address;
        }
        out << '\n';
    }
}

void printPlanSummary(const CompileResult& compile, const QString& sequencePath, QTextStream& out)
{
    out << "================================================================================\n";
    out << " PicoATE Test Runner\n";
    out << "================================================================================\n";
    out << " Sequence : " << compile.sequence.name << " [" << compile.sequence.id << "]\n";
    out << " Version  : " << compile.sequence.version << '\n';
    out << " File     : " << QFileInfo(sequencePath).absoluteFilePath() << '\n';
    out << " Plan     : " << compile.plan.nodes.size() << " node(s), "
        << compile.plan.edges.size() << " edge(s), "
        << compile.plan.cleanupRegions.size() << " cleanup region(s)\n";
    out << "--------------------------------------------------------------------------------\n";
}

void printMeasurement(const MeasurementResult& measurement, QTextStream& out, int depth = 0)
{
    out << QString(10 + depth * 4, ' ') << "measurement ";
    out << (measurement.name.isEmpty() ? QString("<unnamed>") : measurement.name);
    if (measurement.value.isValid()) {
        out << " = " << variantDisplay(measurement.value);
        if (!measurement.unit.isEmpty()) {
            out << ' ' << measurement.unit;
        }
    }
    if (measurement.hasLowerLimit || measurement.hasUpperLimit) {
        out << " [";
        out << (measurement.hasLowerLimit ? QString::number(measurement.lowerLimit) : QString("-inf"));
        out << ", ";
        out << (measurement.hasUpperLimit ? QString::number(measurement.upperLimit) : QString("+inf"));
        out << ']';
    }
    out << ' ' << measurementStatusName(measurement.status);
    if (!measurement.errorCode.isEmpty()) {
        out << " (" << measurement.errorCode << ')';
    }
    if (!measurement.errorMessage.isEmpty()) {
        out << " - " << measurement.errorMessage;
    }
    out << '\n';
}

QString loopStepText(const StepLoopReport& loop)
{
    if (!loop.inLoop) {
        return {};
    }

    return QString("loop body: %1 %2..%3 step %4")
        .arg(loop.variableName.isEmpty() ? QString("<var>") : loop.variableName)
        .arg(loop.from)
        .arg(loop.to)
        .arg(loop.step);
}

QString loopIterationText(const LoopIterationContext& loop)
{
    if (!loop.active) {
        return {};
    }

    return QString("iteration %1: %2=%3")
        .arg(loop.iterationNumber)
        .arg(loop.variableName.isEmpty() ? QString("<var>") : loop.variableName)
        .arg(loop.value);
}

QString resultLabel(NodeOutcome outcome)
{
    switch (outcome) {
    case NodeOutcome::Passed: return "PASS";
    case NodeOutcome::Failed: return "FAIL";
    case NodeOutcome::Error: return "ERROR";
    case NodeOutcome::Timeout: return "TIMEOUT";
    case NodeOutcome::Cancelled: return "CANCEL";
    case NodeOutcome::Skipped: return "SKIP";
    case NodeOutcome::Unknown: return ".....";
    }
    return ".....";
}

class ConsoleRuntimeEventSink final : public IRuntimeEventSink
{
public:
    ConsoleRuntimeEventSink(const ExecutionPlan& plan, QTextStream& out)
        : m_plan(plan), m_out(out)
    {
    }

    void publish(const RuntimeEvent& event) override
    {
        switch (event.kind) {
        case RuntimeEventKind::SessionStateChanged:
            if (event.executionState == ExecutionState::Running) {
                m_out << " RUNNING\n";
            }
            break;
        case RuntimeEventKind::TestItemStarted:
            m_out << '\n' << QString(depthOf(event.nodeId) * 2, ' ')
                  << "> TEST ITEM  " << pathOf(event.nodeId) << '\n';
            break;
        case RuntimeEventKind::AttemptCompleted:
            printCompleted(event, "STEP");
            break;
        case RuntimeEventKind::TestItemCompleted:
            printCompleted(event, "ITEM");
            break;
        case RuntimeEventKind::LoopIterationStarted:
            m_out << QString(depthOf(event.nodeId) * 2, ' ')
                  << "  [LOOP] " << pathOf(event.nodeId) << " | "
                  << loopIterationText(event.loopIteration) << '\n';
            break;
        case RuntimeEventKind::LoopCompleted:
            printCompleted(event, "LOOP");
            break;
        case RuntimeEventKind::RetryScheduled:
            m_out << QString(depthOf(event.nodeId) * 2, ' ')
                  << "  [RETRY] " << event.uutId << " | " << pathOf(event.nodeId)
                  << " | " << event.message << '\n';
            break;
        case RuntimeEventKind::BarrierWaiting:
            m_out << "  [WAIT] " << event.uutId << " | " << pathOf(event.nodeId)
                  << " | barrier\n";
            break;
        case RuntimeEventKind::BarrierReleased:
            m_out << "  [SYNC] " << event.uutId << " | " << pathOf(event.nodeId)
                  << " | released\n";
            break;
        case RuntimeEventKind::NodeStateChanged:
            if ((event.outcome == NodeOutcome::Skipped ||
                 event.outcome == NodeOutcome::Cancelled) &&
                !m_terminalNodes.contains(event.uutId + ':' + event.nodeId)) {
                printCompleted(event, "STEP");
            }
            break;
        case RuntimeEventKind::DeviceStateChanged:
            m_out << "  [DEVICE] " << event.deviceId << " | " << event.message << '\n';
            break;
        default:
            break;
        }
        m_out.flush();
    }

private:
    int depthOf(const NodeId& nodeId) const
    {
        int depth = 0;
        auto current = m_plan.structuralParentOf(nodeId);
        while (current) {
            ++depth;
            current = m_plan.structuralParentOf(*current);
        }
        return depth;
    }

    QString pathOf(const NodeId& nodeId) const
    {
        QStringList parts;
        NodeId current = nodeId;
        while (!current.isEmpty()) {
            const auto* node = m_plan.node(current);
            parts.prepend(node && !node->displayName.isEmpty() ? node->displayName : current);
            const auto parent = m_plan.structuralParentOf(current);
            current = parent ? *parent : NodeId{};
        }
        return parts.join(" > ");
    }

    void printCompleted(const RuntimeEvent& event, const QString& category)
    {
        m_terminalNodes.insert(event.uutId + ':' + event.nodeId);
        m_out << QString(depthOf(event.nodeId) * 2, ' ')
              << "  [" << resultLabel(event.outcome).leftJustified(7, ' ') << "] "
              << event.uutId << " | " << category << " | " << pathOf(event.nodeId);
        if (event.attemptIndex > 0) {
            m_out << " | attempt " << event.attemptIndex;
        }
        const auto iteration = loopIterationText(event.loopIteration);
        if (!iteration.isEmpty()) {
            m_out << " | " << iteration;
        }
        if (!event.errorCode.isEmpty()) {
            m_out << " | " << event.errorCode;
        }
        if (!event.message.isEmpty()) {
            m_out << " | " << event.message;
        }
        m_out << '\n';
        for (const auto& measurement : event.measurements) {
            printMeasurement(measurement, m_out, depthOf(event.nodeId));
        }
    }

    const ExecutionPlan& m_plan;
    QTextStream& m_out;
    QSet<QString> m_terminalNodes;
};

struct ReportCounts {
    int passed = 0;
    int failed = 0;
    int skipped = 0;
    int total = 0;
    QStringList failures;
};

void collectReportCounts(const StepReport& step,
                         const QString& parentPath,
                         ReportCounts& counts)
{
    const auto name = step.displayName.isEmpty() ? step.stepId : step.displayName;
    const auto path = parentPath.isEmpty() ? name : parentPath + " > " + name;
    ++counts.total;
    if (step.outcome == NodeOutcome::Passed) {
        ++counts.passed;
    } else if (step.outcome == NodeOutcome::Skipped || step.outcome == NodeOutcome::Cancelled) {
        ++counts.skipped;
    } else if (step.wasError) {
        ++counts.failed;
        QString detail = path + " [" + nodeOutcomeName(step.outcome) + ']';
        if (!step.attempts.isEmpty()) {
            const auto& last = step.attempts.last();
            if (!last.errorCode.isEmpty()) {
                detail += " " + last.errorCode;
            }
            if (!last.errorMessage.isEmpty()) {
                detail += " - " + last.errorMessage;
            }
        }
        counts.failures.push_back(detail);
    }
    for (const auto& child : step.children) {
        collectReportCounts(child, path, counts);
    }
}

void printExecutionSummary(const ExecutionReport& report, QTextStream& out)
{
    out << "\n================================================================================\n";
    out << " FINAL RESULT: " << (report.hasError ? "FAILED" : "PASSED")
        << " | " << executionStateName(report.state) << '\n';
    out << "================================================================================\n";

    for (const auto& uut : report.uuts) {
        ReportCounts counts;
        for (const auto& step : uut.steps) {
            collectReportCounts(step, {}, counts);
        }
        out << ' ' << uut.uutId << "  " << (uut.hasError ? "FAILED" : "PASSED")
            << " | total " << counts.total
            << " | pass " << counts.passed
            << " | fail " << counts.failed
            << " | skip " << counts.skipped << '\n';
        for (const auto& failure : counts.failures) {
            out << "   ! " << failure << '\n';
        }
    }
    out << "================================================================================\n";
}

int runCommand(const QCommandLineParser& parser, const QStringList& positional, QTextStream& out, QTextStream& err)
{
    QString sequencePath = positional.isEmpty() ? defaultExamplePath() : positional.first();
    sequencePath = QFileInfo(sequencePath).absoluteFilePath();

    bool ok = false;
    const int uutCount = parser.value("uuts").toInt(&ok);
    if (!ok || uutCount <= 0) {
        err << "--uuts must be a positive integer.\n";
        return 2;
    }

    QJsonObject sequenceObject;
    if (!readJsonObject(sequencePath, sequenceObject, err)) {
        return 2;
    }

    SequenceCompiler compiler;
    auto compile = compiler.compileJson(sequenceObject);
    if (!compile.ok()) {
        printCompileErrors(compile.errors, err);
        return 2;
    }
    if (!compile.warnings.isEmpty()) {
        printCompileWarnings(compile.warnings, err);
    }

    printPlanSummary(compile, sequencePath, out);

    StationRuntime stationRuntime;
    const auto stationPath = parser.value("station").trimmed();
    if (!stationPath.isEmpty()) {
        VariableResolverOptions stationOptions;
        stationOptions.sequenceFilePath = QFileInfo(stationPath).absoluteFilePath();
        stationOptions.projectDir = QString::fromUtf8(PICOATE_SOURCE_DIR);
        stationOptions.variables = defaultRuntimeVariables();
        const auto stationResult = stationRuntime.loadStationConfigFile(stationPath, stationOptions);
        if (!stationResult.ok()) {
            printStationRuntimeErrors(stationResult.errors, err);
            return 2;
        }
        printStationSummary(stationRuntime, out);
    }

    ConsoleRuntimeEventSink consoleEvents(compile.plan, out);
    ExecutionSession session(compile.plan, {}, &consoleEvents);
    if (stationRuntime.hasStationConfig()) {
        registerFakeInstrumentDeviceFactories(session.devices());
        const auto configureErrors = configureDeviceSessions(stationRuntime.stationConfig(), session.devices());
        if (!configureErrors.isEmpty()) {
            printStationRuntimeErrors(configureErrors, err);
            return 2;
        }
    }

    const auto uutPrefix = parser.value("uut-prefix");
    for (int i = 1; i <= uutCount; ++i) {
        session.addUut(QString("%1-%2").arg(uutPrefix).arg(i));
    }

    ModuleBindingRegistrationOptions bindingOptions;
    bindingOptions.sequenceFilePath = sequencePath;
    bindingOptions.projectDir = QString::fromUtf8(PICOATE_SOURCE_DIR);
    bindingOptions.variables = defaultRuntimeVariables();

    const auto bindingResult = registerConfiguredModules(session, compile.sequence, bindingOptions);
    if (!bindingResult.ok()) {
        printModuleBindingErrors(bindingResult, err);
        return 2;
    }

    session.run();
    const auto report = session.report();
    printExecutionSummary(report, out);

    if (!report.completed) {
        return 4;
    }
    return report.hasError ? 3 : 0;
}

} // namespace

int main(int argc, char* argv[])
{
    QCoreApplication app(argc, argv);
    QCoreApplication::setApplicationName("PicoATE.Cli");
    QCoreApplication::setApplicationVersion("0.1.0");

    QTextStream out(stdout);
    QTextStream err(stderr);

    QCommandLineParser parser;
    parser.setApplicationDescription(
        "Compile and run a PicoATE sequence JSON file.\n\n"
        "Examples:\n"
        "  PicoATE.Cli run examples/simple_sequence.json\n"
        "  PicoATE.Cli run examples/basic_sequence.json --uuts 2\n"
        "  PicoATE.Cli --uuts 2");
    parser.addHelpOption();
    parser.addVersionOption();

    parser.addOption(QCommandLineOption(
        {"u", "uuts"},
        "Number of UUTs to create for this run.",
        "count",
        "1"));
    parser.addOption(QCommandLineOption(
        "uut-prefix",
        "Prefix used when generating UUT ids.",
        "prefix",
        "UUT"));
    parser.addOption(QCommandLineOption(
        "station",
        "Station config JSON file. Loads logical device configuration before the sequence runs.",
        "station.json"));
    parser.addPositionalArgument(
        "run",
        "Optional command name. If omitted, the first positional argument is treated as the sequence file.");
    parser.addPositionalArgument(
        "sequence.json",
        "Sequence JSON file. Defaults to examples/simple_sequence.json.");

    parser.process(app);

    auto positional = parser.positionalArguments();
    if (!positional.isEmpty() && positional.first().compare("run", Qt::CaseInsensitive) == 0) {
        positional.removeFirst();
    } else if (!positional.isEmpty() && positional.first().startsWith('-')) {
        err << "Unknown command: " << positional.first() << '\n';
        return 2;
    }

    if (positional.size() > 1) {
        err << "Too many positional arguments.\n";
        return 2;
    }

    return runCommand(parser, positional, out, err);
}
