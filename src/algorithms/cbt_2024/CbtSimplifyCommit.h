#pragma once

#include "algorithms/cbt_2024/CbtBisectorTopology.h"

#include <cstdint>
#include <string>
#include <vector>

namespace ParallelRoam::Algorithms::Cbt2024
{
/// <summary>
/// PrepareSimplify、Simplify 和 PropagateSimplify 完成后的 CPU 拓扑快照
/// </summary>
struct CbtSimplifyCommitResult
{
    std::vector<std::uint64_t> HeapIds;
    std::vector<CbtBisectorNeighbors> Neighbors;
    std::vector<CbtBisectorData> BisectorData;
    std::vector<std::uint32_t> SimplificationNodes;
    std::vector<std::uint32_t> ReleasedDynamicSlots;
    std::vector<std::uint32_t> PropagationNodes;
    std::uint32_t PairMergeCount{0U};
    std::uint32_t QuadMergeCount{0U};
    bool Valid{true};
};

/// <summary>
/// 按来源顺序选定唯一简化组，提交后统一修补外部邻接
/// </summary>
[[nodiscard]] CbtSimplifyCommitResult CommitCbtSimplifications(
    const std::vector<std::uint64_t>& heapIds,
    const std::vector<CbtBisectorNeighbors>& neighbors,
    const std::vector<CbtBisectorData>& bisectorData,
    const std::vector<std::uint32_t>& simplifyCandidates,
    std::uint32_t dynamicElementCount);

/// <summary>
/// 核对释放槽已失活，且剩余活动节点没有悬空或单向邻接
/// </summary>
[[nodiscard]] bool ValidateCbtSimplifiedTopology(
    const CbtSimplifyCommitResult& topology,
    std::uint32_t dynamicElementCount,
    std::string* errorMessage);
} // namespace ParallelRoam::Algorithms::Cbt2024
