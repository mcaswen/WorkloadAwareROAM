#include "app/RuntimeBenchmark.h"

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <stdexcept>

namespace ParallelRoam::App
{
namespace
{
struct RuntimeBenchmarkSummary
{
    // 汇总表只保留横向比较最常用的核心指标
    std::size_t SampleCount{0};

    // Frame ms 代表整帧体验，包含算法外的渲染和 UI 成本
    float AverageFrameMilliseconds{0.0F};
    float MaxFrameMilliseconds{0.0F};

    // Total LOD 覆盖算法更新到 renderer 输出可绘制数据的完整边界
    float AverageTotalLodMilliseconds{0.0F};
    float MaxTotalLodMilliseconds{0.0F};
    float AverageCpuUpdateMilliseconds{0.0F};
    float AverageCpuPrepareMilliseconds{0.0F};
    float AverageCpuMergeCandidateMarkMilliseconds{0.0F};
    float AverageCpuMergeTopologyMilliseconds{0.0F};
    float AverageCpuSplitTopologyChunkBuildMilliseconds{0.0F};
    float AverageCpuSplitTopologyQueueInvalidationMilliseconds{0.0F};
    float AverageCpuSplitTopologyParallelCommitMilliseconds{0.0F};
    float AverageCpuSplitTopologyResultMergeMilliseconds{0.0F};
    float AverageCpuSplitTopologyIndexQueueRefreshMilliseconds{0.0F};
    float AverageCpuSplitTopologySerialConvergenceMilliseconds{0.0F};
    float AverageCpuMergeTopologyChunkBuildMilliseconds{0.0F};
    float AverageCpuMergeTopologyQueueInvalidationMilliseconds{0.0F};
    float AverageCpuMergeTopologyParallelCommitMilliseconds{0.0F};
    float AverageCpuMergeTopologyResultMergeMilliseconds{0.0F};
    float AverageCpuMergeTopologyIndexQueueRefreshMilliseconds{0.0F};
    float AverageCpuMergeTopologySerialConvergenceMilliseconds{0.0F};
    float AverageCpuBudgetLeafCollectMilliseconds{0.0F};
    float AverageCpuErrorEvalMilliseconds{0.0F};
    float AverageCpuSplitCandidateMarkMilliseconds{0.0F};
    float AverageCpuSplitTopologyMilliseconds{0.0F};
    float AverageCpuFinalLeafCollectMilliseconds{0.0F};
    float AverageCpuMeshEmitMilliseconds{0.0F};
    float AverageCpuFinalizeMilliseconds{0.0F};
    float AverageCpuUploadMilliseconds{0.0F};
    float AverageSplitMilliseconds{0.0F};
    float AverageMergeMilliseconds{0.0F};
    float AverageEmitMilliseconds{0.0F};
    float AverageValidateMilliseconds{0.0F};
    float AverageFrameFenceWaitMilliseconds{0.0F};
    float MaxFrameFenceWaitMilliseconds{0.0F};
    float AverageRenderMilliseconds{0.0F};
    float MaxRenderMilliseconds{0.0F};

    // 三角形数量是画面复杂度和绘制提交压力的共同代理
    double AverageTriangles{0.0};
    std::size_t MaxTriangles{0};

    // 节点数量体现拓扑状态规模，和三角形数量不总是线性对应
    double AverageNodes{0.0};
    std::size_t MaxNodes{0};

    std::size_t CpuMeshFullRebuildCount{0};
    double AverageCpuMeshUpdatedTriangles{0.0};
    double AverageCpuMeshReusedTriangles{0.0};
    double AverageCpuMeshDirtyRanges{0.0};

    // CPU 百分比按单核 100% 口径，适合观察并行扩展
    float AverageCpuUtilizationPercent{0.0F};
    float MaxCpuUtilizationPercent{0.0F};

    // Worker 数记录算法本帧实际使用的 CPU 并行宽度
    std::size_t MaxCpuWorkerCount{0};
    std::size_t MaxCpuGpuUploadBytes{0};
    std::size_t MaxCpuGpuReadbackBytes{0};

    // 配置深度和实际达到深度分开，避免把 UI 设置误读成结果
    int MaxDepthSetting{0};
    int MaxDepthReached{0};

