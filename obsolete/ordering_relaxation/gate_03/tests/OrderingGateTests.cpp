#include "DataOrientedRoamExperimentTestSupport.h"
#include "algorithms/data_oriented_roam/DataOrientedRoamPassEvidence.h"
#include "algorithms/data_oriented_roam/DataOrientedRoamPassInput.h"
#include "algorithms/data_oriented_roam/DataOrientedRoamPassMeasurement.h"
#include "algorithms/data_oriented_roam/DataOrientedRoamPipeline.h"
#include "algorithms/data_oriented_roam/DataOrientedRoamTopologyExperiment.h"
#include "algorithms/data_oriented_roam/DataOrientedRoamStateOps.h"
#include "algorithms/data_oriented_roam/DataOrientedRoamScoring.h"
#include "algorithms/data_oriented_roam/DataOrientedRoamQueues.h"
#include "algorithms/data_oriented_roam/ordering_relaxation/OrderingExperiment.h"
#include "benchmark/formal/FormalCpuInput.h"
#include "experiment/formal/FormalExperimentCamera.h"
#include "experiment/formal/FormalExperimentManifest.h"
#include "experiment/ExperimentCsvCodec.h"

#include <array>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <sstream>
#include <string>
#include <algorithm>
#include <iomanip>

using namespace ParallelRoam;
using namespace Tests;
using namespace Experiment::Formal;
using namespace Algorithms::DataOrientedRoam::OrderingRelaxation;

