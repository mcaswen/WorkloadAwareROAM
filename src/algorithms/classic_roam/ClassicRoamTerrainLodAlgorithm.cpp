#include "algorithms/classic_roam/ClassicRoamTerrainLodAlgorithm.h"

#include "algorithms/TerrainLodProfiling.h"

namespace ParallelRoam::Algorithms::ClassicRoam
{
// Adapter 层只做接口映射
// ClassicRoamMeshBuilder 仍然拥有实际拓扑状态
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

    // renderer 和 benchmark 不依赖 Classic 内部类型
    // 失败语义统一收敛在算法接口层
    if (input.HeightMap == nullptr || !input.HeightMap->IsValid())
    {
        // 统一接口把无效 HeightMap 作为算法失败处理
        // builder 自身仍保留返回空 mesh 的低层语义
        if (errorMessage != nullptr)
        {
            *errorMessage = "Classic CPU ROAM build failed: invalid height map";
        }
        return false;
    }

    // 当前 Classic 路径输出 CPU mesh
    // Classic 只发布借用的 CPU mesh；GPU 资源字段保持为空。
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
    // Reset 通过重建 builder 丢弃持久拓扑
    // renderer 在算法开关或规则网格回退时会调用
    _builder = ClassicRoamMeshBuilder{};
    _stats = {};
}

ClassicRoamSettings ClassicRoamTerrainLodAlgorithm::ToClassicSettings(const TerrainLodSettings& settings)
{
    // 只复制 Classic builder 已支持的控制变量
    // TerrainLodSettings 中的 GPU 统计字段不进入此层
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
    // 统一 Stats 字段是 benchmark CSV 的稳定契约
    // Classic 的 split/merge/emit 时间映射到 CPU topology 和 mesh build 桶
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
    // Classic 在扫描并弹出 split priority queue 时计算 screen error
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
