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

void AuditStorage()
{
    NativePlanningStorage<std::uint64_t, std::uint64_t> table{true};
    Require(table.Metrics().ReservedBytes == 0 && table.Find(0) == decltype(table)::Missing, "empty storage allocated source domain");
    std::map<std::uint64_t, std::pair<std::size_t, std::uint64_t>> oracle;
    for (std::uint64_t i = 0; i < 4096; ++i)
    {
        const auto key = i == 4095 ? std::numeric_limits<std::uint64_t>::max() : i * 2654435761ULL;
        const auto index = table.Ensure(key);
        table.At(index) = i + 9;
        oracle.emplace(key, std::pair{index, i + 9});
    }
    // 索引跨多次扩容保持，键的极值和业务无效值都不承担空槽标记
    for (const auto& [key, expected] : oracle)
    {
        Require(table.Find(key) == expected.first && table.At(expected.first) == expected.second, "rehash lost a record handle");
        table.Set(key, std::numeric_limits<std::uint64_t>::max());
        Require(table.At(table.Find(key)) == std::numeric_limits<std::uint64_t>::max(), "explicit absent value fell through");
    }
    const auto metrics = table.Metrics();
    Require(table.size() == oracle.size() && metrics.Rehashes > 1 && metrics.MaximumProbe > 1 &&
        metrics.InitializedSlots < metrics.IndexCapacity * 2 && metrics.MovedRecords < metrics.RecordCapacity &&
        metrics.PeakReservedBytes >= metrics.ReservedBytes, "storage growth or accounting contract");
    NativePlanningCosts costs;
    costs.Detailed = true;
    {
        NativePlanningCostScope parent{&costs, NativePlanningCost::Control};
        { NativePlanningCostScope child{&costs, NativePlanningCost::Queue, true}; }
        Require(costs.Active == NativePlanningCost::Control, "nested timing failed to restore parent");
        parent.Finish(); parent.Finish();
    }
    Require(costs.Active == NativePlanningCost::Count && costs.Calls[1] == 1 && costs.Calls[8] == 1,
        "cost scope double-finished or counted a parent twice");
}

void AuditPagedStorage()
{
    NativePlanningStorage<std::uint64_t, std::uint64_t, true> table{true};
    Require(table.Find(700000) == decltype(table)::Missing && table.Metrics().ReservedBytes == 0,
        "missing page read allocated an index");
    std::map<std::uint64_t, std::pair<std::size_t, std::uint64_t>> oracle;
    for (std::uint64_t i = 0; i < 2048; ++i)
    {
        const auto key = (i * 257U) % 800003U;
        const auto record = table.Ensure(key);
        table.At(record) = i + 1;
        oracle.emplace(key, std::pair{record, i + 1});
    }
    // 逐页增长后再跨越页边界，既核对逻辑值，也核对稳定记录身份
    for (const auto key : {255ULL, 256ULL, 257ULL, 700000ULL}) table.Set(key, 77);
    for (const auto& [key, expected] : oracle)
        Require(table.Find(key) == expected.first && table.At(expected.first) == (key == 257 ? 77 : expected.second),
            "page growth changed an existing record");
    table.Set(256, std::numeric_limits<std::uint64_t>::max());
    Require(table.At(table.Find(256)) == std::numeric_limits<std::uint64_t>::max(), "page absent override fell through");
    const auto before = table.Metrics();
    Require(table.Find(4000000000ULL) == decltype(table)::Missing && table.Metrics().ReservedBytes == before.ReservedBytes,
        "high missing page read grew the directory");
    bool rejected = false;
    try { table.Set(1ULL << 32U, 9); } catch (const std::out_of_range&) { rejected = true; }
    Require(rejected && table.Find(0) == oracle.at(0).first, "page key was silently truncated");
    const auto metrics = table.Metrics();
    Require(metrics.PagedIndex && metrics.Rehashes == 0 && metrics.IndexPages > 1 &&
        metrics.InitializedSlots == metrics.IndexPages * NativePlanningPageIndex::PageSize &&
        metrics.DirectoryInitialized == metrics.DirectorySize && metrics.DirectoryMoved > 0 &&
        metrics.PeakReservedBytes >= metrics.ReservedBytes, "page growth costs not accounted");
}

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

