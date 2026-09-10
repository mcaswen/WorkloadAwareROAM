#pragma once

#include "algorithms/data_oriented_roam/DataOrientedRoamTypes.h"
#include "algorithms/data_oriented_roam/DataOrientedRoamMeshState.h"

#include <array>
#include <atomic>
#include <cstdint>
#include <limits>
#include <unordered_set>
#include <vector>

namespace ParallelRoam::Algorithms::DataOrientedRoam
{
class DataOrientedRoamThreadPool;

// 分块编号决定节点能否由某一个线程独立修改而不与其他线程冲突
using DataOrientedRoamChunkId = std::uint32_t;
constexpr DataOrientedRoamChunkId InvalidDataOrientedRoamChunkId =
    std::numeric_limits<DataOrientedRoamChunkId>::max();
// 固定分块网格，避免把内部调度细节暴露为界面参数
constexpr int DataOrientedRoamTopologyChunkGridSize = 8;

/// <summary>
/// 区分误差驱动的普通细分和为保持邻接兼容而执行的强制细分
/// </summary>
enum class DataOrientedRoamSplitReason
{
    Requested,
    ForcedByBaseNeighbor,
};

/// <summary>
/// 根据活动叶节点是在本次更新前保留、刚创建还是刚恢复来划分调试类别
/// </summary>
enum class DataOrientedRoamLeafDebugClass
{
    Original,
    Subdivided,
    Rebuilt,
};

/// <summary>
/// 从跨帧保留的细分队列复制出的候选，供本次任务划分和线程处理使用
/// </summary>
struct DataOrientedRoamSplitCandidate
{
    // Sequence 保留快照遍历序号；提交优先级由分数和稳定路径编号决定
    float Score{0.0F};
    std::uint64_t Sequence{0};
    DataOrientedRoamNodeIndex Node{InvalidDataOrientedRoamNodeIndex};
};

/// <summary>
/// 跨帧保留的细分堆条目，将节点和分数放在一起以便比较时连续读取
/// </summary>
struct DataOrientedRoamSplitQueueEntry
{
    float Score{0.0F};
    DataOrientedRoamNodeIndex Node{InvalidDataOrientedRoamNodeIndex};
};

/// <summary>
/// 从合并队列复制出的候选，复制过程不会修改节点关系
/// </summary>
struct DataOrientedRoamMergeCandidate
{
    // 低误差菱形优先合并，以尽量减少画质损失
    float Score{0.0F};
    DataOrientedRoamNodeIndex Node{InvalidDataOrientedRoamNodeIndex};
};

struct DataOrientedRoamMergeQueueEntry
{
    float Score{0.0F};
    DataOrientedRoamNodeIndex Node{InvalidDataOrientedRoamNodeIndex};
};

/// <summary>
/// 集中保存 DOD 节点在活动列表和跨帧保留队列中的成员信息
/// 节点池仍保持 SoA 布局，这里只集中保存按节点查找成员位置和菱形关系所需的数据
/// </summary>
struct DataOrientedRoamNodeMembership
{
    DataOrientedRoamPosition ActiveInternalPosition{InvalidDataOrientedRoamPosition};
    DataOrientedRoamPosition ActiveLeafPosition{InvalidDataOrientedRoamPosition};
    DataOrientedRoamPosition SplitQueuePosition{InvalidDataOrientedRoamPosition};
    DataOrientedRoamPosition MergeQueuePosition{InvalidDataOrientedRoamPosition};
    DataOrientedRoamNodeIndex MergeQueueRepresentative{InvalidDataOrientedRoamNodeIndex};
    DataOrientedRoamNodeIndex MergeQueuePartner{InvalidDataOrientedRoamNodeIndex};
};
static_assert(sizeof(DataOrientedRoamNodeMembership) == 24U);

struct DataOrientedRoamMergeCandidateEvaluation
{
    // NodeScore 保留单侧候选排序，PairScore 用于预算交换时衡量整个菱形的画质损失
    bool Eligible{false};
    float NodeScore{0.0F};
    float PairScore{0.0F};
};

/// <summary>
/// 将 SoA 节点池同一下标的字段组合成只读节点视图
/// </summary>
struct DataOrientedRoamNodeConstRef
{
    const TriangleDomain& Domain;
    const DataOrientedRoamNodeIndex& Parent;
    const DataOrientedRoamNodeIndex& LeftChild;
    const DataOrientedRoamNodeIndex& RightChild;
    const DataOrientedRoamNodeIndex& BaseNeighbor;
    const DataOrientedRoamNodeIndex& LeftNeighbor;
    const DataOrientedRoamNodeIndex& RightNeighbor;
    const DataOrientedRoamChunkId& InteriorChunkId;
    const float& GeometricError;
    const float& ScreenError;
    const std::size_t& VarianceIndex;
    const std::uint64_t& PathId;
    const std::uint64_t& CreatedBuildId;
    const std::uint64_t& ActivatedBuildId;
    const std::uint64_t& SplitBuildId;
    const std::uint64_t& MergeBuildId;
    const int& Depth;
    const std::uint8_t& VarianceTreeIndex;
    const std::uint8_t& ActivatedByForcedSplit;
    const std::uint8_t& IsSplit;
};

/// <summary>
/// 将 SoA 节点池同一下标的字段组合成可写节点视图，便于拓扑代码按节点访问
/// </summary>
struct DataOrientedRoamNodeRef
{
    TriangleDomain& Domain;
    DataOrientedRoamNodeIndex& Parent;
    DataOrientedRoamNodeIndex& LeftChild;
    DataOrientedRoamNodeIndex& RightChild;
    DataOrientedRoamNodeIndex& BaseNeighbor;
    DataOrientedRoamNodeIndex& LeftNeighbor;
    DataOrientedRoamNodeIndex& RightNeighbor;
    DataOrientedRoamChunkId& InteriorChunkId;
    float& GeometricError;
    float& ScreenError;
    std::size_t& VarianceIndex;
    std::uint64_t& PathId;
    std::uint64_t& CreatedBuildId;
    std::uint64_t& ActivatedBuildId;
    std::uint64_t& SplitBuildId;
    std::uint64_t& MergeBuildId;
    int& Depth;
    std::uint8_t& VarianceTreeIndex;
    std::uint8_t& ActivatedByForcedSplit;
    std::uint8_t& IsSplit;

