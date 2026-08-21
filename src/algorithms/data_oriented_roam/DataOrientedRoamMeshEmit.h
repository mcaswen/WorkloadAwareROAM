#pragma once

#include "algorithms/data_oriented_roam/DataOrientedRoamState.h"

namespace ParallelRoam::Algorithms::DataOrientedRoam
{
/// <summary>
/// 开始记录本次对跨帧保留网格的修改
/// </summary>
void BeginIncrementalMeshUpdate(
    DataOrientedRoamState& state,
    bool resetTopology);

/// <summary>
/// 清空跨帧保留的网格，下一次 CPU 更新将根据当前活动叶集合完整初始化
/// </summary>
void ResetIncrementalMeshStorage(DataOrientedRoamState& state);

/// <summary>
/// 记录主线程已经提交并完成活动索引更新的拓扑变化
/// </summary>
void RecordMeshSplit(DataOrientedRoamState& state, DataOrientedRoamNodeIndex node);
void RecordMeshMerge(DataOrientedRoamState& state, DataOrientedRoamNodeIndex node);

/// <summary>
/// 拓扑稳定后更新槽位，只重写本次更新中发生变化的三角形
/// </summary>
void ApplyIncrementalMeshUpdates(DataOrientedRoamState& state);

/// <summary>
/// 合并下标连续的待更新槽位，并生成渲染器需要的上传范围和网格复用统计
/// </summary>
void FinalizeIncrementalMeshUpdate(DataOrientedRoamState& state);
} // 命名空间 ParallelRoam::Algorithms::DataOrientedRoam
