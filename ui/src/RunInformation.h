#pragma once

#include <QString>
#include <QVariantMap>

namespace PicoATE::Ui {

QString computerStationId();

struct RunInformation {
    QString stationId = computerStationId();
    QString model;
    QString customerId;
    QString order;
    QString tester;
    QString jigNo;

    QVariantMap variables() const;
};

bool saveStationModel(const QString& stationPath, const QString& model, QString* error);

} // namespace PicoATE::Ui
