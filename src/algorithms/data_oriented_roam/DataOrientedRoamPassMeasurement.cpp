#include "algorithms/data_oriented_roam/DataOrientedRoamPassMeasurement.h"

#include "algorithms/data_oriented_roam/DataOrientedRoamPassEvidence.h"
#include "algorithms/data_oriented_roam/DataOrientedRoamPassExecution.h"
#include "algorithms/data_oriented_roam/DataOrientedRoamPassInput.h"
#include "algorithms/data_oriented_roam/DataOrientedRoamQueues.h"
#include "algorithms/data_oriented_roam/DataOrientedRoamState.h"
#include "algorithms/data_oriented_roam/DataOrientedRoamThreadPool.h"
#include "algorithms/data_oriented_roam/DataOrientedRoamTopology.h"

#include <algorithm>
#include <limits>
#include <memory>
#include <stdexcept>
#include <vector>

namespace ParallelRoam::Algorithms::DataOrientedRoam
{
namespace
{
using Sample = DataOrientedRoamPassExperimentSample;

bool IsParallel(TerrainLodPassAction action)
{
    return action == TerrainLodPassAction::ParallelFullRefresh ||
        action == TerrainLodPassAction::ParallelAssisted || action == TerrainLodPassAction::ParallelDirty;
}

std::uint64_t HashLegacyScoreInput(const DataOrientedRoamState& state, TerrainLodPassId pass)
{
    // 旧工程 CSV 的成员集合编码保留原义，真实阶段身份另用完整输入哈希
    std::vector<std::uint64_t> paths;
    if (pass == TerrainLodPassId::MergeScore)
        for (const auto& entry : state.MergeQueue) paths.push_back(state.Nodes.PathIdAt(entry.Node));
    else
        for (const auto& entry : state.SplitQueue) paths.push_back(state.Nodes.PathIdAt(entry.Node));
    std::sort(paths.begin(), paths.end());
    std::uint64_t hash = TerrainLodHashOffset;
    AppendTerrainLodHash(hash, pass);
    AppendTerrainLodHash(hash, state.BuildSequence);
    AppendTerrainLodHash(hash, state.DrawableWidth);
    AppendTerrainLodHash(hash, state.DrawableHeight);
    AppendTerrainLodHash(hash, state.Settings.SplitThreshold);
    AppendTerrainLodHash(hash, state.Settings.MergeThreshold);
    for (int column = 0; column < 4; ++column)
        for (int row = 0; row < 4; ++row)
            AppendTerrainLodHash(hash, state.ViewProjection[column][row]);
    for (const auto path : paths) AppendTerrainLodHash(hash, path);
    return hash;
}

std::uint64_t HashLegacyInput(const DataOrientedRoamState& state, TerrainLodPassId pass)
{
    if (pass == TerrainLodPassId::MergeScore || pass == TerrainLodPassId::SplitScore)
        return HashLegacyScoreInput(state, pass);
    if (pass == TerrainLodPassId::SplitTopology)
    {
        std::vector<DataOrientedRoamSplitCandidate> candidates;
        SnapshotPersistentSplitQueueCandidates(state, candidates);
        return HashFrozenSplitTopologyInput(state, candidates);
    }
    if (pass == TerrainLodPassId::MergeTopology)
    {
        std::vector<DataOrientedRoamMergeCandidate> candidates;
        SnapshotPersistentMergeQueueCandidates(state, state.Settings.MergeThreshold, candidates);
        return HashFrozenMergeTopologyInput(state, candidates);
    }
    std::uint64_t hash = TerrainLodHashOffset;
    AppendTerrainLodHash(hash, state.BuildSequence);
    AppendTerrainLodHash(hash, state.IncrementalMesh.Metadata.Generation);
    for (const auto node : state.IncrementalMesh.Metadata.SlotOwners)
        AppendTerrainLodHash(hash, state.Nodes.PathIdAt(node));
    // 网格输入保留修改顺序，不能沿用结果等价比较的排序编码
    for (const auto& edit : state.IncrementalMesh.Metadata.TopologyEdits)
    {
        AppendTerrainLodHash(hash, edit.Type);
        AppendTerrainLodHash(hash, state.Nodes.PathIdAt(edit.Node));
    }
    for (const auto node : state.IncrementalMesh.Metadata.DebugTransitionLeaves)
        AppendTerrainLodHash(hash, state.Nodes.PathIdAt(node));
    return hash;
}

std::size_t PreparationWorkerLimit(const DataOrientedRoamState& source, TerrainLodPassId pass)
{
    // 资源上限只用于计时前扩容；实际可并行工作仍在生产执行内部重新判断
    if (pass == TerrainLodPassId::MergeScore) return source.MergeQueue.size();
    if (pass == TerrainLodPassId::SplitScore) return source.SplitQueue.size();
    if (pass == TerrainLodPassId::MeshEmit) return source.ActiveLeafNodes.size();
    if (pass == TerrainLodPassId::MergeTopology || pass == TerrainLodPassId::SplitTopology)
        return static_cast<std::size_t>(DataOrientedRoamTopologyChunkGridSize * DataOrientedRoamTopologyChunkGridSize);
    throw std::invalid_argument{"Expected a CPU ROAM pass"};
}

void ReadExecution(const DataOrientedRoamState& state, Sample& sample)
{
    const auto& stats = state.Stats;
    const bool split = sample.PassId == TerrainLodPassId::SplitScore ||
        sample.PassId == TerrainLodPassId::SplitTopology;
    if (sample.PassId == TerrainLodPassId::MergeScore || sample.PassId == TerrainLodPassId::SplitScore)
    {
        sample.CandidateCount = split ? stats.SplitScoreEntryCount : stats.MergeScoreEntryCount;
        sample.EffectiveWorkerCount = split ? stats.SplitCandidateMarkWorkerCount : stats.MergeCandidateMarkWorkerCount;
        sample.ScoreMilliseconds = split ? stats.SplitScoreMilliseconds : stats.MergeScoreMilliseconds;
        sample.HeapifyMilliseconds = split ? stats.SplitHeapifyMilliseconds : stats.MergeHeapifyMilliseconds;
    }
    else if (sample.PassId == TerrainLodPassId::MeshEmit)
    {
        sample.EffectiveWorkerCount = stats.EmitWorkerCount;
        sample.DirtyTriangleCount = stats.MeshUpdatedTriangleCount;
        sample.DirtyRangeCount = stats.MeshDirtyRangeCount;
    }
    else
    {
        // 子项仅用于解释生产内部成本，不参与拼接完整阶段时间
        sample.CandidateCount = split ? stats.SplitCandidateCount : stats.MergeCandidateCount;
        sample.InteriorCandidateCount = split ? stats.InteriorSplitCandidateCount : stats.InteriorMergeCandidateCount;
        sample.BoundaryCandidateCount = split ? stats.BoundarySplitCandidateCount : stats.BoundaryMergeCandidateCount;
        sample.NonEmptyChunkCount = split ? stats.SplitTopologyNonEmptyChunkCount : stats.MergeTopologyNonEmptyChunkCount;
        sample.EffectiveWorkerCount = split ? stats.SplitTopologyCommitWorkerCount : stats.MergeTopologyCommitWorkerCount;
        sample.EarlyCommitCount = split ? stats.ParallelSplitCommitCount : stats.ParallelMergeCommitCount;
        sample.CandidateSnapshotMilliseconds = split ? stats.SplitCandidateSnapshotMilliseconds : stats.MergeCandidateSnapshotMilliseconds;
        sample.ChunkBuildMilliseconds = split ? stats.SplitTopologyChunkBuildMilliseconds : stats.MergeTopologyChunkBuildMilliseconds;
        sample.QueueInvalidationMilliseconds = split ? stats.SplitTopologyQueueInvalidationMilliseconds : stats.MergeTopologyQueueInvalidationMilliseconds;
        sample.CommitMilliseconds = IsParallel(sample.RequestedAction)
            ? (split ? stats.SplitTopologyParallelCommitMilliseconds : stats.MergeTopologyParallelCommitMilliseconds)
            : (split ? stats.SplitTopologySerialCommitMilliseconds : stats.MergeTopologySerialCommitMilliseconds);
        sample.ResultMergeMilliseconds = split ? stats.SplitTopologyResultMergeMilliseconds : stats.MergeTopologyResultMergeMilliseconds;
        sample.IndexQueueRefreshMilliseconds = split ? stats.SplitTopologyIndexQueueRefreshMilliseconds : stats.MergeTopologyIndexQueueRefreshMilliseconds;
        sample.SerialConvergenceMilliseconds = split ? stats.SplitTopologySerialConvergenceMilliseconds : stats.MergeTopologySerialConvergenceMilliseconds;
        // 串行生产入口不登记分块线程，仍在调用线程完成收敛
        if (sample.EffectiveWorkerCount == 0U || sample.RequestedAction == TerrainLodPassAction::SerialImmediate)
            sample.EffectiveWorkerCount = 1U;
    }
    sample.ActiveTriangleCount = state.ActiveLeafNodes.size();
    const bool noWork = sample.PassId == TerrainLodPassId::MeshEmit ? sample.DirtyTriangleCount == 0U :
        ((sample.PassId == TerrainLodPassId::MergeScore || sample.PassId == TerrainLodPassId::SplitScore) &&
            sample.CandidateCount == 0U);
    if (noWork)
    {
        sample.EffectiveWorkerCount = 0U;
        sample.FallbackReason = TerrainLodPassFallbackReason::NoWork;
        sample.FallbackDetail = "no_work";
    }
    else if (sample.EffectiveWorkerCount > 1U && state.ThreadPool == nullptr)
    {
        // 缺池时生产函数会在调用线程遍历任务编号，不能把任务数量当成真实并行
        sample.EffectiveWorkerCount = 1U;
        sample.FallbackReason = TerrainLodPassFallbackReason::ResourceCapacity;
        sample.FallbackDetail = "missing_thread_pool";
    }
    else if (IsParallel(sample.RequestedAction) && sample.EffectiveWorkerCount <= 1U)
    {
        sample.FallbackReason = TerrainLodPassFallbackReason::BelowParallelThreshold;
        const bool topology = sample.PassId == TerrainLodPassId::MergeTopology ||
            sample.PassId == TerrainLodPassId::SplitTopology;
        sample.FallbackDetail = "below_parallel_threshold";
        if (sample.RequestedWorkerCount == 1U)
            sample.FallbackDetail = "requested_one_worker";
        else if (topology)
        {
            const auto& policy = state.Settings.PassPolicy;
            const bool permittedPhase = policy.ParallelTopologyPhase == TerrainLodParallelTopologyPhase::Both ||
                (split && policy.ParallelTopologyPhase == TerrainLodParallelTopologyPhase::SplitOnly) ||
                (!split && policy.ParallelTopologyPhase == TerrainLodParallelTopologyPhase::MergeOnly);
            const auto count = split ? stats.SplitTopologyCandidateCount : stats.MergeTopologyCandidateCount;
            const auto minimum = split ? policy.SplitTopologyMinParallelCandidateCount : policy.MergeTopologyMinParallelCandidateCount;
            if (!permittedPhase || (policy.ParallelTopologyTargetBuild != 0U &&
                    policy.ParallelTopologyTargetBuild != state.BuildSequence))
            {
                sample.FallbackReason = TerrainLodPassFallbackReason::ParallelDisabled;
                sample.FallbackDetail = "parallel_phase_or_build_gate";
            }
            else if (count >= minimum)
                sample.FallbackDetail = sample.NonEmptyChunkCount < 2U ? "insufficient_safe_chunks" : "parallel_not_dispatched";
        }
    }
    const bool parallel = sample.EffectiveWorkerCount > 1U;
    // 请求动作与实际路径同时保留，调用线程遍历多个任务不能冒充并行获益
    sample.ExecutionPath = noWork ? "no_work" : (parallel ? "thread_pool" : "caller_thread");
    if (sample.PassId == TerrainLodPassId::MergeScore || sample.PassId == TerrainLodPassId::SplitScore)
        sample.EffectiveAction = parallel ? TerrainLodPassAction::ParallelFullRefresh : TerrainLodPassAction::SerialFullRefresh;
    else if (sample.PassId == TerrainLodPassId::MeshEmit)
        sample.EffectiveAction = sample.RequestedAction == TerrainLodPassAction::SerialFull
            ? TerrainLodPassAction::SerialFull : (parallel ? TerrainLodPassAction::ParallelDirty : TerrainLodPassAction::SerialDirty);
    else
        sample.EffectiveAction = parallel ? TerrainLodPassAction::ParallelAssisted : TerrainLodPassAction::SerialImmediate;
}
}

void ConfigureDataOrientedRoamPassAction(DataOrientedRoamState& state, TerrainLodPassId passId,
    TerrainLodPassAction action, std::size_t parallelWorkerCount)
{
    if (parallelWorkerCount == 0U || parallelWorkerCount > std::numeric_limits<std::uint32_t>::max())
        throw std::invalid_argument{"Worker request must fit a positive uint32"};
    auto& policy = state.Settings.PassPolicy;
    // 其他阶段的动作和所有下限保持来源值，不借测量准备重新定义输入
    const auto workers = IsParallel(action) ? parallelWorkerCount : 1U;
    if (passId == TerrainLodPassId::MergeScore || passId == TerrainLodPassId::SplitScore)
    {
        if (action != TerrainLodPassAction::SerialFullRefresh && action != TerrainLodPassAction::ParallelFullRefresh)
            throw std::invalid_argument{"Expected a score refresh action"};
        auto& selected = passId == TerrainLodPassId::MergeScore ? policy.MergeScore : policy.SplitScore;
        selected = IsParallel(action) ? TerrainLodScoreRefreshAction::ParallelRefresh : TerrainLodScoreRefreshAction::SerialRefresh;
        (passId == TerrainLodPassId::MergeScore ? policy.MergeScoreWorkerCount : policy.SplitScoreWorkerCount) = workers;
    }
    else if (passId == TerrainLodPassId::MergeTopology || passId == TerrainLodPassId::SplitTopology)
    {
        if (action != TerrainLodPassAction::SerialImmediate && action != TerrainLodPassAction::ParallelAssisted)
            throw std::invalid_argument{"Expected a topology action"};
        auto& selected = passId == TerrainLodPassId::MergeTopology ? policy.MergeTopology : policy.SplitTopology;
        selected = IsParallel(action) ? TerrainLodTopologyAction::ParallelAssisted : TerrainLodTopologyAction::SerialImmediate;
        (passId == TerrainLodPassId::MergeTopology ? policy.MergeTopologyWorkerCount : policy.SplitTopologyWorkerCount) = workers;
    }
    else if (passId == TerrainLodPassId::MeshEmit)
    {
        switch (action)
        {
        case TerrainLodPassAction::SerialDirty: policy.MeshEmit = TerrainLodMeshEmitAction::SerialDirty; break;
        case TerrainLodPassAction::ParallelDirty: policy.MeshEmit = TerrainLodMeshEmitAction::ParallelDirty; break;
        case TerrainLodPassAction::SerialFull: policy.MeshEmit = TerrainLodMeshEmitAction::SerialFull; break;
        default: throw std::invalid_argument{"Expected a mesh emit action"};
        }
        policy.MeshEmitWorkerCount = workers;
    }
    else throw std::invalid_argument{"Expected a CPU ROAM pass"};
}

float PrepareDataOrientedRoamPassWorkers(const DataOrientedRoamState& source,
    TerrainLodPassId passId, std::size_t parallelWorkerCount)
{
    if (parallelWorkerCount == 0U || parallelWorkerCount > std::numeric_limits<std::uint32_t>::max())
        throw std::invalid_argument{"Worker request must fit a positive uint32"};
    const auto workers = std::min(parallelWorkerCount, PreparationWorkerLimit(source, passId));
    if (workers <= 1U || source.ThreadPool == nullptr) return 0.0F;
    // 借用池只扩容与同步唤醒，生命周期仍由来源流水线负责
    Tools::PerformanceTimer timer;
    source.ThreadPool->EnsureWorkerCount(workers);
    source.ThreadPool->ParallelFor(workers, [](std::size_t) {});
    return timer.Stop();
}

DataOrientedRoamPassExperimentSample MeasureDataOrientedRoamPass(
    const DataOrientedRoamState& source, TerrainLodPassId passId, TerrainLodPassAction action,
    const DataOrientedRoamPassExperimentConfig& config)
{
    if (source.HeightMap == nullptr || !source.HeightMap->IsValid())
        throw std::invalid_argument{"A valid source height map is required"};
    if (config.Mode != DataOrientedRoamPassExperimentMode::Diagnostic &&
        config.Mode != DataOrientedRoamPassExperimentMode::PilotMeasurement)
        throw std::invalid_argument{"Unknown measurement mode"};
    Sample sample;
    sample.PassId = passId;
    sample.RequestedAction = action;
    sample.RequestedWorkerCount = IsParallel(action) ? config.ParallelWorkerCount : 1U;
    Tools::PerformanceTimer inputTimer;
    // 公共阶段身份排除待测动作与诊断开关，旧工程字段仍保留各自的兼容编码
    const auto expectedInputHash = HashDataOrientedRoamPassInput(source, passId);
    sample.FrozenStateHash = config.Mode == DataOrientedRoamPassExperimentMode::Diagnostic
        ? HashLegacyInput(source, passId) : expectedInputHash;
    sample.InputCheckMilliseconds = inputTimer.Stop();
    return Detail::MeasurePreparedCpuPass(
        [&] {
            Tools::PerformanceTimer cloneTimer;
            auto state = std::make_unique<DataOrientedRoamState>(source);
            sample.StateCloneMilliseconds = cloneTimer.Stop();
            state->Stats = {};
            // 状态复制保留前序拓扑修改和旧几何，只清空本次执行的统计累加器
            ConfigureDataOrientedRoamPassAction(*state, passId, action, config.ParallelWorkerCount);
            // 诊断入口也禁止递归拓扑配对，测量模式进一步关闭所有额外诊断
            state->Settings.EnableTopologyPairEvidence = false;
            if (config.Mode == DataOrientedRoamPassExperimentMode::PilotMeasurement)
                state->Settings.EnablePassEvidence = state->Settings.EnableTopologyValidation = false;
            sample.DiagnosticsDisabledAtExecution = !state->Settings.EnablePassEvidence &&
                !state->Settings.EnableTopologyValidation && !state->Settings.EnableTopologyPairEvidence;
            const auto workers = std::min(sample.RequestedWorkerCount, PreparationWorkerLimit(source, passId));
            if (workers > 1U && state->ThreadPool != nullptr && state->ThreadPool->WorkerCount() < workers)
            {
                sample.WorkerPreparationMilliseconds =
                    PrepareDataOrientedRoamPassWorkers(source, passId, config.ParallelWorkerCount);
            }
            Tools::PerformanceTimer hashTimer;
            // 配置副本后再次验证身份，避免准备代码意外改写有效算法状态
            sample.StageInputHash = HashDataOrientedRoamPassInput(*state, passId);
            sample.InputCheckMilliseconds += hashTimer.Stop();
            if (sample.StageInputHash != expectedInputHash)
                throw std::runtime_error{"Measurement preparation changed the frozen input"};
            return state;
        },
        [passId](auto& state) { ExecuteDataOrientedRoamPass(state, passId); },
        [&](auto& state, float wallMilliseconds) {
            sample.WallMilliseconds = wallMilliseconds;
            // 验证器会写诊断统计，先保存生产执行的线程数和内部成本
            ReadExecution(state, sample);
            Tools::PerformanceTimer evidenceTimer;
            const auto evidence = CaptureDataOrientedRoamPassEvidence(state, passId);
            sample.ResultHash = evidence.ResultHash;
            sample.ValidationPerformed = evidence.ValidationPerformed;
            sample.Correct = evidence.Correct;
            sample.ValidationMilliseconds = evidenceTimer.Stop();
            if (!sample.Correct) sample.FailureMessage = "phase_output_validation_failed";
            return sample;
        });
}
}
