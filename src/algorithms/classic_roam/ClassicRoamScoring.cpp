#include "algorithms/classic_roam/ClassicRoamMeshBuilder.h"

#include "algorithms/RoamGeometry.h"
#include "algorithms/RoamNestedWedgie.h"
#include "algorithms/RoamScreenError.h"
#include "algorithms/ITerrainLodAlgorithm.h"

#include <algorithm>

namespace ParallelRoam::Algorithms::ClassicRoam
{
TriangleDomainChildren SplitTriangleDomain(const TriangleDomain& domain)
{
    // A/B 始终表示底边，C 表示顶点；两个子三角形继续保持逆时针绕序
    const auto children = Roam::SplitTriangleDomain(domain);
    return TriangleDomainChildren{
        children.Left,
        children.Right,
    };
}

bool ClassicRoamMeshBuilder::ShouldSplit(const ClassicRoamNode& node) const
{
    // 先执行深度限制，避免相机贴近地形时继续细分到实现上限之外
    if (node.Depth >= _settings.MaxDepth)
    {
        return false;
    }

    return ShouldSplitWithScore(node, ComputeScreenErrorScore(node));
}

bool ClassicRoamMeshBuilder::ShouldSplitWithScore(const ClassicRoamNode& node, float screenErrorScore) const
{
    if (node.Depth >= _settings.MaxDepth)
    {
        return false;
    }

    if (screenErrorScore > _settings.SplitThreshold)
    {
        // 误差明确超过细分阈值时直接细分，不再沿用上一帧状态
        return true;
    }

    if (screenErrorScore < _settings.MergeThreshold)
    {
        return false;
    }

    // 误差落在迟滞区间时沿用上一帧状态，减少细分与合并反复切换
    // 固定相机的基准测试也因此能保持稳定拓扑
    return WasSplitLastFrame(node);
}

bool ClassicRoamMeshBuilder::WasSplitLastFrame(const ClassicRoamNode& node) const
{
    return _previousSplitPaths.find(node.PathId) != _previousSplitPaths.end();
}

ClassicRoamMeshBuilder::LeafDebugClass ClassicRoamMeshBuilder::ClassifyLeafDebug(const ClassicRoamNode& node) const
{
    if (node.ActivatedBuildId == _buildSequence || node.MergeBuildId == _buildSequence)
    {
        // 本帧新激活的叶节点和合并后恢复的父节点都标记为重建
        return LeafDebugClass::Rebuilt;
    }

    if (node.Depth > 0)
    {
        return LeafDebugClass::Subdivided;
    }

    return LeafDebugClass::Original;
}

glm::vec3 ClassicRoamMeshBuilder::DebugColorForLeaf(const ClassicRoamNode& node) const
{
    const float depthRatio = std::clamp(
        static_cast<float>(node.Depth) / static_cast<float>(std::max(_settings.MaxDepth, 1)),
        0.0F,
        1.0F);

    switch (ClassifyLeafDebug(node))
    {
    case LeafDebugClass::Original:
        return glm::vec3{0.28F, 0.34F, 0.30F};
    case LeafDebugClass::Subdivided:
        return glm::mix(glm::vec3{0.08F, 0.72F, 0.62F}, glm::vec3{0.10F, 0.34F, 0.95F}, depthRatio);
    case LeafDebugClass::Rebuilt:
        // 强制细分使用粉色，便于定位为修补裂缝而额外细分的区域
        if (node.ActivatedByForcedSplit)
        {
            return glm::mix(glm::vec3{0.96F, 0.34F, 0.90F}, glm::vec3{0.96F, 0.16F, 0.42F}, depthRatio);
        }

        // 普通重建使用暖色，与跨帧保留的稳定细分区域区分
        return glm::mix(glm::vec3{1.0F, 0.68F, 0.15F}, glm::vec3{1.0F, 0.34F, 0.10F}, depthRatio);
    }

    return glm::vec3{0.28F, 0.34F, 0.30F};
}

float ClassicRoamMeshBuilder::DebugHighlightForLeaf(const ClassicRoamNode& node) const
{
    // 高亮强度与颜色使用同一分类，保证界面中的调试含义一致
    switch (ClassifyLeafDebug(node))
    {
    case LeafDebugClass::Original:
        return 0.35F;
    case LeafDebugClass::Subdivided:
        return 0.70F;
    case LeafDebugClass::Rebuilt:
        return 1.0F;
    }

    return 0.35F;
}

void ClassicRoamMeshBuilder::RebuildVarianceTrees(int finestDepth)
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
    const auto signedDisplacement = [this](const TriangleDomain& domain) {
        return Roam::ComputeBaseMidpointDisplacement(*_heightMap, domain);
    };
    static_cast<void>(Roam::BuildNestedWedgieTree(
        rootA,
        finestDepth,
        _varianceTrees[0],
        splitDomain,
        signedDisplacement));
    static_cast<void>(Roam::BuildNestedWedgieTree(
        rootB,
        finestDepth,
        _varianceTrees[1],
        splitDomain,
        signedDisplacement));
    _varianceHeightMap = _heightMap;
    _varianceTreeMaxDepth = finestDepth;
}

void ClassicRoamMeshBuilder::RefreshNodeVarianceErrors()
{
    for (const std::unique_ptr<ClassicRoamNode>& node : _nodes)
    {
        node->GeometricError = VarianceError(node->VarianceTreeIndex, node->VarianceIndex);
    }
}

float ClassicRoamMeshBuilder::VarianceError(std::uint8_t varianceTreeIndex, std::size_t varianceIndex) const
{
    const std::size_t treeIndex = static_cast<std::size_t>(varianceTreeIndex);
    if (treeIndex >= _varianceTrees.size() || varianceIndex >= _varianceTrees[treeIndex].size())
    {
        return 0.0F;
    }

    return _varianceTrees[treeIndex][varianceIndex];
}

float ClassicRoamMeshBuilder::ComputeScreenErrorScore(const ClassicRoamNode& node) const
{
    const float worldError = node.GeometricError * _heightScale;
    const std::array<glm::vec3, 3U> triangle{
        Roam::DomainToWorld(*_heightMap, node.Domain.A, _terrainSize, _heightScale),
        Roam::DomainToWorld(*_heightMap, node.Domain.B, _terrainSize, _heightScale),
        Roam::DomainToWorld(*_heightMap, node.Domain.C, _terrainSize, _heightScale),
    };
    const std::size_t nearPlaneIndex = static_cast<std::size_t>(TerrainLodFrustumPlane::Near);
    return Roam::ComputeScreenErrorScore({
        triangle,
        worldError,
        _viewProjection,
        _frustumPlanes[nearPlaneIndex],
        _frustumPlanes,
        _drawableWidth,
        _drawableHeight,
    });
}
} // 命名空间 ParallelRoam::Algorithms::ClassicRoam
