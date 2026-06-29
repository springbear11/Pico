#pragma once

#include "PicoATE/Core/ExecutionReport.h"

#include <QMetaType>
#include <QString>
#include <QVector>

namespace PicoATE::Ui {

enum class UiRunState {
    Empty,
    SourceSelected,
    Compiling,
    CompileFailed,
    Ready,
    Starting,
    Running,
    Stopping,
    Completed,
    Failed
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
};

struct CompileRequest {
    quint64 requestId = 0;
    QString sequencePath;
    QString stationPath;
};

struct CompileServiceResult {
    quint64 requestId = 0;
    bool success = false;
    QString sequenceId;
    QString sequenceName;
    QString sequenceVersion;
    int nodeCount = 0;
    QVector<UiDiagnostic> diagnostics;
};

struct RunRequest {
    quint64 requestId = 0;
    int uutCount = 1;
    QString uutPrefix = QStringLiteral("UUT");
};

struct RunServiceResult {
    quint64 requestId = 0;
    bool executed = false;
    bool stopRequested = false;
    PicoATE::Core::ExecutionReport report;
    QVector<UiDiagnostic> diagnostics;
};

QString uiRunStateName(UiRunState state);

} // namespace PicoATE::Ui

Q_DECLARE_METATYPE(PicoATE::Ui::UiRunState)
Q_DECLARE_METATYPE(PicoATE::Ui::UiDiagnostic)
Q_DECLARE_METATYPE(PicoATE::Ui::CompileServiceResult)
Q_DECLARE_METATYPE(PicoATE::Ui::RunServiceResult)
