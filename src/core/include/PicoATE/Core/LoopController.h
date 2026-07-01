#pragma once

#include "PicoATE/Core/RuntimeTypes.h"

namespace PicoATE::Core {

struct LoopControllerResult {
    bool progressed = false;
    NodeOutcome outcome = NodeOutcome::Unknown;
    bool skippedBody = false;
    QString message;
};

class LoopController {
public:
    bool controllerReady(const LoopRegion& region, const UutExecution& uut) const;
    bool bodyNodeMayRun(const LoopRegion& region, const UutExecution& uut, const NodeId& nodeId) const;
    LoopControllerResult advance(const LoopRegion& region, UutExecution& uut);

private:
    struct LoopRuntimeState {
        bool started = false;
        bool completed = false;
        int currentIndex = -1;
        QVector<int> values;
        NodeOutcome aggregateOutcome = NodeOutcome::Passed;
        QStringList failedChildren;
    };

    QString stateKey(const UutId& uutId, const LoopId& loopId) const;
    QVector<int> iterationValues(const ForLoopSpec& spec) const;
    bool bodyComplete(const LoopRegion& region, const UutExecution& uut) const;
    void aggregateBodyResult(const LoopRegion& region,
                             const UutExecution& uut,
                             LoopRuntimeState& state) const;
    void resetBody(const LoopRegion& region, UutExecution& uut) const;

    QHash<QString, LoopRuntimeState> m_states;
};

} // namespace PicoATE::Core
