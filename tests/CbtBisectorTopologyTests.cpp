#include "algorithms/cbt_2024/CbtBisectorTopology.h"

#include <array>
#include <cstdint>
#include <iostream>
#include <string>

namespace
{
using ParallelRoam::Algorithms::Cbt2024::BuildSquareCbtBaseTopology;
using ParallelRoam::Algorithms::Cbt2024::CbtActiveDepthMask;
using ParallelRoam::Algorithms::Cbt2024::CbtBaseBisectorCount;
using ParallelRoam::Algorithms::Cbt2024::CbtBaseDepth;
using ParallelRoam::Algorithms::Cbt2024::CbtDebugEventHoldFrames;
using ParallelRoam::Algorithms::Cbt2024::CbtDebugEventLifetimeMask;
using ParallelRoam::Algorithms::Cbt2024::CbtDebugEventMask;
using ParallelRoam::Algorithms::Cbt2024::CbtHeapIdChild;
using ParallelRoam::Algorithms::Cbt2024::CbtHeapIdDepth;
using ParallelRoam::Algorithms::Cbt2024::CbtHeapIdParent;
using ParallelRoam::Algorithms::Cbt2024::CbtMergeEventFlag;
using ParallelRoam::Algorithms::Cbt2024::CbtModifiedFlag;
using ParallelRoam::Algorithms::Cbt2024::CbtOccupancyCapacity;
using ParallelRoam::Algorithms::Cbt2024::CbtSplitEventFlag;
using ParallelRoam::Algorithms::Cbt2024::CbtValidationMaxActiveDepthWord;
using ParallelRoam::Algorithms::Cbt2024::CbtValidationWordCount;
using ParallelRoam::Algorithms::Cbt2024::CbtVisibleFlag;
using ParallelRoam::Algorithms::Cbt2024::DecodeCbtActiveDepth;
using ParallelRoam::Algorithms::Cbt2024::DecodeCbtDebugEventLifetime;
using ParallelRoam::Algorithms::Cbt2024::EncodeCbtActiveDepth;
using ParallelRoam::Algorithms::Cbt2024::EncodeCbtDebugEventLifetime;
using ParallelRoam::Algorithms::Cbt2024::InvalidCbtBisectorIndex;
using ParallelRoam::Algorithms::Cbt2024::ValidateCbtBaseTopology;
using ParallelRoam::Algorithms::Cbt2024::WithCbtDebugEvent;

bool Expect(bool condition, const std::string& message)
{
    if (!condition)
    {
        std::cerr << message << '\n';
    }
    return condition;
}

bool ValidateCapacity(CbtOccupancyCapacity capacity)
{
    const auto topology = BuildSquareCbtBaseTopology(capacity);
    const auto& layout = topology.Layout;
    bool valid = true;

    std::string errorMessage;
    valid &= Expect(ValidateCbtBaseTopology(topology, &errorMessage), errorMessage);
    valid &= Expect(layout.BaseElementOffset == static_cast<std::uint32_t>(capacity), "base offset mismatch");
    valid &= Expect(layout.TotalElementCount == static_cast<std::uint32_t>(capacity) + 6U, "total count mismatch");
    valid &= Expect(layout.ClassificationElementCount == 2U + 2U * layout.TotalElementCount, "classification size mismatch");
    valid &= Expect(layout.SimplificationElementCount == 1U + layout.TotalElementCount, "simplification size mismatch");
    valid &= Expect(layout.AllocationElementCount == 1U + layout.TotalElementCount, "allocation size mismatch");
    valid &= Expect(layout.PropagationElementCount == 2U + layout.TotalElementCount, "propagation size mismatch");
    valid &= Expect(layout.MemoryElementCount == 2U, "memory header size mismatch");
    valid &= Expect(
        layout.ValidationElementCount == CbtValidationWordCount,
        "validation and F diagnostics size mismatch");
    valid &= Expect(CbtValidationWordCount == 19U, "validation diagnostics must include maximum active depth");
    valid &= Expect(
        CbtValidationMaxActiveDepthWord + 1U == CbtValidationWordCount,
        "maximum active depth must occupy the validation tail word");
    valid &= Expect(layout.DrawStateElementCount == 10U, "draw state must contain ten uints");
    valid &= Expect(layout.TopologyDispatchElementCount == 9U, "topology indirect scratch size mismatch");
    valid &= Expect(layout.GeometryDispatchElementCount == 9U, "geometry dispatch size mismatch");

    for (std::uint32_t index = 0U; index < CbtBaseBisectorCount; ++index)
    {
        valid &= Expect(topology.HeapIds[index] == 8U + index, "base heap ID mismatch");
        valid &= Expect(CbtHeapIdDepth(topology.HeapIds[index]) == 3U, "base heap depth mismatch");
        valid &= Expect(topology.ActiveIndices[index] >= layout.DynamicElementCount, "base slot consumes dynamic budget");
        valid &= Expect(topology.BisectorData[index].ProblematicNeighbor == InvalidCbtBisectorIndex, "base problematic neighbor should be invalid");
        valid &= Expect(topology.BisectorData[index].PropagationId == InvalidCbtBisectorIndex, "base propagation ID should be invalid");
        valid &= Expect(
            DecodeCbtActiveDepth(topology.BisectorData[index].Flags) == CbtBaseDepth,
            "base debug depth mismatch");
    }

    valid &= Expect(topology.Neighbors[1].Twin == layout.BaseElementOffset + 3U, "shared diagonal twin mismatch");
    valid &= Expect(topology.Neighbors[3].Twin == layout.BaseElementOffset + 1U, "reverse diagonal twin mismatch");
    const std::array<std::uint32_t, 4> boundaryHalfedges{0U, 2U, 4U, 5U};
    for (const std::uint32_t halfedge : boundaryHalfedges)
    {
        valid &= Expect(topology.Neighbors[halfedge].Twin == InvalidCbtBisectorIndex, "boundary twin should be invalid");
    }

    for (const auto& controlPoint : topology.ControlPoints)
    {
        valid &= Expect(controlPoint.Height == 0.0F, "base control point height should start at zero");
        valid &= Expect(controlPoint.U >= 0.0F && controlPoint.U <= 1.0F, "base control point U is out of range");
        valid &= Expect(controlPoint.V >= 0.0F && controlPoint.V <= 1.0F, "base control point V is out of range");
    }

    valid &= Expect(topology.IndirectDrawState.Active.VertexCountPerInstance == 18U, "active draw count mismatch");
    valid &= Expect(topology.IndirectDrawState.Visible.VertexCountPerInstance == 18U, "visible draw count mismatch");
    valid &= Expect(topology.IndirectDrawState.ModifiedPositionCount == 24U, "modified position count mismatch");
    valid &= Expect(topology.IndirectDrawState.ActiveBisectorCount == 6U, "active bisector count mismatch");
    valid &= Expect(topology.GeometryDispatchCommands[0].ThreadGroupCountX == 1U, "active dispatch count mismatch");
    valid &= Expect(topology.GeometryDispatchCommands[2].ThreadGroupCountX == 1U, "modified dispatch count mismatch");
    return valid;
}

bool ValidateHeapNavigation()
{
    bool valid = true;
    for (std::uint64_t heapId = 8U; heapId <= 13U; ++heapId)
    {
        const std::uint64_t leftChild = CbtHeapIdChild(heapId, false);
        const std::uint64_t rightChild = CbtHeapIdChild(heapId, true);
        valid &= Expect(CbtHeapIdParent(leftChild) == heapId, "left child parent mismatch");
        valid &= Expect(CbtHeapIdParent(rightChild) == heapId, "right child parent mismatch");
        valid &= Expect(CbtHeapIdDepth(leftChild) == CbtHeapIdDepth(heapId) + 1U, "child depth mismatch");
    }
    return valid;
}

bool ValidateDebugFlagLayout()
{
    const std::uint32_t operationalMask = CbtVisibleFlag | CbtModifiedFlag;
    const std::uint32_t eventFlags = CbtSplitEventFlag | CbtMergeEventFlag;
    bool valid = true;
    valid &= Expect((operationalMask & CbtDebugEventMask) == 0U,
                    "debug event flags overlap operational flags");
    valid &= Expect(eventFlags == CbtDebugEventMask,
                    "debug event mask does not cover split and merge");
    valid &= Expect((CbtDebugEventMask & CbtDebugEventLifetimeMask) == 0U,
                    "debug event type overlaps its lifetime");
    valid &= Expect(
        (CbtActiveDepthMask &
         (operationalMask | CbtDebugEventMask | CbtDebugEventLifetimeMask)) == 0U,
        "active depth overlaps another flag range");
    valid &= Expect(
        DecodeCbtDebugEventLifetime(
            EncodeCbtDebugEventLifetime(CbtDebugEventHoldFrames)) ==
            CbtDebugEventHoldFrames,
        "debug event lifetime did not round-trip");
    valid &= Expect(DecodeCbtActiveDepth(EncodeCbtActiveDepth(20U)) == 20U,
                    "active debug depth did not round-trip");
    volatile std::uint32_t eventInput =
        CbtVisibleFlag | CbtSplitEventFlag | EncodeCbtDebugEventLifetime(1U) |
        EncodeCbtActiveDepth(12U);
    const std::uint32_t replacedEvent =
        WithCbtDebugEvent(eventInput, CbtMergeEventFlag);
    valid &= Expect(
        (replacedEvent & (CbtVisibleFlag | CbtModifiedFlag | CbtMergeEventFlag)) ==
                (CbtVisibleFlag | CbtModifiedFlag | CbtMergeEventFlag) &&
            (replacedEvent & CbtSplitEventFlag) == 0U &&
            DecodeCbtDebugEventLifetime(replacedEvent) == CbtDebugEventHoldFrames &&
            DecodeCbtActiveDepth(replacedEvent) == 12U,
        "topology event replacement damaged operational or depth metadata");
    return valid;
}
} // namespace

int main()
{
    bool valid = ValidateHeapNavigation() && ValidateDebugFlagLayout();
    const std::array<CbtOccupancyCapacity, 4> capacities{
        CbtOccupancyCapacity::Capacity128K,
        CbtOccupancyCapacity::Capacity256K,
        CbtOccupancyCapacity::Capacity512K,
        CbtOccupancyCapacity::Capacity1M,
    };
    for (const CbtOccupancyCapacity capacity : capacities)
    {
        valid &= ValidateCapacity(capacity);
    }
    return valid ? 0 : 1;
}
