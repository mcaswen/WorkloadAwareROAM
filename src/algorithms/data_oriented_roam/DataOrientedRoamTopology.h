#pragma once

#include "algorithms/data_oriented_roam/DataOrientedRoamState.h"

namespace ParallelRoam::Algorithms::DataOrientedRoam
{
/// <summary>
/// DOD topology 的每帧提交入口
/// 在 MeshBuilder 的 Build 中调用；直接修改 state 的活动索引、邻居关系、预算和持久队列
/// 并行预提交结束后由主线程继续执行串行收敛，调用方不得并发访问同一个 state
/// </summary>
// merge 先回收低误差 diamond，split 再按高误差顺序消费剩余预算
void RefineWithSplitQueue(DataOrientedRoamState& state);
void MergeWithDiamondQueue(DataOrientedRoamState& state);
} // namespace ParallelRoam::Algorithms::DataOrientedRoam
