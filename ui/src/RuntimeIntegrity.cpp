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
#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <winver.h>
#endif

namespace PicoATE::Ui {
namespace {
bool isDigest(const QString& text)
{
    static const QRegularExpression pattern(QStringLiteral("^[0-9a-fA-F]{64}$"));
    return pattern.match(text).hasMatch();
}

bool isDllInPluginDirectory(const QString& path)
{
    return path.startsWith("plugins/", Qt::CaseInsensitive) && path.endsWith(".dll", Qt::CaseInsensitive) &&
        path == QDir::cleanPath(path) && !path.contains('\\') && !path.contains(':');
}

bool isPluginPath(const QString& path)
{
    return isDllInPluginDirectory(path) && QDir::match(QStringLiteral("PicoATE.*.dll"), QFileInfo(path).fileName());
}

bool isLinked(const QFileInfo& file) { return file.isSymLink() || file.isJunction(); }

QStringList inventory(const QString& directory, QString& error)
{
    QStringList names{QStringLiteral("PicoATE.UI.exe"), QStringLiteral("PicoATECore.dll")};
    if (directory.isEmpty()) return names;
    const QDir root(directory);
    const auto visit = [&](auto&& self, const QString& path) -> void {
        const QFileInfo info(path);
        if (!info.exists()) return;
        if (isLinked(info) || !info.isDir()) {
            error = uiText("Plugin directories must not be links or redirected paths.");
            return;
        }
        const QDir dir(path);
        for (const auto& child : dir.entryInfoList(QDir::Files | QDir::Dirs | QDir::Hidden | QDir::System | QDir::NoDotAndDotDot, QDir::Name)) {
            if (child.isDir()) self(self, child.absoluteFilePath());
            else if (QDir::match(QStringLiteral("PicoATE.*.dll"), child.fileName()))
                names.push_back(root.relativeFilePath(child.absoluteFilePath()));
        }
    };
    visit(visit, root.filePath("plugins"));
    std::sort(names.begin() + 2, names.end(), [](const auto& a, const auto& b) { return a.compare(b, Qt::CaseInsensitive) < 0; });
    return names;
}

bool safeParents(const QString& root, const QString& relative)
{
    auto path = QFileInfo(QDir(root).filePath(relative)).absolutePath();
    const auto boundary = QDir(root).absolutePath();
    while (path.compare(boundary, Qt::CaseInsensitive) != 0) {
        if (isLinked(QFileInfo(path))) return false;
        const auto parent = QFileInfo(path).absolutePath();
        if (parent == path) return false;
        path = parent;
    }
    return true;
}

QString fileDigest(const QString& path, qint64& size, const RuntimeIntegrity::Cancel& cancel, QString& error)
{
    const QFileInfo before(path);
    if (!before.isFile() || isLinked(before)) {
        error = uiText("Required file is missing or is not a regular file.");
        return {};
    }
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) { error = file.errorString(); return {}; }
    size = before.size();
    QCryptographicHash hash(QCryptographicHash::Sha256);
    while (!file.atEnd()) {
        if (cancel && cancel()) { error = uiText("Verification cancelled."); return {}; }
        const auto bytes = file.read(1024 * 1024);
        if (bytes.isEmpty() && file.error() != QFileDevice::NoError) { error = file.errorString(); return {}; }
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
    return !cancelled && baselineError.isEmpty() && inventoryError.isEmpty() && files.size() >= 2 &&
        std::all_of(files.cbegin(), files.cend(), [](const auto& file) { return file.status == IntegrityStatus::Matched; });
}

QString RuntimeIntegrity::baselineFileName() { return QStringLiteral("IntegrityBaseline.json"); }
QStringList RuntimeIntegrity::fileNames(const QString& directory)
{
    QString error;
    return inventory(directory, error);
}

QString RuntimeIntegrity::fileVersion(const QString& path)
{
#ifdef Q_OS_WIN
    const auto nativePath = QDir::toNativeSeparators(path).toStdWString();
    DWORD unused = 0;
    const auto size = GetFileVersionInfoSizeW(nativePath.c_str(), &unused);
    if (!size || size > 1024 * 1024) return {};
    QByteArray bytes(static_cast<qsizetype>(size), '\0');
    if (!GetFileVersionInfoW(nativePath.c_str(), 0, size, bytes.data())) return {};
    VS_FIXEDFILEINFO* info = nullptr;
    UINT length = 0;
    if (!VerQueryValueW(bytes.data(), L"\\", reinterpret_cast<void**>(&info), &length) ||
        length < sizeof(VS_FIXEDFILEINFO) || info->dwSignature != 0xfeef04bd) return {};
    QString result = QString("%1.%2.%3").arg(HIWORD(info->dwFileVersionMS))
        .arg(LOWORD(info->dwFileVersionMS)).arg(HIWORD(info->dwFileVersionLS));
    if (LOWORD(info->dwFileVersionLS)) result += QString(".%1").arg(LOWORD(info->dwFileVersionLS));
    return result;
#else
    Q_UNUSED(path);
    return {};
#endif
}

IntegrityReport RuntimeIntegrity::check(const QString& directory, const Cancel& cancel)
{
    IntegrityReport report;
    report.directory = QDir(directory).absolutePath();
    report.checkedAt = QDateTime::currentDateTimeUtc();
    const auto discovered = inventory(report.directory, report.inventoryError);
    auto names = discovered;
    QHash<QString, QJsonObject> expected;
    QFile baseline(QDir(report.directory).filePath(baselineFileName()));
    report.baselineExists = baseline.exists();
    if (!baseline.open(QIODevice::ReadOnly)) {
        report.baselineError = QStringLiteral("Integrity baseline is missing or cannot be read.");
    } else if (baseline.size() > 4 * 1024 * 1024 || isLinked(QFileInfo(baseline))) {
        report.baselineError = QStringLiteral("Integrity baseline is invalid.");
    } else {
        const auto bytes = baseline.readAll();
        report.baselineDigest = QCryptographicHash::hash(bytes, QCryptographicHash::Sha256);
        QJsonParseError error;
        const auto document = QJsonDocument::fromJson(bytes, &error);
        report.baseline = document.object();
        const int schema = report.baseline.value("schemaVersion").toInt();
        bool valid = error.error == QJsonParseError::NoError && document.isObject() &&
            (schema == 1 || schema == 2) && report.baseline.value("algorithm").toString() == "SHA-256" &&
            report.baseline.value("files").isArray();
        for (const auto& value : report.baseline.value("files").toArray()) {
            const auto entry = value.toObject();
            const auto path = entry.value("path").toString();
            // Earlier schema-2 baselines included vendor dependencies. They are
            // outside the controlled plugin scope, including when now missing.
            if (isDllInPluginDirectory(path) && !isPluginPath(path)) continue;
            const auto key = path.toLower();
            valid = valid && value.isObject() && (fileNames().contains(path) || isPluginPath(path)) &&
                !expected.contains(key) && isDigest(entry.value("sha256").toString()) &&
                (schema == 1 || entry.value("version").isString());
            expected.insert(key, entry);
        }
        valid = valid && expected.contains("picoate.ui.exe") && expected.contains("picoatecore.dll");
        if (!valid) {
            expected.clear();
            report.baseline = {};
            report.baselineError = QStringLiteral("Integrity baseline is invalid.");
        } else if (schema == 1) {
            report.baselineError = QStringLiteral("Upgrade the baseline to include component versions and plugin DLLs.");
        }
    }
    QSet<QString> known;
    for (const auto& name : names) known.insert(name.toLower());
    for (auto it = expected.cbegin(); it != expected.cend(); ++it) {
        if (!known.contains(it.key())) names.push_back(it.value().value("path").toString());
    }
    std::sort(names.begin() + 2, names.end(), [](const auto& a, const auto& b) { return a.compare(b, Qt::CaseInsensitive) < 0; });
    for (const auto& name : names) {
        IntegrityFile entry;
        entry.path = name;
        const auto baselineEntry = expected.value(name.toLower());
        entry.expected = baselineEntry.value("sha256").toString().toLower();
        entry.expectedVersion = baselineEntry.value("version").toString();
        const auto path = QDir(report.directory).filePath(name);
        if (!safeParents(report.directory, name)) entry.error = uiText("Plugin directories must not be links or redirected paths.");
        else {
            entry.actual = fileDigest(path, entry.size, cancel, entry.error);
            if (!entry.actual.isEmpty()) entry.actualVersion = fileVersion(path);
        }
        if (entry.actual.isEmpty()) entry.status = QFileInfo::exists(path) ? IntegrityStatus::ReadError : IntegrityStatus::Missing;
        else if (entry.expected.isEmpty()) entry.status = IntegrityStatus::Unverified;
        else entry.status = entry.actual == entry.expected && entry.actualVersion == entry.expectedVersion
                ? IntegrityStatus::Matched : IntegrityStatus::Modified;
        report.files.push_back(entry);
        if (cancel && cancel()) break;
    }
    QString inventoryError;
    if (inventory(report.directory, inventoryError) != discovered || !inventoryError.isEmpty())
        report.inventoryError = uiText("Plugin inventory changed during verification. Check again.");
    report.cancelled = cancel && cancel();
    return report;
}

QString RuntimeIntegrity::pluginAccessError(const IntegrityReport& report, const QStringList& dllPaths)
{
    if (!report.passed()) return integrityFailureText(report);
    const QDir root(report.directory);
    for (const auto& dll : dllPaths) {
        const auto path = root.relativeFilePath(QFileInfo(dll).absoluteFilePath());
        const auto found = std::find_if(report.files.cbegin(), report.files.cend(), [&](const auto& entry) {
            return entry.path.compare(path, Qt::CaseInsensitive) == 0 && entry.status == IntegrityStatus::Matched;
        });
        if (!isPluginPath(path) || found == report.files.cend()) return uiText("Plugin is outside the approved inventory: %1").arg(dll);
    }
    return {};
}

QString RuntimeIntegrity::authorize(const IntegrityReport& reviewed, const QStringList& selected,
                                    AdminAccess access, const QString& password, const QString& reason, const Cancel& cancel)
{
    if (access != AdminAccess::Supervisor || StartupSupport::adminAccessForPassword(password) != AdminAccess::Supervisor)
        return uiText("Authorization password is invalid or privileges are insufficient.");
    if (reason.trimmed().isEmpty() || selected.isEmpty()) return uiText("Select files and enter an approval reason.");
    QLockFile lock(QDir(reviewed.directory).filePath(baselineFileName() + ".lock"));
    if (!lock.tryLock(0)) return uiText("Integrity baseline is busy. Try again.");
    const auto current = check(reviewed.directory, cancel);
    if (current.cancelled) return uiText("Verification cancelled.");
    if (!current.inventoryError.isEmpty()) return current.inventoryError;
    if (current.baselineExists != reviewed.baselineExists || current.baselineDigest != reviewed.baselineDigest ||
        current.files.size() != reviewed.files.size()) return uiText("Files or baseline changed since verification. Check again.");
    QSet<QString> names;
    for (int i = 0; i < current.files.size(); ++i) {
        const auto& file = current.files[i];
        names.insert(file.path);
        if (file.path != reviewed.files[i].path || file.actual != reviewed.files[i].actual ||
            file.actualVersion != reviewed.files[i].actualVersion || file.status != reviewed.files[i].status)
            return uiText("Files or baseline changed since verification. Check again.");
        if (!isDigest(file.actual) && !(file.status == IntegrityStatus::Missing && isPluginPath(file.path) && !file.expected.isEmpty()))
            return uiText("Restore missing or unreadable files before approval.");
    }
    const QSet<QString> approved(selected.cbegin(), selected.cend());
    auto unknown = approved;
    if (!unknown.subtract(names).isEmpty()) return uiText("Invalid file selection.");
    if (!current.baselineError.isEmpty() && approved.size() != names.size())
        return uiText("Select all files to establish or upgrade the baseline.");
    auto baseline = current.baseline;
    QJsonArray files;
    QJsonArray changes;
    for (const auto& file : current.files) {
        const bool accept = approved.contains(file.path);
        const auto hash = accept ? file.actual : file.expected;
        const auto version = accept ? file.actualVersion : file.expectedVersion;
        if (!hash.isEmpty()) files.push_back(QJsonObject{{"path", file.path}, {"sha256", hash}, {"version", version}});
        if (accept && (file.actual != file.expected || file.actualVersion != file.expectedVersion)) {
            changes.push_back(QJsonObject{{"path", file.path}, {"before", file.expected}, {"after", file.actual},
                {"versionBefore", file.expectedVersion}, {"versionAfter", file.actualVersion},
                {"action", file.actual.isEmpty() ? "remove" : file.expected.isEmpty() ? "add" : "update"}});
        }
    }
    const auto now = QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs);
    const auto user = qEnvironmentVariable("USERNAME");
    auto history = baseline.value("history").toArray();
    history.push_back(QJsonObject{{"atUtc", now}, {"user", user}, {"authority", "dailyAdmin"},
        {"reason", reason.trimmed()}, {"previousBaselineSha256", QString::fromLatin1(current.baselineDigest.toHex())}, {"changes", changes}});
    while (history.size() > 100) history.removeFirst();
    baseline.insert("schemaVersion", 2);
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
    case IntegrityStatus::Unverified: return uiText("Not approved");
    }
    return {};
}

QString integrityFailureText(const IntegrityReport& report)
{
    QStringList errors;
    if (report.cancelled) errors.push_back(uiText("Verification cancelled."));
    if (!report.baselineError.isEmpty()) errors.push_back(uiText(report.baselineError.toUtf8().constData()));
    if (!report.inventoryError.isEmpty()) errors.push_back(report.inventoryError);
    for (const auto& file : report.files) {
        if (file.status != IntegrityStatus::Matched) errors.push_back(file.path + ": " + integrityStatusText(file.status));
    }
    return errors.join(QStringLiteral("\n"));
}
} // namespace PicoATE::Ui
