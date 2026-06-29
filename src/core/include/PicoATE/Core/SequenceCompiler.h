#pragma once

#include "PicoATE/Core/PlanBuilder.h"

#include <QJsonObject>

namespace PicoATE::Core {

struct CompileError {
    QString path;
    QString message;
    QString suggestion;
};

struct CompileWarning {
    QString path;
    QString message;
    QString suggestion;
};

struct CompileResult {
    QVector<CompileError> errors;
    QVector<CompileWarning> warnings;
    SequenceDef sequence;
    ExecutionPlan plan;

    bool ok() const { return errors.isEmpty(); }
};

class SequenceCompiler {
public:
    CompileResult compileJson(const QJsonObject& object) const;

private:
    SequenceDef parseSequence(const QJsonObject& object,
                              QVector<CompileError>& errors) const;
    StepGroupDef parseGroup(const QJsonObject& object,
                            const QString& path,
                            QVector<CompileError>& errors) const;
    StepDef parseStep(const QJsonObject& object,
                      const QString& path,
                      QVector<CompileError>& errors) const;
    QVector<ModuleBindingDef> parseModuleBindings(const QJsonArray& array,
                                                  const QString& path,
                                                  QVector<CompileError>& errors) const;

    QVector<ResourceRequirementDef> parseResources(const QJsonArray& array,
                                                   const QString& path,
                                                   QVector<CompileError>& errors) const;
    RetryPolicyDef parseRetry(const QJsonObject& object,
                              const QString& path,
                              QVector<CompileError>& errors) const;
    TimeoutPolicyDef parseTimeout(const QJsonObject& object,
                                  const QString& path,
                                  QVector<CompileError>& errors) const;
    LoopPolicyDef parseLoopPolicy(const QJsonObject& object,
                                  const QString& path,
                                  QVector<CompileError>& errors) const;
    ErrorPolicyDef parseErrorPolicy(const QJsonObject& object,
                                    const QString& path,
                                    QVector<CompileError>& errors) const;
    BarrierPolicyDef parseBarrierPolicy(const QJsonObject& object,
                                        const QString& path,
                                        const StepDef& step,
                                        QVector<CompileError>& errors) const;

    void addError(QVector<CompileError>& errors,
                  const QString& path,
                  const QString& message,
                  const QString& suggestion = {}) const;
};

} // namespace PicoATE::Core
