#pragma once

#include "StartupSupport.h"

#include <QByteArray>
#include <QDateTime>
#include <QJsonObject>
#include <QStringList>
#include <QVector>

#include <functional>

namespace PicoATE::Ui {

enum class IntegrityStatus { Matched, Modified, Missing, Unverified, ReadError };

struct IntegrityFile {
    QString path;
    QString expected;
    QString actual;
    QString error;
    qint64 size = 0;
    IntegrityStatus status = IntegrityStatus::Unverified;
};

struct IntegrityReport {
    QString directory;
    QString baselineError;
    QByteArray baselineDigest;
    QJsonObject baseline;
    QDateTime checkedAt;
    QVector<IntegrityFile> files;
    bool baselineExists = false;
    bool cancelled = false;

    bool passed() const;
};

class RuntimeIntegrity final {
public:
    using Cancel = std::function<bool()>;
    static QString baselineFileName();
    static QStringList fileNames();
    static IntegrityReport check(const QString& directory, const Cancel& cancel = {});
    static QString authorize(const IntegrityReport& reviewed, const QStringList& selected,
                             AdminAccess access, const QString& password, const QString& reason,
                             const Cancel& cancel = {});
};

QString integrityStatusText(IntegrityStatus status);
QString integrityFailureText(const IntegrityReport& report);

} // namespace PicoATE::Ui
