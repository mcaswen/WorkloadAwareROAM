#pragma once

#include "algorithms/classic_roam/ClassicRoamTypes.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <unordered_set>
#include <vector>

namespace ParallelRoam::Algorithms
{
struct TerrainLodViewInput;
}

namespace ParallelRoam::Algorithms::ClassicRoam
{
/// <summary>
/// Classic CPU ROAM 的裸指针二叉三角树网格生成器
/// 由 ClassicRoamTerrainLodAlgorithm 持有；builder 持有节点、队列、方差树和 CPU mesh，直到 Reset 或析构
/// Build 流程中的 state、topology、queue 和 mesh pass 会修改它；调用方只读取 Build 返回值和 Stats
/// </summary>
class ClassicRoamMeshBuilder
{
public:
    /// <summary>
    /// 根据完整视图输入和像素误差阈值生成当前 active leaf triangle mesh
    /// </summary>
    [[nodiscard]] const Terrain::TerrainMeshData& Build(
        const Terrain::HeightMap& heightMap,
        float terrainSize,
        float heightScale,
        const TerrainLodViewInput& view,
        const ClassicRoamSettings& settings);

    [[nodiscard]] const ClassicRoamStats& Stats() const;
    [[nodiscard]] const std::vector<ClassicRoamMeshUpdateRange>& MeshUpdateRanges() const;
    [[nodiscard]] bool MeshRequiresFullUpload() const;
    [[nodiscard]] std::uint64_t MeshGeneration() const;

private:
    enum class SplitReason
    {
        // 普通误差阈值触发的 split
        Requested,

        // baseNeighbor 为了补齐 diamond 触发的 split
        ForcedByBaseNeighbor,
    };

    /// <summary>
    /// Classic ROAM 的持久化二叉三角树节点，使用裸指针表达 parent / child / neighbor 拓扑
    /// </summary>
    struct ClassicRoamNode
    {
        // Domain 使用 UV 空间表达，避免节点保存重复三维顶点
        TriangleDomain Domain;

        // Parent 和 child 使用经典 ROAM 裸指针拓扑
        ClassicRoamNode* Parent{nullptr};
        ClassicRoamNode* LeftChild{nullptr};
        ClassicRoamNode* RightChild{nullptr};

        // 三个 neighbor 指针对应 base edge、left edge 和 right edge
        ClassicRoamNode* BaseNeighbor{nullptr};
        ClassicRoamNode* LeftNeighbor{nullptr};
        ClassicRoamNode* RightNeighbor{nullptr};

        // GeometricError 是论文公式 (1) 自底向上传播的 nested wedgie thickness
        float GeometricError{0.0F};
        std::size_t VarianceIndex{0};
        std::uint64_t PathId{0};
        std::uint64_t CreatedBuildId{0};
        std::uint64_t ActivatedBuildId{0};
        // SplitBuildId / MergeBuildId 也用于禁止同一 Build 立即逆转刚提交的拓扑事务。
        std::uint64_t SplitBuildId{0};
        std::uint64_t MergeBuildId{0};
        std::uint64_t SplitBlockedBuildId{0};
        int Depth{0};
        std::uint8_t VarianceTreeIndex{0};
        bool ActivatedByForcedSplit{false};

        // Active 区分当前 triangulation 与对象池中等待复用的历史节点
        bool Active{false};

        // IsSplit 决定 child 当前是否参与 active topology
        bool IsSplit{false};

        // intrusive heap index 让持久队列可以 O(log N) 删除任意 topology 节点
        std::size_t SplitQueueIndex{std::numeric_limits<std::size_t>::max()};
        std::size_t MergeQueueIndex{std::numeric_limits<std::size_t>::max()};

        // 一个 diamond 只在 Q_m 中保存 canonical parent；两侧都指向同一 representative
        ClassicRoamNode* MergeQueueRepresentative{nullptr};
        ClassicRoamNode* MergeQueuePartner{nullptr};

        // active leaf 在持久 CPU mesh 中占用的稠密三角形槽位。
        std::size_t MeshSlot{std::numeric_limits<std::size_t>::max()};
    };

    struct SplitQueueEntry
    {
        ClassicRoamNode* Node{nullptr};
        float Score{0.0F};
    };

    struct MergeQueueEntry
    {
        ClassicRoamNode* Node{nullptr};
        float Score{0.0F};
    };

    enum class MeshTopologyEditType
    {
        Split,
        Merge,
    };

    struct MeshTopologyEdit
    {
        MeshTopologyEditType Type{MeshTopologyEditType::Split};
        ClassicRoamNode* Parent{nullptr};
    };

    [[nodiscard]] ClassicRoamNode* AddNode(
        const TriangleDomain& domain,
        ClassicRoamNode* parent,
        int depth,
        std::uint64_t pathId,
        std::uint8_t varianceTreeIndex,
        std::size_t varianceIndex);

    // nested wedgie tree 会在 topology 创建前预计算，并按公式 (1) 向父节点累加厚度
    void RebuildVarianceTrees(int finestDepth);
    void RefreshNodeVarianceErrors();
    [[nodiscard]] float VarianceError(std::uint8_t varianceTreeIndex, std::size_t varianceIndex) const;

