#pragma once

#include "algorithms/TerrainLodPassTrace.h"
#include "algorithms/ITerrainLodAlgorithm.h"

#include <cstddef>
#include <string>
#include <vector>

namespace ParallelRoam::App
{
/// <summary>
/// 选择常规相机路径或预算饱和压力路径
/// </summary>
enum class RuntimeBenchmarkPath
{
    Default,
    BudgetSaturation,
};

/// <summary>
/// 保存命令行显式覆盖的运行时实验参数
/// </summary>
struct RuntimeBenchmarkOverrides
{
    // 空名单保留旧双算法实验；显式算法同时可用于交互启动
    std::vector<Algorithms::TerrainLodAlgorithmId> AlgorithmSequence;
    bool HasInteractiveAlgorithm{false};
    ParallelRoam::Algorithms::TerrainLodAlgorithmId InteractiveAlgorithm{
        ParallelRoam::Algorithms::TerrainLodAlgorithmId::ClassicCpuRoam};
    bool HasTransactional{false};
    ParallelRoam::Algorithms::TransactionalLodSettings Transactional{};
    bool HasTriangleBudget{false};
    std::size_t TriangleBudget{20000};
    // 每个是否覆盖标记用于区分未指定和显式使用默认值
    bool HasPath{false};
    RuntimeBenchmarkPath Path{RuntimeBenchmarkPath::Default};

    bool HasPassPolicy{false};
    Algorithms::TerrainLodPassPolicy PassPolicy{};

    // 两个阈值分别控制细分和合并阶段启用并行辅助的最低候选数量
    bool HasSplitTopologyMinParallelCandidateCount{false};
    std::size_t SplitTopologyMinParallelCandidateCount{32U};

    bool HasMergeTopologyMinParallelCandidateCount{false};
    std::size_t MergeTopologyMinParallelCandidateCount{160U};

    // 非零值只在指定的更新序号启用并行辅助，便于复现实验帧
    bool HasParallelTopologyTargetBuild{false};
    std::size_t ParallelTopologyTargetBuild{0U};

    bool HasParallelTopologyPhase{false};
    Algorithms::TerrainLodParallelTopologyPhase ParallelTopologyPhase{
        Algorithms::TerrainLodParallelTopologyPhase::Both};

    bool HasHeightMapIndex{false};
    int HeightMapIndex{0};

    bool HasTerrainSize{false};
    float TerrainSize{30.0F};

    bool HasHeightScale{false};
    float HeightScale{4.0F};

    bool HasMaxDepth{false};
    int MaxDepth{14};

    bool HasScreenSpaceSplitThresholdPixels{false};
    float ScreenSpaceSplitThresholdPixels{4.0F};

    bool HasScreenSpaceMergeThresholdPixels{false};
    float ScreenSpaceMergeThresholdPixels{2.0F};

    bool HasSampleCount{false};
    std::size_t SampleCount{0U};

    bool HasWarmupSampleCount{false};
    std::size_t WarmupSampleCount{0U};

    bool HasAlgorithmOrderRotation{false};
    std::size_t AlgorithmOrderRotation{0U};

    // 上传配对只对 DOD 的非初始化 CPU 网格数据包执行
    bool EnableCpuUploadPairReplay{false};
    std::size_t CpuUploadWarmupCount{5U};
    std::size_t CpuUploadRepeatCount{30U};
    std::size_t CpuUploadTargetCount{0U};

    // 标签只用于区分实验报告，不改变算法配置
    std::string Label;
};
} // 命名空间 ParallelRoam::App
