#include "LoginDialog.h"
#include "MainWindow.h"
#include "ProductionWindow.h"

#include <QApplication>
#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>

int main(int argc, char* argv[])
{
    QApplication application(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("PicoATE UI"));
    QCoreApplication::setApplicationVersion(QStringLiteral("0.2.0"));
    QCoreApplication::setOrganizationName(QStringLiteral("PicoATE"));

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

    PicoATE::Ui::LoginDialog login(sequenceRoot);
    if (arguments.size() > 1) {
        login.setInitialSequencePath(arguments.at(1));
    }
    if (login.exec() != QDialog::Accepted) {
        return 0;
    }

    const auto selection = login.selection();
    if (selection.mode == PicoATE::Ui::UiMode::Test) {
        PicoATE::Ui::ProductionWindow window(selection);
        window.showMaximized();
        return application.exec();
    }

    PicoATE::Ui::MainWindow window;
    window.openSequenceFile(selection.sequencePath);
    window.openStationFile(selection.stationPath);
    window.showRunPage();
    window.showMaximized();
    return application.exec();
}
