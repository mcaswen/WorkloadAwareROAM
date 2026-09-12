#include "DataOrientedRoamExperimentTestSupport.h"
#include "algorithms/data_oriented_roam/materialization/NativePlanningQueues.h"
#include "algorithms/data_oriented_roam/DataOrientedRoamStateOps.h"
#include "algorithms/data_oriented_roam/DataOrientedRoamTopologyExperiment.h"
#include "algorithms/data_oriented_roam/DataOrientedRoamVariance.h"
#include "terrain/HeightMap.h"

#include <algorithm>
#include <array>
#include <iostream>
#include <map>

using namespace ParallelRoam;
using namespace Tests;
using namespace Algorithms::DataOrientedRoam::Materialization;

namespace
{
using Node = DataOrientedRoamNodeIndex;
using Field = NativePlanningField;
using Kind = NativePlanningQueueKind;

DataOrientedRoamState MakeSource(const Terrain::HeightMap& height)
{
    DataOrientedRoamState state;
    state.HeightMap = &height;
    state.TerrainSize = 30; state.HeightScale = 4;
    state.Settings.MaxDepth = 6; state.Settings.TriangleBudget = 64;
    state.Settings.SplitThreshold = -1; state.Settings.MergeThreshold = -2;
    state.Settings.PassPolicy = MakeTerrainLodSerialIncrementalPolicy();
    state.BuildSequence = 1;
    const auto view = ExperimentView(0);
    state.ViewProjection = view.ViewProjection; state.FrustumPlanes = view.FrustumPlanes;
    state.DrawableWidth = 1280; state.DrawableHeight = 720;
    RebuildVarianceTrees(state, 6);
    ResetTopology(state);
    auto iteration = BeginStrictSplitIteration(state);
    Require(AdvanceStrictSplitIteration(state, iteration).SplitSucceeded, "storage fixture did not refine roots");
    Require(state.ActiveLeafNodes.size() == 4 && state.MergeQueue.size() == 1, "unexpected storage fixture shape");
    return state;
}

void CheckSplit(const NativePlanningView& view, const NativePlanningQueues& queues, const std::map<Node, float>& expected)
{
    Require(queues.Validate(Kind::Split) && queues.Size(Kind::Split) == expected.size(), "split overlay structure");
    std::map<Node, float> actual;
    for (std::size_t i = 0; i < queues.Size(Kind::Split); ++i)
    {
        const auto entry = queues.At(Kind::Split, i);
        Require(actual.emplace(entry.Node, entry.Score).second, "duplicate split entry");
    }
    Require(actual == expected, "split overlay membership or score");
    if (expected.empty())
    {
        Require(queues.Top(Kind::Split).Node == InvalidDataOrientedRoamNodeIndex, "empty split head");
        return;
    }
    // 用无序成员集合独立选最大分数及最小身份，不复用被测堆修复过程
    auto best = expected.begin();
    for (auto next = expected.begin(); next != expected.end(); ++next)
        if (next->second > best->second || (next->second == best->second && view.Path(next->first) < view.Path(best->first)))
            best = next;
    Require(queues.Top(Kind::Split).Node == best->first, "split overlay chose a different logical head");
}

void AuditView(const DataOrientedRoamState& source, bool diagnostics)
{
    const StateSnapshot before{source};
    NativePlanningView view{source, diagnostics};
    Require(view.Metrics().Queries == 0 && view.Metrics().OldNodeRecords == 0 && view.Metrics().VirtualNodes == 0,
        "view constructor materialized source records");
    for (Node node = 0; node < source.Nodes.size(); ++node)
    {
        Require(view.FindPath(source.Nodes.PathIdAt(node)) == node && view.Parent(node) == source.Nodes.ParentAt(node) &&
            view.Depth(node) == source.Nodes.DepthAt(node), "borrowed hierarchy differs from source");
    }
    Require(view.FindPath(0) == InvalidDataOrientedRoamNodeIndex &&
        view.FindPath(3ULL << 32U) == InvalidDataOrientedRoamNodeIndex, "invalid hierarchy path accepted");
    const auto root = source.RootA;
    const auto oldBase = view.Relation(root, Field::BaseNeighbor);
    view.Write(root, Field::BaseNeighbor, InvalidDataOrientedRoamNodeIndex);
    Require(view.Relation(root, Field::BaseNeighbor) == InvalidDataOrientedRoamNodeIndex, "missing relation overlay");
    view.Write(root, Field::BaseNeighbor, oldBase);
    Require(view.Metrics().OldNodeRecords == 1 && view.Metrics().ChangedFields == 0, "restoration erased cumulative coverage");
    view.Write(root, Field::Activity, static_cast<std::uint64_t>(NativePlanningActivity::Dormant));
    Require(view.Read(root, Field::IsSplit) == 1, "activity was conflated with cached split flag");
    view.Write(root, Field::Activity, static_cast<std::uint64_t>(NativePlanningActivity::Internal));

    const auto parent = source.ActiveLeafNodes.front();
    const auto children = view.CreateChildren(parent);
    for (const auto child : children)
    {
        Require(view.Parent(child) == parent && view.FindPath(view.Path(child)) == child &&
            view.Activity(child) == NativePlanningActivity::Dormant && view.CreatedBuild(child) == source.BuildSequence,
            "virtual child identity or lifecycle");
        Require(view.GeometricError(child) == VarianceError(source, view.VarianceTree(child), view.VarianceIndex(child)),
            "virtual child variance differs from native lookup");
        view.Write(child, Field::Activity, static_cast<std::uint64_t>(NativePlanningActivity::Leaf));
    }
    bool duplicateRejected = false;
    try { (void)view.CreateChildren(parent); } catch (const std::logic_error&) { duplicateRejected = true; }
    Require(duplicateRejected, "duplicate virtual children accepted");
    const auto remaining = view.RemainingBudget();
    for (std::size_t i = 0; i < remaining; ++i) Require(view.TryReserveBudget(), "early budget rejection");
    Require(!view.TryReserveBudget(), "hard budget over-reserved");
    view.ReleaseBudget();
    Require(view.RemainingBudget() == 1, "budget reservation was not released");
    const auto metrics = view.Metrics();
    Require(metrics.VirtualNodes == 2 && metrics.VirtualNodeRecords == 2 && metrics.OldNodeRecords == 2 &&
        metrics.ReadCoverageCollected == diagnostics && (metrics.DistinctSourceNodesRead > 0) == diagnostics,
        "view accounting contract");
    Require(before == StateSnapshot{source}, "planning view mutated source storage");
}

void AuditQueues(const DataOrientedRoamState& source, bool diagnostics)
{
    const StateSnapshot before{source};
    NativePlanningView view{source, diagnostics};
    NativePlanningQueues queues{view, diagnostics};
    Require(queues.Metrics(Kind::Split).Reads == 0 && queues.Metrics(Kind::Split).OldSlotsWritten == 0,
        "queue constructor copied the source heap");
    const auto children = view.CreateChildren(source.ActiveLeafNodes.front());
    std::map<Node, float> expected;
    for (const auto& entry : source.SplitQueue) expected[entry.Node] = entry.Score;
    CheckSplit(view, queues, expected);
    for (const auto& entry : source.SplitQueue)
    {
        queues.UpsertSplit(entry.Node, 7); expected[entry.Node] = 7;
        CheckSplit(view, queues, expected);
    }
    const auto oldTail = queues.At(Kind::Split, queues.Size(Kind::Split) - 1U).Node;
    Require(queues.RemoveSplit(oldTail), "tail removal failed"); expected.erase(oldTail);
    CheckSplit(view, queues, expected);
    const auto oldHead = queues.Top(Kind::Split).Node;
    Require(queues.RemoveSplit(oldHead), "head removal failed"); expected.erase(oldHead);
    CheckSplit(view, queues, expected);
    queues.UpsertSplit(children[0], 20); expected[children[0]] = 20;
    queues.UpsertSplit(oldTail, 8); expected[oldTail] = 8;
    CheckSplit(view, queues, expected);
    while (!expected.empty())
    {
        CheckSplit(view, queues, expected);
        const auto top = queues.Top(Kind::Split).Node;
        Require(queues.RemoveSplit(top), "draining split overlay failed"); expected.erase(top);
    }
    CheckSplit(view, queues, expected);
    // 清空再追加跨越来源长度，旧尾部和已删除的反向位置都不能复活
    for (const auto node : {children[1], oldHead, children[0], oldTail})
    {
        queues.UpsertSplit(node, 4); expected[node] = 4;
        CheckSplit(view, queues, expected);
    }
    for (const auto& entry : source.SplitQueue)
    {
        queues.UpsertSplit(entry.Node, 4); expected[entry.Node] = 4;
    }
    CheckSplit(view, queues, expected);
    Require(!queues.RemoveSplit(InvalidDataOrientedRoamNodeIndex), "absent removal unexpectedly succeeded");

    // 以下是代表关系组件输入，不把任意改组称为已通过拓扑资格审查的菱形
    const auto representative = source.MergeQueue.front().Node;
    const auto partner = source.NodeMembership[representative].MergeQueuePartner;
    Require(queues.RemoveMerge(partner) && queues.Size(Kind::Merge) == 0 &&
        queues.MergeRepresentative(representative) == InvalidDataOrientedRoamNodeIndex &&
        queues.MergeRepresentative(partner) == InvalidDataOrientedRoamNodeIndex, "partner removal left a stale representative");
    queues.UpsertMerge(representative, partner, 6);
    queues.UpsertMerge(children[0], children[1], 2);
    Require(queues.Validate(Kind::Merge) && queues.Top(Kind::Merge).Node == children[0], "merge score order");
    queues.UpsertMerge(representative, children[0], 1);
    Require(queues.Validate(Kind::Merge) && queues.Size(Kind::Merge) == 1 &&
        queues.MergeRepresentative(partner) == InvalidDataOrientedRoamNodeIndex &&
        queues.MergeRepresentative(children[1]) == InvalidDataOrientedRoamNodeIndex, "pair regrouping kept an old diamond");
    Require(queues.RemoveMerge(children[0]) && !queues.RemoveMerge(children[0]) && queues.Validate(Kind::Merge),
        "merge re-removal or empty heap invariant");
    const auto metrics = queues.Metrics(Kind::Split);
    Require(metrics.OldSlotsWritten > 0 && metrics.AppendedSlotsWritten > 0 && metrics.ReverseRecords >= expected.size() &&
        metrics.ReadCoverageCollected == diagnostics && (metrics.DistinctSourceSlotsRead > 0) == diagnostics,
        "queue coverage or membership accounting");
    Require(before == StateSnapshot{source}, "planning queues mutated source storage");
    std::cout << "storage diagnostics=" << diagnostics << " split-old-slots=" << metrics.OldSlotsWritten
        << " appended-slots=" << metrics.AppendedSlotsWritten << " reverse-records=" << metrics.ReverseRecords << '\n';
}
}

int main()
{
    try
    {
        Terrain::HeightMap height;
        std::string error;
        Require(height.LoadFromFile("assets/heightmaps/Hm_Terrain_Test_129.pgm", &error), error);
        const auto source = MakeSource(height);
        for (const bool diagnostics : {false, true}) { AuditView(source, diagnostics); AuditQueues(source, diagnostics); }
        return 0;
    }
    catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
