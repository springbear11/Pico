#include "RuntimeIntegrity.h"
#include "UiLanguage.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLockFile>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSet>

#include <algorithm>

namespace PicoATE::Ui {
namespace {

bool isDigest(const QString& text)
{
    static const QRegularExpression pattern(QStringLiteral("^[0-9a-fA-F]{64}$"));
    return pattern.match(text).hasMatch();
}

QString fileDigest(const QString& path, qint64& size, const RuntimeIntegrity::Cancel& cancel,
                   QString& error)
{
    const QFileInfo before(path);
    if (!before.isFile() || before.isSymLink()) {
        error = uiText("Required file is missing or is not a regular file.");
        return {};
    }
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        error = file.errorString();
        return {};
    }
    size = before.size();
    QCryptographicHash hash(QCryptographicHash::Sha256);
    while (!file.atEnd()) {
        if (cancel && cancel()) {
            error = uiText("Verification cancelled.");
            return {};
        }
        const auto bytes = file.read(1024 * 1024);
        if (bytes.isEmpty() && file.error() != QFileDevice::NoError) {
            error = file.errorString();
            return {};
        }
        hash.addData(bytes);
    }
    const QFileInfo after(path);
    if (before.size() != after.size() || before.lastModified() != after.lastModified()) {
        error = uiText("File changed during verification. Check again.");
        return {};
    }
    return QString::fromLatin1(hash.result().toHex());
}

} // namespace

bool IntegrityReport::passed() const
{
    return !cancelled && baselineError.isEmpty() && files.size() == 2 &&
        std::all_of(files.cbegin(), files.cend(), [](const auto& file) {
            return file.status == IntegrityStatus::Matched;
        });
}

QString RuntimeIntegrity::baselineFileName() { return QStringLiteral("IntegrityBaseline.json"); }
QStringList RuntimeIntegrity::fileNames()
{
    return {QStringLiteral("PicoATE.UI.exe"), QStringLiteral("PicoATECore.dll")};
}

IntegrityReport RuntimeIntegrity::check(const QString& directory, const Cancel& cancel)
{
    IntegrityReport report;
    report.directory = QDir(directory).absolutePath();
    report.checkedAt = QDateTime::currentDateTimeUtc();
    const auto names = fileNames();
    QHash<QString, QString> expected;
    QFile baseline(QDir(report.directory).filePath(baselineFileName()));
    report.baselineExists = baseline.exists();
    if (!baseline.open(QIODevice::ReadOnly)) {
        report.baselineError = QStringLiteral("Integrity baseline is missing or cannot be read.");
    } else if (baseline.size() > 1024 * 1024) {
        report.baselineError = QStringLiteral("Integrity baseline is invalid.");
    } else {
        const auto bytes = baseline.readAll();
        report.baselineDigest = QCryptographicHash::hash(bytes, QCryptographicHash::Sha256);
        QJsonParseError error;
        const auto document = QJsonDocument::fromJson(bytes, &error);
        report.baseline = document.object();
        bool valid = error.error == QJsonParseError::NoError && document.isObject() &&
            report.baseline.value("schemaVersion") == QJsonValue(1) &&
            report.baseline.value("algorithm").toString() == QStringLiteral("SHA-256");
        const auto files = report.baseline.value("files").toArray();
        valid = valid && files.size() == names.size();
        for (const auto& value : files) {
            const auto entry = value.toObject();
            const auto path = entry.value("path").toString();
            const auto hash = entry.value("sha256").toString();
            valid = valid && value.isObject() && names.contains(path) &&
                !expected.contains(path) && isDigest(hash);
            expected.insert(path, hash.toLower());
        }
        if (!valid) {
            expected.clear();
            report.baselineError = QStringLiteral("Integrity baseline is invalid.");
        }
    }
    for (const auto& name : names) {
        IntegrityFile entry;
        entry.path = name;
        entry.expected = expected.value(name);
        const auto path = QDir(report.directory).filePath(name);
        entry.actual = fileDigest(path, entry.size, cancel, entry.error);
        if (entry.actual.isEmpty()) {
            entry.status = QFileInfo::exists(path) ? IntegrityStatus::ReadError : IntegrityStatus::Missing;
        } else if (entry.expected.isEmpty()) {
            entry.status = IntegrityStatus::Unverified;
        } else {
            entry.status = entry.actual == entry.expected ? IntegrityStatus::Matched : IntegrityStatus::Modified;
        }
        report.files.push_back(entry);
    }
    report.cancelled = cancel && cancel();
    return report;
}

