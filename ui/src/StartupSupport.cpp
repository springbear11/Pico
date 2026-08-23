#include "StartupSupport.h"

#include "PicoATE/Core/SequenceCompiler.h"
#include "PicoATE/Core/ProductRouting.h"
#include "PicoATE/Core/StationConfig.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QRegularExpression>

#include <algorithm>

namespace PicoATE::Ui {

namespace {

bool readJsonObject(const QString& filePath, QJsonObject& object)
{
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly)) {
        return false;
    }
    QJsonParseError error;
    const auto document = QJsonDocument::fromJson(file.readAll(), &error);
    if (error.error != QJsonParseError::NoError || !document.isObject()) {
        return false;
    }
    object = document.object();
    return true;
}

bool isSequenceCandidate(const QFileInfo& fileInfo)
{
    const auto baseName = fileInfo.completeBaseName();
    return baseName.contains(QStringLiteral("seq"), Qt::CaseInsensitive);
}

void appendStationErrors(StartupValidationResult& result,
                         const QString& stationPath)
{
    if (!QFileInfo::exists(stationPath)) {
        result.errors.push_back(QStringLiteral("缺少 StationSystem.json：%1")
                                    .arg(stationPath));
        return;
    }
    const auto station = PicoATE::Core::loadStationConfigFile(stationPath);
    for (const auto& error : station.errors) {
        result.errors.push_back(
            error.path.isEmpty()
                ? error.message
                : QStringLiteral("%1：%2").arg(error.path, error.message));
    }
}

} // namespace

int StartupSupport::dailyAdminPassword(const QDate& date)
{
    if (!date.isValid()) {
        return -1;
    }
    const int month = date.month();
    const int day = date.day();
    const int monthDigitSum = month / 10 + month % 10;
    const int dayDigitSum = day / 10 + day % 10;
    return 33 + monthDigitSum * dayDigitSum;
}

bool StartupSupport::matchesDailyAdminPassword(const QString& input,
                                               const QDate& date)
{
    bool numeric = false;
    const int value = input.trimmed().toInt(&numeric);
    return numeric && value == dailyAdminPassword(date);
}

bool StartupSupport::matchesAdminPassword(const QString& input,
                                          const QDate& date)
{
    return input.trimmed() == QStringLiteral("300693") ||
           matchesDailyAdminPassword(input, date);
}

QStringList StartupSupport::discoverSequenceFiles(const QString& rootDirectory)
{
    QDir root(rootDirectory);
    if (!root.exists()) {
        return {};
    }

    QStringList result;
    const auto files = root.entryInfoList(
        {QStringLiteral("*.json")},
        QDir::Files | QDir::Readable,
        QDir::Name | QDir::IgnoreCase);
    for (const auto& file : files) {
        if (isSequenceCandidate(file)) {
            result.push_back(file.absoluteFilePath());
        }
    }
    return result;
}

QString StartupSupport::stationPathForSequence(const QString& sequencePath)
{
    const QFileInfo sequence(sequencePath);
    return sequence.absoluteDir().filePath(QStringLiteral("StationSystem.json"));
}

QString StartupSupport::stationPathForRoot(const QString& rootDirectory)
{
    return QDir(rootDirectory).absoluteFilePath(QStringLiteral("StationSystem.json"));
}

QString StartupSupport::productRoutingPathForRoot(const QString& rootDirectory)
{
    return QDir(rootDirectory).absoluteFilePath(QStringLiteral("ProductRouting.json"));
}

QString StartupSupport::productProjectRootPathForRoot(
    const QString& rootDirectory)
{
    return QDir(rootDirectory).absoluteFilePath(QStringLiteral("projects"));
}

