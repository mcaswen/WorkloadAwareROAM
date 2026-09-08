#pragma once

#include "experiment/formal/FormalExperimentTypes.h"

namespace ParallelRoam::Benchmark::Formal
{
/// <summary>
/// 要求输出目录不存在且父目录已存在；成功返回 0，失败返回 1 并保留本次已写产物供诊断
/// </summary>
[[nodiscard]] int PrepareCpuPilotInputs(const Experiment::Formal::FormalInputRequest& request);
} // 命名空间 ParallelRoam::Benchmark::Formal
