#include "ProjectResourcePaths.h"

#include <QDir>
#include <QFileInfo>

namespace PicoATE::Ui::ProjectResourcePaths {

namespace {

QString normalizedRelativeImage(QString configuredImage)
{
    configuredImage = QDir::fromNativeSeparators(configuredImage.trimmed());
    while (configuredImage.startsWith(QStringLiteral("./"))) {
        configuredImage.remove(0, 2);
    }
    if (configuredImage.compare(QStringLiteral("image"), Qt::CaseInsensitive) == 0 ||
        configuredImage.compare(QStringLiteral("images"), Qt::CaseInsensitive) == 0) {
        return {};
    }
    if (configuredImage.startsWith(QStringLiteral("image/"),
                                   Qt::CaseInsensitive)) {
        configuredImage.remove(0, QStringLiteral("image/").size());
    } else if (configuredImage.startsWith(QStringLiteral("images/"),
                                          Qt::CaseInsensitive)) {
        configuredImage.remove(0, QStringLiteral("images/").size());
    }
    return configuredImage;
}

} // namespace

QString projectDirectoryForSequence(const QString& sequencePath)
{
    const auto configured = sequencePath.trimmed();
    if (configured.isEmpty()) {
        return {};
    }
    return QFileInfo(configured).absoluteDir().absolutePath();
}

QString imagesDirectoryForSequence(const QString& sequencePath)
{
    const auto projectDirectory = projectDirectoryForSequence(sequencePath);
    return projectDirectory.isEmpty()
        ? QString{}
        : QDir(projectDirectory).absoluteFilePath(QStringLiteral("images"));
}

QString resolveImage(const QString& sequencePath,
                     const QString& configuredImage)
{
    const auto configured = configuredImage.trimmed();
    if (configured.isEmpty()) {
        return {};
    }

    const QFileInfo direct(configured);
    if (direct.isAbsolute()) {
        return direct.absoluteFilePath();
    }

    const auto imagesDirectory = imagesDirectoryForSequence(sequencePath);
    if (imagesDirectory.isEmpty()) {
        return {};
    }
    const auto relativeImage = normalizedRelativeImage(configured);
    if (relativeImage.isEmpty()) {
        return imagesDirectory;
    }

    const auto cleanRoot = QDir::cleanPath(imagesDirectory);
    const auto candidate = QDir::cleanPath(
        QDir(cleanRoot).absoluteFilePath(relativeImage));
#if defined(Q_OS_WIN)
    constexpr auto pathSensitivity = Qt::CaseInsensitive;
#else
    constexpr auto pathSensitivity = Qt::CaseSensitive;
#endif
    const auto rootPrefix = cleanRoot.endsWith(QLatin1Char('/'))
        ? cleanRoot
        : cleanRoot + QLatin1Char('/');
    return candidate.startsWith(rootPrefix, pathSensitivity)
        ? candidate
        : QString{};
}

QStringList availableImages(const QString& sequencePath)
{
    const auto imagesDirectory = imagesDirectoryForSequence(sequencePath);
    if (imagesDirectory.isEmpty()) {
        return {};
    }
    const QDir directory(imagesDirectory);
    if (!directory.exists()) {
        return {};
    }
    return directory.entryList(
        {QStringLiteral("*.png"), QStringLiteral("*.jpg"),
         QStringLiteral("*.jpeg")},
        QDir::Files | QDir::Readable,
        QDir::Name | QDir::IgnoreCase);
}

} // namespace PicoATE::Ui::ProjectResourcePaths
