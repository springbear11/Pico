#include "UiExecutionTypes.h"

namespace PicoATE::Ui {

QString uiRunStateName(UiRunState state)
{
    switch (state) {
    case UiRunState::Empty:
        return QStringLiteral("Empty");
    case UiRunState::SourceSelected:
        return QStringLiteral("Source selected");
    case UiRunState::Compiling:
        return QStringLiteral("Compiling");
    case UiRunState::CompileFailed:
        return QStringLiteral("Compile failed");
    case UiRunState::Ready:
        return QStringLiteral("Ready");
    case UiRunState::Starting:
        return QStringLiteral("Starting");
    case UiRunState::Running:
        return QStringLiteral("Running");
    case UiRunState::Stopping:
        return QStringLiteral("Stopping");
    case UiRunState::Completed:
        return QStringLiteral("Completed");
    case UiRunState::Failed:
        return QStringLiteral("Failed");
    }
    return QStringLiteral("Unknown");
}

} // namespace PicoATE::Ui
