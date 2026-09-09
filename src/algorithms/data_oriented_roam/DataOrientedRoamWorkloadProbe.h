#pragma once

#include "algorithms/data_oriented_roam/DataOrientedRoamTypes.h"

namespace ParallelRoam::Algorithms::DataOrientedRoam
{
struct DataOrientedRoamState;

/// <summary>
/// 保存允许用于选择的工作量特征，并单独记录采集耗时
/// 结果只包含值，不向发现编排暴露源状态引用
/// </summary>
struct DataOrientedRoamPassWorkload
{
    std::size_t PreMergeQueueEntryCount{0U};
    std::size_t PreSplitQueueEntryCount{0U};
    std::size_t PreActiveTriangleCount{0U};
    std::size_t PreTriangleBudget{0U};
    std::size_t PreRemainingTriangleBudget{0U};
    std::size_t PreTopologyEditCount{0U};
    int PreMaxActiveDepth{0};
    int PreMaxDepth{0};
    // 内部分类先于预算截取且不同于最终成功提交数量
    std::size_t PlanningInteriorCandidateCount{0U};
    std::size_t PlanningBoundaryCandidateCount{0U};
    std::size_t PlanningScheduledCandidateCount{0U};
    std::size_t PlanningNonEmptyChunkCount{0U};
    std::size_t PlanningDirtyTriangleCount{0U};
    std::size_t PlanningDirtyRangeCount{0U};
    bool PlanningMeshInitialization{false};
    bool PlanningMeshFallback{false};
    float PrimaryWorkValue{0.0F};
    std::vector<float> AuxiliaryFeatures;
    double FeatureCollectionMilliseconds{0.0};
};

/// <summary>
/// 从真实阶段输入重新规划工作量
/// 不执行待测策略，也不修改源状态中的队列、预算或网格
/// </summary>
[[nodiscard]] DataOrientedRoamPassWorkload ProbeDataOrientedRoamPassWorkload(
    const DataOrientedRoamState& state, TerrainLodPassId passId);
}
