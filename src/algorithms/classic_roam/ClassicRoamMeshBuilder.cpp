#include "algorithms/classic_roam/ClassicRoamMeshBuilder.h"

#include "algorithms/ITerrainLodAlgorithm.h"
#include "algorithms/RoamNestedWedgie.h"
#include "tools/PerformanceTimer.h"

#include <algorithm>

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
    // nested wedgie tree 的容量随深度指数增长，和现有 UI 的上限保持一致
    normalizedSettings.MaxDepth = std::clamp(normalizedSettings.MaxDepth, 0, MaximumSupportedDepth);
    normalizedSettings.TriangleBudget = std::max<std::size_t>(normalizedSettings.TriangleBudget, 2U);
    const int varianceTreeDepth = Roam::ResolveNestedWedgieTreeDepth(
        heightMap.Width(),
        heightMap.Height(),
        normalizedSettings.MaxDepth,
        MaximumSupportedDepth);
    // reset 判定必须在写入本帧输入前完成
    const bool resetTopology = NeedsTopologyReset(heightMap, terrainSize, heightScale, normalizedSettings);
    const bool rebuildVarianceTrees =
        _varianceHeightMap != &heightMap || _varianceTreeMaxDepth != varianceTreeDepth;
    _heightMap = &heightMap;
    _settings = normalizedSettings;
    // 持久化 bintree 只在输入不兼容时重置
    // 普通相机移动复用旧 child 和 geometric error
    // merge 阈值不能高于 split 阈值，否则同一帧可能反复 split / merge
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
        // 与规则网格 builder 保持空 mesh 失败语义
        ResetIncrementalMeshStorage();
        return _meshData;
    }

    if (rebuildVarianceTrees)
    {
        // topology 节点只缓存 thickness，因此 nested wedgie tree 必须先于节点创建或刷新
        RebuildVarianceTrees(varianceTreeDepth);
    }

    if (resetTopology)
    {
        // 高度图或最大深度不兼容时才清空拓扑，普通相机移动保留树结构
        ResetIncrementalMeshStorage();
        ResetTopology();
    }
    else if (rebuildVarianceTrees)
    {
        // 预计算树扩深时不需要丢弃拓扑，但已有节点必须读取新的 nested wedgie thickness
        RefreshNodeVarianceErrors();
    }
    const float prepareMilliseconds = updateTimer.ElapsedMilliseconds();

    // Q_s/Q_m membership 与 active topology 一起跨帧保留；本帧只刷新 priority 并局部改队列
    OptimizeWithPersistentDualQueues();

    // topology edit 按提交顺序作用到持久 mesh，只重写 split/merge 影响的稠密槽位。
    Tools::PerformanceTimer meshEmitTimer;
    ApplyIncrementalMeshUpdates();
    FinalizeIncrementalMeshUpdate();
    const float meshEmitMilliseconds = meshEmitTimer.Stop();

    if (_settings.EnableTopologyValidation)
    {
        // validator 是调试路径，不参与默认修复逻辑
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
    // 活动 leaf 数由持久 Q_s 直接给出，不再为预算单独递归收集
    _stats.BudgetLeafCollectMilliseconds = 0.0F;
    // 稠密 slot owner 数组就是最终 active leaf 视图，不再递归收集。
    _stats.FinalLeafCollectMilliseconds = 0.0F;
    _stats.MeshEmitMilliseconds = meshEmitMilliseconds;

    CollectActiveSplitPaths();
    // hysteresis 只使用 merge 和 split 完成后的最终 active topology
    _previousSplitPaths = _currentSplitPaths;
    _topologyMaxDepth = _settings.MaxDepth;
    _stats.FinalizeMilliseconds = finalizeTimer.Stop();
    // update 时间覆盖完整算法入口，便于和各互斥阶段总和做 sanity check
    _stats.UpdateMilliseconds = updateTimer.Stop();
    return _meshData;
}
} // 命名空间 ParallelRoam::Algorithms::ClassicRoam
