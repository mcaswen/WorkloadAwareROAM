#pragma once

#include "algorithms/data_oriented_roam/DataOrientedRoamState.h"

namespace ParallelRoam::Algorithms::DataOrientedRoam
{
/// <summary>
/// 构建和读取 DOD 的嵌套楔形误差树
/// 更换高度图或改变预计算深度时重建树，普通更新只让节点刷新对应误差
/// </summary>
void RebuildVarianceTrees(DataOrientedRoamState& state, int finestDepth);
// 让节点池中的每个节点重新读取误差树中相同下标的结果
void RefreshNodeVarianceErrors(DataOrientedRoamState& state);

// VarianceError 只读取状态，供节点创建和屏幕误差计算使用
[[nodiscard]] float VarianceError(
    const DataOrientedRoamState& state,
    std::uint8_t varianceTreeIndex,
    std::size_t varianceIndex);
} // 命名空间 ParallelRoam::Algorithms::DataOrientedRoam