    // 拓扑问题合并输出，汇总表不用展开三类错误
    std::size_t MaxInvalidTopologyCount{0};
};

std::tm ToLocalTime(std::time_t timestamp)
{
    // localtime_r/localtime_s 避免使用带静态存储的 localtime
    std::tm localTime{};
#if defined(_WIN32)
    localtime_s(&localTime, &timestamp);
#else
    localtime_r(&timestamp, &localTime);
#endif
    return localTime;
}

std::string MakeReportTimestamp()
{
    // 时间戳放进文件名，连续多次点击 benchmark 不会覆盖旧结果
    const auto now = std::chrono::system_clock::now();
    const std::time_t timestamp = std::chrono::system_clock::to_time_t(now);
    const std::tm localTime = ToLocalTime(timestamp);

    std::ostringstream stream;
    stream << std::put_time(&localTime, "%Y%m%d-%H%M%S");
    return stream.str();
}

const Render::TerrainRenderStats* FindFirstSampleStats(
    const std::vector<RuntimeBenchmarkAlgorithmResult>& results)
{
    // 同一轮 benchmark 中算法共享地形和 UI 参数
    // 首个样本足够代表本轮配置
    for (const RuntimeBenchmarkAlgorithmResult& result : results)
    {
        if (!result.Samples.empty())
        {
            return &result.Samples.front().Stats;
        }
    }

    return nullptr;
}

RuntimeBenchmarkSummary SummarizeRuntimeBenchmark(const RuntimeBenchmarkAlgorithmResult& result)
{
    RuntimeBenchmarkSummary summary{};
    summary.SampleCount = result.Samples.size();
    if (summary.SampleCount == 0U)
    {
        // 空结果仍输出一行，方便发现某个算法没有采到样本
        return summary;
    }

    // 累加使用 double，避免长时间采样后平均值被 float 精度吞掉
    double totalFrameMilliseconds = 0.0;
    double totalLodMilliseconds = 0.0;
    double totalCpuUpdateMilliseconds = 0.0;
    double totalCpuPrepareMilliseconds = 0.0;
    double totalCpuMergeCandidateMarkMilliseconds = 0.0;
    double totalCpuMergeTopologyMilliseconds = 0.0;
    double totalCpuSplitTopologyChunkBuildMilliseconds = 0.0;
    double totalCpuSplitTopologyQueueInvalidationMilliseconds = 0.0;
    double totalCpuSplitTopologyParallelCommitMilliseconds = 0.0;
    double totalCpuSplitTopologyResultMergeMilliseconds = 0.0;
    double totalCpuSplitTopologyIndexQueueRefreshMilliseconds = 0.0;
    double totalCpuSplitTopologySerialConvergenceMilliseconds = 0.0;
    double totalCpuMergeTopologyChunkBuildMilliseconds = 0.0;
    double totalCpuMergeTopologyQueueInvalidationMilliseconds = 0.0;
    double totalCpuMergeTopologyParallelCommitMilliseconds = 0.0;
    double totalCpuMergeTopologyResultMergeMilliseconds = 0.0;
    double totalCpuMergeTopologyIndexQueueRefreshMilliseconds = 0.0;
    double totalCpuMergeTopologySerialConvergenceMilliseconds = 0.0;
    double totalCpuBudgetLeafCollectMilliseconds = 0.0;
    double totalCpuErrorEvalMilliseconds = 0.0;
    double totalCpuSplitCandidateMarkMilliseconds = 0.0;
    double totalCpuSplitTopologyMilliseconds = 0.0;
    double totalCpuFinalLeafCollectMilliseconds = 0.0;
    double totalCpuMeshEmitMilliseconds = 0.0;
    double totalCpuFinalizeMilliseconds = 0.0;
    double totalCpuUploadMilliseconds = 0.0;
    double totalSplitMilliseconds = 0.0;
    double totalMergeMilliseconds = 0.0;
    double totalEmitMilliseconds = 0.0;
    double totalValidateMilliseconds = 0.0;
    double totalFrameFenceWaitMilliseconds = 0.0;
    double totalRenderMilliseconds = 0.0;
    double totalTriangles = 0.0;
    double totalNodes = 0.0;
    double totalCpuMeshUpdatedTriangles = 0.0;
    double totalCpuMeshReusedTriangles = 0.0;
    double totalCpuMeshDirtyRanges = 0.0;
    double totalCpuUtilization = 0.0;

    for (const RuntimeBenchmarkSample& sample : result.Samples)
    {
        const Render::TerrainRenderStats& stats = sample.Stats;
        // 三类拓扑问题合并成表格中的一个核心风险指标
        const std::size_t invalidTopologyCount =
            stats.RoamTjunctionCount + stats.RoamInvalidNeighborCount + stats.RoamInvalidTopologyCount;

        // 平均值用于整体对比，最大值用于定位尖峰卡顿
        totalFrameMilliseconds += sample.FrameMilliseconds;
        totalLodMilliseconds += stats.RoamTotalMilliseconds;
        totalCpuUpdateMilliseconds += stats.RoamUpdateMilliseconds;
        totalCpuPrepareMilliseconds += stats.RoamCpuPrepareMilliseconds;
        totalCpuMergeCandidateMarkMilliseconds += stats.RoamCpuMergeCandidateMarkMilliseconds;
        totalCpuMergeTopologyMilliseconds += stats.RoamCpuMergeTopologyMilliseconds;
        totalCpuSplitTopologyChunkBuildMilliseconds +=
            stats.RoamCpuSplitTopologyChunkBuildMilliseconds;
        totalCpuSplitTopologyQueueInvalidationMilliseconds +=
            stats.RoamCpuSplitTopologyQueueInvalidationMilliseconds;
        totalCpuSplitTopologyParallelCommitMilliseconds +=
            stats.RoamCpuSplitTopologyParallelCommitMilliseconds;
        totalCpuSplitTopologyResultMergeMilliseconds +=
            stats.RoamCpuSplitTopologyResultMergeMilliseconds;
        totalCpuSplitTopologyIndexQueueRefreshMilliseconds +=
            stats.RoamCpuSplitTopologyIndexQueueRefreshMilliseconds;
        totalCpuSplitTopologySerialConvergenceMilliseconds +=
            stats.RoamCpuSplitTopologySerialConvergenceMilliseconds;
        totalCpuMergeTopologyChunkBuildMilliseconds +=
            stats.RoamCpuMergeTopologyChunkBuildMilliseconds;
        totalCpuMergeTopologyQueueInvalidationMilliseconds +=
            stats.RoamCpuMergeTopologyQueueInvalidationMilliseconds;
        totalCpuMergeTopologyParallelCommitMilliseconds +=
            stats.RoamCpuMergeTopologyParallelCommitMilliseconds;
        totalCpuMergeTopologyResultMergeMilliseconds +=
            stats.RoamCpuMergeTopologyResultMergeMilliseconds;
        totalCpuMergeTopologyIndexQueueRefreshMilliseconds +=
            stats.RoamCpuMergeTopologyIndexQueueRefreshMilliseconds;
        totalCpuMergeTopologySerialConvergenceMilliseconds +=
            stats.RoamCpuMergeTopologySerialConvergenceMilliseconds;
        totalCpuBudgetLeafCollectMilliseconds += stats.RoamCpuBudgetLeafCollectMilliseconds;
        totalCpuErrorEvalMilliseconds += stats.RoamCpuErrorEvalMilliseconds;
        totalCpuSplitCandidateMarkMilliseconds += stats.RoamCpuSplitCandidateMarkMilliseconds;
        totalCpuSplitTopologyMilliseconds += stats.RoamCpuSplitTopologyMilliseconds;
        totalCpuFinalLeafCollectMilliseconds += stats.RoamCpuFinalLeafCollectMilliseconds;
        totalCpuMeshEmitMilliseconds += stats.RoamCpuMeshEmitMilliseconds;
        totalCpuFinalizeMilliseconds += stats.RoamCpuFinalizeMilliseconds;
        totalCpuUploadMilliseconds += stats.RoamCpuUploadMilliseconds;
        totalSplitMilliseconds += stats.RoamSplitMilliseconds;
        totalMergeMilliseconds += stats.RoamMergeMilliseconds;
        totalEmitMilliseconds += stats.RoamEmitMilliseconds;
        totalValidateMilliseconds += stats.RoamValidateMilliseconds;
        totalFrameFenceWaitMilliseconds += stats.RoamFrameFenceWaitMilliseconds;
        totalRenderMilliseconds += stats.RoamRenderMilliseconds;
        totalTriangles += static_cast<double>(stats.TriangleCount);
        totalNodes += static_cast<double>(stats.RoamNodeCount);
        summary.CpuMeshFullRebuildCount += stats.RoamCpuMeshFullRebuildCount;
        totalCpuMeshUpdatedTriangles += static_cast<double>(stats.RoamCpuMeshUpdatedTriangleCount);
        totalCpuMeshReusedTriangles += static_cast<double>(stats.RoamCpuMeshReusedTriangleCount);
        totalCpuMeshDirtyRanges += static_cast<double>(stats.RoamCpuMeshDirtyRangeCount);
        totalCpuUtilization += stats.RoamCpuUtilizationPercent;

        summary.MaxFrameMilliseconds = std::max(summary.MaxFrameMilliseconds, sample.FrameMilliseconds);
        summary.MaxTotalLodMilliseconds =
            std::max(summary.MaxTotalLodMilliseconds, stats.RoamTotalMilliseconds);
        summary.MaxRenderMilliseconds =
            std::max(summary.MaxRenderMilliseconds, stats.RoamRenderMilliseconds);
        summary.MaxFrameFenceWaitMilliseconds =
            std::max(summary.MaxFrameFenceWaitMilliseconds, stats.RoamFrameFenceWaitMilliseconds);
        summary.MaxTriangles = std::max(summary.MaxTriangles, stats.TriangleCount);
        summary.MaxNodes = std::max(summary.MaxNodes, stats.RoamNodeCount);
        // CPU 占用和 worker 数一起观察并行路径是否真正生效
        summary.MaxCpuUtilizationPercent =
            std::max(summary.MaxCpuUtilizationPercent, stats.RoamCpuUtilizationPercent);
        summary.MaxCpuWorkerCount = std::max(summary.MaxCpuWorkerCount, stats.RoamCpuWorkerCount);
        summary.MaxCpuGpuUploadBytes = std::max(summary.MaxCpuGpuUploadBytes, stats.RoamCpuGpuUploadBytes);
        summary.MaxCpuGpuReadbackBytes = std::max(summary.MaxCpuGpuReadbackBytes, stats.RoamCpuGpuReadbackBytes);
        summary.MaxDepthSetting = std::max(summary.MaxDepthSetting, stats.RoamMaxDepthSetting);
        summary.MaxDepthReached = std::max(summary.MaxDepthReached, stats.RoamMaxDepthReached);
        summary.MaxInvalidTopologyCount = std::max(summary.MaxInvalidTopologyCount, invalidTopologyCount);
    }

    const double sampleCount = static_cast<double>(summary.SampleCount);
    summary.AverageFrameMilliseconds = static_cast<float>(totalFrameMilliseconds / sampleCount);
    summary.AverageTotalLodMilliseconds = static_cast<float>(totalLodMilliseconds / sampleCount);
    summary.AverageCpuUpdateMilliseconds = static_cast<float>(totalCpuUpdateMilliseconds / sampleCount);
    summary.AverageCpuPrepareMilliseconds = static_cast<float>(totalCpuPrepareMilliseconds / sampleCount);
    summary.AverageCpuMergeCandidateMarkMilliseconds =
        static_cast<float>(totalCpuMergeCandidateMarkMilliseconds / sampleCount);
    summary.AverageCpuMergeTopologyMilliseconds =
        static_cast<float>(totalCpuMergeTopologyMilliseconds / sampleCount);
    summary.AverageCpuSplitTopologyChunkBuildMilliseconds =
        static_cast<float>(totalCpuSplitTopologyChunkBuildMilliseconds / sampleCount);
    summary.AverageCpuSplitTopologyQueueInvalidationMilliseconds =
        static_cast<float>(totalCpuSplitTopologyQueueInvalidationMilliseconds / sampleCount);
    summary.AverageCpuSplitTopologyParallelCommitMilliseconds =
        static_cast<float>(totalCpuSplitTopologyParallelCommitMilliseconds / sampleCount);
    summary.AverageCpuSplitTopologyResultMergeMilliseconds =
        static_cast<float>(totalCpuSplitTopologyResultMergeMilliseconds / sampleCount);
    summary.AverageCpuSplitTopologyIndexQueueRefreshMilliseconds =
        static_cast<float>(totalCpuSplitTopologyIndexQueueRefreshMilliseconds / sampleCount);
    summary.AverageCpuSplitTopologySerialConvergenceMilliseconds =
        static_cast<float>(totalCpuSplitTopologySerialConvergenceMilliseconds / sampleCount);
    summary.AverageCpuMergeTopologyChunkBuildMilliseconds =
        static_cast<float>(totalCpuMergeTopologyChunkBuildMilliseconds / sampleCount);
    summary.AverageCpuMergeTopologyQueueInvalidationMilliseconds =
        static_cast<float>(totalCpuMergeTopologyQueueInvalidationMilliseconds / sampleCount);
    summary.AverageCpuMergeTopologyParallelCommitMilliseconds =
        static_cast<float>(totalCpuMergeTopologyParallelCommitMilliseconds / sampleCount);
    summary.AverageCpuMergeTopologyResultMergeMilliseconds =
        static_cast<float>(totalCpuMergeTopologyResultMergeMilliseconds / sampleCount);
    summary.AverageCpuMergeTopologyIndexQueueRefreshMilliseconds =
        static_cast<float>(totalCpuMergeTopologyIndexQueueRefreshMilliseconds / sampleCount);
    summary.AverageCpuMergeTopologySerialConvergenceMilliseconds =
        static_cast<float>(totalCpuMergeTopologySerialConvergenceMilliseconds / sampleCount);
    summary.AverageCpuBudgetLeafCollectMilliseconds =
        static_cast<float>(totalCpuBudgetLeafCollectMilliseconds / sampleCount);
    summary.AverageCpuErrorEvalMilliseconds = static_cast<float>(totalCpuErrorEvalMilliseconds / sampleCount);
    summary.AverageCpuSplitCandidateMarkMilliseconds =
        static_cast<float>(totalCpuSplitCandidateMarkMilliseconds / sampleCount);
    summary.AverageCpuSplitTopologyMilliseconds =
        static_cast<float>(totalCpuSplitTopologyMilliseconds / sampleCount);
    summary.AverageCpuFinalLeafCollectMilliseconds =
        static_cast<float>(totalCpuFinalLeafCollectMilliseconds / sampleCount);
    summary.AverageCpuMeshEmitMilliseconds = static_cast<float>(totalCpuMeshEmitMilliseconds / sampleCount);
    summary.AverageCpuFinalizeMilliseconds = static_cast<float>(totalCpuFinalizeMilliseconds / sampleCount);
    summary.AverageCpuUploadMilliseconds = static_cast<float>(totalCpuUploadMilliseconds / sampleCount);
    summary.AverageSplitMilliseconds = static_cast<float>(totalSplitMilliseconds / sampleCount);
    summary.AverageMergeMilliseconds = static_cast<float>(totalMergeMilliseconds / sampleCount);
    summary.AverageEmitMilliseconds = static_cast<float>(totalEmitMilliseconds / sampleCount);
    summary.AverageValidateMilliseconds = static_cast<float>(totalValidateMilliseconds / sampleCount);
    summary.AverageFrameFenceWaitMilliseconds =
        static_cast<float>(totalFrameFenceWaitMilliseconds / sampleCount);
    summary.AverageRenderMilliseconds = static_cast<float>(totalRenderMilliseconds / sampleCount);
    summary.AverageTriangles = totalTriangles / sampleCount;
    summary.AverageNodes = totalNodes / sampleCount;
    summary.AverageCpuMeshUpdatedTriangles = totalCpuMeshUpdatedTriangles / sampleCount;
    summary.AverageCpuMeshReusedTriangles = totalCpuMeshReusedTriangles / sampleCount;
    summary.AverageCpuMeshDirtyRanges = totalCpuMeshDirtyRanges / sampleCount;
    summary.AverageCpuUtilizationPercent = static_cast<float>(totalCpuUtilization / sampleCount);
    return summary;
}

RuntimeBenchmarkSummary SummaryForAlgorithm(
    const std::vector<RuntimeBenchmarkAlgorithmResult>& results,
    Algorithms::TerrainLodAlgorithmId algorithmId)
{
    const auto result = std::find_if(
        results.begin(),
        results.end(),
        [algorithmId](const RuntimeBenchmarkAlgorithmResult& candidate)
        {
            return candidate.AlgorithmId == algorithmId;
        });
    return result == results.end() ? RuntimeBenchmarkSummary{} : SummarizeRuntimeBenchmark(*result);
}

void WriteDetailedCsv(
    const std::filesystem::path& csvPath,
    const std::vector<RuntimeBenchmarkAlgorithmResult>& results)
{
    // CSV 保存逐帧明细，后续可直接导入表格或脚本做曲线
    std::ofstream csv{csvPath};
    if (!csv)
    {
        throw std::runtime_error{"Failed to create runtime benchmark CSV: " + csvPath.string()};
    }

    // 配置字段放在时间序列前，方便按高度图和参数筛选
    csv << "algorithm,buildConfiguration,graphicsBackend,graphicsAdapter,graphicsVersion,vSyncEnabled,"
        << "heightMapPath,heightMapWidth,heightMapHeight,terrainSize,heightScale,"
        << "maxDepthSetting,screenSpaceSplitThresholdPixels,"
        << "screenSpaceMergeThresholdPixels,triangleBudget,dodParallelSplitEnabled,"
        << "pathSampleIndex,pathSampleCount,pathProgress,timeSeconds,"
        << "cameraX,cameraY,cameraZ,frameMilliseconds,triangles,nodes,"
        << "activeSplits,splits,forcedSplits,merges,candidatePeak,persistentSplitQueueSize,persistentMergeQueueSize,"
        << "queueCrossoverCount,queueMembershipUpdateCount,cpuMeshFullRebuildCount,"
        << "cpuMeshUpdatedTriangleCount,cpuMeshReusedTriangleCount,cpuMeshDirtyRangeCount,"
        << "budgetRejectedSplits,tjunctions,invalidNeighbors,"
        << "invalidTopology,cpuWorkers,cpuUtilizationPercent,lodTotalMilliseconds,"
        << "cpuUpdateMilliseconds,cpuPrepareMilliseconds,cpuMergeCandidateMarkMilliseconds,"
        << "cpuMergeTopologyMilliseconds,cpuBudgetLeafCollectMilliseconds,cpuErrorEvalMilliseconds,"
        << "cpuSplitCandidateMarkMilliseconds,cpuSplitTopologyMilliseconds,"
        << "cpuSplitTopologyChunkBuildMilliseconds,cpuSplitTopologyQueueInvalidationMilliseconds,"
        << "cpuSplitTopologyParallelCommitMilliseconds,cpuSplitTopologyResultMergeMilliseconds,"
        << "cpuSplitTopologyIndexQueueRefreshMilliseconds,cpuSplitTopologySerialConvergenceMilliseconds,"
        << "cpuMergeTopologyChunkBuildMilliseconds,cpuMergeTopologyQueueInvalidationMilliseconds,"
        << "cpuMergeTopologyParallelCommitMilliseconds,cpuMergeTopologyResultMergeMilliseconds,"
        << "cpuMergeTopologyIndexQueueRefreshMilliseconds,cpuMergeTopologySerialConvergenceMilliseconds,"
        << "cpuFinalLeafCollectMilliseconds,cpuMeshEmitMilliseconds,cpuFinalizeMilliseconds,"
        << "cpuUploadMilliseconds,"
        << "frameFenceWaitMilliseconds,renderMilliseconds,"
        << "cpuGpuUploadBytes,cpuGpuReadbackBytes,splitMilliseconds,"
        << "mergeMilliseconds,emitMilliseconds,validateMilliseconds,maxDepthReached\n";

    csv << std::fixed << std::setprecision(3);
    for (const RuntimeBenchmarkAlgorithmResult& result : results)
    {
        // 算法名逐行写入，便于把多个算法拼在同一个 CSV 中筛选
        for (const RuntimeBenchmarkSample& sample : result.Samples)
        {
            const Render::TerrainRenderStats& stats = sample.Stats;
            csv << result.AlgorithmName << ','
                << sample.BuildConfiguration << ','
                << sample.GraphicsBackend << ','
                << sample.GraphicsAdapter << ','
                << sample.GraphicsVersion << ','
                << (sample.VSyncEnabled ? "true" : "false") << ','
                << stats.HeightMapPath.generic_string() << ','
                << stats.HeightMapWidth << ','
                << stats.HeightMapHeight << ','
                << stats.TerrainSize << ','
                << stats.HeightScale << ','
                << stats.RoamMaxDepthSetting << ','
                << stats.RoamScreenSpaceSplitThresholdPixels << ','
                << stats.RoamScreenSpaceMergeThresholdPixels << ','
                << stats.RoamTriangleBudgetSetting << ','
                << (stats.RoamParallelSplitEnabled ? "true" : "false") << ','
                << sample.PathSampleIndex << ','
                << sample.PathSampleCount << ','
                << sample.PathProgress << ','
                << sample.TimeSeconds << ','
                << sample.CameraPosition.x << ','
                << sample.CameraPosition.y << ','
                << sample.CameraPosition.z << ','
                << sample.FrameMilliseconds << ','
                << stats.TriangleCount << ','
                << stats.RoamNodeCount << ','
                << stats.RoamActiveSplitCount << ','
                << stats.RoamSplitCount << ','
                << stats.RoamForcedSplitCount << ','
                << stats.RoamMergeCount << ','
                << stats.RoamCandidatePeakCount << ','
                << stats.RoamPersistentSplitQueueSize << ','
                << stats.RoamPersistentMergeQueueSize << ','
                << stats.RoamQueueCrossoverCount << ','
                << stats.RoamQueueMembershipUpdateCount << ','
                << stats.RoamCpuMeshFullRebuildCount << ','
                << stats.RoamCpuMeshUpdatedTriangleCount << ','
                << stats.RoamCpuMeshReusedTriangleCount << ','
                << stats.RoamCpuMeshDirtyRangeCount << ','
                << stats.RoamBudgetRejectedSplitCount << ','
                << stats.RoamTjunctionCount << ','
                << stats.RoamInvalidNeighborCount << ','
                << stats.RoamInvalidTopologyCount << ','
                << stats.RoamCpuWorkerCount << ','
                << stats.RoamCpuUtilizationPercent << ','
                << stats.RoamTotalMilliseconds << ','
                << stats.RoamUpdateMilliseconds << ','
                << stats.RoamCpuPrepareMilliseconds << ','
                << stats.RoamCpuMergeCandidateMarkMilliseconds << ','
                << stats.RoamCpuMergeTopologyMilliseconds << ','
                << stats.RoamCpuBudgetLeafCollectMilliseconds << ','
                << stats.RoamCpuErrorEvalMilliseconds << ','
                << stats.RoamCpuSplitCandidateMarkMilliseconds << ','
                << stats.RoamCpuSplitTopologyMilliseconds << ','
                << stats.RoamCpuSplitTopologyChunkBuildMilliseconds << ','
                << stats.RoamCpuSplitTopologyQueueInvalidationMilliseconds << ','
                << stats.RoamCpuSplitTopologyParallelCommitMilliseconds << ','
                << stats.RoamCpuSplitTopologyResultMergeMilliseconds << ','
                << stats.RoamCpuSplitTopologyIndexQueueRefreshMilliseconds << ','
                << stats.RoamCpuSplitTopologySerialConvergenceMilliseconds << ','
                << stats.RoamCpuMergeTopologyChunkBuildMilliseconds << ','
                << stats.RoamCpuMergeTopologyQueueInvalidationMilliseconds << ','
                << stats.RoamCpuMergeTopologyParallelCommitMilliseconds << ','
                << stats.RoamCpuMergeTopologyResultMergeMilliseconds << ','
                << stats.RoamCpuMergeTopologyIndexQueueRefreshMilliseconds << ','
                << stats.RoamCpuMergeTopologySerialConvergenceMilliseconds << ','
                << stats.RoamCpuFinalLeafCollectMilliseconds << ','
                << stats.RoamCpuMeshEmitMilliseconds << ','
                << stats.RoamCpuFinalizeMilliseconds << ','
                << stats.RoamCpuUploadMilliseconds << ','
                << stats.RoamFrameFenceWaitMilliseconds << ','
                << stats.RoamRenderMilliseconds << ','
                << stats.RoamCpuGpuUploadBytes << ','
                << stats.RoamCpuGpuReadbackBytes << ','
                << stats.RoamSplitMilliseconds << ','
                << stats.RoamMergeMilliseconds << ','
                << stats.RoamEmitMilliseconds << ','
                << stats.RoamValidateMilliseconds << ','
                << stats.RoamMaxDepthReached << '\n';
        }
    }
}

void WriteSummaryMarkdown(
    const std::filesystem::path& markdownPath,
    const std::filesystem::path& csvPath,
    const std::vector<RuntimeBenchmarkAlgorithmResult>& results,
    const std::vector<std::string>& notes)
{
    // Markdown 是面向人看的主输出，字段数量控制在一屏内
    std::ofstream markdown{markdownPath};
    if (!markdown)
    {
        throw std::runtime_error{"Failed to create runtime benchmark table: " + markdownPath.string()};
    }

    markdown << "# 运行时基准测试报告\n\n";
    std::size_t pathSampleCount = 0;
    bool sampleSequenceComplete = !results.empty();
    for (const RuntimeBenchmarkAlgorithmResult& result : results)
    {
        if (!result.Samples.empty())
        {
            if (pathSampleCount == 0U)
            {
                pathSampleCount = result.Samples.front().PathSampleCount;
            }
            sampleSequenceComplete = sampleSequenceComplete && result.Samples.size() == pathSampleCount;
            for (std::size_t sampleIndex = 0; sampleIndex < result.Samples.size(); ++sampleIndex)
            {
                const RuntimeBenchmarkSample& sample = result.Samples[sampleIndex];
                sampleSequenceComplete = sampleSequenceComplete &&
                    sample.PathSampleIndex == sampleIndex &&
                    sample.PathSampleCount == pathSampleCount;
            }
        }
        else
        {
            sampleSequenceComplete = false;
        }
    }

    markdown << "- 相机路径：离散采样点序列；具体路径类型见下方 Benchmark 路径\n";
    markdown << "- 每种算法的路径采样点数：" << pathSampleCount << "\n";
    markdown << "- 采样规则：每种算法按相同 sampleIndex 执行全部相机姿态，完成后再切换算法\n";
    markdown << "- 采样完整性：" << (sampleSequenceComplete ? "完整" : "不完整") << "\n";
    markdown << "- timeSeconds：当前算法实际经过的墙钟时间，不参与路径推进\n";
    markdown << "- 详细 CSV：`" << csvPath.filename().string() << "`\n\n";
    for (const std::string& note : notes)
    {
        markdown << "- " << note << "\n";
    }
    if (!notes.empty())
    {
        markdown << '\n';
    }

    if (const Render::TerrainRenderStats* stats = FindFirstSampleStats(results))
    {
        // 顶部配置块解释 UI 设置和实际达到深度的差异
        markdown << "- Height map：`" << stats->HeightMapPath.generic_string() << "` "
                 << stats->HeightMapWidth << "x" << stats->HeightMapHeight << "\n";
        markdown << "- Terrain size：" << stats->TerrainSize << "\n";
        markdown << "- Height scale：" << stats->HeightScale << "\n";
        markdown << "- Max depth 设置：" << stats->RoamMaxDepthSetting << "\n";
        markdown << "- ROAM 屏幕空间 split/merge 阈值："
                 << stats->RoamScreenSpaceSplitThresholdPixels << " px / "
                 << stats->RoamScreenSpaceMergeThresholdPixels << " px\n";
        markdown << "- ROAM triangle budget：" << stats->RoamTriangleBudgetSetting << "\n";
        markdown << "- DOD 并行 Split："
                 << (stats->RoamParallelSplitEnabled ? "开启" : "关闭") << "\n\n";
    }
    markdown << "## 总体结果\n\n";
    markdown << "| Algorithm | Samples | Avg Frame ms | Max Frame ms | Avg LOD ms | Max LOD ms | "
             << "Avg Triangles | Max Triangles | Avg Nodes | Max Nodes | Avg CPU % | Max CPU % | "
             << "Max Workers | Config Max Depth | Reached Max Depth | Max Topology Issues |\n";
    markdown << "| ---";
    for (int column = 0; column < 15; ++column)
    {
        markdown << " | ---:";
    }
    markdown << " |\n";
    markdown << std::fixed << std::setprecision(2);

    for (const RuntimeBenchmarkAlgorithmResult& result : results)
    {
        // 每个算法一行，和 UI 顺序保持一致
        const RuntimeBenchmarkSummary summary = SummarizeRuntimeBenchmark(result);
        markdown << "| " << result.AlgorithmName
                 << " | " << summary.SampleCount
                 << " | " << summary.AverageFrameMilliseconds
                 << " | " << summary.MaxFrameMilliseconds
                 << " | " << summary.AverageTotalLodMilliseconds
                 << " | " << summary.MaxTotalLodMilliseconds
                 << " | " << summary.AverageTriangles
                 << " | " << summary.MaxTriangles
                 << " | " << summary.AverageNodes
                 << " | " << summary.MaxNodes
                 << " | " << summary.AverageCpuUtilizationPercent
                 << " | " << summary.MaxCpuUtilizationPercent
                 << " | " << summary.MaxCpuWorkerCount
                 << " | " << summary.MaxDepthSetting
                 << " | " << summary.MaxDepthReached
                 << " | " << summary.MaxInvalidTopologyCount
                 << " |\n";
    }

    const RuntimeBenchmarkSummary classicSummary =
        SummaryForAlgorithm(results, Algorithms::TerrainLodAlgorithmId::ClassicCpuRoam);
    const RuntimeBenchmarkSummary dodSummary =
        SummaryForAlgorithm(results, Algorithms::TerrainLodAlgorithmId::DataOrientedCpuRoam);
    markdown << "\n## ROAM 逻辑阶段对比\n\n";
    markdown << "表中数值均为平均毫秒数。Classic 与 DOD 使用相同的屏幕空间误差、迟滞阈值、"
             << "活动叶预算和增量 mesh 输出语义。\n\n";
    markdown << "| ROAM 逻辑阶段 | Classic CPU | DOD CPU | 实现说明 |\n";
    markdown << "| --- | ---: | ---: | --- |\n";
    markdown << std::fixed << std::setprecision(4);
    markdown << "| Prepare / 帧状态 | " << classicSummary.AverageCpuPrepareMilliseconds
             << " | " << dodSummary.AverageCpuPrepareMilliseconds
             << " | 准备持久拓扑和逐帧输入 |\n";
    markdown << "| Merge 候选评分 | " << classicSummary.AverageCpuMergeCandidateMarkMilliseconds
             << " | " << dodSummary.AverageCpuMergeCandidateMarkMilliseconds
             << " | 刷新持久 Q_m 的优先级 |\n";
    markdown << "| Merge 拓扑提交 / 向上级联 | " << classicSummary.AverageCpuMergeTopologyMilliseconds
             << " | " << dodSummary.AverageCpuMergeTopologyMilliseconds
             << " | 提交 diamond merge、邻接修复和级联回收 |\n";
    markdown << "| Split 前 active leaf 收集 | " << classicSummary.AverageCpuBudgetLeafCollectMilliseconds
             << " | " << dodSummary.AverageCpuBudgetLeafCollectMilliseconds
             << " | 两者均直接复用持久活动叶表示 |\n";
    markdown << "| 视点相关 leaf error / 视锥测试 | " << classicSummary.AverageCpuErrorEvalMilliseconds
             << " | " << dodSummary.AverageCpuErrorEvalMilliseconds
             << " | 计入持久 Q_s 优先级刷新，因此独立字段为 0 |\n";
    markdown << "| Split 扫描/标记 | " << classicSummary.AverageCpuSplitCandidateMarkMilliseconds
             << " | " << dodSummary.AverageCpuSplitCandidateMarkMilliseconds
             << " | 刷新 Q_s、建堆并生成提交候选 |\n";
    markdown << "| Split 拓扑 / 裂缝约束提交 | " << classicSummary.AverageCpuSplitTopologyMilliseconds
             << " | " << dodSummary.AverageCpuSplitTopologyMilliseconds
             << " | 按同一预算与 forced-split 约束提交 |\n";
    markdown << "| 细分后 active leaf 收集 / 输出视图 | " << classicSummary.AverageCpuFinalLeafCollectMilliseconds
             << " | " << dodSummary.AverageCpuFinalLeafCollectMilliseconds
             << " | Classic 复用 dense slot owners，DOD 复用 ActiveLeafNodes |\n";
    markdown << "| Mesh emit / draw argument 生成 | " << classicSummary.AverageCpuMeshEmitMilliseconds
             << " | " << dodSummary.AverageCpuMeshEmitMilliseconds
             << " | 两者均只重写 dirty slot；DOD 可将较大批次分段给 worker |\n";
    markdown << "| Finalize / 发布 packet | " << classicSummary.AverageCpuFinalizeMilliseconds
             << " | " << dodSummary.AverageCpuFinalizeMilliseconds
             << " | 汇总统计、更新跨帧状态并发布 renderer packet |\n";

    markdown << "\n## CPU 实现阶段\n\n";
    markdown << "`CPU update` 包含下表中互斥的物理执行区间；`CPU upload` 是算法返回后的 renderer 上传。"
             << "Classic 与 DOD 都持久维护 Q_s/Q_m，并在每个 Build 刷新现有成员的优先级。DOD 对两队列的评分并行化，"
             << "`Split scan/mark` 包含 Q_s 优先级刷新、原地建堆和提交快照生成，`Merge mark` 对应 Q_m 的同类工作。"
             << "DOD 满预算时持续执行 merge-first 资源交换，直到 max(Q_s) 不再高于 min(Q_m)。"
             << "两者单独的 `Error eval` 都为零；Classic 与 DOD 的 `Mesh emit` 都是 dirty-slot 增量更新，"
             << "DOD 对较大的 dirty 批次沿用 worker 分段，因此两者差异不能解释为增量与全量策略差异。\n\n";
    markdown << "| Algorithm | CPU update | Prepare | Merge mark | Merge topology | Budget leaf collect | "
             << "Error eval | Split scan/mark | Split topology | Final leaf collect/view | Mesh emit | "
             << "Finalize | CPU upload |\n";
    markdown << "| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |\n";
    for (const RuntimeBenchmarkAlgorithmResult& result : results)
    {
        const RuntimeBenchmarkSummary summary = SummarizeRuntimeBenchmark(result);
        markdown << "| " << result.AlgorithmName
                 << " | " << summary.AverageCpuUpdateMilliseconds
                 << " | " << summary.AverageCpuPrepareMilliseconds
                 << " | " << summary.AverageCpuMergeCandidateMarkMilliseconds
                 << " | " << summary.AverageCpuMergeTopologyMilliseconds
                 << " | " << summary.AverageCpuBudgetLeafCollectMilliseconds
                 << " | " << summary.AverageCpuErrorEvalMilliseconds
                 << " | " << summary.AverageCpuSplitCandidateMarkMilliseconds
                 << " | " << summary.AverageCpuSplitTopologyMilliseconds
                 << " | " << summary.AverageCpuFinalLeafCollectMilliseconds
                 << " | " << summary.AverageCpuMeshEmitMilliseconds
                 << " | " << summary.AverageCpuFinalizeMilliseconds
                 << " | " << summary.AverageCpuUploadMilliseconds << " |\n";
    }

    const float classicSplitTopologyDetailSum =
        classicSummary.AverageCpuSplitTopologySerialConvergenceMilliseconds;
    const float dodSplitTopologyDetailSum =
        dodSummary.AverageCpuSplitTopologyChunkBuildMilliseconds +
        dodSummary.AverageCpuSplitTopologyQueueInvalidationMilliseconds +
        dodSummary.AverageCpuSplitTopologyParallelCommitMilliseconds +
        dodSummary.AverageCpuSplitTopologyResultMergeMilliseconds +
        dodSummary.AverageCpuSplitTopologyIndexQueueRefreshMilliseconds +
        dodSummary.AverageCpuSplitTopologySerialConvergenceMilliseconds;
    const float dodMergeTopologyDetailSum =
        dodSummary.AverageCpuMergeTopologyChunkBuildMilliseconds +
        dodSummary.AverageCpuMergeTopologyQueueInvalidationMilliseconds +
        dodSummary.AverageCpuMergeTopologyParallelCommitMilliseconds +
        dodSummary.AverageCpuMergeTopologyResultMergeMilliseconds +
        dodSummary.AverageCpuMergeTopologyIndexQueueRefreshMilliseconds +
        dodSummary.AverageCpuMergeTopologySerialConvergenceMilliseconds;
    markdown << "\n### CPU Split 拓扑阶段计时（统一口径）\n\n";
    markdown << "六段均为互斥执行区间。Classic 不执行并行预提交，因此前五项为 0；它与 DOD 的"
             << "`串行收敛` 都从候选刷新结束后开始，并扣除循环中执行的 merge。`六段合计` 与"
             << "`Topology total` 的差值是函数调用、worker 数决策和少量循环控制等尚未单列的开销。\n\n";
    markdown << "| 操作 | 候选排序/分桶 | 队列邻域失效 | chunk 并行提交 | worker 结果汇总 | "
             << "活动叶索引/队列刷新 | 串行收敛 | 六段合计 | Topology total |\n";
    markdown << "| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |\n";
    markdown << "| Classic Split | 0 | 0 | 0 | 0 | 0 | "
             << classicSummary.AverageCpuSplitTopologySerialConvergenceMilliseconds
             << " | " << classicSplitTopologyDetailSum
             << " | " << classicSummary.AverageCpuSplitTopologyMilliseconds << " |\n";
    markdown << "| DoD Split | " << dodSummary.AverageCpuSplitTopologyChunkBuildMilliseconds
             << " | " << dodSummary.AverageCpuSplitTopologyQueueInvalidationMilliseconds
             << " | " << dodSummary.AverageCpuSplitTopologyParallelCommitMilliseconds
             << " | " << dodSummary.AverageCpuSplitTopologyResultMergeMilliseconds
             << " | " << dodSummary.AverageCpuSplitTopologyIndexQueueRefreshMilliseconds
             << " | " << dodSummary.AverageCpuSplitTopologySerialConvergenceMilliseconds
             << " | " << dodSplitTopologyDetailSum
             << " | " << dodSummary.AverageCpuSplitTopologyMilliseconds << " |\n";
    markdown << "| Merge | " << dodSummary.AverageCpuMergeTopologyChunkBuildMilliseconds
             << " | " << dodSummary.AverageCpuMergeTopologyQueueInvalidationMilliseconds
             << " | " << dodSummary.AverageCpuMergeTopologyParallelCommitMilliseconds
             << " | " << dodSummary.AverageCpuMergeTopologyResultMergeMilliseconds
             << " | " << dodSummary.AverageCpuMergeTopologyIndexQueueRefreshMilliseconds
             << " | " << dodSummary.AverageCpuMergeTopologySerialConvergenceMilliseconds
             << " | " << dodMergeTopologyDetailSum
             << " | " << dodSummary.AverageCpuMergeTopologyMilliseconds << " |\n";

    markdown << "\n### 增量 CPU Mesh 输出\n\n";
    markdown << "`Full rebuilds` 是采样窗口内的全量初始化次数；其余列是逐帧平均值。"
             << "Classic 与 DOD 都填充稳定 slot、dirty range 和复用统计。"
             << "D3D12 的 frame slot 会延迟消费两次使用之间积累的 dirty range 并集，"
             << "因此 `Max upload bytes` 表示实际补齐量，不要求等于当前 Build 的 updated triangles。\n\n";
    markdown << "| Algorithm | Full rebuilds | Updated triangles | Reused triangles | Dirty ranges | Max upload bytes |\n";
    markdown << "| --- | ---: | ---: | ---: | ---: | ---: |\n";
    for (const RuntimeBenchmarkAlgorithmResult& result : results)
    {
        const RuntimeBenchmarkSummary summary = SummarizeRuntimeBenchmark(result);
        markdown << "| " << result.AlgorithmName
                 << " | " << summary.CpuMeshFullRebuildCount
                 << " | " << summary.AverageCpuMeshUpdatedTriangles
                 << " | " << summary.AverageCpuMeshReusedTriangles
                 << " | " << summary.AverageCpuMeshDirtyRanges
                 << " | " << summary.MaxCpuGpuUploadBytes << " |\n";
    }

    markdown << "\n### 原生 pass 包络\n\n";
    markdown << "这些是各实现原有的外围 pass 计时。它们与上方互斥阶段重叠，不能重复相加。\n\n";
    markdown << "| Algorithm | Split | Merge | Emit | Validate |\n";
    markdown << "| --- | ---: | ---: | ---: | ---: |\n";
    for (const RuntimeBenchmarkAlgorithmResult& result : results)
    {
        const RuntimeBenchmarkSummary summary = SummarizeRuntimeBenchmark(result);
        markdown << "| " << result.AlgorithmName
                 << " | " << summary.AverageSplitMilliseconds
                 << " | " << summary.AverageMergeMilliseconds
                 << " | " << summary.AverageEmitMilliseconds
                 << " | " << summary.AverageValidateMilliseconds << " |\n";
    }

    markdown << "\n## 渲染与上传\n\n";
    markdown << std::setprecision(2);
    markdown << "| Algorithm | Frame fence wait | Render | Max render | Max upload B | Max readback B |\n";
    markdown << "| --- | ---: | ---: | ---: | ---: | ---: |\n";
    for (const RuntimeBenchmarkAlgorithmResult& result : results)
    {
        const RuntimeBenchmarkSummary summary = SummarizeRuntimeBenchmark(result);
        markdown << "| " << result.AlgorithmName
                 << " | " << summary.AverageFrameFenceWaitMilliseconds
                 << " | " << summary.AverageRenderMilliseconds
                 << " | " << summary.MaxRenderMilliseconds
                 << " | " << summary.MaxCpuGpuUploadBytes
                 << " | " << summary.MaxCpuGpuReadbackBytes << " |\n";
    }
}
} // namespace

std::string RuntimeBenchmarkAlgorithmDisplayName(Algorithms::TerrainLodAlgorithmId algorithmId)
{
    switch (algorithmId)
    {
    case Algorithms::TerrainLodAlgorithmId::ClassicCpuRoam:
        return "Classic CPU ROAM";
    case Algorithms::TerrainLodAlgorithmId::DataOrientedCpuRoam:
        return "Data-Oriented CPU ROAM";
    case Algorithms::TerrainLodAlgorithmId::Count:
        break;
    }

    return "Unknown ROAM";
}

RuntimeBenchmarkReportPaths WriteRuntimeBenchmarkReport(
    const std::vector<RuntimeBenchmarkAlgorithmResult>& results,
    const std::vector<std::string>& notes)
{
    // benchmark-output 已被 gitignore 忽略，生成报告不会污染提交
    const std::filesystem::path outputDirectory{"benchmark-output"};
    std::filesystem::create_directories(outputDirectory);

    const std::string timestamp = MakeReportTimestamp();
    RuntimeBenchmarkReportPaths paths{};
    // 同一时间戳绑定 Markdown 和 CSV，用户可以互相追溯
    paths.MarkdownPath = outputDirectory / ("runtime-benchmark-" + timestamp + ".md");
    paths.CsvPath = outputDirectory / ("runtime-benchmark-" + timestamp + ".csv");

    // 先写 CSV 再写 Markdown，汇总表可以引用已确定的明细文件名
    WriteDetailedCsv(paths.CsvPath, results);
    WriteSummaryMarkdown(paths.MarkdownPath, paths.CsvPath, results, notes);
    // 返回两个路径，让 Application 同时输出日志和刷新 UI
    return paths;
}
} // namespace ParallelRoam::App
