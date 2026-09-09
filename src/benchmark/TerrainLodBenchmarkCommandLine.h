#pragma once

#include "benchmark/TerrainLodBenchmark.h"

#include <string>

namespace ParallelRoam::Benchmark
{
/// <summary>
/// 保存解析得到的选项、帮助请求或首个错误；资产加载和算法执行由运行入口负责
/// </summary>
struct TerrainLodBenchmarkCommandLineParseResult
{
    BenchmarkOptions Options;
    bool ShowHelp{false};
    std::string Error;

    [[nodiscard]] bool Succeeded() const
    {
        return Error.empty();
    }
};

[[nodiscard]] TerrainLodBenchmarkCommandLineParseResult ParseTerrainLodBenchmarkCommandLine(
    int argc,
    char** argv);
} // 命名空间 ParallelRoam::Benchmark
