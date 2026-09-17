#pragma once

#include "algorithms/greedy_transactional_lod/TransactionalState.h"

#include <optional>

namespace ParallelRoam::Algorithms::GreedyTransactionalLod
{
/// <summary>
/// 保存一个完整求得的空批次，复用同实例同代次的失败结论
/// 不保存成功提案或证书；视图、拓扑变化后由持续编排层清除
/// </summary>
class TransactionalIdlePlanCache
{
public:
    /// <summary>
    /// 命中返回独立逻辑副本，仍检查本次资源期限；实际工作计数不重放
    /// </summary>
    std::optional<CertifiedBatch> Find(const TransactionalState& state, bool diagnostics, WorkLedger& work) const;
    /// <summary>
    /// 只接收正常求解完成的批次；未完成或被截断的结果不能用于记录
    /// </summary>
    void Remember(const TransactionalState& state, bool diagnostics, const CertifiedBatch& batch, WorkLedger& work);
    /// <summary>
    /// 在正式发布前调用，避免账本分配失败破坏发布后的异常边界
    /// </summary>
    void Clear(WorkLedger& work);

private:
    const TransactionalState* _owner{};
    bool _diagnostics{};
    std::size_t _attempts{};
    std::optional<CertifiedBatch> _batch;
};
}
