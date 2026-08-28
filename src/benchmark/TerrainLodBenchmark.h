#pragma once

#include "algorithms/TerrainLodPassTrace.h"

#include <cstddef>
#include <filesystem>
#include <string>

namespace ParallelRoam::Benchmark
{
/// <summary>
/// benchmark CLI 可选择的 terrain LOD 算法集合
/// </summary>
enum class BenchmarkAlgorithmSelection
{
    Classic,
    DataOriented,
    All,
};

/// <summary>
/// benchmark 内置场景，覆盖 smoke、预算重入、增量 emit 回归和较长相机路径统计
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
    Standard,
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
/// benchmark 命令行解析后的运行选项
/// </summary>
struct BenchmarkOptions
{
    BenchmarkAlgorithmSelection Algorithm{BenchmarkAlgorithmSelection::All};
    BenchmarkProfile Profile{BenchmarkProfile::Smoke};
    BenchmarkPassPolicySelection PassPolicy{BenchmarkPassPolicySelection::Default};
    std::size_t SplitTopologyMinParallelCandidateCount{32U};
    std::size_t MergeTopologyMinParallelCandidateCount{160U};
    std::size_t ParallelTopologyTargetBuild{0U};
    Algorithms::TerrainLodParallelTopologyPhase ParallelTopologyPhase{
        Algorithms::TerrainLodParallelTopologyPhase::Both};
    std::filesystem::path CsvPath;
};

/// <summary>
/// 运行 Classic 和 Data-Oriented 共享的无窗口地形 LOD benchmark
/// </summary>
[[nodiscard]] int RunTerrainLodBenchmark(const BenchmarkOptions& options);

/// <summary>
/// 从命令行解析少量 benchmark 参数并运行
/// </summary>
[[nodiscard]] int RunTerrainLodBenchmarkFromCommandLine(int argc, char** argv);

[[nodiscard]] std::string BenchmarkUsage();
} // 命名空间 ParallelRoam::Benchmark
