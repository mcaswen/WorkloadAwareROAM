#pragma once

#include "algorithms/data_oriented_roam/DataOrientedRoamTypes.h"

#include <array>
#include <atomic>
#include <cstdint>
#include <limits>
#include <unordered_set>
#include <vector>

namespace ParallelRoam::Algorithms::DataOrientedRoam
{
class DataOrientedRoamThreadPool;

using DataOrientedRoamNodeIndex = std::uint32_t;
constexpr DataOrientedRoamNodeIndex InvalidDataOrientedRoamNodeIndex =
    std::numeric_limits<DataOrientedRoamNodeIndex>::max();
// NodeIndex 已经限制 node pool 为 uint32_t；所有活动列表和持久队列都是
// node pool 的子集，因此 position 可以使用相同宽度，避免 64 位旁路索引。
using DataOrientedRoamPosition = std::uint32_t;
constexpr DataOrientedRoamPosition InvalidDataOrientedRoamPosition =
    std::numeric_limits<DataOrientedRoamPosition>::max();
constexpr DataOrientedRoamPosition InvalidActiveNodePosition =
    InvalidDataOrientedRoamPosition;
static_assert(sizeof(DataOrientedRoamNodeIndex) == sizeof(DataOrientedRoamPosition));
// chunk id 是并发 topology commit 的 ownership 键
using DataOrientedRoamChunkId = std::uint32_t;
constexpr DataOrientedRoamChunkId InvalidDataOrientedRoamChunkId =
    std::numeric_limits<DataOrientedRoamChunkId>::max();
// 固定分块数避免把内部调度策略暴露成 UI 参数
constexpr int DataOrientedRoamTopologyChunkGridSize = 8;

/// <summary>
/// split 提交的来源，用于区分误差驱动和兼容约束传播
/// </summary>
enum class DataOrientedRoamSplitReason
{
    Requested,
    ForcedByBaseNeighbor,
};

/// <summary>
/// 活动叶相对当前 build 的生命周期分类
/// </summary>
enum class DataOrientedRoamLeafDebugClass
{
    Original,
    Subdivided,
    Rebuilt,
};

/// <summary>
/// split priority queue 的候选快照，由候选标记 pass 生成
/// </summary>
struct DataOrientedRoamSplitCandidate
{
    // 高误差优先，Sequence 保证并行收集后的同分候选仍可确定排序
    float Score{0.0F};
    std::uint64_t Sequence{0};
    DataOrientedRoamNodeIndex Node{InvalidDataOrientedRoamNodeIndex};
};

/// <summary>
/// split 持久队列中的连续 heap 项，score 与 node 放在同一个位置供比较直接读取
/// </summary>
struct DataOrientedRoamSplitQueueEntry
{
    float Score{0.0F};
    DataOrientedRoamNodeIndex Node{InvalidDataOrientedRoamNodeIndex};
};

/// <summary>
/// merge 队列的候选快照，拓扑提交前不会修改节点关系
/// </summary>
struct DataOrientedRoamMergeCandidate
{
    // 低误差 diamond 优先回收
    float Score{0.0F};
    DataOrientedRoamNodeIndex Node{InvalidDataOrientedRoamNodeIndex};
};

struct DataOrientedRoamMergeQueueEntry
{
    float Score{0.0F};
    DataOrientedRoamNodeIndex Node{InvalidDataOrientedRoamNodeIndex};
};

/// <summary>
/// DOD 节点的动态 membership sidecar。
/// node pool 仍保持 SoA；只把需要按 node 随机定位的活动/队列元数据收拢，
/// 对应 Classic 节点内的 intrusive position、representative 和 partner 字段。
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

enum class DataOrientedRoamMeshTopologyEditType
{
    Split,
    Merge,
};

struct DataOrientedRoamMeshTopologyEdit
{
    DataOrientedRoamMeshTopologyEditType Type{DataOrientedRoamMeshTopologyEditType::Split};
    DataOrientedRoamNodeIndex Node{InvalidDataOrientedRoamNodeIndex};
};

/// <summary>
/// DOD CPU 输出的持久 Mesh 状态。
/// NodeSlots 是独立的单字段 SoA 反向索引；SlotOwners 是稠密活动 cut，
/// topology edit 只在主线程重放，不让并行 topology worker 直接写 Mesh。
/// </summary>
struct DataOrientedRoamIncrementalMesh
{
    Terrain::TerrainMeshData Data;
    std::vector<DataOrientedRoamPosition> NodeSlots;
    std::vector<DataOrientedRoamNodeIndex> SlotOwners;
    std::vector<std::uint64_t> SlotDirtyGenerations;
    std::vector<DataOrientedRoamPosition> DirtySlots;
    std::vector<DataOrientedRoamMeshUpdateRange> UpdateRanges;
    std::vector<DataOrientedRoamNodeIndex> DebugTransitionLeaves;
    std::vector<DataOrientedRoamMeshTopologyEdit> TopologyEdits;
    std::uint64_t Generation{0U};
    bool RequiresFullUpload{true};
    bool NeedsInitialization{true};
    bool TracksTopologyEdits{false};
};

struct DataOrientedRoamMergeCandidateEvaluation
{
    // NodeScore 保持普通 merge 候选的原排序语义；PairScore 用于预算重平衡时衡量整个 diamond。
    bool Eligible{false};
    float NodeScore{0.0F};
    float PairScore{0.0F};
};

/// <summary>
/// SoA 节点池中单个节点的只读视图，字段引用到底层连续数组
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
/// SoA 节点池中单个节点的可写视图，算法 pass 通过它保持字段访问可读性
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
/// Data-Oriented ROAM 的 SoA 节点池，拓扑、误差、深度和 flag 分别连续存储
/// 由 DataOrientedRoamState 独占；ResetTopology 清空，AddNode 追加，merge 后保留 child index 供复用
/// state 初始化和 topology pass 会修改它；其他 pass 通过索引访问，不持有节点地址
/// </summary>
struct DataOrientedRoamNodePool
{
    // 几何只保存 UV 定义域，世界坐标在评分和 emit 时按需恢复
    std::vector<TriangleDomain> Domains;
    std::vector<DataOrientedRoamNodeIndex> Parents;

