#pragma once

#include "PicoATE/Core/ExecutionReportJson.h"

#include <QDateTime>
#include <QStringList>

namespace PicoATE::Ui {

struct ReportHistoryEntry {
    QString id;
    QDateTime savedAtUtc;
    QString sequenceId;
    QString sequenceVersion;
    QString planId;
    PicoATE::Core::ExecutionState state = PicoATE::Core::ExecutionState::Idle;
    bool hasError = false;
    int uutCount = 0;
    QStringList uutIds;
    QString fileName;
};

struct ReportHistorySaveResult {
    bool success = false;
    ReportHistoryEntry entry;
    QString errorMessage;
};

struct ReportHistoryLoadResult {
    PicoATE::Core::ExecutionReport report;
    QVector<PicoATE::Core::ExecutionReportJsonError> parseErrors;
    QString errorMessage;

    bool ok() const { return errorMessage.isEmpty() && parseErrors.isEmpty(); }
};

class ReportHistoryStore
{
public:
    explicit ReportHistoryStore(QString rootDirectory = {});

    QString rootDirectory() const;
    ReportHistorySaveResult save(const PicoATE::Core::ExecutionReport& report);
    QVector<ReportHistoryEntry> entries(QString* errorMessage = nullptr);
    ReportHistoryLoadResult load(const QString& entryId);

private:
    QVector<ReportHistoryEntry> readIndex(QString* errorMessage) const;
    QVector<ReportHistoryEntry> rebuildIndex(QString* errorMessage);
    bool writeIndex(const QVector<ReportHistoryEntry>& entries,
                    QString* errorMessage) const;
    QString reportPath(const QString& fileName) const;

    QString m_rootDirectory;
};

} // namespace PicoATE::Ui
