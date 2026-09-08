#include "algorithms/data_oriented_roam/DataOrientedRoamPassExecution.h"
#include "algorithms/data_oriented_roam/DataOrientedRoamPassExperiment.h"

#include "algorithms/data_oriented_roam/DataOrientedRoamMeshEmit.h"
#include "algorithms/data_oriented_roam/DataOrientedRoamQueues.h"
#include "algorithms/data_oriented_roam/DataOrientedRoamState.h"
#include "algorithms/data_oriented_roam/DataOrientedRoamTopology.h"
#include "tools/PerformanceTimer.h"

#include <algorithm>
#include <array>
#include <iterator>
#include <memory>
#include <utility>

namespace ParallelRoam::Algorithms::DataOrientedRoam
{
namespace
{
using Sample = DataOrientedRoamPassExperimentSample;

// 三种网格写入方式共有六种执行顺序
// 每个正式输入按顺序轮换，避免某一种方式总是受益于热缓存
// 顺序只影响测量先后，不会改变任何输入副本
// 每次执行前都重新复制冻结状态
// 因此这里不需要在策略之间手工撤销网格修改
constexpr std::array<std::array<TerrainLodPassAction, 3U>, 6U> MeshActionOrders{{
    {TerrainLodPassAction::SerialDirty, TerrainLodPassAction::ParallelDirty,
     TerrainLodPassAction::SerialFull},
    {TerrainLodPassAction::SerialDirty, TerrainLodPassAction::SerialFull,
     TerrainLodPassAction::ParallelDirty},
    {TerrainLodPassAction::ParallelDirty, TerrainLodPassAction::SerialDirty,
     TerrainLodPassAction::SerialFull},
    {TerrainLodPassAction::ParallelDirty, TerrainLodPassAction::SerialFull,
     TerrainLodPassAction::SerialDirty},
    {TerrainLodPassAction::SerialFull, TerrainLodPassAction::SerialDirty,
     TerrainLodPassAction::ParallelDirty},
    {TerrainLodPassAction::SerialFull, TerrainLodPassAction::ParallelDirty,
     TerrainLodPassAction::SerialDirty},
}};

std::uint64_t HashScoreInput(const DataOrientedRoamState& state, TerrainLodPassId passId)
{
    // 评分输入由视点、更新序号和当前队列成员共同决定
    // 堆数组内部顺序不是研究输入的一部分
    // 同一组成员可能因上一次建堆形成不同数组排列
    // 因此先转换为稳定路径并排序
    // 合并和细分使用阶段编号区分相同路径集合
    std::vector<std::uint64_t> paths;
    if (passId == TerrainLodPassId::MergeScore)
    {
        paths.reserve(state.MergeQueue.size());
        for (const DataOrientedRoamMergeQueueEntry& entry : state.MergeQueue)
        {
            paths.push_back(state.Nodes.PathIdAt(entry.Node));
        }
    }
    else
    {
        paths.reserve(state.SplitQueue.size());
        for (const DataOrientedRoamSplitQueueEntry& entry : state.SplitQueue)
        {
            paths.push_back(state.Nodes.PathIdAt(entry.Node));
        }
    }
    std::sort(paths.begin(), paths.end());

    std::uint64_t hash = TerrainLodHashOffset;
    AppendTerrainLodHash(hash, passId);
    AppendTerrainLodHash(hash, state.BuildSequence);
    // 可绘制区域和阈值会改变同一视点下的屏幕误差
    // 它们必须进入冻结输入编号
    AppendTerrainLodHash(hash, state.DrawableWidth);
    AppendTerrainLodHash(hash, state.DrawableHeight);
    AppendTerrainLodHash(hash, state.Settings.SplitThreshold);
    AppendTerrainLodHash(hash, state.Settings.MergeThreshold);
    for (int column = 0; column < 4; ++column)
    {
        for (int row = 0; row < 4; ++row)
        {
            AppendTerrainLodHash(hash, state.ViewProjection[column][row]);
        }
    }
    for (const std::uint64_t path : paths)
    {
        AppendTerrainLodHash(hash, path);
    }
    return hash;
}

std::uint64_t HashScoreResult(const DataOrientedRoamState& state, TerrainLodPassId passId)
{
    // 结果需要同时包含成员和本次重新计算的分数
    // 路径编号替代节点池下标，避免分配顺序进入实验标签
    // 排序后再计算编号可以忽略合法的堆数组排列差异
    // 分数仍按位参与计算，任何评分差异都会被发现
    // 建堆不改变这份规范化结果
    std::vector<std::pair<std::uint64_t, float>> entries;
    if (passId == TerrainLodPassId::MergeScore)
    {
        entries.reserve(state.MergeQueue.size());
        for (const DataOrientedRoamMergeQueueEntry& entry : state.MergeQueue)
        {
            entries.emplace_back(state.Nodes.PathIdAt(entry.Node), entry.Score);
        }
    }
    else
    {
        entries.reserve(state.SplitQueue.size());
        for (const DataOrientedRoamSplitQueueEntry& entry : state.SplitQueue)
        {
            entries.emplace_back(state.Nodes.PathIdAt(entry.Node), entry.Score);
        }
    }
    std::sort(entries.begin(), entries.end());

    std::uint64_t hash = TerrainLodHashOffset;
    for (const auto& [path, score] : entries)
    {
        AppendTerrainLodHash(hash, path);
        AppendTerrainLodHash(hash, score);
    }
    return hash;
}

std::uint64_t HashMeshInput(const DataOrientedRoamState& state)
{
    // 网格阶段读取旧槽位和本帧拓扑修改记录
    // 两部分缺一都不能唯一确定待写入内容
    // 修改顺序需要保留，因为同帧连续细分和合并按顺序重放
    // 节点仍转换成稳定路径，避免副本中的内存地址进入编号
    // 更新序号用于区分调试属性发生变化的相邻帧
    std::uint64_t hash = TerrainLodHashOffset;
    AppendTerrainLodHash(hash, state.BuildSequence);
    // 网格版本参与调试属性和脏槽位判定
    AppendTerrainLodHash(hash, state.IncrementalMesh.Metadata.Generation);
    for (const DataOrientedRoamNodeIndex node : state.IncrementalMesh.Metadata.SlotOwners)
    {
        AppendTerrainLodHash(hash, state.Nodes.PathIdAt(node));
    }
    for (const DataOrientedRoamMeshTopologyEdit& edit : state.IncrementalMesh.Metadata.TopologyEdits)
    {
        AppendTerrainLodHash(hash, edit.Type);
        AppendTerrainLodHash(hash, state.Nodes.PathIdAt(edit.Node));
    }
    for (const DataOrientedRoamNodeIndex node : state.IncrementalMesh.Metadata.DebugTransitionLeaves)
    {
        AppendTerrainLodHash(hash, state.Nodes.PathIdAt(node));
    }
    return hash;
}

std::uint64_t HashNormalizedMesh(const DataOrientedRoamState& state)
{
    // 不同策略允许使用不同的槽位整理顺序
    // 渲染结果等价性必须忽略这种实现细节
    // 槽位所有者先转换为稳定路径
    // 规范化网格编号再把路径和对应顶点绑定
    // 这样可以发现几何或属性差异而不误报顺序差异
    std::vector<std::uint64_t> slotPaths;
    slotPaths.reserve(state.IncrementalMesh.Metadata.SlotOwners.size());
    for (const DataOrientedRoamNodeIndex node : state.IncrementalMesh.Metadata.SlotOwners)
    {
        slotPaths.push_back(state.Nodes.PathIdAt(node));
    }
    return HashTerrainLodNormalizedMesh(state.IncrementalMesh.Data, slotPaths);
}

TerrainLodPassFallbackReason ResolveParallelFallback(
    std::size_t workCount,
    std::size_t effectiveWorkerCount,
    bool requestedParallel)
{
    // 无工作和线程不足是两种不同的实验区域
    // 前者说明阶段没有可比较内容
    // 后者说明并行请求未形成真实并行执行
    // 串行请求不因只使用一个线程而记为回退
    // 调用方仍需结合实际方式判断样本能否参与胜负比较
    if (workCount == 0U)
    {
        return TerrainLodPassFallbackReason::NoWork;
    }
    if (requestedParallel && effectiveWorkerCount <= 1U)
    {
        return TerrainLodPassFallbackReason::BelowParallelThreshold;
    }
    return TerrainLodPassFallbackReason::None;
}

Sample RunScoreAction(
    const DataOrientedRoamState& source,
    TerrainLodPassId passId,
    TerrainLodPassAction action,
    std::size_t repeatIndex,
    std::size_t executionOrder,
    std::size_t parallelWorkerCount)
{
    // 状态复制发生在阶段计时之外并单独记录
    // 串行和并行评分都从相同队列成员和相同视点开始
    // 副本只改变当前评分阶段的策略与线程上限
    // 评分函数本身继续使用生产路径的阈值和任务划分
    // 队列完整性检查防止错误结果进入性能样本
    Tools::PerformanceTimer cloneTimer;
    DataOrientedRoamState state{source};
    const float cloneMilliseconds = cloneTimer.Stop();
    state.Stats = {};
    const bool merge = passId == TerrainLodPassId::MergeScore;
    const bool parallel = action == TerrainLodPassAction::ParallelFullRefresh;
    if (merge)
    {
        state.Settings.PassPolicy.MergeScore = parallel
            ? TerrainLodScoreRefreshAction::ParallelRefresh
            : TerrainLodScoreRefreshAction::SerialRefresh;
        state.Settings.PassPolicy.MergeScoreWorkerCount = parallel ? parallelWorkerCount : 1U;
    }
    else
    {
        state.Settings.PassPolicy.SplitScore = parallel
            ? TerrainLodScoreRefreshAction::ParallelRefresh
            : TerrainLodScoreRefreshAction::SerialRefresh;
        state.Settings.PassPolicy.SplitScoreWorkerCount = parallel ? parallelWorkerCount : 1U;
    }

    Tools::PerformanceTimer wallTimer;
    if (merge)
    {
        RefreshPersistentMergeQueuePriorities(state);
    }
    else
    {
        RefreshPersistentSplitQueuePriorities(state);
    }
    const float wallMilliseconds = wallTimer.Stop();

    Sample sample{};
    sample.PassId = passId;
    sample.RequestedAction = action;
    sample.EffectiveWorkerCount = merge
        ? state.Stats.MergeCandidateMarkWorkerCount
        : state.Stats.SplitCandidateMarkWorkerCount;
    sample.EffectiveAction = sample.EffectiveWorkerCount > 1U
        ? TerrainLodPassAction::ParallelFullRefresh
        : TerrainLodPassAction::SerialFullRefresh;
    sample.RepeatIndex = repeatIndex;
    sample.ExecutionOrder = executionOrder;
    sample.RequestedWorkerCount = parallel ? parallelWorkerCount : 1U;
    sample.CandidateCount = merge
        ? state.Stats.MergeScoreEntryCount
        : state.Stats.SplitScoreEntryCount;
    sample.FallbackReason = ResolveParallelFallback(
        sample.CandidateCount,
        sample.EffectiveWorkerCount,
        parallel);
    sample.FrozenStateHash = HashScoreInput(source, passId);
    sample.ResultHash = HashScoreResult(state, passId);
    sample.StateCloneMilliseconds = cloneMilliseconds;
    sample.ScoreMilliseconds = merge
        ? state.Stats.MergeScoreMilliseconds
        : state.Stats.SplitScoreMilliseconds;
    sample.HeapifyMilliseconds = merge
        ? state.Stats.MergeHeapifyMilliseconds
        : state.Stats.SplitHeapifyMilliseconds;
    sample.WallMilliseconds = wallMilliseconds;
    sample.Correct = CountPersistentQueueInvariantViolations(state) == 0U;
    return sample;
}

std::uint64_t HashTopologyEvidence(const TerrainLodTopologyReplayEvidence& evidence)
{
    // 单独比较拓扑编号不足以证明阶段结果等价
    // 活动叶、队列成员和网格修改也会被后续阶段直接消费
    // 这四类编号与活动三角形数量共同组成结果编号
    // 计时、线程数量和提交顺序不属于结果语义
    // 正确性计数由样本中的独立布尔值检查
    std::uint64_t hash = TerrainLodHashOffset;
    AppendTerrainLodHash(hash, evidence.TopologyHash);
    AppendTerrainLodHash(hash, evidence.ActiveLeafHash);
    AppendTerrainLodHash(hash, evidence.QueueMembershipHash);
    AppendTerrainLodHash(hash, evidence.MeshEditHash);
    AppendTerrainLodHash(hash, evidence.ActiveTriangleCount);
    return hash;
}

Sample MakeTopologySample(
    TerrainLodPassId passId,
    TerrainLodPassAction requestedAction,
    std::size_t repeatIndex,
    std::size_t executionOrder,
    std::size_t requestedWorkerCount,
    std::uint64_t frozenStateHash,
    float snapshotMilliseconds,
    const TerrainLodTopologyReplayEvidence& evidence)
{
    // 拓扑证据已经由生产验证器检查邻接和裂缝
    // 这里把私有证据整理成六个阶段共用的行结构
    // 候选快照只计入并行辅助方式的完整阶段耗时
    // 状态复制始终单列，不进入策略墙钟时间
    // 回退标签依据请求方式和实际线程数量生成
    Sample sample{};
    sample.PassId = passId;
    sample.RequestedAction = requestedAction;
    sample.EffectiveAction = evidence.EffectiveWorkerCount > 1U
        ? TerrainLodPassAction::ParallelAssisted
        : TerrainLodPassAction::SerialImmediate;
    sample.RepeatIndex = repeatIndex;
    sample.ExecutionOrder = executionOrder;
    sample.RequestedWorkerCount = requestedWorkerCount;
    sample.EffectiveWorkerCount = evidence.EffectiveWorkerCount;
    sample.CandidateCount = evidence.InteriorCandidateCount + evidence.BoundaryCandidateCount;
    sample.InteriorCandidateCount = evidence.InteriorCandidateCount;
    sample.BoundaryCandidateCount = evidence.BoundaryCandidateCount;
    sample.NonEmptyChunkCount = evidence.NonEmptyChunkCount;
    sample.EarlyCommitCount = evidence.EarlyCommitCount;
    sample.ActiveTriangleCount = evidence.ActiveTriangleCount;
    sample.FallbackReason = ResolveParallelFallback(
        sample.CandidateCount,
        sample.EffectiveWorkerCount,
        requestedAction == TerrainLodPassAction::ParallelAssisted);
    sample.FrozenStateHash = frozenStateHash;
    sample.ResultHash = HashTopologyEvidence(evidence);
    sample.StateCloneMilliseconds = evidence.StateCloneMilliseconds;
    sample.CandidateSnapshotMilliseconds = snapshotMilliseconds;
    sample.ChunkBuildMilliseconds = evidence.ChunkBuildMilliseconds;
    sample.QueueInvalidationMilliseconds = evidence.QueueInvalidationMilliseconds;
    sample.CommitMilliseconds = evidence.CommitMilliseconds;
    sample.ResultMergeMilliseconds = evidence.ResultMergeMilliseconds;
    sample.IndexQueueRefreshMilliseconds = evidence.IndexQueueRefreshMilliseconds;
    sample.SerialConvergenceMilliseconds = evidence.SerialConvergenceMilliseconds;
    sample.WallMilliseconds = evidence.WallMilliseconds + snapshotMilliseconds;
    sample.Correct = evidence.BudgetViolationCount == 0U &&
        evidence.QueueInvariantViolationCount == 0U &&
        evidence.TjunctionCount == 0U &&
        evidence.InvalidNeighborCount == 0U &&
        evidence.InvalidTopologyCount == 0U;
    return sample;
}

Sample RunTopologyAction(
    const DataOrientedRoamState& source,
    TerrainLodPassId passId,
    TerrainLodPassAction action,
    std::size_t repeatIndex,
    std::size_t executionOrder,
    std::size_t parallelWorkerCount)
{
    // 每种拓扑方式先复制同一评分完成状态
    // 候选数组只读，不会因第一次执行而失效
    // 串行方式直接消费长期队列，不把候选快照计入策略成本
    // 并行辅助方式包含候选扫描、排序和后续全部维护成本
    // 两种方式最终都进入相同的串行收敛规则
    Tools::PerformanceTimer cloneTimer;
    DataOrientedRoamState state{source};
    const float cloneMilliseconds = cloneTimer.Stop();
    const bool split = passId == TerrainLodPassId::SplitTopology;
    const bool parallel = action == TerrainLodPassAction::ParallelAssisted;
    float snapshotMilliseconds = 0.0F;
    std::uint64_t frozenStateHash = TerrainLodHashOffset;
    TerrainLodTopologyReplayEvidence evidence{};

    if (split)
    {
        // 串行样本也生成候选编号，但生成成本留在实验框架之外
        // 并行样本则把同一次扫描纳入完整阶段耗时
        // 这样既能证明输入一致，又不会虚增串行生产路径成本
        std::vector<DataOrientedRoamSplitCandidate> candidates;
        Tools::PerformanceTimer snapshotTimer;
        SnapshotPersistentSplitQueueCandidates(state, candidates);
        if (parallel)
        {
            snapshotMilliseconds = snapshotTimer.Stop();
        }
        frozenStateHash = HashFrozenSplitTopologyInput(source, candidates);
        state.Settings.PassPolicy.SplitTopologyWorkerCount = parallel ? parallelWorkerCount : 1U;
        evidence = ReplayFrozenSplitTopologyAction(state, candidates, parallel, cloneMilliseconds);
    }
    else
    {
        // 合并候选按低误差优先，并用稳定路径打破同分顺序
        // 这与正式并行辅助路径使用的顺序保持一致
        // 串行方式仍从长期队列直接收敛
        std::vector<DataOrientedRoamMergeCandidate> candidates;
        Tools::PerformanceTimer snapshotTimer;
        SnapshotPersistentMergeQueueCandidates(state, state.Settings.MergeThreshold, candidates);
        std::sort(
            candidates.begin(),
            candidates.end(),
            [&state](const DataOrientedRoamMergeCandidate& left,
                     const DataOrientedRoamMergeCandidate& right) {
                return left.Score == right.Score
                    ? state.Nodes.PathIdAt(left.Node) < state.Nodes.PathIdAt(right.Node)
                    : left.Score < right.Score;
            });
        if (parallel)
        {
            snapshotMilliseconds = snapshotTimer.Stop();
        }
        frozenStateHash = HashFrozenMergeTopologyInput(source, candidates);
        state.Settings.PassPolicy.MergeTopologyWorkerCount = parallel ? parallelWorkerCount : 1U;
        evidence = ReplayFrozenMergeTopologyAction(state, candidates, parallel, cloneMilliseconds);
    }

    return MakeTopologySample(
        passId,
        action,
        repeatIndex,
        executionOrder,
        parallel ? parallelWorkerCount : 1U,
        frozenStateHash,
        snapshotMilliseconds,
        evidence);
}

Sample RunMeshAction(
    const DataOrientedRoamState& source,
    TerrainLodPassAction action,
    std::size_t repeatIndex,
    std::size_t executionOrder,
    std::size_t parallelWorkerCount)
{
    // 网格策略读取完全相同的槽位、拓扑修改和活动叶
    // 每次复制都保留旧网格内容，符合增量写入的真实前置条件
    // 完整写入也从同一旧网格开始，但会覆盖全部活动槽位
    // 写入和脏区间整理都包含在墙钟时间内
    // 规范化网格编号用于比较最终可渲染结果
    Tools::PerformanceTimer cloneTimer;
    DataOrientedRoamState state{source};
    const float cloneMilliseconds = cloneTimer.Stop();
    switch (action)
    {
    case TerrainLodPassAction::SerialDirty:
        state.Settings.PassPolicy.MeshEmit = TerrainLodMeshEmitAction::SerialDirty;
        state.Settings.PassPolicy.MeshEmitWorkerCount = 1U;
        break;
    case TerrainLodPassAction::ParallelDirty:
        state.Settings.PassPolicy.MeshEmit = TerrainLodMeshEmitAction::ParallelDirty;
        state.Settings.PassPolicy.MeshEmitWorkerCount = parallelWorkerCount;
        break;
    case TerrainLodPassAction::SerialFull:
        state.Settings.PassPolicy.MeshEmit = TerrainLodMeshEmitAction::SerialFull;
        state.Settings.PassPolicy.MeshEmitWorkerCount = 1U;
        break;
    default:
        break;
    }
    state.Stats = {};

    Tools::PerformanceTimer wallTimer;
    ApplyIncrementalMeshUpdates(state);
    FinalizeIncrementalMeshUpdate(state);
    const float wallMilliseconds = wallTimer.Stop();

    Sample sample{};
    sample.PassId = TerrainLodPassId::MeshEmit;
    sample.RequestedAction = action;
    sample.EffectiveWorkerCount = state.Stats.EmitWorkerCount;
    sample.EffectiveAction = action == TerrainLodPassAction::SerialFull
        ? TerrainLodPassAction::SerialFull
        : (sample.EffectiveWorkerCount > 1U
            ? TerrainLodPassAction::ParallelDirty
            : TerrainLodPassAction::SerialDirty);
    sample.RepeatIndex = repeatIndex;
    sample.ExecutionOrder = executionOrder;
    sample.RequestedWorkerCount = action == TerrainLodPassAction::ParallelDirty
        ? parallelWorkerCount
        : 1U;
    sample.ActiveTriangleCount = state.IncrementalMesh.Metadata.SlotOwners.size();
    sample.DirtyTriangleCount = state.Stats.MeshUpdatedTriangleCount;
    sample.DirtyRangeCount = state.Stats.MeshDirtyRangeCount;
    sample.FallbackReason = ResolveParallelFallback(
        sample.DirtyTriangleCount,
        sample.EffectiveWorkerCount,
        action == TerrainLodPassAction::ParallelDirty);
    if (action != TerrainLodPassAction::ParallelDirty && sample.DirtyTriangleCount == 0U)
    {
        sample.FallbackReason = TerrainLodPassFallbackReason::NoWork;
    }
    sample.FrozenStateHash = HashMeshInput(source);
    sample.ResultHash = HashNormalizedMesh(state);
    sample.StateCloneMilliseconds = cloneMilliseconds;
    sample.WallMilliseconds = wallMilliseconds;
    sample.Correct = state.Stats.MeshUpdatedTriangleCount + state.Stats.MeshReusedTriangleCount ==
        state.IncrementalMesh.Metadata.SlotOwners.size();
    return sample;
}

template <typename RunAction>
bool RunTwoActionBlocks(
    DataOrientedRoamPassExperimentResult& result,
    TerrainLodPassAction firstAction,
    TerrainLodPassAction secondAction,
    const DataOrientedRoamPassExperimentConfig& config,
    RunAction&& runAction)
{
    // 两种方式按正序和反序交替执行
    // 一个配对块内的两个样本共享重复编号
    // 预热块执行相同代码但不写入正式样本
    // 任一结果错误都会使整个目标状态失败
    // 拓扑工作量统一取并行规划得到的候选分类
    const std::size_t totalBlocks = config.WarmupCount + config.MeasuredRepeatCount;
    bool passed = true;
    for (std::size_t block = 0U; block < totalBlocks; ++block)
    {
        const bool warmup = block < config.WarmupCount;
        const std::size_t repeatIndex = warmup ? block : block - config.WarmupCount;
        const std::array<TerrainLodPassAction, 2U> order = block % 2U == 0U
            ? std::array{firstAction, secondAction}
            : std::array{secondAction, firstAction};
        std::array<Sample, 2U> blockSamples{};
        for (std::size_t executionOrder = 0U; executionOrder < order.size(); ++executionOrder)
        {
            blockSamples[executionOrder] = runAction(
                order[executionOrder],
                repeatIndex,
                executionOrder);
        }
        const bool equivalent = blockSamples[0].ResultHash == blockSamples[1].ResultHash &&
            blockSamples[0].Correct && blockSamples[1].Correct;
        const bool topologyPass =
            blockSamples[0].PassId == TerrainLodPassId::MergeTopology ||
            blockSamples[0].PassId == TerrainLodPassId::SplitTopology;
        if (topologyPass)
        {
            // 串行生产路径不会建立分块，所以自身分类数量为零
            // 并行副本提供的分类描述的是共同冻结候选
            // 将同一分类回填到两行后才能进行严格配对统计
            const std::size_t interiorCount = std::max(
                blockSamples[0].InteriorCandidateCount,
                blockSamples[1].InteriorCandidateCount);
            const std::size_t boundaryCount = std::max(
                blockSamples[0].BoundaryCandidateCount,
                blockSamples[1].BoundaryCandidateCount);
            const std::size_t nonEmptyChunkCount = std::max(
                blockSamples[0].NonEmptyChunkCount,
                blockSamples[1].NonEmptyChunkCount);
            for (Sample& sample : blockSamples)
            {
                sample.CandidateCount = interiorCount + boundaryCount;
                sample.InteriorCandidateCount = interiorCount;
                sample.BoundaryCandidateCount = boundaryCount;
                sample.NonEmptyChunkCount = nonEmptyChunkCount;
                sample.FallbackReason = ResolveParallelFallback(
                    sample.CandidateCount,
                    sample.EffectiveWorkerCount,
                    sample.RequestedAction == TerrainLodPassAction::ParallelAssisted);
            }
        }
        for (Sample& sample : blockSamples)
        {
            sample.Equivalent = equivalent;
        }
        passed = passed && equivalent;
        if (warmup)
        {
            result.WarmupExecutionCount += order.size();
        }
        else
        {
            result.Samples.insert(
                result.Samples.end(),
                std::make_move_iterator(blockSamples.begin()),
                std::make_move_iterator(blockSamples.end()));
        }
    }
    return passed;
}

bool RunMeshBlocks(
    DataOrientedRoamPassExperimentResult& result,
    const DataOrientedRoamState& source,
    const DataOrientedRoamPassExperimentConfig& config)
{
    // 三种网格方式不能只用简单的正反顺序
    // 六种排列让每种方式均匀出现在第一、第二和第三位置
    // 每个块只比较最终规范化网格，不要求脏区间相同
    // 全量方式产生更多写入属于预期成本而不是错误
    // 预热执行数量单独汇总供报告元数据使用
    const std::size_t totalBlocks = config.WarmupCount + config.MeasuredRepeatCount;
    bool passed = true;
    for (std::size_t block = 0U; block < totalBlocks; ++block)
    {
        const bool warmup = block < config.WarmupCount;
        const std::size_t repeatIndex = warmup ? block : block - config.WarmupCount;
        const auto& order = MeshActionOrders[block % MeshActionOrders.size()];
        std::array<Sample, 3U> blockSamples{};
        for (std::size_t executionOrder = 0U; executionOrder < order.size(); ++executionOrder)
        {
            blockSamples[executionOrder] = RunMeshAction(
                source,
                order[executionOrder],
                repeatIndex,
                executionOrder,
                config.ParallelWorkerCount);
        }
        const bool equivalent =
            blockSamples[0].ResultHash == blockSamples[1].ResultHash &&
            blockSamples[0].ResultHash == blockSamples[2].ResultHash &&
            blockSamples[0].Correct && blockSamples[1].Correct && blockSamples[2].Correct;
        for (Sample& sample : blockSamples)
        {
            sample.Equivalent = equivalent;
        }
        passed = passed && equivalent;
        if (warmup)
        {
            result.WarmupExecutionCount += order.size();
        }
        else
        {
            result.Samples.insert(
                result.Samples.end(),
                std::make_move_iterator(blockSamples.begin()),
                std::make_move_iterator(blockSamples.end()));
        }
    }
    return passed;
}

std::unique_ptr<DataOrientedRoamState> PrepareNextFrameState(
    const DataOrientedRoamState& previousFrameState,
    const TerrainLodViewInput& nextView,
    const DataOrientedRoamSettings& settings)
{
    // 来源必须是上一帧完整结束后的状态且副本不推进正式流水线
    // 外层先确认深度和预算不变再复用生产准备规则
    auto state = std::make_unique<DataOrientedRoamState>(previousFrameState);
    if (!PrepareDataOrientedRoamFrame(*state, *previousFrameState.HeightMap,
            previousFrameState.TerrainSize, previousFrameState.HeightScale, nextView, settings))
        throw std::runtime_error{"Invalid replay height map"};
    return state;
}
} // 匿名命名空间

DataOrientedRoamPassExperimentResult RunDataOrientedRoamPassExperiment(
    const DataOrientedRoamState& previousFrameState,
    const TerrainLodViewInput& nextView,
    const DataOrientedRoamSettings& settings,
    const DataOrientedRoamPassExperimentConfig& config)
{
    // 五阶段测量后只以串行基线推进副本且推进不进入策略样本
    // 后一阶段消费前一阶段真实输出而上一帧来源保持不变
    DataOrientedRoamPassExperimentResult result{};
    if (config.MeasuredRepeatCount == 0U || config.ParallelWorkerCount == 0U)
    {
        result.FailureMessage = "正式重复次数和并行线程数量都必须大于零";
        return result;
    }
    if (previousFrameState.HeightMap == nullptr || !previousFrameState.HeightMap->IsValid())
    {
        result.FailureMessage = "上一帧没有可重放的有效高度图";
        return result;
    }
    if (previousFrameState.Settings.MaxDepth != settings.MaxDepth ||
        previousFrameState.Settings.TriangleBudget != settings.TriangleBudget)
    {
        result.FailureMessage = "阶段重放不允许在目标帧改变最大深度或三角形预算";
        return result;
    }

    std::unique_ptr<DataOrientedRoamState> state = PrepareNextFrameState(
        previousFrameState,
        nextView,
        settings);
    bool passed = true;

    passed = RunTwoActionBlocks(
        result,
        TerrainLodPassAction::SerialFullRefresh,
        TerrainLodPassAction::ParallelFullRefresh,
        config,
        [&](TerrainLodPassAction action, std::size_t repeat, std::size_t order) {
            return RunScoreAction(
                *state,
                TerrainLodPassId::MergeScore,
                action,
                repeat,
                order,
                config.ParallelWorkerCount);
        }) && passed;
    state->Settings.PassPolicy.MergeScore = TerrainLodScoreRefreshAction::SerialRefresh;
    // 生成合并拓扑共同使用的已评分长期队列
    ExecuteDataOrientedRoamPass(*state, TerrainLodPassId::MergeScore);

    passed = RunTwoActionBlocks(
        result,
        TerrainLodPassAction::SerialImmediate,
        TerrainLodPassAction::ParallelAssisted,
        config,
        [&](TerrainLodPassAction action, std::size_t repeat, std::size_t order) {
            return RunTopologyAction(
                *state,
                TerrainLodPassId::MergeTopology,
                action,
                repeat,
                order,
                config.ParallelWorkerCount);
        }) && passed;
    state->Settings.PassPolicy.MergeTopology = TerrainLodTopologyAction::SerialImmediate;
    ExecuteDataOrientedRoamPass(*state, TerrainLodPassId::MergeTopology);

    // 合并完成后的状态才是细分评分的真实输入
    passed = RunTwoActionBlocks(
        result,
        TerrainLodPassAction::SerialFullRefresh,
        TerrainLodPassAction::ParallelFullRefresh,
        config,
        [&](TerrainLodPassAction action, std::size_t repeat, std::size_t order) {
            return RunScoreAction(
                *state,
                TerrainLodPassId::SplitScore,
                action,
                repeat,
                order,
                config.ParallelWorkerCount);
        }) && passed;
    state->Settings.PassPolicy.SplitScore = TerrainLodScoreRefreshAction::SerialRefresh;
    // 生成细分拓扑共同使用的已评分长期队列
    ExecuteDataOrientedRoamPass(*state, TerrainLodPassId::SplitScore);

    passed = RunTwoActionBlocks(
        result,
        TerrainLodPassAction::SerialImmediate,
        TerrainLodPassAction::ParallelAssisted,
        config,
        [&](TerrainLodPassAction action, std::size_t repeat, std::size_t order) {
            return RunTopologyAction(
                *state,
                TerrainLodPassId::SplitTopology,
                action,
                repeat,
                order,
                config.ParallelWorkerCount);
        }) && passed;
    state->Settings.PassPolicy.SplitTopology = TerrainLodTopologyAction::SerialImmediate;
    ExecuteDataOrientedRoamPass(*state, TerrainLodPassId::SplitTopology);

    // 网格阶段同时看到本帧合并和细分留下的修改记录
    passed = RunMeshBlocks(result, *state, config) && passed;
    result.Passed = passed;
    if (!passed)
    {
        result.FailureMessage = "至少一个冻结输入的策略结果不等价或未通过正确性检查";
    }
    return result;
}
} // 命名空间 ParallelRoam::Algorithms::DataOrientedRoam
