#include "algorithms/cbt_2024/CbtBisectCommit.h"
#include "algorithms/cbt_2024/CbtSplitPlanner.h"

#include <cmath>
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

struct TemplateFixture
{
    std::vector<std::uint64_t> HeapIds = std::vector<std::uint64_t>(16U, 0U);
    std::vector<CbtBisectorNeighbors> Neighbors = std::vector<CbtBisectorNeighbors>(16U);
    std::vector<CbtBisectorData> Data = std::vector<CbtBisectorData>(16U);

    TemplateFixture()
    {
        for (CbtBisectorNeighbors& neighbors : Neighbors)
        {
            neighbors = {InvalidCbtBisectorIndex, InvalidCbtBisectorIndex, InvalidCbtBisectorIndex};
        }
        for (CbtBisectorData& data : Data)
        {
            data.Indices.fill(InvalidCbtBisectorIndex);
            data.ProblematicNeighbor = InvalidCbtBisectorIndex;
            data.PropagationId = InvalidCbtBisectorIndex;
        }
        HeapIds[8] = 8U;
        HeapIds[9] = 9U;
        HeapIds[10] = 10U;
        HeapIds[11] = 11U;
        HeapIds[12] = 12U;
        Neighbors[8] = {9U, 10U, InvalidCbtBisectorIndex};
        Neighbors[9] = {8U, InvalidCbtBisectorIndex, InvalidCbtBisectorIndex};
        Neighbors[10] = {8U, InvalidCbtBisectorIndex, InvalidCbtBisectorIndex};
        Data[9].SubdivisionPattern = CbtCenterSplitPattern;
        Data[9].Indices[0] = 11U;
        Data[10].SubdivisionPattern = CbtCenterSplitPattern;
        Data[10].Indices[0] = 12U;
        Data[8].Indices = {0U, 1U, 2U};
    }
};

bool SamePoint(const CbtBaseControlPoint& point, float u, float v)
{
    return std::abs(point.U - u) < 1.0e-6F &&
           std::abs(point.Height) < 1.0e-6F &&
           std::abs(point.V - v) < 1.0e-6F;
}
} // namespace

