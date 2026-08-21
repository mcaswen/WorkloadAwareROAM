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
    // UV 等于 1 时必须归入最后一个分块，不能计算到网格范围之外
    const float clamped = std::clamp(value, 0.0F, 1.0F);
    const int coord = static_cast<int>(
        clamped * static_cast<float>(DataOrientedRoamTopologyChunkGridSize));
    return std::clamp(coord, 0, DataOrientedRoamTopologyChunkGridSize - 1);
}

DataOrientedRoamChunkId ChunkIdForUv(const glm::vec2& uv)
{
    // 分块编号按行优先编码，可以直接作为数组下标
    const int x = ChunkCoord(uv.x);
    const int y = ChunkCoord(uv.y);
    // 分块只由归一化 UV 决定，不受地形世界尺寸影响
    return static_cast<DataOrientedRoamChunkId>(y * DataOrientedRoamTopologyChunkGridSize + x);
}

std::size_t ExactBintreeNodeCapacity(int maxDepth)
{
    // 两个根节点都可能各自展开成完整二叉树
    const int safeDepth = std::clamp(maxDepth, 0, ExactReserveMaxDepth);
    // safeDepth 已限制移位范围，避免计算节点数量时溢出
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
        // 只有从根可达的活动叶节点才进入网格输出和统计
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
    // 所有 SoA 数组容量保持一致，因此用 Domains 代表整个节点池容量
    return Domains.capacity();
}

std::size_t DataOrientedRoamNodePool::storage_bytes() const
{
    // 按容量而非当前大小估算，才能反映预分配后实际保留的内存
    return Domains.capacity() * sizeof(TriangleDomain) +
           // 拓扑下标数组是细分和合并阶段最常访问的数据
           Parents.capacity() * sizeof(DataOrientedRoamNodeIndex) +
           LeftChildren.capacity() * sizeof(DataOrientedRoamNodeIndex) +
           RightChildren.capacity() * sizeof(DataOrientedRoamNodeIndex) +
           BaseNeighbors.capacity() * sizeof(DataOrientedRoamNodeIndex) +
           LeftNeighbors.capacity() * sizeof(DataOrientedRoamNodeIndex) +
           RightNeighbors.capacity() * sizeof(DataOrientedRoamNodeIndex) +
           InteriorChunkIds.capacity() * sizeof(DataOrientedRoamChunkId) +
           // 误差数组与拓扑下标分开，便于连续批量评分
           GeometricErrors.capacity() * sizeof(float) +
           ScreenErrors.capacity() * sizeof(float) +
           VarianceIndices.capacity() * sizeof(std::size_t) +
           // 更新序号只用于调试分类，细分队列的正常处理不会读取它
           PathIds.capacity() * sizeof(std::uint64_t) +
           CreatedBuildIds.capacity() * sizeof(std::uint64_t) +
           ActivatedBuildIds.capacity() * sizeof(std::uint64_t) +
           SplitBuildIds.capacity() * sizeof(std::uint64_t) +
           MergeBuildIds.capacity() * sizeof(std::uint64_t) +
           // 字节标志独立存储，避免位压缩布尔数组必须通过临时对象读写
           Depths.capacity() * sizeof(int) +
           VarianceTreeIndices.capacity() * sizeof(std::uint8_t) +
           ActivatedByForcedSplits.capacity() * sizeof(std::uint8_t) +
           IsSplits.capacity() * sizeof(std::uint8_t);
}

std::size_t DataOrientedRoamNodePool::array_count() const
{
    // 数组数量用于报告 SoA 将节点拆成多少个连续字段
    return 20U;
}

bool DataOrientedRoamNodePool::empty() const
{
    // 所有数组始终同步增删，检查 Domains 即可判断节点池是否为空
    return Domains.empty();
}

void DataOrientedRoamNodePool::clear()
{
    // 清空操作保留已分配容量，后续更新可以直接复用
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
    // 所有数组必须一起清空，保证相同下标始终表示同一节点
    Depths.clear();
    VarianceTreeIndices.clear();
    ActivatedByForcedSplits.clear();
    IsSplits.clear();
}

