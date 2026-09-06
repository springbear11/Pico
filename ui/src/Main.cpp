#include "ApplicationDiagnostics.h"
#include "AdminStartupSplash.h"
#include "InputWheelGuard.h"
#include "LoginDialog.h"
#include "MainWindow.h"
#include "PicoATEStyle.h"
#include "ProductionWindow.h"
#include "UiLanguage.h"

#include <QApplication>
#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QGuiApplication>
#include <QIcon>
#include <QScreen>
#include <QTimer>
#include <QWidget>

#ifndef PICOATE_VERSION
#define PICOATE_VERSION "0.3.0"
#endif

namespace {

void prepareForMaximizedDisplay(QWidget& window,
                                QScreen* preferredScreen,
                                const QSize& preferredNormalSize,
                                const QSize& preferredMinimumSize)
{
    auto* screen = preferredScreen ? preferredScreen : window.screen();
    if (!screen) {
        screen = QGuiApplication::primaryScreen();
    }
    if (!screen) {
        window.resize(preferredNormalSize);
        return;
    }

    const auto available = screen->availableGeometry();
    auto safeArea = available.adjusted(32, 48, -32, -32);
    if (!safeArea.isValid()) {
        safeArea = available;
    }

    // restoreGeometry() may retain a previous maximized state. Clear it while
    // the window is still hidden so setGeometry() establishes a real normal
    // geometry instead of an oversized maximized client area.
    window.setWindowState(Qt::WindowNoState);
    window.setMinimumSize(preferredMinimumSize.boundedTo(safeArea.size()));

    const auto normalSize = preferredNormalSize
        .boundedTo(safeArea.size())
        .expandedTo(window.minimumSize());
    QRect normalGeometry(QPoint{}, normalSize);
    normalGeometry.moveCenter(safeArea.center());
    window.setGeometry(normalGeometry);
}

} // namespace

