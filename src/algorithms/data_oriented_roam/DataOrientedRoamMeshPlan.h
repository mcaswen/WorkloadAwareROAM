#pragma once

#include "algorithms/data_oriented_roam/DataOrientedRoamMeshState.h"

namespace ParallelRoam::Algorithms::DataOrientedRoam
{
struct DataOrientedRoamNodePool;

/// <summary>
/// 规划仅借用拓扑和帧序号以禁止访问真实顶点与索引数组
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
/// 原位重放有序修改并返回是否需要重建几何存储
/// </summary>
[[nodiscard]] bool ApplyDataOrientedRoamMeshPlan(
    const DataOrientedRoamMeshPlanInput& input, DataOrientedRoamMeshMetadata& mesh);

/// <summary>
/// 将最终脏槽位合并为区间并保存下一帧需要老化的调试属性
/// </summary>
void FinalizeDataOrientedRoamMeshPlan(
    const DataOrientedRoamMeshPlanInput& input, DataOrientedRoamMeshMetadata& mesh);
}