void DataOrientedRoamNodePool::reserve(std::size_t capacity)
{
    // 拓扑下标数组与区域数组使用相同的容量策略
    Domains.reserve(capacity);
    Parents.reserve(capacity);
    LeftChildren.reserve(capacity);
    RightChildren.reserve(capacity);
    BaseNeighbors.reserve(capacity);
    LeftNeighbors.reserve(capacity);
    RightNeighbors.reserve(capacity);
    InteriorChunkIds.reserve(capacity);
    // 几何误差和误差树下标单独连续存储，便于批量读取
    GeometricErrors.reserve(capacity);
    ScreenErrors.reserve(capacity);
    VarianceIndices.reserve(capacity);
    // 更新序号用于调试显示和本帧重建分类
    PathIds.reserve(capacity);
    CreatedBuildIds.reserve(capacity);
    ActivatedBuildIds.reserve(capacity);
    SplitBuildIds.reserve(capacity);
    MergeBuildIds.reserve(capacity);
    // 深度和标志单独存储，访问拓扑下标时无需加载无关状态
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
    // 每个数组必须按相同顺序追加，保证同一下标始终对齐
    Domains.push_back(domain);
    // 父节点与区域同步写入，使验证器可以遍历整个节点池检查关系
    Parents.push_back(parent);
    // 子节点和邻居初始为空，细分和邻接连接时再填写
    LeftChildren.push_back(InvalidDataOrientedRoamNodeIndex);
    RightChildren.push_back(InvalidDataOrientedRoamNodeIndex);
    BaseNeighbors.push_back(InvalidDataOrientedRoamNodeIndex);
    LeftNeighbors.push_back(InvalidDataOrientedRoamNodeIndex);
    RightNeighbors.push_back(InvalidDataOrientedRoamNodeIndex);
    InteriorChunkIds.push_back(ComputeInteriorChunkId(domain));
    GeometricErrors.push_back(geometricError);
    ScreenErrors.push_back(0.0F);
    VarianceIndices.push_back(varianceIndex);
    // PathId 独立于节点池下标，用于跨帧迟滞判断
    PathIds.push_back(pathId);
    CreatedBuildIds.push_back(buildSequence);
    ActivatedBuildIds.push_back(buildSequence);
    // 细分与合并序号初始为零，仅在相应拓扑阶段成功提交后写入
    SplitBuildIds.push_back(0);
    MergeBuildIds.push_back(0);
    Depths.push_back(depth);
    VarianceTreeIndices.push_back(varianceTreeIndex);
    // 标志使用字节数组，避免位压缩布尔数组必须通过临时对象读写
    ActivatedByForcedSplits.push_back(0);
    IsSplits.push_back(0);
    return index;
}

DataOrientedRoamNodeRef DataOrientedRoamNodePool::operator[](DataOrientedRoamNodeIndex node)
{
    // 可写节点视图只保存字段引用，不复制节点数据
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
    // 只读节点视图让评分和验证代码无需了解 SoA 数组细节
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
    // 路径编号使用二叉堆编码，合并后再次细分仍能对应同一稳定位置
    return parentPathId * 2ULL;
}

std::uint64_t RightChildPathId(std::uint64_t parentPathId)
{
    // 右子节点用末位 1 与左子节点区分
    return parentPathId * 2ULL + 1ULL;
}

DataOrientedRoamChunkId ComputeInteriorChunkId(const TriangleDomain& domain)
{
    // 三个顶点全部落在同一分块时，负责该分块的线程才不会与其他线程修改同一组节点
    const DataOrientedRoamChunkId chunkA = ChunkIdForUv(domain.A);
    const DataOrientedRoamChunkId chunkB = ChunkIdForUv(domain.B);
    const DataOrientedRoamChunkId chunkC = ChunkIdForUv(domain.C);
    // 跨分块三角形可能修改其他任务的邻居，因此必须交给主线程顺序处理
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

    // 节点在 SoA 数组中的下标不会变化，因此拓扑关系可以长期保存这些下标
    const DataOrientedRoamNodeIndex node = state.Nodes.Add(
        domain,
        parent,
        depth,
        pathId,
        state.BuildSequence,
        geometricError,
        varianceTreeIndex,
        varianceIndex);
    // 新节点默认是活动叶节点，只有再次细分后才加入活动内部节点索引
    state.NodeMembership.emplace_back();
    state.IncrementalMesh.NodeSlots.push_back(InvalidDataOrientedRoamPosition);
    state.SplitQueueBlockedBuildIds.push_back(0U);
    return node;
}

