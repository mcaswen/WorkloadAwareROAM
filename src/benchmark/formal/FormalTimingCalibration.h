#pragma once

#include "experiment/formal/FormalCpuPairRecords.h"

namespace ParallelRoam::Benchmark::Formal
{
/// <summary>
/// 汇总一次标定批次的原值与可用性，环境判定只使用空计时的预定上限
/// </summary>
struct FormalTimingCalibrationResult
{
    std::vector<Experiment::Formal::CpuTimingCalibrationRecord> Records;
    bool Complete{false};
    bool EnvironmentValid{false};
    double EmptyP99Milliseconds{0.0};
    double NoWorkP99Milliseconds{0.0};
    std::string Failure;
};

/// <summary>
/// 每次从独立流水线的根阶段取样，空计时与真实无工作各保留一千次
/// 此调用必须位于正式目标循环之外，并同步结束对根状态的借用
/// </summary>
[[nodiscard]] FormalTimingCalibrationResult CalibrateCpuPassTiming(
    const Experiment::Formal::FormalScenario& scenario, const Experiment::Formal::CameraSample& rootCamera,
    const std::string& runId, const std::string& position);
}
