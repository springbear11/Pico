#include "PicoATE/Core/DeviceTransportSession.h"
#include "PicoATE/Core/ExecutionSession.h"
#include "PicoATE/Core/ModuleBindingRegistrar.h"
#include "PicoATE/Core/PersistentQProcessTransport.h"
#include "PicoATE/Core/SequenceCompiler.h"
#include "PicoATE/Core/StationRuntime.h"
#include "PicoATE/Core/StationRunPreparation.h"

#include <QCommandLineOption>
#include <QCommandLineParser>
#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QJsonValue>
#include <QMetaType>
#include <QSet>
#include <QTextStream>

#if defined(Q_OS_WIN)
#include <qt_windows.h>
#else
#include <cstdio>
#include <unistd.h>
#endif

using namespace PicoATE::Core;

namespace {

enum class ConsoleTone {
    Default,
    Dim,
    Info,
    Success,
    Warning,
    Error
};

bool enableAnsiColors()
{
#if defined(Q_OS_WIN)
    const auto output = GetStdHandle(STD_OUTPUT_HANDLE);
    if (output == INVALID_HANDLE_VALUE || output == nullptr) {
        return false;
    }
    DWORD mode = 0;
    if (!GetConsoleMode(output, &mode)) {
        return false;
    }
    return SetConsoleMode(output, mode | ENABLE_VIRTUAL_TERMINAL_PROCESSING);
#else
    return ::isatty(fileno(stdout)) != 0;
#endif
}

QString styledText(const QString& text, ConsoleTone tone, bool enabled)
{
    if (!enabled || tone == ConsoleTone::Default) {
        return text;
    }

    const char* code = "0";
    switch (tone) {
    case ConsoleTone::Dim: code = "90"; break;
    case ConsoleTone::Info: code = "36"; break;
    case ConsoleTone::Success: code = "32"; break;
    case ConsoleTone::Warning: code = "33"; break;
    case ConsoleTone::Error: code = "31"; break;
    default: break;
    }
    return QStringLiteral("\x1b[%1m%2\x1b[0m")
        .arg(QString::fromLatin1(code), text);
}

QString phaseName(ExecutionPhase phase)
{
    switch (phase) {
    case ExecutionPhase::Setup: return QStringLiteral("SETUP");
    case ExecutionPhase::Main: return QStringLiteral("MAIN");
    case ExecutionPhase::Cleanup: return QStringLiteral("CLEANUP");
    }
    return QStringLiteral("-");
}

QString compactDuration(qint64 milliseconds)
{
    if (milliseconds < 0) {
        return QStringLiteral("--:--.---");
    }
    return QStringLiteral("%1:%2.%3")
        .arg(milliseconds / 60000, 2, 10, QLatin1Char('0'))
        .arg(milliseconds / 1000 % 60, 2, 10, QLatin1Char('0'))
        .arg(milliseconds % 1000, 3, 10, QLatin1Char('0'));
}

QString shortDuration(qint64 milliseconds)
{
    if (milliseconds < 0) {
        return {};
    }
    if (milliseconds < 1000) {
        return QStringLiteral("%1 ms").arg(milliseconds);
    }
    return QStringLiteral("%1 s")
        .arg(QString::number(milliseconds / 1000.0, 'f', 3));
}

ConsoleTone toneForOutcome(NodeOutcome outcome)
{
    switch (outcome) {
    case NodeOutcome::Passed: return ConsoleTone::Success;
    case NodeOutcome::Failed:
    case NodeOutcome::Error:
    case NodeOutcome::Timeout:
        return ConsoleTone::Error;
    case NodeOutcome::Skipped:
    case NodeOutcome::Cancelled:
        return ConsoleTone::Warning;
    default:
        return ConsoleTone::Dim;
    }
}

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
    case ActivationState::WaitingForTimer:
        return "WaitingForTimer";
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
    case ExecNodeKind::Break:
        return "Break";
    case ExecNodeKind::Counter:
        return "Counter";
    case ExecNodeKind::Aggregate:
        return "Aggregate";
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
    if (value.metaType().id() == QMetaType::Double ||
        value.metaType().id() == QMetaType::Float) {
        return QString::number(value.toDouble(), 'g', 15);
    }
    if (value.metaType().id() == QMetaType::QByteArray) {
        return QString::fromLatin1(value.toByteArray().toHex(' ').toUpper());
    }

