#pragma once

#include <QIcon>
#include <QJsonObject>
#include <QString>

namespace PicoATE::Core {
struct StepReport;
}

namespace PicoATE::Ui {

QIcon functionIcon(const QString& key);
QString functionIconKey(const QJsonObject& step);
QString functionIconKey(const PicoATE::Core::StepReport& step);
QIcon functionIconForStep(const QJsonObject& step);
QIcon functionIconForStep(const PicoATE::Core::StepReport& step);

} // namespace PicoATE::Ui
