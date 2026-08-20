#pragma once

#include "algorithms/data_oriented_roam/DataOrientedRoamState.h"

namespace ParallelRoam::Algorithms::DataOrientedRoam
{
/// <summary>
/// DOD topology 和持久队列的 debug 验证接口
/// 只在启用 validation 的 Build 中调用；读取 state 并把错误计数写入 Stats，不主动修复拓扑
/// </summary>
// ValidateTopology 是可选 debug pass，不主动修复拓扑
void ValidateTopology(DataOrientedRoamState& state);
void ValidateIncrementalMesh(DataOrientedRoamState& state);
} // namespace ParallelRoam::Algorithms::DataOrientedRoam
