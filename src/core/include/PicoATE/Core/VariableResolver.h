#pragma once

#include <QHash>
#include <QString>
#include <QVariant>
#include <QVariantList>
#include <QVariantMap>
#include <QVector>

namespace PicoATE::Core {

struct VariableResolutionError {
    QString path;
    QString variableName;
    QString message;
    QString suggestion;
};

struct VariableResolverOptions {
    QString sequenceFilePath;
    QString projectDir;
    QHash<QString, QString> variables;
    bool useEnvironment = true;
};

class VariableResolver {
public:
    explicit VariableResolver(VariableResolverOptions options = {});

    QString sequenceDir() const;
    QString projectDir() const;
    bool variableValue(const QString& name, QString& value) const;

    QString resolveString(const QString& value,
                          QVector<VariableResolutionError>& errors,
                          const QString& path = {}) const;
    QVariant resolveVariant(const QVariant& value,
                            QVector<VariableResolutionError>& errors,
                            const QString& path = {}) const;
    QVariantMap resolveMap(const QVariantMap& map,
                           QVector<VariableResolutionError>& errors,
                           const QString& path = {}) const;
    QVariantList resolveList(const QVariantList& list,
                             QVector<VariableResolutionError>& errors,
                             const QString& path = {}) const;

private:
    void addError(QVector<VariableResolutionError>& errors,
                  const QString& path,
                  const QString& variableName,
                  const QString& message,
                  const QString& suggestion = {}) const;

    VariableResolverOptions m_options;
};

} // namespace PicoATE::Core
