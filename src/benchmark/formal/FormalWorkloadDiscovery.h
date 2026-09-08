#pragma once

#include "experiment/formal/FormalExperimentRecords.h"
#include "experiment/formal/FormalExperimentTargetSelector.h"

#include <functional>

namespace ParallelRoam::Benchmark::Formal
{
/// <summary>
/// 场景失败时保留已输出记录但不提供可供配对消费的目标
/// </summary>
struct FormalWorkloadDiscoveryResult
{
    bool Complete{false};
    std::size_t RecordCount{0U};
    std::vector<Experiment::Formal::TargetStateRef> Targets;
    std::vector<Experiment::Formal::CpuTargetCoverageRecord> Coverage;
    std::string Failure;
};

/// <summary>
/// 独立流水线从根顺序运行冻结相机且逐帧发布已诊断记录
/// </summary>
[[nodiscard]] FormalWorkloadDiscoveryResult DiscoverCpuScenarioWorkloads(
    const Experiment::Formal::FormalScenario& scenario,
    const std::vector<Experiment::Formal::CameraSample>& cameras,
    const Experiment::Formal::TargetSelectionConfig& config,
    const std::function<void(const Experiment::Formal::CpuDiscoveryRecord&)>& writeRecord);
}
