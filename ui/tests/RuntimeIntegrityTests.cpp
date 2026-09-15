#include "RuntimeIntegrity.h"
#include "CoreExecutionService.h"
#include "PluginCatalog.h"

#include <QFile>
#include <QDir>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLockFile>
#include <QTemporaryDir>
#include <QtTest>

using namespace PicoATE::Ui;

namespace {
bool writeFile(const QString& path, const QByteArray& bytes)
{
    if (!QDir().mkpath(QFileInfo(path).absolutePath())) return false;
    QFile file(path);
    return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size();
}
QByteArray readFile(const QString& path)
{
    QFile file(path);
    return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray{};
}
QString password() { return QString::number(StartupSupport::dailyAdminPassword()); }
void createRuntime(const QString& root)
{
    QVERIFY(writeFile(root + "/PicoATE.UI.exe", "test-ui"));
    QVERIFY(writeFile(root + "/PicoATECore.dll", "test-core"));
}
QString approve(const IntegrityReport& report, QStringList names = {})
{
    if (names.isEmpty()) for (const auto& file : report.files) names.push_back(file.path);
    return RuntimeIntegrity::authorize(report, names, AdminAccess::Supervisor, password(), "Unit test approval");
}
}

class RuntimeIntegrityTests final : public QObject {
    Q_OBJECT
private slots:
    void privilegeIsNotJustAdmin();
    void missingBaselineNeverAutoApproves();
    void authorizationRequiresRoleAndDailyPassword();
    void selectedApprovalDoesNotAcceptOtherChangedFile();
    void missingAndStaleFilesCannotBeApproved();
    void malformedBaselineCannotPass();
    void concurrentAndCancelledUpdatesDoNotWrite();
    void outOfScopeFilesAreIgnored();
    void vendorLibrariesAndLegacyEntriesAreIgnored();
    void runChecksBeforeExecutionEveryTime();
    void addedPluginsRequireIndividualApproval();
    void missingPluginsRequireExplicitRetirement();
    void versionChangesAndLegacyBaselineNeedApproval();
    void scanAndRunRejectUnapprovedAndExternalPlugins();
    void pluginInventoryChangesInvalidateReview();
    void releaseComponentVersions();
};

void RuntimeIntegrityTests::privilegeIsNotJustAdmin()
{
    const QDate date(2026, 9, 9);
    QCOMPARE(StartupSupport::adminAccessForPassword("300693", date), AdminAccess::Standard);
    QCOMPARE(StartupSupport::adminAccessForPassword(QString::number(StartupSupport::dailyAdminPassword(date)), date), AdminAccess::Supervisor);
    QCOMPARE(StartupSupport::adminAccessForPassword("wrong", date), AdminAccess::None);
    QCOMPARE(StartupSupport::adminAccessForPassword("-1", QDate{}), AdminAccess::None);
    QVERIFY(StartupSupport::matchesAdminPassword("300693", date));
}

void RuntimeIntegrityTests::missingBaselineNeverAutoApproves()
{
    QTemporaryDir dir;
    createRuntime(dir.path());
    const auto result = RuntimeIntegrity::check(dir.path());
    QVERIFY(!result.passed());
    QCOMPARE(result.files.size(), 2);
    QCOMPARE(result.files[0].status, IntegrityStatus::Unverified);
    QVERIFY(!QFile::exists(dir.filePath(RuntimeIntegrity::baselineFileName())));
    QVERIFY(!approve(result, {"PicoATE.UI.exe"}).isEmpty());
    QVERIFY(approve(result).isEmpty());
    QVERIFY(RuntimeIntegrity::check(dir.path()).passed());
}