void AuditCompactState(const DataOrientedRoamState& source)
{
    using Member = NativePlanningMembership::Field;
    NativePlanningMembership members{source, true};
    const auto node = source.ActiveLeafNodes.front();
    const auto extra = static_cast<Node>(source.Nodes.size() + 257U);
    Require(members.Get(node, Member::Split) == source.NodeMembership[node].SplitQueuePosition &&
        members.Metrics().SourceCopies == 0, "membership read copied the old domain");
    members.Set(extra, Member::Partner, node);
    Require(members.Metrics().SourceCopies == 0 && members.Get(extra - 1U, Member::Split) == InvalidDataOrientedRoamPosition,
        "virtual membership tail requires an old-state copy or inherits stale membership");
    members.Set(node, Member::Split, InvalidDataOrientedRoamPosition);
    Require(members.Metrics().SourceCopies == source.Nodes.size() && members.Records(Member::Split) == 1 &&
        members.Get(extra, Member::Partner) == node &&
        members.Get(node, Member::Representative) == source.NodeMembership[node].MergeQueueRepresentative,
        "membership projection lost a field or virtual suffix");
    members.Set(node, Member::Split, source.NodeMembership[node].SplitQueuePosition);
    Require(members.Records(Member::Split) == 1, "restored membership erased cumulative write coverage");
    NativePlanningView view{source};
    for (const auto field : {Field::ActivatedBuild, Field::SplitBuild, Field::MergeBuild, Field::SplitBlockedBuild})
    {
        const auto old = view.Read(node, field);
        view.Write(node, field, (1ULL << 60U) + static_cast<std::size_t>(field));
        Require(view.Read(node, field) == (1ULL << 60U) + static_cast<std::size_t>(field), "compact build counter truncated");
        view.Write(node, field, old);
    }
    for (const auto field : {Field::ForcedActivation, Field::IsSplit, Field::CurrentSplitPath}) view.Write(node, field, 1);
    view.Write(node, Field::Activity, static_cast<std::uint64_t>(NativePlanningActivity::Internal));
    view.Write(node, Field::IsSplit, 0);
    Require(view.Read<Field::IsSplit>(node) == view.Read(node, Field::IsSplit), "fixed read mismatch");
    Require(view.Read(node, Field::ForcedActivation) == 1 && view.Read(node, Field::CurrentSplitPath) == 1 &&
        view.Read(node, Field::IsSplit) == 0 && view.Activity(node) == NativePlanningActivity::Internal,
        "compact flags overwrote adjacent fields");
    // 同一字段在有序赋值中往返，结果和累计计数都须与逐次写入相同
    const auto writes = view.Metrics().FieldWrites;
    view.WriteFields(node, {{Field::IsSplit, 1}, {Field::IsSplit, 0}, {Field::ForcedActivation, 1}});
    Require(view.Metrics().FieldWrites == writes + 2, "grouped writes changed no-op filtering");
    {
        const auto cursor = view.Inspect(node);
        Require(cursor.Read<Field::IsSplit>() == view.Read<Field::IsSplit>(node) &&
            cursor.Read<Field::MergeBuild>() == view.Read<Field::MergeBuild>(node) &&
            cursor.Read<Field::Activity>() == view.Read<Field::Activity>(node), "local read cursor changed field values");
    }
    bool invalidField = false;
    try { view.WriteFields(node, {{Field::Activity, 99}}); }
    catch (const std::invalid_argument&) { invalidField = true; }
    Require(invalidField && view.Activity(node) == NativePlanningActivity::Internal, "grouped write accepted invalid activity");
}

