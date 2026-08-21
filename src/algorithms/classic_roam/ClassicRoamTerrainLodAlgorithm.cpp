#include "algorithms/classic_roam/ClassicRoamTerrainLodAlgorithm.h"

#include "algorithms/TerrainLodProfiling.h"

namespace ParallelRoam::Algorithms::ClassicRoam
{
// 适配层只转换公共接口与 Classic 类型，实际拓扑仍由 ClassicRoamMeshBuilder 持有
TerrainLodAlgorithmInfo ClassicRoamTerrainLodAlgorithm::Info() const
{
    return TerrainLodAlgorithmInfo{
        TerrainLodAlgorithmId::ClassicCpuRoam,
        "classic_cpu_roam",
        "Classic CPU ROAM",
        "Object-style CPU ROAM baseline with persistent topology, dual queues, and incremental mesh emit",
    };
}

TerrainLodAlgorithmCapabilities ClassicRoamTerrainLodAlgorithm::Capabilities() const
{
    TerrainLodAlgorithmCapabilities capabilities{};
    capabilities.SupportsCpuMeshOutput = true;
    capabilities.SupportsSplit = true;
    capabilities.SupportsMerge = true;
    capabilities.SupportsCrackFix = true;
    capabilities.SupportsTopologyValidation = true;
    return capabilities;
}

bool ClassicRoamTerrainLodAlgorithm::BuildRenderData(
    const TerrainLodBuildInput& input,
    TerrainLodRenderPacket& outPacket,
    std::string* errorMessage)
{
    _stats = {};
    outPacket = {};

    // 在公共接口层检查输入，使渲染器和基准测试无需了解 Classic 内部类型
    if (input.HeightMap == nullptr || !input.HeightMap->IsValid())
    {
        // 公共接口将无效高度图视为构建失败，底层生成器仍保留返回空网格的行为
        if (errorMessage != nullptr)
        {
            *errorMessage = "Classic CPU ROAM build failed: invalid height map";
        }
        return false;
    }

    // Classic 直接返回生成器内部保留的 CPU 网格引用，避免复制完整顶点和索引数组
    outPacket.Mode = TerrainLodRenderMode::CpuMesh;
    const TerrainLodCpuSample cpuSampleStart = CaptureTerrainLodCpuSample();
    const Terrain::TerrainMeshData& meshData = _builder.Build(
        *input.HeightMap,
        input.Settings.TerrainSize,
        input.Settings.HeightScale,
        input.View,
        ToClassicSettings(input.Settings));
    outPacket.BorrowedCpuMesh = &meshData;
    outPacket.CpuMeshLifetime = TerrainLodCpuMeshLifetime::UntilNextBuildOrReset;
    outPacket.CpuMeshRequiresFullUpload = _builder.MeshRequiresFullUpload();
    outPacket.CpuMeshGeneration = _builder.MeshGeneration();
    outPacket.CpuMeshUpdateRanges.reserve(_builder.MeshUpdateRanges().size());
    for (const ClassicRoamMeshUpdateRange& range : _builder.MeshUpdateRanges())
    {
        outPacket.CpuMeshUpdateRanges.push_back(TerrainLodCpuMeshUpdateRange{
            range.FirstTriangle * 3U,
            range.TriangleCount * 3U,
            range.FirstTriangle * 3U,
            range.TriangleCount * 3U,
        });
    }
    const TerrainLodCpuSample cpuSampleEnd = CaptureTerrainLodCpuSample();
    _stats = ToTerrainLodStats(_builder.Stats());
    _stats.CpuUtilizationPercent = ComputeCpuUtilizationPercent(cpuSampleStart, cpuSampleEnd);
    outPacket.ActiveTriangleCount = _stats.ActiveTriangleCount;
    outPacket.IndexCount = meshData.Indices.size();
    return !meshData.Vertices.empty() && !meshData.Indices.empty();
}

const TerrainLodStats& ClassicRoamTerrainLodAlgorithm::Stats() const
{
    return _stats;
}

void ClassicRoamTerrainLodAlgorithm::Reset()
{
    // 切换算法或改用规则网格时，重建生成器以丢弃全部跨帧保留的拓扑和网格状态
    _builder = ClassicRoamMeshBuilder{};
    _stats = {};
}

ClassicRoamSettings ClassicRoamTerrainLodAlgorithm::ToClassicSettings(const TerrainLodSettings& settings)
{
    // 只传递 Classic 实现真正支持的参数，避免公共设置中的其他字段产生误导
    ClassicRoamSettings classicSettings{};
    classicSettings.MaxDepth = settings.MaxDepth;
    classicSettings.SplitThreshold = settings.ScreenSpaceSplitThresholdPixels;
    classicSettings.MergeThreshold = settings.ScreenSpaceMergeThresholdPixels;
    classicSettings.TriangleBudget = settings.TriangleBudget;
    classicSettings.EnableLocalConstraints = settings.EnableLocalConstraints;
    classicSettings.EnableTopologyValidation = settings.EnableTopologyValidation;
    return classicSettings;
}

TerrainLodStats ClassicRoamTerrainLodAlgorithm::ToTerrainLodStats(const ClassicRoamStats& stats)
{
    // 将 Classic 统计映射到公共字段，使基准测试 CSV 可以直接比较不同实现
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
    lodStats.CpuWorkerCount = 1U;
    lodStats.CpuUpdateMilliseconds = stats.UpdateMilliseconds;
    lodStats.CpuPrepareMilliseconds = stats.PrepareMilliseconds;
    lodStats.CpuMergeCandidateMarkMilliseconds = stats.MergeCandidateMarkMilliseconds;
    lodStats.CpuMergeTopologyMilliseconds = stats.MergeTopologyMilliseconds;
    lodStats.CpuBudgetLeafCollectMilliseconds = stats.BudgetLeafCollectMilliseconds;
    // Classic 在刷新和处理细分优先队列时同步计算屏幕误差
    lodStats.CpuSplitCandidateMarkMilliseconds = stats.SplitInitialScanMilliseconds;
    lodStats.CpuSplitTopologySerialConvergenceMilliseconds =
        stats.SplitSerialConvergenceMilliseconds;
    lodStats.CpuSplitTopologyMilliseconds = stats.SplitSerialConvergenceMilliseconds;
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
} // 命名空间 ParallelRoam::Algorithms::ClassicRoam
