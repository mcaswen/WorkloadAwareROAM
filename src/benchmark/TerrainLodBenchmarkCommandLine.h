#pragma once

#include "benchmark/TerrainLodBenchmark.h"

#include <string>

namespace ParallelRoam::Benchmark
{
/// <summary>
/// 保存无窗口选项、帮助请求和首个解析错误；不触发资产或算法执行
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
