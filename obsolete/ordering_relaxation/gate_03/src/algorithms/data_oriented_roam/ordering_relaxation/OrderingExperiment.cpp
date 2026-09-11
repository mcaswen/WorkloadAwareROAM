#include "algorithms/data_oriented_roam/ordering_relaxation/OrderingExperiment.h"

#include "algorithms/data_oriented_roam/DataOrientedRoamPassEvidence.h"
#include "algorithms/data_oriented_roam/DataOrientedRoamPassExecution.h"
#include "algorithms/data_oriented_roam/DataOrientedRoamPassInput.h"
#include "algorithms/data_oriented_roam/DataOrientedRoamPassMeasurement.h"
#include "algorithms/data_oriented_roam/DataOrientedRoamQueues.h"
#include "algorithms/data_oriented_roam/DataOrientedRoamScoring.h"
#include "algorithms/data_oriented_roam/DataOrientedRoamState.h"

#include <algorithm>
#include <bit>
#include <cmath>
#include <memory>
#include <numeric>
#include <stdexcept>
#include <utility>

namespace ParallelRoam::Algorithms::DataOrientedRoam::OrderingRelaxation
{
namespace
{
using Clock = std::chrono::steady_clock;
using Words = std::vector<std::uint64_t>;
constexpr auto SplitPass = TerrainLodPassId::SplitTopology;
constexpr std::size_t InspectionLimit = 64U;
constexpr std::size_t Capacity = 32U;

double Milliseconds(Clock::time_point begin)
{
    return std::chrono::duration<double, std::milli>(Clock::now() - begin).count();
}

void AddWork(TopologyOperationWork& sum, const TopologyOperationWork& value)
{
    sum.PrimitiveAttempts += value.PrimitiveAttempts;
    sum.PrimitiveCompleted += value.PrimitiveCompleted;
    sum.ForcedCompleted += value.ForcedCompleted;
    sum.NodesCreated += value.NodesCreated;
    sum.NodesReused += value.NodesReused;
    sum.NeighborAssignments += value.NeighborAssignments;
    sum.ActiveIndexCalls += value.ActiveIndexCalls;
    sum.QueueCalls += value.QueueCalls;
    sum.QueueMembershipUpdates += value.QueueMembershipUpdates;
    sum.MeshEditCalls += value.MeshEditCalls;
    sum.PathInsertCalls += value.PathInsertCalls;
}

bool Intersects(const Words& a, const Words& b)
{
    auto left = a.begin();
    auto right = b.begin();
    while (left != a.end() && right != b.end())
    {
        if (*left == *right) return true;
        if (*left < *right) ++left;
        else ++right;
    }
    return false;
}

std::uint64_t Path(const DataOrientedRoamState& state, DataOrientedRoamNodeIndex node)
{
    if (node == InvalidDataOrientedRoamNodeIndex) return 0U;
    if (!state.IsValidNode(node)) throw std::runtime_error{"logical projection contains invalid node"};
    return state.Nodes.PathIdAt(node);
}

void Float(Words& output, float value)
{
    output.push_back(std::bit_cast<std::uint32_t>(value));
}

void Append(Words& output, const Words& values)
{
    output.push_back(values.size());
    output.insert(output.end(), values.begin(), values.end());
}

Words SortedPaths(const DataOrientedRoamState& state, const std::vector<DataOrientedRoamNodeIndex>& nodes)
{
    Words result;
    for (const auto node : nodes) result.push_back(Path(state, node));
    std::sort(result.begin(), result.end());
    return result;
}

/// <summary>
/// 保留会影响后续更新的历史字段，不能只比较当前活动叶几何
/// 浮点字段保留位模式，避免容差掩盖资格和控制分歧
/// </summary>
Words NodeWords(const DataOrientedRoamState& state, DataOrientedRoamNodeIndex node)
{
    const auto& pool = state.Nodes;
    Words result{Path(state, node)};
    const auto& domain = pool.DomainAt(node);
    for (const auto point : {domain.A, domain.B, domain.C})
    {
        Float(result, point.x);
        Float(result, point.y);
    }
    // 关系属于持有该字段的节点；物理下标只用于查找，编码中一律映射回路径
    for (const auto related : {pool.ParentAt(node), pool.LeftChildAt(node), pool.RightChildAt(node),
        pool.BaseNeighborAt(node), pool.LeftNeighborAt(node), pool.RightNeighborAt(node)})
        result.push_back(Path(state, related));
    result.push_back(pool.InteriorChunkIdAt(node));
    Float(result, pool.GeometricErrorAt(node));
    Float(result, pool.ScreenErrorAt(node));
    result.insert(result.end(), {pool.VarianceIndexAt(node), pool.CreatedBuildIds[node],
        pool.ActivatedBuildIdAt(node), pool.SplitBuildIdAt(node), pool.MergeBuildIdAt(node),
        static_cast<std::uint64_t>(pool.DepthAt(node)), pool.VarianceTreeIndexAt(node),
        pool.ActivatedByForcedSplitAt(node), pool.IsSplitAt(node)});
    return result;
}

Words ProjectNodes(const DataOrientedRoamState& state, const Words& paths)
{
    std::vector<std::pair<std::uint64_t, DataOrientedRoamNodeIndex>> selected;
    for (std::size_t i = 0U; i < state.Nodes.size(); ++i)
        if (std::binary_search(paths.begin(), paths.end(), state.Nodes.PathIds[i]))
            selected.emplace_back(state.Nodes.PathIds[i], static_cast<DataOrientedRoamNodeIndex>(i));
    if (selected.size() != paths.size()) throw std::runtime_error{"planned logical resource is missing"};
    std::sort(selected.begin(), selected.end());
    Words result;
    for (const auto& entry : selected) Append(result, NodeWords(state, entry.second));
    return result;
}

Words SnapshotReads(const TopologySplitObservation& operation)
{
    Words result;
    // 根自身产生的子节点没有开始快照值，不能将其当成必须预先存在的资源
    std::set_difference(operation.ReadPaths.begin(), operation.ReadPaths.end(),
        operation.CreatedPaths.begin(), operation.CreatedPaths.end(), std::back_inserter(result));
    return result;
}

/// <summary>
/// 按根身份核对局部操作，前面独立根造成的预算起点变化是允许的
/// 堆维护次数可能随存储排列变化，继续记录，但不混入闭包等价条件
/// </summary>
bool SameOperation(const TopologySplitObservation& planned, const TopologySplitObservation& actual)
{
    if (planned.RootPath != actual.RootPath || planned.RootSucceeded != actual.RootSucceeded ||
        planned.LocalFootprintComplete != actual.LocalFootprintComplete ||
        planned.ReadPaths != actual.ReadPaths || planned.WrittenPaths != actual.WrittenPaths ||
        planned.CreatedPaths != actual.CreatedPaths || planned.CompletedPaths != actual.CompletedPaths ||
        planned.Attempts.size() != actual.Attempts.size() || planned.PeakBudgetUse != actual.PeakBudgetUse ||
        planned.BudgetRejected != actual.BudgetRejected ||
        planned.BudgetBefore - planned.BudgetAfter != actual.BudgetBefore - actual.BudgetAfter)
        return false;
    for (std::size_t i = 0U; i < planned.Attempts.size(); ++i)
    {
        const auto& a = planned.Attempts[i];
        const auto& b = actual.Attempts[i];
        if (a.Path != b.Path || a.Forced != b.Forced || a.Completed != b.Completed) return false;
    }
    // 跨根预算起点可以变化，比较相对预留轨迹，不能只验证最终净增量
    if (planned.BudgetRemaining.size() != actual.BudgetRemaining.size()) return false;
    for (std::size_t i = 0U; i < planned.BudgetRemaining.size(); ++i)
        if (planned.BudgetBefore - planned.BudgetRemaining[i] != actual.BudgetBefore - actual.BudgetRemaining[i])
            return false;
    return true;
}

void Accumulate(BatchApplication& application, const TopologySplitStep& step,
    const TopologyConvergenceObservation& observation)
{
    application.Steps.push_back(step);
    for (const auto& root : observation.Roots)
    {
        application.Roots.push_back(root);
        AddWork(application.Work, root.Work);
    }
    application.MergeAttempts += observation.MergeAttempts;
    application.MergeFailures += observation.MergeFailures;
    application.PrimitiveMerges += observation.PrimitiveMerges;
    application.CoordinatorQueueCalls += observation.CoordinatorQueueCalls;
}

/// <summary>
/// 正式应用和排列审计共用同一检查，审计只豁免严格前缀的根排序要求
/// 任一控制中断都保留真实前态演化，不将已应用部分重新认证为短批次
/// </summary>
BatchApplication ApplyOrdered(DataOrientedRoamState& state, const BatchPlan& plan,
    TopologySplitIteration& position, const std::vector<std::size_t>& order, bool permutation)
{
    const auto begin = Clock::now();
    BatchApplication application;
    try
    {
        if (plan.InputHash != HashDataOrientedRoamPassInput(state, SplitPass) ||
            plan.Position.Iteration != position.Iteration || plan.Position.MaximumIterations != position.MaximumIterations ||
            plan.Position.Stop != position.Stop)
            throw std::runtime_error{"batch-start state or iteration changed"};
        if (plan.ResourceLimited)
        {
            application.Status = ApplicationStatus::ResourceLimited;
            application.Detail = "batch construction reached the variant deadline";
        }
        else if (plan.StrictFallback)
        {
            TopologyConvergenceObservation observation;
            const auto step = AdvancePlannedSplitIteration(state, position, 0U, observation);
            Accumulate(application, step, observation);
        }
        else
        {
            application.Status = ApplicationStatus::ValidatedBatch;
            for (const auto index : order)
            {
                const auto& member = plan.Members.at(index);
                const auto decision = InspectSplitIteration(state, position);
                // 审计只置换细分根；正式严格前缀还必须逐步匹配严格头
                if (decision.Stop != TopologySplitStop::Running || decision.Merge ||
                    (!permutation && plan.Configuration.Mode == OrderingMode::StrictPrefix &&
                        decision.Path != member.Operation.RootPath))
                {
                    application.Status = ApplicationStatus::ControlInterrupted;
                    application.Detail = "hard control boundary or new strict head";
                    break;
                }
                if (ProjectNodes(state, SnapshotReads(member.Operation)) != member.ReadState)
                    throw std::runtime_error{"pre-root logical read state differs from snapshot"};
                const auto found = std::find(state.Nodes.PathIds.begin(), state.Nodes.PathIds.end(), member.Operation.RootPath);
                const auto root = static_cast<DataOrientedRoamNodeIndex>(found - state.Nodes.PathIds.begin());
                const auto slot = state.NodeMembership.at(root).SplitQueuePosition;
                if (slot >= state.SplitQueue.size() || state.SplitQueue[slot].Score != member.Score)
                    throw std::runtime_error{"planned root queue score changed"};
                TopologyConvergenceObservation observation;
                const auto step = AdvancePlannedSplitIteration(state, position, member.Operation.RootPath, observation);
                Accumulate(application, step, observation);
                if (!step.SplitSucceeded || observation.Roots.size() != 1U ||
                    !SameOperation(member.Operation, observation.Roots.front()) ||
                    ProjectNodes(state, member.Operation.WrittenPaths) != member.WrittenState)
                    throw std::runtime_error{"root closure, resource trajectory or local effect differs from plan"};
            }
            if (application.Status == ApplicationStatus::ValidatedBatch &&
                !CaptureDataOrientedRoamPassEvidence(state, SplitPass).Correct)
            {
                application.Status = ApplicationStatus::InvalidResult;
                application.Detail = "topology, queue, mesh edits or budget validation failed";
            }
        }
    }
    catch (const std::bad_alloc&)
    {
        application.Status = ApplicationStatus::ResourceLimited;
        application.Detail = "allocation failed";
    }
    catch (const std::exception& error)
    {
        application.Status = ApplicationStatus::PlanMismatch;
        application.Detail = error.what();
    }
    application.Milliseconds = Milliseconds(begin);
    return application;
}

/// <summary>
/// 比较同源批次的后继语义；配置和借用地形已由开始身份约束
/// 规范化存储位置的同时保留队列资格、资源所有权和实际网格效果
/// </summary>
Words LogicalState(const DataOrientedRoamState& state, const TopologySplitIteration& position)
{
    auto paths = state.Nodes.PathIds;
    std::sort(paths.begin(), paths.end());
    Words result = ProjectNodes(state, paths);
    Append(result, SortedPaths(state, state.ActiveLeafNodes));
    Append(result, SortedPaths(state, state.ActiveInternalNodes));
    for (const auto* set : {&state.PreviousSplitPaths, &state.CurrentSplitPaths})
    {
        Words values(set->begin(), set->end());
        std::sort(values.begin(), values.end());
        Append(result, values);
    }
    std::vector<Words> memberships;
    for (std::size_t i = 0U; i < state.Nodes.size(); ++i)
    {
        const auto& member = state.NodeMembership.at(i);
        memberships.push_back({state.Nodes.PathIds[i], state.SplitQueueBlockedBuildIds.at(i),
            member.ActiveLeafPosition != InvalidDataOrientedRoamPosition,
            member.ActiveInternalPosition != InvalidDataOrientedRoamPosition,
            member.SplitQueuePosition != InvalidDataOrientedRoamPosition,
            member.MergeQueuePosition != InvalidDataOrientedRoamPosition,
            Path(state, member.MergeQueueRepresentative), Path(state, member.MergeQueuePartner)});
    }
    std::sort(memberships.begin(), memberships.end());
    for (const auto& member : memberships) Append(result, member);
    const auto queue = [&](const auto& entries) {
        std::vector<Words> values;
        for (const auto& entry : entries)
        {
            Words item{Path(state, entry.Node)};
            Float(item, entry.Score);
            values.push_back(std::move(item));
        }
        std::sort(values.begin(), values.end());
        result.push_back(values.size());
        for (const auto& value : values) Append(result, value);
    };
    queue(state.SplitQueue);
    queue(state.MergeQueue);
    const auto next = InspectSplitIteration(state, position);
    result.insert(result.end(), {state.RemainingSerialSplitBudget,
        state.RemainingParallelSplitBudget.load(std::memory_order_relaxed), position.Iteration,
        position.MaximumIterations, static_cast<std::uint64_t>(position.Stop),
        static_cast<std::uint64_t>(next.Stop), next.Merge, next.Path});
    Float(result, next.Score);

    // 原元数据未在批内应用，新增日志按逻辑操作对齐；真实顺序另由原 replay 验证器检查
    const auto& metadata = state.IncrementalMesh.Metadata;
    std::vector<Words> edits;
    for (const auto& edit : metadata.TopologyEdits)
        edits.push_back({Path(state, edit.Node), static_cast<std::uint64_t>(edit.Type)});
    std::sort(edits.begin(), edits.end());
    result.push_back(edits.size());
    for (const auto& edit : edits) Append(result, edit);
    result.insert(result.end(), {metadata.Generation, metadata.RequiresFullUpload,
        metadata.NeedsInitialization, metadata.TracksTopologyEdits});
    Append(result, SortedPaths(state, metadata.DebugTransitionLeaves));
    std::vector<Words> slots;
    for (std::size_t slot = 0U; slot < metadata.SlotOwners.size(); ++slot)
        slots.push_back({Path(state, metadata.SlotOwners[slot]), metadata.SlotDirtyGenerations.at(slot)});
    std::sort(slots.begin(), slots.end());
    result.push_back(slots.size());
    for (const auto& slot : slots) Append(result, slot);
    Words dirtyOwners;
    for (const auto slot : metadata.DirtySlots) dirtyOwners.push_back(Path(state, metadata.SlotOwners.at(slot)));
    std::sort(dirtyOwners.begin(), dirtyOwners.end());
    Append(result, dirtyOwners);
    std::vector<Words> reverseSlots;
    for (std::size_t node = 0U; node < metadata.NodeSlots.size(); ++node)
    {
        const auto slot = metadata.NodeSlots[node];
        reverseSlots.push_back({Path(state, static_cast<DataOrientedRoamNodeIndex>(node)),
            slot == InvalidDataOrientedRoamPosition ? 0U : Path(state, metadata.SlotOwners.at(slot))});
    }
    std::sort(reverseSlots.begin(), reverseSlots.end());
    for (const auto& slot : reverseSlots) Append(result, slot);
    Words updateOwners;
    for (const auto& range : metadata.UpdateRanges)
        for (std::size_t i = 0U; i < range.TriangleCount; ++i)
            updateOwners.push_back(Path(state, metadata.SlotOwners.at(range.FirstTriangle + i)));
    std::sort(updateOwners.begin(), updateOwners.end());
    Append(result, updateOwners);

    DataOrientedRoamState emitted{state};
    if (!CaptureDataOrientedRoamPassEvidence(emitted, SplitPass).Correct)
        throw std::runtime_error{"permutation has invalid topology or edit replay"};
    ConfigureDataOrientedRoamPassAction(emitted, TerrainLodPassId::MeshEmit, TerrainLodPassAction::SerialFull, 1U);
    ExecuteDataOrientedRoamPass(emitted, TerrainLodPassId::MeshEmit);
    const auto mesh = CaptureDataOrientedRoamPassEvidence(emitted, TerrainLodPassId::MeshEmit);
    if (!mesh.Correct) throw std::runtime_error{"permutation cannot emit valid geometry"};
    result.push_back(mesh.ResultHash);
    return result;
}
} // 匿名命名空间

const char* RejectionName(RejectionReason reason)
{
    constexpr std::array names{"accepted", "priority_window", "inspection_limit", "batch_capacity",
        "dependency_unknown", "shared_forced_closure", "write_write_overlap", "read_write_dependency",
        "coordinator_dependency", "budget_reservation_conflict", "node_resource_allocation_conflict",
        "operation_failed", "prefix_stopped_not_inspected", "head_failed_not_inspected", "resource_limit_not_inspected"};
    return names.at(static_cast<std::size_t>(reason));
}

const char* ApplicationName(ApplicationStatus status)
{
    constexpr std::array names{"validated_batch", "strict_step", "control_interrupted", "plan_mismatch",
        "invalid_result", "resource_limited"};
    return names.at(static_cast<std::size_t>(status));
}

RejectionReason DependencyConflict(const TopologySplitObservation& a, const TopologySplitObservation& b)
{
    if (!a.LocalFootprintComplete || !b.LocalFootprintComplete) return RejectionReason::DependencyUnknown;
    Words closureA, closureB;
    for (const auto& attempt : a.Attempts) closureA.push_back(attempt.Path);
    for (const auto& attempt : b.Attempts) closureB.push_back(attempt.Path);
    std::sort(closureA.begin(), closureA.end());
    std::sort(closureB.begin(), closureB.end());
    if (Intersects(closureA, closureB)) return RejectionReason::SharedForcedClosure;
    if (Intersects(a.WrittenPaths, b.WrittenPaths)) return RejectionReason::WriteWriteOverlap;
    if (Intersects(a.WrittenPaths, b.ReadPaths) || Intersects(b.WrittenPaths, a.ReadPaths))
        return RejectionReason::ReadWriteDependency;
    return RejectionReason::Accepted;
}

BatchPlan BuildBatch(const DataOrientedRoamState& state, const OrderingConfiguration& configuration,
    const TopologySplitIteration& position, ExperimentDeadline deadline)
{
    const auto begin = Clock::now();
    if (!std::isfinite(configuration.Alpha) ||
        (configuration.Mode == OrderingMode::PriorityBand ? configuration.Alpha <= 0.0 || configuration.Alpha > 1.0
                                                        : configuration.Alpha != 0.0))
        throw std::invalid_argument{"invalid ordering relaxation configuration"};
    BatchPlan plan;
    plan.Configuration = configuration;
    plan.InputHash = HashDataOrientedRoamPassInput(state, SplitPass);
    plan.Position = position;
    const auto decision = InspectSplitIteration(state, position);
    if (configuration.Mode == OrderingMode::Strict || decision.Merge || decision.Stop != TopologySplitStop::Running)
    {
        plan.ConstructionMilliseconds = Milliseconds(begin);
        return plan;
    }
    // 先保留完整严格顺序，前缀模式不能先删掉不合格项再继续找成员
    auto candidates = state.SplitQueue;
    for (const auto& entry : candidates)
        if (!state.IsValidNode(entry.Node) || !std::isfinite(entry.Score))
            throw std::invalid_argument{"invalid frozen split candidate"};
    std::sort(candidates.begin(), candidates.end(), [&](const auto& a, const auto& b) {
        return SplitPriorityPrecedes(state, a.Node, a.Score, b.Node, b.Score);
    });
    plan.MaximumScore = decision.Score;
    plan.MinimumScore = configuration.Mode == OrderingMode::StrictPrefix ? -std::numeric_limits<double>::infinity()
        : (1.0 - configuration.Alpha) * static_cast<double>(decision.Score);
    if (decision.Score <= 0.0F)
    {
        plan.ConstructionMilliseconds = Milliseconds(begin);
        return plan;
    }

    bool prefixStopped = false;
    bool headFailed = false;
    for (std::size_t rank = 0U; rank < candidates.size(); ++rank)
    {
        const auto& candidate = candidates[rank];
        if (!ShouldSplitWithScore(state, candidate.Node, candidate.Score))
        {
            if (configuration.Mode == OrderingMode::StrictPrefix) prefixStopped = true;
            continue;
        }
        ++plan.CandidateCount;
        if (static_cast<double>(candidate.Score) < plan.MinimumScore)
        {
            ++plan.Reasons[static_cast<std::size_t>(RejectionReason::PriorityWindow)];
            continue;
        }
        ++plan.InBandCount;
        RejectionReason limit = RejectionReason::Accepted;
        if (plan.ResourceLimited) limit = RejectionReason::ResourceLimit;
        else if (headFailed) limit = RejectionReason::HeadFailed;
        else if (prefixStopped) limit = RejectionReason::PrefixStopped;
        else if (plan.Inspected.size() >= InspectionLimit) limit = RejectionReason::InspectionLimit;
        else if (plan.Members.size() >= Capacity) limit = RejectionReason::BatchCapacity;
        if (limit != RejectionReason::Accepted)
        {
            ++plan.Reasons[static_cast<std::size_t>(limit)];
            continue;
        }
        if (Clock::now() >= deadline)
        {
            plan.ResourceLimited = true;
            // 结束试算后仍补齐已有候选的分母，不把超时遗漏的一项当成已检查拒绝
            ++plan.Reasons[static_cast<std::size_t>(RejectionReason::ResourceLimit)];
            continue;
        }
        const auto trialBegin = Clock::now();
        // 每根独立复制同一个输入；受控入口复用 AnalyzeSplitOperation 的真实观察操作
        // 保留试算后状态是为了记录局部效果，不能用它作为下一候选的来源
        DataOrientedRoamState trial{state};
        auto trialPosition = position;
        TopologyConvergenceObservation observation;
        const auto step = AdvancePlannedSplitIteration(trial, trialPosition, Path(state, candidate.Node), observation);
        if (observation.Roots.size() != 1U || !step.SplitAttempted)
            throw std::runtime_error{"snapshot candidate did not enter a split operation"};
        PlannedSplit member;
        member.Score = candidate.Score;
        member.Operation = std::move(observation.Roots.front());
        AddWork(plan.TrialWork, member.Operation.Work);
        plan.TrialMerges += observation.PrimitiveMerges;
        CandidateDecision checked{member.Operation.RootPath, rank, candidate.Score};
        checked.Observation = member.Operation;
        if (!member.Operation.LocalFootprintComplete) checked.Reason = RejectionReason::DependencyUnknown;
        for (const auto& accepted : plan.Members)
        {
            const auto conflict = DependencyConflict(member.Operation, accepted.Operation);
            // 原因优先级对全部已选根统一裁决，不能由先遇到哪一个冲突对象决定
            if (conflict != RejectionReason::Accepted && (checked.Reason == RejectionReason::Accepted ||
                static_cast<int>(conflict) < static_cast<int>(checked.Reason)))
            {
                checked.Reason = conflict;
                checked.ConflictPath = accepted.Operation.RootPath;
            }
        }
        if (checked.Reason == RejectionReason::Accepted &&
            member.Operation.PeakBudgetUse > state.RemainingSerialSplitBudget - plan.PeakReservation)
            checked.Reason = RejectionReason::BudgetReservation;
        if (checked.Reason == RejectionReason::Accepted && member.Operation.Work.NodesCreated >
            static_cast<std::size_t>(InvalidDataOrientedRoamNodeIndex) - state.Nodes.size() - plan.NewNodes)
            checked.Reason = RejectionReason::NodeAllocation;
        if (checked.Reason == RejectionReason::Accepted && !member.Operation.RootSucceeded)
            checked.Reason = RejectionReason::OperationFailed;
        // 失败试算同样产生工作计数，只有完整成功的根才能进入封闭成员集合
        if (checked.Reason == RejectionReason::Accepted)
        {
            member.ReadState = ProjectNodes(state, SnapshotReads(member.Operation));
            member.WrittenState = ProjectNodes(trial, member.Operation.WrittenPaths);
            plan.PeakReservation += member.Operation.PeakBudgetUse;
            plan.NewNodes += member.Operation.Work.NodesCreated;
            plan.LastAcceptedRank = rank;
            plan.Members.push_back(std::move(member));
        }
        else
        {
            if (configuration.Mode == OrderingMode::StrictPrefix) prefixStopped = true;
            if (rank == 0U) headFailed = true;
        }
        plan.TrialMilliseconds += Milliseconds(trialBegin);
        ++plan.Reasons[static_cast<std::size_t>(checked.Reason)];
        plan.Inspected.push_back(checked);
    }
    plan.StrictFallback = plan.Members.empty();
    plan.ConstructionMilliseconds = Milliseconds(begin);
    return plan;
}

BatchApplication ApplyBatch(DataOrientedRoamState& state, const BatchPlan& plan, TopologySplitIteration& position)
{
    std::vector<std::size_t> order(plan.Members.size());
    std::iota(order.begin(), order.end(), 0U);
    return ApplyOrdered(state, plan, position, order, false);
}

PermutationAudit AuditBatch(const DataOrientedRoamState& source, const BatchPlan& plan, ExperimentDeadline deadline)
{
    PermutationAudit audit;
    if (plan.Members.size() < 2U || plan.StrictFallback) return audit;
    const auto begin = Clock::now();
    audit.Performed = true;
    std::vector<std::size_t> forward(plan.Members.size());
    std::iota(forward.begin(), forward.end(), 0U);
    std::vector<std::vector<std::size_t>> orders;
    if (forward.size() <= 4U)
    {
        do { orders.push_back(forward); } while (std::next_permutation(forward.begin(), forward.end()));
    }
    else
    {
        orders.push_back(forward);
        auto reverse = forward;
        std::reverse(reverse.begin(), reverse.end());
        orders.push_back(reverse);
        auto rotated = forward;
        std::rotate(rotated.begin(), rotated.begin() + 1, rotated.end());
        orders.push_back(rotated);
        std::swap(forward[0], forward[1]);
        orders.push_back(forward);
    }
    Words expected;
    try
    {
        for (const auto& order : orders)
        {
            if (Clock::now() >= deadline)
            {
                audit.ResourceLimited = true;
                throw std::runtime_error{"audit resource limit; evidence incomplete"};
            }
            DataOrientedRoamState state{source};
            auto position = plan.Position;
            const auto application = ApplyOrdered(state, plan, position, order, true);
            ++audit.Applications;
            AddWork(audit.Cost, application.Work);
            if (application.Status != ApplicationStatus::ValidatedBatch)
                throw std::runtime_error{std::string{ApplicationName(application.Status)} + ": " + application.Detail};
            const auto projected = LogicalState(state, position);
            if (audit.Applications == 1U) expected = projected;
            else if (projected != expected)
            {
                const auto mismatch = std::mismatch(projected.begin(), projected.end(), expected.begin(), expected.end());
                throw std::runtime_error{"logical state differs at word " + std::to_string(mismatch.first - projected.begin())};
            }
        }
        audit.Passed = true;
    }
    catch (const std::exception& error)
    {
        audit.Difference = "permutation " + std::to_string(audit.Applications) + ": " + error.what();
    }
    audit.Milliseconds = Milliseconds(begin);
    return audit;
}

OrderingResult RunOrderingExperiment(DataOrientedRoamState& state, const OrderingConfiguration& configuration,
    const BatchObserver& observer, bool auditFirstBatch)
{
    const auto begin = Clock::now();
    const auto deadline = begin + std::chrono::seconds{180};
    OrderingResult result;
    result.Position = BeginStrictSplitIteration(state);
    try
    {
        while (result.Position.Stop == TopologySplitStop::Running)
        {
            if (Clock::now() >= deadline)
            {
                result.Status = ApplicationStatus::ResourceLimited;
                result.Detail = "variant wall-clock limit";
                break;
            }
            const auto plan = BuildBatch(state, configuration, result.Position, deadline);
            // 只在可能执行审计时保存开始状态，不为每个根长期保存完整副本
            std::unique_ptr<DataOrientedRoamState> auditSource;
            if (auditFirstBatch && !result.Audit.Performed && plan.Members.size() > 1U)
                auditSource = std::make_unique<DataOrientedRoamState>(state);
            const auto application = ApplyBatch(state, plan, result.Position);
            ++result.Batches;
            result.Status = application.Status;
            result.Detail = application.Detail;
            // 控制中断和失败前的实际工作也要入账，不能随独立资格一起丢弃
            AddWork(result.ActualWork, application.Work);
            AddWork(result.TrialWork, plan.TrialWork);
            result.RootAttempts += application.Roots.size();
            result.PrimitiveMerges += application.PrimitiveMerges;
            result.MergeAttempts += application.MergeAttempts;
            result.MergeFailures += application.MergeFailures;
            result.CoordinatorQueueCalls += application.CoordinatorQueueCalls;
            result.Decisions.insert(result.Decisions.end(), application.Steps.begin(), application.Steps.end());
            for (std::size_t i = 0U; i < result.Reasons.size(); ++i) result.Reasons[i] += plan.Reasons[i];
            if (application.Status == ApplicationStatus::ValidatedBatch)
            {
                ++result.ValidatedBatches;
                result.MaximumWidth = std::max(result.MaximumWidth, plan.Members.size());
                if (plan.Members.size() > 1U)
                {
                    ++result.MultiBatches;
                    result.FeasibleRoots += application.Roots.size();
                    result.FeasiblePrimitives += application.Work.PrimitiveCompleted;
                }
            }
            if (application.Status == ApplicationStatus::ControlInterrupted) ++result.InterruptedBatches;
            if (observer) observer(result.Batches - 1U, plan, application);
            if (application.Status == ApplicationStatus::PlanMismatch || application.Status == ApplicationStatus::InvalidResult ||
                application.Status == ApplicationStatus::ResourceLimited) break;
            if (auditSource && application.Status == ApplicationStatus::ValidatedBatch)
            {
                result.Audit = AuditBatch(*auditSource, plan, std::min(deadline, Clock::now() + std::chrono::minutes{5}));
                if (!result.Audit.Passed)
                {
                    result.Status = result.Audit.ResourceLimited ? ApplicationStatus::ResourceLimited : ApplicationStatus::PlanMismatch;
                    result.Detail = (result.Audit.ResourceLimited ? "audit_incomplete: " : "dependency_model_mismatch: ") +
                        result.Audit.Difference;
                    // 同模型正向证据暂停；此前明细保留，但不继续汇报已认证独立总量
                    result.FeasibleRoots = 0U;
                    result.FeasiblePrimitives = 0U;
                    break;
                }
            }
        }
        if (result.Position.Stop != TopologySplitStop::Running && !CaptureDataOrientedRoamPassEvidence(state, SplitPass).Correct)
        {
            result.Status = ApplicationStatus::InvalidResult;
            result.Detail = "final topology, queue, budget or mesh edits invalid";
        }
    }
    catch (const std::bad_alloc&)
    {
        result.Status = ApplicationStatus::ResourceLimited;
        result.Detail = "allocation failed";
    }
    catch (const std::exception& error)
    {
        result.Status = ApplicationStatus::PlanMismatch;
        result.Detail = error.what();
    }
    result.Milliseconds = Milliseconds(begin);
    return result;
}
} // 命名空间 ParallelRoam::Algorithms::DataOrientedRoam::OrderingRelaxation
