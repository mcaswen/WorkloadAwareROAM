#pragma once

#include "experiment/greedy_transactional_lod/TransactionalSamples.h"
#include <set>
#include <tuple>

namespace ParallelRoam::Experiment::GreedyTransactionalLod
{
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
        const TransactionalExecution& execution={});
    static TransactionFootprint Footprint(const TransactionalState& state,const Proposal& proposal);
    static bool Conflict(const TransactionFootprint& first,const TransactionFootprint& second);
};
}
