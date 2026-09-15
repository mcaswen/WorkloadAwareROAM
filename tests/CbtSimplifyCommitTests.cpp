#include "algorithms/cbt_2024/CbtBisectCommit.h"
#include "algorithms/cbt_2024/CbtOccupancyTree.h"
#include "algorithms/cbt_2024/CbtSimplifyCommit.h"

#include <cstdint>
#include <iostream>
#include <string>
#include <vector>

namespace
{
using namespace ParallelRoam::Algorithms::Cbt2024;

bool Expect(bool condition, const char* message)
{
    if (!condition)
    {
        std::cerr << message << '\n';
    }
    return condition;
}

bool SameNeighbors(const CbtBisectorNeighbors& first, const CbtBisectorNeighbors& second)
{
    return first.Previous == second.Previous &&
           first.Next == second.Next &&
           first.Twin == second.Twin;
}

struct CompactBaseTopology
{
    static constexpr std::uint32_t DynamicCount = 8U;
    std::vector<std::uint64_t> HeapIds =
        std::vector<std::uint64_t>(DynamicCount + CbtBaseBisectorCount, 0U);
    std::vector<CbtBisectorNeighbors> Neighbors =
        std::vector<CbtBisectorNeighbors>(HeapIds.size());
    std::vector<CbtBisectorData> Data =
        std::vector<CbtBisectorData>(HeapIds.size());

    CompactBaseTopology()
    {
        const CbtBaseTopology base =
            BuildSquareCbtBaseTopology(CbtOccupancyCapacity::Capacity128K);
        for (std::uint32_t local = 0U; local < CbtBaseBisectorCount; ++local)
        {
            const std::uint32_t physical = DynamicCount + local;
            HeapIds[physical] = base.HeapIds[local];
            Data[physical] = base.BisectorData[local];
            const auto remap = [&](std::uint32_t neighbor) {
                return neighbor == InvalidCbtBisectorIndex
                    ? InvalidCbtBisectorIndex
                    : DynamicCount + neighbor - base.Layout.BaseElementOffset;
            };
            Neighbors[physical] = {
                remap(base.Neighbors[local].Previous),
                remap(base.Neighbors[local].Next),
                remap(base.Neighbors[local].Twin),
            };
        }
        for (std::uint32_t slot = 0U; slot < DynamicCount; ++slot)
        {
            Data[slot].Indices.fill(InvalidCbtBisectorIndex);
            Data[slot].ProblematicNeighbor = InvalidCbtBisectorIndex;
            Data[slot].PropagationId = InvalidCbtBisectorIndex;
        }
    }
};

struct QuadFixture
{
    static constexpr std::uint32_t DynamicCount = 12U;
    std::vector<std::uint64_t> HeapIds = std::vector<std::uint64_t>(16U, 0U);
    std::vector<CbtBisectorNeighbors> Neighbors =
        std::vector<CbtBisectorNeighbors>(16U);
    std::vector<CbtBisectorData> Data = std::vector<CbtBisectorData>(16U);

    QuadFixture()
    {
        for (CbtBisectorNeighbors& neighbors : Neighbors)
        {
            neighbors = {
                InvalidCbtBisectorIndex,
                InvalidCbtBisectorIndex,
                InvalidCbtBisectorIndex,
            };
        }
        for (CbtBisectorData& data : Data)
        {
            data.Indices.fill(InvalidCbtBisectorIndex);
            data.ProblematicNeighbor = InvalidCbtBisectorIndex;
            data.PropagationId = InvalidCbtBisectorIndex;
        }
        HeapIds[8] = 16U;
        HeapIds[9] = 17U;
        HeapIds[10] = 18U;
        HeapIds[11] = 19U;
        HeapIds[12] = 20U;
        HeapIds[13] = 21U;
        HeapIds[14] = 22U;
        HeapIds[15] = 23U;
        Neighbors[8] = {9U, 11U, 12U};
        Neighbors[9] = {10U, 8U, 13U};
        Neighbors[10] = {11U, 9U, 14U};
        Neighbors[11] = {8U, 10U, 15U};
        Neighbors[12].Previous = 8U;
        Neighbors[13].Previous = 9U;
        Neighbors[14].Previous = 10U;
        Neighbors[15].Previous = 11U;
        for (std::uint32_t slot = 8U; slot <= 11U; ++slot)
        {
            Data[slot].BisectorState = CbtSimplifyElement;
        }
    }
};
} // namespace

