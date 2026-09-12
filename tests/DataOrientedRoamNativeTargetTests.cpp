#include "DataOrientedRoamExperimentTestSupport.h"
#include "algorithms/data_oriented_roam/DataOrientedRoamStateOps.h"
#include "algorithms/data_oriented_roam/DataOrientedRoamScoring.h"
#include "algorithms/data_oriented_roam/DataOrientedRoamVariance.h"
#include "algorithms/data_oriented_roam/DataOrientedRoamTopologyExperiment.h"
#include "algorithms/data_oriented_roam/DataOrientedRoamQueues.h"
#include "algorithms/data_oriented_roam/DataOrientedRoamValidation.h"
#include "algorithms/data_oriented_roam/DataOrientedRoamCandidateMarking.h"
#include "terrain/HeightMap.h"

#include <algorithm>
#include <cmath>
#include <iostream>
#include <map>
#include <optional>
#include <set>
#include <tuple>

using namespace ParallelRoam;
using namespace Tests;

namespace
{
using Index = DataOrientedRoamNodeIndex;

Index FindPath(const DataOrientedRoamState& state, std::uint64_t path)
{
    for (std::size_t i = 0; i < state.Nodes.size(); ++i)
        if (state.Nodes.PathIdAt(static_cast<Index>(i)) == path) return static_cast<Index>(i);
    return InvalidDataOrientedRoamNodeIndex;
}

// 人工分数只用于构造合法的不同历史，不将其称为同一严格控制器输入
void ApplyRoot(DataOrientedRoamState& state, std::uint64_t path)
{
    const auto node = FindPath(state, path);
    Require(state.IsLeaf(node), "fixture root is not a leaf");
    for (auto& entry : state.SplitQueue) entry.Score = -std::numeric_limits<float>::max();
    const auto position = state.NodeMembership[node].SplitQueuePosition;
    Require(position < state.SplitQueue.size(), "fixture root is inactive");
    state.SplitQueue[position].Score = std::numeric_limits<float>::max();
    // 人工评分后仍恢复合法堆与反向位置，避免无序尾部污染后续原生更新
    std::sort(state.SplitQueue.begin(), state.SplitQueue.end(), [&](const auto& a, const auto& b) {
        return SplitPriorityPrecedes(state, a.Node, a.Score, b.Node, b.Score);
    });
    for (std::size_t i = 0; i < state.SplitQueue.size(); ++i)
        state.NodeMembership[state.SplitQueue[i].Node].SplitQueuePosition = static_cast<DataOrientedRoamPosition>(i);
    auto iteration = BeginStrictSplitIteration(state);
    const auto step = AdvanceStrictSplitIteration(state, iteration);
    Require(step.SplitPath == path && step.SplitSucceeded, "fixture root did not complete");
}

std::set<std::uint64_t> Leaves(const DataOrientedRoamState& state)
{
    std::set<std::uint64_t> result;
    for (const auto node : state.ActiveLeafNodes) result.insert(state.Nodes.PathIdAt(node));
    return result;
}

std::uint64_t Path(const DataOrientedRoamState& state, Index node)
{
    return state.IsValidNode(node) ? state.Nodes.PathIdAt(node) : 0;
}

void RequireLegal(DataOrientedRoamState& state)
{
    state.Stats = {};
    ValidateTopology(state);
    Require(state.Stats.InvalidTopologyCount == 0 && state.Stats.InvalidNeighborCount == 0 &&
        state.Stats.TjunctionCount == 0, "history audit produced an invalid conforming mesh");
    Require(CountPersistentQueueInvariantViolations(state) == 0, "history audit queue invariant");
    Require(state.ActiveLeafNodes.size() <= state.Settings.TriangleBudget &&
        state.ActiveLeafNodes.size() + state.RemainingSerialSplitBudget == state.Settings.TriangleBudget,
        "history audit hard budget");
    for (const auto node : state.ActiveInternalNodes)
    {
        const auto& domain = state.Nodes.DomainAt(node);
        const bool boundary = (domain.A.x == domain.B.x && (domain.A.x == 0 || domain.A.x == 1)) ||
            (domain.A.y == domain.B.y && (domain.A.y == 0 || domain.A.y == 1));
        const auto mate = state.Nodes.BaseNeighborAt(node);
        if (boundary)
            Require(!state.IsValidNode(mate), "boundary internal has a base mate");
        else
        {
            // 完整共形状态中的内部底边是同层反射伙伴；不以原数组的值自证几何关系
            Require(state.IsValidNode(mate) && !state.IsLeaf(mate), "internal base lacks a refined mate");
            const auto& other = state.Nodes.DomainAt(mate);
            Require(domain.A == other.B && domain.B == other.A && domain.A + domain.B - domain.C == other.C &&
                state.Nodes.DepthAt(node) == state.Nodes.DepthAt(mate) && state.Nodes.BaseNeighborAt(mate) == node,
                "internal base does not match the fixed geometric diamond");
        }
    }
}

DataOrientedRoamState MakeUniformSeed(const Terrain::HeightMap& height)
{
    DataOrientedRoamState seed;
    seed.HeightMap = &height; seed.TerrainSize = 30; seed.HeightScale = 4;
    seed.Settings.MaxDepth = 6; seed.Settings.TriangleBudget = 4096;
    seed.Settings.SplitThreshold = 1.0e30F; seed.Settings.MergeThreshold = -1.0e30F;
    seed.Settings.EnableLocalConstraints = true;
    seed.Settings.PassPolicy = Algorithms::MakeTerrainLodSerialIncrementalPolicy();
    seed.BuildSequence = 1;
    const auto view = ExperimentView(0);
    seed.ViewProjection = view.ViewProjection; seed.FrustumPlanes = view.FrustumPlanes;
    seed.DrawableWidth = 1280; seed.DrawableHeight = 720;
    RebuildVarianceTrees(seed, 6);
    ResetTopology(seed);
    for (;;)
    {
        const auto found = std::find_if(seed.ActiveLeafNodes.begin(), seed.ActiveLeafNodes.end(), [&](Index node) {
            return seed.Nodes.DepthAt(node) < 3;
        });
        if (found == seed.ActiveLeafNodes.end()) break;
        ApplyRoot(seed, seed.Nodes.PathIdAt(*found));
    }
    return seed;
}

/// <summary>
/// 按字段和查询结果分别计数，避免把读取范围差异当成续接行为差异
/// </summary>
struct RelationAuditCounts
{
    std::size_t Fields{0}, EqualTargets{0}, Neighborhoods{0};
    std::array<std::size_t, 3> ByField{};
};

std::set<std::uint64_t> NeighborhoodPaths(const DataOrientedRoamState& state, Index node)
{
    DataOrientedRoamNeighborhood neighborhood;
    AppendPersistentMergeQueueNeighborhood(state, node, neighborhood);
    std::set<std::uint64_t> paths;
    for (const auto neighbor : neighborhood) paths.insert(Path(state, neighbor));
    return paths;
}

void CompareRelations(const DataOrientedRoamState& ab, const DataOrientedRoamState& ba,
    std::uint64_t a, std::uint64_t b, RelationAuditCounts& counts)
{
    for (const auto node : ab.ActiveInternalNodes)
    {
        const auto path = ab.Nodes.PathIdAt(node);
        const auto other = FindPath(ba, path);
        Require(ba.IsValidNode(other), "equal target lost an internal node");
        // 物理创建顺序允许不同，所有邻接与查询结果先转换为稳定身份
        const std::array<Index, 3> first{ab.Nodes.BaseNeighborAt(node),
            ab.Nodes.LeftNeighborAt(node), ab.Nodes.RightNeighborAt(node)};
        const std::array<Index, 3> second{ba.Nodes.BaseNeighborAt(other),
            ba.Nodes.LeftNeighborAt(other), ba.Nodes.RightNeighborAt(other)};
        const auto firstPaths = NeighborhoodPaths(ab, node);
        const auto secondPaths = NeighborhoodPaths(ba, other);
        if (firstPaths != secondPaths)
        {
            if (counts.Neighborhoods == 0)
            {
                std::cout << "neighborhood a=" << a << " b=" << b << " internal=" << path << " ab-only=";
                for (const auto neighbor : firstPaths)
                    if (!secondPaths.contains(neighbor)) std::cout << neighbor << ';';
                std::cout << " ba-only=";
                for (const auto neighbor : secondPaths)
                    if (!firstPaths.contains(neighbor)) std::cout << neighbor << ';';
                std::cout << '\n';
            }
            ++counts.Neighborhoods;
        }
        for (std::size_t field = 0; field < 3; ++field)
        {
            if (Path(ab, first[field]) == Path(ba, second[field])) continue;
            if (counts.Fields < 3)
                std::cout << "history a=" << a << " b=" << b << " internal=" << path
                    << " field=" << field << " ab=" << Path(ab, first[field])
                    << " ba=" << Path(ba, second[field]) << '\n';
            ++counts.Fields;
            ++counts.ByField[field];
        }
    }
}

// 同一合法叶目标的历史内邻接可能不同，先找见证再判断该字段的续接用途
void AuditInternalRelations()
{
    Terrain::HeightMap height;
    std::string error;
    Require(height.LoadFromFile("assets/heightmaps/Hm_Terrain_Test_129.pgm", &error), error);
    const auto seed = MakeUniformSeed(height);
    const auto candidates = Leaves(seed);
    RelationAuditCounts counts;
    for (const auto a : candidates)
    {
        for (const auto b : candidates)
        {
            if (a >= b) continue;
            DataOrientedRoamState ab{seed}, ba{seed};
            ApplyRoot(ab, a);
            if (ab.IsLeaf(FindPath(ab, b))) ApplyRoot(ab, b);
            ApplyRoot(ba, b);
            if (ba.IsLeaf(FindPath(ba, a))) ApplyRoot(ba, a);
            RequireLegal(ab); RequireLegal(ba);
            if (Leaves(ab) != Leaves(ba)) continue;
            ++counts.EqualTargets;
            CompareRelations(ab, ba, a, b, counts);
        }
    }
    Require(counts.EqualTargets > 0, "no equal-target relation audit cases");
    Require(counts.Fields > 0, "history-dependent relation witness disappeared");
    std::cout << "equal-target pairs=" << counts.EqualTargets << " internal relation differences=" << counts.Fields
        << " base=" << counts.ByField[0] << " left=" << counts.ByField[1] << " right=" << counts.ByField[2]
        << " neighborhood differences=" << counts.Neighborhoods << '\n';
}

// 另一历史只标定见证字段；干预值可取自历史，也可从当前孩子的底边恢复
std::size_t ReplaceInternalSides(DataOrientedRoamState& target, const DataOrientedRoamState& donor,
    bool recoverFromChildren = false)
{
    const StateSnapshot original{target};
    const auto savedLeft = target.Nodes.LeftNeighbors, savedRight = target.Nodes.RightNeighbors;
    std::size_t changed = 0;
    for (const auto node : target.ActiveInternalNodes)
    {
        const auto source = FindPath(donor, Path(target, node));
        Require(donor.IsValidNode(source) && !donor.IsLeaf(source), "donor internal identity");
        const auto translate = [&](Index neighbor) {
            const auto path = Path(donor, neighbor);
            const auto translated = path == 0 ? InvalidDataOrientedRoamNodeIndex : FindPath(target, path);
            Require(path == 0 || target.IsValidNode(translated), "donor neighbor missing in target cache");
            return translated;
        };
        auto left = translate(donor.Nodes.LeftNeighborAt(source));
        auto right = translate(donor.Nodes.RightNeighborAt(source));
        if (recoverFromChildren)
        {
            // 只恢复已发现历史差异的父节点，不对任意深层内部关系作推广
            if (left == target.Nodes.LeftNeighborAt(node) && right == target.Nodes.RightNeighborAt(node)) continue;
            const auto leftChild = target.Nodes.LeftChildAt(node), rightChild = target.Nodes.RightChildAt(node);
            Require(target.IsLeaf(leftChild) && target.IsLeaf(rightChild) &&
                target.NodeMembership[leftChild].ActiveLeafPosition != InvalidDataOrientedRoamPosition &&
                target.NodeMembership[rightChild].ActiveLeafPosition != InvalidDataOrientedRoamPosition,
                "local recovery requires active leaf children");
            left = target.Nodes.BaseNeighborAt(leftChild);
            right = target.Nodes.BaseNeighborAt(rightChild);
        }
        changed += static_cast<std::size_t>(left != target.Nodes.LeftNeighborAt(node));
        changed += static_cast<std::size_t>(right != target.Nodes.RightNeighborAt(node));
        target.Nodes.LeftNeighbors[node] = left;
        target.Nodes.RightNeighbors[node] = right;
    }
    const auto changedLeft = target.Nodes.LeftNeighbors, changedRight = target.Nodes.RightNeighbors;
    // 原址还原后检查包括容量、数据地址在内的全部快照，排除附带变量
    std::copy(savedLeft.begin(), savedLeft.end(), target.Nodes.LeftNeighbors.begin());
    std::copy(savedRight.begin(), savedRight.end(), target.Nodes.RightNeighbors.begin());
    Require(original == StateSnapshot{target}, "side substitution changed unrelated storage");
    std::copy(changedLeft.begin(), changedLeft.end(), target.Nodes.LeftNeighbors.begin());
    std::copy(changedRight.begin(), changedRight.end(), target.Nodes.RightNeighbors.begin());
    Require(changed > 0, "empty isolation intervention");
    return changed;
}

template<class Entries>
auto QueueProjection(const DataOrientedRoamState& state, const Entries& entries, bool merge)
{
    std::map<std::uint64_t, std::pair<float, std::uint64_t>> result;
    for (const auto& entry : entries)
    {
        const auto partner = merge ? Path(state, state.NodeMembership[entry.Node].MergeQueuePartner) : 0;
        Require(result.emplace(Path(state, entry.Node), std::pair{entry.Score, partner}).second,
            "duplicate logical queue member");
    }
    return result;
}

void RequireContinuationProjection(const DataOrientedRoamState& expected, const DataOrientedRoamState& actual,
    bool allowDormantRelations = false)
{
    const auto& a = expected.Nodes;
    const auto& b = actual.Nodes;
    // 两边都是原生执行且从同一物理输入开始；这里额外检查缓存创建顺序未受干预影响
    Require(a.PathIds == b.PathIds && a.Parents == b.Parents && a.LeftChildren == b.LeftChildren &&
        a.RightChildren == b.RightChildren &&
        a.IsSplits == b.IsSplits && a.Depths == b.Depths && a.VarianceIndices == b.VarianceIndices &&
        a.VarianceTreeIndices == b.VarianceTreeIndices && a.InteriorChunkIds == b.InteriorChunkIds,
        "continuation hierarchy/base relation changed");
    Require(a.CreatedBuildIds == b.CreatedBuildIds && a.ActivatedBuildIds == b.ActivatedBuildIds &&
        a.SplitBuildIds == b.SplitBuildIds && a.MergeBuildIds == b.MergeBuildIds &&
        a.ActivatedByForcedSplits == b.ActivatedByForcedSplits &&
        a.GeometricErrors == b.GeometricErrors && a.ScreenErrors == b.ScreenErrors,
        "continuation history or score cache changed");
    for (Index node = 0; node < a.size(); ++node)
    {
        Require(a.DomainAt(node).A == b.DomainAt(node).A && a.DomainAt(node).B == b.DomainAt(node).B &&
            a.DomainAt(node).C == b.DomainAt(node).C, "continuation domain changed");
        const bool active = expected.NodeMembership[node].ActiveLeafPosition != InvalidDataOrientedRoamPosition ||
            expected.NodeMembership[node].ActiveInternalPosition != InvalidDataOrientedRoamPosition;
        if (active || !allowDormantRelations)
            Require(a.BaseNeighborAt(node) == b.BaseNeighborAt(node), "continuation base adjacency changed");
        // 休眠例外只由专用干预启用；节点重新激活后立即要求完整活动叶关系一致
        if (expected.IsLeaf(node) && (active || !allowDormantRelations))
            Require(a.LeftNeighborAt(node) == b.LeftNeighborAt(node) && a.RightNeighborAt(node) == b.RightNeighborAt(node),
                "continuation leaf adjacency changed");
        const auto& first = expected.NodeMembership[node];
        const auto& second = actual.NodeMembership[node];
        Require((first.ActiveInternalPosition != InvalidDataOrientedRoamPosition) ==
            (second.ActiveInternalPosition != InvalidDataOrientedRoamPosition) &&
            (first.ActiveLeafPosition != InvalidDataOrientedRoamPosition) ==
            (second.ActiveLeafPosition != InvalidDataOrientedRoamPosition) &&
            first.MergeQueueRepresentative == second.MergeQueueRepresentative,
            "continuation activity or merge representative changed");
    }
    Require(expected.SplitQueueBlockedBuildIds == actual.SplitQueueBlockedBuildIds &&
        expected.PreviousSplitPaths == actual.PreviousSplitPaths && expected.CurrentSplitPaths == actual.CurrentSplitPaths &&
        expected.RemainingSerialSplitBudget == actual.RemainingSerialSplitBudget && Leaves(expected) == Leaves(actual),
        "continuation budget/block/history changed");
    Require(QueueProjection(expected, expected.SplitQueue, false) == QueueProjection(actual, actual.SplitQueue, false),
        "continuation split queue changed");
    Require(QueueProjection(expected, expected.MergeQueue, true) == QueueProjection(actual, actual.MergeQueue, true),
        "continuation merge queue changed");
}

auto StepProjection(const TopologySplitStep& step)
{
    return std::tuple{step.SplitPath, step.MergePath, step.SplitAttempted, step.SplitSucceeded,
        step.BudgetRejected, step.SplitBlocked, step.MergeAttempted, step.MergeSucceeded};
}

std::optional<TopologySplitObservation> ObserveSelectedRoot(const DataOrientedRoamState& state)
{
    const auto merge = TopPersistentMergeQueueNode(state);
    if (state.IsValidNode(merge) && TopPersistentMergeQueueScore(state) < state.Settings.MergeThreshold) return {};
    const auto split = TopPersistentSplitQueueNode(state);
    if (!state.IsValidNode(split) || !ShouldSplitWithScore(state, split, TopPersistentSplitQueueScore(state))) return {};
    // 该观察会克隆并哈希微型输入；仅补充诊断，实际状态仍由严格步进独立推进
    return AnalyzeSplitOperation(state, split);
}

void RequireSameOperation(const TopologySplitObservation& a, const TopologySplitObservation& b)
{
    Require(a.RootPath == b.RootPath && a.RootSucceeded == b.RootSucceeded && a.CompletedPaths == b.CompletedPaths &&
        a.CreatedPaths == b.CreatedPaths && a.Attempts.size() == b.Attempts.size() &&
        a.BudgetBefore == b.BudgetBefore && a.BudgetAfter == b.BudgetAfter &&
        a.BudgetRemaining == b.BudgetRemaining && a.BudgetRejected == b.BudgetRejected,
        "isolated relation changed root operations or budget ledger");
    for (std::size_t i = 0; i < a.Attempts.size(); ++i)
        Require(std::tie(a.Attempts[i].Path, a.Attempts[i].Forced, a.Attempts[i].Completed) ==
            std::tie(b.Attempts[i].Path, b.Attempts[i].Forced, b.Attempts[i].Completed),
            "isolated relation changed forced split attempt");
}

/// <summary>
/// 以实际进入的分支说明诊断覆盖，维护次数差异不当作逻辑结果差异
/// </summary>
struct ContinuationCounts
{
    std::size_t Steps{0}, Splits{0}, ForcedAttempts{0}, Merges{0}, Exchanges{0}, BudgetFailures{0}, MergeFailures{0};
};

ContinuationCounts ContinueTogether(DataOrientedRoamState& original, DataOrientedRoamState& altered,
    bool allowDormantRelations = false)
{
    auto first = BeginStrictSplitIteration(original), second = BeginStrictSplitIteration(altered);
    ContinuationCounts counts;
    while (first.Stop == TopologySplitStop::Running)
    {
        Require(first.Iteration < 4096, "bounded isolation audit exhausted");
        RequireContinuationProjection(original, altered, allowDormantRelations);
        for (const auto& entry : original.MergeQueue)
        {
            // 严格细分循环以最大有限分数尝试合并；完整候选不能被当作失败移除夹具
            Require(std::isfinite(entry.Score) && CanMergeNode(original, entry.Node, std::numeric_limits<float>::max()),
                "valid merge queue contains a root rejected by strict convergence");
        }
        const auto a = ObserveSelectedRoot(original), b = ObserveSelectedRoot(altered);
        Require(a.has_value() == b.has_value(), "isolated relation changed selected root kind");
        if (a)
        {
            RequireSameOperation(*a, *b);
            for (const auto& attempt : a->Attempts) counts.ForcedAttempts += static_cast<std::size_t>(attempt.Forced);
        }
        const auto firstStep = AdvanceStrictSplitIteration(original, first);
        const auto secondStep = AdvanceStrictSplitIteration(altered, second);
        Require(StepProjection(firstStep) == StepProjection(secondStep) && first.Stop == second.Stop &&
            first.Iteration == second.Iteration && first.MaximumIterations == second.MaximumIterations,
            "isolated relation changed strict control");
        if (a) Require(firstStep.SplitAttempted && firstStep.SplitSucceeded == a->RootSucceeded,
            "diagnostic operation disagrees with actual strict step");
        ++counts.Steps;
        counts.Splits += static_cast<std::size_t>(firstStep.SplitAttempted);
        counts.Merges += static_cast<std::size_t>(firstStep.MergeSucceeded);
        counts.Exchanges += static_cast<std::size_t>(firstStep.BudgetRejected && firstStep.MergeSucceeded);
        counts.BudgetFailures += static_cast<std::size_t>(firstStep.BudgetRejected);
        counts.MergeFailures += static_cast<std::size_t>(firstStep.MergeAttempted && !firstStep.MergeSucceeded);
        RequireContinuationProjection(original, altered, allowDormantRelations);
        RequireLegal(original); RequireLegal(altered);
    }
    std::cout << " stop=" << static_cast<int>(first.Stop) << " steps=" << counts.Steps << " splits=" << counts.Splits
        << " forced=" << counts.ForcedAttempts << " merges=" << counts.Merges << " exchanges=" << counts.Exchanges
        << " budgetFailures=" << counts.BudgetFailures << " mergeFailures=" << counts.MergeFailures << '\n';
    return counts;
}

void ConfigureContinuation(DataOrientedRoamState& state, int mode)
{
    if (mode != 4) ++state.BuildSequence;
    CollectActiveSplitPaths(state);
    state.PreviousSplitPaths = state.CurrentSplitPaths;
    state.Settings.SplitThreshold = mode == 1 ? 1.0e30F : 0.25F;
    state.Settings.MergeThreshold = mode == 1 ? 1.0e30F : 0.1F;
    if (mode >= 2) state.Settings.TriangleBudget = state.ActiveLeafNodes.size() + (mode == 3 ? 1 : 0);
    RefreshPersistentMergeQueuePriorities(state);
    RefreshPersistentSplitQueuePriorities(state);
    if (mode >= 2)
    {
        // 固定高分头部用于触发预算分支，两边都采用相同合法堆输入
        const auto node = FindPath(state, 16);
        Require(state.IsLeaf(node), "budget fixture lost selected leaf");
        state.SplitQueue[state.NodeMembership[node].SplitQueuePosition].Score = std::numeric_limits<float>::max();
        std::sort(state.SplitQueue.begin(), state.SplitQueue.end(), [&](const auto& a, const auto& b) {
            return SplitPriorityPrecedes(state, a.Node, a.Score, b.Node, b.Score);
        });
        for (std::size_t i = 0; i < state.SplitQueue.size(); ++i)
            state.NodeMembership[state.SplitQueue[i].Node].SplitQueuePosition = static_cast<DataOrientedRoamPosition>(i);
    }
    state.Stats = {};
}

void AuditNeighborhoodRefresh(const DataOrientedRoamState& witness)
{
    DataOrientedRoamState baseline{witness};
    ConfigureContinuation(baseline, 0);
    std::size_t insertions = 0;
    for (const auto path : {8ULL, 9ULL})
    {
        DataOrientedRoamState refreshed{baseline};
        DataOrientedRoamNeighborhood neighborhood;
        AppendPersistentMergeQueueNeighborhood(refreshed, FindPath(refreshed, path), neighborhood);
        InvalidatePersistentMergeQueueNeighborhood(refreshed, neighborhood);
        RefreshPersistentMergeQueueNeighborhood(refreshed, neighborhood);
        insertions += refreshed.Stats.MergeQueueMembershipUpdateCount;
        RequireContinuationProjection(baseline, refreshed);
        RequireLegal(refreshed);
    }
    Require(insertions > 0, "refresh audit never reinserted an eligible candidate");

    // 人工移除仍合法的候选会破坏完整性，后续刷新将其补回；这不是自然失败合并的证据
    DataOrientedRoamState incomplete{baseline};
    const auto node = TopPersistentMergeQueueNode(incomplete);
    Require(incomplete.IsValidNode(node) && CanMergeNode(incomplete, node, std::numeric_limits<float>::max()),
        "missing-candidate fixture requires a valid eligible root");
    RemovePersistentMergeQueueCandidate(incomplete, node);
    Require(CountPersistentQueueInvariantViolations(incomplete) > 0, "missing eligible candidate went undetected");
    DataOrientedRoamNeighborhood missing;
    missing.append_unique(node);
    RefreshPersistentMergeQueueNeighborhood(incomplete, missing);
    RequireContinuationProjection(baseline, incomplete);
    RequireLegal(incomplete);
    std::cout << "refresh roundtrips=2 insertions=" << insertions << " synthetic-missing-restored=1\n";
}

void AuditIsolatedContinuation()
{
    Terrain::HeightMap height;
    std::string error;
    Require(height.LoadFromFile("assets/heightmaps/Hm_Terrain_Test_129.pgm", &error), error);
    const auto seed = MakeUniformSeed(height);
    DataOrientedRoamState ab{seed}, ba{seed};
    ApplyRoot(ab, 8); ApplyRoot(ab, 9);
    ApplyRoot(ba, 9); ApplyRoot(ba, 8);
    AuditNeighborhoodRefresh(ab);
    const std::array<const char*, 5> names{"refine", "coarsen", "exchange", "one-slot", "same-build-block"};
    for (const bool recover : {false, true})
    {
        for (int mode = 0; mode < static_cast<int>(names.size()); ++mode)
        {
            DataOrientedRoamState original{ab};
            ConfigureContinuation(original, mode);
            DataOrientedRoamState altered{original};
            const auto changed = ReplaceInternalSides(altered, ba, recover);
            RequireLegal(original); RequireLegal(altered);
            RequireContinuationProjection(original, altered);
            std::cout << (recover ? "recovery " : "isolation ") << names[static_cast<std::size_t>(mode)]
                << " fields=" << changed;
            const auto counts = ContinueTogether(original, altered);
            if (mode == 0) Require(counts.Splits > 0 && counts.ForcedAttempts > 0, "no refinement coverage");
            if (mode == 1) Require(counts.Merges > 0, "no merge coverage");
            if (mode == 2) Require(counts.Exchanges > 0, "no budget exchange coverage");
            if (mode >= 3) Require(counts.BudgetFailures > 0, "no budget rejection coverage");
        }
    }
}

void NormalizeNonLeafRelations(DataOrientedRoamState& state)
{
    const StateSnapshot before{state};
    const auto savedBase = state.Nodes.BaseNeighbors, savedLeft = state.Nodes.LeftNeighbors,
        savedRight = state.Nodes.RightNeighbors;
    std::size_t deepInternals = 0, dormantNodes = 0, changed = 0;
    for (Index node = 0; node < state.Nodes.size(); ++node)
    {
        if (state.NodeMembership[node].ActiveLeafPosition != InvalidDataOrientedRoamPosition) continue;
        if (state.NodeMembership[node].ActiveInternalPosition != InvalidDataOrientedRoamPosition)
        {
            const auto left = state.Nodes.LeftChildAt(node), right = state.Nodes.RightChildAt(node);
            Require(state.IsValidNode(left) && state.IsValidNode(right), "internal recovery requires cached children");
            deepInternals += static_cast<std::size_t>(!state.IsLeaf(left) || !state.IsLeaf(right));
            state.Nodes.LeftNeighbors[node] = state.Nodes.BaseNeighborAt(left);
            state.Nodes.RightNeighbors[node] = state.Nodes.BaseNeighborAt(right);
        }
        else
        {
            ++dormantNodes;
            state.Nodes.BaseNeighbors[node] = InvalidDataOrientedRoamNodeIndex;
            state.Nodes.LeftNeighbors[node] = InvalidDataOrientedRoamNodeIndex;
            state.Nodes.RightNeighbors[node] = InvalidDataOrientedRoamNodeIndex;
        }
        changed += static_cast<std::size_t>(savedBase[node] != state.Nodes.BaseNeighborAt(node)) +
            static_cast<std::size_t>(savedLeft[node] != state.Nodes.LeftNeighborAt(node)) +
            static_cast<std::size_t>(savedRight[node] != state.Nodes.RightNeighborAt(node));
    }
    const auto changedBase = state.Nodes.BaseNeighbors, changedLeft = state.Nodes.LeftNeighbors,
        changedRight = state.Nodes.RightNeighbors;
    std::copy(savedBase.begin(), savedBase.end(), state.Nodes.BaseNeighbors.begin());
    std::copy(savedLeft.begin(), savedLeft.end(), state.Nodes.LeftNeighbors.begin());
    std::copy(savedRight.begin(), savedRight.end(), state.Nodes.RightNeighbors.begin());
    Require(before == StateSnapshot{state}, "non-leaf normalization changed unrelated storage");
    std::copy(changedBase.begin(), changedBase.end(), state.Nodes.BaseNeighbors.begin());
    std::copy(changedLeft.begin(), changedLeft.end(), state.Nodes.LeftNeighbors.begin());
    std::copy(changedRight.begin(), changedRight.end(), state.Nodes.RightNeighbors.begin());
    Require(changed > 0 && (deepInternals > 0 || dormantNodes > 0), "non-leaf intervention lacks coverage");
    std::cout << "normalize deep=" << deepInternals << " dormant=" << dormantNodes << " fields=" << changed;
}

void AuditDeepAndDormantRelations()
{
    Terrain::HeightMap height;
    std::string error;
    Require(height.LoadFromFile("assets/heightmaps/Hm_Terrain_Test_129.pgm", &error), error);
    auto original = MakeUniformSeed(height);
    for (;;)
    {
        const auto found = std::find_if(original.ActiveLeafNodes.begin(), original.ActiveLeafNodes.end(), [&](Index node) {
            return original.Nodes.DepthAt(node) < 5;
        });
        if (found == original.ActiveLeafNodes.end()) break;
        ApplyRoot(original, Path(original, *found));
    }
    ConfigureContinuation(original, 1);
    DataOrientedRoamState altered{original};
    NormalizeNonLeafRelations(altered);
    RequireLegal(original); RequireLegal(altered);
    const auto coarsen = ContinueTogether(original, altered, true);
    Require(coarsen.Merges > 0 && original.ActiveLeafNodes.size() == 2, "deep fixture did not coarsen to roots");
    const auto cachedBefore = original.Nodes.size();
    ConfigureContinuation(original, 0); ConfigureContinuation(altered, 0);
    NormalizeNonLeafRelations(altered);
    RequireLegal(original); RequireLegal(altered);
    const auto refine = ContinueTogether(original, altered, true);
    Require(refine.Splits > 0 && original.ActiveLeafNodes.size() > 2, "dormant fixture did not reactivate cache");
    std::size_t reactivated = 0;
    for (Index node = 0; node < cachedBefore; ++node)
        if (original.Nodes.DepthAt(node) > 0 &&
            original.NodeMembership[node].ActiveLeafPosition != InvalidDataOrientedRoamPosition) ++reactivated;
    // 最终旧叶可能已变为内部，激活轮次同时覆盖这些确实被复用的旧缓存
    for (Index node = 0; node < cachedBefore; ++node)
        if (original.Nodes.DepthAt(node) > 0 && original.Nodes.ActivatedBuildIdAt(node) == original.BuildSequence &&
            original.NodeMembership[node].ActiveInternalPosition != InvalidDataOrientedRoamPosition) ++reactivated;
    Require(reactivated > 0, "no old cached identity was reused");
    std::cout << "dormant reactivated=" << reactivated << " old-cache=" << cachedBefore << '\n';
}
}

int main()
{
    try
    {
        AuditInternalRelations();
        AuditIsolatedContinuation();
        AuditDeepAndDormantRelations();
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