    // 初始化或重置持久化根 diamond
    void ResetTopology();

    // 判断设置变化是否必须重建整棵树
    [[nodiscard]] bool NeedsTopologyReset(
        const Terrain::HeightMap& heightMap,
        float terrainSize,
        float heightScale,
        const ClassicRoamSettings& settings) const;

    // 论文 dual-queue optimizer：跨帧保留 Q_s/Q_m，并在一个 crossover 循环中更新 topology
    void OptimizeWithPersistentDualQueues();

    // 重置时从 base triangulation 初始化队列；普通帧只更新已有成员的 priority
    void InitializePersistentQueues();
    void RefreshPersistentQueuePriorities();

    [[nodiscard]] float SplitQueueScore(const ClassicRoamNode& node) const;
    [[nodiscard]] float MergeQueueScore(const ClassicRoamNode& node) const;

    // Q_s 保存当前 triangulation 的全部 active leaves
    void InsertSplitQueueNode(ClassicRoamNode* node);
    void RemoveSplitQueueNode(ClassicRoamNode* node);
    void UpdateSplitQueueScore(ClassicRoamNode* node, float score);
    [[nodiscard]] ClassicRoamNode* TopSplitQueueNode() const;

    // Q_m 保存当前全部 mergeable diamonds，每个 diamond 只保留一个 canonical parent
    [[nodiscard]] bool IsMergeableTopology(const ClassicRoamNode* node) const;
    [[nodiscard]] ClassicRoamNode* CanonicalMergeQueueNode(ClassicRoamNode* node) const;
    void InsertMergeQueueNodeIfEligible(ClassicRoamNode* node);
    void RemoveMergeQueueCandidate(ClassicRoamNode* node);
    [[nodiscard]] ClassicRoamNode* TopMergeQueueNode() const;

    // topology 变更前后只失效和重建局部 diamond membership
    void AppendQueueNeighborhood(ClassicRoamNode* seed, std::vector<ClassicRoamNode*>& nodes) const;
    void InvalidateMergeQueueNeighborhood(const std::vector<ClassicRoamNode*>& nodes);
    void RefreshMergeQueueNeighborhood(const std::vector<ClassicRoamNode*>& nodes);

    // indexed binary heap 的局部维护函数
    [[nodiscard]] bool SplitEntryPrecedes(const SplitQueueEntry& left, const SplitQueueEntry& right) const;
    [[nodiscard]] bool MergeEntryPrecedes(const MergeQueueEntry& left, const MergeQueueEntry& right) const;
    void SwapSplitQueueEntries(std::size_t left, std::size_t right);
    void SwapMergeQueueEntries(std::size_t left, std::size_t right);
    void SiftSplitQueueUp(std::size_t index);
    void SiftSplitQueueDown(std::size_t index);
    void SiftMergeQueueUp(std::size_t index);
    void SiftMergeQueueDown(std::size_t index);
    void RestoreSplitQueueAt(std::size_t index);
    void RestoreMergeQueueAt(std::size_t index);
    void HeapifySplitQueue();
    void HeapifyMergeQueue();

    // 沿 base edge split，生成两个 child triangle
    [[nodiscard]] bool SplitNode(
        ClassicRoamNode* node,
        SplitReason reason,
        ClassicRoamNode* forcedFrom,
        std::size_t reservedSplitSlots);

    // split 后按 Classic ROAM diamond 关系连接 child 和 neighbor
    void LinkSplitNeighbors(ClassicRoamNode* node, ClassicRoamNode* baseNeighbor);

    // 邻居还指向旧 leaf 时，需要改指向 split 后对应的 child
    void ReplaceNeighborReference(ClassicRoamNode* neighbor, ClassicRoamNode* oldNode, ClassicRoamNode* newNode) const;

    // 判断 parent 是否可以在给定最大误差下安全回收为 leaf
    [[nodiscard]] bool CanMergeNode(const ClassicRoamNode* node, float maximumScore) const;

    // 回收一个 parent 的两个 leaf child
    void MergeSingleNode(ClassicRoamNode* node);

    // 若 base neighbor 也 split，则按 diamond 成对回收
    [[nodiscard]] bool MergeNodeOrDiamond(ClassicRoamNode* node, float maximumScore);

    // 收集当前 active leaf，供裂缝检测和 neighbor 重建复用
    void CollectLeafNodes(std::vector<ClassicRoamNode*>& leafNodes) const;

    // 从指定根节点收集 active leaf
    void CollectLeafNodesFrom(ClassicRoamNode* node, std::vector<ClassicRoamNode*>& leafNodes) const;

    // 收集当前 active internal path，供 hysteresis 复用
    void CollectActiveSplitPaths();
    void CollectActiveSplitPathsFrom(const ClassicRoamNode* node);

    // 聚合当前帧 leaf 分类和深度统计
    void AccumulateLeafStats(
        const Terrain::TerrainMeshData& meshData,
        const std::vector<ClassicRoamNode*>& leafNodes);