int main(int argc, char* argv[])
{
    QApplication application(argc, argv);
    PicoATE::Ui::InputWheelGuard inputWheelGuard(&application);
    application.installEventFilter(&inputWheelGuard);
    application.setStyle(new PicoATE::Ui::PicoATEStyle);
    PicoATE::Ui::applyPicoATEApplicationTheme(application);
    QCoreApplication::setApplicationName(QStringLiteral("PicoATE UI"));
    QCoreApplication::setApplicationVersion(QStringLiteral(PICOATE_VERSION));
    QCoreApplication::setOrganizationName(QStringLiteral("PicoATE"));
    PicoATE::Ui::UiLanguage::instance().restorePreference();
    application.setWindowIcon(QIcon(QStringLiteral(":/branding/PicoATE.png")));
    PicoATE::Ui::ApplicationDiagnostics::install();

    const auto arguments = application.arguments();
    QString sequenceRoot = QCoreApplication::applicationDirPath();
#ifdef PICOATE_PROJECT_DIR
    const auto developmentExamples = QDir(QStringLiteral(PICOATE_PROJECT_DIR))
        .filePath(QStringLiteral("examples"));
    const auto localProjectRoot =
        PicoATE::Ui::StartupSupport::productProjectRootPathForRoot(sequenceRoot);
    const bool hasLocalProjects = !QDir(localProjectRoot).entryList(
        QDir::Dirs | QDir::NoDotAndDotDot).isEmpty();
    const bool hasLocalRouting = QFileInfo::exists(
        PicoATE::Ui::StartupSupport::productRoutingPathForRoot(sequenceRoot));
    if (!hasLocalProjects && !hasLocalRouting &&
        PicoATE::Ui::StartupSupport::discoverSequenceFiles(sequenceRoot).isEmpty() &&
        QDir(developmentExamples).exists()) {
        sequenceRoot = developmentExamples;
    }
#endif

    auto login = PicoATE::Ui::createLoginDialog(sequenceRoot);
    if (arguments.size() > 1) {
        login->setInitialSequencePath(arguments.at(1));
    }
    if (login->exec() != QDialog::Accepted) {
        return 0;
    }

    const auto selection = login->selection();
    auto* const startupScreen = login->screen();
    login.reset();
    if (selection.mode == PicoATE::Ui::UiMode::Test) {
        auto window = PicoATE::Ui::createProductionWindow(selection);
        prepareForMaximizedDisplay(*window,
                                   startupScreen,
                                   QSize(1180, 760),
                                   QSize(840, 560));
        window->showMaximized();
        return application.exec();
    }

    PicoATE::Ui::AdminStartupSplash startupSplash;
    if (startupScreen) {
        const auto availableGeometry = startupScreen->availableGeometry();
        startupSplash.move(availableGeometry.center()
                           - startupSplash.rect().center());
    }
    QElapsedTimer startupTimer;
    startupTimer.start();
    startupSplash.show();
    startupSplash.raise();
    PicoATE::Ui::ApplicationDiagnostics::recordAction(
        QStringLiteral("ADMIN_STARTUP_BEGIN"));

    std::unique_ptr<PicoATE::Ui::MainWindow> window;
    constexpr int SplashWarmupMs = 140;
    QTimer::singleShot(
        SplashWarmupMs,
        &startupSplash,
        [&startupSplash,
         &startupTimer,
         &window,
         startupScreen,
         selection] {
            const auto windowStartedAt = startupTimer.elapsed();
            window = PicoATE::Ui::createMainWindow();
            auto* const windowPointer = window.get();
            PicoATE::Ui::ApplicationDiagnostics::recordAction(
                QStringLiteral("ADMIN_STARTUP_WINDOW_CREATED"),
                QStringLiteral("stage=%1ms,total=%2ms")
                    .arg(startupTimer.elapsed() - windowStartedAt)
                    .arg(startupTimer.elapsed()));

            prepareForMaximizedDisplay(*windowPointer,
                                       startupScreen,
                                       QSize(1180, 760),
                                       QSize(900, 600));
            QObject::connect(
                windowPointer,
                &PicoATE::Ui::MainWindow::adminWorkspaceReady,
                &startupSplash,
                [windowPointer,
                 autoRouting = selection.sequenceLoadMode ==
                                   PicoATE::Ui::SequenceLoadMode::AutoBySn,
                 &startupSplash,
                 &startupTimer] {
                    PicoATE::Ui::ApplicationDiagnostics::recordAction(
                        QStringLiteral("ADMIN_STARTUP_READY"),
                        QStringLiteral("total=%1ms").arg(startupTimer.elapsed()));
                    constexpr qint64 MinimumSplashMs = 350;
                    const auto delay = static_cast<int>(
                        qMax<qint64>(0, MinimumSplashMs - startupTimer.elapsed()));
                    QTimer::singleShot(
                        delay,
                        &startupSplash,
                        [windowPointer, autoRouting, &startupSplash] {
                            windowPointer->showMaximized();
                            windowPointer->raise();
                            windowPointer->activateWindow();
                            if (autoRouting) {
                                QTimer::singleShot(
                                    120,
                                    windowPointer,
                                    &PicoATE::Ui::MainWindow::showStartupScanDialog);
                            }
                            QTimer::singleShot(
                                80,
                                &startupSplash,
                                [&startupSplash] { startupSplash.hide(); });
                        });
                });

            QTimer::singleShot(
                0,
                &startupSplash,
                [&startupSplash, &startupTimer, &window, selection] {
                    auto* const stagedWindow = window.get();
                    if (selection.sequenceLoadMode ==
                        PicoATE::Ui::SequenceLoadMode::AutoBySn) {
                        stagedWindow->configureAutoRouting(
                            selection.productRoutingPath);
                    } else {
                        stagedWindow->setProductRoutingPath(
                            selection.productRoutingPath);
                        if (selection.newProjectTemplate) {
                            stagedWindow->initializeNewProjectTemplate(
                                selection.projectRootPath);
                        } else {
                            stagedWindow->openSequenceFile(selection.sequencePath);
                        }
                    }
                    PicoATE::Ui::ApplicationDiagnostics::recordAction(
                        QStringLiteral("ADMIN_STARTUP_SEQUENCE_READY"),
                        QStringLiteral("total=%1ms").arg(startupTimer.elapsed()));

                    QTimer::singleShot(
                        0,
                        &startupSplash,
                        [&startupTimer, &window, selection] {
                            auto* const stagedWindow = window.get();
                            if (selection.sequenceLoadMode !=
                                    PicoATE::Ui::SequenceLoadMode::AutoBySn &&
                                !selection.newProjectTemplate) {
                                stagedWindow->openStationFile(selection.stationPath);
                            }
                            stagedWindow->showRunPage();
                            PicoATE::Ui::ApplicationDiagnostics::recordAction(
                                QStringLiteral("ADMIN_STARTUP_SOURCES_READY"),
                                QStringLiteral("total=%1ms")
                                    .arg(startupTimer.elapsed()));
                            QTimer::singleShot(
                                0,
                                stagedWindow,
                                [stagedWindow] {
                                    stagedWindow->initializeAdminWorkspace();
                                });
                        });
                });
        });
    return application.exec();
}
