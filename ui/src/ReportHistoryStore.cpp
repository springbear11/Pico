#include "ReportHistoryStore.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSaveFile>
#include <QStandardPaths>
#include <QUuid>

#include <algorithm>

namespace PicoATE::Ui {

namespace {

constexpr auto IndexFileName = "index.json";

QString stateName(PicoATE::Core::ExecutionState state)
{
    using PicoATE::Core::ExecutionState;
    switch (state) {
    case ExecutionState::Idle: return "Idle";
    case ExecutionState::Starting: return "Starting";
    case ExecutionState::Running: return "Running";
    case ExecutionState::Paused: return "Paused";
    case ExecutionState::Stopping: return "Stopping";
    case ExecutionState::CleaningUp: return "CleaningUp";
    case ExecutionState::Completed: return "Completed";
    case ExecutionState::CompletedWithError: return "CompletedWithError";
    case ExecutionState::Aborted: return "Aborted";
    }
    return "Idle";
}

PicoATE::Core::ExecutionState stateFromName(const QString& value)
{
    using PicoATE::Core::ExecutionState;
    if (value == "Starting") return ExecutionState::Starting;
    if (value == "Running") return ExecutionState::Running;
    if (value == "Paused") return ExecutionState::Paused;
    if (value == "Stopping") return ExecutionState::Stopping;
    if (value == "CleaningUp") return ExecutionState::CleaningUp;
    if (value == "Completed") return ExecutionState::Completed;
    if (value == "CompletedWithError") return ExecutionState::CompletedWithError;
    if (value == "Aborted") return ExecutionState::Aborted;
    return ExecutionState::Idle;
}

QJsonObject entryToJson(const ReportHistoryEntry& entry)
{
    QJsonArray uutIds;
    for (const auto& id : entry.uutIds) uutIds.push_back(id);
    return {
        {"id", entry.id},
        {"savedAtUtc", entry.savedAtUtc.toString(Qt::ISODateWithMs)},
        {"sequenceId", entry.sequenceId},
        {"sequenceVersion", entry.sequenceVersion},
        {"planId", entry.planId},
        {"state", stateName(entry.state)},
        {"hasError", entry.hasError},
        {"uutCount", entry.uutCount},
        {"uutIds", uutIds},
        {"fileName", entry.fileName},
    };
}

ReportHistoryEntry entryFromJson(const QJsonObject& object)
{
    ReportHistoryEntry entry;
    entry.id = object.value("id").toString();
    entry.savedAtUtc = QDateTime::fromString(
        object.value("savedAtUtc").toString(), Qt::ISODateWithMs);
    entry.sequenceId = object.value("sequenceId").toString();
    entry.sequenceVersion = object.value("sequenceVersion").toString();
    entry.planId = object.value("planId").toString();
    entry.state = stateFromName(object.value("state").toString());
    entry.hasError = object.value("hasError").toBool(false);
    entry.uutCount = object.value("uutCount").toInt();
    const auto uutIds = object.value("uutIds").toArray();
    for (const auto& id : uutIds) entry.uutIds.push_back(id.toString());
    entry.fileName = QFileInfo(object.value("fileName").toString()).fileName();
    return entry;
}

ReportHistoryEntry entryForReport(const QString& id,
                                  const QString& fileName,
                                  const QDateTime& savedAtUtc,
                                  const PicoATE::Core::ExecutionReport& report)
{
    ReportHistoryEntry entry;
    entry.id = id;
    entry.fileName = fileName;
    entry.savedAtUtc = savedAtUtc;
    entry.sequenceId = report.sequenceId;
    entry.sequenceVersion = report.sequenceVersion;
    entry.planId = report.planId;
    entry.state = report.state;
    entry.hasError = report.hasError;
    entry.uutCount = report.uuts.size();
    for (const auto& uut : report.uuts) entry.uutIds.push_back(uut.uutId);
    return entry;
}

void sortNewestFirst(QVector<ReportHistoryEntry>& entries)
{
    std::sort(entries.begin(), entries.end(), [](const auto& left, const auto& right) {
        if (left.savedAtUtc != right.savedAtUtc) return left.savedAtUtc > right.savedAtUtc;
        return left.id > right.id;
    });
}

} // namespace

ReportHistoryStore::ReportHistoryStore(QString rootDirectory)
    : m_rootDirectory(std::move(rootDirectory))
{
    if (m_rootDirectory.trimmed().isEmpty()) {
        m_rootDirectory = QDir(
            QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation))
                                  .filePath("history");
    }
    m_rootDirectory = QDir::cleanPath(QFileInfo(m_rootDirectory).absoluteFilePath());
}

QString ReportHistoryStore::rootDirectory() const
{
    return m_rootDirectory;
}

ReportHistorySaveResult ReportHistoryStore::save(
    const PicoATE::Core::ExecutionReport& report)
{
    ReportHistorySaveResult result;
    QDir directory;
    if (!directory.mkpath(m_rootDirectory)) {
        result.errorMessage = QString("Failed to create report history directory: %1")
                                  .arg(m_rootDirectory);
        return result;
    }

    QString indexError;
    auto indexEntries = entries(&indexError);
    if (!indexError.isEmpty()) {
        result.errorMessage = indexError;
        return result;
    }

    const auto savedAtUtc = QDateTime::currentDateTimeUtc();
    const auto uuid = QUuid::createUuid().toString(QUuid::WithoutBraces).left(8);
    const auto id = QString("%1-%2")
                        .arg(savedAtUtc.toString("yyyyMMdd-HHmmss-zzz"), uuid);
    const auto fileName = id + ".json";
    QSaveFile file(reportPath(fileName));
    if (!file.open(QIODevice::WriteOnly)) {
        result.errorMessage = file.errorString();
        return result;
    }
    if (file.write(PicoATE::Core::serializeExecutionReport(report)) < 0 || !file.commit()) {
        result.errorMessage = file.errorString();
        return result;
    }

    result.entry = entryForReport(id, fileName, savedAtUtc, report);
    indexEntries.push_back(result.entry);
    sortNewestFirst(indexEntries);
    if (!writeIndex(indexEntries, &result.errorMessage)) {
        QFile::remove(reportPath(fileName));
        return result;
    }
    result.success = true;
    return result;
}

