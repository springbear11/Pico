#include "PicoATE/Core/VariableResolver.h"

#include <QDir>
#include <QFileInfo>
#include <QMetaType>
#include <QProcessEnvironment>
#include <QRegularExpression>
#include <QSet>
#include <utility>

namespace PicoATE::Core {

namespace {

QString childPath(const QString& path, const QString& key)
{
    return path.isEmpty() ? key : QString("%1.%2").arg(path, key);
}

QString listPath(const QString& path, int index)
{
    return QString("%1[%2]").arg(path.isEmpty() ? QString("<root>") : path).arg(index);
}

} // namespace

VariableResolver::VariableResolver(VariableResolverOptions options)
    : m_options(std::move(options))
{
}

QString VariableResolver::sequenceDir() const
{
    if (!m_options.sequenceFilePath.isEmpty()) {
        return QFileInfo(m_options.sequenceFilePath).absoluteDir().absolutePath();
    }
    return QDir::currentPath();
}

QString VariableResolver::projectDir() const
{
    if (!m_options.projectDir.isEmpty()) {
        return QFileInfo(m_options.projectDir).absoluteFilePath();
    }
    return sequenceDir();
}

bool VariableResolver::variableValue(const QString& name, QString& value) const
{
    if (m_options.variables.contains(name)) {
        value = m_options.variables.value(name);
        return true;
    }
    if (name == "SEQUENCE_DIR") {
        value = sequenceDir();
        return true;
    }
    if (name == "PROJECT_DIR") {
        value = projectDir();
        return true;
    }

    if (m_options.useEnvironment) {
        const auto environment = QProcessEnvironment::systemEnvironment();
        if (environment.contains(name)) {
            value = environment.value(name);
            return true;
        }
    }

    return false;
}

QString VariableResolver::resolveString(const QString& input,
                                        QVector<VariableResolutionError>& errors,
                                        const QString& path) const
{
    static const QRegularExpression pattern(R"(\$\{([A-Za-z_][A-Za-z0-9_]*)\})");

    QString value = input;
    constexpr int maxReplacements = 64;
    int replacements = 0;
    QSet<QString> reportedUnresolved;

    auto match = pattern.match(value);
    while (match.hasMatch()) {
        if (replacements >= maxReplacements) {
            addError(errors,
                     path,
                     {},
                     "Variable resolution exceeded maximum depth",
                     "Check for recursive variable definitions");
            return value;
        }

        const auto variableName = match.captured(1);
        QString replacement;
        if (!variableValue(variableName, replacement)) {
            if (!reportedUnresolved.contains(variableName)) {
                addError(errors,
                         path,
                         variableName,
                         QString("Unresolved variable: %1").arg(variableName),
                         "Provide it through VariableResolverOptions::variables or the environment");
                reportedUnresolved.insert(variableName);
            }
            match = pattern.match(value, match.capturedEnd(0));
            continue;
        }

        value.replace(match.capturedStart(0), match.capturedLength(0), replacement);
        ++replacements;
        match = pattern.match(value);
    }

    return value;
}

QVariant VariableResolver::resolveVariant(const QVariant& value,
                                          QVector<VariableResolutionError>& errors,
                                          const QString& path) const
{
    if (value.metaType().id() == QMetaType::QString) {
        return resolveString(value.toString(), errors, path);
    }
    if (value.metaType().id() == QMetaType::QVariantMap) {
        return resolveMap(value.toMap(), errors, path);
    }
    if (value.metaType().id() == QMetaType::QVariantList) {
        return resolveList(value.toList(), errors, path);
    }
    if (value.canConvert<QStringList>() && value.metaType().id() == QMetaType::QStringList) {
        QVariantList list;
        for (const auto& item : value.toStringList()) {
            list.push_back(item);
        }
        return resolveList(list, errors, path);
    }

    return value;
}

QVariantMap VariableResolver::resolveMap(const QVariantMap& map,
                                         QVector<VariableResolutionError>& errors,
                                         const QString& path) const
{
    QVariantMap resolved;
    for (auto it = map.constBegin(); it != map.constEnd(); ++it) {
        resolved.insert(it.key(), resolveVariant(it.value(), errors, childPath(path, it.key())));
    }
    return resolved;
}

QVariantList VariableResolver::resolveList(const QVariantList& list,
                                           QVector<VariableResolutionError>& errors,
                                           const QString& path) const
{
    QVariantList resolved;
    resolved.reserve(list.size());
    for (int i = 0; i < list.size(); ++i) {
        resolved.push_back(resolveVariant(list[i], errors, listPath(path, i)));
    }
    return resolved;
}

void VariableResolver::addError(QVector<VariableResolutionError>& errors,
                                const QString& path,
                                const QString& variableName,
                                const QString& message,
                                const QString& suggestion) const
{
    errors.push_back({path, variableName, message, suggestion});
}

} // namespace PicoATE::Core
