#pragma once

#include "algorithms/TerrainLodPassTrace.h"
#include "terrain/HeightMap.h"
#include "terrain/TerrainMeshBuilder.h"

#include <glm/glm.hpp>

#include <cstddef>

namespace ParallelRoam::Algorithms::DataOrientedRoam
{
/// <summary>
/// 表示 DOD ROAM 三角形在高度图上的覆盖区域
/// 该值可在各阶段之间复制，不负责管理节点、队列或线程资源
/// </summary>
struct TriangleDomain
{
    // 三个 UV 点定义一个 ROAM 三角形，写入网格时再生成世界空间顶点
    glm::vec2 A{0.0F};
    glm::vec2 B{0.0F};
    glm::vec2 C{0.0F};
};

struct TriangleDomainChildren
{
    TriangleDomain Left;
    TriangleDomain Right;
};

/// <summary>
/// 描述 DOD 跨帧保留的 CPU 网格中一段需要重新上传的连续三角形
/// 适配层会把三角形槽位换算为公共接口使用的顶点和索引范围
/// </summary>
struct DataOrientedRoamMeshUpdateRange
{
    std::size_t FirstTriangle{0U};
    std::size_t TriangleCount{0U};
};

[[nodiscard]] TriangleDomainChildren SplitTriangleDomain(const TriangleDomain& domain);

/// <summary>
/// 控制 DOD CPU ROAM 每帧的细分、合并、并行度和拓扑验证
/// 地形 LOD 适配器每帧生成一次，流水线在本次更新期间将其保存到状态中
/// </summary>
struct DataOrientedRoamSettings
{
    int MaxDepth{14};
    // 细分和合并阈值统一使用屏幕像素误差
    float SplitThreshold{4.0F};
    float MergeThreshold{2.0F};
    // 当前可用于渲染的活动叶三角形数量上限
    std::size_t TriangleBudget{20000U};
    TerrainLodPassPolicy PassPolicy{};
    // 默认由跨帧保留的细分队列保存分数，避免评分时随机写入节点池
    bool MirrorSplitScoresToNodePool{false};
    bool EnableLocalConstraints{true};
    bool EnableTopologyValidation{false};
    // 只在研究基准中计算结果哈希并执行持久队列全量检查
    bool EnablePassEvidence{false};
};

/// <summary>
/// 记录 DOD CPU ROAM 最近一次更新的规模、并行行为、结果和各阶段耗时
/// 数据由 DataOrientedRoamState 保存，更新完成后只读导出
/// </summary>
struct DataOrientedRoamStats
{
    TerrainLodPassTraceArray PassTraces{MakeTerrainLodPassTraces()};
    std::uint64_t BuildSequence{0U};
    std::uint64_t TopologyHash{0U};
    std::uint64_t ActiveLeafHash{0U};
    std::uint64_t MeshHash{0U};
    std::uint64_t NormalizedMeshHash{0U};
    std::size_t TriangleBudget{0U};
    std::size_t QueueInvariantViolationCount{0U};
    float PassEvidenceMilliseconds{0.0F};
    // 节点池当前规模、预留容量和 SoA 数组内存占用
    std::size_t NodeCount{0};
    std::size_t ReservedNodeCapacity{0};
    std::size_t NodeStorageBytes{0};
    std::size_t NodeStorageArrayCount{0};
    // 最终活动叶三角形的数量，以及原有、新建和重新激活三类数量
    std::size_t ActiveTriangleCount{0};
    std::size_t OriginalTriangleCount{0};
    std::size_t SubdividedTriangleCount{0};
    std::size_t RebuiltTriangleCount{0};
    // 当前细分路径数量
    std::size_t ActiveSplitCount{0};
    // 本帧普通细分、强制细分和合并的成功次数
    std::size_t SplitCount{0};
    std::size_t ForcedSplitCount{0};
    std::size_t MergeCount{0};
    // 达到深度上限后的裂缝风险和邻接约束传播次数
    std::size_t CrackRiskCount{0};
    std::size_t ConstraintPassCount{0};
    // 候选峰值、拒绝原因和验证器发现的问题数量
    std::size_t CandidatePeakCount{0};
    std::size_t RejectedSplitCount{0};
    std::size_t BudgetRejectedSplitCount{0};
    std::size_t RejectedMergeCount{0};
    std::size_t TjunctionCount{0};
    std::size_t InvalidNeighborCount{0};
    std::size_t InvalidTopologyCount{0};
    // Q_s 刷新时实际重新评分的活动叶节点数量
    std::size_t ErrorEvaluationCount{0};
    // Q_s/Q_m 本帧整批重新评分的条目数量和各自实际线程数量
    std::size_t SplitScoreEntryCount{0U};
    std::size_t MergeScoreEntryCount{0U};
    std::size_t SplitCandidateMarkWorkerCount{0U};
    std::size_t MergeCandidateMarkWorkerCount{0U};
    // 本帧候选评分实际使用的线程数量
    std::size_t ErrorEvaluationWorkerCount{0};
    // 保留收集和候选评分线程数，供公共统计接口区分阶段
    std::size_t CollectWorkerCount{0};
    std::size_t CandidateMarkWorkerCount{0};
    // 网格提交阶段实际使用的线程数量
    std::size_t EmitWorkerCount{0};
    // 跨帧保留网格的完整重建次数、重新写入数量、复用数量和上传区间数量
    std::size_t MeshFullRebuildCount{0};
    std::size_t MeshUpdatedTriangleCount{0};
    std::size_t MeshReusedTriangleCount{0};
    std::size_t MeshDirtyRangeCount{0};
    // 本帧细分候选数量
    std::size_t SplitCandidateCount{0};
    // 合并候选数量以及帧末 Q_s/Q_m 的成员规模和跨帧维护次数
    std::size_t MergeCandidateCount{0};
    std::size_t PersistentSplitQueueSize{0};
    std::size_t PersistentMergeQueueSize{0};
    std::size_t QueueCrossoverCount{0};
    std::size_t QueueMembershipUpdateCount{0};
    // 拓扑分块总数和提交阶段的实际线程数量
    std::size_t TopologyChunkCount{0};
    std::size_t TopologyCommitWorkerCount{0};
    // 细分和合并只有达到各自的候选数量阈值后才会使用多个线程
    std::size_t TopologyCommitMinCandidateCount{0};
    std::size_t SplitTopologyCommitMinCandidateCount{0};
    std::size_t MergeTopologyCommitMinCandidateCount{0};
    // 候选数量、非空分块数量和各拓扑阶段实际使用的线程数
    std::size_t SplitTopologyCandidateCount{0};
    std::size_t SplitTopologyNonEmptyChunkCount{0};
    std::size_t SplitTopologyCommitWorkerCount{0};
    std::size_t MergeTopologyCandidateCount{0};
    std::size_t MergeTopologyNonEmptyChunkCount{0};
    std::size_t MergeTopologyCommitWorkerCount{0};
    // 完全位于单个分块内或跨越分块的候选数量，以及多线程处理成功的数量
    std::size_t InteriorSplitCandidateCount{0};
    std::size_t BoundarySplitCandidateCount{0};
    std::size_t InteriorMergeCandidateCount{0};
    std::size_t BoundaryMergeCandidateCount{0};
    std::size_t ParallelSplitCommitCount{0};
    std::size_t ParallelMergeCommitCount{0};
    // 完整更新、输入准备、叶集合读取、网格提交和收尾耗时
    float UpdateMilliseconds{0.0F};
    float PrepareMilliseconds{0.0F};
    float BudgetLeafCollectMilliseconds{0.0F};
    float FinalLeafCollectMilliseconds{0.0F};
    float MeshEmitMilliseconds{0.0F};
    float FinalizeMilliseconds{0.0F};
    // 旧版误差评估字段仅用于兼容历史报告，当前评分耗时归入 Q_s/Q_m 刷新
    float ErrorEvaluationSingleThreadMilliseconds{0.0F};
    float ErrorEvaluationParallelMilliseconds{0.0F};
    // 活动叶读取以及细分/合并候选评分耗时
    float ActiveLeafCollectMilliseconds{0.0F};
    float SplitCandidateMarkMilliseconds{0.0F};
    float MergeCandidateMarkMilliseconds{0.0F};
    // 分别记录细分候选分块、暂时移出队列、线程处理、主线程整理结果、更新索引和继续串行处理的耗时
    float SplitTopologyChunkBuildMilliseconds{0.0F};
    float SplitTopologyQueueInvalidationMilliseconds{0.0F};
    float SplitTopologyParallelCommitMilliseconds{0.0F};
    float SplitTopologyResultMergeMilliseconds{0.0F};
    float SplitTopologyIndexQueueRefreshMilliseconds{0.0F};
    float SplitTopologySerialConvergenceMilliseconds{0.0F};
    // 合并使用相同的六段计时，并额外记录为了腾出细分预算而执行合并的耗时
    float MergeCrossoverMilliseconds{0.0F};
    float MergeTopologyChunkBuildMilliseconds{0.0F};
    float MergeTopologyQueueInvalidationMilliseconds{0.0F};
    float MergeTopologyParallelCommitMilliseconds{0.0F};
    float MergeTopologyResultMergeMilliseconds{0.0F};
    float MergeTopologyIndexQueueRefreshMilliseconds{0.0F};
    float MergeTopologySerialConvergenceMilliseconds{0.0F};
    float SplitMilliseconds{0.0F};
    float EmitMilliseconds{0.0F};
    float ValidateMilliseconds{0.0F};
    float MergeMilliseconds{0.0F};
    int MaxDepthReached{0};
};
} // 命名空间 ParallelRoam::Algorithms::DataOrientedRoam
