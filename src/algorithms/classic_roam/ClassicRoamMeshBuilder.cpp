#include "algorithms/classic_roam/ClassicRoamMeshBuilder.h"

#include "algorithms/ITerrainLodAlgorithm.h"
#include "algorithms/RoamNestedWedgie.h"
#include "tools/PerformanceTimer.h"

#include <algorithm>
#include <vector>

namespace ParallelRoam::Algorithms::ClassicRoam
{
namespace
{
constexpr int MaximumSupportedDepth = 20;
} // 匿名命名空间

const Terrain::TerrainMeshData& ClassicRoamMeshBuilder::Build(
    const Terrain::HeightMap& heightMap,
    float terrainSize,
    float heightScale,
    const TerrainLodViewInput& view,
    const ClassicRoamSettings& settings)
{
    Tools::PerformanceTimer updateTimer;
    ++_buildSequence;
    ClassicRoamSettings normalizedSettings = settings;
    // 误差树容量随深度指数增长，因此使用与界面相同的安全上限
    normalizedSettings.MaxDepth = std::clamp(normalizedSettings.MaxDepth, 0, MaximumSupportedDepth);
    normalizedSettings.TriangleBudget = std::max<std::size_t>(normalizedSettings.TriangleBudget, 2U);
    const int varianceTreeDepth = Roam::ResolveNestedWedgieTreeDepth(
        heightMap.Width(),
        heightMap.Height(),
        normalizedSettings.MaxDepth,
        MaximumSupportedDepth);
    // 先用上一帧状态判断是否需要重置，避免新输入覆盖比较基准
    const bool resetTopology = NeedsTopologyReset(heightMap, terrainSize, heightScale, normalizedSettings);
    const bool rebuildVarianceTrees =
        _varianceHeightMap != &heightMap || _varianceTreeMaxDepth != varianceTreeDepth;
    _heightMap = &heightMap;
    _settings = normalizedSettings;
    // 只有高度图、世界尺度、深度或预算不兼容时才重置跨帧保留的二叉三角树
    // 普通相机移动继续复用已有子节点和几何误差
    // 合并阈值不得高于细分阈值，否则同一帧可能反复细分和合并
    _settings.MergeThreshold = std::min(_settings.MergeThreshold, _settings.SplitThreshold);
    _stats = {};
    _currentSplitPaths.clear();
    _viewProjection = view.ViewProjection;
    _frustumPlanes = view.FrustumPlanes;
    _drawableWidth = std::max(view.DrawableWidth, 1U);
    _drawableHeight = std::max(view.DrawableHeight, 1U);
    _terrainSize = terrainSize;
    _heightScale = heightScale;
    BeginIncrementalMeshUpdate(resetTopology);

    if (!heightMap.IsValid())
    {
        // 无效高度图返回空网格，与规则网格生成器的失败行为保持一致
        ResetIncrementalMeshStorage();
        return _meshData;
    }

    if (rebuildVarianceTrees)
    {
        // 拓扑节点只缓存误差树中的结果，因此必须先重建误差树再创建或刷新节点
        RebuildVarianceTrees(varianceTreeDepth);
    }

    if (resetTopology)
    {
        // 输入结构发生变化时重建拓扑，普通相机移动则保留树结构
        ResetIncrementalMeshStorage();
        ResetTopology();
    }
    else if (rebuildVarianceTrees)
    {
        // 误差树扩深不会改变已有拓扑，但所有现有节点必须重新读取几何误差
        RefreshNodeVarianceErrors();
    }
    const float prepareMilliseconds = updateTimer.ElapsedMilliseconds();

    // Q_s 和 Q_m 随活动拓扑跨帧保留，本帧只刷新分数并维护受拓扑变化影响的成员
    OptimizeWithPersistentDualQueues();

    // 按拓扑修改顺序更新跨帧保留的网格，只重写细分或合并影响的连续槽位
    Tools::PerformanceTimer meshEmitTimer;
    ApplyIncrementalMeshUpdates();
    FinalizeIncrementalMeshUpdate();
    const float meshEmitMilliseconds = meshEmitTimer.Stop();

    if (_settings.EnableTopologyValidation)
    {
        // 验证器只报告问题，不改变正常更新路径的拓扑
        Tools::PerformanceTimer validateTimer;
        ValidateTopology();
        _stats.ValidateMilliseconds = validateTimer.Stop();
    }

    Tools::PerformanceTimer finalizeTimer;
    AccumulateLeafStats(_meshData, _meshSlotOwners);
    _stats.MergeMilliseconds =
        _stats.MergeCandidateMarkMilliseconds + _stats.MergeTopologyMilliseconds;
    _stats.SplitMilliseconds =
        _stats.SplitInitialScanMilliseconds + _stats.SplitSerialConvergenceMilliseconds;
    _stats.EmitMilliseconds = meshEmitMilliseconds;
    _stats.PrepareMilliseconds = prepareMilliseconds;
    // 活动叶数量可直接从跨帧保留的 Q_s 获得，无需为预算再次遍历拓扑
    _stats.BudgetLeafCollectMilliseconds = 0.0F;
    // 按槽位记录叶节点的连续数组已经包含最终活动叶集合，无需再次递归收集
    _stats.FinalLeafCollectMilliseconds = 0.0F;
    _stats.MeshEmitMilliseconds = meshEmitMilliseconds;

    CollectActiveSplitPaths();
    // 下一帧迟滞判断只复用合并和细分全部完成后的最终拓扑
    _previousSplitPaths = _currentSplitPaths;
    _topologyMaxDepth = _settings.MaxDepth;
    _stats.FinalizeMilliseconds = finalizeTimer.Stop();
    // 总更新时间覆盖本次完整更新，可用于核对各阶段计时是否完整
    _stats.UpdateMilliseconds = updateTimer.Stop();
    FinalizePassTraces();
    CollectPassEvidence();
    return _meshData;
}

void ClassicRoamMeshBuilder::FinalizePassTraces()
{
    // Classic 的所有算法阶段都固定在调用线程执行
    // requested 与 effective 相同，作为 DOD 内部策略的外部串行对照
    _stats.BuildSequence = _buildSequence;
    _stats.TriangleBudget = _settings.TriangleBudget;

    // 经典对象式路径只提供串行实现，但仍保留调用方请求值作为外部基线证据
    // 请求并行时实际动作保持串行，并明确记录实现不支持并行
    const auto requestedScoreAction = [](TerrainLodScoreRefreshAction action) {
        switch (action)
        {
        case TerrainLodScoreRefreshAction::Automatic: return TerrainLodPassAction::Automatic;
        case TerrainLodScoreRefreshAction::SerialRefresh: return TerrainLodPassAction::SerialFullRefresh;
        case TerrainLodScoreRefreshAction::ParallelRefresh: return TerrainLodPassAction::ParallelFullRefresh;
        }
        return TerrainLodPassAction::Automatic;
    };
    // 拓扑动作采用相同映射，使两个算法的阶段记录可以共用一套字段
    const auto requestedTopologyAction = [](TerrainLodTopologyAction action) {
        switch (action)
        {
        case TerrainLodTopologyAction::Automatic: return TerrainLodPassAction::Automatic;
        case TerrainLodTopologyAction::SerialImmediate: return TerrainLodPassAction::SerialImmediate;
        case TerrainLodTopologyAction::ParallelAssisted: return TerrainLodPassAction::ParallelAssisted;
        }
        return TerrainLodPassAction::Automatic;
    };
    // 空阶段与不支持并行是两种不同回退原因
    const auto serialFallback = [](std::size_t workCount, bool requestedParallel) {
        if (workCount == 0U)
        {
            return TerrainLodPassFallbackReason::NoWork;
        }
        return requestedParallel
            ? TerrainLodPassFallbackReason::ParallelDisabled
            : TerrainLodPassFallbackReason::None;
    };

    // 队列成员随拓扑局部维护，但相机变化后会重新评分全部现有成员
    TerrainLodPassTrace& mergeScore = TerrainLodPassTraceFor(
        _stats.PassTraces,
        TerrainLodPassId::MergeScore);
    mergeScore.RequestedAction = requestedScoreAction(_settings.PassPolicy.MergeScore);
    mergeScore.EffectiveAction = TerrainLodPassAction::SerialFullRefresh;
    mergeScore.MembershipUpdate = TerrainLodMembershipUpdateMode::Incremental;
    mergeScore.PriorityRefresh = TerrainLodPriorityRefreshMode::FullAllCurrentEntries;
    mergeScore.RequestedWorkerCount = 1U;
    mergeScore.EffectiveWorkerCount = _stats.MergeScoreEntryCount == 0U ? 0U : 1U;
    mergeScore.CandidateCount = _stats.MergeScoreEntryCount;
    mergeScore.FallbackReason = serialFallback(
        _stats.MergeScoreEntryCount,
        _settings.PassPolicy.MergeScore == TerrainLodScoreRefreshAction::ParallelRefresh);
    mergeScore.WallMilliseconds = _stats.MergeCandidateMarkMilliseconds;

    // Q_s 评分和 Q_m 使用相同语义，只是处理的成员集合不同
    TerrainLodPassTrace& splitScore = TerrainLodPassTraceFor(
        _stats.PassTraces,
        TerrainLodPassId::SplitScore);
    splitScore.RequestedAction = requestedScoreAction(_settings.PassPolicy.SplitScore);
    splitScore.EffectiveAction = TerrainLodPassAction::SerialFullRefresh;
    splitScore.MembershipUpdate = TerrainLodMembershipUpdateMode::Incremental;
    splitScore.PriorityRefresh = TerrainLodPriorityRefreshMode::FullAllCurrentEntries;
    splitScore.RequestedWorkerCount = 1U;
    splitScore.EffectiveWorkerCount = _stats.SplitScoreEntryCount == 0U ? 0U : 1U;
    splitScore.CandidateCount = _stats.SplitScoreEntryCount;
    splitScore.FallbackReason = serialFallback(
        _stats.SplitScoreEntryCount,
        _settings.PassPolicy.SplitScore == TerrainLodScoreRefreshAction::ParallelRefresh);
    splitScore.WallMilliseconds = _stats.SplitInitialScanMilliseconds;

    // Classic 在一个收敛循环中交叉处理合并和细分
    // 两类事务仍使用各自已有计时，不能把整段循环重复计入两边
    TerrainLodPassTrace& mergeTopology = TerrainLodPassTraceFor(
        _stats.PassTraces,
        TerrainLodPassId::MergeTopology);
    mergeTopology.RequestedAction = requestedTopologyAction(_settings.PassPolicy.MergeTopology);
    mergeTopology.EffectiveAction = TerrainLodPassAction::SerialImmediate;
    mergeTopology.MembershipUpdate = TerrainLodMembershipUpdateMode::Incremental;
    mergeTopology.DataUpdate = TerrainLodDataUpdateMode::Incremental;
    mergeTopology.RequestedWorkerCount = 1U;
    mergeTopology.EffectiveWorkerCount = _stats.MergeScoreEntryCount == 0U ? 0U : 1U;
    mergeTopology.CandidateCount = _stats.MergeScoreEntryCount;
    mergeTopology.FallbackReason = serialFallback(
        _stats.MergeScoreEntryCount,
        _settings.PassPolicy.MergeTopology == TerrainLodTopologyAction::ParallelAssisted);
    mergeTopology.WallMilliseconds = _stats.MergeTopologyMilliseconds;

    // 强制细分和预算交换都属于串行细分拓扑包络
    TerrainLodPassTrace& splitTopology = TerrainLodPassTraceFor(
        _stats.PassTraces,
        TerrainLodPassId::SplitTopology);
    splitTopology.RequestedAction = requestedTopologyAction(_settings.PassPolicy.SplitTopology);
    splitTopology.EffectiveAction = TerrainLodPassAction::SerialImmediate;
    splitTopology.MembershipUpdate = TerrainLodMembershipUpdateMode::Incremental;
    splitTopology.DataUpdate = TerrainLodDataUpdateMode::Incremental;
    splitTopology.RequestedWorkerCount = 1U;
    splitTopology.EffectiveWorkerCount = _stats.SplitScoreEntryCount == 0U ? 0U : 1U;
    splitTopology.CandidateCount = _stats.SplitScoreEntryCount;
    splitTopology.FallbackReason = serialFallback(
        _stats.SplitScoreEntryCount,
        _settings.PassPolicy.SplitTopology == TerrainLodTopologyAction::ParallelAssisted);
    splitTopology.WallMilliseconds = _stats.SplitSerialConvergenceMilliseconds;

    // 首帧仍走脏槽位提交机制，但脏集合覆盖全部活动叶
    // DataUpdate 单独记录该帧在结果范围上属于全量初始化
    TerrainLodPassTrace& meshEmit = TerrainLodPassTraceFor(
        _stats.PassTraces,
        TerrainLodPassId::MeshEmit);
    switch (_settings.PassPolicy.MeshEmit)
    {
    case TerrainLodMeshEmitAction::Automatic:
        meshEmit.RequestedAction = TerrainLodPassAction::Automatic;
        break;
    case TerrainLodMeshEmitAction::SerialDirty:
        meshEmit.RequestedAction = TerrainLodPassAction::SerialDirty;
        break;
    case TerrainLodMeshEmitAction::ParallelDirty:
        meshEmit.RequestedAction = TerrainLodPassAction::ParallelDirty;
        break;
    case TerrainLodMeshEmitAction::SerialFull:
        meshEmit.RequestedAction = TerrainLodPassAction::SerialFull;
        break;
    }
    meshEmit.EffectiveAction = _settings.PassPolicy.MeshEmit == TerrainLodMeshEmitAction::SerialFull
        ? TerrainLodPassAction::SerialFull
        : TerrainLodPassAction::SerialDirty;
    meshEmit.DataUpdate = _stats.MeshFullRebuildCount == 0U
        ? TerrainLodDataUpdateMode::Incremental
        : TerrainLodDataUpdateMode::Full;
    meshEmit.RequestedWorkerCount = 1U;
    meshEmit.EffectiveWorkerCount = _stats.MeshUpdatedTriangleCount == 0U ? 0U : 1U;
    meshEmit.DirtyItemCount = _stats.MeshUpdatedTriangleCount;
    meshEmit.FallbackReason = serialFallback(
        _stats.MeshUpdatedTriangleCount,
        _settings.PassPolicy.MeshEmit == TerrainLodMeshEmitAction::ParallelDirty);
    meshEmit.WallMilliseconds = _stats.MeshEmitMilliseconds;
}

void ClassicRoamMeshBuilder::CollectPassEvidence()
{
    // 普通交互帧不执行下面的全量遍历
    // 基准测试会把这段额外成本单独记录，避免混入算法阶段耗时
    if (!_settings.EnablePassEvidence)
    {
        return;
    }

    Tools::PerformanceTimer evidenceTimer;
    // 活动内部路径不依赖指针地址，排序后可跨 Reset 比较
    std::vector<std::uint64_t> splitPathIds;
    splitPathIds.reserve(_currentSplitPaths.size());
    splitPathIds.insert(splitPathIds.end(), _currentSplitPaths.begin(), _currentSplitPaths.end());
    _stats.TopologyHash = HashTerrainLodPathIds(std::move(splitPathIds));

    // 活动叶按 PathId 规范化，网格槽位交换不会改变该哈希
    std::vector<std::uint64_t> leafPathIds;
    leafPathIds.reserve(_meshSlotOwners.size());
    for (const ClassicRoamNode* node : _meshSlotOwners)
    {
        if (node != nullptr)
        {
            leafPathIds.push_back(node->PathId);
        }
    }
    _stats.ActiveLeafHash = HashTerrainLodPathIds(leafPathIds);
    // 网格哈希保留当前顶点和索引顺序，用于同一算法的确定性重放
    _stats.MeshHash = HashTerrainLodMesh(_meshData);
    _stats.NormalizedMeshHash = HashTerrainLodNormalizedMesh(_meshData, leafPathIds);
    if (!_settings.EnableTopologyValidation)
    {
        _stats.QueueInvariantViolationCount = CountPersistentQueueInvariantViolations(_meshSlotOwners);
    }
    _stats.PassEvidenceMilliseconds = evidenceTimer.Stop();
}
} // 命名空间 ParallelRoam::Algorithms::ClassicRoam
