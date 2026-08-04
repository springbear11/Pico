#pragma once

#include "PicoATE/Core/ExecutionReport.h"

#include <QByteArray>
#include <QMetaType>
#include <QString>
#include <QVariantMap>
#include <QVector>

namespace PicoATE::Ui {

enum class UiRunState {
    Empty,
    SourceSelected,
    Compiling,
    CompileFailed,
    Ready,
    TestingDevice,
    Starting,
    Running,
    Pausing,
    Paused,
    Stopping,
    Completed,
    Failed
};

enum class DeviceConnectionTestOutcome {
    Passed,
    Failed,
    TimedOut,
    Cancelled
};

enum class UiDiagnosticSeverity {
    Error,
    Warning
};

struct UiDiagnostic {
    UiDiagnosticSeverity severity = UiDiagnosticSeverity::Error;
    QString path;
    QString message;
    QString suggestion;

    friend bool operator==(const UiDiagnostic&, const UiDiagnostic&) = default;
};

struct CompileRequest {
    quint64 requestId = 0;
    QString sequencePath;
    QByteArray sequenceJson;
    QString stationPath;
    QByteArray stationJson;
};

struct CompileServiceResult {
    quint64 requestId = 0;
    bool success = false;
    QString sequenceId;
    QString sequenceName;
    QString sequenceVersion;
    int nodeCount = 0;
    int loopTestCount = 1;
    PicoATE::Core::ExecutionReport previewReport;
    QVector<UiDiagnostic> diagnostics;
};

struct RunRequest {
    struct UutInput {
        PicoATE::Core::UutId uutId;
        QVariantMap variables;
    };

    quint64 requestId = 0;
    int uutCount = 1;
    QString uutPrefix = QStringLiteral("UUT");
    QVector<UutInput> uuts;
};

struct RunServiceResult {
    quint64 requestId = 0;
    bool executed = false;
    bool stopRequested = false;
    PicoATE::Core::ExecutionReport report;
    QVector<UiDiagnostic> diagnostics;
};

struct DeviceConnectionTestRequest {
    quint64 requestId = 0;
    QString sequencePath;
    QByteArray sequenceJson;
    QString stationPath;
    QByteArray stationJson;
    QString deviceId;
    int timeoutMs = 5000;
};

struct DeviceConnectionTestResult {
    quint64 requestId = 0;
    QString deviceId;
    DeviceConnectionTestOutcome outcome = DeviceConnectionTestOutcome::Failed;
    QString errorCode;
    QString errorMessage;
    QString suggestion;
    QVariantMap metadata;
    qint64 elapsedMs = 0;

    bool passed() const { return outcome == DeviceConnectionTestOutcome::Passed; }
};

QString uiRunStateName(UiRunState state);
QString deviceConnectionTestOutcomeName(DeviceConnectionTestOutcome outcome);

} // namespace PicoATE::Ui

Q_DECLARE_METATYPE(PicoATE::Ui::UiRunState)
Q_DECLARE_METATYPE(PicoATE::Ui::UiDiagnostic)
Q_DECLARE_METATYPE(PicoATE::Ui::CompileServiceResult)
Q_DECLARE_METATYPE(PicoATE::Ui::RunServiceResult)
Q_DECLARE_METATYPE(PicoATE::Ui::DeviceConnectionTestOutcome)
Q_DECLARE_METATYPE(PicoATE::Ui::DeviceConnectionTestResult)
