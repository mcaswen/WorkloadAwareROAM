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
/// 使用经典裸指针二叉三角树维护 CPU ROAM 地形网格
/// 对象跨帧保留节点池、双优先队列、误差树和 CPU 网格，直到 Reset 或析构
/// 调用方每帧更新一次状态，并从返回网格和统计信息中读取结果
/// </summary>
class ClassicRoamMeshBuilder
{
public:
    /// <summary>
    /// 根据相机和像素误差阈值更新拓扑，并返回当前活动叶三角形组成的网格
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
        // 屏幕误差超过阈值后主动请求细分
        Requested,

        // 为补齐底边菱形关系而执行的强制细分
        ForcedByBaseNeighbor,
    };

    /// <summary>
    /// Classic ROAM 跨帧保留的二叉三角树节点，使用裸指针连接父子节点和三个邻居
    /// </summary>
    struct ClassicRoamNode
    {
        // Domain 只保存高度图 UV，避免相邻节点重复存储三维顶点
        TriangleDomain Domain;

        // 父子指针组成跨帧保留的二叉三角树
        ClassicRoamNode* Parent{nullptr};
        ClassicRoamNode* LeftChild{nullptr};
        ClassicRoamNode* RightChild{nullptr};

        // 三个邻居分别位于底边、左边和右边
        ClassicRoamNode* BaseNeighbor{nullptr};
        ClassicRoamNode* LeftNeighbor{nullptr};
        ClassicRoamNode* RightNeighbor{nullptr};

        // GeometricError 保存论文公式 (1) 自底向上得到的保守几何误差
        float GeometricError{0.0F};
        std::size_t VarianceIndex{0};
        std::uint64_t PathId{0};
        std::uint64_t CreatedBuildId{0};
        std::uint64_t ActivatedBuildId{0};
        // SplitBuildId 和 MergeBuildId 防止同一次更新立即撤销刚完成的拓扑修改
        std::uint64_t SplitBuildId{0};
        std::uint64_t MergeBuildId{0};
        std::uint64_t SplitBlockedBuildId{0};
        int Depth{0};
        std::uint8_t VarianceTreeIndex{0};
        bool ActivatedByForcedSplit{false};

        // Active 区分当前活动三角网格与节点池中等待复用的历史节点
        bool Active{false};

        // IsSplit 为真时由两个子节点代替当前节点参与活动拓扑
        bool IsSplit{false};

        // 节点保存自身堆下标，使跨帧保留的队列能以 O(log N) 删除任意拓扑节点
        std::size_t SplitQueueIndex{std::numeric_limits<std::size_t>::max()};
        std::size_t MergeQueueIndex{std::numeric_limits<std::size_t>::max()};

        // 每个可合并菱形在 Q_m 中只保留一个固定代表节点，两侧父节点都指向它
        ClassicRoamNode* MergeQueueRepresentative{nullptr};
        ClassicRoamNode* MergeQueuePartner{nullptr};

        // 活动叶节点在跨帧保留的 CPU 网格中占用的连续三角形槽位
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

    // 在创建拓扑节点前预计算误差树，使每个节点能直接读取覆盖其子树的几何误差
    void RebuildVarianceTrees(int finestDepth);
    void RefreshNodeVarianceErrors();
    [[nodiscard]] float VarianceError(std::uint8_t varianceTreeIndex, std::size_t varianceIndex) const;

    // 清空旧状态并建立覆盖整个地形的根菱形
    void ResetTopology();

    // 判断输入变化是否使已有节点、误差或预算状态失效
    [[nodiscard]] bool NeedsTopologyReset(
        const Terrain::HeightMap& heightMap,
        float terrainSize,
        float heightScale,
        const ClassicRoamSettings& settings) const;

    // 按 ROAM 双队列策略在同一循环中合并低误差区域并细分高误差区域
    void OptimizeWithPersistentDualQueues();

    // 重置后从两个根三角形建立队列，普通帧只刷新现有成员的分数
    void InitializePersistentQueues();
    void RefreshPersistentQueuePriorities();

    [[nodiscard]] float SplitQueueScore(const ClassicRoamNode& node) const;
    [[nodiscard]] float MergeQueueScore(const ClassicRoamNode& node) const;

    // Q_s 保存当前全部活动叶节点，队首是最值得细分的节点
    void InsertSplitQueueNode(ClassicRoamNode* node);
    void RemoveSplitQueueNode(ClassicRoamNode* node);
    void UpdateSplitQueueScore(ClassicRoamNode* node, float score);
    [[nodiscard]] ClassicRoamNode* TopSplitQueueNode() const;

    // Q_m 保存当前全部可合并菱形，每个菱形只保留一个固定的代表父节点
    [[nodiscard]] bool IsMergeableTopology(const ClassicRoamNode* node) const;
    [[nodiscard]] ClassicRoamNode* CanonicalMergeQueueNode(ClassicRoamNode* node) const;
    void InsertMergeQueueNodeIfEligible(ClassicRoamNode* node);
    void RemoveMergeQueueCandidate(ClassicRoamNode* node);
    [[nodiscard]] ClassicRoamNode* TopMergeQueueNode() const;

    // 拓扑变化只影响局部邻域，因此只移除并重建附近的合并队列成员
    void AppendQueueNeighborhood(ClassicRoamNode* seed, std::vector<ClassicRoamNode*>& nodes) const;
    void InvalidateMergeQueueNeighborhood(const std::vector<ClassicRoamNode*>& nodes);
    void RefreshMergeQueueNeighborhood(const std::vector<ClassicRoamNode*>& nodes);

    // 维护带节点下标的二叉堆，使任意成员更新和删除都保持对数复杂度
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

    // 沿底边细分节点，并在需要时先递归补齐底边邻居
    [[nodiscard]] bool SplitNode(
        ClassicRoamNode* node,
        SplitReason reason,
        ClassicRoamNode* forcedFrom,
        std::size_t reservedSplitSlots);

    // 细分后按 ROAM 菱形规则连接两个子节点及周围邻居
    void LinkSplitNeighbors(ClassicRoamNode* node, ClassicRoamNode* baseNeighbor);

    // 将邻居中指向旧叶节点的边替换为细分后对应的子节点
    void ReplaceNeighborReference(ClassicRoamNode* neighbor, ClassicRoamNode* oldNode, ClassicRoamNode* newNode) const;

    // 判断父节点能否在误差限制和本帧状态约束下安全恢复为叶节点
    [[nodiscard]] bool CanMergeNode(const ClassicRoamNode* node, float maximumScore) const;

    // 停用两个叶子节点并重新激活其父节点
    void MergeSingleNode(ClassicRoamNode* node);

    // 底边邻居也已细分时，将菱形两侧成对合并，避免产生裂缝
    [[nodiscard]] bool MergeNodeOrDiamond(ClassicRoamNode* node, float maximumScore);

    // 收集当前活动叶节点，供验证器和最终统计复用
    void CollectLeafNodes(std::vector<ClassicRoamNode*>& leafNodes) const;

    // 从指定根节点递归收集活动叶节点
    void CollectLeafNodesFrom(ClassicRoamNode* node, std::vector<ClassicRoamNode*>& leafNodes) const;

    // 保存当前仍处于细分状态的路径，供下一帧迟滞判断复用
    void CollectActiveSplitPaths();
    void CollectActiveSplitPathsFrom(const ClassicRoamNode* node);

    // 根据最终活动叶集合汇总调试分类和最大深度
    void AccumulateLeafStats(
        const Terrain::TerrainMeshData& meshData,
        const std::vector<ClassicRoamNode*>& leafNodes);

    // 将现有固定实现映射为统一阶段记录，并按需生成重放证据
    void FinalizePassTraces();
    void CollectPassEvidence();

    // 验证器只报告当前拓扑问题，不在正常更新路径中修改状态
    void ValidateTopology();
    void ValidatePersistentQueues(const std::vector<ClassicRoamNode*>& leafNodes);
    [[nodiscard]] std::size_t CountPersistentQueueInvariantViolations(
        const std::vector<ClassicRoamNode*>& leafNodes) const;
    void ValidateIncrementalMesh(const std::vector<ClassicRoamNode*>& leafNodes);

    // 让每个活动叶节点占用一个稠密网格槽位，拓扑变化时只重写受影响部分
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

    // 将屏幕误差与细分阈值和迟滞状态比较，决定节点是否需要细分
    [[nodiscard]] bool ShouldSplit(const ClassicRoamNode& node) const;
    [[nodiscard]] bool ShouldSplitWithScore(const ClassicRoamNode& node, float screenErrorScore) const;

    // 误差位于迟滞区间时沿用上一帧的细分状态，避免临界位置反复切换
    [[nodiscard]] bool WasSplitLastFrame(const ClassicRoamNode& node) const;

    enum class LeafDebugClass
    {
        Original,
        Subdivided,
        Rebuilt,
    };

    // 将活动叶节点分为原始、稳定细分和本帧重建三类
    [[nodiscard]] LeafDebugClass ClassifyLeafDebug(const ClassicRoamNode& node) const;

    // 调试颜色和高亮都使用同一叶节点分类，保证界面与统计含义一致
    [[nodiscard]] glm::vec3 DebugColorForLeaf(const ClassicRoamNode& node) const;
    [[nodiscard]] float DebugHighlightForLeaf(const ClassicRoamNode& node) const;

    // 计算细分与合并队列共用的屏幕误差分数
    [[nodiscard]] float ComputeScreenErrorScore(const ClassicRoamNode& node) const;

    [[nodiscard]] bool IsLeaf(const ClassicRoamNode* node) const;

    const Terrain::HeightMap* _heightMap{nullptr};
    ClassicRoamSettings _settings;
    ClassicRoamStats _stats;

    // 两棵误差树分别覆盖一个根三角形，并按完整二叉树下标存储
    // 预计算深度可以超过运行时 MaxDepth，以保留高度图原始分辨率中的细节误差
    std::array<std::vector<float>, 2> _varianceTrees;
    const Terrain::HeightMap* _varianceHeightMap{nullptr};
    int _varianceTreeMaxDepth{-1};

    // _nodes 统一管理节点生命周期，拓扑关系仍由 ClassicRoamNode 指针表达
    std::vector<std::unique_ptr<ClassicRoamNode>> _nodes;
    std::unordered_set<std::uint64_t> _previousSplitPaths;
    std::unordered_set<std::uint64_t> _currentSplitPaths;
    // 两个带下标的优先队列随活动拓扑一起跨帧保留
    std::vector<SplitQueueEntry> _splitQueue;
    std::vector<MergeQueueEntry> _mergeQueue;
    // 该数组按绘制顺序记录每个槽位对应的活动叶节点，无需再次遍历拓扑
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