    [[nodiscard]] operator DataOrientedRoamNodeConstRef() const;
};

/// <summary>
/// 使用独立连续数组保存 DOD ROAM 节点的拓扑、误差、深度和标志
/// 这些数组都由 DataOrientedRoamState 管理，ResetTopology 负责清空，AddNode 以相同下标追加所有字段
/// 合并后仍保留子节点下标，便于以后再次细分时复用已有节点
/// </summary>
struct DataOrientedRoamNodePool
{
    // 节点只保存 UV 区域，评分和网格提交时再按需换算世界坐标
    std::vector<TriangleDomain> Domains;
    std::vector<DataOrientedRoamNodeIndex> Parents;

    // 合并后保留子节点下标，使后续细分可以复用节点和静态几何误差
    std::vector<DataOrientedRoamNodeIndex> LeftChildren;
    std::vector<DataOrientedRoamNodeIndex> RightChildren;

    // 三个邻居分别对应底边、左边和右边
    std::vector<DataOrientedRoamNodeIndex> BaseNeighbors;
    std::vector<DataOrientedRoamNodeIndex> LeftNeighbors;
    std::vector<DataOrientedRoamNodeIndex> RightNeighbors;

    // InteriorChunkIds 记录整个三角形所在的分块，避免拓扑阶段反复根据 UV 计算
    std::vector<DataOrientedRoamChunkId> InteriorChunkIds;

    // GeometricErrors 保存保守几何误差，与相机无关并可跨帧复用
    std::vector<float> GeometricErrors;
    // 只有需要把节点状态复制给 GPU 时才同步 ScreenErrors，DOD 的 Q_s 分数由 SplitQueue 单独保存
    std::vector<float> ScreenErrors;
    std::vector<std::size_t> VarianceIndices;

    // PathIds 始终对应固定的拓扑位置，供跨帧迟滞判断使用，不能用节点池数组下标代替
    std::vector<std::uint64_t> PathIds;

    // 更新序号让调试显示能够区分新建、重新激活和刚合并的节点
    std::vector<std::uint64_t> CreatedBuildIds;
    std::vector<std::uint64_t> ActivatedBuildIds;
    std::vector<std::uint64_t> SplitBuildIds;
    std::vector<std::uint64_t> MergeBuildIds;
    std::vector<int> Depths;
    std::vector<std::uint8_t> VarianceTreeIndices;

    // 标志位单独连续保存，避免访问拓扑下标或误差时带入无关字节
    std::vector<std::uint8_t> ActivatedByForcedSplits;
    std::vector<std::uint8_t> IsSplits;

    [[nodiscard]] std::size_t size() const noexcept
    {
        return Domains.size();
    }