void ReserveNodePool(DataOrientedRoamState& state)
{
    // 预分配用于降低扩容次数，算法正确性仍只能依赖稳定下标
    // MaxDepth 提供节点池理论容量的主要上界
    std::size_t targetCapacity = LargeDepthReserveFallback;
    if (state.Settings.MaxDepth <= ExactReserveMaxDepth)
    {
        // 完整二叉树容量给出当前最大深度下的节点数量上界
        targetCapacity = ExactBintreeNodeCapacity(state.Settings.MaxDepth);
    }
    // 深度超出安全移位范围时只预留固定容量，避免一次性申请过大内存

    // 活动预算只限制当前可渲染叶节点，历史停用节点可能使节点池大于该值
    // 节点通过下标引用，因此数组扩容不会破坏拓扑关系
    const std::size_t budgetCapacity = state.Settings.TriangleBudget <=
            std::numeric_limits<std::size_t>::max() / 2U
        ? state.Settings.TriangleBudget * 2U
        : std::numeric_limits<std::size_t>::max();
    targetCapacity = std::min(targetCapacity, std::max<std::size_t>(budgetCapacity, 2U));

    if (state.Nodes.capacity() < targetCapacity)
    {
        // 取理论容量和预算容量中的较大值可以减少节点池扩容
        // 所有算法阶段仍必须通过下标访问节点
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
    // 只有 ResetTopology 会清空节点池，普通相机移动继续复用历史节点和上一帧保留的细分路径
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

    // 两个根节点的路径编号位于不同区间，避免迟滞判断把它们当成同一位置
    // 两个根三角形隔着共享对角线互为底边邻居，构成初始菱形
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
        // 首次更新尚无根菱形，需要初始化完整拓扑
        return true;
    }

    if (state.HeightMap != &heightMap)
    {
        // 更换高度图后，全部节点的几何误差缓存都不再有效
        return true;
    }

    if (settings.MaxDepth < state.TopologyMaxDepth)
    {
        // 降低最大深度后，历史节点可能超过新的允许层级
        return true;
    }

    if (settings.TriangleBudget != state.Settings.TriangleBudget)
    {
        // 预算变化后从根节点重新分配，确保降低上限时立即满足数量限制
        return true;
    }

    return terrainSize != state.TerrainSize || heightScale != state.HeightScale;
}

void CollectLeafNodes(const DataOrientedRoamState& state, std::vector<DataOrientedRoamNodeIndex>& leafNodes)
{
    // 只从两个根节点沿活动路径收集，节点池中的历史子节点不属于当前网格
    leafNodes.clear();
    leafNodes.reserve(state.Nodes.size());
    CollectLeafNodesFrom(state, state.RootA, leafNodes);
    CollectLeafNodesFrom(state, state.RootB, leafNodes);
}

void CollectActiveSplitPaths(DataOrientedRoamState& state)
{
    // 活动细分路径来自合并和细分后的最终拓扑，下一帧迟滞判断只复用这些路径
    state.CurrentSplitPaths.clear();
    state.Stats.ActiveSplitCount = 0;
    // 从根节点沿活动拓扑遍历，已经合并的旧路径会自然消失
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
    // leafNodes 已是最终活动叶集合，统计阶段无需再次递归遍历拓扑
    for (DataOrientedRoamNodeIndex leafIndex : leafNodes)
    {
        // 停用的历史子节点仍保留在节点池中，但不计入本帧结果
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
