#pragma once

#include "PicoATE/Core/ExecutionReport.h"

#include <QDateTime>
#include <QFile>
#include <QJsonObject>
#include <QString>
#include <QVector>

namespace PicoATE::Ui {

struct RuntimeLogLine {
    QDateTime timestampUtc;
    QString message;
};

struct RunArtifactSettings {
    bool txtLogEnabled = false;
    bool csvReportEnabled = false;
    bool xlsxReportEnabled = false;
    QString outputDirectory;
};

struct RunArtifactResult {
    bool success = true;
    QString errorMessage;
    QStringList filePaths;
};

RunArtifactSettings runArtifactSettingsFromStation(
    const QJsonObject& station,
    const QString& stationFilePath = {});

class RunArtifactWriter
{
public:
    RunArtifactWriter() = default;
    ~RunArtifactWriter();

    RunArtifactResult begin(const RunArtifactSettings& settings,
                            const QString& serialNumber,
                            const QDateTime& startedAt = QDateTime::currentDateTime());
    RunArtifactResult appendLogLines(const QVector<RuntimeLogLine>& lines);
    RunArtifactResult finalize(const PicoATE::Core::ExecutionReport& report);
    void abandon();

    bool active() const;
    QString dateDirectory() const;
    QString baseName() const;

private:
    void closeFiles();
    RunArtifactResult failure(const QString& message) const;

    RunArtifactSettings m_settings;
    QFile m_txtFile;
    QFile m_csvFile;
    QString m_xlsxFilePath;
    QString m_dateDirectory;
    QString m_baseName;
    bool m_active = false;
};

} // namespace PicoATE::Ui
