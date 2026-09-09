#pragma once

#include "experiment/formal/FormalCpuPairRecords.h"

#include <functional>

namespace ParallelRoam::Benchmark::Formal
{
/// <summary>
/// 返回场景内实际目标的完整性，失败时保留已测目标和原因供运行器归档
/// </summary>
struct FormalCpuPassBenchmarkResult
{
    bool Complete{false};
    std::vector<Experiment::Formal::CpuTargetPairSummary> Targets;
    std::string Failure;
};

[[nodiscard]] Experiment::Formal::CpuPairConfiguration MakeCpuPairConfiguration(
    const Experiment::Formal::FormalScenario& scenario, const Experiment::Formal::CpuPairSelection& selection);

/// <summary>
/// 从根沿冻结相机推进到最后一个目标，只在命中的生产边界同步测量
/// 目标必须已经完成清单校验；回调写值记录，不接收 DOD 状态所有权
/// </summary>
[[nodiscard]] FormalCpuPassBenchmarkResult MeasureCpuScenarioTargets(
    const Experiment::Formal::FormalScenario& scenario,
    const std::vector<Experiment::Formal::CameraSample>& cameras,
    const std::vector<Experiment::Formal::TargetStateRef>& targets,
    const Experiment::Formal::CpuPairSelection& selection, const std::string& runId,
    const std::function<void(const Experiment::Formal::CpuPairRecord&)>& writeRecord);
}
