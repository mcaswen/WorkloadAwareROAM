#include "algorithms/classic_roam/ClassicRoamMeshBuilder.h"

#include <algorithm>
#include <memory>

namespace ParallelRoam::Algorithms::ClassicRoam
{
namespace
{
constexpr std::uint64_t RootAPathId = 1ULL;
constexpr std::uint64_t RootBPathId = 1ULL << 32U;
} // 匿名命名空间

const ClassicRoamStats& ClassicRoamMeshBuilder::Stats() const
{
    // adapter 层通过只读引用读取最近一帧统计
    return _stats;
}

const std::vector<ClassicRoamMeshUpdateRange>& ClassicRoamMeshBuilder::MeshUpdateRanges() const
{
    return _meshUpdateRanges;
}

bool ClassicRoamMeshBuilder::MeshRequiresFullUpload() const
{
    return _meshRequiresFullUpload;
}

std::uint64_t ClassicRoamMeshBuilder::MeshGeneration() const
{
    return _meshGeneration;
}

ClassicRoamMeshBuilder::ClassicRoamNode* ClassicRoamMeshBuilder::AddNode(
    const TriangleDomain& domain,
    ClassicRoamNode* parent,
    int depth,
    std::uint64_t pathId,
    std::uint8_t varianceTreeIndex,
    std::size_t varianceIndex)
{
    std::unique_ptr<ClassicRoamNode> node = std::make_unique<ClassicRoamNode>();
    node->Domain = domain;
    node->Parent = parent;
    node->Depth = depth;
    node->PathId = pathId;
    node->VarianceTreeIndex = varianceTreeIndex;
    node->VarianceIndex = varianceIndex;
    // CreatedBuildId 记录节点第一次进入持久化池的帧
    node->CreatedBuildId = _buildSequence;
    node->ActivatedBuildId = _buildSequence;
    // nested wedgie tree 已按公式 (1) 累积子树误差，节点只复制稳定索引对应值
    node->GeometricError = VarianceError(varianceTreeIndex, varianceIndex);
    // 创建时更新一次，后续最终统计会按 active leaf 重算
    _stats.MaxDepthReached = std::max(_stats.MaxDepthReached, depth);

    // unique_ptr 池负责生命周期，节点之间保留 Classic ROAM 裸指针关系
    ClassicRoamNode* nodePointer = node.get();
    _nodes.push_back(std::move(node));
    return nodePointer;
}

void ClassicRoamMeshBuilder::ResetTopology()
{
    // ResetTopology 是唯一清空 node pool 的入口，避免普通 frame 破坏持久化拓扑
    _splitQueue.clear();
    _mergeQueue.clear();
    _nodes.clear();
    _previousSplitPaths.clear();
    _currentSplitPaths.clear();

    // rootA 和 rootB 分别覆盖同一正方形的两半
    // 两个根三角形共享对角线 base edge，构成 Classic ROAM 的根 diamond
    _rootA = AddNode(
        TriangleDomain{glm::vec2{0.0F, 1.0F}, glm::vec2{1.0F, 0.0F}, glm::vec2{0.0F, 0.0F}},
        nullptr,
        0,
        RootAPathId,
        0,
        0);
    _rootB = AddNode(
        TriangleDomain{glm::vec2{1.0F, 0.0F}, glm::vec2{0.0F, 1.0F}, glm::vec2{1.0F, 1.0F}},
        nullptr,
        0,
        RootBPathId,
        1,
        0);

    // 根节点跨共享 base edge 互为 base neighbor
    _rootA->BaseNeighbor = _rootB;
    _rootB->BaseNeighbor = _rootA;
    _rootA->Active = true;
    _rootB->Active = true;
    InitializePersistentQueues();
    _topologyMaxDepth = _settings.MaxDepth;
}

bool ClassicRoamMeshBuilder::NeedsTopologyReset(
    const Terrain::HeightMap& heightMap,
    float terrainSize,
    float heightScale,
    const ClassicRoamSettings& settings) const
{
    if (_rootA == nullptr || _rootB == nullptr || _nodes.empty())
    {
        // 首帧没有 root diamond，必须初始化
        return true;
    }

    if (_heightMap != &heightMap)
    {
        // Height Map 变化会让几何误差缓存失效
        return true;
    }

    if (settings.MaxDepth < _topologyMaxDepth)
    {
        // 降低最大深度时，保守重建以清理过深的历史节点
        return true;
    }

    if (settings.TriangleBudget != _settings.TriangleBudget)
    {
        // 预算变化时从两个根重新按优先级分配，保证降低预算后当前 Build 立即满足硬上限
        return true;
    }

    // world-space 输入变化会改变 screen error 和 debug 高度映射
    // terrain size 或 height scale 改变时，保守重建以避免旧 score 驱动错误 hysteresis
    return terrainSize != _terrainSize || heightScale != _heightScale;
}

void ClassicRoamMeshBuilder::CollectLeafNodes(std::vector<ClassicRoamNode*>& leafNodes) const
{
    // leaf 集合是当前 active mesh 的拓扑基础
    leafNodes.clear();
    leafNodes.reserve(_nodes.size());
    // reserve 使用持久池上界，避免递归 push 时反复扩容
    // 只能从 root 递归收集，不能遍历整个 node pool
    CollectLeafNodesFrom(_rootA, leafNodes);
    CollectLeafNodesFrom(_rootB, leafNodes);
}

void ClassicRoamMeshBuilder::CollectLeafNodesFrom(ClassicRoamNode* node, std::vector<ClassicRoamNode*>& leafNodes) const
{
    if (node == nullptr)
    {
        return;
    }

    if (IsLeaf(node))
    {
        // inactive child 可能还留在 node pool 中，但不会从 root active 路径抵达
        leafNodes.push_back(node);
        return;
    }

    CollectLeafNodesFrom(node->LeftChild, leafNodes);
    CollectLeafNodesFrom(node->RightChild, leafNodes);
}

void ClassicRoamMeshBuilder::CollectActiveSplitPaths()
{
    // 每帧从 active topology 重新构造 split path 集合
    // merge 掉的旧路径不能继续影响下一帧 hysteresis
    _currentSplitPaths.clear();
    _stats.ActiveSplitCount = 0;
    CollectActiveSplitPathsFrom(_rootA);
    CollectActiveSplitPathsFrom(_rootB);
}

void ClassicRoamMeshBuilder::CollectActiveSplitPathsFrom(const ClassicRoamNode* node)
{
    if (node == nullptr || IsLeaf(node))
    {
        // leaf 没有 child，不属于 split path
        return;
    }

    _currentSplitPaths.insert(node->PathId);
    ++_stats.ActiveSplitCount;
    CollectActiveSplitPathsFrom(node->LeftChild);
    CollectActiveSplitPathsFrom(node->RightChild);
}

void ClassicRoamMeshBuilder::AccumulateLeafStats(
    const Terrain::TerrainMeshData& meshData,
    const std::vector<ClassicRoamNode*>& leafNodes)
{
    _stats.NodeCount = _nodes.size();
    _stats.ActiveTriangleCount = meshData.Indices.size() / 3U;
    // active depth 只按最终 leaf 计算，不受 inactive child 干扰
    _stats.MaxDepthReached = 0;

    for (const ClassicRoamNode* leaf : leafNodes)
    {
        // 只统计 active leaf，node pool 中保留的 inactive child 不参与当前帧数据
        _stats.MaxDepthReached = std::max(_stats.MaxDepthReached, leaf->Depth);
        switch (ClassifyLeafDebug(*leaf))
        {
        case LeafDebugClass::Original:
            ++_stats.OriginalTriangleCount;
            break;
        case LeafDebugClass::Subdivided:
            ++_stats.SubdividedTriangleCount;
            break;
        case LeafDebugClass::Rebuilt:
            ++_stats.RebuiltTriangleCount;
            break;
        }
    }
}

bool ClassicRoamMeshBuilder::IsLeaf(const ClassicRoamNode* node) const
{
    if (node == nullptr)
    {
        // nullptr 不能作为 active leaf 参与任何 pass
        return false;
    }

    return !node->IsSplit;
}
} // 命名空间 ParallelRoam::Algorithms::ClassicRoam
