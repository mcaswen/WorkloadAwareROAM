#pragma once

#include "experiment/greedy_transactional_lod/TransactionalState.h"

namespace ParallelRoam::Experiment::GreedyTransactionalLod
{
/// <summary>
/// 直接物化封闭批次的局部记录，不重演细分历史或重新进行选择
/// 分配、校验和邻接合成在发布前完成，已发布代际只推进一次
/// </summary>
class TransactionalCommit
{
public:
    static void Apply(TransactionalState& state,const CertifiedBatch& batch,WorkLedger& work);
};
}
