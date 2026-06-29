#include "PicoATE/Core/RuntimeVariableResolver.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QMetaType>
#include <QRegularExpression>
#include <QStringList>

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

bool isWholeVariableExpression(const QString& value, QString& variableName)
{
    static const QRegularExpression pattern(R"(^\$\{([^}]+)\}$)");
    const auto match = pattern.match(value.trimmed());
    if (!match.hasMatch()) {
        return false;
    }
    variableName = match.captured(1).trimmed();
    return true;
}

} // namespace

RuntimeVariableResolver::RuntimeVariableResolver(RuntimeVariableContext context)
    : m_context(std::move(context))
{
}

bool RuntimeVariableResolver::variableValue(const QString& name, QVariant& value) const
{
    const auto normalized = name.trimmed();
    if (normalized == "uut.id") {
        value = m_context.uutId;
        return true;
    }
    if (normalized == "frame.id") {
        value = m_context.frameId;
        return true;
    }
    if (normalized == "attempt.id") {
        value = m_context.attemptId;
        return true;
    }
    if (normalized == "attempt.index") {
        value = m_context.attemptIndex;
        return true;
    }
    if (normalized == "attempt.number") {
        value = m_context.attemptIndex + 1;
        return true;
    }

    if (normalized.startsWith("var.")) {
        return variableFromMapPath(normalized.mid(4), value);
    }

    if (normalized.startsWith("loop.")) {
        return variableFromMapPath(normalized, value);
    }

    return variableFromMapPath(normalized, value);
}

QVariant RuntimeVariableResolver::resolveVariant(const QVariant& value,
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

QVariantMap RuntimeVariableResolver::resolveMap(const QVariantMap& map,
                                                QVector<VariableResolutionError>& errors,
                                                const QString& path) const
{
    QVariantMap resolved;
    for (auto it = map.constBegin(); it != map.constEnd(); ++it) {
        resolved.insert(it.key(), resolveVariant(it.value(), errors, childPath(path, it.key())));
    }
    return resolved;
}

QVariantList RuntimeVariableResolver::resolveList(const QVariantList& list,
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

QVariant RuntimeVariableResolver::resolveString(const QString& input,
                                                QVector<VariableResolutionError>& errors,
                                                const QString& path) const
{
    QString wholeName;
    if (isWholeVariableExpression(input, wholeName)) {
        QVariant value;
        if (!variableValue(wholeName, value)) {
            addError(errors, path, wholeName);
            return input;
        }
        return value;
    }

    static const QRegularExpression pattern(R"(\$\{([^}]+)\})");
    QString output;
    int offset = 0;
    auto matches = pattern.globalMatch(input);
    while (matches.hasNext()) {
        const auto match = matches.next();
        output += input.mid(offset, match.capturedStart(0) - offset);

        const auto variableName = match.captured(1).trimmed();
        QVariant value;
        if (variableValue(variableName, value)) {
            output += stringify(value);
        } else {
            addError(errors, path, variableName);
            output += match.captured(0);
        }

        offset = match.capturedEnd(0);
    }

    if (offset == 0) {
        return input;
    }

    output += input.mid(offset);
    return output;
}

bool RuntimeVariableResolver::variableFromMapPath(const QString& name, QVariant& value) const
{
    if (m_context.variables.contains(name)) {
        value = m_context.variables.value(name);
        return true;
    }

    const auto parts = name.split('.', Qt::SkipEmptyParts);
    if (parts.isEmpty() || !m_context.variables.contains(parts.first())) {
        return false;
    }

    QVariant current = m_context.variables.value(parts.first());
    for (int i = 1; i < parts.size(); ++i) {
        if (current.metaType().id() != QMetaType::QVariantMap) {
            return false;
        }
        const auto map = current.toMap();
        if (!map.contains(parts[i])) {
            return false;
        }
        current = map.value(parts[i]);
    }

    value = current;
    return true;
}

QString RuntimeVariableResolver::stringify(const QVariant& value) const
{
    if (value.metaType().id() == QMetaType::Bool) {
        return value.toBool() ? QString("true") : QString("false");
    }
    if (value.metaType().id() == QMetaType::QVariantMap) {
        return QString::fromUtf8(QJsonDocument(QJsonObject::fromVariantMap(value.toMap()))
                                     .toJson(QJsonDocument::Compact));
    }
    if (value.metaType().id() == QMetaType::QVariantList) {
        return QString::fromUtf8(QJsonDocument(QJsonArray::fromVariantList(value.toList()))
                                     .toJson(QJsonDocument::Compact));
    }
    return value.toString();
}

void RuntimeVariableResolver::addError(QVector<VariableResolutionError>& errors,
                                       const QString& path,
                                       const QString& variableName) const
{
    errors.push_back({path,
                      variableName,
                      QString("Unresolved runtime variable: %1").arg(variableName),
                      "Provide it through UUT variables or use supported runtime variables such as uut.id, attempt.index, or var.<name>"});
}

} // namespace PicoATE::Core