void RuntimeIntegrityTests::authorizationRequiresRoleAndDailyPassword()
{
    QTemporaryDir dir;
    createRuntime(dir.path());
    const auto result = RuntimeIntegrity::check(dir.path());
    const auto names = RuntimeIntegrity::fileNames();
    QVERIFY(!RuntimeIntegrity::authorize(result, names, AdminAccess::Standard, password(), "test").isEmpty());
    QVERIFY(!RuntimeIntegrity::authorize(result, names, AdminAccess::Supervisor, "300693", "test").isEmpty());
    QVERIFY(!RuntimeIntegrity::authorize(result, names, AdminAccess::Supervisor, password(), "").isEmpty());
    QVERIFY(!QFile::exists(dir.filePath(RuntimeIntegrity::baselineFileName())));
    QVERIFY(approve(result).isEmpty());
    const auto baseline = RuntimeIntegrity::check(dir.path()).baseline;
    const auto entry = baseline.value("history").toArray().first().toObject();
    QCOMPARE(entry.value("authority").toString(), QString("dailyAdmin"));
    QVERIFY(!entry.contains("password"));
    QCOMPARE(entry.value("changes").toArray().size(), 2);
}

void RuntimeIntegrityTests::selectedApprovalDoesNotAcceptOtherChangedFile()
{
    QTemporaryDir dir;
    createRuntime(dir.path());
    QVERIFY(approve(RuntimeIntegrity::check(dir.path())).isEmpty());
    QVERIFY(writeFile(dir.filePath("PicoATE.UI.exe"), "updated-ui"));
    QVERIFY(writeFile(dir.filePath("PicoATECore.dll"), "updated-core"));
    auto result = RuntimeIntegrity::check(dir.path());
    QCOMPARE(result.files[0].status, IntegrityStatus::Modified);
    QCOMPARE(result.files[1].status, IntegrityStatus::Modified);
    QVERIFY(approve(result, {"PicoATE.UI.exe"}).isEmpty());
    result = RuntimeIntegrity::check(dir.path());
    QCOMPARE(result.files[0].status, IntegrityStatus::Matched);
    QCOMPARE(result.files[1].status, IntegrityStatus::Modified);
    QVERIFY(!result.passed());
    QVERIFY(approve(result, {"PicoATECore.dll"}).isEmpty());
    QVERIFY(RuntimeIntegrity::check(dir.path()).passed());
}

void RuntimeIntegrityTests::missingAndStaleFilesCannotBeApproved()
{
    QTemporaryDir dir;
    createRuntime(dir.path());
    QVERIFY(approve(RuntimeIntegrity::check(dir.path())).isEmpty());
    const auto snapshot = RuntimeIntegrity::check(dir.path());
    const auto baselinePath = dir.filePath(RuntimeIntegrity::baselineFileName());
    const auto before = readFile(baselinePath);
    QVERIFY(writeFile(dir.filePath("PicoATE.UI.exe"), "changed-after-review"));
    QVERIFY(!approve(snapshot).isEmpty());
    QCOMPARE(readFile(baselinePath), before);
    QVERIFY(QFile::remove(dir.filePath("PicoATECore.dll")));
    const auto missing = RuntimeIntegrity::check(dir.path());
    QCOMPARE(missing.files[1].status, IntegrityStatus::Missing);
    QVERIFY(!approve(missing).isEmpty());
    QCOMPARE(readFile(baselinePath), before);
    createRuntime(dir.path());
    const auto reviewed = RuntimeIntegrity::check(dir.path());
    QVERIFY(writeFile(baselinePath, before + "\n"));
    QVERIFY(!approve(reviewed).isEmpty());
}

void RuntimeIntegrityTests::malformedBaselineCannotPass()
{
    QTemporaryDir dir;
    createRuntime(dir.path());
    const auto path = dir.filePath(RuntimeIntegrity::baselineFileName());
    for (const auto& bytes : {QByteArray("{}"), QByteArray("not json"), QByteArray("[]")}) {
        QVERIFY(writeFile(path, bytes));
        QVERIFY(!RuntimeIntegrity::check(dir.path()).passed());
    }
    QVERIFY(approve(RuntimeIntegrity::check(dir.path())).isEmpty());
    auto original = RuntimeIntegrity::check(dir.path()).baseline;
    auto files = original.value("files").toArray();
    auto bad = files[0].toObject();
    bad.insert("path", "../outside.exe");
    files[0] = bad;
    original.insert("files", files);
    QVERIFY(writeFile(path, QJsonDocument(original).toJson()));
    const auto report = RuntimeIntegrity::check(dir.path());
    QVERIFY(!report.passed());
    QVERIFY(!report.baselineError.isEmpty());
    QVERIFY(!approve(report, {"../outside.exe"}).isEmpty());
}

