#pragma once

#include "experiment/formal/FormalExperimentTypes.h"

namespace ParallelRoam::Experiment::Formal
{
/// <summary>
/// 只接受 64 点轨迹 A 与 sampleIndex 0..63，地形尺寸须为有限正数，范围或相机参数无效时抛异常
/// </summary>
[[nodiscard]] CameraSample GenerateCameraSample(const FormalScenario& scenario, std::uint32_t sampleIndex);
/// <summary>
/// 按传入场景顺序生成各自的 64 个点，约束同单点生成，任一失败均抛异常且不返回部分集合
/// </summary>
[[nodiscard]] std::vector<CameraSample> GenerateCameraSamples(const std::vector<FormalScenario>& scenarios);
/// <summary>
/// 从冻结行构建右手 NO 视图，不重算轨迹或核验哈希，姿态退化或投影参数无效时抛异常
/// </summary>
[[nodiscard]] Algorithms::TerrainLodViewInput BuildCameraView(const CameraSample& sample);
/// <summary>
/// 同时核对冻结行的派生哈希与场景轨迹预期值，任一不一致或参数无效均抛异常
/// </summary>
void ValidateCameraSample(const CameraSample& sample, const FormalScenario& scenario);
} // 命名空间 ParallelRoam::Experiment::Formal
