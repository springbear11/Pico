#include "RunInformation.h"

#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QSysInfo>

namespace PicoATE::Ui {

QString computerStationId()
{
    const auto name = QSysInfo::machineHostName().trimmed();
    return name.isEmpty() ? qEnvironmentVariable("COMPUTERNAME").trimmed() : name;
}

QVariantMap RunInformation::variables() const
{
    return {{"stationId", stationId}, {"model", model}, {"customerId", customerId},
            {"order", order}, {"tester", tester}, {"jigNo", jigNo}};
}

bool saveStationModel(const QString& stationPath, const QString& model, QString* error)
{
    QFile source(stationPath);
    if (!source.open(QIODevice::ReadOnly)) {
        if (error) *error = source.errorString();
        return false;
    }
    QJsonParseError parse;
    const auto document = QJsonDocument::fromJson(source.readAll(), &parse);
    source.close();
    if (parse.error != QJsonParseError::NoError || !document.isObject()) {
        if (error) *error = parse.error != QJsonParseError::NoError
            ? parse.errorString() : QStringLiteral("Station JSON must be an object");
        return false;
    }
    auto root = document.object();
    root.insert(QStringLiteral("model"), model.trimmed());
    QSaveFile destination(stationPath);
    if (!destination.open(QIODevice::WriteOnly)) {
        if (error) *error = destination.errorString();
        return false;
    }
    const auto bytes = QJsonDocument(root).toJson(QJsonDocument::Indented);
    if (destination.write(bytes) != bytes.size() || !destination.commit()) {
        if (error) *error = destination.errorString();
        return false;
    }
    return true;
}

} // namespace PicoATE::Ui
