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