QJsonObject StartupSupport::newProjectSequenceTemplate()
{
    const auto group = [](const QString& id, const QString& name) {
        return QJsonObject{
            {QStringLiteral("id"), id},
            {QStringLiteral("name"), name},
            {QStringLiteral("kind"), id},
            {QStringLiteral("steps"), QJsonArray{}}};
    };
    return QJsonObject{
        {QStringLiteral("id"), QStringLiteral("NA")},
        {QStringLiteral("name"), QStringLiteral("NA")},
        {QStringLiteral("version"), QStringLiteral("1.0.0")},
        {QStringLiteral("groups"),
         QJsonArray{group(QStringLiteral("setup"), QStringLiteral("Setup")),
                    group(QStringLiteral("main"), QStringLiteral("Main")),
                    group(QStringLiteral("cleanup"), QStringLiteral("Cleanup"))}}};
}

QJsonObject StartupSupport::newProjectStationTemplate()
{
    return QJsonObject{
        {QStringLiteral("stationId"), QStringLiteral("NA")},
        {QStringLiteral("model"), QStringLiteral("NA")},
        {QStringLiteral("customerId"), QStringLiteral("NA")},
        {QStringLiteral("pluginRegistry"),
         QStringLiteral("plugins/PluginRegistry.json")},
        {QStringLiteral("stopOnFailure"), true},
        {QStringLiteral("scanDialogEnabled"), true},
        {QStringLiteral("txtLogEnabled"), true},
        {QStringLiteral("csvReportEnabled"), true},
        {QStringLiteral("xlsxReportEnabled"), true},
        {QStringLiteral("pdfReportEnabled"), true},
        {QStringLiteral("loopTestEnabled"), false},
        {QStringLiteral("loopTestCount"), 1},
        {QStringLiteral("uutCount"), 1},
        {QStringLiteral("reportOutputDirectory"), QString{}},
        {QStringLiteral("snLength"), 0},
        {QStringLiteral("snPattern"), QString{}},
        {QStringLiteral("snAllowedRegex"), QStringLiteral("^[A-Z0-9]+$")},
        {QStringLiteral("metadata"),
         QJsonObject{{QStringLiteral("jigNo"), QStringLiteral("NA")},
                     {QStringLiteral("order"), QStringLiteral("NA")},
                     {QStringLiteral("tester"), QStringLiteral("NA")}}},
        {QStringLiteral("devices"), QJsonArray{}}};
}

bool StartupSupport::stationScanDialogEnabled(const QString& stationPath,
                                              bool defaultValue)
{
    QJsonObject root;
    if (!readJsonObject(stationPath, root)) {
        return defaultValue;
    }
    const auto value = root.value(QStringLiteral("scanDialogEnabled"));
    return value.isBool() ? value.toBool() : defaultValue;
}

int StartupSupport::stationSnLength(const QString& stationPath, int defaultValue)
{
    QJsonObject root;
    if (!readJsonObject(stationPath, root)) {
        return defaultValue;
    }
    const auto value = root.value(QStringLiteral("snLength"));
    if (!value.isDouble()) {
        return defaultValue;
    }
    const int length = value.toInt(defaultValue);
    return length >= 0 && length <= 256 ? length : defaultValue;
}

int StartupSupport::stationUutCount(const QString& stationPath, int defaultValue)
{
    QJsonObject root;
    if (!readJsonObject(stationPath, root)) {
        return defaultValue;
    }
    const auto value = root.value(QStringLiteral("uutCount"));
    if (!value.isDouble()) {
        return defaultValue;
    }
    const int count = value.toInt(defaultValue);
    return count >= 1 && count <= 64 ? count : defaultValue;
}

SnValidationRules StartupSupport::stationSnValidationRules(
    const QString& stationPath)
{
    SnValidationRules rules;
    QJsonObject root;
    if (!readJsonObject(stationPath, root)) {
        return rules;
    }
    rules.exactLength = stationSnLength(stationPath);
    rules.wildcardPattern = root.value(QStringLiteral("snPattern"))
                                .toString().trimmed();
    rules.allowedRegex = root.value(QStringLiteral("snAllowedRegex"))
                             .toString().trimmed();
    return rules;
}

