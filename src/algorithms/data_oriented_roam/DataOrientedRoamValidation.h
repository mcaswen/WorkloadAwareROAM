#pragma once

#include "algorithms/data_oriented_roam/DataOrientedRoamState.h"

namespace ParallelRoam::Algorithms::DataOrientedRoam
{
/// <summary>
/// 检查 DOD 活动拓扑、跨帧保留的队列和只更新变化部分的网格是否保持一致
/// 只在启用验证的更新中运行，将错误数量写入统计结果，但不主动修复状态
/// </summary>
// ValidateTopology 只报告问题，确保调试开关不会改变正常算法结果
void ValidateTopology(DataOrientedRoamState& state);
void ValidateIncrementalMesh(DataOrientedRoamState& state);
} // 命名空间 ParallelRoam::Algorithms::DataOrientedRoam
