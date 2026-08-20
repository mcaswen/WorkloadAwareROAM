#include "algorithms/data_oriented_roam/DataOrientedRoamScoring.h"
#include "algorithms/data_oriented_roam/DataOrientedRoamVariance.h"

#include "algorithms/ITerrainLodAlgorithm.h"
#include "algorithms/RoamGeometry.h"
#include "algorithms/RoamNestedWedgie.h"
#include "algorithms/RoamScreenError.h"

#include <algorithm>

namespace ParallelRoam::Algorithms::DataOrientedRoam
{
TriangleDomainChildren SplitTriangleDomain(const TriangleDomain& domain)
{
    const auto children = Roam::SplitTriangleDomain(domain);
    return TriangleDomainChildren{
        children.Left,
        children.Right,
    };
}

bool ShouldSplitWithScore(
    const DataOrientedRoamState& state,
    DataOrientedRoamNodeIndex node,
    float screenErrorScore)
{
    if (state.Nodes.DepthAt(node) >= state.Settings.MaxDepth)
    {
        return false;
    }

    if (screenErrorScore > state.Settings.SplitThreshold)
    {
        // 高于 split 阈值时直接展开
        return true;
    }

    if (screenErrorScore < state.Settings.MergeThreshold)
    {
        // 低于 merge 阈值时明确不 split
        // 中间区间才交给 hysteresis 保持稳定
        return false;
    }

    // hysteresis 区间沿用上一帧 split 状态
    // 避免相机轻微移动造成频繁 split / merge 抖动
    return WasSplitLastFrame(state, node);
}

bool WasSplitLastFrame(const DataOrientedRoamState& state, DataOrientedRoamNodeIndex node)
{
    // hysteresis 只看上一帧最终 active split path
    return state.PreviousSplitPaths.find(state.Nodes.PathIdAt(node)) != state.PreviousSplitPaths.end();
}

DataOrientedRoamLeafDebugClass ClassifyLeafDebug(
    const DataOrientedRoamState& state,
    DataOrientedRoamNodeIndex node)
{
    if (state.Nodes.ActivatedBuildIdAt(node) == state.BuildSequence ||
        state.Nodes.MergeBuildIdAt(node) == state.BuildSequence)
    {
        return DataOrientedRoamLeafDebugClass::Rebuilt;
    }

    if (state.Nodes.DepthAt(node) > 0)
    {
        return DataOrientedRoamLeafDebugClass::Subdivided;
    }

    return DataOrientedRoamLeafDebugClass::Original;
}

glm::vec3 DebugColorForLeaf(const DataOrientedRoamState& state, DataOrientedRoamNodeIndex node)
{
    const float depthRatio = std::clamp(
        static_cast<float>(state.Nodes.DepthAt(node)) /
            static_cast<float>(std::max(state.Settings.MaxDepth, 1)),
        0.0F,
        1.0F);

    switch (ClassifyLeafDebug(state, node))
    {
    case DataOrientedRoamLeafDebugClass::Original:
        return glm::vec3{0.28F, 0.34F, 0.30F};
    case DataOrientedRoamLeafDebugClass::Subdivided:
        return glm::mix(glm::vec3{0.08F, 0.72F, 0.62F}, glm::vec3{0.10F, 0.34F, 0.95F}, depthRatio);
    case DataOrientedRoamLeafDebugClass::Rebuilt:
        if (state.Nodes.ActivatedByForcedSplitAt(node))
        {
            return glm::mix(glm::vec3{0.96F, 0.34F, 0.90F}, glm::vec3{0.96F, 0.16F, 0.42F}, depthRatio);
        }

        return glm::mix(glm::vec3{1.0F, 0.68F, 0.15F}, glm::vec3{1.0F, 0.34F, 0.10F}, depthRatio);
    }

    return glm::vec3{0.28F, 0.34F, 0.30F};
}

float DebugHighlightForLeaf(const DataOrientedRoamState& state, DataOrientedRoamNodeIndex node)
{
    switch (ClassifyLeafDebug(state, node))
    {
    case DataOrientedRoamLeafDebugClass::Original:
        return 0.35F;
    case DataOrientedRoamLeafDebugClass::Subdivided:
        return 0.70F;
    case DataOrientedRoamLeafDebugClass::Rebuilt:
        return 1.0F;
    }

    return 0.35F;
}

void RebuildVarianceTrees(DataOrientedRoamState& state, int finestDepth)
{
    const TriangleDomain rootA{
        glm::vec2{0.0F, 1.0F},
        glm::vec2{1.0F, 0.0F},
        glm::vec2{0.0F, 0.0F},
    };
    const TriangleDomain rootB{
        glm::vec2{1.0F, 0.0F},
        glm::vec2{0.0F, 1.0F},
        glm::vec2{1.0F, 1.0F},
    };
    const auto splitDomain = [](const TriangleDomain& domain) {
        return SplitTriangleDomain(domain);
    };
    const auto signedDisplacement = [&state](const TriangleDomain& domain) {
        return Roam::ComputeBaseMidpointDisplacement(*state.HeightMap, domain);
    };
    static_cast<void>(Roam::BuildNestedWedgieTree(
        rootA,
        finestDepth,
        state.VarianceTrees[0],
        splitDomain,
        signedDisplacement));
    static_cast<void>(Roam::BuildNestedWedgieTree(
        rootB,
        finestDepth,
        state.VarianceTrees[1],
        splitDomain,
        signedDisplacement));
    state.VarianceHeightMap = state.HeightMap;
    state.VarianceTreeMaxDepth = finestDepth;
}

void RefreshNodeVarianceErrors(DataOrientedRoamState& state)
{
    for (std::size_t node = 0U; node < state.Nodes.size(); ++node)
    {
        state.Nodes.GeometricErrors[node] = VarianceError(
            state,
            state.Nodes.VarianceTreeIndices[node],
            state.Nodes.VarianceIndices[node]);
    }
}

float VarianceError(
    const DataOrientedRoamState& state,
    std::uint8_t varianceTreeIndex,
    std::size_t varianceIndex)
{
    const std::size_t treeIndex = static_cast<std::size_t>(varianceTreeIndex);
    if (treeIndex >= state.VarianceTrees.size() || varianceIndex >= state.VarianceTrees[treeIndex].size())
    {
        return 0.0F;
    }
    return state.VarianceTrees[treeIndex][varianceIndex];
}

float ComputeScreenErrorScore(const DataOrientedRoamState& state, DataOrientedRoamNodeIndex node)
{
    const float worldError = state.Nodes.GeometricErrorAt(node) * state.HeightScale;
    const TriangleDomain& domain = state.Nodes.DomainAt(node);
    const std::array<glm::vec3, 3U> triangle{
        Roam::DomainToWorld(*state.HeightMap, domain.A, state.TerrainSize, state.HeightScale),
        Roam::DomainToWorld(*state.HeightMap, domain.B, state.TerrainSize, state.HeightScale),
        Roam::DomainToWorld(*state.HeightMap, domain.C, state.TerrainSize, state.HeightScale),
    };
    const std::size_t nearPlaneIndex = static_cast<std::size_t>(TerrainLodFrustumPlane::Near);
    return Roam::ComputeScreenErrorScore({
        triangle,
        worldError,
        state.ViewProjection,
        state.FrustumPlanes[nearPlaneIndex],
        state.FrustumPlanes,
        state.DrawableWidth,
        state.DrawableHeight,
    });
}
} // 命名空间 ParallelRoam::Algorithms::DataOrientedRoam