SnValidationResult StartupSupport::validateSerialNumber(
    const QString& serialNumber,
    const SnValidationRules& rules)
{
    const auto sn = serialNumber.trimmed();
    if (sn.isEmpty()) {
        return {QStringLiteral("SN cannot be empty")};
    }
    if (rules.exactLength > 0 && sn.size() != rules.exactLength) {
        return {QStringLiteral("SN must contain exactly %1 characters (current: %2)")
                    .arg(rules.exactLength)
                    .arg(sn.size())};
    }
    if (!rules.wildcardPattern.isEmpty()) {
        const QRegularExpression wildcard(
            QRegularExpression::wildcardToRegularExpression(
                rules.wildcardPattern,
                QRegularExpression::NonPathWildcardConversion));
        if (!wildcard.match(sn).hasMatch()) {
            return {QStringLiteral("SN must match pattern: %1")
                        .arg(rules.wildcardPattern)};
        }
    }
    if (!rules.allowedRegex.isEmpty()) {
        const QRegularExpression allowed(rules.allowedRegex);
        if (!allowed.isValid()) {
            return {QStringLiteral("Invalid SN character regular expression: %1")
                        .arg(allowed.errorString())};
        }
        const auto match = allowed.match(sn);
        if (!match.hasMatch() || match.capturedStart() != 0 ||
            match.capturedLength() != sn.size()) {
            return {QStringLiteral("SN contains characters not allowed by: %1")
                        .arg(rules.allowedRegex)};
        }
    }
    return {};
}

StartupValidationResult StartupSupport::validateSelection(
    UiMode mode,
    const QString& sequencePath,
    const QString& stationPath,
    const QString& adminPassword,
    const QDate& date)
{
    StartupValidationResult result;
    QJsonObject sequence;
    if (!readJsonObject(sequencePath, sequence)) {
        result.errors.push_back(QStringLiteral("测试脚本无法读取或不是有效 JSON：%1")
                                    .arg(sequencePath));
    } else if (!sequence.value(QStringLiteral("groups")).isArray()) {
        result.errors.push_back(QStringLiteral("所选 JSON 不是 PicoATE Sequence：%1")
                                    .arg(sequencePath));
    } else if (mode == UiMode::Test) {
        PicoATE::Core::SequenceCompiler compiler;
        const auto compiled = compiler.compileJson(sequence);
        for (const auto& error : compiled.errors) {
            result.errors.push_back(
                error.path.isEmpty()
                    ? error.message
                    : QStringLiteral("%1：%2").arg(error.path, error.message));
        }
    }

    // Station validity is a production-run gate, not an Admin access gate.
    // Admin must remain available so an invalid or missing Station can be repaired.
    if (mode == UiMode::Test) {
        appendStationErrors(result, stationPath);
    }

    if (mode == UiMode::Admin &&
        !matchesAdminPassword(adminPassword, date)) {
        result.errors.push_back(QStringLiteral("Admin 密码错误"));
    }
    return result;
}

StartupValidationResult StartupSupport::validateAutoSelection(
    UiMode mode,
    const QString& productRoutingPath,
    const QString& stationPath,
    const QString& adminPassword,
    const QDate& date)
{
    StartupValidationResult result;
    const auto routing = PicoATE::Core::loadProductRoutingFile(productRoutingPath);
    for (const auto& error : routing.errors) {
        result.errors.push_back(
            error.path.isEmpty()
                ? error.message
                : QStringLiteral("%1：%2").arg(error.path, error.message));
    }
    if (routing.ok()) {
        const bool hasEnabledRoute = std::any_of(
            routing.config.routes.cbegin(),
            routing.config.routes.cend(),
            [](const auto& route) { return route.enabled; });
        if (!hasEnabledRoute) {
            result.errors.push_back(QStringLiteral(
                "ProductRouting.json 中没有已启用的产品路由"));
        }
    }

    if (mode == UiMode::Test && !stationPath.trimmed().isEmpty()) {
        appendStationErrors(result, stationPath);
    }
    if (mode == UiMode::Admin &&
        !matchesAdminPassword(adminPassword, date)) {
        result.errors.push_back(QStringLiteral("Admin 密码错误"));
    }
    return result;
}

} // namespace PicoATE::Ui
