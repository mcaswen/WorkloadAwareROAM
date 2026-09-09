#pragma once

#include "experiment/formal/FormalExperimentRecords.h"
#include "experiment/formal/FormalExperimentTargetSelector.h"

#include <functional>

namespace ParallelRoam::Benchmark::Formal
{
/// <summary>
/// 保存单个场景的发现状态、目标与覆盖结果
/// 失败时保留已输出记录，但不提供可供配对消费的目标
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
/// 用独立流水线从根状态依次执行冻结相机输入
/// 每帧完成诊断后发布记录，使输出保留真实执行顺序和检查结果
/// </summary>
[[nodiscard]] FormalWorkloadDiscoveryResult DiscoverCpuScenarioWorkloads(
    const Experiment::Formal::FormalScenario& scenario,
    const std::vector<Experiment::Formal::CameraSample>& cameras,
    const Experiment::Formal::TargetSelectionConfig& config,
    const std::function<void(const Experiment::Formal::CpuDiscoveryRecord&)>& writeRecord);
}
