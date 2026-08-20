#pragma once

#include "algorithms/data_oriented_roam/DataOrientedRoamState.h"

namespace ParallelRoam::Algorithms::DataOrientedRoam
{
/// <summary>
/// variance tree 的构建和读取接口
/// 树由 DataOrientedRoamState 持有；输入 HeightMap 或拓扑深度变化时重建，普通 Build 只刷新节点误差
/// </summary>
void RebuildVarianceTrees(DataOrientedRoamState& state, int finestDepth);
// RefreshNodeVarianceErrors 把 state 当前节点映射到已构建的 variance tree
void RefreshNodeVarianceErrors(DataOrientedRoamState& state);

// VarianceError 只读 state，返回值供 AddNode 和 scoring 使用，不产生独立所有权
[[nodiscard]] float VarianceError(
    const DataOrientedRoamState& state,
    std::uint8_t varianceTreeIndex,
    std::size_t varianceIndex);
} // namespace ParallelRoam::Algorithms::DataOrientedRoam
