#include "algorithms/data_oriented_roam/DataOrientedRoamTerrainLodAlgorithm.h"

#include "algorithms/TerrainLodProfiling.h"

#include <algorithm>

namespace ParallelRoam::Algorithms::DataOrientedRoam
{
namespace
{
float ErrorEvaluationMilliseconds(const DataOrientedRoamStats& stats)
{
    return stats.ErrorEvaluationWorkerCount > 1U ?
        stats.ErrorEvaluationParallelMilliseconds :
        stats.ErrorEvaluationSingleThreadMilliseconds;
}

} // 匿名命名空间

// adapter 保持和 Classic adapter 相同的接口形状
// 差异只在内部 builder 的 SoA node pool 表达
// benchmark 因此可以在同一 profile 下直接比较 Classic 与 DOD
TerrainLodAlgorithmInfo DataOrientedRoamTerrainLodAlgorithm::Info() const
{
    return TerrainLodAlgorithmInfo{
        TerrainLodAlgorithmId::DataOrientedCpuRoam,
        "data_oriented_cpu_roam",
        "Data-Oriented CPU ROAM",
        "SoA CPU ROAM with batched screen-error evaluation",
    };
}

TerrainLodAlgorithmCapabilities DataOrientedRoamTerrainLodAlgorithm::Capabilities() const
{
    TerrainLodAlgorithmCapabilities capabilities{};
    capabilities.SupportsCpuMeshOutput = true;
    capabilities.SupportsSplit = true;
    capabilities.SupportsMerge = true;
    capabilities.SupportsCrackFix = true;
    capabilities.SupportsTopologyValidation = true;
    return capabilities;
}

bool DataOrientedRoamTerrainLodAlgorithm::BuildRenderData(
    const TerrainLodBuildInput& input,
    TerrainLodRenderPacket& outPacket,
    std::string* errorMessage)
{
    _stats = {};
    outPacket = {};

    // 无效 HeightMap 在算法边界上直接失败
    // 避免 benchmark 把空 mesh 当作合法低细节输出
    if (input.HeightMap == nullptr || !input.HeightMap->IsValid())
    {
        if (errorMessage != nullptr)
        {
            *errorMessage = "Data-Oriented CPU ROAM build failed: invalid height map";
        }
        return false;
    }

    // DOD 与 Classic 共用 borrowed CPU Mesh/update range 契约。
    outPacket.Mode = TerrainLodRenderMode::CpuMesh;
    const TerrainLodCpuSample cpuSampleStart = CaptureTerrainLodCpuSample();
    const Terrain::TerrainMeshData& meshData = _pipeline.Build(
        *input.HeightMap,
        input.Settings.TerrainSize,
        input.Settings.HeightScale,
        input.View,
        ToDataOrientedSettings(input.Settings));
    outPacket.BorrowedCpuMesh = &meshData;
    outPacket.CpuMeshLifetime = TerrainLodCpuMeshLifetime::UntilNextBuildOrReset;
    outPacket.CpuMeshRequiresFullUpload = _pipeline.MeshRequiresFullUpload();
    outPacket.CpuMeshGeneration = _pipeline.MeshGeneration();
    outPacket.CpuMeshUpdateRanges.reserve(_pipeline.MeshUpdateRanges().size());
    for (const DataOrientedRoamMeshUpdateRange& range : _pipeline.MeshUpdateRanges())
    {
        outPacket.CpuMeshUpdateRanges.push_back(TerrainLodCpuMeshUpdateRange{
            range.FirstTriangle * 3U,
            range.TriangleCount * 3U,
            range.FirstTriangle * 3U,
            range.TriangleCount * 3U,
        });
    }
    const TerrainLodCpuSample cpuSampleEnd = CaptureTerrainLodCpuSample();
    _stats = ToTerrainLodStats(_pipeline.Stats());
    _stats.CpuUtilizationPercent = ComputeCpuUtilizationPercent(cpuSampleStart, cpuSampleEnd);
    outPacket.ActiveTriangleCount = _stats.ActiveTriangleCount;
    outPacket.IndexCount = meshData.Indices.size();
    return !meshData.Vertices.empty() && !meshData.Indices.empty();
}

const TerrainLodStats& DataOrientedRoamTerrainLodAlgorithm::Stats() const
{
    return _stats;
}

void DataOrientedRoamTerrainLodAlgorithm::Reset()
{
    // Reset 丢弃 index pool 和 hysteresis path
    // 下一帧会重新建立 root diamond
    _pipeline = DataOrientedRoamPipeline{};
    _stats = {};
}

DataOrientedRoamSettings DataOrientedRoamTerrainLodAlgorithm::ToDataOrientedSettings(
    const TerrainLodSettings& settings)
{
    // DOD 使用与 Classic 相同的控制变量
    // 这是三版本 benchmark 可比性的前提
    DataOrientedRoamSettings dataSettings{};
    dataSettings.MaxDepth = settings.MaxDepth;
    dataSettings.SplitThreshold = settings.ScreenSpaceSplitThresholdPixels;
    dataSettings.MergeThreshold = settings.ScreenSpaceMergeThresholdPixels;
    dataSettings.TriangleBudget = settings.TriangleBudget;
    // worker 数保持 DOD 内部策略  避免扩大统一参数面
    dataSettings.ErrorEvaluationWorkerCount = 0U;
    dataSettings.EnableParallelSplit = settings.EnableParallelSplit;
    dataSettings.EnableLocalConstraints = settings.EnableLocalConstraints;
    dataSettings.EnableTopologyValidation = settings.EnableTopologyValidation;
    return dataSettings;
}

TerrainLodStats DataOrientedRoamTerrainLodAlgorithm::ToTerrainLodStats(const DataOrientedRoamStats& stats)
{
    // DOD 私有统计映射到统一字段
    // CSV 不暴露具体 node pool 实现细节
    TerrainLodStats lodStats{};
    lodStats.ActiveTriangleCount = stats.ActiveTriangleCount;
    lodStats.ActiveNodeCount = stats.NodeCount;
    lodStats.OriginalTriangleCount = stats.OriginalTriangleCount;
    lodStats.SubdividedTriangleCount = stats.SubdividedTriangleCount;
    lodStats.RebuiltTriangleCount = stats.RebuiltTriangleCount;
    lodStats.ActiveSplitCount = stats.ActiveSplitCount;
    lodStats.SplitCount = stats.SplitCount;
    lodStats.ForcedSplitCount = stats.ForcedSplitCount;
    lodStats.MergeCount = stats.MergeCount;
    lodStats.CrackRiskCount = stats.CrackRiskCount;
    lodStats.ConstraintPassCount = stats.ConstraintPassCount;
    lodStats.CandidatePeakCount = stats.CandidatePeakCount;
    lodStats.PersistentSplitQueueSize = stats.PersistentSplitQueueSize;
    lodStats.PersistentMergeQueueSize = stats.PersistentMergeQueueSize;
    lodStats.QueueCrossoverCount = stats.QueueCrossoverCount;
    lodStats.QueueMembershipUpdateCount = stats.QueueMembershipUpdateCount;
    lodStats.CpuMeshFullRebuildCount = stats.MeshFullRebuildCount;
    lodStats.CpuMeshUpdatedTriangleCount = stats.MeshUpdatedTriangleCount;
    lodStats.CpuMeshReusedTriangleCount = stats.MeshReusedTriangleCount;
    lodStats.CpuMeshDirtyRangeCount = stats.MeshDirtyRangeCount;
    lodStats.RejectedSplitCount = stats.RejectedSplitCount;
    lodStats.BudgetRejectedSplitCount = stats.BudgetRejectedSplitCount;
    lodStats.RejectedMergeCount = stats.RejectedMergeCount;
    lodStats.TjunctionCount = stats.TjunctionCount;
    lodStats.InvalidNeighborCount = stats.InvalidNeighborCount;
    lodStats.InvalidTopologyCount = stats.InvalidTopologyCount;
    lodStats.CpuWorkerCount = std::max({
        // 统一字段记录本帧用到的最大 CPU 并行宽度
        std::size_t{1},
        stats.ErrorEvaluationWorkerCount,
        stats.CollectWorkerCount,
        stats.CandidateMarkWorkerCount,
        stats.EmitWorkerCount,
        stats.TopologyCommitWorkerCount,
    });
    lodStats.TopologyCommitMinCandidateCount = stats.TopologyCommitMinCandidateCount;
    lodStats.SplitTopologyCommitMinCandidateCount = stats.SplitTopologyCommitMinCandidateCount;
    lodStats.MergeTopologyCommitMinCandidateCount = stats.MergeTopologyCommitMinCandidateCount;
    lodStats.SplitTopologyCandidateCount = stats.SplitTopologyCandidateCount;
    lodStats.SplitTopologyNonEmptyChunkCount = stats.SplitTopologyNonEmptyChunkCount;
    lodStats.SplitTopologyCommitWorkerCount = stats.SplitTopologyCommitWorkerCount;
    lodStats.ParallelSplitCommitCount = stats.ParallelSplitCommitCount;
    lodStats.MergeTopologyCandidateCount = stats.MergeTopologyCandidateCount;
    lodStats.MergeTopologyNonEmptyChunkCount = stats.MergeTopologyNonEmptyChunkCount;
    lodStats.MergeTopologyCommitWorkerCount = stats.MergeTopologyCommitWorkerCount;
    lodStats.ParallelMergeCommitCount = stats.ParallelMergeCommitCount;
    // Q_s refresh 已包含评分与预算计数，error/collect 独立时间保持为零。
    const float errorEvaluationMilliseconds = ErrorEvaluationMilliseconds(stats);
    const float splitCollectMilliseconds =
        // 保留公式兼容旧报告；当前 ActiveLeafCollectMilliseconds 为零。
        stats.ActiveLeafCollectMilliseconds + stats.SplitCandidateMarkMilliseconds;
    lodStats.CpuUpdateMilliseconds = stats.UpdateMilliseconds;
    lodStats.CpuPrepareMilliseconds = stats.PrepareMilliseconds;
    lodStats.CpuMergeCandidateMarkMilliseconds = stats.MergeCandidateMarkMilliseconds;
    lodStats.CpuMergeTopologyMilliseconds =
        std::max(0.0F, stats.MergeMilliseconds - stats.MergeCandidateMarkMilliseconds);
    lodStats.CpuSplitTopologyChunkBuildMilliseconds = stats.SplitTopologyChunkBuildMilliseconds;
    lodStats.CpuSplitTopologyQueueInvalidationMilliseconds =
        stats.SplitTopologyQueueInvalidationMilliseconds;
    lodStats.CpuSplitTopologyParallelCommitMilliseconds = stats.SplitTopologyParallelCommitMilliseconds;
    lodStats.CpuSplitTopologyResultMergeMilliseconds = stats.SplitTopologyResultMergeMilliseconds;
    lodStats.CpuSplitTopologyIndexQueueRefreshMilliseconds =
        stats.SplitTopologyIndexQueueRefreshMilliseconds;
    lodStats.CpuSplitTopologySerialConvergenceMilliseconds =
        stats.SplitTopologySerialConvergenceMilliseconds;
    lodStats.CpuMergeTopologyChunkBuildMilliseconds = stats.MergeTopologyChunkBuildMilliseconds;
    lodStats.CpuMergeTopologyQueueInvalidationMilliseconds =
        stats.MergeTopologyQueueInvalidationMilliseconds;
    lodStats.CpuMergeTopologyParallelCommitMilliseconds = stats.MergeTopologyParallelCommitMilliseconds;
    lodStats.CpuMergeTopologyResultMergeMilliseconds = stats.MergeTopologyResultMergeMilliseconds;
    lodStats.CpuMergeTopologyIndexQueueRefreshMilliseconds =
        stats.MergeTopologyIndexQueueRefreshMilliseconds;
    lodStats.CpuMergeTopologySerialConvergenceMilliseconds =
        stats.MergeTopologySerialConvergenceMilliseconds;
    lodStats.CpuBudgetLeafCollectMilliseconds = stats.BudgetLeafCollectMilliseconds;
    lodStats.CpuErrorEvalMilliseconds = errorEvaluationMilliseconds;
    lodStats.CpuSplitCandidateMarkMilliseconds =
        stats.ActiveLeafCollectMilliseconds + stats.SplitCandidateMarkMilliseconds;
    lodStats.CpuSplitTopologyMilliseconds =
        std::max(
            0.0F,
            stats.SplitMilliseconds -
                errorEvaluationMilliseconds -
                splitCollectMilliseconds -
                stats.MergeCrossoverMilliseconds);
    lodStats.CpuFinalLeafCollectMilliseconds = stats.FinalLeafCollectMilliseconds;
    lodStats.CpuMeshEmitMilliseconds = stats.MeshEmitMilliseconds;
    lodStats.CpuFinalizeMilliseconds = stats.FinalizeMilliseconds;
    lodStats.SplitMilliseconds = stats.SplitMilliseconds;
    lodStats.MergeMilliseconds = stats.MergeMilliseconds;
    lodStats.EmitMilliseconds = stats.EmitMilliseconds;
    lodStats.ValidateMilliseconds = stats.ValidateMilliseconds;
    lodStats.MaxActiveDepth = stats.MaxDepthReached;
    return lodStats;
}
} // 命名空间 ParallelRoam::Algorithms::DataOrientedRoam
