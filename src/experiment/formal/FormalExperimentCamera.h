#pragma once

#include "experiment/formal/FormalExperimentTypes.h"

namespace ParallelRoam::Experiment::Formal
{
/// <summary>
/// 从 64 点轨迹 A 生成指定采样，sampleIndex 必须在 0..63 内
/// 地形尺寸须为有限正数；采样范围或相机参数无效时抛出异常
/// </summary>
[[nodiscard]] CameraSample GenerateCameraSample(const FormalScenario& scenario, std::uint32_t sampleIndex);
/// <summary>
/// 按场景顺序生成各自的 64 个点，沿用单点生成约束；任一点失败即抛出异常，不返回部分集合
/// </summary>
[[nodiscard]] std::vector<CameraSample> GenerateCameraSamples(const std::vector<FormalScenario>& scenarios);
/// <summary>
/// 从冻结行构建右手 NO 视图，姿态退化或投影参数无效时抛出异常
/// 这里只检查能否构建视图，轨迹和哈希的一致性由 ValidateCameraSample 核对
/// </summary>
[[nodiscard]] Algorithms::TerrainLodViewInput BuildCameraView(const CameraSample& sample);
/// <summary>
/// 同时核对冻结行的派生哈希与场景轨迹预期值，任一不一致或参数无效均抛异常
/// </summary>
void ValidateCameraSample(const CameraSample& sample, const FormalScenario& scenario);
} // 命名空间 ParallelRoam::Experiment::Formal
