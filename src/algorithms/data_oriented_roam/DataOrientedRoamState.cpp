#include "algorithms/data_oriented_roam/DataOrientedRoamStateOps.h"
#include "algorithms/data_oriented_roam/DataOrientedRoamQueues.h"
#include "algorithms/data_oriented_roam/DataOrientedRoamScoring.h"
#include "algorithms/data_oriented_roam/DataOrientedRoamVariance.h"

#include <algorithm>
#include <limits>

namespace ParallelRoam::Algorithms::DataOrientedRoam
{
namespace
{
constexpr std::uint64_t RootAPathId = 1ULL;
constexpr std::uint64_t RootBPathId = 1ULL << 32U;
constexpr int ExactReserveMaxDepth = 20;
constexpr std::size_t LargeDepthReserveFallback = 1'000'000U;

int ChunkCoord(float value)
{
    // uv=1 必须落到最后一个 chunk，而不是越过网格边界
    const float clamped = std::clamp(value, 0.0F, 1.0F);
    const int coord = static_cast<int>(
        clamped * static_cast<float>(DataOrientedRoamTopologyChunkGridSize));
    return std::clamp(coord, 0, DataOrientedRoamTopologyChunkGridSize - 1);
}

DataOrientedRoamChunkId ChunkIdForUv(const glm::vec2& uv)
{
    // chunk id 使用 row-major 编码，便于作为 vector 下标
    const int x = ChunkCoord(uv.x);
    const int y = ChunkCoord(uv.y);
    // 这里不依赖 terrainSize，heightmap 切换才需要重建缓存
    return static_cast<DataOrientedRoamChunkId>(y * DataOrientedRoamTopologyChunkGridSize + x);
}

std::size_t ExactBintreeNodeCapacity(int maxDepth)
{
    // 两棵根树都可能展开成完整二叉树
    const int safeDepth = std::clamp(maxDepth, 0, ExactReserveMaxDepth);
    // safeDepth 已限制移位范围，避免大深度溢出
    const std::size_t nodesPerRoot = (std::size_t{1} << static_cast<unsigned int>(safeDepth + 1)) - 1U;
    return nodesPerRoot * 2U;
}

void CollectLeafNodesFrom(
    const DataOrientedRoamState& state,
    DataOrientedRoamNodeIndex node,
    std::vector<DataOrientedRoamNodeIndex>& leafNodes)
{
    if (!state.IsValidNode(node))
    {
        return;
    }

    if (state.IsLeaf(node))
    {
        // active leaf 才会进入 mesh 输出和统计流程
        leafNodes.push_back(node);
        return;
    }

    CollectLeafNodesFrom(state, state.Nodes[node].LeftChild, leafNodes);
    CollectLeafNodesFrom(state, state.Nodes[node].RightChild, leafNodes);
}

void CollectActiveSplitPathsFrom(DataOrientedRoamState& state, DataOrientedRoamNodeIndex node)
{
    if (!state.IsValidNode(node) || state.IsLeaf(node))
    {
        return;
    }

    state.CurrentSplitPaths.insert(state.Nodes.PathIdAt(node));
    ++state.Stats.ActiveSplitCount;
    CollectActiveSplitPathsFrom(state, state.Nodes.LeftChildAt(node));
    CollectActiveSplitPathsFrom(state, state.Nodes.RightChildAt(node));
}
} // 匿名命名空间

DataOrientedRoamNodeRef::operator DataOrientedRoamNodeConstRef() const
{
    return DataOrientedRoamNodeConstRef{
        Domain,
        Parent,
        LeftChild,
        RightChild,
        BaseNeighbor,
        LeftNeighbor,
        RightNeighbor,
        InteriorChunkId,
        GeometricError,
        ScreenError,
        VarianceIndex,
        PathId,
        CreatedBuildId,
        ActivatedBuildId,
        SplitBuildId,
        MergeBuildId,
        Depth,
        VarianceTreeIndex,
        ActivatedByForcedSplit,
        IsSplit,
    };
}

std::size_t DataOrientedRoamNodePool::capacity() const
{
    // 预分配容量用 domain 数组代表整个 node pool
    return Domains.capacity();
}

std::size_t DataOrientedRoamNodePool::storage_bytes() const
{
    // storage 估算按 capacity 计算，反映预分配后的内存占用
    return Domains.capacity() * sizeof(TriangleDomain) +
           // topology index arrays 是 split / merge 最频繁访问的连续字段
           Parents.capacity() * sizeof(DataOrientedRoamNodeIndex) +
           LeftChildren.capacity() * sizeof(DataOrientedRoamNodeIndex) +
           RightChildren.capacity() * sizeof(DataOrientedRoamNodeIndex) +
           BaseNeighbors.capacity() * sizeof(DataOrientedRoamNodeIndex) +
           LeftNeighbors.capacity() * sizeof(DataOrientedRoamNodeIndex) +
           RightNeighbors.capacity() * sizeof(DataOrientedRoamNodeIndex) +
           InteriorChunkIds.capacity() * sizeof(DataOrientedRoamChunkId) +
           // error arrays 与拓扑 index 分离，方便后续批量评估
           GeometricErrors.capacity() * sizeof(float) +
           ScreenErrors.capacity() * sizeof(float) +
           VarianceIndices.capacity() * sizeof(std::size_t) +
           // build id arrays 服务 debug 分类，不参与 split 队列热路径
           PathIds.capacity() * sizeof(std::uint64_t) +
           CreatedBuildIds.capacity() * sizeof(std::uint64_t) +
           ActivatedBuildIds.capacity() * sizeof(std::uint64_t) +
           SplitBuildIds.capacity() * sizeof(std::uint64_t) +
           MergeBuildIds.capacity() * sizeof(std::uint64_t) +
           // byte flags 独立存储，避免 vector<bool> bit proxy
           Depths.capacity() * sizeof(int) +
           VarianceTreeIndices.capacity() * sizeof(std::uint8_t) +
           ActivatedByForcedSplits.capacity() * sizeof(std::uint8_t) +
           IsSplits.capacity() * sizeof(std::uint8_t);
}

std::size_t DataOrientedRoamNodePool::array_count() const
{
    // array_count 用数组数量描述 SoA 字段拆分程度
    return 20U;
}

bool DataOrientedRoamNodePool::empty() const
{
    // 所有数组同步增删，因此只检查 Domains 即可
    return Domains.empty();
}

void DataOrientedRoamNodePool::clear()
{
    // clear 保留 capacity，后续 frame 可复用预分配内存
    Domains.clear();
    Parents.clear();
    LeftChildren.clear();
    RightChildren.clear();
    BaseNeighbors.clear();
    LeftNeighbors.clear();
    RightNeighbors.clear();
    InteriorChunkIds.clear();
    GeometricErrors.clear();
    ScreenErrors.clear();
    VarianceIndices.clear();
    PathIds.clear();
    CreatedBuildIds.clear();
    ActivatedBuildIds.clear();
    SplitBuildIds.clear();
    MergeBuildIds.clear();
    // 所有数组一起清空，避免同 index 指向错位字段
    Depths.clear();
    VarianceTreeIndices.clear();
    ActivatedByForcedSplits.clear();
    IsSplits.clear();
}

void DataOrientedRoamNodePool::reserve(std::size_t capacity)
{
    // topology index 数组必须和 domain 数组保持相同容量策略
    Domains.reserve(capacity);
    Parents.reserve(capacity);
    LeftChildren.reserve(capacity);
    RightChildren.reserve(capacity);
    BaseNeighbors.reserve(capacity);
    LeftNeighbors.reserve(capacity);
    RightNeighbors.reserve(capacity);
    InteriorChunkIds.reserve(capacity);
    // 几何误差和方差树索引与拓扑字段分离
    GeometricErrors.reserve(capacity);
    ScreenErrors.reserve(capacity);
    VarianceIndices.reserve(capacity);
    // build id 数组服务 debug 和本帧重建分类
    PathIds.reserve(capacity);
    CreatedBuildIds.reserve(capacity);
    ActivatedBuildIds.reserve(capacity);
    SplitBuildIds.reserve(capacity);
    MergeBuildIds.reserve(capacity);
    // depth 和 flag 分离，避免访问拓扑 index 时带入不需要的状态字节
    Depths.reserve(capacity);
    VarianceTreeIndices.reserve(capacity);
    ActivatedByForcedSplits.reserve(capacity);
    IsSplits.reserve(capacity);
}

DataOrientedRoamNodeIndex DataOrientedRoamNodePool::Add(
    const TriangleDomain& domain,
    DataOrientedRoamNodeIndex parent,
    int depth,
    std::uint64_t pathId,
    std::uint64_t buildSequence,
    float geometricError,
    std::uint8_t varianceTreeIndex,
    std::size_t varianceIndex)
{
    const auto index = static_cast<DataOrientedRoamNodeIndex>(Domains.size());
    // 每个 push 顺序必须完全一致，保证 index 能跨数组对齐
    Domains.push_back(domain);
    // parent 与 domain 同步写入，validator 才能遍历全池检查
    Parents.push_back(parent);
    // child 和 neighbor 默认无效，split / link pass 再填充
    LeftChildren.push_back(InvalidDataOrientedRoamNodeIndex);
    RightChildren.push_back(InvalidDataOrientedRoamNodeIndex);
    BaseNeighbors.push_back(InvalidDataOrientedRoamNodeIndex);
    LeftNeighbors.push_back(InvalidDataOrientedRoamNodeIndex);
    RightNeighbors.push_back(InvalidDataOrientedRoamNodeIndex);
    InteriorChunkIds.push_back(ComputeInteriorChunkId(domain));
    GeometricErrors.push_back(geometricError);
    ScreenErrors.push_back(0.0F);
    VarianceIndices.push_back(varianceIndex);
    // PathId 独立于 SoA 下标，用于跨帧 hysteresis
    PathIds.push_back(pathId);
    CreatedBuildIds.push_back(buildSequence);
    ActivatedBuildIds.push_back(buildSequence);
    // split / merge id 初始为 0，只有对应 pass 会写入
    SplitBuildIds.push_back(0);
    MergeBuildIds.push_back(0);
    Depths.push_back(depth);
    VarianceTreeIndices.push_back(varianceTreeIndex);
    // flag 使用 byte 数组，避免 vector<bool> 的代理语义干扰 pass 代码
    ActivatedByForcedSplits.push_back(0);
    IsSplits.push_back(0);
    return index;
}

DataOrientedRoamNodeRef DataOrientedRoamNodePool::operator[](DataOrientedRoamNodeIndex node)
{
    // 可写 proxy 只保存字段引用，不拷贝节点数据
    return DataOrientedRoamNodeRef{
        Domains[node],
        Parents[node],
        LeftChildren[node],
        RightChildren[node],
        BaseNeighbors[node],
        LeftNeighbors[node],
        RightNeighbors[node],
        InteriorChunkIds[node],
        GeometricErrors[node],
        ScreenErrors[node],
        VarianceIndices[node],
        PathIds[node],
        CreatedBuildIds[node],
        ActivatedBuildIds[node],
        SplitBuildIds[node],
        MergeBuildIds[node],
        Depths[node],
        VarianceTreeIndices[node],
        ActivatedByForcedSplits[node],
        IsSplits[node],
    };
}

DataOrientedRoamNodeConstRef DataOrientedRoamNodePool::operator[](DataOrientedRoamNodeIndex node) const
{
    // 只读 proxy 让 scoring / validation 不需要知道数组细节
    return DataOrientedRoamNodeConstRef{
        Domains[node],
        Parents[node],
        LeftChildren[node],
        RightChildren[node],
        BaseNeighbors[node],
        LeftNeighbors[node],
        RightNeighbors[node],
        InteriorChunkIds[node],
        GeometricErrors[node],
        ScreenErrors[node],
        VarianceIndices[node],
        PathIds[node],
        CreatedBuildIds[node],
        ActivatedBuildIds[node],
        SplitBuildIds[node],
        MergeBuildIds[node],
        Depths[node],
        VarianceTreeIndices[node],
        ActivatedByForcedSplits[node],
        IsSplits[node],
    };
}

std::uint64_t LeftChildPathId(std::uint64_t parentPathId)
{
    // path id 使用二叉堆编码
    // merge 后重新 split 可以复用上一帧 hysteresis 状态
    return parentPathId * 2ULL;
}

std::uint64_t RightChildPathId(std::uint64_t parentPathId)
{
    // right child 通过末位 1 和 left child 区分
    return parentPathId * 2ULL + 1ULL;
}

DataOrientedRoamChunkId ComputeInteriorChunkId(const TriangleDomain& domain)
{
    // 三个顶点完全落在同一格内才允许按 chunk 独占写拓扑
    const DataOrientedRoamChunkId chunkA = ChunkIdForUv(domain.A);
    const DataOrientedRoamChunkId chunkB = ChunkIdForUv(domain.B);
    const DataOrientedRoamChunkId chunkC = ChunkIdForUv(domain.C);
    // boundary triangle 可能写跨 chunk neighbor，必须回退串行路径
    if (chunkA == chunkB && chunkA == chunkC)
    {
        return chunkA;
    }

    return InvalidDataOrientedRoamChunkId;
}

DataOrientedRoamNodeIndex AddNode(
    DataOrientedRoamState& state,
    const TriangleDomain& domain,
    DataOrientedRoamNodeIndex parent,
    int depth,
    std::uint64_t pathId,
    std::uint8_t varianceTreeIndex,
    std::size_t varianceIndex)
{
    const float geometricError = VarianceError(state, varianceTreeIndex, varianceIndex);
    state.Stats.MaxDepthReached = std::max(state.Stats.MaxDepthReached, depth);

    // SoA 数组 index 是持久 node pool 的稳定节点引用
    const DataOrientedRoamNodeIndex node = state.Nodes.Add(
        domain,
        parent,
        depth,
        pathId,
        state.BuildSequence,
        geometricError,
        varianceTreeIndex,
        varianceIndex);
    // 新节点默认为 leaf，后续 split 才会加入 active internal 索引
    state.NodeMembership.emplace_back();
    state.IncrementalMesh.NodeSlots.push_back(InvalidDataOrientedRoamPosition);
    state.SplitQueueBlockedBuildIds.push_back(0U);
    return node;
}

void ReserveNodePool(DataOrientedRoamState& state)
{
    // 预分配降低扩容概率，但正确性不能依赖地址稳定
    // MaxDepth 现在是节点池容量估算的主要上界
    std::size_t targetCapacity = LargeDepthReserveFallback;
    if (state.Settings.MaxDepth <= ExactReserveMaxDepth)
    {
        // 完整 bintree 容量给出当前 maxDepth 的理论上界
        targetCapacity = ExactBintreeNodeCapacity(state.Settings.MaxDepth);
    }
    // 超过精确移位范围时使用固定 fallback，避免一次性巨大预分配

    // active budget 只约束初始有效工作集
    // 历史 inactive 节点之后可能超过该预算，但按索引引用在扩容后仍然有效
    const std::size_t budgetCapacity = state.Settings.TriangleBudget <=
            std::numeric_limits<std::size_t>::max() / 2U
        ? state.Settings.TriangleBudget * 2U
        : std::numeric_limits<std::size_t>::max();
    targetCapacity = std::min(targetCapacity, std::max<std::size_t>(budgetCapacity, 2U));

    if (state.Nodes.capacity() < targetCapacity)
    {
        // 取较大值可以减少 SoA 节点池扩容频率
        // 但所有 pass 仍必须通过 index 访问节点
        state.Nodes.reserve(targetCapacity);
    }

    state.NodeMembership.reserve(targetCapacity);
    state.IncrementalMesh.NodeSlots.reserve(targetCapacity);
    state.IncrementalMesh.SlotOwners.reserve(state.Settings.TriangleBudget);
    state.IncrementalMesh.SlotDirtyGenerations.reserve(state.Settings.TriangleBudget);
    state.IncrementalMesh.DirtySlots.reserve(state.Settings.TriangleBudget);
    state.ActiveInternalNodes.reserve(targetCapacity / 2U);
    state.ActiveLeafNodes.reserve(targetCapacity / 2U + 1U);
    state.SplitQueue.reserve(targetCapacity / 2U + 1U);
    state.SplitQueueBlockedBuildIds.reserve(targetCapacity);
    state.MergeQueue.reserve(targetCapacity / 2U);
}

void ResetTopology(DataOrientedRoamState& state)
{
    // ResetTopology 是唯一清空 node pool 的入口
    // 普通相机移动保留历史节点和 split path
    state.Nodes.clear();
    state.PreviousSplitPaths.clear();
    state.CurrentSplitPaths.clear();
    state.ActiveInternalNodes.clear();
    state.ActiveLeafNodes.clear();
    state.NodeMembership.clear();
    state.IncrementalMesh.NodeSlots.clear();
    state.SplitQueue.clear();
    state.SplitQueueBlockedBuildIds.clear();
    state.MergeQueue.clear();

    state.RootA = AddNode(
        state,
        TriangleDomain{glm::vec2{0.0F, 1.0F}, glm::vec2{1.0F, 0.0F}, glm::vec2{0.0F, 0.0F}},
        InvalidDataOrientedRoamNodeIndex,
        0,
        RootAPathId,
        0U,
        0U);
    state.RootB = AddNode(
        state,
        TriangleDomain{glm::vec2{1.0F, 0.0F}, glm::vec2{0.0F, 1.0F}, glm::vec2{1.0F, 1.0F}},
        InvalidDataOrientedRoamNodeIndex,
        0,
        RootBPathId,
        1U,
        0U);

    // 两个 root 的 path id 位于不同区间，避免 hysteresis 键碰撞
    // 两个根三角形跨共享对角线互为 base neighbor
    // 这是 Classic ROAM 根 diamond 的起点
    state.Nodes[state.RootA].BaseNeighbor = state.RootB;
    state.Nodes[state.RootB].BaseNeighbor = state.RootA;
    state.ActiveLeafNodes.push_back(state.RootA);
    state.NodeMembership[state.RootA].ActiveLeafPosition = 0U;
    state.ActiveLeafNodes.push_back(state.RootB);
    state.NodeMembership[state.RootB].ActiveLeafPosition = 1U;
    InitializePersistentSplitQueue(state);
    InitializePersistentMergeQueue(state);
    state.TopologyMaxDepth = state.Settings.MaxDepth;
}

bool NeedsTopologyReset(
    const DataOrientedRoamState& state,
    const Terrain::HeightMap& heightMap,
    float terrainSize,
    float heightScale,
    const DataOrientedRoamSettings& settings)
{
    if (!state.IsValidNode(state.RootA) || !state.IsValidNode(state.RootB) || state.Nodes.empty())
    {
        // 首帧没有 root diamond，必须初始化拓扑
        return true;
    }

    if (state.HeightMap != &heightMap)
    {
        // HeightMap 指针变化意味着所有 geometric error 缓存失效
        return true;
    }

    if (settings.MaxDepth < state.TopologyMaxDepth)
    {
        // 降低深度时历史节点可能超过新上限
        return true;
    }

    if (settings.TriangleBudget != state.Settings.TriangleBudget)
    {
        // 预算变化后从根重新分配，保证降低上限立即生效
        return true;
    }

    return terrainSize != state.TerrainSize || heightScale != state.HeightScale;
}

void CollectLeafNodes(const DataOrientedRoamState& state, std::vector<DataOrientedRoamNodeIndex>& leafNodes)
{
    // 只能从 root 递归收集 active topology
    // node pool 中的 inactive child 不属于当前 mesh
    leafNodes.clear();
    leafNodes.reserve(state.Nodes.size());
    CollectLeafNodesFrom(state, state.RootA, leafNodes);
    CollectLeafNodesFrom(state, state.RootB, leafNodes);
}

void CollectActiveSplitPaths(DataOrientedRoamState& state)
{
    // active split path 反映 merge 后的当前拓扑
    // hysteresis 下一帧只沿这些 path 复用 split 状态
    state.CurrentSplitPaths.clear();
    state.Stats.ActiveSplitCount = 0;
    // 只从 root 走 active topology，merge 掉的路径会自然消失
    CollectActiveSplitPathsFrom(state, state.RootA);
    CollectActiveSplitPathsFrom(state, state.RootB);
}

void AccumulateLeafStats(
    DataOrientedRoamState& state,
    const std::vector<DataOrientedRoamNodeIndex>& leafNodes)
{
    state.Stats.NodeCount = state.Nodes.size();
    state.Stats.ReservedNodeCapacity = state.Nodes.capacity();
    state.Stats.NodeStorageBytes = state.Nodes.storage_bytes();
    state.Stats.NodeStorageArrayCount = state.Nodes.array_count();
    state.Stats.ActiveTriangleCount = leafNodes.size();

    state.Stats.MaxDepthReached = 0;
    // leafNodes 来自最终快照，避免统计环节再递归扫描 active topology
    for (DataOrientedRoamNodeIndex leafIndex : leafNodes)
    {
        // inactive child 仍留在 node pool 中但不参与当前帧统计
        state.Stats.MaxDepthReached = std::max(
            state.Stats.MaxDepthReached,
            state.Nodes.DepthAt(leafIndex));
        switch (ClassifyLeafDebug(state, leafIndex))
        {
        case DataOrientedRoamLeafDebugClass::Original:
            ++state.Stats.OriginalTriangleCount;
            break;
        case DataOrientedRoamLeafDebugClass::Subdivided:
            ++state.Stats.SubdividedTriangleCount;
            break;
        case DataOrientedRoamLeafDebugClass::Rebuilt:
            ++state.Stats.RebuiltTriangleCount;
            break;
        }
    }
}
} // 命名空间 ParallelRoam::Algorithms::DataOrientedRoam
