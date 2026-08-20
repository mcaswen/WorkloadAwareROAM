#pragma once

#include "algorithms/data_oriented_roam/DataOrientedRoamState.h"

namespace ParallelRoam::Algorithms::DataOrientedRoam
{
/// <summary>
/// 开始一个持久 Mesh generation。
/// </summary>
void BeginIncrementalMeshUpdate(
    DataOrientedRoamState& state,
    bool resetTopology);

/// <summary>
/// 丢弃持久 Mesh 数据；下一次 CPU Build 从当前 active leaf cut 做完整初始化。
/// </summary>
void ResetIncrementalMeshStorage(DataOrientedRoamState& state);

/// <summary>
/// 记录已经成功提交的 topology edit；只在主线程活动索引更新点调用。
/// </summary>
void RecordMeshSplit(DataOrientedRoamState& state, DataOrientedRoamNodeIndex node);
void RecordMeshMerge(DataOrientedRoamState& state, DataOrientedRoamNodeIndex node);

/// <summary>
/// 拓扑稳定后重放 slot edit，并只重写本 Build 的 dirty triangle。
/// </summary>
void ApplyIncrementalMeshUpdates(DataOrientedRoamState& state);
void FinalizeIncrementalMeshUpdate(DataOrientedRoamState& state);
} // namespace ParallelRoam::Algorithms::DataOrientedRoam
