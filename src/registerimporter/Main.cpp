#include "RegisterConfigImporter.h"

#include <QCommandLineParser>
#include <QCoreApplication>
#include <QJsonDocument>
#include <QTextStream>

using namespace PicoATE::RegisterImport;

int main(int argc, char* argv[])
{
    QCoreApplication application(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("PicoATE.RegisterImporter"));
    QCoreApplication::setApplicationVersion(QStringLiteral("1"));

    QCommandLineParser parser;
    parser.setApplicationDescription(
        QStringLiteral("Convert a PicoATE project Register workbook into register_config JSON."));
    parser.addHelpOption();
    parser.addVersionOption();

    const QCommandLineOption registerDirectoryOption(
        QStringList{QStringLiteral("register-dir")},
        QStringLiteral("Register directory containing exactly one .xlsx/.xlsm workbook."),
        QStringLiteral("directory"));
    const QCommandLineOption workbookOption(
        QStringList{QStringLiteral("workbook")},
        QStringLiteral("Import one explicit .xlsx/.xlsm workbook."),
        QStringLiteral("file"));
    const QCommandLineOption deviceOption(
        QStringList{QStringLiteral("device-id")},
        QStringLiteral("Logical Modbus device ID written into generated steps."),
        QStringLiteral("id"),
        QStringLiteral("MODBUS1"));
    const QCommandLineOption prettyOption(
        QStringList{QStringLiteral("pretty")},
        QStringLiteral("Pretty-print the JSON response."));
    parser.addOption(registerDirectoryOption);
    parser.addOption(workbookOption);
    parser.addOption(deviceOption);
    parser.addOption(prettyOption);
    parser.process(application);

    RegisterImportOptions options;
    options.deviceId = parser.value(deviceOption).trimmed();

    RegisterImportResult result;
    if (parser.isSet(workbookOption) == parser.isSet(registerDirectoryOption)) {
        result.errors.push_back({
            QStringLiteral("arguments"),
            QStringLiteral("Specify exactly one of --register-dir or --workbook"),
            QStringLiteral("Use --workbook <file> for a workbook selected in Flow"),
        });
    } else if (parser.isSet(workbookOption)) {
        result = importRegisterWorkbook(parser.value(workbookOption), options);
    } else {
        result = importRegisterDirectory(parser.value(registerDirectoryOption), options);
    }

    const auto format = parser.isSet(prettyOption)
        ? QJsonDocument::Indented
        : QJsonDocument::Compact;
    QTextStream output(stdout);
    output << QJsonDocument(resultToJson(result)).toJson(format);
    output.flush();
    return result.ok() ? 0 : 2;
}