void RuntimeIntegrityTests::concurrentAndCancelledUpdatesDoNotWrite()
{
    QTemporaryDir dir;
    createRuntime(dir.path());
    const auto result = RuntimeIntegrity::check(dir.path());
    QLockFile lock(dir.filePath(RuntimeIntegrity::baselineFileName() + ".lock"));
    QVERIFY(lock.tryLock());
    QVERIFY(!approve(result).isEmpty());
    lock.unlock();
    QVERIFY(!RuntimeIntegrity::authorize(result, RuntimeIntegrity::fileNames(), AdminAccess::Supervisor,
        password(), "test", [] { return true; }).isEmpty());
    QVERIFY(!QFile::exists(dir.filePath(RuntimeIntegrity::baselineFileName())));
    QVERIFY(RuntimeIntegrity::check(dir.path(), [] { return true; }).cancelled);
}

void RuntimeIntegrityTests::outOfScopeFilesAreIgnored()
{
    QTemporaryDir dir;
    createRuntime(dir.path());
    QVERIFY(approve(RuntimeIntegrity::check(dir.path())).isEmpty());
    QVERIFY(writeFile(dir.filePath("StationSystem.json"), "changed station"));
    QVERIFY(writeFile(dir.filePath("sequence.json"), "changed sequence"));
    QVERIFY(writeFile(dir.filePath("Plugin.dll"), "changed plugin"));
    const auto result = RuntimeIntegrity::check(dir.path());
    QVERIFY(result.passed());
    QCOMPARE(result.files.size(), 2);
}

void RuntimeIntegrityTests::vendorLibrariesAndLegacyEntriesAreIgnored()
{
    QTemporaryDir dir;
    createRuntime(dir.path());
    QVERIFY(writeFile(dir.filePath("plugins/PicoATE.Driver.dll"), "pico plugin"));
    QVERIFY(approve(RuntimeIntegrity::check(dir.path())).isEmpty());
    QVERIFY(writeFile(dir.filePath("plugins/ControlCAN.dll"), "vendor driver"));
    QVERIFY(writeFile(dir.filePath("plugins/vendor/NewDriver.DLL"), "new dependency"));
    QVERIFY(writeFile(dir.filePath("plugins/PicoATE.dll"), "not a PicoATE.*.dll plugin"));
    auto baseline = RuntimeIntegrity::check(dir.path()).baseline;
    auto files = baseline.value("files").toArray();
    for (const auto* path : {"plugins/ControlCAN.dll", "plugins/vendor/ECanVci64.dll"}) {
        files.push_back(QJsonObject{{"path", path}, {"sha256", QString(64, '0')}, {"version", "99.0.0"}});
    }
    baseline["files"] = files;
    const auto baselinePath = dir.filePath(RuntimeIntegrity::baselineFileName());
    const auto legacyBytes = QJsonDocument(baseline).toJson();
    QVERIFY(writeFile(baselinePath, legacyBytes));
    auto report = RuntimeIntegrity::check(dir.path());
    QVERIFY(report.passed());
    QCOMPARE(report.files.size(), 3);
    QCOMPARE(RuntimeIntegrity::fileNames(dir.path()).size(), 3);
    QCOMPARE(readFile(baselinePath), legacyBytes);
    QVERIFY(writeFile(dir.filePath("plugins/ControlCAN.dll"), "replaced vendor binary"));
    QVERIFY(RuntimeIntegrity::check(dir.path()).passed());
    QVERIFY(QFile::remove(dir.filePath("plugins/ControlCAN.dll")));
    report = RuntimeIntegrity::check(dir.path());
    QVERIFY(report.passed());
    QVERIFY(!approve(report, {"plugins/ControlCAN.dll"}).isEmpty());
    QVERIFY(approve(report, {"PicoATE.UI.exe"}).isEmpty());
    report = RuntimeIntegrity::check(dir.path());
    QCOMPARE(report.baseline.value("files").toArray().size(), 3);
    QVERIFY(report.passed());
    QVERIFY(writeFile(dir.filePath("plugins/PicoATE.Driver.dll"), "replaced pico binary"));
    QVERIFY(!RuntimeIntegrity::check(dir.path()).passed());
    QCOMPARE(RuntimeIntegrity::check(dir.path()).files[2].status, IntegrityStatus::Modified);
}

