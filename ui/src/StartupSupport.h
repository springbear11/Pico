#pragma once

#include <QDate>
#include <QString>
#include <QStringList>

namespace PicoATE::Ui {

enum class UiMode {
    Test,
    Admin
};

struct StartupValidationResult {
    QStringList errors;

    bool ok() const { return errors.isEmpty(); }
};

struct SnValidationRules {
    int exactLength = 0;
    QString wildcardPattern;
    QString allowedRegex;
};

struct SnValidationResult {
    QString errorMessage;

    bool ok() const { return errorMessage.isEmpty(); }
};

class StartupSupport final
{
public:
    static int dailyAdminPassword(const QDate& date = QDate::currentDate());
    static bool matchesDailyAdminPassword(
        const QString& input,
        const QDate& date = QDate::currentDate());

    static QStringList discoverSequenceFiles(const QString& rootDirectory);
    static QString stationPathForSequence(const QString& sequencePath);
    static bool stationScanDialogEnabled(const QString& stationPath,
                                         bool defaultValue = true);
    static int stationSnLength(const QString& stationPath,
                               int defaultValue = 0);
    static SnValidationRules stationSnValidationRules(
        const QString& stationPath);
    static SnValidationResult validateSerialNumber(
        const QString& serialNumber,
        const SnValidationRules& rules);
    static StartupValidationResult validateSelection(
        UiMode mode,
        const QString& sequencePath,
        const QString& stationPath,
        const QString& adminPassword = {},
        const QDate& date = QDate::currentDate());
};

} // namespace PicoATE::Ui
