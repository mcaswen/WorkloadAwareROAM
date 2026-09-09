#pragma once

#include "experiment/formal/FormalExperimentTypes.h"

namespace ParallelRoam::Benchmark::Formal
{
/// <summary>
/// 要求输出目录尚不存在、父目录已存在，由本次调用独占创建输出目录
/// 成功返回 0；失败返回 1，并保留本次已写产物供诊断
/// </summary>
[[nodiscard]] int PrepareCpuPilotInputs(const Experiment::Formal::FormalInputRequest& request);

/// <summary>
/// 独占新目录写入工作量发现证据
/// 全部场景完成且目标引用重新校验后才发布标准目标文件
/// </summary>
[[nodiscard]] int DiscoverCpuPilotWorkloads(
    const Experiment::Formal::FormalInputRequest& request, std::uint32_t targetsPerPass);
} // 命名空间 ParallelRoam::Benchmark::Formal
