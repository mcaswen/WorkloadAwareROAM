#include "algorithms/cbt_2024/CbtSplitPlanner.h"

#include <algorithm>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <limits>

namespace ParallelRoam::Algorithms::Cbt2024
{
namespace
{
std::uint32_t HeapDepth(std::uint64_t heapId)
{
    // 上游 HeapIDDepth 返回位长，和本项目公开的零基 CbtHeapIdDepth 相差一。
    return static_cast<std::uint32_t>(std::bit_width(heapId));
}

bool IsValidNode(std::uint32_t node, std::size_t nodeCount)
{
    // 边界哨兵不占数组槽；其他邻接必须能被局部参考拓扑解引用。
    return node == InvalidCbtBisectorIndex || node < nodeCount;
}

std::uint32_t OrPattern(
    std::vector<std::uint32_t>& patterns,
    std::uint32_t node,
    std::uint32_t pattern)
{
    // 返回 OR 前的 pattern，等价于 HLSL InterlockedOr 的 original-value 输出。
    const std::uint32_t previous = patterns[node];
    patterns[node] |= pattern;
    return previous;
}
} // namespace

CbtSplitPlanningResult PlanCbtSplits(
    const std::vector<CbtSplitPlanningNode>& nodes,
    const std::vector<std::uint32_t>& splitCandidates,
    std::uint32_t baseDepth,
    std::uint32_t availableMemory)
{
    CbtSplitPlanningResult result{};
    result.SubdivisionPatterns.resize(nodes.size(), CbtNoSplitPattern);
    result.InitialAvailableMemory = availableMemory;
    result.RemainingMemory = availableMemory;

    // 邻接输入错误不是容量拒绝；参考实现立即标记无效，避免把越界误判为候选竞争。
    for (const CbtSplitPlanningNode& node : nodes)
    {
        if (node.HeapId == 0U ||
            !IsValidNode(node.Neighbors.Previous, nodes.size()) ||
            !IsValidNode(node.Neighbors.Next, nodes.size()) ||
            !IsValidNode(node.Neighbors.Twin, nodes.size()))
        {
            result.Valid = false;
            return result;
        }
    }

    for (const std::uint32_t candidate : splitCandidates)
    {
        // 候选顺序模拟一种合法的 GPU 原子提交顺序；最终共享 pattern 仍通过 OR 合并。
        if (candidate >= nodes.size())
        {
            result.Valid = false;
            return result;
        }

        std::uint32_t current = candidate;
        const CbtBisectorNeighbors& candidateNeighbors = nodes[current].Neighbors;
        // 如果当前节点已位于另一个非 unchanged 邻居的兼容路径上，由路径拥有者处理它。
        bool ownedByNeighborPath = false;
        for (const std::uint32_t neighbor : {candidateNeighbors.Previous, candidateNeighbors.Next})
        {
            if (neighbor != InvalidCbtBisectorIndex &&
                nodes[neighbor].Neighbors.Twin == current &&
                nodes[neighbor].BisectorState != 0)
            {
                ownedByNeighborPath = true;
                break;
            }
        }
        if (ownedByNeighborPath)
        {
            ++result.DuplicateCandidateCount;
            continue;
        }

        std::uint32_t currentDepth = HeapDepth(nodes[current].HeapId);
        std::uint32_t twin = candidateNeighbors.Twin;
        std::int64_t maximumRequiredMemory =
            2 * (static_cast<std::int64_t>(currentDepth) - static_cast<std::int64_t>(baseDepth)) - 1;
        // 边界和直接 facing twin 使用上游的紧预留特化。
        if (twin == InvalidCbtBisectorIndex)
        {
            maximumRequiredMemory = 1;
        }
        else if (nodes[twin].Neighbors.Twin == current)
        {
            maximumRequiredMemory = 2;
        }
        if (maximumRequiredMemory <= 0 ||
            maximumRequiredMemory > static_cast<std::int64_t>(std::numeric_limits<std::uint32_t>::max()))
        {
            result.Valid = false;
            return result;
        }

        const std::uint32_t reservation = static_cast<std::uint32_t>(maximumRequiredMemory);
        // 原子扣减失败必须完整回滚；顺序参考用等价的先检查后扣减表示。
        if (result.RemainingMemory < reservation)
        {
            ++result.RejectedCandidateCount;
            continue;
        }
        result.RemainingMemory -= reservation;

        // 第一个成功把中心位从零置一的候选拥有该节点及其 allocation list 记录。
        std::uint32_t usedMemory = 1U;
        if (OrPattern(result.SubdivisionPatterns, current, CbtCenterSplitPattern) != CbtNoSplitPattern)
        {
            result.RemainingMemory += reservation;
            ++result.DuplicateCandidateCount;
            continue;
        }
        result.AllocationNodes.push_back(current);

        while (twin != InvalidCbtBisectorIndex)
        {
            // 同深 twin 只需中心二分；更粗邻居需要左右 double pattern 并继续追踪。
            const CbtSplitPlanningNode& neighbor = nodes[twin];
            const std::uint32_t neighborDepth = HeapDepth(neighbor.HeapId);
            if (neighborDepth == currentDepth)
            {
                if (OrPattern(result.SubdivisionPatterns, twin, CbtCenterSplitPattern) == CbtNoSplitPattern)
                {
                    result.AllocationNodes.push_back(twin);
                    ++usedMemory;
                }
                break;
            }

            const std::uint32_t doublePattern = neighbor.Neighbors.Previous == current
                ? CbtRightDoubleSplitPattern
                : CbtLeftDoubleSplitPattern;
            const std::uint32_t previousPattern =
                OrPattern(result.SubdivisionPatterns, twin, doublePattern);
            if (previousPattern != CbtNoSplitPattern)
            {
                ++usedMemory;
                break;
            }

            result.AllocationNodes.push_back(twin);
            usedMemory += 2U;
            current = twin;
            currentDepth = neighborDepth;
            twin = neighbor.Neighbors.Twin;
        }

        // 上游只返还保守值和实际值的正差，避免共享链竞争时把预算加成负数。
        result.RemainingMemory += reservation > usedMemory ? reservation - usedMemory : 0U;
    }

    for (const std::uint32_t pattern : result.SubdivisionPatterns)
    {
        // Allocate 使用的真实槽位数来自最终合并 pattern，而不是每候选的保守预留。
        result.RequiredSlotCount += static_cast<std::uint32_t>(std::popcount(pattern));
    }
    return result;
}

CbtSplitSlotAllocation AllocateCbtSplitSlots(
    const CbtSplitPlanningResult& plan,
    const CbtOccupancyTree& occupancyTree)
{
    CbtSplitSlotAllocation allocation{};
    allocation.NodeIndices.resize(plan.SubdivisionPatterns.size());
    for (std::array<std::uint32_t, 3>& indices : allocation.NodeIndices)
    {
        indices.fill(InvalidCbtBisectorIndex);
    }
    if (!plan.Valid)
    {
        allocation.Valid = false;
        return allocation;
    }

    // AllocationNodes 只包含首次把 pattern 从零置为非零的节点，顺序决定 rank 区间。
    std::uint32_t nextFreeRank = 0U;
    for (const std::uint32_t node : plan.AllocationNodes)
    {
        if (node >= plan.SubdivisionPatterns.size())
        {
            allocation.Valid = false;
            return allocation;
        }
        const std::uint32_t slotCount =
            static_cast<std::uint32_t>(std::popcount(plan.SubdivisionPatterns[node]));
        // 原子 add 为每节点切出不重叠的 rank 区间；顺序参考直接递增同一游标。
        for (std::uint32_t slot = 0U; slot < slotCount; ++slot)
        {
            const std::uint32_t physicalIndex = occupancyTree.DecodeBitComplement(nextFreeRank++);
            if (physicalIndex == InvalidCbtBitIndex)
            {
                allocation.Valid = false;
                return allocation;
            }
            allocation.NodeIndices[node][slot] = physicalIndex;
        }
    }
    allocation.AllocatedSlotCount = nextFreeRank;
    allocation.Valid = nextFreeRank == plan.RequiredSlotCount;
    return allocation;
}
} // namespace ParallelRoam::Algorithms::Cbt2024