QString RuntimeIntegrity::authorize(const IntegrityReport& reviewed, const QStringList& selected,
                                    AdminAccess access, const QString& password, const QString& reason,
                                    const Cancel& cancel)
{
    if (access != AdminAccess::Supervisor ||
        StartupSupport::adminAccessForPassword(password) != AdminAccess::Supervisor) {
        return uiText("Daily administrator password is required.");
    }
    if (reason.trimmed().isEmpty() || selected.isEmpty()) {
        return uiText("Select files and enter an approval reason.");
    }
    const auto names = fileNames();
    for (const auto& name : selected) {
        if (!names.contains(name)) return uiText("Invalid file selection.");
    }
    QLockFile lock(QDir(reviewed.directory).filePath(baselineFileName() + ".lock"));
    if (!lock.tryLock(0)) return uiText("Integrity baseline is busy. Try again.");
    const auto current = check(reviewed.directory, cancel);
    if (current.cancelled) return uiText("Verification cancelled.");
    if (current.baselineExists != reviewed.baselineExists || current.baselineDigest != reviewed.baselineDigest ||
        current.files.size() != reviewed.files.size()) {
        return uiText("Files or baseline changed since verification. Check again.");
    }
    for (int i = 0; i < current.files.size(); ++i) {
        if (!isDigest(current.files[i].actual)) return uiText("Restore missing or unreadable files before approval.");
        if (current.files[i].actual != reviewed.files[i].actual) {
            return uiText("Files or baseline changed since verification. Check again.");
        }
    }
    const QSet<QString> selection(selected.cbegin(), selected.cend());
    if (!current.baselineError.isEmpty() && selection.size() != names.size()) {
        return uiText("Select both files to establish a new baseline.");
    }
    auto baseline = current.baselineError.isEmpty() ? current.baseline : QJsonObject{};
    QJsonArray files;
    QJsonArray changes;
    for (const auto& file : current.files) {
        const auto hash = selection.contains(file.path) ? file.actual : file.expected;
        files.push_back(QJsonObject{{"path", file.path}, {"sha256", hash}});
        if (selection.contains(file.path) && file.actual != file.expected) {
            changes.push_back(QJsonObject{{"path", file.path}, {"before", file.expected}, {"after", file.actual}});
        }
    }
    const auto now = QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs);
    const auto user = qEnvironmentVariable("USERNAME");
    auto history = baseline.value("history").toArray();
    history.push_back(QJsonObject{{"atUtc", now}, {"user", user}, {"authority", "dailyAdmin"},
        {"reason", reason.trimmed()}, {"previousBaselineSha256", QString::fromLatin1(current.baselineDigest.toHex())},
        {"changes", changes}});
    while (history.size() > 100) history.removeFirst();
    baseline.insert("schemaVersion", 1);
    baseline.insert("algorithm", "SHA-256");
    baseline.insert("updatedAtUtc", now);
    baseline.insert("updatedBy", user);
    baseline.insert("files", files);
    baseline.insert("history", history);
    if (cancel && cancel()) return uiText("Verification cancelled.");
    QSaveFile output(QDir(reviewed.directory).filePath(baselineFileName()));
    if (!output.open(QIODevice::WriteOnly)) return output.errorString();
    const auto bytes = QJsonDocument(baseline).toJson(QJsonDocument::Indented);
    if (output.write(bytes) != bytes.size() || !output.commit()) return output.errorString();
    return {};
}

QString integrityStatusText(IntegrityStatus status)
{
    switch (status) {
    case IntegrityStatus::Matched: return uiText("Matched");
    case IntegrityStatus::Modified: return uiText("Modified");
    case IntegrityStatus::Missing: return uiText("Missing");
    case IntegrityStatus::ReadError: return uiText("Read error");
    case IntegrityStatus::Unverified: return uiText("Unverified");
    }
    return {};
}

QString integrityFailureText(const IntegrityReport& report)
{
    QStringList errors;
    if (!report.baselineError.isEmpty()) errors.push_back(uiText(report.baselineError.toUtf8().constData()));
    for (const auto& file : report.files) {
        if (file.status != IntegrityStatus::Matched) {
            errors.push_back(file.path + ": " + integrityStatusText(file.status));
        }
    }
    return errors.join(QStringLiteral("\n"));
}

} // namespace PicoATE::Ui
