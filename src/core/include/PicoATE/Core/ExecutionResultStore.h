#pragma once

#include "PicoATE/Core/ExecutionPlan.h"
#include "PicoATE/Core/RuntimeTypes.h"

#include <QHash>

#include <optional>

namespace PicoATE::Core {

enum class StepResultField {
    Outputs,
    Measurements,
    Outcome
};

struct StepResultReference {
    QString expression;
    QString nodeAddress;
    StepResultField field = StepResultField::Outputs;
    QString valuePath;
};

struct StoredStepResult {
    UutId uutId;
    FrameId frameId;
    NodeId nodeId;
    int attemptIndex = 0;
    NodeResult result;
};

struct StepResultLookup {
    bool found = false;
    QVariant value;
    QString errorCode;
    QString message;
};

std::optional<StepResultReference> parseStepResultReference(const QString& expression);
std::optional<NodeId> resolveStepReferenceNode(const ExecutionPlan& plan,
                                               const NodeId& currentNodeId,
                                               const QString& nodeAddress);

class ExecutionResultStore {
public:
    explicit ExecutionResultStore(const ExecutionPlan& plan);

    void commit(const UutId& uutId,
                const FrameId& frameId,
                const NodeId& nodeId,
                int attemptIndex,
                const NodeResult& result);

    StepResultLookup lookup(const UutId& uutId,
                            const FrameId& frameId,
                            const NodeId& currentNodeId,
                            const StepResultReference& reference) const;

    std::optional<StoredStepResult> latest(const UutId& uutId,
                                           const FrameId& frameId,
                                           const NodeId& nodeId) const;
    QVector<StoredStepResult> history(const UutId& uutId,
                                      const FrameId& frameId,
                                      const NodeId& nodeId) const;

private:
    QString resultKey(const UutId& uutId,
                      const FrameId& frameId,
                      const NodeId& nodeId) const;

    const ExecutionPlan& m_plan;
    QHash<QString, QVector<StoredStepResult>> m_results;
};

} // namespace PicoATE::Core
