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
    case UiRunState::TestingDevice:
        return QStringLiteral("Testing device connection");
    case UiRunState::Starting:
        return QStringLiteral("Starting");
    case UiRunState::Running:
        return QStringLiteral("Running");
    case UiRunState::Pausing:
        return QStringLiteral("Pausing");
    case UiRunState::Paused:
        return QStringLiteral("Paused");
    case UiRunState::Stopping:
        return QStringLiteral("Stopping");
    case UiRunState::Completed:
        return QStringLiteral("Completed");
    case UiRunState::Failed:
        return QStringLiteral("Failed");
    }
    return QStringLiteral("Unknown");
}

QString deviceConnectionTestOutcomeName(DeviceConnectionTestOutcome outcome)
{
    switch (outcome) {
    case DeviceConnectionTestOutcome::Passed:
        return QStringLiteral("Passed");
    case DeviceConnectionTestOutcome::Failed:
        return QStringLiteral("Failed");
    case DeviceConnectionTestOutcome::TimedOut:
        return QStringLiteral("Timed out");
    case DeviceConnectionTestOutcome::Cancelled:
        return QStringLiteral("Cancelled");
    }
    return QStringLiteral("Unknown");
}

} // namespace PicoATE::Ui
