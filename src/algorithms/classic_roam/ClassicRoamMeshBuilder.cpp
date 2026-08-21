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
    return _meshData;
}
} // 命名空间 ParallelRoam::Algorithms::ClassicRoam
