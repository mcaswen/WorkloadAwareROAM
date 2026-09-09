#pragma once

#include "algorithms/data_oriented_roam/DataOrientedRoamTypes.h"
#include "experiment/formal/FormalExperimentRecords.h"

namespace ParallelRoam::Algorithms::DataOrientedRoam { struct DataOrientedRoamPassWorkload; }

namespace ParallelRoam::Benchmark::Formal
{
/// <summary>
/// 将已校验清单映射为固定串行来源设置，发现与配对使用相同输入协议
/// </summary>
[[nodiscard]] Algorithms::DataOrientedRoam::DataOrientedRoamSettings MakeCpuSourceSettings(
    const Experiment::Formal::FormalScenario& scenario);

/// <summary>
/// 只映射探测器返回的领域值，编排层不读取 DOD 节点、队列或槽位
/// </summary>
[[nodiscard]] Experiment::Formal::CpuDiscoveryRecord MakeCpuDiscoveryInputRecord(
    const Experiment::Formal::CameraSample& camera, Algorithms::TerrainLodPassId pass,
    const Algorithms::DataOrientedRoam::DataOrientedRoamPassWorkload& work,
    std::uint64_t inputHash, double hashMilliseconds);
[[nodiscard]] Experiment::Formal::CpuPairRecord MakeCpuPairInputRecord(
    const Experiment::Formal::CameraSample& camera, Algorithms::TerrainLodPassId pass,
    const Algorithms::DataOrientedRoam::DataOrientedRoamPassWorkload& work, std::uint64_t inputHash);

/// <summary>
/// 来源帧必须完成既有诊断，不能仅凭零违规数量推断检查已经执行
/// </summary>
[[nodiscard]] bool IsCpuSourceFrameValid(const Algorithms::DataOrientedRoam::DataOrientedRoamStats& stats,
    const Algorithms::DataOrientedRoam::DataOrientedRoamSettings& settings, std::uint32_t sampleIndex);
}