void RuntimeIntegrityTests::runChecksBeforeExecutionEveryTime()
{
    QTemporaryDir dir;
    createRuntime(dir.path());
    CoreExecutionService service(dir.path());
    CompileRequest compile;
    compile.sequencePath = dir.filePath("sequence.json");
    compile.sequenceJson = R"({"id":"integrity","name":"Integrity","groups":[{"id":"main","kind":"main","steps":[{"id":"done","kind":"noop"}]}]})";
    QVERIFY(service.compile(compile).success);
    RunRequest request;
    request.runtimeIntegrityDirectory = dir.path();
    const auto token = std::make_shared<PicoATE::Core::StopToken>();
    const auto missing = service.run(request, token);
    QVERIFY(!missing.executed);
    QVERIFY(!missing.diagnostics.isEmpty());
    QVERIFY(missing.report.uuts.isEmpty());
    QVERIFY(approve(RuntimeIntegrity::check(dir.path())).isEmpty());
    QVERIFY(service.run(request, token).executed);
    QVERIFY(writeFile(dir.filePath("PicoATECore.dll"), "new core"));
    const auto changed = service.run(request, token);
    QVERIFY(!changed.executed);
    QVERIFY(changed.report.uuts.isEmpty());
    QVERIFY(approve(RuntimeIntegrity::check(dir.path())).isEmpty());
    QVERIFY(service.run(request, token).executed);
    request.runtimeIntegrityDirectory.clear();
    QVERIFY(QFile::remove(dir.filePath(RuntimeIntegrity::baselineFileName())));
    QVERIFY(service.run(request, token).executed);
}

void RuntimeIntegrityTests::addedPluginsRequireIndividualApproval()
{
    QTemporaryDir dir;
    createRuntime(dir.path());
    QVERIFY(approve(RuntimeIntegrity::check(dir.path())).isEmpty());
    QVERIFY(writeFile(dir.filePath("plugins/PicoATE.New.dll"), "new plugin"));
    QVERIFY(writeFile(dir.filePath("plugins/subfolder/PicoATE.Second.DLL"), "second plugin"));
    auto report = RuntimeIntegrity::check(dir.path());
    QCOMPARE(report.files.size(), 4);
    QCOMPARE(report.files[2].status, IntegrityStatus::Unverified);
    QVERIFY(!report.passed());
    QVERIFY(!RuntimeIntegrity::authorize(report, {"plugins/PicoATE.New.dll"}, AdminAccess::Standard,
        "300693", "new plugin").isEmpty());
    QVERIFY(approve(report, {"plugins/PicoATE.New.dll"}).isEmpty());
    report = RuntimeIntegrity::check(dir.path());
    QCOMPARE(report.files[2].status, IntegrityStatus::Matched);
    QCOMPARE(report.files[3].status, IntegrityStatus::Unverified);
    QCOMPARE(report.baseline.value("files").toArray().size(), 3);
    QVERIFY(!report.passed());
    QVERIFY(approve(report, {"plugins/subfolder/PicoATE.Second.DLL"}).isEmpty());
    QVERIFY(RuntimeIntegrity::check(dir.path()).passed());
    QVERIFY(writeFile(dir.filePath("plugins/PicoATE.New.dll"), "replacement"));
    QCOMPARE(RuntimeIntegrity::check(dir.path()).files[2].status, IntegrityStatus::Modified);
    QVERIFY(!RuntimeIntegrity::check(dir.path()).passed());
}

