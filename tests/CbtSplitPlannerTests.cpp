#include "algorithms/cbt_2024/CbtSplitPlanner.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <iostream>
#include <vector>

namespace
{
using namespace ParallelRoam::Algorithms::Cbt2024;

std::uint64_t HeapIdAtDepth(std::uint32_t depth)
{
    return std::uint64_t{1U} << (depth - 1U);
}

CbtSplitPlanningNode Node(
    std::uint32_t depth,
    std::uint32_t previous = InvalidCbtBisectorIndex,
    std::uint32_t next = InvalidCbtBisectorIndex,
    std::uint32_t twin = InvalidCbtBisectorIndex)
{
    return {HeapIdAtDepth(depth), {previous, next, twin}, 0};
}

bool Expect(bool condition, const char* message)
{
    if (!condition)
    {
        std::cerr << message << '\n';
    }
    return condition;
}
} // namespace

int main()
{
    bool passed = true;

    // 边界节点只预留并使用一个槽位。
    const CbtSplitPlanningResult boundary = PlanCbtSplits({Node(4U)}, {0U}, 4U, 1U);
    passed &= Expect(boundary.Valid, "boundary plan should be valid");
    passed &= Expect(boundary.SubdivisionPatterns == std::vector<std::uint32_t>{CbtCenterSplitPattern},
                     "boundary plan should request center split");
    passed &= Expect(boundary.RequiredSlotCount == 1U && boundary.RemainingMemory == 0U,
                     "boundary reservation should consume one slot");

    // 直接 facing twin 采用两槽特化，并为双方各登记一次 allocation。
    std::vector<CbtSplitPlanningNode> directTwin{Node(4U, InvalidCbtBisectorIndex, InvalidCbtBisectorIndex, 1U),
                                                 Node(4U, InvalidCbtBisectorIndex, InvalidCbtBisectorIndex, 0U)};
    const CbtSplitPlanningResult twin = PlanCbtSplits(directTwin, {0U}, 4U, 2U);
    passed &= Expect(twin.Valid && twin.RequiredSlotCount == 2U,
                     "direct twin should consume exactly two slots");
    passed &= Expect(twin.SubdivisionPatterns ==
                         std::vector<std::uint32_t>{CbtCenterSplitPattern, CbtCenterSplitPattern},
                     "direct twin patterns should both be center split");

    // 两次降深再遇到同深节点，覆盖 center、double、double、center 的长兼容链。
    std::vector<CbtSplitPlanningNode> longChain{
        Node(8U, InvalidCbtBisectorIndex, InvalidCbtBisectorIndex, 1U),
        Node(7U, 0U, InvalidCbtBisectorIndex, 2U),
        Node(6U, 1U, InvalidCbtBisectorIndex, 3U),
        Node(6U),
    };
    const CbtSplitPlanningResult longPlan = PlanCbtSplits(longChain, {0U}, 4U, 7U);
    passed &= Expect(longPlan.Valid && longPlan.RequiredSlotCount == 6U && longPlan.RemainingMemory == 1U,
                     "long chain should return one conservatively reserved slot");
    passed &= Expect(longPlan.SubdivisionPatterns == std::vector<std::uint32_t>{
                         CbtCenterSplitPattern,
                         CbtRightDoubleSplitPattern,
                         CbtRightDoubleSplitPattern,
                         CbtCenterSplitPattern},
                     "long chain patterns do not match upstream traversal");

    // 保守需求恰好满足时成功；少一个槽位时不能留下任何 pattern 或 allocation。
    const CbtSplitPlanningResult insufficient = PlanCbtSplits(longChain, {0U}, 4U, 6U);
    passed &= Expect(insufficient.Valid && insufficient.RejectedCandidateCount == 1U,
                     "one-slot-short reservation should reject the candidate");
    passed &= Expect(insufficient.RequiredSlotCount == 0U && insufficient.AllocationNodes.empty() &&
                         insufficient.RemainingMemory == 6U,
                     "rejected reservation should leave planning state untouched");

    // 两个候选共享同一同深终点；第二条链只能认领自己的中心节点。
    std::vector<CbtSplitPlanningNode> shared{
        Node(6U, InvalidCbtBisectorIndex, InvalidCbtBisectorIndex, 2U),
        Node(6U, InvalidCbtBisectorIndex, InvalidCbtBisectorIndex, 2U),
        Node(6U),
    };
    const CbtSplitPlanningResult sharedPlan = PlanCbtSplits(shared, {0U, 1U}, 4U, 6U);
    passed &= Expect(sharedPlan.Valid && sharedPlan.RequiredSlotCount == 3U,
                     "shared compatibility endpoint should be counted once");
    passed &= Expect(sharedPlan.AllocationNodes.size() == 3U,
                     "shared endpoint should occur once in allocation list");

    // Allocate 只读取旧树：输出必须避开占用位，且树和位域保持逐字不变。
    CbtOccupancyTree occupancy{CbtOccupancyCapacity::Capacity128K};
    passed &= Expect(occupancy.SetBit(0U, true) && occupancy.SetBit(2U, true) &&
                         occupancy.SetBit(5U, true),
                     "test occupancy bits should be in range");
    occupancy.Reduce();
    const std::vector<std::uint32_t> treeBefore = occupancy.PackedTree();
    const std::vector<std::uint64_t> bitsBefore = occupancy.Bitfield();
    const CbtSplitSlotAllocation slots = AllocateCbtSplitSlots(longPlan, occupancy);
    passed &= Expect(slots.Valid && slots.AllocatedSlotCount == longPlan.RequiredSlotCount,
                     "rank-select allocation should cover every pattern bit");
    std::vector<std::uint32_t> allocated;
    for (const auto& nodeIndices : slots.NodeIndices)
    {
        for (const std::uint32_t index : nodeIndices)
        {
            if (index != InvalidCbtBisectorIndex)
            {
                allocated.push_back(index);
            }
        }
    }
    std::sort(allocated.begin(), allocated.end());
    passed &= Expect(std::adjacent_find(allocated.begin(), allocated.end()) == allocated.end(),
                     "allocated physical slots must be unique");
    passed &= Expect(std::none_of(allocated.begin(), allocated.end(), [&](std::uint32_t index) {
                         return occupancy.GetBit(index);
                     }),
                     "allocated physical slots must come from the old OCBT complement");
    passed &= Expect(occupancy.PackedTree() == treeBefore && occupancy.Bitfield() == bitsBefore,
                     "allocation must not mutate OCBT before Bisect");

    return passed ? 0 : 1;
}
