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
        // state 只借用线程池指针，pipeline move 后必须重新绑定
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
        // 移动赋值会替换 owner，旧裸指针不能继续留在 state 中
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
    // merge 阈值不能高于 split 阈值
    // 否则同一帧可能在 hysteresis 区间反复展开和回收
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
        // 保持返回空 mesh 的语义和 Classic builder 一致
        ResetIncrementalMeshStorage(state);
        return;
    }

    if (rebuildVarianceTrees)
    {
        RebuildVarianceTrees(state, varianceTreeDepth);
    }

    if (resetTopology)
    {
        // reset 只发生在缓存误差或深度上限不再兼容时
        ResetIncrementalMeshStorage(state);
    }
    // Mesh reset 会释放 node-to-slot 和 slot owner 容量，因此统一在 reset 之后预留。
    ReserveNodePool(state);
    if (resetTopology)
    {
        ResetTopology(state);
    }
    else if (rebuildVarianceTrees)
    {
        // 预计算树扩深时保留拓扑，但已有节点必须刷新 nested wedgie thickness
        RefreshNodeVarianceErrors(state);
    }
    const float prepareMilliseconds = updateTimer.ElapsedMilliseconds();

    Tools::PerformanceTimer mergeTimer;
    // merge pass 先运行，远处旧细节先回收
    MergeWithDiamondQueue(state);
    const float mergeMilliseconds = mergeTimer.Stop();

    Tools::PerformanceTimer splitTimer;
    // 持久 Q_s 并行刷新 priority 并生成提交快照，随后执行 split/crossover。
    RefineWithSplitQueue(state);
    const float splitMilliseconds = splitTimer.Stop();

    // split/merge 已增量维护 ActiveLeafNodes；拓扑稳定后直接复用这份只读输出视图，
    // 避免为了 emit、统计和 GPU snapshot 再从两个 root 递归遍历或复制活动 leaf。
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
    // 预算交叉 merge 发生在 Split 收敛循环内，但统计上仍属于 Merge topology。
    state.Stats.MergeMilliseconds = mergeMilliseconds + state.Stats.MergeCrossoverMilliseconds;
    state.Stats.SplitMilliseconds = splitMilliseconds;
    state.Stats.EmitMilliseconds = meshEmitMilliseconds;
    state.Stats.PrepareMilliseconds = prepareMilliseconds;
    // DOD 直接用 Q_s.size() 计算预算，不再有独立的 leaf collect。
    state.Stats.BudgetLeafCollectMilliseconds = 0.0F;
    // 字段为统一报告 schema 保留；DOD 不再执行最终 leaf collect/copy pass。
    state.Stats.FinalLeafCollectMilliseconds = 0.0F;
    state.Stats.MeshEmitMilliseconds = meshEmitMilliseconds;

    CollectActiveSplitPaths(state);
    // split path 集合是 hysteresis 的跨帧状态
    // 必须在 merge 和 split 都完成后再更新
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
