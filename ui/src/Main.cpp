#include "ApplicationDiagnostics.h"
#include "LoginDialog.h"
#include "MainWindow.h"
#include "PicoATEStyle.h"
#include "ProductionWindow.h"

#include <QApplication>
#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QIcon>
#include <QTimer>

int main(int argc, char* argv[])
{
    QApplication application(argc, argv);
    application.setStyle(new PicoATE::Ui::PicoATEStyle);
    QCoreApplication::setApplicationName(QStringLiteral("PicoATE UI"));
    QCoreApplication::setApplicationVersion(QStringLiteral("0.2.0"));
    QCoreApplication::setOrganizationName(QStringLiteral("PicoATE"));
    application.setWindowIcon(QIcon(QStringLiteral(":/branding/PicoATE.png")));
    PicoATE::Ui::ApplicationDiagnostics::install();

    const auto arguments = application.arguments();
    QString sequenceRoot = QCoreApplication::applicationDirPath();
#ifdef PICOATE_PROJECT_DIR
    const auto developmentExamples = QDir(QStringLiteral(PICOATE_PROJECT_DIR))
        .filePath(QStringLiteral("examples"));
    if (PicoATE::Ui::StartupSupport::discoverSequenceFiles(sequenceRoot).isEmpty() &&
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
    login.reset();
    if (selection.mode == PicoATE::Ui::UiMode::Test) {
        auto window = PicoATE::Ui::createProductionWindow(selection);
        window->showMaximized();
        return application.exec();
    }

    auto window = PicoATE::Ui::createMainWindow();
    window->openSequenceFile(selection.sequencePath);
    window->openStationFile(selection.stationPath);
    window->showRunPage();
    window->showMaximized();
    auto* const windowPointer = window.get();
    QTimer::singleShot(0, windowPointer, [windowPointer] {
        windowPointer->initializeAdminWorkspace();
    });
    return application.exec();
}
