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

// 适配层与 Classic 保持相同接口，只在内部使用 SoA 节点池
// 基准测试因此可以用同一组输入和统计字段直接比较两种实现
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

    // 在公共算法边界拒绝无效高度图，避免基准测试把空网格误认为合法低细节结果
    if (input.HeightMap == nullptr || !input.HeightMap->IsValid())
    {
        if (errorMessage != nullptr)
        {
            *errorMessage = "Data-Oriented CPU ROAM build failed: invalid height map";
        }
        return false;
    }

    // DOD 和 Classic 都直接引用各自内部保留的 CPU 网格，并按相同规则报告需要增量上传的范围
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
    outPacket.CpuUploadAction = input.Settings.PassPolicy.CpuUpload;
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
    if (input.Settings.EnablePassEvidence)
    {
        _stats.ReplayInputHash = HashTerrainLodBuildInput(input);
    }
    _stats.CpuUtilizationPercent = ComputeCpuUtilizationPercent(cpuSampleStart, cpuSampleEnd);
    outPacket.ActiveTriangleCount = _stats.ActiveTriangleCount;
    outPacket.IndexCount = meshData.Indices.size();
    _stats.ResourceValidationFailureCount = outPacket.HasConsistentResourceContract() ? 0U : 1U;
    return !meshData.Vertices.empty() && !meshData.Indices.empty();
}

const TerrainLodStats& DataOrientedRoamTerrainLodAlgorithm::Stats() const
{
    return _stats;
}

void DataOrientedRoamTerrainLodAlgorithm::Reset()
{
    // Reset 丢弃节点池和迟滞路径，下一帧从根菱形重新建立状态
    _pipeline = DataOrientedRoamPipeline{};
    _stats = {};
}

DataOrientedRoamSettings DataOrientedRoamTerrainLodAlgorithm::ToDataOrientedSettings(
    const TerrainLodSettings& settings)
{
    // DOD 使用与 Classic 相同的公共质量参数，保证基准测试输入可比
    DataOrientedRoamSettings dataSettings{};
    dataSettings.MaxDepth = settings.MaxDepth;
    dataSettings.SplitThreshold = settings.ScreenSpaceSplitThresholdPixels;
    dataSettings.MergeThreshold = settings.ScreenSpaceMergeThresholdPixels;
    dataSettings.TriangleBudget = settings.TriangleBudget;
    // 线程数量仍由 DOD 内部自动决定，暂不扩大公共参数接口
    dataSettings.PassPolicy = settings.PassPolicy;
    if (!settings.EnableParallelSplit &&
        dataSettings.PassPolicy.SplitTopology == TerrainLodTopologyAction::Automatic)
    {
        dataSettings.PassPolicy.SplitTopology = TerrainLodTopologyAction::SerialImmediate;
    }
    dataSettings.EnableLocalConstraints = settings.EnableLocalConstraints;
    dataSettings.EnableTopologyValidation = settings.EnableTopologyValidation;
    dataSettings.EnablePassEvidence = settings.EnablePassEvidence;
    return dataSettings;
}

TerrainLodStats DataOrientedRoamTerrainLodAlgorithm::ToTerrainLodStats(const DataOrientedRoamStats& stats)
{
    // 将 DOD 私有统计映射到公共字段，CSV 无需了解节点池实现细节
    TerrainLodStats lodStats{};
    lodStats.PassTraces = stats.PassTraces;
    lodStats.BuildSequence = stats.BuildSequence;
    lodStats.TopologyHash = stats.TopologyHash;
    lodStats.ActiveLeafHash = stats.ActiveLeafHash;
    lodStats.MeshHash = stats.MeshHash;
    lodStats.NormalizedMeshHash = stats.NormalizedMeshHash;
    lodStats.TriangleBudget = stats.TriangleBudget;
    lodStats.QueueInvariantViolationCount = stats.QueueInvariantViolationCount;
    lodStats.PassEvidenceMilliseconds = stats.PassEvidenceMilliseconds;
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
        // 公共字段记录本帧各阶段实际使用过的最大 CPU 线程数
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
    lodStats.InteriorSplitCandidateCount = stats.InteriorSplitCandidateCount;
    lodStats.BoundarySplitCandidateCount = stats.BoundarySplitCandidateCount;
    lodStats.InteriorMergeCandidateCount = stats.InteriorMergeCandidateCount;
    lodStats.BoundaryMergeCandidateCount = stats.BoundaryMergeCandidateCount;
    // Q_s 刷新已经包含评分与预算统计，独立误差评估和叶收集时间保持为零
    const float errorEvaluationMilliseconds = ErrorEvaluationMilliseconds(stats);
    const float splitCollectMilliseconds =
        // 保留相加公式以兼容旧报告，当前 ActiveLeafCollectMilliseconds 为零
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
    lodStats.BudgetViolationCount = lodStats.ActiveTriangleCount > lodStats.TriangleBudget ? 1U : 0U;
    return lodStats;
}
} // 命名空间 ParallelRoam::Algorithms::DataOrientedRoam
