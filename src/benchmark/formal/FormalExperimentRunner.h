#pragma once

#include "experiment/formal/FormalExperimentTypes.h"

namespace ParallelRoam::Benchmark::Formal
{
/// <summary>
/// 要求输出目录不存在且父目录已存在；成功返回 0，失败返回 1 并保留本次已写产物供诊断
/// </summary>
[[nodiscard]] int PrepareCpuPilotInputs(const Experiment::Formal::FormalInputRequest& request);

/// <summary>
/// 独占新目录写入发现证据并在全部场景完成后发布目标
/// </summary>
[[nodiscard]] int DiscoverCpuPilotWorkloads(
    const Experiment::Formal::FormalInputRequest& request, std::uint32_t targetsPerPass);
} // 命名空间 ParallelRoam::Benchmark::Formal
