#pragma once

#include "PicoATE/Core/ExecutionPlan.h"

#include <QHash>
#include <QSet>

#include <functional>
#include <memory>

namespace PicoATE::Core {

struct CompileOptions {
    QString profileName;
};

class PlanCache {
public:
    using Compiler = std::function<std::shared_ptr<const ExecutionPlan>(
        const SequenceId&, const CompileOptions&)>;

    std::shared_ptr<const ExecutionPlan> getOrCompile(const SequenceId& sequenceId,
                                                       const CompileOptions& options,
                                                       const Compiler& compiler);

    void put(std::shared_ptr<const ExecutionPlan> plan);
    std::shared_ptr<const ExecutionPlan> get(const PlanId& planId) const;

    void purgeUnused();
    void pin(const PlanId& planId);
    void unpin(const PlanId& planId);

private:
    QHash<PlanId, std::weak_ptr<const ExecutionPlan>> m_cache;
    QSet<PlanId> m_pinned;
};

} // namespace PicoATE::Core