    const auto json = QJsonValue::fromVariant(value);
    if (json.isArray()) {
        return QString::fromUtf8(
            QJsonDocument(json.toArray()).toJson(QJsonDocument::Compact));
    }
    if (json.isObject()) {
        return QString::fromUtf8(
            QJsonDocument(json.toObject()).toJson(QJsonDocument::Compact));
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

void printStationRuntimeWarnings(const QVector<StationConfigDiagnostic>& warnings,
                                 QTextStream& out)
{
    for (const auto& warning : warnings) {
        out << "[WARNING] "
            << (warning.path.isEmpty() ? "<root>" : warning.path)
            << ": " << warning.message << '\n';
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
    out << " STATION\n";
    out << "   ID       : "
        << (station.stationId.isEmpty() ? QString("<unset>") : station.stationId)
        << '\n';
    out << "   Model    : "
        << (station.model.isEmpty() ? QString("<unset>") : station.model)
        << '\n';
    out << "   Customer : "
        << (station.customerId.isEmpty() ? QString("<unset>") : station.customerId)
        << '\n';
    out << "   Devices  : " << station.devices.size() << " configured\n";
    for (const auto& device : station.devices) {
        out << "     " << device.deviceId.leftJustified(14)
            << (device.deviceType.isEmpty() ? QString("<type>") : device.deviceType)
                   .leftJustified(10)
            << device.driverId
            << " | lifetime=" << deviceSessionLifetimeName(device.lifetime);
        if (!device.address.isEmpty()) {
            out << " | resource=" << device.address;
        }
        out << '\n';
    }
    out << "--------------------------------------------------------------------------------\n";
}

void printPlanSummary(const CompileResult& compile,
                      const QString& sequencePath,
                      const QString& stationPath,
                      int uutCount,
                      QTextStream& out)
{
    out << "================================================================================\n";
    out << " PICOATE CLI | LIVE TEST RUN\n";
    out << "================================================================================\n";
    out << " Sequence  : " << compile.sequence.name << " [" << compile.sequence.id << "]\n";
    out << " Version   : " << compile.sequence.version << '\n';
    out << " File      : " << QFileInfo(sequencePath).absoluteFilePath() << '\n';
    out << " Station   : "
        << (stationPath.trimmed().isEmpty()
                ? QStringLiteral("<none>")
                : QFileInfo(stationPath).absoluteFilePath())
        << '\n';
    out << " UUT Count : " << uutCount << '\n';
    out << " Started   : "
        << QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd HH:mm:ss.zzz"))
        << '\n';
    out << " Plan      : " << compile.plan.nodes.size() << " node(s), "
        << compile.plan.edges.size() << " edge(s), "
        << compile.plan.cleanupRegions.size() << " cleanup region(s)\n";
    out << "--------------------------------------------------------------------------------\n";
}

void printRuntimeHeader(QTextStream& out)
{
    out << " TIME         | UUT       | PHASE   | EVENT         | DETAILS\n";
    out << "--------------------------------------------------------------------------------\n";
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
    ConsoleRuntimeEventSink(const ExecutionPlan& plan,
                            QTextStream& out,
                            bool colorsEnabled,
                            bool verbose,
                            bool moduleLogsEnabled)
        : m_plan(plan)
        , m_out(out)
        , m_colorsEnabled(colorsEnabled)
        , m_verbose(verbose)
        , m_moduleLogsEnabled(moduleLogsEnabled)
    {
    }

    void publish(const RuntimeEvent& event) override
    {
        switch (event.kind) {
        case RuntimeEventKind::SessionStateChanged:
            printLine(event,
                      QStringLiteral("SESSION"),
                      executionStateName(event.executionState),
                      event.executionState == ExecutionState::Completed
                          ? ConsoleTone::Success
                          : event.executionState == ExecutionState::CompletedWithError ||
                                    event.executionState == ExecutionState::Aborted
                              ? ConsoleTone::Error
                              : ConsoleTone::Info);
            break;
        case RuntimeEventKind::UutRegistered:
            printLine(event, QStringLiteral("UUT READY"), QStringLiteral("registered"), ConsoleTone::Info);
            break;
        case RuntimeEventKind::UutCompleted:
            printLine(event,
                      QStringLiteral("UUT %1").arg(resultLabel(event.outcome)),
                      event.message.isEmpty() ? nodeOutcomeName(event.outcome) : event.message,
                      toneForOutcome(event.outcome));
            break;
        case RuntimeEventKind::TestItemStarted:
            printStarted(event, QStringLiteral("ITEM START"));
            break;
        case RuntimeEventKind::AttemptStarted:
            if (event.nodeKind != ExecNodeKind::TestItem) {
                printStarted(event,
                             event.nodeKind == ExecNodeKind::Loop
                                 ? QStringLiteral("LOOP START")
                                 : QStringLiteral("STEP START"));
            }
            break;
        case RuntimeEventKind::AttemptCompleted:
            if (event.nodeKind != ExecNodeKind::TestItem &&
                event.nodeKind != ExecNodeKind::Loop) {
                printCompleted(event, QStringLiteral("STEP"));
            }
            break;
        case RuntimeEventKind::TestItemCompleted:
            printCompleted(event, QStringLiteral("ITEM"));
            break;
        case RuntimeEventKind::LoopIterationStarted:
            printLine(event,
                      QStringLiteral("LOOP ITER"),
                      pathOf(event.nodeId) + QStringLiteral(" | ") +
                          loopIterationText(event.loopIteration),
                      ConsoleTone::Info);
            break;
        case RuntimeEventKind::LoopCompleted:
            printCompleted(event, QStringLiteral("LOOP"));
            break;
        case RuntimeEventKind::RetryScheduled:
            printLine(event,
                      QStringLiteral("RETRY"),
                      pathOf(event.nodeId) + QStringLiteral(" | ") +
                          attemptText(event) +
                          (event.message.isEmpty()
                               ? QString{}
                               : QStringLiteral(" | ") + oneLine(event.message)),
                      ConsoleTone::Warning);
            break;
        case RuntimeEventKind::BarrierWaiting:
            printLine(event,
                      QStringLiteral("BARRIER WAIT"),
                      pathOf(event.nodeId) +
                          (event.message.isEmpty()
                               ? QString{}
                               : QStringLiteral(" | ") + oneLine(event.message)),
                      ConsoleTone::Warning);
            break;
        case RuntimeEventKind::BarrierReleased:
            printLine(event,
                      QStringLiteral("BARRIER OPEN"),
                      pathOf(event.nodeId) + QStringLiteral(" | released"),
                      ConsoleTone::Success);
            break;
        case RuntimeEventKind::NodeStateChanged:
            if ((event.outcome == NodeOutcome::Skipped ||
                 event.outcome == NodeOutcome::Cancelled) &&
                !m_terminalNodes.contains(terminalKey(event))) {
                printCompleted(event,
                               event.nodeKind == ExecNodeKind::TestItem
                                   ? QStringLiteral("ITEM")
                                   : QStringLiteral("STEP"));
            }
            break;
        case RuntimeEventKind::CleanupActivated:
            printLine(event,
                      QStringLiteral("CLEANUP"),
                      pathOf(event.nodeId) +
                          (event.message.isEmpty()
                               ? QString{}
                               : QStringLiteral(" | ") + oneLine(event.message)),
                      ConsoleTone::Warning);
            break;
        case RuntimeEventKind::DeviceStateChanged:
            printLine(event,
                      QStringLiteral("DEVICE"),
                      event.deviceId + QStringLiteral(" | ") +
                          deviceConnectionStateName(event.deviceState) +
                          (event.message.isEmpty()
                               ? QString{}
                               : QStringLiteral(" | ") + oneLine(event.message)),
                      event.deviceState == DeviceConnectionState::Error
                          ? ConsoleTone::Error
                          : event.deviceState == DeviceConnectionState::Connected
                              ? ConsoleTone::Success
                              : ConsoleTone::Info);
            break;
        case RuntimeEventKind::ModuleLog:
            if (m_moduleLogsEnabled) {
                printLine(event,
                          QStringLiteral("LOG"),
                          pathOf(event.nodeId) +
                              (event.attemptIndex > 0
                                   ? QStringLiteral(" | attempt %1").arg(event.attemptIndex)
                                   : QString{}) +
                              QStringLiteral(" | ") + oneLine(event.message),
                          ConsoleTone::Default);
            }
            break;
        case RuntimeEventKind::OperatorPromptRequested:
            printLine(event,
                      QStringLiteral("PROMPT OPEN"),
                      pathOf(event.nodeId) + QStringLiteral(" | ") + oneLine(event.message),
                      ConsoleTone::Warning);
            break;
        case RuntimeEventKind::OperatorPromptClosed:
            printLine(event,
                      QStringLiteral("PROMPT CLOSE"),
                      pathOf(event.nodeId) +
                          (event.message.isEmpty()
                               ? QString{}
                               : QStringLiteral(" | ") + oneLine(event.message)),
                      toneForOutcome(event.outcome));
            break;
        case RuntimeEventKind::BreakpointHit:
            printLine(event,
                      QStringLiteral("BREAKPOINT"),
                      pathOf(event.nodeId),
                      ConsoleTone::Warning);
            break;
        case RuntimeEventKind::DebugStepCompleted:
            printLine(event,
                      QStringLiteral("DEBUG STEP"),
                      pathOf(event.nodeId) +
                          (event.message.isEmpty()
                               ? QString{}
                               : QStringLiteral(" | ") + oneLine(event.message)),
                      ConsoleTone::Info);
            break;
        default:
            break;
        }
        m_out.flush();
    }

private:
    static QString oneLine(QString value)
    {
        value.replace('\r', ' ');
        value.replace('\n', ' ');
        value.replace('\t', ' ');
        return value.simplified();
    }

    QString pathOf(const NodeId& nodeId) const
    {
        if (nodeId.isEmpty()) {
            return QStringLiteral("session");
        }
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

    QString terminalKey(const RuntimeEvent& event) const
    {
        return event.uutId + QLatin1Char(':') + event.nodeId +
               QLatin1Char(':') + event.frameId;
    }

    QString attemptText(const RuntimeEvent& event) const
    {
        const int attempt = event.details.value(
            QStringLiteral("retryAttemptIndex"), event.attemptIndex).toInt();
        const int maximum = event.details.value(QStringLiteral("maxAttempts"), 1).toInt();
        QString text;
        if (attempt > 0) {
            text = QStringLiteral("attempt %1/%2").arg(attempt).arg(qMax(1, maximum));
        }
        if (event.details.value(QStringLiteral("periodicInvocation")).toBool()) {
            const int periodicIndex = event.details.value(QStringLiteral("periodicIndex")).toInt();
            text += (text.isEmpty() ? QString{} : QStringLiteral(" | ")) +
                    QStringLiteral("periodic #%1").arg(periodicIndex);
        }
        const auto iteration = loopIterationText(event.loopIteration);
        if (!iteration.isEmpty()) {
            text += (text.isEmpty() ? QString{} : QStringLiteral(" | ")) + iteration;
        }
        return text;
    }

    QString withVerbose(const RuntimeEvent& event, QString details) const
    {
        if (!m_verbose) {
            return details;
        }
        QStringList fields;
        fields.push_back(QStringLiteral("seq=%1").arg(event.sequenceNumber));
        if (!event.nodeId.isEmpty()) {
            fields.push_back(QStringLiteral("node=%1").arg(event.nodeId));
        }
        if (!event.requestId.isEmpty()) {
            fields.push_back(QStringLiteral("request=%1").arg(event.requestId));
        }
        if (!event.frameId.isEmpty()) {
            fields.push_back(QStringLiteral("frame=%1").arg(event.frameId));
        }
        if (!fields.isEmpty()) {
            details += (details.isEmpty() ? QString{} : QStringLiteral(" | ")) +
                       QStringLiteral("[") + fields.join(QLatin1Char(' ')) +
                       QStringLiteral("]");
        }
        return details;
    }

    void printLine(const RuntimeEvent& event,
                   const QString& category,
                   const QString& details,
                   ConsoleTone tone)
    {
        const auto timestamp = (event.timestampUtc.isValid()
                                    ? event.timestampUtc.toLocalTime()
                                    : QDateTime::currentDateTime())
                                   .toString(QStringLiteral("HH:mm:ss.zzz"));
        const auto uut = event.uutId.isEmpty() ? QStringLiteral("SESSION") : event.uutId;
        const auto phase = event.nodeId.isEmpty() ? QStringLiteral("-") : phaseName(event.nodePhase);
        const auto eventName = styledText(category.leftJustified(13), tone, m_colorsEnabled);
        m_out << ' ' << timestamp.leftJustified(12) << " | "
              << uut.left(9).leftJustified(9) << " | "
              << phase.leftJustified(7) << " | "
              << eventName << " | "
              << withVerbose(event, details) << '\n';
    }

    void printStarted(const RuntimeEvent& event, const QString& category)
    {
        QString details = pathOf(event.nodeId);
        const auto attempt = attemptText(event);
        if (!attempt.isEmpty()) {
            details += QStringLiteral(" | ") + attempt;
        }
        printLine(event, category, details, ConsoleTone::Info);
    }

    void printCompleted(const RuntimeEvent& event, const QString& category)
    {
        m_terminalNodes.insert(terminalKey(event));
        QString details = pathOf(event.nodeId);
        const auto attempt = attemptText(event);
        if (!attempt.isEmpty()) {
            details += QStringLiteral(" | ") + attempt;
        }
        const qint64 durationMs = event.details.value(QStringLiteral("durationMs"), -1).toLongLong();
        if (durationMs >= 0) {
            details += QStringLiteral(" | ") + shortDuration(durationMs);
        }
        if (!event.errorCode.isEmpty()) {
            details += QStringLiteral(" | error=") + event.errorCode;
        }
        if (!event.message.isEmpty()) {
            details += QStringLiteral(" | ") + oneLine(event.message);
        }
        printLine(event,
                  category + QLatin1Char(' ') + resultLabel(event.outcome),
                  details,
                  toneForOutcome(event.outcome));
        for (const auto& measurement : event.measurements) {
            printMeasurement(event, measurement);
        }
    }

    void printMeasurement(const RuntimeEvent& event,
                          const MeasurementResult& measurement)
    {
        QString details = measurement.name.isEmpty()
            ? QStringLiteral("measurement")
            : measurement.name;
        if (measurement.rawValue.isValid()) {
            details += QStringLiteral(" | raw=") + variantDisplay(measurement.rawValue);
        }
        details += QStringLiteral(" | actual=") + variantDisplay(measurement.value);
        if (!measurement.unit.isEmpty()) {
            details += QLatin1Char(' ') + measurement.unit;
        }
        if (measurement.hasLowerLimit || measurement.hasUpperLimit) {
            const auto lower = measurement.hasLowerLimit
                ? QString::number(measurement.lowerLimit, 'g', 15)
                : QStringLiteral("-inf");
            const auto upper = measurement.hasUpperLimit
                ? QString::number(measurement.upperLimit, 'g', 15)
                : QStringLiteral("+inf");
            details += QStringLiteral(" | limits=[%1, %2]").arg(lower, upper);
        }
        details += QStringLiteral(" | status=") + measurementStatusName(measurement.status);
        if (!measurement.errorCode.isEmpty()) {
            details += QStringLiteral(" | error=") + measurement.errorCode;
        }
        if (!measurement.errorMessage.isEmpty()) {
            details += QStringLiteral(" | ") + oneLine(measurement.errorMessage);
        }

        ConsoleTone tone = ConsoleTone::Dim;
        if (measurement.status == MeasurementStatus::Passed) {
            tone = ConsoleTone::Success;
        } else if (measurement.status == MeasurementStatus::Failed ||
                   measurement.status == MeasurementStatus::Error) {
            tone = ConsoleTone::Error;
        } else if (measurement.status == MeasurementStatus::Skipped) {
            tone = ConsoleTone::Warning;
        }
        printLine(event, QStringLiteral("MEASUREMENT"), details, tone);
    }

    const ExecutionPlan& m_plan;
    QTextStream& m_out;
    QSet<QString> m_terminalNodes;
    bool m_colorsEnabled = false;
    bool m_verbose = false;
    bool m_moduleLogsEnabled = true;
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

void printExecutionSummary(const ExecutionReport& report,
                           QTextStream& out,
                           bool colorsEnabled,
                           qint64 observedDurationMs,
                           const QDateTime& observedFinishedAt)
{
    const auto finalLabel = report.hasError ? QStringLiteral("FAILED") : QStringLiteral("PASSED");
    const auto finalTone = report.hasError ? ConsoleTone::Error : ConsoleTone::Success;
    out << "\n================================================================================\n";
    out << " FINAL RESULT : " << styledText(finalLabel, finalTone, colorsEnabled)
        << " | " << executionStateName(report.state) << '\n';
    const qint64 durationMs = report.metadata.durationMs >= 0
        ? report.metadata.durationMs
        : observedDurationMs;
    const auto finishedAt = report.metadata.finishedAt.isValid()
        ? report.metadata.finishedAt
        : observedFinishedAt;
    out << " DURATION     : " << compactDuration(durationMs) << '\n';
    if (finishedAt.isValid()) {
        out << " FINISHED     : "
            << finishedAt.toLocalTime().toString(QStringLiteral("yyyy-MM-dd HH:mm:ss.zzz"))
            << '\n';
    }
    out << "================================================================================\n";

    if (!report.sessionSteps.isEmpty()) {
        ReportCounts counts;
        for (const auto& step : report.sessionSteps) {
            collectReportCounts(step, {}, counts);
        }
        out << " SESSION  "
            << styledText(report.sessionHasError ? QStringLiteral("FAILED")
                                                 : QStringLiteral("PASSED"),
                          report.sessionHasError ? ConsoleTone::Error : ConsoleTone::Success,
                          colorsEnabled)
            << " | total " << counts.total
            << " | pass " << counts.passed
            << " | fail " << counts.failed
            << " | skip " << counts.skipped << '\n';
        for (const auto& failure : counts.failures) {
            out << "   ! " << failure << '\n';
        }
    }

    for (const auto& uut : report.uuts) {
        ReportCounts counts;
        for (const auto& step : uut.steps) {
            collectReportCounts(step, {}, counts);
        }
        const bool uutFailed = uut.hasError || uut.outcome == NodeOutcome::Cancelled;
        out << ' ' << uut.uutId << "  "
            << styledText(uutFailed ? QStringLiteral("FAILED") : QStringLiteral("PASSED"),
                          uutFailed ? ConsoleTone::Error : ConsoleTone::Success,
                          colorsEnabled)
            << " | total " << counts.total
            << " | pass " << counts.passed
            << " | fail " << counts.failed
            << " | skip " << counts.skipped
            << " | duration " << compactDuration(uut.durationMs) << '\n';
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
    const auto stationPath = parser.value("station").trimmed();
    const bool colorsEnabled = !parser.isSet("no-color") && enableAnsiColors();
    const bool verbose = parser.isSet("verbose");
    const bool moduleLogsEnabled = !parser.isSet("no-module-logs");

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

    printPlanSummary(compile, sequencePath, stationPath, uutCount, out);

    StationRuntime stationRuntime;
    if (!stationPath.isEmpty()) {
        QJsonObject stationObject;
        if (!readJsonObject(stationPath, stationObject, err)) {
            return 2;
        }
        const auto variables = defaultRuntimeVariables();
        StationRunPreparationOptions preparationOptions;
        preparationOptions.stationFilePath = QFileInfo(stationPath).absoluteFilePath();
        preparationOptions.projectDir = QString::fromUtf8(PICOATE_SOURCE_DIR);
        preparationOptions.nativeHostProgram =
            variables.value(QStringLiteral("PICOATE_NATIVE_HOST"));
        preparationOptions.variables = variables;
        const auto preparation = StationRunPreparationService().prepare(
            stationObject, preparationOptions);
        if (!preparation.ok()) {
            printStationRuntimeErrors(preparation.errors, err);
            return 2;
        }
        printStationRuntimeWarnings(preparation.warnings, out);
        const auto stationResult = stationRuntime.applyStationConfig(
            preparation.stationConfig);
        if (!stationResult.ok()) {
            printStationRuntimeErrors(stationResult.errors, err);
            return 2;
        }
        printStationSummary(stationRuntime, out);
    }

    const auto failureHandling = stationRuntime.hasStationConfig()
        ? failureHandlingMode(stationRuntime.stationConfig())
        : FailureHandlingMode::UseNodePolicy;
    printRuntimeHeader(out);
    ConsoleRuntimeEventSink consoleEvents(
        compile.plan, out, colorsEnabled, verbose, moduleLogsEnabled);
    ExecutionSession session(compile.plan, {}, &consoleEvents, {}, failureHandling);
    if (stationRuntime.hasStationConfig()) {
        registerFakeInstrumentDeviceFactories(session.devices());
        const auto configureErrors = configureDeviceSessions(stationRuntime.stationConfig(), session.devices());
        if (!configureErrors.isEmpty()) {
            printStationRuntimeErrors(configureErrors, err);
            return 2;
        }
    }

    const auto uutPrefix = parser.value("uut-prefix");
    for (int index = 0; index < uutCount; ++index) {
        const auto uutId = QString("%1-%2").arg(uutPrefix).arg(index + 1);
        const auto binding = bindSequenceVariablesForUut(
            compile.plan.variables, index, uutId);
        if (!binding.ok()) {
            for (const auto& diagnostic : binding.errors) {
                err << "variables."
                    << (diagnostic.variableName.isEmpty()
                            ? QStringLiteral("<unknown>")
                            : diagnostic.variableName)
                    << ": " << diagnostic.message << '\n';
            }
            return 2;
        }
        auto& uut = session.addUut(uutId);
        uut.variables = binding.variables;
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

    if (stationRuntime.hasStationConfig()) {
        const auto variables = defaultRuntimeVariables();
        StationPluginRegistrationOptions stationPluginOptions;
        stationPluginOptions.stationFilePath = QFileInfo(stationPath).absoluteFilePath();
        stationPluginOptions.projectDir = QString::fromUtf8(PICOATE_SOURCE_DIR);
        stationPluginOptions.nativeHostProgram =
            variables.value(QStringLiteral("PICOATE_NATIVE_HOST"));
        stationPluginOptions.variables = variables;
        const auto stationBindings = registerStationPluginModules(
            session,
            stationRuntime.stationConfig(),
            stationPluginOptions);
        if (!stationBindings.ok()) {
            printModuleBindingErrors(stationBindings, err);
            return 2;
        }
    }

    QElapsedTimer runTimer;
    runTimer.start();
    session.run();
    const auto observedFinishedAt = QDateTime::currentDateTime();
    const auto observedDurationMs = runTimer.elapsed();
    const auto report = session.report();
    printExecutionSummary(
        report, out, colorsEnabled, observedDurationMs, observedFinishedAt);

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
    parser.addOption(QCommandLineOption(
        "verbose",
        "Include event sequence, node, request, and frame identifiers."));
    parser.addOption(QCommandLineOption(
        "no-color",
        "Disable ANSI colors even when stdout is an interactive console."));
    parser.addOption(QCommandLineOption(
        "no-module-logs",
        "Hide live module log records while keeping control and result events."));
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
