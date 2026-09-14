#pragma once

#include "algorithms/ITerrainLodAlgorithm.h"
#include "algorithms/greedy_transactional_lod/TransactionalTypes.h"

namespace ParallelRoam::Algorithms::GreedyTransactionalLod
{
/// <summary>
/// 将公共输入转换为一次性自有种子；DOD 仅提供初网格，不提供后续目标
/// 构造结果不借用临时 DOD 或渲染包，持续更新不再调用该组件
/// </summary>
struct TransactionalSeedBuilder
{
    static void ValidateInput(const TerrainLodBuildInput& input);
    static Configuration ConfigurationFor(const TerrainLodBuildInput& input);
    static InitialMesh Build(const TerrainLodBuildInput& input);
};
}
