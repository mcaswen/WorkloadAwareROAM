#pragma once

#include "terrain/HeightMap.h"
#include "terrain/TerrainMeshBuilder.h"

#include <glm/glm.hpp>

#include <cstddef>

namespace ParallelRoam::Algorithms::DataOrientedRoam
{
/// <summary>
/// DOD ROAM 的跨 pass 值类型
/// 由 DataOrientedRoamState 或调用方按值持有；不直接拥有 node pool、queue 或 worker
/// </summary>
struct TriangleDomain
{
    // 三个 UV 点定义一个 ROAM 三角形，世界空间顶点在 emit 时生成
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
/// DOD 持久 CPU Mesh 的连续三角形更新范围。
/// adapter 会把三角形槽位换算为统一 packet 的 vertex/index 范围。
/// </summary>
struct DataOrientedRoamMeshUpdateRange
{
    std::size_t FirstTriangle{0U};
    std::size_t TriangleCount{0U};
};

[[nodiscard]] TriangleDomainChildren SplitTriangleDomain(const TriangleDomain& domain);

/// <summary>
/// Data-Oriented CPU ROAM 的单帧细分、合并和拓扑验证参数
/// 由 TerrainLod 适配器按帧生成，MeshBuilder 在 Build 期间复制到 state
/// </summary>
struct DataOrientedRoamSettings
{
    int MaxDepth{14};
    // split 和 merge 阈值统一使用像素误差
    float SplitThreshold{4.0F};
    float MergeThreshold{2.0F};
    // 活动 leaf triangle 的硬上限
    std::size_t TriangleBudget{20000U};
    // 0 自动选择 worker 数，1 保持串行评估
    std::size_t ErrorEvaluationWorkerCount{0};
    // 关闭时跳过 Split 候选快照、分桶和 chunk 提交，直接消费实时 Q_s
    bool EnableParallelSplit{true};
    // score 由持久 split queue 按节点索引保存，节点池不承担随机镜像写入。
    bool MirrorSplitScoresToNodePool{false};
    bool EnableLocalConstraints{true};
    bool EnableTopologyValidation{false};
};

/// <summary>
/// Data-Oriented CPU ROAM 的运行统计
/// 由 DataOrientedRoamState 持有，在每次 Build 中更新，Build 结束后只读导出
/// </summary>
struct DataOrientedRoamStats
{
    // 节点池规模和预分配占用
    std::size_t NodeCount{0};
    std::size_t ReservedNodeCapacity{0};
    std::size_t NodeStorageBytes{0};
    std::size_t NodeStorageArrayCount{0};
    // 活动叶和 internal 节点的生命周期分类
    // 当前活动三角形和拓扑操作统计
    std::size_t ActiveTriangleCount{0};
    std::size_t OriginalTriangleCount{0};
    std::size_t SubdividedTriangleCount{0};
    std::size_t RebuiltTriangleCount{0};
    // split/merge 以及局部约束传播统计
    std::size_t ActiveSplitCount{0};
    // 本帧新增、回收和 forced split 的数量
    std::size_t SplitCount{0};
    std::size_t ForcedSplitCount{0};
    std::size_t MergeCount{0};
    // 裂缝风险和约束传播结果
    std::size_t CrackRiskCount{0};
    std::size_t ConstraintPassCount{0};
    // 候选峰值、拒绝次数和裂缝验证统计
    std::size_t CandidatePeakCount{0};
    std::size_t RejectedSplitCount{0};
    std::size_t BudgetRejectedSplitCount{0};
    std::size_t RejectedMergeCount{0};
    std::size_t TjunctionCount{0};
    std::size_t InvalidNeighborCount{0};
    std::size_t InvalidTopologyCount{0};
    // 评分 pass 使用的 worker 和候选数量
    // Q_s priority refresh 实际重新计算的 active leaf 数量
    std::size_t ErrorEvaluationCount{0};
    // 本帧实际采用的评分 worker 数
    std::size_t ErrorEvaluationWorkerCount{0};
    // collect/mark worker 统计保持统一接口
    std::size_t CollectWorkerCount{0};
    std::size_t CandidateMarkWorkerCount{0};
    // mesh emit 和持久队列统计
    // mesh emit 阶段采用的 worker 数
    std::size_t EmitWorkerCount{0};
    // 持久 Mesh 的完整重建、增量重写、复用和 dirty range 数量
    std::size_t MeshFullRebuildCount{0};
    std::size_t MeshUpdatedTriangleCount{0};
    std::size_t MeshReusedTriangleCount{0};
    std::size_t MeshDirtyRangeCount{0};
    // Q_s/Q_m 成员规模和 topology chunk 规模
    std::size_t SplitCandidateCount{0};
    // 持久 split/merge queue 在帧边界保留成员
    std::size_t MergeCandidateCount{0};
    std::size_t PersistentSplitQueueSize{0};
    std::size_t PersistentMergeQueueSize{0};
    std::size_t QueueCrossoverCount{0};
    std::size_t QueueMembershipUpdateCount{0};
    // topology chunk 的并行覆盖统计
    // 并行 topology 的候选数量、chunk 数量和 worker 数量
    std::size_t TopologyChunkCount{0};
    std::size_t TopologyCommitWorkerCount{0};
    // split 和 merge 使用独立的并行阈值
    std::size_t TopologyCommitMinCandidateCount{0};
    std::size_t SplitTopologyCommitMinCandidateCount{0};
    std::size_t MergeTopologyCommitMinCandidateCount{0};
    // 非空 chunk 数量决定实际可并行的任务数
    std::size_t SplitTopologyCandidateCount{0};
    std::size_t SplitTopologyNonEmptyChunkCount{0};
    std::size_t SplitTopologyCommitWorkerCount{0};
    std::size_t MergeTopologyCandidateCount{0};
    std::size_t MergeTopologyNonEmptyChunkCount{0};
    std::size_t MergeTopologyCommitWorkerCount{0};
    // split/merge 的 interior、boundary 和并行提交数量
    std::size_t InteriorSplitCandidateCount{0};
    std::size_t BoundarySplitCandidateCount{0};
    std::size_t InteriorMergeCandidateCount{0};
    std::size_t BoundaryMergeCandidateCount{0};
    std::size_t ParallelSplitCommitCount{0};
    std::size_t ParallelMergeCommitCount{0};
    // Build 和 mesh 输出的总体计时
    float UpdateMilliseconds{0.0F};
    float PrepareMilliseconds{0.0F};
    float BudgetLeafCollectMilliseconds{0.0F};
    float FinalLeafCollectMilliseconds{0.0F};
    float MeshEmitMilliseconds{0.0F};
    float FinalizeMilliseconds{0.0F};
    // 保留旧报告字段；评分已归入 Q_s/Q_m refresh
    // 旧版误差统计字段，保留用于兼容报告格式
    float ErrorEvaluationSingleThreadMilliseconds{0.0F};
    float ErrorEvaluationParallelMilliseconds{0.0F};
    // 候选标记和队列刷新计时
    float ActiveLeafCollectMilliseconds{0.0F};
    float SplitCandidateMarkMilliseconds{0.0F};
    float MergeCandidateMarkMilliseconds{0.0F};
    // 六段 topology 计时用于解释 chunk、队列和串行收敛成本
    // split topology 的六段计时
    float SplitTopologyChunkBuildMilliseconds{0.0F};
    float SplitTopologyQueueInvalidationMilliseconds{0.0F};
    float SplitTopologyParallelCommitMilliseconds{0.0F};
    float SplitTopologyResultMergeMilliseconds{0.0F};
    float SplitTopologyIndexQueueRefreshMilliseconds{0.0F};
    float SplitTopologySerialConvergenceMilliseconds{0.0F};
    // merge topology 的六段计时
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
} // namespace ParallelRoam::Algorithms::DataOrientedRoam
