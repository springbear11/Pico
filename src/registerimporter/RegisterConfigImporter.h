#pragma once

#include <QJsonObject>
#include <QString>
#include <QVector>

namespace PicoATE::RegisterImport {

struct RegisterImportDiagnostic {
    QString path;
    QString message;
    QString suggestion;
};

struct RegisterImportOptions {
    QString deviceId = QStringLiteral("MODBUS1");
    int waitMs = 20;
    int timeoutMs = 3000;
};

struct RegisterImportResult {
    QString workbookPath;
    QJsonObject testItem;
    QVector<RegisterImportDiagnostic> errors;
    QVector<RegisterImportDiagnostic> warnings;
    int sheetCount = 0;
    int parameterCount = 0;

    bool ok() const { return errors.isEmpty() && !testItem.isEmpty(); }
};

RegisterImportResult importRegisterDirectory(
    const QString& directoryPath,
    const RegisterImportOptions& options = {});

RegisterImportResult importRegisterWorkbook(
    const QString& workbookPath,
    const RegisterImportOptions& options = {});

QJsonObject resultToJson(const RegisterImportResult& result);

} // namespace PicoATE::RegisterImport
