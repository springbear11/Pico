#pragma once

#include "PicoATE/Core/ExecutionReport.h"

#include <QDateTime>
#include <QJsonObject>
#include <QString>
#include <QVector>

#include <memory>
#include <vector>

namespace PicoATE::Ui {

struct RuntimeLogLine {
    QDateTime timestampUtc;
    QString message;
    PicoATE::Core::UutId uutId;
};

struct RunArtifactSettings {
    bool txtLogEnabled = false;
    bool csvReportEnabled = false;
    bool xlsxReportEnabled = false;
    bool pdfReportEnabled = false;
    QString outputDirectory;
};

struct RunArtifactContext {
    QString sequenceName;
    QString sequenceFilePath;
    QString serialNumber;
    QString stationId;
    QString model;
    QString customerId;
    QString stationFilePath;
    QString order;
    QString tester;
    QString jigNo;
};

struct RunArtifactUutContext {
    PicoATE::Core::UutId uutId;
    QString serialNumber;
};

struct RunArtifactResult {
    bool success = true;
    QString errorMessage;
    QStringList filePaths;
};

RunArtifactSettings runArtifactSettingsFromStation(
    const QJsonObject& station,
    const QString& stationFilePath = {});
RunArtifactContext runArtifactContextFromDocuments(
    const QJsonObject& sequence,
    const QString& sequenceFilePath,
    const QJsonObject& station,
    const QString& stationFilePath,
    const QString& serialNumber);

class RunArtifactWriter
{
public:
    RunArtifactWriter();
    ~RunArtifactWriter();

    RunArtifactResult begin(const RunArtifactSettings& settings,
                            const QString& serialNumber,
                            const QDateTime& startedAt = QDateTime::currentDateTime());
    RunArtifactResult begin(const RunArtifactSettings& settings,
                            const RunArtifactContext& context,
                            const QDateTime& startedAt = QDateTime::currentDateTime());
    RunArtifactResult beginForUuts(
        const RunArtifactSettings& settings,
        const RunArtifactContext& context,
        const QVector<RunArtifactUutContext>& uuts,
        const QDateTime& startedAt = QDateTime::currentDateTime());
    RunArtifactResult appendLogLines(const QVector<RuntimeLogLine>& lines);
    RunArtifactResult finalize(const PicoATE::Core::ExecutionReport& report);
    void abandon();

    bool active() const;
    QString dateDirectory() const;
    QString baseName() const;
    QStringList baseNames() const;

private:
    void closeFiles();
    RunArtifactResult failure(const QString& message) const;

    RunArtifactSettings m_settings;
    struct ArtifactChannel;
    std::vector<std::unique_ptr<ArtifactChannel>> m_channels;
    QString m_dateDirectory;
    bool m_active = false;
};

} // namespace PicoATE::Ui
