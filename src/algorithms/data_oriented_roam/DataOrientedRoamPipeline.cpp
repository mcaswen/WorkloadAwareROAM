#include "algorithms/data_oriented_roam/DataOrientedRoamPipeline.h"

#include "algorithms/ITerrainLodAlgorithm.h"
#include "algorithms/data_oriented_roam/DataOrientedRoamPassExecution.h"
#include "algorithms/data_oriented_roam/DataOrientedRoamCandidateMarking.h"
#include "algorithms/data_oriented_roam/DataOrientedRoamMeshEmit.h"
#include "algorithms/data_oriented_roam/DataOrientedRoamQueues.h"
#include "algorithms/data_oriented_roam/DataOrientedRoamScoring.h"
#include "algorithms/data_oriented_roam/DataOrientedRoamState.h"
#include "algorithms/data_oriented_roam/DataOrientedRoamStateOps.h"
#include "algorithms/data_oriented_roam/DataOrientedRoamThreadPool.h"
#include "algorithms/data_oriented_roam/DataOrientedRoamTopology.h"
#include "algorithms/data_oriented_roam/DataOrientedRoamValidation.h"
#include "algorithms/data_oriented_roam/DataOrientedRoamVariance.h"
#include "tools/PerformanceTimer.h"

#include <algorithm>
#include <utility>