void RuntimeIntegrityTests::missingPluginsRequireExplicitRetirement()
{
    QTemporaryDir dir;
    createRuntime(dir.path());
    QVERIFY(writeFile(dir.filePath("plugins/PicoATE.Old.dll"), "old"));
    QVERIFY(approve(RuntimeIntegrity::check(dir.path())).isEmpty());
    QVERIFY(QFile::remove(dir.filePath("plugins/PicoATE.Old.dll")));
    auto report = RuntimeIntegrity::check(dir.path());
    QCOMPARE(report.files.size(), 3);
    QCOMPARE(report.files[2].status, IntegrityStatus::Missing);
    QVERIFY(!report.passed());
    QVERIFY(approve(report, {"PicoATE.UI.exe"}).isEmpty());
    report = RuntimeIntegrity::check(dir.path());
    QVERIFY(!report.passed());
    QVERIFY(approve(report, {"plugins/PicoATE.Old.dll"}).isEmpty());
    report = RuntimeIntegrity::check(dir.path());
    QVERIFY(report.passed());
    QCOMPARE(report.files.size(), 2);
    const auto change = report.baseline.value("history").toArray().last().toObject().value("changes").toArray().first().toObject();
    QCOMPARE(change.value("action").toString(), QString("remove"));
    QCOMPARE(change.value("path").toString(), QString("plugins/PicoATE.Old.dll"));
    QVERIFY(QFile::remove(dir.filePath("PicoATECore.dll")));
    QVERIFY(!approve(RuntimeIntegrity::check(dir.path()), {"PicoATECore.dll"}).isEmpty());
}

void RuntimeIntegrityTests::versionChangesAndLegacyBaselineNeedApproval()
{
    QTemporaryDir dir;
    createRuntime(dir.path());
    QVERIFY(approve(RuntimeIntegrity::check(dir.path())).isEmpty());
    auto baseline = RuntimeIntegrity::check(dir.path()).baseline;
    auto files = baseline.value("files").toArray();
    auto entry = files[0].toObject();
    entry["version"] = "99.0.0";
    files[0] = entry;
    baseline["files"] = files;
    const auto path = dir.filePath(RuntimeIntegrity::baselineFileName());
    QVERIFY(writeFile(path, QJsonDocument(baseline).toJson()));
    auto report = RuntimeIntegrity::check(dir.path());
    QCOMPARE(report.files[0].status, IntegrityStatus::Modified);
    QCOMPARE(report.files[0].expected, report.files[0].actual);
    QVERIFY(!report.passed());
    QVERIFY(approve(report, {"PicoATE.UI.exe"}).isEmpty());
    baseline = RuntimeIntegrity::check(dir.path()).baseline;
    baseline["schemaVersion"] = 1;
    QVERIFY(writeFile(path, QJsonDocument(baseline).toJson()));
    QVERIFY(writeFile(dir.filePath("plugins/PicoATE.New.dll"), "new plugin"));
    report = RuntimeIntegrity::check(dir.path());
    QVERIFY(!report.passed());
    QVERIFY(!report.baselineError.isEmpty());
    QVERIFY(!approve(report, {"PicoATE.UI.exe", "PicoATECore.dll"}).isEmpty());
    QVERIFY(approve(report).isEmpty());
    report = RuntimeIntegrity::check(dir.path());
    QVERIFY(report.passed());
    QCOMPARE(report.baseline.value("schemaVersion").toInt(), 2);
    QCOMPARE(report.files.size(), 3);
}

