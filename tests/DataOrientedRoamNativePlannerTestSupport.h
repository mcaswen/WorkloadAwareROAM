#pragma once

#include "DataOrientedRoamExperimentTestSupport.h"
#include "algorithms/data_oriented_roam/materialization/NativeTargetPlanner.h"
#include "algorithms/data_oriented_roam/DataOrientedRoamCandidateMarking.h"
#include "algorithms/data_oriented_roam/DataOrientedRoamTopology.h"
#include "algorithms/data_oriented_roam/DataOrientedRoamTopologyExperiment.h"
#include "algorithms/data_oriented_roam/DataOrientedRoamQueues.h"

#include <algorithm>
#include <array>
#include <bit>
#include <map>
#include <set>
#include <sstream>

namespace NativePlannerTests
{
using namespace ParallelRoam;
using namespace Tests;
namespace Native = Algorithms::DataOrientedRoam::Materialization;
namespace Dod = Algorithms::DataOrientedRoam;
using Field = Native::NativePlanningField;
using Activity = Native::NativePlanningActivity;

/// <summary>
/// 外部完整逻辑投影以稳定身份比较缓存和队列，不复用规划器宣称的变动集合
/// 数组仅压缩固定字段格式，投影构造及排序都属于诊断费用
/// </summary>
struct LogicalNode
{
    std::uint64_t Path{0}, Parent{0}, LeftChild{0}, RightChild{0};
    std::array<std::uint64_t, 3> Neighbors{};
    // 创建、激活、细分、合并、阻塞轮次，以及当前路径成员
    std::array<std::uint64_t, 6> History{};
    bool IsSplit{false}, Forced{false};
    Activity Active{Activity::Dormant};
    int Depth{0};
    std::size_t VarianceTree{0}, VarianceIndex{0};
    std::array<std::uint32_t, 8> Geometry{};
    bool operator==(const LogicalNode&) const = default;
};

/// <summary>
/// 正向成员包含代表的伙伴，物理堆次序不参与逻辑等价
/// </summary>
struct LogicalEntry
{
    std::uint64_t Path{0}, Partner{0};
    float Score{0};
    bool operator==(const LogicalEntry&) const = default;
};

/// <summary>
/// 完整参考状态只用于外部验证，不作为正常规划输出或物化输入
/// </summary>
struct Projection
{
    std::vector<LogicalNode> Nodes;
    std::vector<LogicalEntry> Split, Merge;
    std::size_t Budget{0};
    bool operator==(const Projection&) const = default;
};

inline std::array<std::uint32_t, 8> GeometryBits(const Dod::TriangleDomain& domain, float error, float screen)
{
    return {std::bit_cast<std::uint32_t>(domain.A.x), std::bit_cast<std::uint32_t>(domain.A.y),
        std::bit_cast<std::uint32_t>(domain.B.x), std::bit_cast<std::uint32_t>(domain.B.y),
        std::bit_cast<std::uint32_t>(domain.C.x), std::bit_cast<std::uint32_t>(domain.C.y),
        std::bit_cast<std::uint32_t>(error), std::bit_cast<std::uint32_t>(screen)};
}

inline void Sort(Projection& result)
{
    const auto order = [](const auto& a, const auto& b) { return a.Path < b.Path; };
    std::sort(result.Nodes.begin(), result.Nodes.end(), order);
    std::sort(result.Split.begin(), result.Split.end(), order);
    std::sort(result.Merge.begin(), result.Merge.end(), order);
}

inline Projection Project(const Dod::DataOrientedRoamState& state)
{
    Projection result;
    const auto path = [&](auto node) { return state.IsValidNode(node) ? state.Nodes.PathIdAt(node) : 0ULL; };
    for (Dod::DataOrientedRoamNodeIndex node = 0; node < state.Nodes.size(); ++node)
    {
        const auto& pool = state.Nodes;
        const auto& member = state.NodeMembership[node];
        const auto activity = member.ActiveInternalPosition != Dod::InvalidDataOrientedRoamPosition ? Activity::Internal :
            member.ActiveLeafPosition != Dod::InvalidDataOrientedRoamPosition ? Activity::Leaf : Activity::Dormant;
        result.Nodes.push_back({path(node), path(pool.ParentAt(node)), path(pool.LeftChildAt(node)), path(pool.RightChildAt(node)),
            {path(pool.BaseNeighborAt(node)), path(pool.LeftNeighborAt(node)), path(pool.RightNeighborAt(node))},
            {pool.CreatedBuildIds[node], pool.ActivatedBuildIdAt(node), pool.SplitBuildIdAt(node), pool.MergeBuildIdAt(node),
                state.SplitQueueBlockedBuildIds[node], state.CurrentSplitPaths.contains(path(node)) ? 1ULL : 0ULL},
            pool.IsSplitAt(node), pool.ActivatedByForcedSplitAt(node), activity, pool.DepthAt(node),
            pool.VarianceTreeIndexAt(node), pool.VarianceIndexAt(node),
            GeometryBits(pool.DomainAt(node), pool.GeometricErrorAt(node), pool.ScreenErrorAt(node))});
    }
    for (const auto& entry : state.SplitQueue) result.Split.push_back({path(entry.Node), 0, entry.Score});
    for (const auto& entry : state.MergeQueue)
        result.Merge.push_back({path(entry.Node), path(state.NodeMembership[entry.Node].MergeQueuePartner), entry.Score});
    result.Budget = state.RemainingSerialSplitBudget;
    Sort(result);
    return result;
}

inline Projection Project(const Native::NativePlanningView& view, const Native::NativePlanningQueues& queues)
{
    Projection result;
    const auto path = [&](auto node) { return view.IsValidNode(node) ? view.Path(node) : 0ULL; };
    for (Dod::DataOrientedRoamNodeIndex node = 0; node < view.NodeCount(); ++node)
    {
        result.Nodes.push_back({path(node), path(view.Parent(node)), path(view.Relation(node, Field::LeftChild)),
            path(view.Relation(node, Field::RightChild)),
            {path(view.Relation(node, Field::BaseNeighbor)), path(view.Relation(node, Field::LeftNeighbor)),
                path(view.Relation(node, Field::RightNeighbor))},
            {view.CreatedBuild(node), view.Read(node, Field::ActivatedBuild), view.Read(node, Field::SplitBuild),
                view.Read(node, Field::MergeBuild), view.Read(node, Field::SplitBlockedBuild), view.Read(node, Field::CurrentSplitPath)},
            view.Read(node, Field::IsSplit) != 0, view.Read(node, Field::ForcedActivation) != 0, view.Activity(node), view.Depth(node),
            view.VarianceTree(node), view.VarianceIndex(node), GeometryBits(view.Domain(node), view.GeometricError(node),
                node < view.Source().Nodes.size() ? view.Source().Nodes.ScreenErrorAt(node) : 0)});
    }
    for (std::size_t index = 0; index < queues.Size(Native::NativePlanningQueueKind::Split); ++index)
    {
        const auto entry = queues.At(Native::NativePlanningQueueKind::Split, index);
        result.Split.push_back({path(entry.Node), 0, entry.Score});
    }
    for (std::size_t index = 0; index < queues.Size(Native::NativePlanningQueueKind::Merge); ++index)
    {
        const auto entry = queues.At(Native::NativePlanningQueueKind::Merge, index);
        result.Merge.push_back({path(entry.Node), path(queues.MergePartner(entry.Node)), entry.Score});
    }
    result.Budget = view.RemainingBudget();
    Sort(result);
    return result;
}

inline void Compare(const Projection& a, const Projection& b)
{
    if (a == b) return;
    std::ostringstream error;
    error << "logical projection mismatch: nodes " << a.Nodes.size() << '/' << b.Nodes.size()
        << " split " << a.Split.size() << '/' << b.Split.size() << " merge " << a.Merge.size() << '/' << b.Merge.size();
    for (std::size_t i = 0; i < std::min(a.Nodes.size(), b.Nodes.size()); ++i)
        if (a.Nodes[i] != b.Nodes[i]) { error << " first node=" << a.Nodes[i].Path << '/' << b.Nodes[i].Path; break; }
    throw std::runtime_error(error.str());
}

/// <summary>
/// 达到上限只停止存储并标记不完整，观察回调不干扰真实控制器的返回语义
/// </summary>
struct TraceLog
{
    std::vector<Dod::DecisionEvent> Events;
    bool Complete{true};
    static void Append(void* context, const Dod::DecisionEvent& event)
    {
        auto& self = *static_cast<TraceLog*>(context);
        if (self.Events.size() >= 1000000) { self.Complete = false; return; }
        self.Events.push_back(event);
    }
    Dod::DecisionTraceSink Sink() { return {this, &Append}; }
};

inline void Compare(const TraceLog& a, const TraceLog& b)
{
    Require(a.Complete && b.Complete, "decision trace capacity reached; audit incomplete");
    for (std::size_t i = 0; i < std::min(a.Events.size(), b.Events.size()); ++i)
        if (a.Events[i] != b.Events[i])
        {
            std::ostringstream error;
            error << "decision mismatch index=" << i << " kinds=" << static_cast<int>(a.Events[i].Kind) << '/'
                << static_cast<int>(b.Events[i].Kind) << " nodes=" << a.Events[i].Node << '/' << b.Events[i].Node;
            throw std::runtime_error(error.str());
        }
    Require(a.Events.size() == b.Events.size(), "decision trace length mismatch");
}

inline std::vector<std::uint64_t> Events(const Projection& state)
{
    std::vector<std::uint64_t> result;
    for (const auto& node : state.Nodes) if (node.Active == Activity::Internal) result.push_back(node.Path);
    return result;
}

inline void VerifyPlan(const Projection& before, const Projection& after, const TraceLog& legacy,
    const Dod::TopologySplitIteration& iteration, const Native::NativeTargetPlan& plan)
{
    const auto oldEvents = Events(before), newEvents = Events(after);
    std::vector<std::uint64_t> added, removed;
    std::set_difference(newEvents.begin(), newEvents.end(), oldEvents.begin(), oldEvents.end(), std::back_inserter(added));
    std::set_difference(oldEvents.begin(), oldEvents.end(), newEvents.begin(), newEvents.end(), std::back_inserter(removed));
    Require(added == plan.AddedEvents && removed == plan.RemovedEvents, "net event difference mismatch");
    Require(plan.Stop == iteration.Stop && plan.Iteration == iteration.Iteration &&
        plan.MaximumIterations == iteration.MaximumIterations && plan.FinalLeafCount == after.Split.size() &&
        plan.RemainingBudget == after.Budget && plan.FinalLeafCount + plan.RemainingBudget == plan.BudgetCap,
        "strict summary or hard budget mismatch");

    std::map<std::uint64_t, LogicalNode> oldNodes, newNodes;
    for (const auto& node : before.Nodes) oldNodes.emplace(node.Path, node);
    for (const auto& node : after.Nodes) newNodes.emplace(node.Path, node);
    std::vector<Native::NativeContinuationObligation> expected;
    using Obligation = Native::NativeObligationKind;
    // 独立遍历完整参考缓存，恢复默认值后找例外；不读取 Planner 的触及记录或义务键
    for (const auto& target : after.Nodes)
    {
        const auto found = oldNodes.find(target.Path);
        const bool born = found == oldNodes.end();
        LogicalNode baseline;
        if (!born) baseline = found->second;
        else { baseline.History[0] = plan.BuildSequence; baseline.History[1] = plan.BuildSequence; }
        const bool inAdded = std::binary_search(added.begin(), added.end(), target.Path);
        const bool inRemoved = std::binary_search(removed.begin(), removed.end(), target.Path);
        if (inAdded) baseline.History[2] = plan.BuildSequence;
        if (inRemoved) { baseline.History[3] = plan.BuildSequence; baseline.History[1] = plan.BuildSequence; baseline.Forced = false; }
        if ((born || found->second.Active == Activity::Dormant) && target.Active != Activity::Dormant)
        { baseline.History[1] = plan.BuildSequence; baseline.Forced = false; }
        constexpr std::array kinds{Obligation::ActivatedBuild, Obligation::SplitBuild, Obligation::MergeBuild, Obligation::SplitBlockedBuild};
        for (std::size_t field = 1; field <= 4; ++field)
            if (target.History[field] != baseline.History[field]) expected.push_back({kinds[field - 1], target.Path, target.History[field]});
        if (target.Forced != baseline.Forced) expected.push_back({Obligation::ForcedActivation, target.Path, target.Forced ? 1ULL : 0ULL});
        if (born && target.Active == Activity::Dormant) expected.push_back({Obligation::CacheBirth, target.Path, target.History[0]});
    }
    std::set<std::uint64_t> failed;
    for (const auto& event : legacy.Events) if (event.Kind == Dod::DecisionEventKind::RemoveMerge) failed.insert(event.Node);
    for (const auto path : failed)
    {
        const auto& node = newNodes.at(path);
        const auto leaf = [&](std::uint64_t p) { return p != 0 && newNodes.at(p).Active == Activity::Leaf; };
        bool eligible = node.Active == Activity::Internal && leaf(node.LeftChild) && leaf(node.RightChild);
        if (eligible && node.Neighbors[0] && !leaf(node.Neighbors[0]))
        {
            const auto& base = newNodes.at(node.Neighbors[0]);
            eligible = base.Active == Activity::Internal && base.Neighbors[0] == path && leaf(base.LeftChild) &&
                leaf(base.RightChild) && path < base.Path;
        }
        const bool present = std::any_of(after.Merge.begin(), after.Merge.end(), [&](const auto& entry) { return entry.Path == path; });
        if (eligible && !present) expected.push_back({Obligation::FailedMergeRemoval, path, 1});
    }
    std::sort(expected.begin(), expected.end());
    Require(expected == plan.Obligations, "continuation obligation mismatch");
    for (const auto& evaluation : plan.Evaluations)
        Require(newNodes.contains(evaluation.Path) && std::isfinite(evaluation.Score), "invalid pure evaluation cache");
    Native::NativePlanningWork work;
    bool exchange = false;
    for (const auto& event : legacy.Events)
    {
        using K = Dod::DecisionEventKind;
        if (event.Kind == K::SelectSplit) ++work.SplitRoots;
        if (event.Kind == K::SelectMerge) { ++work.MergeRoots; exchange = event.Reason == Dod::DecisionReason::BudgetExchange; }
        if (event.Kind == K::MergeRootEnd && event.Reason == Dod::DecisionReason::Success && exchange) ++work.Exchanges;
        if (event.Kind == K::SplitRootEnd && event.Reason == Dod::DecisionReason::Failure) ++work.SplitRootFailures;
        if (event.Kind == K::MergeRootEnd && event.Reason == Dod::DecisionReason::Failure) ++work.MergeRootFailures;
        if (event.Kind == K::AttemptBegin) { ++work.SplitAttempts; if (event.Reason == Dod::DecisionReason::Forced) ++work.ForcedAttempts; }
        if (event.Kind == K::SplitComplete) { ++work.PrimitiveSplits; if (event.Reason == Dod::DecisionReason::Forced) ++work.ForcedSplits; }
        if (event.Kind == K::MergeComplete) ++work.PrimitiveMerges;
        if (event.Kind == K::Reserve && event.Reason == Dod::DecisionReason::BudgetRejected) ++work.BudgetRejections;
    }
    const auto& actual = plan.Metrics.Work;
    Require(work.SplitRoots == actual.SplitRoots && work.MergeRoots == actual.MergeRoots &&
        work.SplitRootFailures == actual.SplitRootFailures && work.MergeRootFailures == actual.MergeRootFailures && work.Exchanges == actual.Exchanges &&
        work.SplitAttempts == actual.SplitAttempts && work.ForcedAttempts == actual.ForcedAttempts &&
        work.PrimitiveSplits == actual.PrimitiveSplits && work.ForcedSplits == actual.ForcedSplits &&
        work.PrimitiveMerges == actual.PrimitiveMerges && work.BudgetRejections == actual.BudgetRejections, "logical work summary mismatch");
}
}
