#include "DataOrientedRoamExperimentTestSupport.h"
#include "algorithms/data_oriented_roam/DataOrientedRoamPassEvidence.h"
#include "algorithms/data_oriented_roam/DataOrientedRoamPassInput.h"
#include "algorithms/data_oriented_roam/DataOrientedRoamPassMeasurement.h"
#include "algorithms/data_oriented_roam/DataOrientedRoamPipeline.h"
#include "algorithms/data_oriented_roam/DataOrientedRoamTopologyExperiment.h"
#include "algorithms/data_oriented_roam/DataOrientedRoamStateOps.h"
#include "algorithms/data_oriented_roam/DataOrientedRoamScoring.h"
#include "algorithms/data_oriented_roam/DataOrientedRoamQueues.h"
#include "benchmark/formal/FormalCpuInput.h"
#include "experiment/formal/FormalExperimentCamera.h"
#include "experiment/formal/FormalExperimentManifest.h"

#include <array>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <sstream>
#include <string>
#include <algorithm>

using namespace ParallelRoam;
using namespace Tests;
using namespace Experiment::Formal;

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
} // 匿名命名空间

int main(int argc, char** argv)
{
    try
    {
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
        std::cout << "operation observation fixtures complete\n";
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