void RuntimeIntegrityTests::scanAndRunRejectUnapprovedAndExternalPlugins()
{
    QTemporaryDir dir;
    createRuntime(dir.path());
    QVERIFY(approve(RuntimeIntegrity::check(dir.path())).isEmpty());
    CoreExecutionService service(dir.path());
    CompileRequest compile;
    compile.sequencePath = dir.filePath("sequence.json");
    compile.sequenceJson = R"({"id":"guard","name":"Guard","groups":[{"id":"main","kind":"main","steps":[{"id":"done","kind":"noop"}]}]})";
    QVERIFY(service.compile(compile).success);
    RunRequest run;
    run.runtimeIntegrityDirectory = dir.path();
    QVERIFY(service.run(run, std::make_shared<PicoATE::Core::StopToken>()).executed);
    const auto plugin = dir.filePath("plugins/PicoATE.New.dll");
    QVERIFY(writeFile(plugin, "not executable"));
    const auto registry = dir.filePath("plugins/PluginRegistry.json");
    QVERIFY(writeFile(registry, R"({"plugins":[]})"));
    const auto before = readFile(registry);
    const auto scanned = PluginCatalog::scanPlugins(dir.filePath("plugins"), "missing-host.exe", registry, 100, dir.path());
    QVERIFY(!scanned.ok());
    QCOMPARE(scanned.errors.first().path, RuntimeIntegrity::baselineFileName());
    QVERIFY(!scanned.registrySaved);
    QCOMPARE(readFile(registry), before);
    const auto blocked = service.run(run, std::make_shared<PicoATE::Core::StopToken>());
    QVERIFY(!blocked.executed);
    QVERIFY(blocked.report.uuts.isEmpty());
    QVERIFY(approve(RuntimeIntegrity::check(dir.path())).isEmpty());
    QVERIFY(service.run(run, std::make_shared<PicoATE::Core::StopToken>()).executed);
    const auto report = RuntimeIntegrity::check(dir.path());
    QVERIFY(RuntimeIntegrity::pluginAccessError(report, {plugin}).isEmpty());
    QVERIFY(writeFile(dir.filePath("outside.dll"), "outside"));
    QVERIFY(!RuntimeIntegrity::pluginAccessError(report, {dir.filePath("outside.dll")}).isEmpty());
    QVERIFY(writeFile(registry, R"({"plugins":[{"moduleId":"plugin.new","dll":"../outside.dll"}]})"));
    QVERIFY(!service.run(run, std::make_shared<PicoATE::Core::StopToken>()).executed);
}

void RuntimeIntegrityTests::pluginInventoryChangesInvalidateReview()
{
    QTemporaryDir dir;
    createRuntime(dir.path());
    QVERIFY(approve(RuntimeIntegrity::check(dir.path())).isEmpty());
    const auto reviewed = RuntimeIntegrity::check(dir.path());
    const auto baseline = readFile(dir.filePath(RuntimeIntegrity::baselineFileName()));
    QVERIFY(writeFile(dir.filePath("plugins/PicoATE.Later.dll"), "added after review"));
    QVERIFY(!approve(reviewed).isEmpty());
    QCOMPARE(readFile(dir.filePath(RuntimeIntegrity::baselineFileName())), baseline);
    auto report = RuntimeIntegrity::check(dir.path());
    QVERIFY(approve(report).isEmpty());
    report = RuntimeIntegrity::check(dir.path());
    QVERIFY(QFile::rename(dir.filePath("plugins/PicoATE.Later.dll"), dir.filePath("plugins/PicoATE.Renamed.dll")));
    QVERIFY(!approve(report).isEmpty());
}

void RuntimeIntegrityTests::releaseComponentVersions()
{
    const auto root = qEnvironmentVariable("PICOATE_VERSIONED_RUNTIME");
    if (root.isEmpty()) QSKIP("Set PICOATE_VERSIONED_RUNTIME after building release components");
    QCOMPARE(RuntimeIntegrity::fileVersion(QDir(root).filePath("PicoATE.UI.exe")), QString("1.0.0"));
    QCOMPARE(RuntimeIntegrity::fileVersion(QDir(root).filePath("PicoATECore.dll")), QString("1.0.0"));
    int plugins = 0;
    for (const auto& path : RuntimeIntegrity::fileNames(root)) {
        if (path.startsWith("plugins/") && QFileInfo(path).fileName().startsWith("PicoATE.")) {
            QCOMPARE(RuntimeIntegrity::fileVersion(QDir(root).filePath(path)), QString("1.0.0"));
            ++plugins;
        }
    }
    QVERIFY(plugins >= 9);
    QCOMPARE(RuntimeIntegrity::fileNames(root).size(), plugins + 2);
    const auto generatedBaseline = qEnvironmentVariable("PICOATE_GENERATED_BASELINE_RUNTIME");
    if (!generatedBaseline.isEmpty()) {
        const auto report = RuntimeIntegrity::check(generatedBaseline);
        QVERIFY2(report.passed(), qPrintable(integrityFailureText(report)));
        QCOMPARE(report.files.size(), RuntimeIntegrity::fileNames(generatedBaseline).size());
    }
}

QTEST_GUILESS_MAIN(RuntimeIntegrityTests)
#include "RuntimeIntegrityTests.moc"
