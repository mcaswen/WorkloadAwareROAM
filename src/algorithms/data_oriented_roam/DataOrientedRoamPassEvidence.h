#pragma once

#include "algorithms/TerrainLodPassTrace.h"

namespace ParallelRoam::Algorithms::DataOrientedRoam
{
struct DataOrientedRoamState;

/// <summary>
/// 保存停表后的阶段检查与规范化结果，拓扑明细同时供旧诊断入口使用
/// 哈希相等只证明规范化结果一致，不能替代 ValidationPerformed 和 Correct
/// </summary>
struct DataOrientedRoamPassEvidence
{
    std::uint64_t ResultHash{0U};
    bool ValidationPerformed{false};
    bool Correct{false};
    TerrainLodTopologyReplayEvidence Topology;
};

/// <summary>
/// 检查当前阶段输出，不执行策略或修复状态；验证器只写副本的诊断统计
/// 中间拓扑阶段不要求尚未更新的几何已经与活动叶一致
/// </summary>
[[nodiscard]] DataOrientedRoamPassEvidence CaptureDataOrientedRoamPassEvidence(
    DataOrientedRoamState& state, TerrainLodPassId passId);
}
