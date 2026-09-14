#include "algorithms/data_oriented_roam/DataOrientedRoamPassExecution.h"
#include "algorithms/ITerrainLodAlgorithm.h"
#include "algorithms/RoamNestedWedgie.h"
#include "algorithms/data_oriented_roam/DataOrientedRoamMeshEmit.h"
#include "algorithms/data_oriented_roam/DataOrientedRoamQueues.h"
#include "algorithms/data_oriented_roam/DataOrientedRoamScoring.h"
#include "algorithms/data_oriented_roam/DataOrientedRoamStateOps.h"
#include "algorithms/data_oriented_roam/DataOrientedRoamTopology.h"
#include "algorithms/data_oriented_roam/DataOrientedRoamVariance.h"
#include "tools/PerformanceTimer.h"
#include "profiling/CpuProfiling.h"

#include <algorithm>
#include <stdexcept>

namespace ParallelRoam::Algorithms::DataOrientedRoam
{
bool PrepareDataOrientedRoamFrame(
    DataOrientedRoamState& state,
    const Terrain::HeightMap& heightMap,
    float terrainSize,
    float heightScale,
    const TerrainLodViewInput& view,
    const DataOrientedRoamSettings& settings)
{
    ROAM_CPU_ZONE("dod.prepare");
    constexpr int MaximumSupportedDepth = 20;
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
        return false;
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
    return true;
}

void ExecuteDataOrientedRoamPass(DataOrientedRoamState& state, TerrainLodPassId passId)
{
    // 拓扑入口只消费本帧已评分队列以保持观察边界与真实输入一致
    switch (passId)
    {
    case TerrainLodPassId::MergeScore:
        { ROAM_CPU_ZONE("dod.merge_score"); RefreshPersistentMergeQueuePriorities(state); break; }
    case TerrainLodPassId::MergeTopology:
        { ROAM_CPU_ZONE("dod.merge_topology"); CommitScoredMergeTopology(state); break; }
    case TerrainLodPassId::SplitScore:
        { ROAM_CPU_ZONE("dod.split_score"); RefreshPersistentSplitQueuePriorities(state); break; }
    case TerrainLodPassId::SplitTopology:
        { ROAM_CPU_ZONE("dod.split_topology"); CommitScoredSplitTopology(state); break; }
    case TerrainLodPassId::MeshEmit:
    {
        ROAM_CPU_ZONE("dod.mesh");
        ApplyIncrementalMeshUpdates(state);
        FinalizeIncrementalMeshUpdate(state);
        break;
    }
    default: throw std::invalid_argument{"Expected a CPU ROAM pass"};
    }
}

void ExecuteDataOrientedRoamCpuPasses(
    DataOrientedRoamState& state,
    const DataOrientedRoamPassObserver& observer)
{
    // 评分和拓扑共同组成旧包络且网格区间收尾仍计入提交阶段
    constexpr TerrainLodPassId order[]{TerrainLodPassId::MergeScore,
        TerrainLodPassId::MergeTopology, TerrainLodPassId::SplitScore,
        TerrainLodPassId::SplitTopology, TerrainLodPassId::MeshEmit};
    state.Stats.MergeMilliseconds = 0.0F;
    state.Stats.SplitMilliseconds = 0.0F;
    for (const auto passId : order)
    {
        // 发现探测可能遍历整份状态但不能污染生产阶段时间
        if (observer)
            observer(state, passId);
        Tools::PerformanceTimer timer;
        ExecuteDataOrientedRoamPass(state, passId);
        const float milliseconds = timer.Stop();
        if (passId == TerrainLodPassId::MergeScore || passId == TerrainLodPassId::MergeTopology)
            state.Stats.MergeMilliseconds += milliseconds;
        else if (passId == TerrainLodPassId::SplitScore || passId == TerrainLodPassId::SplitTopology)
            state.Stats.SplitMilliseconds += milliseconds;
        else
            state.Stats.EmitMilliseconds = state.Stats.MeshEmitMilliseconds = milliseconds;
    }
}
}