    // 只需要一个字段时直接读取对应数组，避免额外组合包含全部字段的节点视图
    // 除可选的分数副本外，这些接口都只提供读取功能，写入集中在专用拓扑函数中
    // 与 operator[] 相同，调用方负责保证下标有效
    [[nodiscard]] const TriangleDomain& DomainAt(DataOrientedRoamNodeIndex node) const noexcept
    {
        return Domains[node];
    }

    [[nodiscard]] DataOrientedRoamNodeIndex ParentAt(DataOrientedRoamNodeIndex node) const noexcept
    {
        return Parents[node];
    }

    [[nodiscard]] DataOrientedRoamNodeIndex LeftChildAt(DataOrientedRoamNodeIndex node) const noexcept
    {
        return LeftChildren[node];
    }

    [[nodiscard]] DataOrientedRoamNodeIndex RightChildAt(DataOrientedRoamNodeIndex node) const noexcept
    {
        return RightChildren[node];
    }

    [[nodiscard]] DataOrientedRoamNodeIndex BaseNeighborAt(DataOrientedRoamNodeIndex node) const noexcept
    {
        return BaseNeighbors[node];
    }

    [[nodiscard]] DataOrientedRoamNodeIndex LeftNeighborAt(DataOrientedRoamNodeIndex node) const noexcept
    {
        return LeftNeighbors[node];
    }

    [[nodiscard]] DataOrientedRoamNodeIndex RightNeighborAt(DataOrientedRoamNodeIndex node) const noexcept
    {
        return RightNeighbors[node];
    }

    [[nodiscard]] DataOrientedRoamChunkId InteriorChunkIdAt(DataOrientedRoamNodeIndex node) const noexcept
    {
        return InteriorChunkIds[node];
    }

    [[nodiscard]] float GeometricErrorAt(DataOrientedRoamNodeIndex node) const noexcept
    {
        return GeometricErrors[node];
    }

    [[nodiscard]] float ScreenErrorAt(DataOrientedRoamNodeIndex node) const noexcept
    {
        return ScreenErrors[node];
    }

    [[nodiscard]] float& ScreenErrorAt(DataOrientedRoamNodeIndex node) noexcept
    {
        return ScreenErrors[node];
    }

    [[nodiscard]] std::uint64_t PathIdAt(DataOrientedRoamNodeIndex node) const noexcept
    {
        return PathIds[node];
    }

    [[nodiscard]] std::uint64_t SplitBuildIdAt(DataOrientedRoamNodeIndex node) const noexcept
    {
        return SplitBuildIds[node];
    }

    [[nodiscard]] std::uint64_t MergeBuildIdAt(DataOrientedRoamNodeIndex node) const noexcept
    {
        return MergeBuildIds[node];
    }

    [[nodiscard]] std::uint64_t ActivatedBuildIdAt(DataOrientedRoamNodeIndex node) const noexcept
    {
        return ActivatedBuildIds[node];
    }

    [[nodiscard]] int DepthAt(DataOrientedRoamNodeIndex node) const noexcept
    {
        return Depths[node];
    }

    [[nodiscard]] std::size_t VarianceIndexAt(DataOrientedRoamNodeIndex node) const noexcept
    {
        return VarianceIndices[node];
    }

    [[nodiscard]] std::uint8_t VarianceTreeIndexAt(DataOrientedRoamNodeIndex node) const noexcept
    {
        return VarianceTreeIndices[node];
    }

    [[nodiscard]] bool IsSplitAt(DataOrientedRoamNodeIndex node) const noexcept
    {
        return IsSplits[node] != 0U;
    }

    [[nodiscard]] bool ActivatedByForcedSplitAt(DataOrientedRoamNodeIndex node) const noexcept
    {
        return ActivatedByForcedSplits[node] != 0U;
    }

    [[nodiscard]] std::size_t capacity() const;
    [[nodiscard]] std::size_t storage_bytes() const;
    [[nodiscard]] std::size_t array_count() const;
    [[nodiscard]] bool empty() const;

    void clear();
    void reserve(std::size_t capacity);

    [[nodiscard]] DataOrientedRoamNodeIndex Add(
        const TriangleDomain& domain,
        DataOrientedRoamNodeIndex parent,
        int depth,
        std::uint64_t pathId,
        std::uint64_t buildSequence,
        float geometricError,
        std::uint8_t varianceTreeIndex,
        std::size_t varianceIndex);

