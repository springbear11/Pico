#include "ApplicationDiagnostics.h"
#include "AdminStartupSplash.h"
#include "LoginDialog.h"
#include "MainWindow.h"
#include "PicoATEStyle.h"
#include "ProductionWindow.h"

#include <QApplication>
#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFileInfo>
#include <QIcon>
#include <QScreen>
#include <QTimer>

#ifndef PICOATE_VERSION
#define PICOATE_VERSION "0.2.0"
#endif

int main(int argc, char* argv[])
{
    QApplication application(argc, argv);
    application.setStyle(new PicoATE::Ui::PicoATEStyle);
    PicoATE::Ui::applyPicoATEApplicationTheme(application);
    QCoreApplication::setApplicationName(QStringLiteral("PicoATE UI"));
    QCoreApplication::setApplicationVersion(QStringLiteral(PICOATE_VERSION));
    QCoreApplication::setOrganizationName(QStringLiteral("PicoATE"));
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
    application.processEvents(QEventLoop::ExcludeUserInputEvents);

    auto window = PicoATE::Ui::createMainWindow();
    if (startupScreen) {
        window->setGeometry(startupScreen->availableGeometry());
    }
    if (selection.sequenceLoadMode == PicoATE::Ui::SequenceLoadMode::AutoBySn) {
        window->configureAutoRouting(selection.productRoutingPath);
    } else {
        window->setProductRoutingPath(selection.productRoutingPath);
        if (selection.newProjectTemplate) {
            window->initializeNewProjectTemplate(selection.projectRootPath);
        } else {
            window->openSequenceFile(selection.sequencePath);
            window->openStationFile(selection.stationPath);
        }
    }
    window->showRunPage();
    auto* const windowPointer = window.get();
    QObject::connect(
        windowPointer,
        &PicoATE::Ui::MainWindow::adminWorkspaceReady,
        &startupSplash,
        [windowPointer,
         autoRouting = selection.sequenceLoadMode ==
                           PicoATE::Ui::SequenceLoadMode::AutoBySn,
         &startupSplash,
         &startupTimer] {
            constexpr qint64 MinimumSplashMs = 350;
            const auto delay = static_cast<int>(
                qMax<qint64>(0, MinimumSplashMs - startupTimer.elapsed()));
            QTimer::singleShot(delay, &startupSplash,
                               [windowPointer, autoRouting, &startupSplash] {
                windowPointer->showMaximized();
                windowPointer->raise();
                windowPointer->activateWindow();
                if (autoRouting) {
                    QTimer::singleShot(
                        120, windowPointer,
                        &PicoATE::Ui::MainWindow::showStartupScanDialog);
                }
                QTimer::singleShot(80, &startupSplash,
                                   [&startupSplash] { startupSplash.hide(); });
            });
        });
    QTimer::singleShot(0, windowPointer, [windowPointer] {
        windowPointer->initializeAdminWorkspace();
    });
    return application.exec();
}
