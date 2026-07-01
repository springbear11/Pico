#pragma once

#include "PicoATE/Core/RuntimeTypes.h"
#include "PicoATE/Core/ExecutionResultStore.h"
#include "PicoATE/Core/VariableResolver.h"

#include <QVariant>
#include <QVariantList>
#include <QVariantMap>
#include <QVector>

namespace PicoATE::Core {

struct RuntimeVariableContext {
    UutId uutId;
    FrameId frameId;
    AttemptId attemptId;
    NodeId currentNodeId;
    int attemptIndex = 0;
    QVariantMap variables;
    const ExecutionResultStore* resultStore = nullptr;
};

class RuntimeVariableResolver {
public:
    explicit RuntimeVariableResolver(RuntimeVariableContext context);

    bool variableValue(const QString& name, QVariant& value) const;

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
    QVariant resolveString(const QString& value,
                           QVector<VariableResolutionError>& errors,
                           const QString& path) const;
    bool variableFromMapPath(const QString& name, QVariant& value) const;
    bool resolvedValue(const QString& name,
                       QVariant& value,
                       QString& errorMessage,
                       QString& suggestion) const;
    QString stringify(const QVariant& value) const;
    void addError(QVector<VariableResolutionError>& errors,
                  const QString& path,
                  const QString& variableName,
                  const QString& message = {},
                  const QString& suggestion = {}) const;

    RuntimeVariableContext m_context;
};

} // namespace PicoATE::Core
