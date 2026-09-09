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

/// <summary>
/// 独占新目录测量完整冻结目标，内部校验通过后才发布配对产物
/// 外部源码、资产及文件摘要仍由脚本核对，裸入口不声明脚本尝试完成
/// </summary>
[[nodiscard]] int RunCpuPilotPairing(const Experiment::Formal::FormalInputRequest& request,
    const Experiment::Formal::CpuPairSelection& selection);
} // 命名空间 ParallelRoam::Benchmark::Formal
