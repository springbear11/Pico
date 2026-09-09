#include "RuntimeIntegrity.h"
#include "CoreExecutionService.h"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLockFile>
#include <QTemporaryDir>
#include <QtTest>

using namespace PicoATE::Ui;

namespace {
bool writeFile(const QString& path, const QByteArray& bytes)
{
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
QString approve(const IntegrityReport& report, const QStringList& names = RuntimeIntegrity::fileNames())
{
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
    void runChecksBeforeExecutionEveryTime();
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

QTEST_GUILESS_MAIN(RuntimeIntegrityTests)
#include "RuntimeIntegrityTests.moc"
