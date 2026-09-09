#pragma once

#include "algorithms/TerrainLodPassTrace.h"
#include "experiment/formal/FormalExperimentTypes.h"

#include <cstddef>
#include <filesystem>
#include <string>

namespace ParallelRoam::Benchmark
{
/// <summary>
/// 指定无窗口基准运行单个算法，或依次运行全部可用算法
/// </summary>
enum class BenchmarkAlgorithmSelection
{
    Classic,
    DataOriented,
    All,
};

/// <summary>
/// 选择无窗口入口要执行的任务，包括固定场景回归、输入准备和工作量发现
/// 各任务由运行入口分流，并非都使用同一套场景参数
/// </summary>
enum class BenchmarkProfile
{
    Smoke,
    BudgetReentry,
    BudgetSaturation,
    IncrementalEmit,
    PassTraceReplay,
    PassPolicyReplay,
    TopologyPairReplay,
    ClassicDodContract,
    PassCrossoverReplay,
    PassCrossoverStressReplay,
    Standard,
    CpuPilotInputs,
    CpuWorkloadDiscovery,
    CpuPassPairPilot,
};

enum class BenchmarkPassPolicySelection
{
    Default,
    SerialIncremental,
    MaximumParallelIncremental,
    SerialFull,
    MaximumParallelFull,
};

/// <summary>
/// 保存无窗口运行请求，可由命令行解析或调用方直接构造
/// 普通基准使用策略覆盖项，清单驱动的任务使用 FormalInput
/// </summary>
struct BenchmarkOptions
{
    BenchmarkAlgorithmSelection Algorithm{BenchmarkAlgorithmSelection::All};
    BenchmarkProfile Profile{BenchmarkProfile::Smoke};
    BenchmarkPassPolicySelection PassPolicy{BenchmarkPassPolicySelection::Default};
    std::size_t MergeScoreMinParallelEntryCount{Algorithms::TerrainLodPassPolicy{}.MergeScoreMinParallelEntryCount};
    std::size_t SplitScoreMinParallelEntryCount{Algorithms::TerrainLodPassPolicy{}.SplitScoreMinParallelEntryCount};
    std::size_t MeshEmitMinParallelTriangleCount{Algorithms::TerrainLodPassPolicy{}.MeshEmitMinParallelTriangleCount};
    std::size_t SplitTopologyMinParallelCandidateCount{32U};
    std::size_t MergeTopologyMinParallelCandidateCount{160U};
    std::size_t ParallelTopologyTargetBuild{0U};
    Algorithms::TerrainLodParallelTopologyPhase ParallelTopologyPhase{
        Algorithms::TerrainLodParallelTopologyPhase::Both};
    std::size_t PassExperimentWarmupCount{5U};
    std::size_t PassExperimentRepeatCount{30U};
    std::size_t PassExperimentWorkerCount{8U};
    std::size_t PassExperimentTargetCount{0U};
    std::filesystem::path CsvPath;
    Experiment::Formal::FormalInputRequest FormalInput;
    Experiment::Formal::CpuPairSelection CpuPair;
    std::size_t TargetsPerPass{4U};
};

/// <summary>
/// 运行 Classic 和 Data-Oriented 共享的无窗口地形 LOD benchmark
/// </summary>
[[nodiscard]] int RunTerrainLodBenchmark(const BenchmarkOptions& options);

/// <summary>
/// 解析无窗口命令，输出帮助或错误后返回，合法运行请求交给任务入口
/// </summary>
[[nodiscard]] int RunTerrainLodBenchmarkFromCommandLine(int argc, char** argv);

[[nodiscard]] std::string BenchmarkUsage();
} // 命名空间 ParallelRoam::Benchmark