void CheckSplit(const NativePlanningView& view, const NativePlanningQueues& queues, const std::map<Node, float>& expected)
{
    Require(queues.Validate(Kind::Split) && queues.Size(Kind::Split) == expected.size(), "split overlay structure");
    std::map<Node, float> actual;
    for (std::size_t i = 0; i < queues.Size(Kind::Split); ++i)
    {
        const auto entry = queues.At(Kind::Split, i);
        Require(entry.Path == view.Path(entry.Node), "cached heap key differs from stable identity");
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
    Require(queues.StorageMetrics()[0].SourceCopies == 0, "read-only heap allocated a private copy");
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

    // 更多虚拟身份只用于堆存储压力，独立成员 oracle 不假定这些都是活动叶
    std::vector<Node> pending{children[0], children[1]};
    for (std::size_t i = 0; i < pending.size() && pending.size() < 100; ++i)
    {
        if (view.Depth(pending[i]) >= source.Settings.MaxDepth) continue;
        const auto pair = view.CreateChildren(pending[i]);
        pending.insert(pending.end(), pair.begin(), pair.end());
    }
    for (std::size_t i = 0; i < pending.size(); ++i)
    {
        const auto score = static_cast<float>((i * 17U) % 11U);
        queues.UpsertSplit(pending[i], score); expected[pending[i]] = score;
        CheckSplit(view, queues, expected);
    }
    for (std::size_t i = 0; i < pending.size(); i += 3)
    {
        Require(queues.RemoveSplit(pending[i]), "growth fixture lost reverse membership"); expected.erase(pending[i]);
        queues.UpsertSplit(pending[i], -3); expected[pending[i]] = -3;
        CheckSplit(view, queues, expected);
    }

    // 以下是代表关系组件输入，不把任意改组称为已通过拓扑资格审查的菱形
    const auto representative = source.MergeQueue.front().Node;
    const auto partner = source.NodeMembership[representative].MergeQueuePartner;
    Require(queues.RemoveMerge(partner) && queues.Size(Kind::Merge) == 0 &&
        queues.MergeRepresentative(representative) == InvalidDataOrientedRoamNodeIndex &&
        queues.MergeRepresentative(partner) == InvalidDataOrientedRoamNodeIndex, "partner removal left a stale representative");
    queues.UpsertMerge(representative, partner, 6);
    queues.BeginMergeMaintenance();
    queues.InvalidateMerge(partner);
    Require(queues.MergeRepresentative(representative) == InvalidDataOrientedRoamNodeIndex,
        "deferred invalidation left a logically visible candidate");
    bool headRejected = false;
    try { (void)queues.Top(Kind::Merge); } catch (const std::logic_error&) { headRejected = true; }
    Require(headRejected, "incomplete merge heap leaked to root selection");
    queues.UpsertMerge(representative, partner, 6);
    queues.FinishMergeMaintenance();
    Require(queues.Validate(Kind::Merge) && queues.Metrics(Kind::Merge).RestoredEntries == 1 &&
        queues.Metrics(Kind::Merge).UnchangedUpserts == 1, "surviving candidate was not restored in place");
    queues.BeginMergeMaintenance();
    queues.InvalidateMerge(representative);
    queues.FinishMergeMaintenance();
    Require(queues.Validate(Kind::Merge) && queues.Size(Kind::Merge) == 0 &&
        queues.Metrics(Kind::Merge).DeferredErases == 1, "obsolete deferred candidate survived publication");
    queues.UpsertMerge(representative, partner, 6);
    queues.UpsertMerge(children[0], children[1], 2);
    Require(queues.Validate(Kind::Merge) && queues.Top(Kind::Merge).Node == children[0], "merge score order");
    queues.BeginMergeMaintenance();
    queues.InvalidateMerge(representative); queues.InvalidateMerge(children[0]);
    queues.UpsertMerge(representative, children[0], 1);
    queues.FinishMergeMaintenance();
    Require(queues.Validate(Kind::Merge) && queues.Size(Kind::Merge) == 1 &&
        queues.MergeRepresentative(partner) == InvalidDataOrientedRoamNodeIndex &&
        queues.MergeRepresentative(children[1]) == InvalidDataOrientedRoamNodeIndex, "pair regrouping kept an old diamond");
    Require(queues.RemoveMerge(children[0]) && !queues.RemoveMerge(children[0]) && queues.Validate(Kind::Merge),
        "merge re-removal or empty heap invariant");
    const auto metrics = queues.Metrics(Kind::Split);
    Require(queues.StorageMetrics()[0].SourceCopies == source.SplitQueue.size(),
        "dense heap source copy was not charged independently of writes");
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
        AuditStorage();
        AuditPagedStorage();
        Terrain::HeightMap height;
        std::string error;
        Require(height.LoadFromFile("assets/heightmaps/Hm_Terrain_Test_129.pgm", &error), error);
        const auto source = MakeSource(height);
        AuditCompactState(source);
        for (const bool diagnostics : {false, true}) { AuditView(source, diagnostics); AuditQueues(source, diagnostics); }
        return 0;
    }
    catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