int main()
{
    bool passed = true;

    // 一次边界 center split 后立刻 merge，heapID 和基础邻接应完整往返
    CompactBaseTopology roundTrip;
    const std::uint32_t baseNode = CompactBaseTopology::DynamicCount;
    const std::uint64_t originalHeapId = roundTrip.HeapIds[baseNode];
    const CbtBisectorNeighbors originalNeighbors = roundTrip.Neighbors[baseNode];
    roundTrip.Data[baseNode].SubdivisionPattern = CbtCenterSplitPattern;
    roundTrip.Data[baseNode].Indices[0] = 0U;
    const CbtBisectCommitResult split = CommitCbtBisects(
        roundTrip.HeapIds,
        roundTrip.Neighbors,
        roundTrip.Data,
        {baseNode},
        CompactBaseTopology::DynamicCount);
    auto splitData = split.BisectorData;
    splitData[baseNode].BisectorState = CbtSimplifyElement;
    splitData[0].BisectorState = CbtSimplifyElement;
    const CbtSimplifyCommitResult merged = CommitCbtSimplifications(
        split.HeapIds,
        split.Neighbors,
        splitData,
        {baseNode},
        CompactBaseTopology::DynamicCount);
    passed &= Expect(merged.Valid && merged.PairMergeCount == 1U && merged.QuadMergeCount == 0U,
                     "pair simplify was not committed");
    passed &= Expect(merged.HeapIds[baseNode] == originalHeapId && merged.HeapIds[0] == 0U,
                     "pair simplify heap IDs did not round-trip");
    passed &= Expect(SameNeighbors(merged.Neighbors[baseNode], originalNeighbors),
                     "pair simplify neighbors did not round-trip");
    passed &= Expect(merged.ReleasedDynamicSlots == std::vector<std::uint32_t>{0U},
                     "pair simplify did not release its sibling slot");
    passed &= Expect(
        (merged.BisectorData[baseNode].Flags & CbtMergeEventFlag) != 0U &&
            DecodeCbtDebugEventLifetime(merged.BisectorData[baseNode].Flags) ==
                CbtDebugEventHoldFrames &&
            DecodeCbtActiveDepth(merged.BisectorData[baseNode].Flags) == CbtBaseDepth,
        "merge debug metadata mismatch");
    const std::uint32_t mergePropagationNeighbor = originalNeighbors.Next;
    passed &= Expect(
        mergePropagationNeighbor != InvalidCbtBisectorIndex &&
            (merged.BisectorData[mergePropagationNeighbor].Flags & CbtMergeEventFlag) != 0U &&
            (merged.BisectorData[mergePropagationNeighbor].Flags & CbtModifiedFlag) != 0U &&
            DecodeCbtDebugEventLifetime(
                merged.BisectorData[mergePropagationNeighbor].Flags) ==
                CbtDebugEventHoldFrames,
        "merge propagation target was not included in debug metadata");
    std::string validationError;
    passed &= Expect(
        ValidateCbtSimplifiedTopology(
            merged,
            CompactBaseTopology::DynamicCount,
            &validationError),
        validationError.c_str());

    CbtOccupancyTree occupancy{CbtOccupancyCapacity::Capacity128K};
    passed &= Expect(occupancy.SetBit(0U, true), "could not occupy split slot");
    occupancy.Reduce();
    passed &= Expect(occupancy.SetBit(0U, false), "could not release merged slot");
    occupancy.Reduce();
    passed &= Expect(occupancy.DecodeBitComplement(0U) == 0U,
                     "released slot did not return to the OCBT free ranks");

    // facing pair 由较小逻辑 heapID 唯一提交，并同时释放两个 odd sibling
    QuadFixture quad;
    const CbtSimplifyCommitResult quadMerged = CommitCbtSimplifications(
        quad.HeapIds,
        quad.Neighbors,
        quad.Data,
        {10U, 8U},
        QuadFixture::DynamicCount);
    passed &= Expect(quadMerged.Valid &&
                         quadMerged.SimplificationNodes == std::vector<std::uint32_t>{8U} &&
                         quadMerged.QuadMergeCount == 1U,
                     "facing simplify was not uniquely claimed");
    passed &= Expect(quadMerged.HeapIds[8] == 8U && quadMerged.HeapIds[10] == 9U &&
                         quadMerged.HeapIds[9] == 0U && quadMerged.HeapIds[11] == 0U,
                     "facing simplify heap IDs mismatch");
    passed &= Expect(quadMerged.ReleasedDynamicSlots == std::vector<std::uint32_t>({9U, 11U}),
                     "facing simplify released slots mismatch");
    validationError.clear();
    passed &= Expect(
        ValidateCbtSimplifiedTopology(
            quadMerged,
            QuadFixture::DynamicCount,
            &validationError),
        validationError.c_str());

    QuadFixture depthMismatch;
    depthMismatch.HeapIds[11] = 38U;
    const CbtSimplifyCommitResult rejected = CommitCbtSimplifications(
        depthMismatch.HeapIds,
        depthMismatch.Neighbors,
        depthMismatch.Data,
        {8U},
        QuadFixture::DynamicCount);
    passed &= Expect(rejected.Valid && rejected.SimplificationNodes.empty() &&
                         rejected.ReleasedDynamicSlots.empty(),
                     "different-depth facing nodes should reject simplify");

    // 相邻 merge 同时删除双方 sibling 时，传播要通过保留 pair 互相接回
    std::vector<std::uint64_t> adjacentHeapIds{16U, 17U, 18U, 19U, 20U, 21U};
    std::vector<CbtBisectorNeighbors> adjacentNeighbors(6U);
    adjacentNeighbors[0] = {1U, InvalidCbtBisectorIndex, 4U};
    adjacentNeighbors[1] = {InvalidCbtBisectorIndex, 0U, 3U};
    adjacentNeighbors[2] = {3U, InvalidCbtBisectorIndex, 5U};
    adjacentNeighbors[3] = {InvalidCbtBisectorIndex, 2U, 1U};
    adjacentNeighbors[4] = {0U, InvalidCbtBisectorIndex, InvalidCbtBisectorIndex};
    adjacentNeighbors[5] = {2U, InvalidCbtBisectorIndex, InvalidCbtBisectorIndex};
    std::vector<CbtBisectorData> adjacentData(6U);
    for (CbtBisectorData& data : adjacentData)
    {
        data.Indices.fill(InvalidCbtBisectorIndex);
        data.ProblematicNeighbor = InvalidCbtBisectorIndex;
        data.PropagationId = InvalidCbtBisectorIndex;
    }
    for (std::uint32_t slot = 0U; slot < 4U; ++slot)
    {
        adjacentData[slot].BisectorState = CbtSimplifyElement;
    }
    const CbtSimplifyCommitResult adjacentMerged = CommitCbtSimplifications(
        adjacentHeapIds,
        adjacentNeighbors,
        adjacentData,
        {0U, 2U},
        4U);
    passed &= Expect(adjacentMerged.Valid && adjacentMerged.PairMergeCount == 2U &&
                         adjacentMerged.Neighbors[0].Next == 2U &&
                         adjacentMerged.Neighbors[2].Next == 0U,
                     "adjacent simplify propagation did not reconnect retained pairs");
    validationError.clear();
    passed &= Expect(
        ValidateCbtSimplifiedTopology(adjacentMerged, 4U, &validationError),
        validationError.c_str());

    return passed ? 0 : 1;
}
