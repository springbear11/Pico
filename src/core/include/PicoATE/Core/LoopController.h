#pragma once

#include "PicoATE/Core/RuntimeTypes.h"

namespace PicoATE::Core {

struct LoopControllerResult {
    bool progressed = false;
    NodeOutcome outcome = NodeOutcome::Unknown;
    bool skippedBody = false;
    QString message;
    QString errorCode;
    QVariantMap outputs;
    int delayBeforeNextMs = 0;
};

class LoopController {
public:
    bool controllerReady(const LoopRegion& region, const UutExecution& uut) const;
    bool bodyNodeMayRun(const LoopRegion& region, const UutExecution& uut, const NodeId& nodeId) const;
    LoopControllerResult advance(const LoopRegion& region, UutExecution& uut);
    void requestBreak(const LoopRegion& region,
                      const UutId& uutId,
                      const NodeId& breakNodeId);
    void reset(const LoopRegion& region, const UutId& uutId);

private:
    struct LoopRuntimeState {
        bool started = false;
        bool completed = false;
        int currentIndex = -1;
        QVector<int> values;
        NodeOutcome aggregateOutcome = NodeOutcome::Passed;
        QStringList failedChildren;
        QDateTime startedAt;
        bool breakRequested = false;
        NodeId breakNodeId;
    };

    struct LoopBodySummary {
        NodeOutcome outcome = NodeOutcome::Passed;
        QStringList failedChildren;
    };

    QString stateKey(const UutId& uutId, const LoopId& loopId) const;
    LoopControllerResult advanceFor(const LoopRegion& region,
                                    UutExecution& uut,
                                    LoopRuntimeState& state);
    LoopControllerResult advanceWhile(const LoopRegion& region,
                                      UutExecution& uut,
                                      LoopRuntimeState& state);
    QVector<int> iterationValues(const ForLoopSpec& spec) const;
    bool bodyComplete(const LoopRegion& region, const UutExecution& uut) const;
    LoopBodySummary summarizeBody(const LoopRegion& region,
                                  const UutExecution& uut) const;
    void aggregateBodyResult(const LoopRegion& region,
                             const UutExecution& uut,
                             LoopRuntimeState& state) const;
    QVariantMap whileOutputs(const LoopRuntimeState& state, qint64 elapsedMs) const;
    void publishLoopVariables(UutExecution& uut, const QVariantMap& outputs) const;
    void resetBody(const LoopRegion& region, UutExecution& uut) const;

    QHash<QString, LoopRuntimeState> m_states;
};

} // namespace PicoATE::Core