    // merge 后保留 child index，使后续 split 可以复用节点和静态误差
    std::vector<DataOrientedRoamNodeIndex> LeftChildren;
    std::vector<DataOrientedRoamNodeIndex> RightChildren;

    // 三个 neighbor 对应 base、left、right 三条边
    std::vector<DataOrientedRoamNodeIndex> BaseNeighbors;
    std::vector<DataOrientedRoamNodeIndex> LeftNeighbors;
    std::vector<DataOrientedRoamNodeIndex> RightNeighbors;

    // InteriorChunkIds 缓存分块归属，避免 topology pass 反复按 UV 计算
    std::vector<DataOrientedRoamChunkId> InteriorChunkIds;

    // GeometricErrors 保存 nested wedgie thickness，与相机无关且可跨帧复用
    std::vector<float> GeometricErrors;
    // ScreenErrors 仅按需镜像给 GPU 快照，DOD 的 Q_s score 由 SplitQueue 独立持有
    std::vector<float> ScreenErrors;
    std::vector<std::size_t> VarianceIndices;

    // PathIds 是 hysteresis 的稳定键，不能使用 vector index 代替
    std::vector<std::uint64_t> PathIds;

    // build id 让 debug overlay 区分新建、激活和合并节点
    std::vector<std::uint64_t> CreatedBuildIds;
    std::vector<std::uint64_t> ActivatedBuildIds;
    std::vector<std::uint64_t> SplitBuildIds;
    std::vector<std::uint64_t> MergeBuildIds;
    std::vector<int> Depths;
    std::vector<std::uint8_t> VarianceTreeIndices;