namespace
{
constexpr auto SplitPass = TerrainLodPassId::SplitTopology;

/// <summary>
/// 从旧执行前工作量分层冻结的输入身份，锚点另行保留
/// 这些输入不随本轮观察到的闭包长度或成功率调整
/// </summary>
struct NaturalInput
{
    const char* Scenario;
    std::size_t Sample;
    std::uint64_t Hash;
    // 首轮严格观察留下的根尝试数，用于单步提取后的控制流对照
    std::size_t StrictRootAttempts;
};

constexpr std::array<NaturalInput, 4> NaturalInputs{{
    {"test129-a-b4096", 5U, 2878997366482998522ULL, 9U},
    {"peking547-a-b20000", 9U, 10820725050463347417ULL, 37U},
    {"peking547-a-b80000", 60U, 2459220430983787707ULL, 144U},
    {"peking547-a-b20000", 14U, 4677929227313376363ULL, 40U},
}};

void PrepareExecution(DataOrientedRoamState& state)
{
    state.Stats = {};
    ConfigureDataOrientedRoamPassAction(state, SplitPass, TerrainLodPassAction::SerialImmediate, 1U);
    state.Settings.EnablePassEvidence = false;
    state.Settings.EnableTopologyValidation = false;
    state.Settings.EnableTopologyPairEvidence = false;
}

std::string CaptureResult(DataOrientedRoamState& state)
{
    const auto evidence = CaptureDataOrientedRoamPassEvidence(state, SplitPass);
    Require(evidence.Correct, "strict result violates topology, queue, budget or mesh edits");
    std::ostringstream result;
    // 输入编码还覆盖有序队列、分数和历史状态，不能只用合法性或叶集合代替后继状态
    result << evidence.ResultHash << ',' << HashDataOrientedRoamPassInput(state, SplitPass)
        << ',' << state.ActiveLeafNodes.size() << ',' << state.RemainingSerialSplitBudget
        << ',' << state.Stats.SplitCount << ',' << state.Stats.ForcedSplitCount
        << ',' << state.Stats.MergeCount << ',' << state.Stats.RejectedSplitCount
        << ',' << state.Stats.BudgetRejectedSplitCount << ',' << state.Stats.RejectedMergeCount
        << ',' << state.Stats.QueueCrossoverCount;
    ConfigureDataOrientedRoamPassAction(state, TerrainLodPassId::MeshEmit, TerrainLodPassAction::SerialFull, 1U);
    ExecuteDataOrientedRoamPass(state, TerrainLodPassId::MeshEmit);
    const auto mesh = CaptureDataOrientedRoamPassEvidence(state, TerrainLodPassId::MeshEmit);
    Require(mesh.Correct, "strict result cannot emit a valid full mesh");
    result << ',' << mesh.ResultHash;
    return result.str();
}

bool Contains(const std::vector<std::uint64_t>& paths, std::uint64_t path)
{
    return std::binary_search(paths.begin(), paths.end(), path);
}

DataOrientedRoamState MakeFixture(const Terrain::HeightMap& height)
{
    DataOrientedRoamState state;
    state.HeightMap = &height;
    state.Settings.MaxDepth = 4;
    state.Settings.TriangleBudget = 32U;
    state.Settings.EnableLocalConstraints = true;
    state.BuildSequence = 1U;
    ResetTopology(state);
    state.RemainingSerialSplitBudget = 30U;
    return state;
}

void CheckObservation()
{
    const Terrain::HeightMap flat;
    auto direct = MakeFixture(flat);
    direct.Settings.EnableLocalConstraints = false;
    const StateSnapshot before{direct};
    const auto root = direct.RootA;
    const auto path = direct.Nodes.PathIdAt(root);
    const auto observed = AnalyzeSplitOperation(direct, root);
    Require(before == StateSnapshot{direct}, "single-root analysis changed its source");
    Require(observed.RootSucceeded && observed.LocalFootprintComplete &&
        observed.Work.PrimitiveAttempts == 1U && observed.Work.PrimitiveCompleted == 1U &&
        observed.Work.NodesCreated == 2U && observed.PeakBudgetUse == 1U &&
        observed.BudgetAfter + 1U == observed.BudgetBefore, "direct split accounting");
    Require(Contains(observed.ReadPaths, direct.Nodes.PathIdAt(direct.RootB)) &&
        !Contains(observed.WrittenPaths, direct.Nodes.PathIdAt(direct.RootB)),
        "a leaf base neighbor is read even when linking does not write it");
    Require(Contains(observed.WrittenPaths, path) && Contains(observed.WrittenPaths, path * 2U) &&
        Contains(observed.WrittenPaths, path * 2U + 1U) && observed.Work.NeighborAssignments == 10U,
        "new parent/children and same-value neighbor clears must be recorded");

    // 为同一父节点预建历史子节点，确认复用不被计作新分配
    const auto children = SplitTriangleDomain(direct.Nodes.DomainAt(root));
    const auto left = AddNode(direct, children.Left, root, 1, path * 2U, 0U, 1U);
    const auto right = AddNode(direct, children.Right, root, 1, path * 2U + 1U, 0U, 2U);
    direct.Nodes.LeftChildren[root] = left;
    direct.Nodes.RightChildren[root] = right;
    const auto reused = AnalyzeSplitOperation(direct, root);
    Require(reused.RootSucceeded && reused.Work.NodesCreated == 0U && reused.Work.NodesReused == 2U &&
        reused.CreatedPaths.empty() && Contains(reused.WrittenPaths, path * 2U), "historical child reuse");

    auto diamond = MakeFixture(flat);
    const auto forced = AnalyzeSplitOperation(diamond, diamond.RootA);
    Require(forced.RootSucceeded && forced.Work.PrimitiveAttempts == 2U && forced.Work.ForcedCompleted == 1U &&
        forced.CompletedPaths == std::vector<std::uint64_t>{diamond.Nodes.PathIdAt(diamond.RootB), 1U} &&
        forced.PeakBudgetUse == 2U && forced.Work.NodesCreated == 4U, "forced diamond completion order");
    diamond.RemainingSerialSplitBudget = 1U;
    const auto rejected = AnalyzeSplitOperation(diamond, diamond.RootA);
    Require(!rejected.RootSucceeded && rejected.Work.PrimitiveCompleted == 0U && rejected.BudgetRejected == 1U &&
        rejected.BudgetBefore == rejected.BudgetAfter && rejected.BudgetRemaining == std::vector<std::size_t>{1U, 0U, 0U, 1U},
        "failed forced split must release its ancestor reservation");

    // 构造三根依赖图：先完成 B/C 菱形，再因不足以细分新对侧而令 A 失败
    // 此图只检查递归与预算语义，不作为自然地形质量或合法性证据
    auto partial = MakeFixture(flat);
    const auto b = partial.RootA;
    const auto c = partial.RootB;
    const auto a = AddNode(partial, partial.Nodes.DomainAt(b), InvalidDataOrientedRoamNodeIndex, 1, 32U, 0U, 0U);
    partial.Nodes.BaseNeighbors[a] = b;
    partial.Nodes.LeftNeighbors[b] = a;
    partial.ActiveLeafNodes.push_back(a);
    partial.NodeMembership[a].ActiveLeafPosition = 2U;
    InsertPersistentSplitQueueNode(partial, a);
    partial.RemainingSerialSplitBudget = 3U;
    const auto incomplete = AnalyzeSplitOperation(partial, a);
    Require(!incomplete.RootSucceeded && incomplete.LocalFootprintComplete && incomplete.Work.PrimitiveCompleted == 2U &&
        incomplete.Work.ForcedCompleted == 2U && incomplete.BudgetAfter == 1U && incomplete.PeakBudgetUse == 3U &&
        incomplete.CompletedPaths == std::vector<std::uint64_t>{partial.Nodes.PathIdAt(c), partial.Nodes.PathIdAt(b)},
        "partial failure must retain both completed forced primitives");
    Require(Contains(incomplete.WrittenPaths, 32U) && !incomplete.Attempts.front().Completed,
        "a failed root may still receive neighbor writes from completed forced work");

    auto depth = MakeFixture(flat);
    depth.Settings.MaxDepth = 0;
    const auto stopped = AnalyzeSplitOperation(depth, depth.RootA);
    Require(!stopped.RootSucceeded && stopped.ReadPaths == std::vector<std::uint64_t>{1U} &&
        stopped.WrittenPaths.empty() && stopped.BudgetBefore == stopped.BudgetAfter, "depth rejection footprint");
    const auto unknown = AnalyzeSplitOperation(depth, static_cast<DataOrientedRoamNodeIndex>(depth.Nodes.size()));
    Require(!unknown.RootSucceeded && !unknown.LocalFootprintComplete, "unknown node cannot claim complete dependencies");
}

void SetSplitScores(DataOrientedRoamState& state, float score)
{
    for (auto& entry : state.SplitQueue) entry.Score = score;
    std::sort(state.SplitQueue.begin(), state.SplitQueue.end(), [&state](const auto& a, const auto& b) {
        return SplitPriorityPrecedes(state, a.Node, a.Score, b.Node, b.Score);
    });
    for (std::size_t i = 0U; i < state.SplitQueue.size(); ++i)
        state.NodeMembership[state.SplitQueue[i].Node].SplitQueuePosition = static_cast<DataOrientedRoamPosition>(i);
}

void CheckStrictSteps()
{
    const Terrain::HeightMap flat;
    auto ties = MakeFixture(flat);
    ties.Settings.SplitThreshold = 10.0F;
    ties.Settings.MergeThreshold = 1.0F;
    SetSplitScores(ties, 8.0F);
    ties.PreviousSplitPaths.insert(ties.Nodes.PathIdAt(ties.RootB));
    auto iteration = BeginStrictSplitIteration(ties);
    const StateSnapshot beforeStop{ties};
    const auto stopped = AdvanceStrictSplitIteration(ties, iteration);
    Require(!stopped.SplitAttempted && iteration.Stop == TopologySplitStop::NoEligibleSplit &&
        iteration.Iteration == 1U && beforeStop == StateSnapshot{ties}, "equal-score head stop cannot skip to eligible suffix");
    (void)AdvanceStrictSplitIteration(ties, iteration);
    Require(iteration.Iteration == 1U && beforeStop == StateSnapshot{ties}, "stopped iteration must be idempotent");
    ties.PreviousSplitPaths.insert(1U);
    iteration = BeginStrictSplitIteration(ties);
    const auto hysteresis = AdvanceStrictSplitIteration(ties, iteration);
    Require(hysteresis.SplitSucceeded && hysteresis.SplitPath == 1U, "equal-score hysteresis follows strict PathId head");

    auto exchange = MakeFixture(flat);
    exchange.Settings.SplitThreshold = -1.0F;
    auto seed = BeginStrictSplitIteration(exchange);
    Require(AdvanceStrictSplitIteration(exchange, seed).SplitSucceeded && exchange.ActiveLeafNodes.size() == 4U,
        "budget exchange fixture requires a completed diamond");
    ++exchange.BuildSequence;
    exchange.Settings.TriangleBudget = 4U;
    exchange.Settings.SplitThreshold = 1.0F;
    exchange.Settings.MergeThreshold = 0.0F;
    InitializePersistentMergeQueue(exchange);
    SetSplitScores(exchange, 100.0F);
    auto equalBudget = exchange;
    equalBudget.Settings.SplitThreshold = -1.0F;
    SetSplitScores(equalBudget, TopPersistentMergeQueueScore(equalBudget));
    auto equalIteration = BeginStrictSplitIteration(equalBudget);
    const auto equal = AdvanceStrictSplitIteration(equalBudget, equalIteration);
    Require(equal.BudgetRejected && !equal.MergeAttempted && equalIteration.Stop == TopologySplitStop::BudgetBlocked,
        "equal split/merge scores cannot authorize budget exchange");
    iteration = BeginStrictSplitIteration(exchange);
    const auto exchanged = AdvanceStrictSplitIteration(exchange, iteration);
    Require(exchanged.SplitAttempted && !exchanged.SplitSucceeded && exchanged.BudgetRejected &&
        exchanged.MergeAttempted && exchanged.MergeSucceeded && exchange.Stats.QueueCrossoverCount == 1U &&
        exchange.RemainingSerialSplitBudget == 2U, "one iteration must retain failed split followed by budget merge");

    auto urgent = MakeFixture(flat);
    urgent.Settings.SplitThreshold = -1.0F;
    auto urgentSeed = BeginStrictSplitIteration(urgent);
    (void)AdvanceStrictSplitIteration(urgent, urgentSeed);
    ++urgent.BuildSequence;
    urgent.Settings.MergeThreshold = std::numeric_limits<float>::max();
    InitializePersistentMergeQueue(urgent);
    iteration = BeginStrictSplitIteration(urgent);
    const auto merged = AdvanceStrictSplitIteration(urgent, iteration);
    Require(merged.MergeAttempted && merged.MergeSucceeded && !merged.SplitAttempted &&
        urgent.ActiveLeafNodes.size() == 2U, "priority merge must run before any root split");

    auto staleMerge = MakeFixture(flat);
    staleMerge.Settings.MergeThreshold = 1.0F;
    staleMerge.MergeQueue.push_back({0.0F, staleMerge.RootA});
    auto& staleMembership = staleMerge.NodeMembership[staleMerge.RootA];
    staleMembership.MergeQueuePosition = 0U;
    staleMembership.MergeQueueRepresentative = staleMerge.RootA;
    iteration = BeginStrictSplitIteration(staleMerge);
    const auto failedMerge = AdvanceStrictSplitIteration(staleMerge, iteration);
    Require(failedMerge.MergeAttempted && !failedMerge.MergeSucceeded && !failedMerge.SplitAttempted &&
        staleMerge.MergeQueue.empty() && staleMerge.Stats.RejectedMergeCount == 1U,
        "failed priority merge removes only its stale candidate and consumes one iteration");

    auto blocked = MakeFixture(flat);
    blocked.Nodes.Depths[blocked.RootB] = static_cast<std::uint8_t>(blocked.Settings.MaxDepth);
    blocked.Settings.SplitThreshold = 1.0F;
    SetSplitScores(blocked, 100.0F);
    iteration = BeginStrictSplitIteration(blocked);
    const auto rejected = AdvanceStrictSplitIteration(blocked, iteration);
    Require(rejected.SplitAttempted && !rejected.SplitSucceeded && !rejected.BudgetRejected && rejected.SplitBlocked &&
        blocked.SplitQueueBlockedBuildIds[blocked.RootA] == blocked.BuildSequence, "non-budget failure must block the head for this build");

    auto limited = MakeFixture(flat);
    limited.Settings.TriangleBudget = 200U;
    limited.Settings.SplitThreshold = -1.0F;
    iteration = BeginStrictSplitIteration(limited);
    const auto limit = iteration.MaximumIterations;
    Require(limit == 1608U, "iteration limit must use the entry node count");
    (void)AdvanceStrictSplitIteration(limited, iteration);
    Require(limited.Nodes.size() > 2U && iteration.MaximumIterations == limit, "node creation cannot extend the iteration limit");
    // 直接设置边界计数，避免为了验证一次上限判断空跑上千个循环
    iteration.Iteration = limit;
    const StateSnapshot beforeLimit{limited};
    (void)AdvanceStrictSplitIteration(limited, iteration);
    Require(iteration.Stop == TopologySplitStop::IterationLimit && iteration.Iteration == limit + 1U &&
        beforeLimit == StateSnapshot{limited}, "iteration limit check must preserve original post-increment semantics");
    TopologySplitIteration uninitialized;
    bool rejectedUninitialized = false;
    try { (void)AdvanceStrictSplitIteration(limited, uninitialized); }
    catch (const std::invalid_argument&) { rejectedUninitialized = true; }
    Require(rejectedUninitialized, "step entry must reject an uninitialized iteration");
}

void WritePaths(std::ostream& out, const std::vector<std::uint64_t>& paths)
{
    for (std::size_t i = 0; i < paths.size(); ++i) out << (i ? ";" : "") << paths[i];
}

DataOrientedRoamState MakeBatchFixture(const Terrain::HeightMap& height)
{
    auto state = MakeFixture(height);
    state.Settings.MaxDepth = 6;
    state.Settings.TriangleBudget = 128U;
    state.Settings.SplitThreshold = -1.0F;
    state.Settings.MergeThreshold = -2.0F;
    auto iteration = BeginStrictSplitIteration(state);
    while (iteration.Stop == TopologySplitStop::Running) (void)AdvanceStrictSplitIteration(state, iteration);
    Require(state.ActiveLeafNodes.size() == 128U, "uniform fixture did not reach its frozen level");
    ++state.BuildSequence;
    state.Settings.MaxDepth = 8;
    state.Settings.TriangleBudget = 256U;
    state.Settings.SplitThreshold = 1.0F;
    state.Settings.MergeThreshold = 0.0F;
    InitializePersistentMergeQueue(state);
    SetSplitScores(state, 100.0F);
    PrepareExecution(state);
    return state;
}

bool SameStep(const TopologySplitStep& a, const TopologySplitStep& b)
{
    return a.SplitPath == b.SplitPath && a.MergePath == b.MergePath &&
        a.SplitAttempted == b.SplitAttempted && a.SplitSucceeded == b.SplitSucceeded &&
        a.BudgetRejected == b.BudgetRejected && a.SplitBlocked == b.SplitBlocked &&
        a.MergeAttempted == b.MergeAttempted && a.MergeSucceeded == b.MergeSucceeded;
}

void CheckBatches()
{
    const Terrain::HeightMap flat;
    auto state = MakeBatchFixture(flat);
    auto position = BeginStrictSplitIteration(state);
    const StateSnapshot source{state};
    const auto decision = InspectSplitIteration(state, position);
    Require(!decision.Merge && decision.Stop == TopologySplitStop::Running && source == StateSnapshot{state},
        "control preview must be read-only");
    auto plan = BuildBatch(state, {OrderingMode::PriorityBand, 0.25}, position);
    Require(source == StateSnapshot{state}, "batch trials modified their shared source");
    Require(plan.Members.size() >= 3U && plan.Members.front().Operation.RootPath == decision.Path,
        "uniform terrain must expose at least three head-preserving independent roots");
    for (const auto& member : plan.Members)
        Require(member.Operation.InputHash == plan.InputHash && member.Operation.BudgetBefore == state.RemainingSerialSplitBudget,
            "candidate trial used a predecessor result instead of batch-start state");
    Require(plan.Inspected.size() <= 64U && plan.Members.size() <= 32U, "fixed inspection or capacity limit exceeded");

    auto boundary = state;
    // 100 的带宽 0.25 精确包含 75；紧邻其下的 float 不得因舍入被接收
    SetSplitScores(boundary, 75.0F);
    boundary.SplitQueue.front().Score = 100.0F;
    boundary.SplitQueue.back().Score = std::nextafter(75.0F, 0.0F);
    auto boundaryPosition = BeginStrictSplitIteration(boundary);
    const auto boundaryPlan = BuildBatch(boundary, {OrderingMode::PriorityBand, 0.25}, boundaryPosition);
    Require(boundaryPlan.InBandCount + 1U == boundaryPlan.CandidateCount && boundaryPlan.MinimumScore == 75.0,
        "priority band changed its inclusive floating-point boundary");

    // 截取三个已经独立的成员，固定六个排列的解析审计规模
    plan.Members.resize(3U);
    plan.PeakReservation = 0U;
    plan.NewNodes = 0U;
    for (const auto& member : plan.Members)
    {
        plan.PeakReservation += member.Operation.PeakBudgetUse;
        plan.NewNodes += member.Operation.Work.NodesCreated;
    }
    const auto audit = AuditBatch(state, plan);
    Require(audit.Performed && audit.Passed && audit.Applications == 6U,
        "independent permutations failed: " + audit.Difference);
    auto applied = state;
    auto appliedPosition = position;
    const auto batch = ApplyBatch(applied, plan, appliedPosition);
    Require(batch.Status == ApplicationStatus::ValidatedBatch && batch.Roots.size() == 3U &&
        appliedPosition.Iteration == position.Iteration + 3U, "closed plan did not apply exactly its three roots");
    for (std::size_t i = 0U; i < batch.Roots.size(); ++i)
        Require(batch.Roots[i].RootPath == plan.Members[i].Operation.RootPath, "newly created candidate entered current batch");

    // 故意把同一根复制为两个成员，模拟依赖漏边；成功执行一种前缀不能使审计通过
    auto missingEdge = plan;
    missingEdge.Members[1] = missingEdge.Members[0];
    const auto rejectedAudit = AuditBatch(state, missingEdge);
    Require(rejectedAudit.Performed && !rejectedAudit.Passed, "dependency auditor accepted overlapping roots");
    auto changed = state;
    --changed.RemainingSerialSplitBudget;
    auto changedPosition = position;
    Require(ApplyBatch(changed, plan, changedPosition).Status == ApplicationStatus::PlanMismatch,
        "batch accepted a different start state");

    auto limitPosition = position;
    limitPosition.MaximumIterations = limitPosition.Iteration + 1U;
    auto interruptedPlan = BuildBatch(state, {OrderingMode::PriorityBand, 0.25}, limitPosition);
    auto interruptedState = state;
    const auto interrupted = ApplyBatch(interruptedState, interruptedPlan, limitPosition);
    Require(interrupted.Status == ApplicationStatus::ControlInterrupted && interrupted.Roots.size() == 1U,
        "iteration boundary must cancel the rest without validating an adaptive prefix");

    auto urgent = state;
    urgent.Settings.MergeThreshold = 1.0F;
    auto urgentPosition = BeginStrictSplitIteration(urgent);
    const auto urgentPlan = BuildBatch(urgent, {OrderingMode::PriorityBand, 0.25}, urgentPosition);
    Require(urgentPlan.StrictFallback && InspectSplitIteration(urgent, urgentPosition).Merge,
        "an eligible old merge must prevent batch construction");
    const auto urgentApplication = ApplyBatch(urgent, urgentPlan, urgentPosition);
    Require(urgentApplication.Status == ApplicationStatus::StrictStep && urgentApplication.Roots.empty() &&
        urgentApplication.MergeAttempts == 1U, "planned entry must preserve a priority merge");

    auto newHead = state;
    newHead.DrawableWidth = 1280U;
    newHead.DrawableHeight = 720U;
    newHead.Settings.SplitThreshold = 0.000001F;
    SetSplitScores(newHead, 0.00001F);
    auto newHeadPosition = BeginStrictSplitIteration(newHead);
    const auto newHeadPlan = BuildBatch(newHead, {OrderingMode::PriorityBand, 0.25}, newHeadPosition);
    auto strictNewHead = newHead;
    auto strictNewHeadPosition = newHeadPosition;
    auto strictNewHeadPlan = newHeadPlan;
    // 给应用器同一独立集合但要求严格顺序，单独验证新队首的中断边界
    strictNewHeadPlan.Configuration = {OrderingMode::StrictPrefix, 0.0};
    const auto strictNewHeadApplication = ApplyBatch(strictNewHead, strictNewHeadPlan, strictNewHeadPosition);
    Require(strictNewHeadApplication.Status == ApplicationStatus::ControlInterrupted && strictNewHeadApplication.Roots.size() == 1U,
        "strict application must stop when a newly generated split becomes the head");
    const auto newHeadApplication = ApplyBatch(newHead, newHeadPlan, newHeadPosition);
    Require(newHeadApplication.Status == ApplicationStatus::ValidatedBatch &&
        newHeadApplication.Roots.size() == newHeadPlan.Members.size() &&
        InspectSplitIteration(newHead, newHeadPosition).Score > newHeadPlan.MaximumScore,
        "higher-priority children must wait until the sealed batch is finished");

    const auto expiredPlan = BuildBatch(state, {OrderingMode::PriorityBand, 0.25}, position, ExperimentDeadline::min());
    auto expiredState = state;
    auto expiredPosition = position;
    const auto expired = ApplyBatch(expiredState, expiredPlan, expiredPosition);
    Require(expired.Status == ApplicationStatus::ResourceLimited && !expired.Detail.empty() &&
        expired.Roots.empty() && expiredPosition.Iteration == position.Iteration && source == StateSnapshot{state},
        "resource stop must carry its cause and cannot consume a root");
    std::size_t accounted = 0U;
    for (const auto count : expiredPlan.Reasons) accounted += count;
    Require(accounted == expiredPlan.CandidateCount && expiredPlan.Inspected.empty(),
        "resource stop must account for uninspected candidate exposures");
    const auto expiredAudit = AuditBatch(state, plan, ExperimentDeadline::min());
    Require(expiredAudit.Performed && !expiredAudit.Passed && expiredAudit.ResourceLimited && expiredAudit.Applications == 0U,
        "audit timeout is incomplete evidence rather than a dependency counterexample");

    // 同一局部操作分别隔离读写和写写冲突，主原因不能依赖传入顺序
    TopologySplitObservation a, b;
    a.ReadPaths = {1U}; a.WrittenPaths = {2U};
    b.ReadPaths = {2U}; b.WrittenPaths = {3U};
    Require(DependencyConflict(a, b) == RejectionReason::ReadWriteDependency &&
        DependencyConflict(b, a) == RejectionReason::ReadWriteDependency, "read/write conflict missing");
    b.WrittenPaths = {2U};
    Require(DependencyConflict(a, b) == RejectionReason::WriteWriteOverlap, "write/write principal reason missing");
    a.Attempts = {{7U, true, true}}; b.Attempts = a.Attempts;
    Require(DependencyConflict(a, b) == RejectionReason::SharedForcedClosure, "shared closure must precede write conflict");

    auto tight = state;
    tight.Settings.TriangleBudget = tight.ActiveLeafNodes.size() + 2U;
    auto tightPosition = BeginStrictSplitIteration(tight);
    const auto tightPlan = BuildBatch(tight, {OrderingMode::PriorityBand, 1.0}, tightPosition);
    Require(tightPlan.Members.size() == 1U && tightPlan.PeakReservation == 2U &&
        tightPlan.Reasons[static_cast<std::size_t>(RejectionReason::BudgetReservation)] > 0U,
        "joint reservation did not reject individually affordable independent roots");

    auto prefix = BuildBatch(state, {OrderingMode::StrictPrefix, 0.0}, position);
    Require(prefix.Members.size() == 1U &&
        prefix.Reasons[static_cast<std::size_t>(RejectionReason::PrefixStopped)] > 0U,
        "strict prefix must stop at the first conflicting neighbor");
    for (double alpha : {-1.0, 0.0, 1.01, std::numeric_limits<double>::quiet_NaN()})
    {
        bool rejected = false;
        try { (void)BuildBatch(state, {OrderingMode::PriorityBand, alpha}, position); }
        catch (const std::invalid_argument&) { rejected = true; }
        Require(rejected, "invalid alpha accepted");
    }
    auto strict = state;
    auto strictPosition = BeginStrictSplitIteration(strict);
    std::vector<TopologySplitStep> strictSteps;
    while (strictPosition.Stop == TopologySplitStop::Running)
        strictSteps.push_back(AdvanceStrictSplitIteration(strict, strictPosition));
    const auto strictResult = CaptureResult(strict);
    for (auto mode : {OrderingMode::Strict, OrderingMode::StrictPrefix})
    {
        auto variant = state;
        const auto result = RunOrderingExperiment(variant, {mode, 0.0});
        Require(result.Position.Stop == strictPosition.Stop && result.Decisions.size() == strictSteps.size() &&
            std::equal(result.Decisions.begin(), result.Decisions.end(), strictSteps.begin(), SameStep),
            "zero relaxation or strict prefix changed a strict control decision");
        Require(CaptureResult(variant) == strictResult, "zero relaxation or strict prefix changed full successor");
    }
}

void WriteObservation(std::ostream& out, const NaturalInput& target, const TopologyConvergenceObservation& observation)
{
    for (std::size_t i = 0; i < observation.Roots.size(); ++i)
    {
        const auto& root = observation.Roots[i];
        out << target.Scenario << ',' << target.Sample << ',' << i << ',' << root.InputHash << ',' << root.RootPath
            << ',' << root.RootSucceeded << ',' << root.LocalFootprintComplete << ',' << root.RequiresSerialMaintenance
            << ',' << root.BudgetBefore << ',' << root.BudgetAfter << ',' << root.PeakBudgetUse << ',' << root.BudgetRejected
            << ',' << root.Work.PrimitiveAttempts << ',' << root.Work.PrimitiveCompleted << ',' << root.Work.ForcedCompleted
            << ',' << root.Work.NodesCreated << ',' << root.Work.NodesReused << ',' << root.Work.NeighborAssignments
            << ',' << root.Work.ActiveIndexCalls << ',' << root.Work.QueueCalls << ',' << root.Work.QueueMembershipUpdates
            << ',' << root.Work.MeshEditCalls << ',' << root.Work.PathInsertCalls << ',';
        WritePaths(out, root.ReadPaths); out << ',';
        WritePaths(out, root.WrittenPaths); out << ',';
        WritePaths(out, root.CreatedPaths); out << ',';
        WritePaths(out, root.CompletedPaths); out << ',';
        for (std::size_t j = 0; j < root.BudgetRemaining.size(); ++j)
            out << (j ? ";" : "") << root.BudgetRemaining[j];
        out << '\n';
    }
}

void RunNatural(const std::filesystem::path& scenariosPath, const std::filesystem::path& camerasPath,
    const std::filesystem::path& output, const std::filesystem::path& baseline = {}, bool observeRoots = true)
{
    Require(std::filesystem::create_directory(output), "natural output must be a new directory");
    std::ofstream results{output / "results.csv"};
    results.exceptions(std::ios::failbit | std::ios::badbit);
    results << "scenario,sample,input,result,successor,triangles,budget,splits,forced,merges,rejectedSplit,"
        "budgetRejected,rejectedMerge,crossovers,mesh\n";
    std::vector<std::string> baselineRows;
    std::ofstream observations;
    std::ofstream summary;
    if (!baseline.empty())
    {
        std::ifstream old{baseline};
        Require(old.good(), "missing pre-change result file");
        for (std::string row; std::getline(old, row);) baselineRows.push_back(row);
        Require(baselineRows.size() == NaturalInputs.size() + 1U, "pre-change result coverage");
        if (observeRoots)
        {
            observations.open(output / "roots.csv");
            observations.exceptions(std::ios::failbit | std::ios::badbit);
            observations << "scenario,sample,rootIndex,input,root,succeeded,localComplete,serialMaintenance,budgetBefore,budgetAfter,"
                "peakBudget,budgetRejected,attempts,completed,forced,created,reused,neighborAssignments,activeCalls,queueCalls,"
                "membershipUpdates,meshEdits,pathInserts,reads,writes,createdPaths,completedPaths,budgetTrace\n";
        }
        summary.open(output / "summary.csv");
        summary.exceptions(std::ios::failbit | std::ios::badbit);
        summary << "scenario,sample,totalRoots,recordedRoots,mergeAttempts,mergeFailures,primitiveMerges,coordinatorQueueCalls,observationMs,iterations,stop\n";
    }
    const auto scenarios = LoadScenarioManifest(scenariosPath, std::filesystem::current_path());
    const auto cameras = LoadCameraManifest(camerasPath, scenarios);
    std::size_t completed = 0U;
    for (const auto& scenario : scenarios)
    {
        std::size_t last = 0U;
        bool selected = false;
        for (const auto& target : NaturalInputs)
            if (target.Scenario == scenario.ScenarioId)
            {
                selected = true;
                last = std::max(last, target.Sample);
            }
        if (!selected) continue;
        Terrain::HeightMap height;
        std::string error;
        Require(height.LoadFromFile(scenario.HeightMapPath, &error), error);
        DataOrientedRoamPipeline pipeline;
        const auto settings = Benchmark::Formal::MakeCpuSourceSettings(scenario);
        for (const auto& camera : cameras)
        {
            if (camera.ScenarioId != scenario.ScenarioId || camera.SampleIndex > last) continue;
            (void)pipeline.BuildWithPassObserver(height, scenario.Settings.TerrainSize, scenario.Settings.HeightScale,
                BuildCameraView(camera), settings, [&](const auto& input, auto pass) {
                    if (pass != SplitPass) return;
                    for (const auto& target : NaturalInputs)
                    {
                        if (target.Scenario != scenario.ScenarioId || target.Sample != camera.SampleIndex) continue;
                        Require(HashDataOrientedRoamPassInput(input, pass) == target.Hash, "frozen natural input changed");
                        const StateSnapshot before{input};
                        DataOrientedRoamState state{input};
                        PrepareExecution(state);
                        ExecuteDataOrientedRoamPass(state, SplitPass);
                        const std::string captured = CaptureResult(state);
                        const std::string row = std::string{target.Scenario} + ',' + std::to_string(target.Sample) + ',' +
                            std::to_string(target.Hash) + ',' + captured;
                        results << row << '\n';
                        results.flush();
                        if (!baseline.empty())
                        {
                            Require(std::find(baselineRows.begin(), baselineRows.end(), row) != baselineRows.end(),
                                "new strict execution differs from the archived old result");
                            DataOrientedRoamState stepped{input};
                            PrepareExecution(stepped);
                            auto position = BeginStrictSplitIteration(stepped);
                            std::size_t roots = 0U;
                            while (position.Stop == TopologySplitStop::Running)
                                if (AdvanceStrictSplitIteration(stepped, position).SplitAttempted) ++roots;
                            Require(CaptureResult(stepped) == captured, "strict steps changed the old successor state");
                            Require(position.Stop == TopologySplitStop::BudgetBlocked && roots == target.StrictRootAttempts,
                                "strict natural root count or stopping reason changed");
                            DataOrientedRoamState observed{input};
                            PrepareExecution(observed);
                            const auto started = std::chrono::steady_clock::now();
                            const auto observation = ObserveStrictSplitConvergence(observed, observeRoots ? 8U : 0U);
                            const double elapsed = std::chrono::duration<double, std::milli>(
                                std::chrono::steady_clock::now() - started).count();
                            Require(CaptureResult(observed) == captured, "observation changed strict behavior");
                            Require(observation.RootAttempts == roots, "observed and stepped controller root counts differ");
                            if (observeRoots)
                            {
                                WriteObservation(observations, target, observation);
                                observations.flush();
                            }
                            summary << target.Scenario << ',' << target.Sample << ',' << observation.RootAttempts << ','
                                << observation.Roots.size() << ',' << observation.MergeAttempts << ',' << observation.MergeFailures << ','
                                << observation.PrimitiveMerges << ',' << observation.CoordinatorQueueCalls << ',' << elapsed << ','
                                << position.Iteration << ',' << static_cast<int>(position.Stop) << '\n';
                            summary.flush();
                        }
                        Require(before == StateSnapshot{input}, "natural execution modified the source");
                        ++completed;
                        std::cout << target.Scenario << '/' << target.Sample << " complete\n" << std::flush;
                    }
                });
        }
    }
    Require(completed == NaturalInputs.size(), "natural input coverage incomplete");
}

std::string PathText(const std::vector<std::uint64_t>& paths)
{
    std::ostringstream output;
    WritePaths(output, paths);
    return output.str();
}

void AppendWork(Experiment::ExperimentCsvRow& row, const TopologyOperationWork& work)
{
    for (const auto value : {work.PrimitiveAttempts, work.PrimitiveCompleted, work.ForcedCompleted,
        work.NodesCreated, work.NodesReused, work.NeighborAssignments, work.ActiveIndexCalls,
        work.QueueCalls, work.QueueMembershipUpdates, work.MeshEditCalls, work.PathInsertCalls})
        row.push_back(std::to_string(value));
}

void WriteWorkHeader(Experiment::ExperimentCsvRow& row, const std::string& prefix)
{
    for (const auto* name : {"primitiveAttempts", "primitiveCompleted", "forcedCompleted", "created", "reused",
        "neighborAssignments", "activeCalls", "queueCalls", "membershipUpdates", "meshEdits", "pathInserts"})
        row.push_back(prefix + name);
}

void RunRelaxedNatural(const std::filesystem::path& scenariosPath, const std::filesystem::path& camerasPath,
    const std::filesystem::path& output, const std::string& scenarioId, std::size_t sample)
{
    using Experiment::WriteExperimentCsvRow;
    using Experiment::FormatExperimentCsvDouble;
    using Experiment::ExperimentCsvRow;
    const auto target = std::find_if(NaturalInputs.begin(), NaturalInputs.end(), [&](const auto& input) {
        return input.Scenario == scenarioId && input.Sample == sample;
    });
    Require(target != NaturalInputs.end(), "relaxation mode accepts only frozen workload representatives");
    Require(std::filesystem::create_directory(output), "relaxation output must be a new directory");
    std::ofstream variants{output / "variants.csv"}, batches{output / "batches.csv"}, candidates{output / "candidates.csv"},
        reasons{output / "reasons.csv"}, roots{output / "applied-roots.csv"}, audits{output / "audits.csv"};
    for (auto* stream : {&variants, &batches, &candidates, &reasons, &roots, &audits})
        stream->exceptions(std::ios::failbit | std::ios::badbit);
    ExperimentCsvRow header{"configuration", "alpha", "status", "detail", "stop", "iterations", "sourceHash",
        "strictEquivalent", "fullResult", "triangles", "remainingBudget", "utilization", "batches", "validatedBatches",
        "multiBatches", "maximumWidth", "rootAttempts", "feasibleRoots", "feasiblePrimitives", "interruptedBatches",
        "primitiveMerges", "mergeAttempts", "mergeFailures", "coordinatorQueueCalls", "milliseconds"};
    WriteWorkHeader(header, "actual_"); WriteWorkHeader(header, "trial_");
    WriteExperimentCsvRow(variants, header);
    WriteExperimentCsvRow(batches, {"configuration", "batch", "inputHash", "iteration", "status", "detail", "plannedWidth",
        "validatedWidth", "appliedRoots", "candidateCount", "inBandCount", "inspectedCount", "lastAcceptedRank", "maxScore",
        "minScore", "peakReservation", "newNodes", "primitiveCompleted", "maxRootPrimitive", "constructionMs", "trialMs", "applyMs"});
    header = {"configuration", "batch", "path", "rank", "score", "reason", "conflictRoot", "rootSucceeded", "inputHash",
        "budgetBefore", "budgetAfter", "peakBudget", "reads", "writes", "createdPaths", "closureCompleted"};
    WriteWorkHeader(header, "trial_"); WriteExperimentCsvRow(candidates, header);
    WriteExperimentCsvRow(reasons, {"configuration", "batch", "reason", "count"});
    header = {"configuration", "batch", "root", "succeeded", "budgetBefore", "budgetAfter", "peakBudget", "reads", "writes", "closureCompleted"};
    WriteWorkHeader(header, "actual_"); WriteExperimentCsvRow(roots, header);
    WriteExperimentCsvRow(audits, {"configuration", "performed", "passed", "applications", "milliseconds", "difference"});

    const auto scenarios = LoadScenarioManifest(scenariosPath, std::filesystem::current_path());
    const auto cameras = LoadCameraManifest(camerasPath, scenarios);
    const auto scenario = std::find_if(scenarios.begin(), scenarios.end(), [&](const auto& value) { return value.ScenarioId == scenarioId; });
    Require(scenario != scenarios.end(), "frozen scenario missing");
    Terrain::HeightMap height;
    std::string error;
    Require(height.LoadFromFile(scenario->HeightMapPath, &error), error);
    DataOrientedRoamPipeline pipeline;
    const auto settings = Benchmark::Formal::MakeCpuSourceSettings(*scenario);
    bool completed = false;
    for (const auto& camera : cameras)
    {
        if (camera.ScenarioId != scenarioId || camera.SampleIndex > sample) continue;
        (void)pipeline.BuildWithPassObserver(height, scenario->Settings.TerrainSize, scenario->Settings.HeightScale,
            BuildCameraView(camera), settings, [&](const auto& input, auto pass) {
                if (pass != SplitPass || camera.SampleIndex != sample) return;
                Require(HashDataOrientedRoamPassInput(input, pass) == target->Hash, "frozen source hash changed");
                const StateSnapshot unchanged{input};
                DataOrientedRoamState strict{input};
                PrepareExecution(strict);
                auto strictPosition = BeginStrictSplitIteration(strict);
                std::vector<TopologySplitStep> strictSteps;
                while (strictPosition.Stop == TopologySplitStop::Running)
                    strictSteps.push_back(AdvanceStrictSplitIteration(strict, strictPosition));
                Require(static_cast<std::size_t>(std::count_if(strictSteps.begin(), strictSteps.end(),
                    [](const auto& step) { return step.SplitAttempted; })) == target->StrictRootAttempts,
                    "frozen strict root attempt count changed");
                const std::string strictResult = CaptureResult(strict);
                // 旧版完整结果独立留存，当前严格与新观察入口之间的互相一致不足以替代旧版本对照
                std::ofstream baseline{output / "strict.csv"};
                baseline << target->Scenario << ',' << target->Sample << ',' << target->Hash << ',' << strictResult << '\n';
                baseline.close();
                constexpr std::array configurations{
                    OrderingConfiguration{OrderingMode::Strict, 0.0}, OrderingConfiguration{OrderingMode::StrictPrefix, 0.0},
                    OrderingConfiguration{OrderingMode::PriorityBand, 0.1}, OrderingConfiguration{OrderingMode::PriorityBand, 0.25},
                    OrderingConfiguration{OrderingMode::PriorityBand, 1.0}};
                constexpr std::array names{"strict", "strict-prefix", "alpha-0.1", "alpha-0.25", "alpha-1"};
                bool audited = false;
                for (std::size_t variant = 0U; variant < configurations.size(); ++variant)
                {
                    const auto* name = names[variant];
                    std::cout << scenarioId << '/' << sample << ' ' << name << " begin\n" << std::flush;
                    DataOrientedRoamState state{input};
                    PrepareExecution(state);
                    const auto observer = [&](std::size_t index, const BatchPlan& plan, const BatchApplication& application) {
                        const auto batch = std::to_string(index);
                        std::size_t maximumRoot = 0U;
                        for (const auto& root : application.Roots)
                            maximumRoot = std::max(maximumRoot, root.Work.PrimitiveCompleted);
                        WriteExperimentCsvRow(batches, {name, batch, std::to_string(plan.InputHash), std::to_string(plan.Position.Iteration),
                            ApplicationName(application.Status), application.Detail, std::to_string(plan.Members.size()),
                            std::to_string(application.Status == ApplicationStatus::ValidatedBatch ? plan.Members.size() : 0U),
                            std::to_string(application.Roots.size()), std::to_string(plan.CandidateCount), std::to_string(plan.InBandCount),
                            std::to_string(plan.Inspected.size()), std::to_string(plan.LastAcceptedRank), FormatExperimentCsvDouble(plan.MaximumScore),
                            std::isfinite(plan.MinimumScore) ? FormatExperimentCsvDouble(plan.MinimumScore) : "unbounded",
                            std::to_string(plan.PeakReservation), std::to_string(plan.NewNodes), std::to_string(application.Work.PrimitiveCompleted),
                            std::to_string(maximumRoot), FormatExperimentCsvDouble(plan.ConstructionMilliseconds),
                            FormatExperimentCsvDouble(plan.TrialMilliseconds), FormatExperimentCsvDouble(application.Milliseconds)});
                        for (const auto& checked : plan.Inspected)
                        {
                            const auto& root = checked.Observation;
                            ExperimentCsvRow row{name, batch, std::to_string(checked.Path), std::to_string(checked.Rank),
                                FormatExperimentCsvDouble(checked.Score), RejectionName(checked.Reason), std::to_string(checked.ConflictPath),
                                std::to_string(root.RootSucceeded), std::to_string(root.InputHash), std::to_string(root.BudgetBefore),
                                std::to_string(root.BudgetAfter), std::to_string(root.PeakBudgetUse), PathText(root.ReadPaths), PathText(root.WrittenPaths),
                                PathText(root.CreatedPaths), PathText(root.CompletedPaths)};
                            AppendWork(row, root.Work); WriteExperimentCsvRow(candidates, row);
                        }
                        for (std::size_t reason = 0U; reason < plan.Reasons.size(); ++reason)
                            if (plan.Reasons[reason] != 0U)
                                WriteExperimentCsvRow(reasons, {name, batch, RejectionName(static_cast<RejectionReason>(reason)),
                                    std::to_string(plan.Reasons[reason])});
                        for (const auto& root : application.Roots)
                        {
                            ExperimentCsvRow row{name, batch, std::to_string(root.RootPath), std::to_string(root.RootSucceeded),
                                std::to_string(root.BudgetBefore), std::to_string(root.BudgetAfter), std::to_string(root.PeakBudgetUse),
                                PathText(root.ReadPaths), PathText(root.WrittenPaths), PathText(root.CompletedPaths)};
                            AppendWork(row, root.Work); WriteExperimentCsvRow(roots, row);
                        }
                        for (auto* stream : {&batches, &candidates, &reasons, &roots}) stream->flush();
                    };
                    const auto result = RunOrderingExperiment(state, configurations[variant], observer, !audited);
                    audited = audited || result.Audit.Performed;
                    const bool finished = result.Position.Stop != TopologySplitStop::Running &&
                        result.Status != ApplicationStatus::InvalidResult && result.Status != ApplicationStatus::PlanMismatch &&
                        result.Status != ApplicationStatus::ResourceLimited;
                    const auto triangles = state.ActiveLeafNodes.size();
                    const auto budget = state.RemainingSerialSplitBudget;
                    const std::string captured = finished ? CaptureResult(state) : "incomplete";
                    const bool equivalent = finished && captured == strictResult && result.Position.Stop == strictPosition.Stop &&
                        result.Position.Iteration == strictPosition.Iteration && result.Decisions.size() == strictSteps.size() &&
                        std::equal(result.Decisions.begin(), result.Decisions.end(), strictSteps.begin(), SameStep);
                    ExperimentCsvRow row{name, FormatExperimentCsvDouble(configurations[variant].Alpha), ApplicationName(result.Status),
                        result.Detail, std::to_string(static_cast<int>(result.Position.Stop)), std::to_string(result.Position.Iteration),
                        std::to_string(target->Hash), std::to_string(equivalent), captured, std::to_string(triangles), std::to_string(budget),
                        FormatExperimentCsvDouble(static_cast<double>(triangles) / static_cast<double>(state.Settings.TriangleBudget)),
                        std::to_string(result.Batches), std::to_string(result.ValidatedBatches), std::to_string(result.MultiBatches),
                        std::to_string(result.MaximumWidth), std::to_string(result.RootAttempts), std::to_string(result.FeasibleRoots),
                        std::to_string(result.FeasiblePrimitives), std::to_string(result.InterruptedBatches), std::to_string(result.PrimitiveMerges),
                        std::to_string(result.MergeAttempts), std::to_string(result.MergeFailures), std::to_string(result.CoordinatorQueueCalls),
                        FormatExperimentCsvDouble(result.Milliseconds)};
                    AppendWork(row, result.ActualWork); AppendWork(row, result.TrialWork); WriteExperimentCsvRow(variants, row);
                    variants.flush();
                    WriteExperimentCsvRow(audits, {name, std::to_string(result.Audit.Performed), std::to_string(result.Audit.Passed),
                        std::to_string(result.Audit.Applications), FormatExperimentCsvDouble(result.Audit.Milliseconds), result.Audit.Difference});
                    audits.flush();
                    Require(unchanged == StateSnapshot{input}, "relaxation run changed the borrowed source");
                    Require(finished, std::string{name} + " did not finish: " + result.Detail);
                    if (variant <= 1U) Require(equivalent, std::string{name} + " differs from strict execution");
                    std::cout << name << " complete; multi=" << result.MultiBatches << ", maxWidth=" << result.MaximumWidth << '\n' << std::flush;
                }
                completed = true;
            });
    }
    Require(completed, "requested frozen input not reached");
}
} // 匿名命名空间

int main(int argc, char** argv)
{
    try
    {
        if (argc == 7 && std::string{argv[1]} == "--relax-natural")
        {
            RunRelaxedNatural(argv[2], argv[3], argv[4], argv[5],
                static_cast<std::size_t>(Experiment::ParseExperimentCsvUnsigned(argv[6])));
            return 0;
        }
        if (argc == 5 && std::string{argv[1]} == "--baseline-natural")
        {
            RunNatural(argv[2], argv[3], argv[4]);
            return 0;
        }
        if (argc == 6 && std::string{argv[1]} == "--observe-natural")
        {
            RunNatural(argv[2], argv[3], argv[4], argv[5]);
            return 0;
        }
        if (argc == 6 && std::string{argv[1]} == "--verify-natural")
        {
            RunNatural(argv[2], argv[3], argv[4], argv[5], false);
            return 0;
        }
        Require(argc == 1, "expected --observe-natural scenarios cameras new-output baseline-results");
        CheckObservation();
        CheckStrictSteps();
        CheckBatches();
        std::cout << "operation observation fixtures complete\n";
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
