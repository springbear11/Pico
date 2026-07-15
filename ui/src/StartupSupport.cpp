#include "StartupSupport.h"

#include "PicoATE/Core/SequenceCompiler.h"
#include "PicoATE/Core/StationConfig.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>

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
    if (!baseName.contains(QStringLiteral("seq"), Qt::CaseInsensitive)) {
        return false;
    }
    QJsonObject root;
    return readJsonObject(fileInfo.absoluteFilePath(), root) &&
           root.value(QStringLiteral("groups")).isArray();
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
        if (!QFileInfo::exists(stationPath)) {
            result.errors.push_back(QStringLiteral("缺少 StationSystem.json：%1")
                                        .arg(stationPath));
        } else {
            const auto station = PicoATE::Core::loadStationConfigFile(stationPath);
            if (!station.ok()) {
                for (const auto& error : station.errors) {
                    result.errors.push_back(
                        error.path.isEmpty()
                            ? error.message
                            : QStringLiteral("%1：%2").arg(error.path, error.message));
                }
            }
        }
    }

    if (mode == UiMode::Admin &&
        !matchesDailyAdminPassword(adminPassword, date)) {
        result.errors.push_back(QStringLiteral("Admin 密码错误"));
    }
    return result;
}

} // namespace PicoATE::Ui