namespace ParallelRoam::Algorithms::DataOrientedRoam
{
namespace
{
/// <summary>
/// 将评分策略转换为阶段记录使用的统一动作
/// </summary>
TerrainLodPassAction RequestedScoreAction(TerrainLodScoreRefreshAction action)
{
    switch (action)
    {
    case TerrainLodScoreRefreshAction::Automatic: return TerrainLodPassAction::Automatic;
    case TerrainLodScoreRefreshAction::SerialRefresh: return TerrainLodPassAction::SerialFullRefresh;
    case TerrainLodScoreRefreshAction::ParallelRefresh: return TerrainLodPassAction::ParallelFullRefresh;
    }
    return TerrainLodPassAction::Automatic;
}

/// <summary>
/// 将拓扑策略转换为阶段记录使用的统一动作
/// </summary>
TerrainLodPassAction RequestedTopologyAction(TerrainLodTopologyAction action)
{
    switch (action)
    {
    case TerrainLodTopologyAction::Automatic: return TerrainLodPassAction::Automatic;
    case TerrainLodTopologyAction::SerialImmediate: return TerrainLodPassAction::SerialImmediate;
    case TerrainLodTopologyAction::ParallelAssisted: return TerrainLodPassAction::ParallelAssisted;
    }
    return TerrainLodPassAction::Automatic;
}

/// <summary>
/// 将网格提交策略转换为阶段记录使用的统一动作
/// </summary>
TerrainLodPassAction RequestedMeshAction(TerrainLodMeshEmitAction action)
{
    switch (action)
    {
    case TerrainLodMeshEmitAction::Automatic: return TerrainLodPassAction::Automatic;
    case TerrainLodMeshEmitAction::SerialDirty: return TerrainLodPassAction::SerialDirty;
    case TerrainLodMeshEmitAction::ParallelDirty: return TerrainLodPassAction::ParallelDirty;
    case TerrainLodMeshEmitAction::SerialFull: return TerrainLodPassAction::SerialFull;
    }
    return TerrainLodPassAction::Automatic;
}

/// <summary>
/// 根据工作量和实际线程数量解释并行请求为何退回串行
/// </summary>
TerrainLodPassFallbackReason PolicyFallback(
    std::size_t workCount,
    std::size_t workerCount,
    bool mayRequestParallel)
{
    if (workCount == 0U)
    {
        return TerrainLodPassFallbackReason::NoWork;
    }
    return mayRequestParallel && workerCount <= 1U
        ? TerrainLodPassFallbackReason::BelowParallelThreshold
        : TerrainLodPassFallbackReason::None;
}

void FinalizePassTraces(DataOrientedRoamState& state)
{
    // 这里只把现有阈值和线程解析结果翻译成统一记录
    // 不重新选择线程，也不改变已经完成的算法操作
    DataOrientedRoamStats& stats = state.Stats;
    stats.BuildSequence = state.BuildSequence;
    stats.TriangleBudget = state.Settings.TriangleBudget;

    // Q_m 成员局部维护，当前成员的视点相关分数每帧整批刷新
    // effective 根据评分阶段实际使用的线程数量填写
    TerrainLodPassTrace& mergeScore = TerrainLodPassTraceFor(
        stats.PassTraces,
        TerrainLodPassId::MergeScore);
    mergeScore.RequestedAction = RequestedScoreAction(state.Settings.PassPolicy.MergeScore);
    mergeScore.EffectiveAction = stats.MergeCandidateMarkWorkerCount > 1U
        ? TerrainLodPassAction::ParallelFullRefresh
        : TerrainLodPassAction::SerialFullRefresh;
    mergeScore.FallbackReason = PolicyFallback(
        stats.MergeScoreEntryCount,
        stats.MergeCandidateMarkWorkerCount,
        state.Settings.PassPolicy.MergeScore != TerrainLodScoreRefreshAction::SerialRefresh);
    mergeScore.MembershipUpdate = TerrainLodMembershipUpdateMode::Incremental;
    mergeScore.PriorityRefresh = TerrainLodPriorityRefreshMode::FullAllCurrentEntries;
    mergeScore.RequestedWorkerCount =
        state.Settings.PassPolicy.MergeScore == TerrainLodScoreRefreshAction::SerialRefresh
        ? 1U
        : state.Settings.PassPolicy.MergeScoreWorkerCount;
    mergeScore.EffectiveWorkerCount = stats.MergeCandidateMarkWorkerCount;
    mergeScore.CandidateCount = stats.MergeScoreEntryCount;
    mergeScore.ScoreMilliseconds = stats.MergeScoreMilliseconds;
    mergeScore.HeapifyMilliseconds = stats.MergeHeapifyMilliseconds;
    mergeScore.MembershipUpdateCount = stats.MergeQueueMembershipUpdateCount;
    mergeScore.MembershipUpdateMilliseconds = stats.MergeQueueMembershipUpdateMilliseconds;
    mergeScore.WallMilliseconds = stats.MergeCandidateMarkMilliseconds;

    // Q_s 与 Q_m 的刷新语义相同，分别保存条目数量和实际线程数量
    TerrainLodPassTrace& splitScore = TerrainLodPassTraceFor(
        stats.PassTraces,
        TerrainLodPassId::SplitScore);
    splitScore.RequestedAction = RequestedScoreAction(state.Settings.PassPolicy.SplitScore);
    splitScore.EffectiveAction = stats.SplitCandidateMarkWorkerCount > 1U
        ? TerrainLodPassAction::ParallelFullRefresh
        : TerrainLodPassAction::SerialFullRefresh;
    splitScore.FallbackReason = PolicyFallback(
        stats.SplitScoreEntryCount,
        stats.SplitCandidateMarkWorkerCount,
        state.Settings.PassPolicy.SplitScore != TerrainLodScoreRefreshAction::SerialRefresh);
    splitScore.MembershipUpdate = TerrainLodMembershipUpdateMode::Incremental;
    splitScore.PriorityRefresh = TerrainLodPriorityRefreshMode::FullAllCurrentEntries;
    splitScore.RequestedWorkerCount =
        state.Settings.PassPolicy.SplitScore == TerrainLodScoreRefreshAction::SerialRefresh
        ? 1U
        : state.Settings.PassPolicy.SplitScoreWorkerCount;
    splitScore.EffectiveWorkerCount = stats.SplitCandidateMarkWorkerCount;
    splitScore.CandidateCount = stats.SplitScoreEntryCount;
    splitScore.ScoreMilliseconds = stats.SplitScoreMilliseconds;
    splitScore.HeapifyMilliseconds = stats.SplitHeapifyMilliseconds;
    splitScore.MembershipUpdateCount = stats.SplitQueueMembershipUpdateCount;
    splitScore.MembershipUpdateMilliseconds = stats.SplitQueueMembershipUpdateMilliseconds;
    splitScore.WallMilliseconds = stats.SplitCandidateMarkMilliseconds;

    // 合并只把安全候选交给线程处理
    // 结果整理、索引队列刷新和最终收敛仍属于同一包络
    TerrainLodPassTrace& mergeTopology = TerrainLodPassTraceFor(
        stats.PassTraces,
        TerrainLodPassId::MergeTopology);
    mergeTopology.RequestedAction = RequestedTopologyAction(state.Settings.PassPolicy.MergeTopology);
    const bool serialMergeTopology =
        state.Settings.PassPolicy.MergeTopology == TerrainLodTopologyAction::SerialImmediate;
    const std::size_t mergeTopologyCandidateCount = serialMergeTopology
        ? stats.MergeScoreEntryCount
        : stats.MergeCandidateCount;
    const std::size_t mergeTopologyWorkerCount = serialMergeTopology
        ? (mergeTopologyCandidateCount == 0U ? 0U : 1U)
        : stats.MergeTopologyCommitWorkerCount;
    mergeTopology.EffectiveAction = mergeTopologyWorkerCount > 1U
        ? TerrainLodPassAction::ParallelAssisted
        : TerrainLodPassAction::SerialImmediate;
    mergeTopology.FallbackReason = PolicyFallback(
        mergeTopologyCandidateCount,
        mergeTopologyWorkerCount,
        state.Settings.PassPolicy.MergeTopology != TerrainLodTopologyAction::SerialImmediate);
    mergeTopology.MembershipUpdate = TerrainLodMembershipUpdateMode::Incremental;
    mergeTopology.DataUpdate = TerrainLodDataUpdateMode::Incremental;
    mergeTopology.RequestedWorkerCount =
        state.Settings.PassPolicy.MergeTopology == TerrainLodTopologyAction::SerialImmediate
        ? 1U
        : state.Settings.PassPolicy.MergeTopologyWorkerCount;
    mergeTopology.EffectiveWorkerCount = mergeTopologyWorkerCount;
    mergeTopology.CandidateCount = mergeTopologyCandidateCount;
    mergeTopology.CandidateSnapshotMilliseconds = stats.MergeCandidateSnapshotMilliseconds;
    mergeTopology.WallMilliseconds =
        stats.MergeCandidateSnapshotMilliseconds +
        stats.MergeTopologyChunkBuildMilliseconds +
        stats.MergeTopologyQueueInvalidationMilliseconds +
        stats.MergeTopologyParallelCommitMilliseconds +
        stats.MergeTopologyResultMergeMilliseconds +
        stats.MergeTopologyIndexQueueRefreshMilliseconds +
        stats.MergeTopologySerialConvergenceMilliseconds;

    // 串行策略跳过候选快照和分块准备，直接由主线程读取 Q_s 收敛
    TerrainLodPassTrace& splitTopology = TerrainLodPassTraceFor(
        stats.PassTraces,
        TerrainLodPassId::SplitTopology);
    splitTopology.RequestedAction = RequestedTopologyAction(state.Settings.PassPolicy.SplitTopology);
    const bool serialSplitTopology =
        state.Settings.PassPolicy.SplitTopology == TerrainLodTopologyAction::SerialImmediate;
    const std::size_t splitTopologyCandidateCount = serialSplitTopology
        ? stats.SplitScoreEntryCount
        : stats.SplitCandidateCount;
    const std::size_t splitTopologyWorkerCount = serialSplitTopology
        ? (splitTopologyCandidateCount == 0U ? 0U : 1U)
        : stats.SplitTopologyCommitWorkerCount;
    splitTopology.EffectiveAction = splitTopologyWorkerCount > 1U
        ? TerrainLodPassAction::ParallelAssisted
        : TerrainLodPassAction::SerialImmediate;
    splitTopology.FallbackReason = PolicyFallback(
        splitTopologyCandidateCount,
        splitTopologyWorkerCount,
        state.Settings.PassPolicy.SplitTopology != TerrainLodTopologyAction::SerialImmediate);
    splitTopology.MembershipUpdate = TerrainLodMembershipUpdateMode::Incremental;
    splitTopology.DataUpdate = TerrainLodDataUpdateMode::Incremental;
    splitTopology.RequestedWorkerCount = serialSplitTopology
        ? 1U
        : state.Settings.PassPolicy.SplitTopologyWorkerCount;
    splitTopology.EffectiveWorkerCount = splitTopologyWorkerCount;
    splitTopology.CandidateCount = splitTopologyCandidateCount;
    splitTopology.CandidateSnapshotMilliseconds = stats.SplitCandidateSnapshotMilliseconds;
    splitTopology.WallMilliseconds =
        stats.SplitCandidateSnapshotMilliseconds +
        stats.SplitTopologyChunkBuildMilliseconds +
        stats.SplitTopologyQueueInvalidationMilliseconds +
        stats.SplitTopologyParallelCommitMilliseconds +
        stats.SplitTopologyResultMergeMilliseconds +
        stats.SplitTopologyIndexQueueRefreshMilliseconds +
        stats.SplitTopologySerialConvergenceMilliseconds;

    // 全量策略复用当前槽位和活动叶，只重写全部槽位内容
    TerrainLodPassTrace& meshEmit = TerrainLodPassTraceFor(
        stats.PassTraces,
        TerrainLodPassId::MeshEmit);
    meshEmit.RequestedAction = RequestedMeshAction(state.Settings.PassPolicy.MeshEmit);
    meshEmit.EffectiveAction = state.Settings.PassPolicy.MeshEmit == TerrainLodMeshEmitAction::SerialFull
        ? TerrainLodPassAction::SerialFull
        : (stats.EmitWorkerCount > 1U
            ? TerrainLodPassAction::ParallelDirty
            : TerrainLodPassAction::SerialDirty);
    meshEmit.FallbackReason = PolicyFallback(
        stats.MeshUpdatedTriangleCount,
        stats.EmitWorkerCount,
        state.Settings.PassPolicy.MeshEmit == TerrainLodMeshEmitAction::Automatic ||
        state.Settings.PassPolicy.MeshEmit == TerrainLodMeshEmitAction::ParallelDirty);
    meshEmit.DataUpdate = stats.MeshFullRebuildCount == 0U
        ? TerrainLodDataUpdateMode::Incremental
        : TerrainLodDataUpdateMode::Full;
    meshEmit.RequestedWorkerCount =
        state.Settings.PassPolicy.MeshEmit == TerrainLodMeshEmitAction::SerialDirty ||
        state.Settings.PassPolicy.MeshEmit == TerrainLodMeshEmitAction::SerialFull
        ? 1U
        : state.Settings.PassPolicy.MeshEmitWorkerCount;
    meshEmit.EffectiveWorkerCount = stats.EmitWorkerCount;
    meshEmit.DirtyItemCount = stats.MeshUpdatedTriangleCount;
    meshEmit.WallMilliseconds = stats.MeshEmitMilliseconds;
}

void CollectPassEvidence(DataOrientedRoamState& state)
{
    // 哈希和队列检查需要遍历当前状态，只在研究基准中开启
    // 额外耗时单独保存，不进入任一可比较阶段
    if (!state.Settings.EnablePassEvidence)
    {
        return;
    }

    Tools::PerformanceTimer evidenceTimer;
    // 拓扑哈希只使用稳定 PathId，不使用节点池下标或线程完成顺序
    std::vector<std::uint64_t> splitPathIds;
    splitPathIds.reserve(state.CurrentSplitPaths.size());
    splitPathIds.insert(
        splitPathIds.end(),
        state.CurrentSplitPaths.begin(),
        state.CurrentSplitPaths.end());
    state.Stats.TopologyHash = HashTerrainLodPathIds(std::move(splitPathIds));

    // 活动叶数组允许因提交顺序不同而重排，哈希前统一按 PathId 排序
    std::vector<std::uint64_t> leafPathIds;
    leafPathIds.reserve(state.ActiveLeafNodes.size());
    for (const DataOrientedRoamNodeIndex node : state.ActiveLeafNodes)
    {
        if (state.IsValidNode(node))
        {
            leafPathIds.push_back(state.Nodes.PathIdAt(node));
        }
    }
    state.Stats.ActiveLeafHash = HashTerrainLodPathIds(leafPathIds);

    // 网格槽位顺序可能不同于活动叶数组，规范化时必须使用槽位对应的稳定路径
    std::vector<std::uint64_t> slotPathIds;
    slotPathIds.reserve(state.IncrementalMesh.Metadata.SlotOwners.size());
    for (const DataOrientedRoamNodeIndex node : state.IncrementalMesh.Metadata.SlotOwners)
    {
        if (state.IsValidNode(node))
        {
            slotPathIds.push_back(state.Nodes.PathIdAt(node));
        }
    }
    // 精确哈希用于同一实现重放，规范化哈希用于忽略槽位顺序比较策略结果
    state.Stats.MeshHash = HashTerrainLodMesh(state.IncrementalMesh.Data);
    state.Stats.NormalizedMeshHash = HashTerrainLodNormalizedMesh(
        state.IncrementalMesh.Data,
        slotPathIds);
    if (!state.Settings.EnableTopologyValidation)
    {
        state.Stats.QueueInvariantViolationCount = CountPersistentQueueInvariantViolations(state);
    }
    state.Stats.PassEvidenceMilliseconds = evidenceTimer.Stop();
}
}

DataOrientedRoamPipeline::DataOrientedRoamPipeline()
    : _state(std::make_unique<DataOrientedRoamState>())
    , _threadPool(std::make_unique<DataOrientedRoamThreadPool>())
{
    _state->ThreadPool = _threadPool.get();
}

DataOrientedRoamPipeline::~DataOrientedRoamPipeline() = default;

DataOrientedRoamPipeline::DataOrientedRoamPipeline(DataOrientedRoamPipeline&& other) noexcept
    : _state(std::move(other._state))
    , _threadPool(std::move(other._threadPool))
{
    if (_state != nullptr)
    {
        // 状态只保存线程池地址但不负责释放，移动对象后必须改为指向新对象中的线程池
        _state->ThreadPool = _threadPool.get();
    }
}

DataOrientedRoamPipeline& DataOrientedRoamPipeline::operator=(DataOrientedRoamPipeline&& other) noexcept
{
    if (this == &other)
    {
        return *this;
    }

    _state = std::move(other._state);
    _threadPool = std::move(other._threadPool);
    if (_state != nullptr)
    {
        // 移动赋值会换用另一个线程池，状态中保存的地址也必须同步更新
        _state->ThreadPool = _threadPool.get();
    }

    return *this;
}

const Terrain::TerrainMeshData& DataOrientedRoamPipeline::Build(
    const Terrain::HeightMap& heightMap,
    float terrainSize,
    float heightScale,
    const TerrainLodViewInput& view,
    const DataOrientedRoamSettings& settings)
{
    BuildInternal(heightMap, terrainSize, heightScale, view, settings);
    return _state->IncrementalMesh.Data;
}

const Terrain::TerrainMeshData& DataOrientedRoamPipeline::BuildWithPassObserver(
    const Terrain::HeightMap& heightMap,
    float terrainSize,
    float heightScale,
    const TerrainLodViewInput& view,
    const DataOrientedRoamSettings& settings,
    const DataOrientedRoamPassObserver& observer)
{
    BuildInternal(heightMap, terrainSize, heightScale, view, settings, observer);
    return _state->IncrementalMesh.Data;
}

void DataOrientedRoamPipeline::BuildInternal(
    const Terrain::HeightMap& heightMap,
    float terrainSize,
    float heightScale,
    const TerrainLodViewInput& view,
    const DataOrientedRoamSettings& settings,
    const DataOrientedRoamPassObserver& observer)
{
    DataOrientedRoamState& state = *_state;
    Tools::PerformanceTimer updateTimer;
    if (!PrepareDataOrientedRoamFrame(state, heightMap, terrainSize, heightScale, view, settings))
        return;
    const float prepareMilliseconds = updateTimer.ElapsedMilliseconds();

    ExecuteDataOrientedRoamCpuPasses(state, observer);
    const std::vector<DataOrientedRoamNodeIndex>& finalActiveLeaves = state.ActiveLeafNodes;

    if (state.Settings.EnableTopologyValidation)
    {
        Tools::PerformanceTimer validateTimer;
        ValidateTopology(state);
        ValidateIncrementalMesh(state);
        state.Stats.ValidateMilliseconds = validateTimer.Stop();
    }

    Tools::PerformanceTimer finalizeTimer;
    AccumulateLeafStats(state, finalActiveLeaves);
    state.Stats.PersistentSplitQueueSize = state.SplitQueue.size();
    state.Stats.PersistentMergeQueueSize = state.MergeQueue.size();
    // 为细分腾出预算而执行的合并虽然发生在细分循环内，耗时仍计入合并阶段
    state.Stats.MergeMilliseconds += state.Stats.MergeCrossoverMilliseconds;
    state.Stats.PrepareMilliseconds = prepareMilliseconds;
    // DOD 直接使用 Q_s 成员数量计算预算，不再单独遍历活动叶
    state.Stats.BudgetLeafCollectMilliseconds = 0.0F;
    // 为兼容公共报告保留该字段，DOD 不再执行最终叶集合收集或复制阶段
    state.Stats.FinalLeafCollectMilliseconds = 0.0F;

    CollectActiveSplitPaths(state);
    // 仍处于细分状态的路径集合用于下一帧迟滞判断，必须在合并和细分全部完成后更新
    state.PreviousSplitPaths = state.CurrentSplitPaths;
    state.TopologyMaxDepth = state.Settings.MaxDepth;
    state.Stats.FinalizeMilliseconds = finalizeTimer.Stop();
    state.Stats.UpdateMilliseconds = updateTimer.Stop();
    FinalizePassTraces(state);
    CollectPassEvidence(state);
}

const DataOrientedRoamStats& DataOrientedRoamPipeline::Stats() const
{
    return _state->Stats;
}

const DataOrientedRoamState& DataOrientedRoamPipeline::State() const
{
    return *_state;
}

const std::vector<DataOrientedRoamMeshUpdateRange>& DataOrientedRoamPipeline::MeshUpdateRanges() const
{
    return _state->IncrementalMesh.Metadata.UpdateRanges;
}

bool DataOrientedRoamPipeline::MeshRequiresFullUpload() const
{
    return _state->IncrementalMesh.Metadata.RequiresFullUpload;
}

std::uint64_t DataOrientedRoamPipeline::MeshGeneration() const
{
    return _state->IncrementalMesh.Metadata.Generation;
}
} // 命名空间 ParallelRoam::Algorithms::DataOrientedRoam
