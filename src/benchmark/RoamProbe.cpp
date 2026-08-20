#include "benchmark/RoamProbe.h"

#include "benchmark/TerrainLodBenchmark.h"

namespace ParallelRoam::Benchmark
{
int RunRoamProbe()
{
    // RoamProbe 是旧命令行兼容入口
    // 实际逻辑复用当前 benchmark smoke profile
    // 避免维护两套回归场景
    BenchmarkOptions options{};
    options.Algorithm = BenchmarkAlgorithmSelection::Classic;
    options.Profile = BenchmarkProfile::Smoke;
    return RunTerrainLodBenchmark(options);
}
} // 命名空间 ParallelRoam::Benchmark
