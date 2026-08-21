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
    // 适配层通过只读引用读取最近一次更新的统计结果
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
    // CreatedBuildId 记录节点首次进入节点池时的更新序号
    node->CreatedBuildId = _buildSequence;
    node->ActivatedBuildId = _buildSequence;
    // 误差树已经累积整棵子树的误差，节点只需读取自身稳定下标对应的结果
    node->GeometricError = VarianceError(varianceTreeIndex, varianceIndex);
    // 创建节点时先更新一次最大深度，更新收尾时再按最终活动叶重新统计
    _stats.MaxDepthReached = std::max(_stats.MaxDepthReached, depth);

    // unique_ptr 池负责释放节点，节点之间仍使用 Classic ROAM 的裸指针表达拓扑
    ClassicRoamNode* nodePointer = node.get();
    _nodes.push_back(std::move(node));
    return nodePointer;
}

void ClassicRoamMeshBuilder::ResetTopology()
{
    // 仅允许 ResetTopology 清空节点池，普通帧更新必须保留跨帧拓扑
    _splitQueue.clear();
    _mergeQueue.clear();
    _nodes.clear();
    _previousSplitPaths.clear();
    _currentSplitPaths.clear();

    // rootA 和 rootB 分别覆盖地形正方形的一半
    // 两个根三角形共享对角底边，共同构成初始菱形
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

    // 两个根节点隔着共享对角线互为底边邻居
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
        // 首次构建尚无根菱形，需要初始化完整状态
        return true;
    }

    if (_heightMap != &heightMap)
    {
        // 更换高度图后，旧节点的几何误差和采样位置都不再有效
        return true;
    }

    if (settings.MaxDepth < _topologyMaxDepth)
    {
        // 降低最大深度时重建拓扑，确保过深的历史节点不会继续参与更新
        return true;
    }

    if (settings.TriangleBudget != _settings.TriangleBudget)
    {
        // 预算变化后从根节点重新分配，确保降低上限时本次更新立即满足数量限制
        return true;
    }

    // 世界尺寸或高度比例会同时影响屏幕误差和顶点位置
    // 参数变化后重建拓扑，避免旧分数和迟滞状态驱动错误决策
    return terrainSize != _terrainSize || heightScale != _heightScale;
}

void ClassicRoamMeshBuilder::CollectLeafNodes(std::vector<ClassicRoamNode*>& leafNodes) const
{
    // 当前可渲染的三角网格直接由活动叶集合生成
    leafNodes.clear();
    leafNodes.reserve(_nodes.size());
    // 按节点池上界预留空间，避免递归追加时反复扩容
    // 只从两个根沿活动路径收集，节点池中的历史节点不属于当前拓扑
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
        // 已停用的历史子节点仍在节点池中，但不会出现在从根开始的活动路径上
        leafNodes.push_back(node);
        return;
    }

    CollectLeafNodesFrom(node->LeftChild, leafNodes);
    CollectLeafNodesFrom(node->RightChild, leafNodes);
}

void ClassicRoamMeshBuilder::CollectActiveSplitPaths()
{
    // 每帧根据最终活动拓扑重建细分路径集合
    // 已合并的路径不会继续影响下一帧迟滞判断
    _currentSplitPaths.clear();
    _stats.ActiveSplitCount = 0;
    CollectActiveSplitPathsFrom(_rootA);
    CollectActiveSplitPathsFrom(_rootB);
}

void ClassicRoamMeshBuilder::CollectActiveSplitPathsFrom(const ClassicRoamNode* node)
{
    if (node == nullptr || IsLeaf(node))
    {
        // 叶节点没有活动子节点，因此不属于细分路径
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
    // 最大活动深度只按最终叶节点计算，不受节点池中历史子节点影响
    _stats.MaxDepthReached = 0;

    for (const ClassicRoamNode* leaf : leafNodes)
    {
        // 只统计活动叶节点，节点池中等待复用的历史子节点不计入本帧结果
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
        // 空指针不表示有效叶节点，不能参与任何算法阶段
        return false;
    }

    return !node->IsSplit;
}
} // 命名空间 ParallelRoam::Algorithms::ClassicRoam