    // flags 分离保存，避免和 index / float 字段混在同一 cache line
    std::vector<std::uint8_t> ActivatedByForcedSplits;
    std::vector<std::uint8_t> IsSplits;

    [[nodiscard]] std::size_t size() const noexcept
    {
        return Domains.size();
    }

    // 热路径只读取单个 SoA 字段时不再构造完整的 20 字段代理
    // 除缓存 score 外均只提供只读访问，写操作交给专用拓扑函数
    // 与 operator[] 一致，由调用方保证索引有效
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

    // proxy 让 pass 保持节点语义，同时底层继续使用 SoA 布局
    [[nodiscard]] DataOrientedRoamNodeRef operator[](DataOrientedRoamNodeIndex node);
    [[nodiscard]] DataOrientedRoamNodeConstRef operator[](DataOrientedRoamNodeIndex node) const;
};

/// <summary>
/// DOD ROAM 的可变工作集，所有 pass 都只通过这个状态对象交换数据
/// 由 DataOrientedRoamPipeline 创建并跨帧保存；Build 期间由各 pass 修改，pipeline 析构时释放
/// Pipeline 持有它；HeightMap、ThreadPool 仅在 Build 调用期间借用
/// </summary>
struct DataOrientedRoamState
{
    // HeightMap 和 ThreadPool 只在 Build 调用期间借用
    const Terrain::HeightMap* HeightMap{nullptr};
    DataOrientedRoamSettings Settings;
    DataOrientedRoamStats Stats;
    DataOrientedRoamNodePool Nodes;

    // 两棵 nested wedgie tree 分别对应两个根三角形；深度可超过运行时 MaxDepth
    std::array<std::vector<float>, 2> VarianceTrees;
    const Terrain::HeightMap* VarianceHeightMap{nullptr};
    int VarianceTreeMaxDepth{-1};

    // 两组稳定 path id 在帧边界交换，为 split/merge 提供 hysteresis 记忆
    std::unordered_set<std::uint64_t> PreviousSplitPaths;
    std::unordered_set<std::uint64_t> CurrentSplitPaths;

    // 当前活动 internal 节点的连续索引，避免 merge 每帧扫描历史 node pool
    std::vector<DataOrientedRoamNodeIndex> ActiveInternalNodes;
    // 当前活动 leaf 的稠密视图只随拓扑变化，评分和 heapify 不再改变它的顺序
    std::vector<DataOrientedRoamNodeIndex> ActiveLeafNodes;
    // 每个 node 的活动/队列 membership 使用紧凑 sidecar，避免六组旁路数组分散访问
    std::vector<DataOrientedRoamNodeMembership> NodeMembership;

    // CPU adapter 跨 Build 借用这份持久 Mesh；GPU topology-only 路径不维护其 edit。
    DataOrientedRoamIncrementalMesh IncrementalMesh;

    // 持久 Q_s 独立保存 node/score，反向位置支持 forced split 按 node 删除
    std::vector<DataOrientedRoamSplitQueueEntry> SplitQueue;
    std::vector<std::uint64_t> SplitQueueBlockedBuildIds;

    // 每个可 merge 的 diamond 只保存一个 canonical representative
    std::vector<DataOrientedRoamMergeQueueEntry> MergeQueue;

    // RootA 和 RootB 构成初始 diamond
    DataOrientedRoamNodeIndex RootA{InvalidDataOrientedRoamNodeIndex};
    DataOrientedRoamNodeIndex RootB{InvalidDataOrientedRoamNodeIndex};

    glm::mat4 ViewProjection{1.0F};
    std::array<glm::vec4, 6> FrustumPlanes{};
    std::uint32_t DrawableWidth{1U};
    std::uint32_t DrawableHeight{1U};
    // 串行 topology 使用普通计数；不会在每个事务里执行 atomic load/CAS。
    std::size_t RemainingSerialSplitBudget{0U};
    // 并行 commit 的 worker 之间通过 atomic token 共享硬预算。
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

} // namespace ParallelRoam::Algorithms::DataOrientedRoam
