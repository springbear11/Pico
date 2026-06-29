#include "PicoATE/Core/PlanCache.h"

namespace PicoATE::Core {

std::shared_ptr<const ExecutionPlan> PlanCache::getOrCompile(
    const SequenceId& sequenceId,
    const CompileOptions& options,
    const Compiler& compiler)
{
    for (auto it = m_cache.constBegin(); it != m_cache.constEnd(); ++it) {
        auto plan = it.value().lock();
        if (plan && plan->sequenceId == sequenceId) {
            return plan;
        }
    }

    auto plan = compiler(sequenceId, options);
    put(plan);
    return plan;
}

void PlanCache::put(std::shared_ptr<const ExecutionPlan> plan)
{
    if (!plan) {
        return;
    }
    m_cache.insert(plan->id, plan);
}

std::shared_ptr<const ExecutionPlan> PlanCache::get(const PlanId& planId) const
{
    auto it = m_cache.constFind(planId);
    if (it == m_cache.constEnd()) {
        return {};
    }
    return it.value().lock();
}

void PlanCache::purgeUnused()
{
    QVector<PlanId> toRemove;
    for (auto it = m_cache.constBegin(); it != m_cache.constEnd(); ++it) {
        if (m_pinned.contains(it.key())) {
            continue;
        }
        if (it.value().expired()) {
            toRemove.push_back(it.key());
        }
    }

    for (const auto& planId : toRemove) {
        m_cache.remove(planId);
    }
}

void PlanCache::pin(const PlanId& planId)
{
    m_pinned.insert(planId);
}

void PlanCache::unpin(const PlanId& planId)
{
    m_pinned.remove(planId);
}

} // namespace PicoATE::Core
