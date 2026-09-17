#include "algorithms/greedy_transactional_lod/TransactionalIdlePlanCache.h"

namespace ParallelRoam::Algorithms::GreedyTransactionalLod
{
std::optional<CertifiedBatch> TransactionalIdlePlanCache::Find(const TransactionalState& state,
    bool diagnostics, WorkLedger& work) const
{
    work.CheckLimit();
    if (!_batch || _owner != &state || _batch->Version != state.Version() || _diagnostics != diagnostics)
    {
        return {};
    }
    // 返回独立逻辑记录；调用方不可修改缓存中的目录结果或预算分母
    auto result = *_batch;
    work.CheckLimit();
    ++work.Reasons["idle_plan_hit"];
    work.Reasons["idle_plan_reused_attempts"] += _attempts;
    return result;
}

void TransactionalIdlePlanCache::Remember(const TransactionalState& state, bool diagnostics,
    const CertifiedBatch& batch, WorkLedger& work)
{
    if (!batch.Exchanges.empty() || batch.Version != state.Version())
    {
        Clear(work);
        return;
    }
    work.CheckLimit();
    // 完整拷贝成功后才替换旧记录；异常不把半份失败目录变成可命中状态
    auto prepared = batch;
    std::size_t attemptCount = 0;
    std::size_t bytes = sizeof(CertifiedBatch) + prepared.IntentBudgets.capacity() * sizeof(IntentBudget) +
        (prepared.IntentIds.capacity() + prepared.PoolIds.capacity()) * sizeof(Identity) +
        prepared.IntentResults.capacity() * sizeof(std::string) +
        prepared.Attempts.capacity() * sizeof(std::vector<std::pair<char, std::string>>);
    for (const auto& reason : prepared.IntentResults)
    {
        bytes += reason.capacity() + 1;
    }
    for (const auto& attempts : prepared.Attempts)
    {
        attemptCount += attempts.size();
        bytes += attempts.capacity() * sizeof(std::pair<char, std::string>);
        for (const auto& attempt : attempts)
        {
            bytes += attempt.second.capacity() + 1;
        }
    }
    work.CheckLimit();
    ++work.Reasons["idle_plan_stored"];
    // 容量估计含短字符串重复计量，不冒充分配器实测驻留内存
    work.Reasons["idle_plan_payload_bytes"] += bytes;
    _batch = std::move(prepared);
    _owner = &state;
    _diagnostics = diagnostics;
    _attempts = attemptCount;
}

void TransactionalIdlePlanCache::Clear(WorkLedger& work)
{
    if (_batch)
    {
        // 账本插入可能分配；整个清理必须在真正状态变化前完成
        ++work.Reasons["idle_plan_invalidated"];
        _batch.reset();
        _owner = nullptr;
    }
}
}
