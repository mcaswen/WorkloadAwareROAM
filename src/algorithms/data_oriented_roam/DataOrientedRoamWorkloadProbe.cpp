#include "algorithms/data_oriented_roam/DataOrientedRoamWorkloadProbe.h"
#include "algorithms/data_oriented_roam/DataOrientedRoamMeshPlan.h"
#include "algorithms/data_oriented_roam/DataOrientedRoamTopologyPlan.h"
#include "tools/PerformanceTimer.h"

#include <algorithm>
#include <stdexcept>

namespace ParallelRoam::Algorithms::DataOrientedRoam
{
namespace
{
/// <summary>
/// 统一以 binary32 保存选择值且空集合的比率定义为零
/// </summary>
float Ratio(std::size_t numerator, std::size_t denominator)
{
    return denominator == 0U ? 0.0F : static_cast<float>(numerator) / static_cast<float>(denominator);
}
}

DataOrientedRoamPassWorkload ProbeDataOrientedRoamPassWorkload(
    const DataOrientedRoamState& state, TerrainLodPassId passId)
{
    Tools::PerformanceTimer timer;
    DataOrientedRoamPassWorkload work;
    // Stats 在准备时清零且不能代表边界处的真实集合
    work.PreMergeQueueEntryCount = state.MergeQueue.size();
    work.PreSplitQueueEntryCount = state.SplitQueue.size();
    work.PreActiveTriangleCount = state.ActiveLeafNodes.size();
    work.PreTriangleBudget = state.Settings.TriangleBudget;
    work.PreRemainingTriangleBudget = work.PreTriangleBudget > work.PreActiveTriangleCount
        ? work.PreTriangleBudget - work.PreActiveTriangleCount : 0U;
    work.PreTopologyEditCount = state.IncrementalMesh.Metadata.TopologyEdits.size();
    work.PreMaxDepth = state.Settings.MaxDepth;
    for (const auto node : state.ActiveLeafNodes)
        work.PreMaxActiveDepth = std::max(work.PreMaxActiveDepth, state.Nodes.DepthAt(node));
    const float activeRatio = Ratio(work.PreActiveTriangleCount, work.PreTriangleBudget);
    const float remainingRatio = Ratio(work.PreRemainingTriangleBudget, work.PreTriangleBudget);
    const float depthRatio = Ratio(static_cast<std::size_t>(work.PreMaxActiveDepth),
        static_cast<std::size_t>(std::max(work.PreMaxDepth, 0)));

    if (passId == TerrainLodPassId::MergeScore || passId == TerrainLodPassId::SplitScore)
    {
        const bool merge = passId == TerrainLodPassId::MergeScore;
        work.PrimaryWorkValue = static_cast<float>(merge
            ? work.PreMergeQueueEntryCount : work.PreSplitQueueEntryCount);
        work.AuxiliaryFeatures = {work.PrimaryWorkValue, activeRatio};
        if (!merge)
            work.AuxiliaryFeatures.push_back(remainingRatio);
        work.AuxiliaryFeatures.push_back(depthRatio);
    }
    else if (passId == TerrainLodPassId::MergeTopology || passId == TerrainLodPassId::SplitTopology)
    {
        // 临时计划在返回前销毁且测量阶段必须重新承担自己的规划成本
        const auto collect = [&work](const auto& plan) {
            work.PlanningInteriorCandidateCount = plan.InteriorCandidateCount;
            work.PlanningBoundaryCandidateCount = plan.BoundaryCandidateCount;
            work.PlanningScheduledCandidateCount = plan.ScheduledCandidateCount;
            work.PlanningNonEmptyChunkCount = plan.NonEmptyChunkCount;
        };
        if (passId == TerrainLodPassId::MergeTopology)
            collect(PlanDataOrientedRoamMergeTopology(state));
        else
            collect(PlanDataOrientedRoamSplitTopology(state));
        const auto candidates = work.PlanningInteriorCandidateCount + work.PlanningBoundaryCandidateCount;
        work.PrimaryWorkValue = static_cast<float>(candidates);
        // 协议分母 8 是线程上限而不是 64 个空间分块
        work.AuxiliaryFeatures = {Ratio(work.PlanningInteriorCandidateCount, candidates),
            Ratio(work.PlanningBoundaryCandidateCount, candidates),
            Ratio(work.PlanningNonEmptyChunkCount, 8U), remainingRatio};
    }
    else if (passId == TerrainLodPassId::MeshEmit)
    {
        // 仅复制元数据以模拟填洞与调试过渡而不触碰几何存储
        auto metadata = state.IncrementalMesh.Metadata;
        work.PlanningMeshInitialization = metadata.NeedsInitialization;
        const DataOrientedRoamMeshPlanInput input{state.Nodes, state.ActiveLeafNodes, state.BuildSequence};
        const bool reinitialized = ApplyDataOrientedRoamMeshPlan(input, metadata);
        work.PlanningMeshFallback = reinitialized && !work.PlanningMeshInitialization;
        FinalizeDataOrientedRoamMeshPlan(input, metadata);
        work.PlanningDirtyTriangleCount = metadata.RequiresFullUpload
            ? metadata.SlotOwners.size() : metadata.DirtySlots.size();
        work.PlanningDirtyRangeCount = metadata.UpdateRanges.size();
        work.PrimaryWorkValue = Ratio(work.PlanningDirtyTriangleCount, work.PreActiveTriangleCount);
        work.AuxiliaryFeatures = {activeRatio, Ratio(work.PreTopologyEditCount, work.PreActiveTriangleCount),
            Ratio(work.PlanningDirtyRangeCount, work.PlanningDirtyTriangleCount)};
    }
    else
        throw std::invalid_argument{"Expected a CPU ROAM workload pass"};
    // 输入哈希与帧末正确性诊断由调用方单独记录成本
    work.FeatureCollectionMilliseconds = timer.Stop();
    return work;
}
}