    // validator 只检查当前拓扑，不在默认路径修复裂缝
    void ValidateTopology();
    void ValidatePersistentQueues(const std::vector<ClassicRoamNode*>& leafNodes);
    void ValidateIncrementalMesh(const std::vector<ClassicRoamNode*>& leafNodes);

    // 现代 indexed-mesh 等价的增量输出：active leaf 对应稠密固定槽位。
    void BeginIncrementalMeshUpdate(bool resetTopology);
    void RecordMeshSplit(ClassicRoamNode* parent);
    void RecordMeshMerge(ClassicRoamNode* parent);
    void ApplyIncrementalMeshUpdates();
    void InitializeIncrementalMesh();
    void ResetIncrementalMeshStorage();
    void ReplaceMeshLeafWithChildren(ClassicRoamNode* parent);
    void ReplaceMeshChildrenWithLeaf(ClassicRoamNode* parent);
    void AppendMeshLeaf(ClassicRoamNode* node);
    void RemoveMeshLeaf(ClassicRoamNode* node);
    void WriteMeshLeaf(std::size_t slot, const ClassicRoamNode& node);
    void RefreshMeshLeafDebugAttributes(ClassicRoamNode& node);
    void MarkMeshSlotDirty(std::size_t slot);
    void FinalizeIncrementalMeshUpdate();

    // 当前实现使用阈值决策，后续可替换为 priority queue
    [[nodiscard]] bool ShouldSplit(const ClassicRoamNode& node) const;
    [[nodiscard]] bool ShouldSplitWithScore(const ClassicRoamNode& node, float screenErrorScore) const;

    // 判断节点是否在 hysteresis 区间内沿用上一帧 split 状态
    [[nodiscard]] bool WasSplitLastFrame(const ClassicRoamNode& node) const;

    enum class LeafDebugClass
    {
        Original,
        Subdivided,
        Rebuilt,
    };

    // 对 active leaf 做调试分类，供颜色输出和 benchmark 统计共用
    [[nodiscard]] LeafDebugClass ClassifyLeafDebug(const ClassicRoamNode& node) const;

    // 按 leaf 调试分类输出稳定颜色，避免 UI 和 benchmark 口径分裂
    [[nodiscard]] glm::vec3 DebugColorForLeaf(const ClassicRoamNode& node) const;
    [[nodiscard]] float DebugHighlightForLeaf(const ClassicRoamNode& node) const;

    // 统一组合视锥、保守几何误差和投影长边密度
    [[nodiscard]] float ComputeScreenErrorScore(const ClassicRoamNode& node) const;

    [[nodiscard]] bool IsLeaf(const ClassicRoamNode* node) const;

    const Terrain::HeightMap* _heightMap{nullptr};
    ClassicRoamSettings _settings;
    ClassicRoamStats _stats;

    // 两棵 nested wedgie tree 分别对应两个根三角形，使用二叉堆索引存储
    // 预计算深度可大于运行时 MaxDepth，以覆盖高度图源分辨率中的更深误差
    std::array<std::vector<float>, 2> _varianceTrees;
    const Terrain::HeightMap* _varianceHeightMap{nullptr};
    int _varianceTreeMaxDepth{-1};

    // _nodes 只负责生命周期，算法拓扑通过 ClassicRoamNode* 表达
    std::vector<std::unique_ptr<ClassicRoamNode>> _nodes;
    std::unordered_set<std::uint64_t> _previousSplitPaths;
    std::unordered_set<std::uint64_t> _currentSplitPaths;
    // 两个 indexed heaps 和 active topology 一起跨帧保留
    std::vector<SplitQueueEntry> _splitQueue;
    std::vector<MergeQueueEntry> _mergeQueue;
    // mesh slot owner 数组本身就是活动 leaf 的稠密输出视图。
    Terrain::TerrainMeshData _meshData;
    std::vector<ClassicRoamNode*> _meshSlotOwners;
    std::vector<std::uint64_t> _meshSlotDirtyGeneration;
    std::vector<std::size_t> _dirtyMeshSlots;
    std::vector<ClassicRoamMeshUpdateRange> _meshUpdateRanges;
    std::vector<ClassicRoamNode*> _debugTransitionLeaves;
    std::vector<MeshTopologyEdit> _meshTopologyEdits;
    ClassicRoamNode* _rootA{nullptr};
    ClassicRoamNode* _rootB{nullptr};
    glm::mat4 _viewProjection{1.0F};
    std::array<glm::vec4, 6> _frustumPlanes{};
    std::uint32_t _drawableWidth{1U};
    std::uint32_t _drawableHeight{1U};
    std::size_t _remainingSplitBudget{0U};
    float _terrainSize{1.0F};
    float _heightScale{1.0F};
    int _topologyMaxDepth{0};
    std::uint64_t _buildSequence{0};
    std::uint64_t _meshGeneration{0};
    bool _meshRequiresFullUpload{true};
    bool _meshNeedsInitialization{true};
};
} // 命名空间 ParallelRoam::Algorithms::ClassicRoam
