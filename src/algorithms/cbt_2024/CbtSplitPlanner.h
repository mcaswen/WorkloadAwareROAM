#pragma once

#include "algorithms/cbt_2024/CbtBisectorTopology.h"
#include "algorithms/cbt_2024/CbtOccupancyTree.h"

#include <array>
#include <cstdint>
#include <vector>

namespace ParallelRoam::Algorithms::Cbt2024
{
/// <summary>
/// SplitElement 所需的最小 CPU 拓扑视图；索引均为该数组内的局部物理槽位。
/// </summary>
struct CbtSplitPlanningNode
{
    std::uint64_t HeapId{0U}; // 逻辑 LEB 路径，仅用于取得官方位长深度。
    CbtBisectorNeighbors Neighbors{}; // 局部数组中的 prev、next、twin。
    std::int32_t BisectorState{0}; // 非零节点可拥有另一候选的兼容路径。
};

/// <summary>
/// 保守预留、pattern 认领和 allocation list 的确定性 CPU 参考结果。
/// </summary>
struct CbtSplitPlanningResult
{
    std::vector<std::uint32_t> SubdivisionPatterns; // 每节点分布式保存的兼容链结果。
    std::vector<std::uint32_t> AllocationNodes; // pattern 首次从零变为非零的节点。
    std::uint32_t InitialAvailableMemory{0U}; // 规划前旧 OCBT 的 free count。
    std::uint32_t RemainingMemory{0U}; // 原子预留和返还后的预算。
    std::uint32_t RequiredSlotCount{0U}; // 所有最终 pattern 的 popcount 总和。
    std::uint32_t RejectedCandidateCount{0U}; // 保守预留不足而完整拒绝的候选。
    std::uint32_t DuplicateCandidateCount{0U}; // 被既有路径或 pattern 去重的候选。
    bool Valid{true}; // false 表示输入 heapID 或邻接本身非法。
};

/// <summary>
/// AllocateElement 对旧 OCBT 补集做 rank-select 后得到的每节点槽位。
/// </summary>
struct CbtSplitSlotAllocation
{
    std::vector<std::array<std::uint32_t, 3>> NodeIndices; // 与 BisectorData.indices 同布局。
    std::uint32_t AllocatedSlotCount{0U}; // 已消费的全局 free-rank 数。
    bool Valid{true}; // rank 越过旧树补集或计划无效时为 false。
};

[[nodiscard]] CbtSplitPlanningResult PlanCbtSplits(
    const std::vector<CbtSplitPlanningNode>& nodes,
    const std::vector<std::uint32_t>& splitCandidates,
    std::uint32_t baseDepth,
    std::uint32_t availableMemory);

[[nodiscard]] CbtSplitSlotAllocation AllocateCbtSplitSlots(
    const CbtSplitPlanningResult& plan,
    const CbtOccupancyTree& occupancyTree);
} // namespace ParallelRoam::Algorithms::Cbt2024