    // 节点视图让算法代码按节点表达逻辑，底层数据仍保持 SoA 布局
    [[nodiscard]] DataOrientedRoamNodeRef operator[](DataOrientedRoamNodeIndex node);
    [[nodiscard]] DataOrientedRoamNodeConstRef operator[](DataOrientedRoamNodeIndex node) const;
};

/// <summary>
/// 汇总 DOD ROAM 跨帧保留的拓扑、队列、网格和本次更新的临时数据
/// DataOrientedRoamPipeline 创建并管理该状态，各算法阶段通过它交换结果
/// HeightMap 和 ThreadPool 只在更新期间临时引用，状态对象不负责释放
/// </summary>
struct DataOrientedRoamState
{
    DataOrientedRoamState() = default;

    /// <summary>
    /// 深拷贝拓扑和派生索引，供冻结输入配对回归使用
    /// 高度图和线程池仍是只读借用，不转移资源所有权
    /// </summary>
    DataOrientedRoamState(const DataOrientedRoamState& source);
    DataOrientedRoamState& operator=(const DataOrientedRoamState&) = delete;

    // HeightMap 和 ThreadPool 只在当前更新期间有效
    const Terrain::HeightMap* HeightMap{nullptr};
    DataOrientedRoamSettings Settings;
    DataOrientedRoamStats Stats;
    DataOrientedRoamNodePool Nodes;

    // 两棵误差树分别覆盖一个根三角形，预计算深度可以超过运行时 MaxDepth
    std::array<std::vector<float>, 2> VarianceTrees;
    const Terrain::HeightMap* VarianceHeightMap{nullptr};
    int VarianceTreeMaxDepth{-1};

    // 每次更新结束时交换两组路径编号，记录哪些位置仍处于细分状态，供下一帧迟滞判断
    std::unordered_set<std::uint64_t> PreviousSplitPaths;
    std::unordered_set<std::uint64_t> CurrentSplitPaths;

    // 连续保存当前活动内部节点，避免合并阶段扫描整个历史节点池
    std::vector<DataOrientedRoamNodeIndex> ActiveInternalNodes;
    // 活动叶节点稠密数组只随拓扑变化，评分和建堆不会改变其顺序
    std::vector<DataOrientedRoamNodeIndex> ActiveLeafNodes;
    // 每个节点的活动列表和队列成员信息集中存储，避免分散到多个辅助数组中
    std::vector<DataOrientedRoamNodeMembership> NodeMembership;

    // CPU 适配层直接引用这份长期保留的网格，仅输出拓扑时不会记录网格修改
    DataOrientedRoamIncrementalMesh IncrementalMesh;

    // 跨帧保留的 Q_s 连续保存节点和分数，反向位置支持强制细分按节点删除成员
    std::vector<DataOrientedRoamSplitQueueEntry> SplitQueue;
    std::vector<std::uint64_t> SplitQueueBlockedBuildIds;

    // 每个可合并菱形在 Q_m 中只保存一个固定代表节点
    std::vector<DataOrientedRoamMergeQueueEntry> MergeQueue;

    // RootA 和 RootB 覆盖整个地形并构成初始菱形
    DataOrientedRoamNodeIndex RootA{InvalidDataOrientedRoamNodeIndex};
    DataOrientedRoamNodeIndex RootB{InvalidDataOrientedRoamNodeIndex};

    glm::mat4 ViewProjection{1.0F};
    std::array<glm::vec4, 6> FrustumPlanes{};
    std::uint32_t DrawableWidth{1U};
    std::uint32_t DrawableHeight{1U};
    // 串行拓扑阶段使用普通计数，避免每次修改都执行原子读写
    std::size_t RemainingSerialSplitBudget{0U};
    // 多个线程通过原子计数共享剩余名额，确保活动三角形不超过数量上限
    std::atomic<std::size_t> RemainingParallelSplitBudget{0U};
    float TerrainSize{1.0F};
    float HeightScale{1.0F};
    int TopologyMaxDepth{0};
    std::uint64_t BuildSequence{0};
    DataOrientedRoamThreadPool* ThreadPool{nullptr};

    [[nodiscard]] bool IsValidNode(DataOrientedRoamNodeIndex node) const noexcept
    {
        return node != InvalidDataOrientedRoamNodeIndex && node < Nodes.size();
    }

    [[nodiscard]] bool IsLeaf(DataOrientedRoamNodeIndex node) const noexcept
    {
        return IsValidNode(node) && !Nodes.IsSplitAt(node);
    }
};

} // 命名空间 ParallelRoam::Algorithms::DataOrientedRoam
