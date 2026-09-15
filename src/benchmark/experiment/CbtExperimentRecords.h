#pragma once

#include "algorithms/TerrainLodCbtTypes.h"

#include <filesystem>
#include <fstream>
#include <optional>

namespace ParallelRoam::Benchmark::Experiment
{
/// <summary>
/// 记录实际捕获身份和同步成本，缺失捕获时不生成假零值
/// </summary>
struct CbtCaptureRecord
{
    std::uint64_t ResourceGeneration{};
    std::uint64_t TopologyGeneration{};
    std::size_t Faces{};
    std::uint64_t ReadbackBytes{};
    double Milliseconds{};
};

/// <summary>
/// CBT 专属延迟样本按资源与采样代记账，不借用 CPU 五阶段统计
/// </summary>
class CbtExperimentRecords
{
public:
    explicit CbtExperimentRecords(const std::filesystem::path& path);
    void Append(std::size_t frame, const Algorithms::TerrainLodCbtStats& stats,
        std::size_t delayedFaces, const std::optional<CbtCaptureRecord>& capture);
private:
    std::ofstream _stream;
};
}
