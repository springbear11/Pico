#pragma once

#include "PicoATE/Core/ExecutionReport.h"

#include <QString>

namespace PicoATE::Ui {

struct ReportExportResult {
    bool success = false;
    QString errorMessage;
};

class ReportExporter
{
public:
    static ReportExportResult saveJson(
        const QString& filePath,
        const PicoATE::Core::ExecutionReport& report);
    static ReportExportResult saveCsv(
        const QString& filePath,
        const PicoATE::Core::ExecutionReport& report);
};

} // namespace PicoATE::Ui
