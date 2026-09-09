#pragma once

#include "algorithms/data_oriented_roam/DataOrientedRoamMeshState.h"

namespace ParallelRoam::Algorithms::DataOrientedRoam
{
struct DataOrientedRoamNodePool;

/// <summary>
/// 为网格规划提供只读节点、活动叶集合和帧序号
/// 不暴露真实顶点与索引数组，使规划无法修改几何存储
/// </summary>
struct DataOrientedRoamMeshPlanInput
{
    const DataOrientedRoamNodePool& Nodes;
    const std::vector<DataOrientedRoamNodeIndex>& ActiveLeafNodes;
    std::uint64_t BuildSequence;

    [[nodiscard]] bool IsValidNode(DataOrientedRoamNodeIndex node) const;
    [[nodiscard]] bool IsLeaf(DataOrientedRoamNodeIndex node) const;
};

/// <summary>
/// 开始新一代元数据更新并在代数回绕时重置脏标记
/// </summary>
void BeginDataOrientedRoamMeshPlan(DataOrientedRoamMeshMetadata& mesh, bool resetTopology);

/// <summary>
/// 按拓扑提交顺序在元数据上原位重放修改
/// 返回值表示是否需要重建几何存储，实际几何由调用方更新
/// </summary>
[[nodiscard]] bool ApplyDataOrientedRoamMeshPlan(
    const DataOrientedRoamMeshPlanInput& input, DataOrientedRoamMeshMetadata& mesh);

/// <summary>
/// 将最终脏槽位合并为区间并保存下一帧需要老化的调试属性
/// </summary>
void FinalizeDataOrientedRoamMeshPlan(
    const DataOrientedRoamMeshPlanInput& input, DataOrientedRoamMeshMetadata& mesh);
}
