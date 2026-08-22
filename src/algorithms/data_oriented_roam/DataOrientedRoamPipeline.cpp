#include "algorithms/data_oriented_roam/DataOrientedRoamPipeline.h"

#include "algorithms/ITerrainLodAlgorithm.h"
#include "algorithms/RoamNestedWedgie.h"
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
constexpr int MaximumSupportedDepth = 20;

TerrainLodPassFallbackReason AutomaticFallback(std::size_t workCount, std::size_t workerCount)
{
    // 没有工作与工作量不足是两种不同事实
    // 后续模型可以据此区分空阶段和串行更合适的小阶段
    if (workCount == 0U)
    {
        return TerrainLodPassFallbackReason::NoWork;
    }
    return workerCount <= 1U
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
    mergeScore.RequestedAction = TerrainLodPassAction::Automatic;
    mergeScore.EffectiveAction = stats.MergeCandidateMarkWorkerCount > 1U
        ? TerrainLodPassAction::ParallelFullRefresh
        : TerrainLodPassAction::SerialFullRefresh;
    mergeScore.FallbackReason = AutomaticFallback(
        stats.MergeScoreEntryCount,
        stats.MergeCandidateMarkWorkerCount);
    mergeScore.MembershipUpdate = TerrainLodMembershipUpdateMode::Incremental;
    mergeScore.PriorityRefresh = TerrainLodPriorityRefreshMode::FullAllCurrentEntries;
    mergeScore.RequestedWorkerCount = state.Settings.ErrorEvaluationWorkerCount;
    mergeScore.EffectiveWorkerCount = stats.MergeCandidateMarkWorkerCount;
    mergeScore.CandidateCount = stats.MergeScoreEntryCount;
    mergeScore.WallMilliseconds = stats.MergeCandidateMarkMilliseconds;

    // Q_s 与 Q_m 的刷新语义相同，分别保存条目数量和实际线程数量
    TerrainLodPassTrace& splitScore = TerrainLodPassTraceFor(
        stats.PassTraces,
        TerrainLodPassId::SplitScore);
    splitScore.RequestedAction = TerrainLodPassAction::Automatic;
    splitScore.EffectiveAction = stats.SplitCandidateMarkWorkerCount > 1U
        ? TerrainLodPassAction::ParallelFullRefresh
        : TerrainLodPassAction::SerialFullRefresh;
    splitScore.FallbackReason = AutomaticFallback(
        stats.SplitScoreEntryCount,
        stats.SplitCandidateMarkWorkerCount);
    splitScore.MembershipUpdate = TerrainLodMembershipUpdateMode::Incremental;
    splitScore.PriorityRefresh = TerrainLodPriorityRefreshMode::FullAllCurrentEntries;
    splitScore.RequestedWorkerCount = state.Settings.ErrorEvaluationWorkerCount;
    splitScore.EffectiveWorkerCount = stats.SplitCandidateMarkWorkerCount;
    splitScore.CandidateCount = stats.SplitScoreEntryCount;
    splitScore.WallMilliseconds = stats.SplitCandidateMarkMilliseconds;

    // 合并只把安全候选交给线程处理
    // 结果整理、索引队列刷新和最终收敛仍属于同一包络
    TerrainLodPassTrace& mergeTopology = TerrainLodPassTraceFor(
        stats.PassTraces,
        TerrainLodPassId::MergeTopology);
    mergeTopology.RequestedAction = TerrainLodPassAction::Automatic;
    mergeTopology.EffectiveAction = stats.MergeTopologyCommitWorkerCount > 1U
        ? TerrainLodPassAction::ParallelAssisted
        : TerrainLodPassAction::SerialImmediate;
    mergeTopology.FallbackReason = AutomaticFallback(
        stats.MergeCandidateCount,
        stats.MergeTopologyCommitWorkerCount);
    mergeTopology.MembershipUpdate = TerrainLodMembershipUpdateMode::Incremental;
    mergeTopology.DataUpdate = TerrainLodDataUpdateMode::Incremental;
    mergeTopology.RequestedWorkerCount = state.Settings.ErrorEvaluationWorkerCount;
    mergeTopology.EffectiveWorkerCount = stats.MergeTopologyCommitWorkerCount;
    mergeTopology.CandidateCount = stats.MergeCandidateCount;
    mergeTopology.WallMilliseconds =
        stats.MergeTopologyChunkBuildMilliseconds +
        stats.MergeTopologyQueueInvalidationMilliseconds +
        stats.MergeTopologyParallelCommitMilliseconds +
        stats.MergeTopologyResultMergeMilliseconds +
        stats.MergeTopologyIndexQueueRefreshMilliseconds +
        stats.MergeTopologySerialConvergenceMilliseconds;

    // 旧开关只控制细分候选快照和并行辅助部分
    // 关闭时 Q_s 评分仍可能并行，因此这里只映射拓扑阶段
    TerrainLodPassTrace& splitTopology = TerrainLodPassTraceFor(
        stats.PassTraces,
        TerrainLodPassId::SplitTopology);
    splitTopology.RequestedAction = state.Settings.EnableParallelSplit
        ? TerrainLodPassAction::ParallelAssisted
        : TerrainLodPassAction::SerialImmediate;
    splitTopology.EffectiveAction = stats.SplitTopologyCommitWorkerCount > 1U
        ? TerrainLodPassAction::ParallelAssisted
        : TerrainLodPassAction::SerialImmediate;
    const std::size_t splitTopologyCandidateCount = state.Settings.EnableParallelSplit
        ? stats.SplitCandidateCount
        : stats.SplitScoreEntryCount;
    // 串行旧路径没有候选快照，只能记录本帧评分条目规模
    // ParallelDisabled 不作为失败，因为这正是调用方请求的固定行为
    splitTopology.FallbackReason = state.Settings.EnableParallelSplit
        ? AutomaticFallback(splitTopologyCandidateCount, stats.SplitTopologyCommitWorkerCount)
        : TerrainLodPassFallbackReason::None;
    splitTopology.MembershipUpdate = TerrainLodMembershipUpdateMode::Incremental;
    splitTopology.DataUpdate = TerrainLodDataUpdateMode::Incremental;
    splitTopology.RequestedWorkerCount = state.Settings.EnableParallelSplit
        ? state.Settings.ErrorEvaluationWorkerCount
        : 1U;
    splitTopology.EffectiveWorkerCount = state.Settings.EnableParallelSplit
        ? stats.SplitTopologyCommitWorkerCount
        : (splitTopologyCandidateCount == 0U ? 0U : 1U);
    splitTopology.CandidateCount = splitTopologyCandidateCount;
    splitTopology.WallMilliseconds =
        stats.SplitTopologyChunkBuildMilliseconds +
        stats.SplitTopologyQueueInvalidationMilliseconds +
        stats.SplitTopologyParallelCommitMilliseconds +
        stats.SplitTopologyResultMergeMilliseconds +
        stats.SplitTopologyIndexQueueRefreshMilliseconds +
        stats.SplitTopologySerialConvergenceMilliseconds;

    // DOD 始终通过脏槽位写入网格，超过阈值时只改变写入线程数量
    // 首帧的脏槽位覆盖完整活动网格，因此结果范围单独标为 Full
    TerrainLodPassTrace& meshEmit = TerrainLodPassTraceFor(
        stats.PassTraces,
        TerrainLodPassId::MeshEmit);
    meshEmit.RequestedAction = TerrainLodPassAction::Automatic;
    meshEmit.EffectiveAction = stats.EmitWorkerCount > 1U
        ? TerrainLodPassAction::ParallelDirty
        : TerrainLodPassAction::SerialDirty;
    meshEmit.FallbackReason = AutomaticFallback(
        stats.MeshUpdatedTriangleCount,
        stats.EmitWorkerCount);
    meshEmit.DataUpdate = stats.MeshFullRebuildCount == 0U
        ? TerrainLodDataUpdateMode::Incremental
        : TerrainLodDataUpdateMode::Full;
    meshEmit.RequestedWorkerCount = state.Settings.ErrorEvaluationWorkerCount;
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
    state.Stats.ActiveLeafHash = HashTerrainLodPathIds(std::move(leafPathIds));
    // 当前阶段先保存精确网格顺序，后续策略比较再增加规范化等价哈希
    state.Stats.MeshHash = HashTerrainLodMesh(state.IncrementalMesh.Data);
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

void DataOrientedRoamPipeline::BuildInternal(
    const Terrain::HeightMap& heightMap,
    float terrainSize,
    float heightScale,
    const TerrainLodViewInput& view,
    const DataOrientedRoamSettings& settings)
{
    DataOrientedRoamState& state = *_state;
    Tools::PerformanceTimer updateTimer;
    ++state.BuildSequence;

    DataOrientedRoamSettings normalizedSettings = settings;
    normalizedSettings.MaxDepth = std::clamp(normalizedSettings.MaxDepth, 0, MaximumSupportedDepth);
    normalizedSettings.TriangleBudget = std::max<std::size_t>(normalizedSettings.TriangleBudget, 2U);
    const int varianceTreeDepth = Roam::ResolveNestedWedgieTreeDepth(
        heightMap.Width(),
        heightMap.Height(),
        normalizedSettings.MaxDepth,
        MaximumSupportedDepth);
    const bool resetTopology = NeedsTopologyReset(state, heightMap, terrainSize, heightScale, normalizedSettings);
    const bool rebuildVarianceTrees =
        state.VarianceHeightMap != &heightMap || state.VarianceTreeMaxDepth != varianceTreeDepth;
    state.HeightMap = &heightMap;
    state.Settings = normalizedSettings;
    // 合并阈值不得高于细分阈值，否则同一帧可能反复展开和回收同一区域
    state.Settings.MergeThreshold = std::min(state.Settings.MergeThreshold, state.Settings.SplitThreshold);
    state.Stats = {};
    state.CurrentSplitPaths.clear();
    state.ViewProjection = view.ViewProjection;
    state.FrustumPlanes = view.FrustumPlanes;
    state.DrawableWidth = std::max(view.DrawableWidth, 1U);
    state.DrawableHeight = std::max(view.DrawableHeight, 1U);
    state.TerrainSize = terrainSize;
    state.HeightScale = heightScale;
    BeginIncrementalMeshUpdate(state, resetTopology);

    if (!heightMap.IsValid())
    {
        // 无效高度图返回空网格，与 Classic 生成器保持相同失败行为
        ResetIncrementalMeshStorage(state);
        return;
    }

    if (rebuildVarianceTrees)
    {
        RebuildVarianceTrees(state, varianceTreeDepth);
    }

    if (resetTopology)
    {
        // 只有高度图、深度或预算等变化使现有状态不能继续使用时才清空跨帧数据
        ResetIncrementalMeshStorage(state);
    }
    // 网格重置会清空节点到槽位的对应关系，因此要在重置后统一预留节点和网格容量
    ReserveNodePool(state);
    if (resetTopology)
    {
        ResetTopology(state);
    }
    else if (rebuildVarianceTrees)
    {
        // 扩展误差树不会改变现有拓扑，但全部节点必须重新读取几何误差
        RefreshNodeVarianceErrors(state);
    }
    const float prepareMilliseconds = updateTimer.ElapsedMilliseconds();

    Tools::PerformanceTimer mergeTimer;
    // 先合并误差已经降低的旧细节，为后续细分腾出三角形名额
    MergeWithDiamondQueue(state);
    const float mergeMilliseconds = mergeTimer.Stop();

    Tools::PerformanceTimer splitTimer;
    // 刷新 Q_s 中保留的分数并复制当前候选，再执行细分和必要的预算调整
    RefineWithSplitQueue(state);
    const float splitMilliseconds = splitTimer.Stop();

    // 细分和合并会同步维护 ActiveLeafNodes，拓扑稳定后直接复用这份最终活动叶集合
    // 网格提交和统计无需再从两个根节点递归遍历或复制
    const std::vector<DataOrientedRoamNodeIndex>& finalActiveLeaves = state.ActiveLeafNodes;
    Tools::PerformanceTimer meshEmitTimer;
    ApplyIncrementalMeshUpdates(state);
    FinalizeIncrementalMeshUpdate(state);
    const float meshEmitMilliseconds = meshEmitTimer.Stop();

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
    state.Stats.MergeMilliseconds = mergeMilliseconds + state.Stats.MergeCrossoverMilliseconds;
    state.Stats.SplitMilliseconds = splitMilliseconds;
    state.Stats.EmitMilliseconds = meshEmitMilliseconds;
    state.Stats.PrepareMilliseconds = prepareMilliseconds;
    // DOD 直接使用 Q_s 成员数量计算预算，不再单独遍历活动叶
    state.Stats.BudgetLeafCollectMilliseconds = 0.0F;
    // 为兼容公共报告保留该字段，DOD 不再执行最终叶集合收集或复制阶段
    state.Stats.FinalLeafCollectMilliseconds = 0.0F;
    state.Stats.MeshEmitMilliseconds = meshEmitMilliseconds;

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
    return _state->IncrementalMesh.UpdateRanges;
}

bool DataOrientedRoamPipeline::MeshRequiresFullUpload() const
{
    return _state->IncrementalMesh.RequiresFullUpload;
}

std::uint64_t DataOrientedRoamPipeline::MeshGeneration() const
{
    return _state->IncrementalMesh.Generation;
}
} // 命名空间 ParallelRoam::Algorithms::DataOrientedRoam
