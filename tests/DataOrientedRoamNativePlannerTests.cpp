#include "DataOrientedRoamNativePlannerTestSupport.h"
#include "algorithms/data_oriented_roam/DataOrientedRoamStateOps.h"
#include "algorithms/data_oriented_roam/DataOrientedRoamScoring.h"
#include "algorithms/data_oriented_roam/DataOrientedRoamVariance.h"
#include "algorithms/data_oriented_roam/DataOrientedRoamValidation.h"
#include "terrain/HeightMap.h"

#include <iostream>

using namespace NativePlannerTests;

namespace
{
std::size_t partialRootFailures = 0;
Dod::DataOrientedRoamState Seed(const Terrain::HeightMap& height)
{
    Dod::DataOrientedRoamState state;
    state.HeightMap = &height; state.TerrainSize = 30; state.HeightScale = 4;
    state.Settings.MaxDepth = 6; state.Settings.TriangleBudget = 128;
    state.Settings.SplitThreshold = -1; state.Settings.MergeThreshold = -2;
    state.Settings.EnableLocalConstraints = true; state.Settings.MirrorSplitScoresToNodePool = false;
    state.Settings.PassPolicy = Algorithms::MakeTerrainLodSerialIncrementalPolicy();
    state.BuildSequence = 1;
    const auto camera = ExperimentView(0);
    state.ViewProjection = camera.ViewProjection; state.FrustumPlanes = camera.FrustumPlanes;
    state.DrawableWidth = 1280; state.DrawableHeight = 720;
    Dod::RebuildVarianceTrees(state, 6); Dod::ResetTopology(state);
    state.Settings.MaxDepth = 3;
    Dod::AdvanceSplitTopologySerialForExperiment(state);
    state.Settings.MaxDepth = 6;
    Require(state.ActiveLeafNodes.size() == 16, "unexpected uniform seed");
    return state;
}

void Refresh(Dod::DataOrientedRoamState& source)
{
    ++source.BuildSequence;
    source.CurrentSplitPaths.clear();
    Dod::RefreshPersistentMergeQueuePriorities(source);
    Dod::RefreshPersistentSplitQueuePriorities(source);
}

/// <summary>
/// 微型夹具保存逐步参考投影，用于定位首个分歧；自然输入只做完整终态投影
/// </summary>
struct StepAudit
{
    std::vector<Projection> Steps;
    std::size_t Next{0};
    Projection Final;
    static void After(void* context, const Native::NativePlanningView& view, const Native::NativePlanningQueues& queues)
    {
        auto& self = *static_cast<StepAudit*>(context);
        Require(self.Next < self.Steps.size(), "planner executed an extra strict step");
        Compare(self.Steps[self.Next++], Project(view, queues));
        Require(queues.Validate(Native::NativePlanningQueueKind::Split) &&
            queues.Validate(Native::NativePlanningQueueKind::Merge), "private heap invariant");
    }
    static void Finish(void* context, const Native::NativePlanningView& view, const Native::NativePlanningQueues& queues,
        const Native::NativeTargetPlan&)
    {
        static_cast<StepAudit*>(context)->Final = Project(view, queues);
    }
};

Native::NativeTargetPlan Check(std::string_view name, const Dod::DataOrientedRoamState& source, bool legal = true)
{
    StateSnapshot snapshot{source};
    auto legacy = source;
    auto iteration = Dod::BeginStrictSplitIteration(legacy);
    const auto before = Project(legacy);
    TraceLog oldTrace, newTrace;
    StepAudit audit;
    while (iteration.Stop == Dod::TopologySplitStop::Running)
    {
        (void)Dod::AdvanceStrictSplitIterationWithDecisions(legacy, iteration, oldTrace.Sink());
        audit.Steps.push_back(Project(legacy));
    }
    const auto plan = Native::BuildNativeSplitTarget(source,
        {newTrace.Sink(), &audit, StepAudit::After, StepAudit::Finish, true});
    Require(audit.Next == audit.Steps.size(), "planner skipped a strict step");
    Compare(oldTrace, newTrace); Compare(Project(legacy), audit.Final);
    VerifyPlan(before, audit.Final, oldTrace, iteration, plan);
    std::size_t completedInRoot = 0, partial = 0;
    for (const auto& event : oldTrace.Events)
    {
        if (event.Kind == Dod::DecisionEventKind::SelectSplit) completedInRoot = 0;
        if (event.Kind == Dod::DecisionEventKind::SplitComplete) ++completedInRoot;
        if (event.Kind == Dod::DecisionEventKind::SplitRootEnd && event.Reason == Dod::DecisionReason::Failure && completedInRoot > 0)
            ++partial;
    }
    partialRootFailures += partial;
    auto plainLegacy = source;
    Dod::AdvanceSplitTopologySerialForExperiment(plainLegacy);
    Compare(Project(legacy), Project(plainLegacy));
    const auto plain = Native::BuildNativeSplitTarget(source);
    Require(plain.AddedEvents == plan.AddedEvents && plain.RemovedEvents == plan.RemovedEvents &&
        plain.Obligations == plan.Obligations && plain.Evaluations == plan.Evaluations && plain.Stop == plan.Stop &&
        plain.Iteration == plan.Iteration && plain.Metrics.Work == plan.Metrics.Work, "diagnostics changed the target plan");
    Require(snapshot == StateSnapshot{source}, "planner modified source values, capacity or addresses");
    if (legal)
    {
        legacy.Stats = {};
        Dod::ValidateTopology(legacy);
        Require(legacy.Stats.InvalidTopologyCount == 0 && Dod::CountPersistentQueueInvariantViolations(legacy) == 0, "reference legality");
    }
    for (const auto& evaluation : plan.Evaluations)
    {
        const auto found = std::find_if(audit.Final.Nodes.begin(), audit.Final.Nodes.end(), [&](const auto& node) { return node.Path == evaluation.Path; });
        Require(found != audit.Final.Nodes.end(), "evaluation identity missing");
        const auto& g = found->Geometry;
        const Dod::TriangleDomain domain{{std::bit_cast<float>(g[0]), std::bit_cast<float>(g[1])},
            {std::bit_cast<float>(g[2]), std::bit_cast<float>(g[3])}, {std::bit_cast<float>(g[4]), std::bit_cast<float>(g[5])}};
        Require(evaluation.Score == Dod::ComputeScreenErrorScore(source, domain, std::bit_cast<float>(g[6])), "pure cache value mismatch");
    }
    std::cout << name << " steps=" << plan.Iteration << " trace=" << oldTrace.Events.size()
        << " k=" << plan.AddedEvents.size() + plan.RemovedEvents.size() << " h=" << plan.Obligations.size()
        << " forced=" << plan.Metrics.Work.ForcedAttempts << " exchanges=" << plan.Metrics.Work.Exchanges
        << " partialFailures=" << partial << '\n';
    return plan;
}

void BoundaryCases(const Dod::DataOrientedRoamState& seed)
{
    auto stop = seed;
    Refresh(stop);
    stop.Settings.SplitThreshold = 100; stop.Settings.MergeThreshold = 10;
    for (auto& entry : stop.SplitQueue) entry.Score = 50;
    std::sort(stop.SplitQueue.begin(), stop.SplitQueue.end(), [&](const auto& a, const auto& b) {
        return stop.Nodes.PathIdAt(a.Node) < stop.Nodes.PathIdAt(b.Node);
    });
    for (std::size_t i = 0; i < stop.SplitQueue.size(); ++i)
        stop.NodeMembership[stop.SplitQueue[i].Node].SplitQueuePosition = static_cast<Dod::DataOrientedRoamPosition>(i);
    for (auto& entry : stop.MergeQueue) entry.Score = 10;
    std::sort(stop.MergeQueue.begin(), stop.MergeQueue.end(), [&](const auto& a, const auto& b) {
        return stop.Nodes.PathIdAt(a.Node) < stop.Nodes.PathIdAt(b.Node);
    });
    for (std::size_t i = 0; i < stop.MergeQueue.size(); ++i)
        stop.NodeMembership[stop.MergeQueue[i].Node].MergeQueuePosition = static_cast<Dod::DataOrientedRoamPosition>(i);
    // 相同分数但首项不在迟滞集合中，不能越过它去执行后面的可细分项
    stop.PreviousSplitPaths.insert(stop.Nodes.PathIdAt(stop.SplitQueue[1].Node));
    const auto stopped = Check("tie-head-stop / merge-equality", stop);
    Require(stopped.Metrics.Work.SplitRoots == 0 && stopped.Metrics.Work.MergeRoots == 0, "strict threshold boundary");
    stop.PreviousSplitPaths.insert(stop.Nodes.PathIdAt(stop.SplitQueue[0].Node));
    Require(Check("tie-head-hysteresis", stop).Metrics.Work.SplitRoots > 0, "hysteresis did not select head");

    auto sameBuild = seed;
    sameBuild.Settings.TriangleBudget = sameBuild.ActiveLeafNodes.size();
    Dod::RefreshPersistentSplitQueuePriorities(sameBuild);
    Require(Check("same-build-budget-block", sameBuild).Stop == Dod::TopologySplitStop::BudgetBlocked, "same-build suppression");

    // 防御分支采用显式故障注入，不把过期成员或失效邻接当成自然共形输入
    auto failedMerge = seed;
    Refresh(failedMerge);
    const auto representative = failedMerge.MergeQueue.front().Node;
    failedMerge.Nodes.IsSplits[representative] = 0;
    failedMerge.Settings.MergeThreshold = 1e30F; failedMerge.Settings.SplitThreshold = 1e30F;
    Require(Check("fault: stale merge", failedMerge, false).Metrics.Work.MergeRootFailures > 0, "merge failure branch missing");

    auto failedSplit = seed;
    Refresh(failedSplit);
    const auto root = failedSplit.SplitQueue.front().Node;
    failedSplit.Nodes.BaseNeighbors[root] = failedSplit.RootA;
    for (auto& entry : failedSplit.SplitQueue)
        entry.Score = entry.Node == root ? std::numeric_limits<float>::max() : -std::numeric_limits<float>::max();
    std::sort(failedSplit.SplitQueue.begin(), failedSplit.SplitQueue.end(), [&](const auto& a, const auto& b) {
        return a.Score != b.Score ? a.Score > b.Score : failedSplit.Nodes.PathIdAt(a.Node) < failedSplit.Nodes.PathIdAt(b.Node);
    });
    for (std::size_t i = 0; i < failedSplit.SplitQueue.size(); ++i)
        failedSplit.NodeMembership[failedSplit.SplitQueue[i].Node].SplitQueuePosition = static_cast<Dod::DataOrientedRoamPosition>(i);
    const auto blocked = Check("fault: non-leaf prerequisite", failedSplit, false);
    Require(blocked.AddedEvents.empty() && blocked.RemovedEvents.empty() && !blocked.Obligations.empty(),
        "empty net target concealed a continuation obligation");
    Require(std::any_of(blocked.Obligations.begin(), blocked.Obligations.end(), [](const auto& value) {
        return value.Kind == Native::NativeObligationKind::SplitBlockedBuild;
    }), "non-budget split failure did not produce a named disposition");

    // 人工移动迭代位置只核查原控制组件的后增边界，完整 Planner 仍使用自然冻结上限
    auto limit = seed;
    auto iteration = Dod::BeginStrictSplitIteration(limit);
    iteration.Iteration = iteration.MaximumIterations;
    TraceLog trace;
    (void)Dod::AdvanceStrictSplitIterationWithDecisions(limit, iteration, trace.Sink());
    Require(iteration.Stop == Dod::TopologySplitStop::IterationLimit && iteration.Iteration == iteration.MaximumIterations + 1 &&
        trace.Events.size() == 1 && trace.Events.front().Value == static_cast<std::size_t>(Dod::TopologySplitStop::IterationLimit),
        "iteration post-increment boundary");
}
}

int main()
{
    try
    {
        Terrain::HeightMap height;
        std::string error;
        Require(height.LoadFromFile("assets/heightmaps/Hm_Terrain_Test_129.pgm", &error), error);
        const auto seed = Seed(height);
        auto refine = seed; Refresh(refine);
        Check("refine-to-depth", refine);
        auto coarse = seed; coarse.Settings.MergeThreshold = coarse.Settings.SplitThreshold = 1e30F; Refresh(coarse);
        Check("coarsen", coarse);
        Dod::AdvanceSplitTopologySerialForExperiment(coarse);
        coarse.Settings.SplitThreshold = -1; coarse.Settings.MergeThreshold = -2; Refresh(coarse);
        Check("reactivate-cache", coarse);
        for (const std::size_t extra : {0U, 1U, 2U})
        {
            auto exchange = seed; exchange.Settings.TriangleBudget = seed.ActiveLeafNodes.size() + extra; Refresh(exchange);
            Check("budget-" + std::to_string(extra), exchange);
        }
        BoundaryCases(seed);
        Require(partialRootFailures > 0, "fixtures did not exercise a failed root retaining successful prerequisites");
        return 0;
    }
    catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
