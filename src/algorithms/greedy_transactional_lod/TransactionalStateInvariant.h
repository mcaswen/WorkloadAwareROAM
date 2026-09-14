#pragma once

#include "algorithms/greedy_transactional_lod/TransactionalState.h"

namespace ParallelRoam::Algorithms::GreedyTransactionalLod
{
/// <summary>
/// 从实际活动面独立核对完整结构，只供初建及显式诊断使用
/// 正常事务通过局部构造维护这些不变量，不逐轮执行全域核查
/// </summary>
struct TransactionalStateInvariant
{
    static void Validate(const TransactionalState& state);
};
}