QVector<ReportHistoryEntry> ReportHistoryStore::entries(QString* errorMessage)
{
    if (errorMessage) errorMessage->clear();
    const auto indexPath = reportPath(IndexFileName);
    if (!QFileInfo::exists(indexPath)) {
        return rebuildIndex(errorMessage);
    }

    QString readError;
    auto result = readIndex(&readError);
    if (readError.isEmpty()) return result;
    return rebuildIndex(errorMessage);
}

ReportHistoryLoadResult ReportHistoryStore::load(const QString& entryId)
{
    ReportHistoryLoadResult result;
    QString indexError;
    const auto allEntries = entries(&indexError);
    if (!indexError.isEmpty()) {
        result.errorMessage = indexError;
        return result;
    }
    const auto it = std::find_if(allEntries.cbegin(), allEntries.cend(), [&entryId](const auto& item) {
        return item.id == entryId;
    });
    if (it == allEntries.cend()) {
        result.errorMessage = QString("Report history entry not found: %1").arg(entryId);
        return result;
    }

    QFile file(reportPath(it->fileName));
    if (!file.open(QIODevice::ReadOnly)) {
        result.errorMessage = file.errorString();
        return result;
    }
    const auto parsed = PicoATE::Core::parseExecutionReport(file.readAll());
    result.report = parsed.report;
    result.parseErrors = parsed.errors;
    return result;
}

QVector<ReportHistoryEntry> ReportHistoryStore::readIndex(QString* errorMessage) const
{
    QVector<ReportHistoryEntry> result;
    QFile file(reportPath(IndexFileName));
    if (!file.open(QIODevice::ReadOnly)) {
        if (errorMessage) *errorMessage = file.errorString();
        return result;
    }
    QJsonParseError parseError;
    const auto document = QJsonDocument::fromJson(file.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        if (errorMessage) {
            *errorMessage = parseError.error == QJsonParseError::NoError
                ? QString("Report history index root must be an object")
                : parseError.errorString();
        }
        return result;
    }
    const auto root = document.object();
    if (root.value("schema").toString() != "picoate.report-history-index" ||
        root.value("schemaVersion").toInt(-1) != 1 ||
        !root.value("entries").isArray()) {
        if (errorMessage) *errorMessage = "Unsupported or invalid report history index";
        return result;
    }
    for (const auto& value : root.value("entries").toArray()) {
        if (!value.isObject()) continue;
        auto entry = entryFromJson(value.toObject());
        if (!entry.id.isEmpty() && !entry.fileName.isEmpty()) result.push_back(entry);
    }
    sortNewestFirst(result);
    if (errorMessage) errorMessage->clear();
    return result;
}

QVector<ReportHistoryEntry> ReportHistoryStore::rebuildIndex(QString* errorMessage)
{
    QVector<ReportHistoryEntry> result;
    QDir directory;
    if (!directory.mkpath(m_rootDirectory)) {
        if (errorMessage) *errorMessage = "Failed to create report history directory";
        return result;
    }
    const QDir historyDirectory(m_rootDirectory);
    const auto files = historyDirectory.entryInfoList(
        {"*.json"}, QDir::Files | QDir::Readable, QDir::Time);
    for (const auto& info : files) {
        if (info.fileName() == IndexFileName) continue;
        QFile file(info.absoluteFilePath());
        if (!file.open(QIODevice::ReadOnly)) continue;
        const auto parsed = PicoATE::Core::parseExecutionReport(file.readAll());
        if (!parsed.ok()) continue;
        result.push_back(entryForReport(
            info.completeBaseName(),
            info.fileName(),
            info.lastModified().toUTC(),
            parsed.report));
    }
    sortNewestFirst(result);
    QString writeError;
    if (!writeIndex(result, &writeError) && errorMessage) *errorMessage = writeError;
    else if (errorMessage) errorMessage->clear();
    return result;
}

bool ReportHistoryStore::writeIndex(const QVector<ReportHistoryEntry>& entries,
                                    QString* errorMessage) const
{
    QJsonArray array;
    for (const auto& entry : entries) array.push_back(entryToJson(entry));
    const QJsonObject root{
        {"schema", "picoate.report-history-index"},
        {"schemaVersion", 1},
        {"entries", array},
    };
    QSaveFile file(reportPath(IndexFileName));
    if (!file.open(QIODevice::WriteOnly)) {
        if (errorMessage) *errorMessage = file.errorString();
        return false;
    }
    if (file.write(QJsonDocument(root).toJson(QJsonDocument::Indented)) < 0 ||
        !file.commit()) {
        if (errorMessage) *errorMessage = file.errorString();
        return false;
    }
    if (errorMessage) errorMessage->clear();
    return true;
}

QString ReportHistoryStore::reportPath(const QString& fileName) const
{
    return QDir(m_rootDirectory).filePath(QFileInfo(fileName).fileName());
}

} // namespace PicoATE::Ui
