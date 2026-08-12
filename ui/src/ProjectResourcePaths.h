#pragma once

#include <QString>
#include <QStringList>

namespace PicoATE::Ui::ProjectResourcePaths {

QString projectDirectoryForSequence(const QString& sequencePath);
QString imagesDirectoryForSequence(const QString& sequencePath);
QString resolveImage(const QString& sequencePath,
                     const QString& configuredImage);
QStringList availableImages(const QString& sequencePath);

} // namespace PicoATE::Ui::ProjectResourcePaths