int main()
{
    bool passed = true;

    TemplateFixture center;
    center.Data[8].SubdivisionPattern = CbtCenterSplitPattern;
    center.Data[10].SubdivisionPattern = CbtNoSplitPattern;
    const CbtBisectCommitResult centerResult =
        CommitCbtBisects(center.HeapIds, center.Neighbors, center.Data, {8U}, 8U);
    passed &= Expect(centerResult.Valid && centerResult.HeapIds[8] == 16U &&
                         centerResult.HeapIds[0] == 17U,
                     "center template heap IDs mismatch");
    passed &= Expect(centerResult.Neighbors[8].Previous == 0U &&
                         centerResult.Neighbors[0].Next == 8U &&
                         centerResult.TemplateCounts[0] == 1U,
                     "center template neighbor layout mismatch");
    passed &= Expect(centerResult.PropagationNodes == std::vector<std::uint32_t>{0U} &&
                         centerResult.BisectorData[0].ProblematicNeighbor == InvalidCbtBisectorIndex,
                     "center template propagation was not completed");
    passed &= Expect(
        (centerResult.BisectorData[8].Flags & CbtSplitEventFlag) != 0U &&
            DecodeCbtDebugEventLifetime(centerResult.BisectorData[8].Flags) ==
                CbtDebugEventHoldFrames &&
            DecodeCbtActiveDepth(centerResult.BisectorData[8].Flags) == 5U,
        "split debug metadata mismatch");
    passed &= Expect(
        (centerResult.BisectorData[10].Flags & CbtSplitEventFlag) != 0U &&
            (centerResult.BisectorData[10].Flags & CbtModifiedFlag) != 0U &&
            DecodeCbtDebugEventLifetime(centerResult.BisectorData[10].Flags) ==
                CbtDebugEventHoldFrames,
        "split propagation target was not included in debug metadata");

    TemplateFixture boundaryCenter;
    boundaryCenter.Neighbors[8].Next = InvalidCbtBisectorIndex;
    boundaryCenter.Neighbors[8].Twin = InvalidCbtBisectorIndex;
    boundaryCenter.Data[8].SubdivisionPattern = CbtCenterSplitPattern;
    const CbtBisectCommitResult boundaryCenterResult = CommitCbtBisects(
        boundaryCenter.HeapIds,
        boundaryCenter.Neighbors,
        boundaryCenter.Data,
        {8U},
        8U);
    passed &= Expect(boundaryCenterResult.Valid &&
                         boundaryCenterResult.BisectorData[0].ProblematicNeighbor ==
                             InvalidCbtBisectorIndex,
                     "boundary center propagation was not completed");

    TemplateFixture right;
    right.Data[8].SubdivisionPattern = CbtRightDoubleSplitPattern;
    right.Data[10].SubdivisionPattern = CbtNoSplitPattern;
    const CbtBisectCommitResult rightResult =
        CommitCbtBisects(right.HeapIds, right.Neighbors, right.Data, {8U}, 8U);
    passed &= Expect(rightResult.Valid && rightResult.HeapIds[8] == 32U &&
                         rightResult.HeapIds[0] == 17U && rightResult.HeapIds[1] == 33U,
                     "right-double template heap IDs mismatch");
    passed &= Expect(rightResult.Neighbors[8].Previous == 1U &&
                         rightResult.Neighbors[8].Twin == 0U &&
                         rightResult.TemplateCounts[1] == 1U,
                     "right-double template neighbor layout mismatch");

    TemplateFixture left;
    left.Data[8].SubdivisionPattern = CbtLeftDoubleSplitPattern;
    const CbtBisectCommitResult leftResult =
        CommitCbtBisects(left.HeapIds, left.Neighbors, left.Data, {8U}, 8U);
    passed &= Expect(leftResult.Valid && leftResult.HeapIds[8] == 16U &&
                         leftResult.HeapIds[0] == 34U && leftResult.HeapIds[1] == 35U,
                     "left-double template heap IDs mismatch");
    passed &= Expect(leftResult.Neighbors[1].Next == 0U &&
                         leftResult.Neighbors[1].Twin == 8U &&
                         leftResult.TemplateCounts[2] == 1U,
                     "left-double template neighbor layout mismatch");

    TemplateFixture triple;
    triple.Data[8].SubdivisionPattern =
        CbtCenterSplitPattern | CbtRightSplitPattern | CbtLeftSplitPattern;
    const CbtBisectCommitResult tripleResult =
        CommitCbtBisects(triple.HeapIds, triple.Neighbors, triple.Data, {8U}, 8U);
    passed &= Expect(tripleResult.Valid && tripleResult.HeapIds[8] == 32U &&
                         tripleResult.HeapIds[0] == 34U &&
                         tripleResult.HeapIds[1] == 33U &&
                         tripleResult.HeapIds[2] == 35U,
                     "triple template heap IDs mismatch");
    passed &= Expect(tripleResult.Neighbors[8].Previous == 1U &&
                         tripleResult.Neighbors[8].Twin == 2U &&
                         tripleResult.TemplateCounts[3] == 1U,
                     "triple template neighbor layout mismatch");

    // 在完整六半边基础网格上提交边界 center split，验证传播后的双向邻接。
    const CbtBaseTopology base = BuildSquareCbtBaseTopology(CbtOccupancyCapacity::Capacity128K);
    constexpr std::uint32_t dynamicCount = 8U;
    std::vector<std::uint64_t> heapIds(dynamicCount + CbtBaseBisectorCount, 0U);
    std::vector<CbtBisectorNeighbors> neighbors(heapIds.size());
    std::vector<CbtBisectorData> data(heapIds.size());
    for (std::uint32_t local = 0U; local < CbtBaseBisectorCount; ++local)
    {
        const std::uint32_t physical = dynamicCount + local;
        heapIds[physical] = base.HeapIds[local];
        data[physical] = base.BisectorData[local];
        const auto remap = [&](std::uint32_t neighbor) {
            return neighbor == InvalidCbtBisectorIndex
                ? InvalidCbtBisectorIndex
                : dynamicCount + neighbor - base.Layout.BaseElementOffset;
        };
        neighbors[physical] = {
            remap(base.Neighbors[local].Previous),
            remap(base.Neighbors[local].Next),
            remap(base.Neighbors[local].Twin),
        };
    }
    data[dynamicCount].SubdivisionPattern = CbtCenterSplitPattern;
    data[dynamicCount].Indices[0] = 0U;
    const CbtBisectCommitResult integrated =
        CommitCbtBisects(heapIds, neighbors, data, {dynamicCount}, dynamicCount);
    std::string validationError;
    const bool integratedValid =
        ValidateCbtCommittedTopology(integrated, dynamicCount, &validationError);
    passed &= Expect(integratedValid, integratedValid ? "" : validationError.c_str());

    const CbtLebTriangleResult baseTriangle =
        EvaluateCbtLebTriangle(8U, base.BaseDepth, base.ControlPoints);
    passed &= Expect(baseTriangle.Valid && baseTriangle.BaseBisector == 0U &&
                         SamePoint(baseTriangle.Child[0], 0.0F, 1.0F) &&
                         SamePoint(baseTriangle.Child[2], 0.0F, 0.0F),
                     "base LEB triangle decode mismatch");
    const CbtLebTriangleResult leftChild =
        EvaluateCbtLebTriangle(16U, base.BaseDepth, base.ControlPoints);
    passed &= Expect(leftChild.Valid && SamePoint(leftChild.Child[0], 0.0F, 0.0F) &&
                         SamePoint(leftChild.Child[1], 0.0F, 0.5F) &&
                         SamePoint(leftChild.Child[2], 1.0F / 3.0F, 1.0F / 3.0F),
                     "left-child LEB triangle decode mismatch");
    passed &= Expect(SamePoint(leftChild.Parent[0], 0.0F, 1.0F) &&
                         SamePoint(leftChild.Parent[2], 0.0F, 0.0F),
                     "LEB parent triangle decode mismatch");

    return passed ? 0 : 1;
}
