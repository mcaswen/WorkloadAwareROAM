#pragma once

#include "algorithms/greedy_transactional_lod/TransactionalSamples.h"
#include <set>
#include <tuple>

namespace ParallelRoam::Algorithms::GreedyTransactionalLod
{
class TransactionalRejectionHints;
struct RejectionHint;
/// <summary>
/// 几何读取和写入资源；派生邻接由批准批次统一合成，不允许并发覆盖
/// </summary>
struct TransactionFootprint
{
    using Resource=std::tuple<char,Identity,Identity>;
    std::set<Resource> Reads, Writes;
};

/// <summary>
/// 同批全局顺序不可回退预留，失败资源命名仅在当前批次存在
/// </summary>
class TransactionalReservation
{
public:
    static CertifiedBatch Plan(const TransactionalState& state,const TransactionalSamples& samples,WorkLedger& work,
        const TransactionalExecution& execution={}, const TransactionalRejectionHints* hints = nullptr,
        std::vector<RejectionHint>* learned = nullptr);
    static TransactionFootprint Footprint(const TransactionalState& state,const Proposal& proposal);
    static bool Conflict(const TransactionFootprint& first,const TransactionFootprint& second);
    /// <summary>
    /// 仅按冻结意图顺序命名完整面数，后续失败或配对盈余不会回流
    /// </summary>
    static void AssignFreeFaces(std::size_t availableFaces, CertifiedBatch& batch);
};
}
