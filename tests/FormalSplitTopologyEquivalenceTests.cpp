#include "DataOrientedRoamExperimentTestSupport.h"
#include "algorithms/data_oriented_roam/DataOrientedRoamPassEvidence.h"
#include "algorithms/data_oriented_roam/DataOrientedRoamPassInput.h"
#include "algorithms/data_oriented_roam/DataOrientedRoamPassMeasurement.h"
#include "algorithms/data_oriented_roam/DataOrientedRoamPipeline.h"
#include "algorithms/data_oriented_roam/DataOrientedRoamQueues.h"
#include "algorithms/data_oriented_roam/DataOrientedRoamTopologyPlan.h"
#include "benchmark/formal/FormalCpuInput.h"
#include "experiment/formal/FormalExperimentCamera.h"
#include "experiment/formal/FormalExperimentManifest.h"

#include <algorithm>
#include <iostream>
#include <string>
#include <utility>
#include <vector>

using namespace ParallelRoam;
using namespace Tests;
using namespace Experiment::Formal;

namespace
{
constexpr auto SplitPass = TerrainLodPassId::SplitTopology;

/// <summary>
/// 保存阶段结束后的规范化结果与可逐项比较的叶、队列内容
/// 不持有流水线或线程池引用，完整网格在同一副本中继续生成
/// </summary>
struct StageResult
{
    DataOrientedRoamPassEvidence Evidence;
    std::vector<std::uint64_t> Leaves;
    std::vector<std::pair<std::uint64_t, float>> SplitQueue;
    std::vector<std::pair<std::uint64_t, float>> MergeQueue;
    std::uint64_t MeshHash{0U};
    std::size_t RemainingBudget{0U};
    std::size_t ParallelCommits{0U};
};

StageResult ExecuteVariant(const DataOrientedRoamState& input, TerrainLodPassAction action,
    std::size_t workers, bool diagnostics)
{
    (void)PrepareDataOrientedRoamPassWorkers(input, SplitPass, workers);
    DataOrientedRoamState state{input};
    state.Stats = {};
    ConfigureDataOrientedRoamPassAction(state, SplitPass, action, workers);
    state.Settings.EnablePassEvidence = state.Settings.EnableTopologyValidation = diagnostics;
    state.Settings.EnableTopologyPairEvidence = false;
    Require(HashDataOrientedRoamPassInput(state, SplitPass) == HashDataOrientedRoamPassInput(input, SplitPass),
        "execution setup changed the frozen input");
    ExecuteDataOrientedRoamPass(state, SplitPass);
    StageResult result;
    result.ParallelCommits = state.Stats.ParallelSplitCommitCount;
    result.RemainingBudget = state.RemainingSerialSplitBudget;
    result.Evidence = CaptureDataOrientedRoamPassEvidence(state, SplitPass);
    Require(result.Evidence.Correct, "invalid topology, queue, budget or mesh-edit sequence");
    for (const auto node : state.ActiveLeafNodes) result.Leaves.push_back(state.Nodes.PathIdAt(node));
    for (const auto& entry : state.SplitQueue) result.SplitQueue.emplace_back(state.Nodes.PathIdAt(entry.Node), entry.Score);
    for (const auto& entry : state.MergeQueue) result.MergeQueue.emplace_back(state.Nodes.PathIdAt(entry.Node), entry.Score);
    std::sort(result.Leaves.begin(), result.Leaves.end());
    std::sort(result.SplitQueue.begin(), result.SplitQueue.end());
    std::sort(result.MergeQueue.begin(), result.MergeQueue.end());
    // 阶段哈希之外继续消费真实修改序列，检查全部有效网格属性
    ConfigureDataOrientedRoamPassAction(state, TerrainLodPassId::MeshEmit, TerrainLodPassAction::SerialFull, 1U);
    ExecuteDataOrientedRoamPass(state, TerrainLodPassId::MeshEmit);
    const auto mesh = CaptureDataOrientedRoamPassEvidence(state, TerrainLodPassId::MeshEmit);
    Require(mesh.Correct, "invalid final full mesh");
    result.MeshHash = mesh.ResultHash;
    return result;
}

void RequireEquivalent(const StageResult& serial, const StageResult& parallel, const std::string& label)
{
    Require(serial.Evidence.ResultHash == parallel.Evidence.ResultHash && serial.MeshHash == parallel.MeshHash &&
        serial.Leaves == parallel.Leaves && serial.SplitQueue == parallel.SplitQueue &&
        serial.MergeQueue == parallel.MergeQueue && serial.RemainingBudget == parallel.RemainingBudget,
        label + ": serial/parallel topology or successor state differs");
}

void CheckAnchor(const DataOrientedRoamState& input)
{
    const StateSnapshot before{input};
    Require(HashDataOrientedRoamPassInput(input, SplitPass) == 4677929227313376363ULL &&
        input.ActiveLeafNodes.size() == 19990U && input.RemainingSerialSplitBudget == 10U &&
        input.IncrementalMesh.Metadata.TopologyEdits.size() == 10U, "original sample 14 identity changed");
    const auto plan = PlanDataOrientedRoamSplitTopology(input);
    Require(plan.InteriorCandidateCount == 2U && plan.ScheduledCandidateCount == 0U &&
        plan.NonEmptyChunkCount == 0U && plan.InteriorCandidateCount + plan.BoundaryCandidateCount == plan.Candidates.size(),
        "sample 14 must classify the safe suffix but schedule an empty prefix");
    const auto serial = ExecuteVariant(input, TerrainLodPassAction::SerialImmediate, 1U, false);
    Require(serial.Evidence.ResultHash == 1100364126311268133ULL && serial.MeshHash == 159806467330165317ULL,
        "original serial reference changed");
    for (const bool diagnostics : {false, true})
        for (const std::size_t workers : {1U, 2U, 8U})
        {
            const auto parallel = ExecuteVariant(input, TerrainLodPassAction::ParallelAssisted, workers, diagnostics);
            RequireEquivalent(serial, parallel, "sample 14");
            Require(parallel.ParallelCommits == 0U, "empty prefix cannot report parallel commits");
        }
    for (const std::size_t repeats : {1U, 6U})
    {
        DataOrientedRoamPassExperimentConfig config;
        config.Mode = DataOrientedRoamPassExperimentMode::PilotMeasurement;
        config.WarmupCount = repeats == 1U ? 0U : 1U;
        config.MeasuredRepeatCount = repeats;
        config.ParallelWorkerCount = 8U;
        const auto paired = RunFrozenDataOrientedRoamPassExperiment(input, SplitPass, config);
        Require(paired.Passed && paired.Samples.size() == repeats * 2U, paired.FailureMessage);
        for (const auto& sample : paired.Samples)
            Require(sample.EffectiveWorkerCount == 1U && sample.EarlyCommitCount == 0U,
                "sample 14 fallback must remain visible in the public pairing result");
    }
    Require(before == StateSnapshot{input}, "anchor regression changed its source");
    std::cout << "sampleIndex=14 input=4677929227313376363 result=1100364126311268133 mesh=159806467330165317 prefix=0\n";
}

void CheckControlledNonemptyPrefix(const DataOrientedRoamState& source)
{
    DataOrientedRoamState input{source};
    const auto original = PlanDataOrientedRoamSplitTopology(input);
    std::vector<DataOrientedRoamNodeIndex> promoted;
    for (const auto& candidate : original.Candidates)
        if (SafeInteriorSplitChunkId(input, candidate.Node) != InvalidDataOrientedRoamChunkId)
            promoted.push_back(candidate.Node);
    Require(promoted.size() == 2U, "the controlled fixture needs both existing safe nodes");
    // 只改变两个既有安全项的冻结评分，构造可交换提交的正例；此夹具不进入研究数据
    const float highest = original.Candidates.front().Score + 2.0F;
    for (auto& entry : input.SplitQueue)
    {
        if (entry.Node == promoted[0]) entry.Score = highest;
        if (entry.Node == promoted[1]) entry.Score = highest - 1.0F;
        if (input.Settings.MirrorSplitScoresToNodePool) input.Nodes.ScreenErrors[entry.Node] = entry.Score;
    }
    std::sort(input.SplitQueue.begin(), input.SplitQueue.end(), [&input](const auto& left, const auto& right) {
        return SplitPriorityPrecedes(input, left.Node, left.Score, right.Node, right.Score);
    });
    for (std::size_t index = 0U; index < input.SplitQueue.size(); ++index)
        input.NodeMembership[input.SplitQueue[index].Node].SplitQueuePosition = static_cast<DataOrientedRoamPosition>(index);
    input.Settings.PassPolicy.SplitTopologyMinParallelCandidateCount = 0U;
    Require(CountPersistentQueueInvariantViolations(input) == 0U, "controlled priority fixture must retain valid queues");
    const auto plan = PlanDataOrientedRoamSplitTopology(input);
    Require(plan.ScheduledCandidateCount == 2U && plan.NonEmptyChunkCount == 2U, "nonempty prefix spans two real chunks");
    const auto serial = ExecuteVariant(input, TerrainLodPassAction::SerialImmediate, 1U, false);
    for (const std::size_t workers : {2U, 8U})
    {
        const auto parallel = ExecuteVariant(input, TerrainLodPassAction::ParallelAssisted, workers, false);
        RequireEquivalent(serial, parallel, "controlled nonempty prefix");
        Require(parallel.ParallelCommits == 2U, "the nonempty fixture must perform real early commits");
        DataOrientedRoamPassExperimentConfig config;
        config.Mode = DataOrientedRoamPassExperimentMode::PilotMeasurement;
        config.WarmupCount = 5U;
        config.MeasuredRepeatCount = 30U;
        config.ParallelWorkerCount = workers;
        const auto paired = RunFrozenDataOrientedRoamPassExperiment(input, SplitPass, config);
        Require(paired.Passed, paired.FailureMessage);
        for (const auto& sample : paired.Samples)
            if (sample.RequestedAction == TerrainLodPassAction::ParallelAssisted)
                Require(sample.ExecutionPath == "thread_pool" && sample.EffectiveWorkerCount > 1U &&
                    sample.EffectiveWorkerCount <= workers && sample.EarlyCommitCount == 2U, "physical parallel prefix");
    }
    // 缺池和单块保留相同真实节点，单独检查回退原因与正确性
    DataOrientedRoamPassExperimentConfig config;
    config.Mode = DataOrientedRoamPassExperimentMode::PilotMeasurement;
    config.ParallelWorkerCount = 2U;
    input.ThreadPool = nullptr;
    const auto noPool = MeasureDataOrientedRoamPass(input, SplitPass, TerrainLodPassAction::ParallelAssisted, config);
    Require(noPool.Correct && noPool.EffectiveWorkerCount == 1U && noPool.FallbackDetail == "missing_thread_pool",
        "missing pool cannot claim physical parallelism");
    input.ThreadPool = source.ThreadPool;
    input.Settings.TriangleBudget = input.ActiveLeafNodes.size() + 1U;
    input.RemainingSerialSplitBudget = 1U;
    input.RemainingParallelSplitBudget.store(1U);
    const auto single = MeasureDataOrientedRoamPass(input, SplitPass, TerrainLodPassAction::ParallelAssisted, config);
    Require(single.Correct && single.NonEmptyChunkCount == 1U && single.EffectiveWorkerCount == 1U &&
        single.FallbackDetail == "insufficient_safe_chunks", "one chunk must retain serial fallback");
    std::cout << "controlled nonempty prefix: two real chunks, P=2/8, W=5/R=30\n";
}

void CheckFollowingFrames(const FormalScenario& scenario, const std::vector<CameraSample>& cameras,
    const Terrain::HeightMap& height, std::size_t workers, bool alternating)
{
    auto serialSettings = Benchmark::Formal::MakeCpuSourceSettings(scenario);
    auto parallelSettings = serialSettings;
    DataOrientedRoamPipeline serial;
    DataOrientedRoamPipeline parallel;
    for (std::size_t index = 0U; index < cameras.size(); ++index)
    {
        const auto selected = alternating && index >= 14U && index % 2U != 0U ? (index + 1U) % cameras.size() : index;
        const auto view = BuildCameraView(cameras[selected]);
        if (index == 14U)
        {
            parallelSettings.PassPolicy.SplitTopology = TerrainLodTopologyAction::ParallelAssisted;
            parallelSettings.PassPolicy.SplitTopologyWorkerCount = workers;
        }
        (void)serial.Build(height, scenario.Settings.TerrainSize, scenario.Settings.HeightScale, view, serialSettings);
        (void)parallel.Build(height, scenario.Settings.TerrainSize, scenario.Settings.HeightScale, view, parallelSettings);
        Require(serial.Stats().TopologyHash == parallel.Stats().TopologyHash &&
            serial.Stats().NormalizedMeshHash == parallel.Stats().NormalizedMeshHash &&
            CountPersistentQueueInvariantViolations(serial.State()) == 0U &&
            CountPersistentQueueInvariantViolations(parallel.State()) == 0U,
            "following-frame equivalence failed at frame " + std::to_string(index));
        if (index >= 14U)
        {
            DataOrientedRoamState serialCopy{serial.State()};
            DataOrientedRoamState parallelCopy{parallel.State()};
            const auto serialMesh = CaptureDataOrientedRoamPassEvidence(serialCopy, TerrainLodPassId::MeshEmit);
            const auto parallelMesh = CaptureDataOrientedRoamPassEvidence(parallelCopy, TerrainLodPassId::MeshEmit);
            Require(serialMesh.Correct && parallelMesh.Correct && serialMesh.ResultHash == parallelMesh.ResultHash,
                "following-frame full attributes differ at frame " + std::to_string(index));
        }
    }
    std::cout << "following frames: P=" << workers << " alternating=" << alternating << '\n';
}

void CheckScenarioInputs(const std::filesystem::path& targetPath, bool scanPrefixes)
{
    const auto scenarios = LoadScenarioManifest("docs/parallel-roam/cpu-pilot-scenarios-v1.csv", std::filesystem::current_path());
    const auto cameras = GenerateCameraSamples(scenarios);
    const auto targets = scanPrefixes ? std::vector<TargetStateRef>{} : LoadTargetManifest(targetPath, scenarios, cameras);
    std::size_t checked = 0U;
    for (const auto& scenario : scenarios)
    {
        Terrain::HeightMap height;
        std::string error;
        Require(height.LoadFromFile(scenario.HeightMapPath, &error), error);
        DataOrientedRoamPipeline pipeline;
        const auto settings = Benchmark::Formal::MakeCpuSourceSettings(scenario);
        for (const auto& camera : cameras)
        {
            if (camera.ScenarioId != scenario.ScenarioId) continue;
            (void)pipeline.BuildWithPassObserver(height, scenario.Settings.TerrainSize, scenario.Settings.HeightScale,
                BuildCameraView(camera), settings, [&](const auto& input, auto pass) {
                    if (scanPrefixes)
                    {
                        if (pass != SplitPass) return;
                        const auto plan = PlanDataOrientedRoamSplitTopology(input);
                        if (plan.NonEmptyChunkCount < 2U) return;
                        // 自然来源的多块前缀独立接受差分检验，不依据耗时选择输入
                        DataOrientedRoamPassExperimentConfig config;
                        config.Mode = DataOrientedRoamPassExperimentMode::PilotMeasurement;
                        config.WarmupCount = 0U;
                        config.MeasuredRepeatCount = 1U;
                        const auto result = RunFrozenDataOrientedRoamPassExperiment(input, pass, config);
                        std::cout << scenario.ScenarioId << " sample=" << camera.SampleIndex
                            << " input=" << HashDataOrientedRoamPassInput(input, pass)
                            << " scheduled=" << plan.ScheduledCandidateCount << " chunks=" << plan.NonEmptyChunkCount
                            << " passed=" << result.Passed << ' ' << result.FailureMessage << std::endl;
                        for (const auto& sample : result.Samples)
                            std::cout << "  action=" << ToString(sample.RequestedAction) << " result=" << sample.ResultHash
                                << " early=" << sample.EarlyCommitCount << " workers=" << sample.EffectiveWorkerCount << std::endl;
                        Require(result.Passed, "natural nonempty prefix is a new counterexample");
                        ++checked;
                        return;
                    }
                    for (const auto& target : targets)
                    {
                        if (target.ScenarioId != scenario.ScenarioId || target.SampleIndex != camera.SampleIndex || target.PassId != pass) continue;
                        Require(HashDataOrientedRoamPassInput(input, pass) == target.ReplayInputHash, "old target input changed");
                        DataOrientedRoamPassExperimentConfig config;
                        config.Mode = DataOrientedRoamPassExperimentMode::PilotMeasurement;
                        config.WarmupCount = 0U;
                        config.MeasuredRepeatCount = 1U;
                        const auto result = RunFrozenDataOrientedRoamPassExperiment(input, pass, config);
                        std::cout << scenario.ScenarioId << " sample=" << camera.SampleIndex << " pass=" << ToString(pass)
                            << " passed=" << result.Passed << ' ' << result.FailureMessage << std::endl;
                        Require(result.Passed, "old target core equivalence failed");
                        ++checked;
                    }
                });
        }
    }
    if (!scanPrefixes) Require(checked == targets.size(), "old target coverage incomplete");
    std::cout << (scanPrefixes ? "natural multichunk prefixes=" : "old frozen targets=") << checked << '\n';
}
}

int main(int argc, char** argv)
{
    try
    {
        if (argc == 3 && std::string{argv[1]} == "--targets")
        {
            CheckScenarioInputs(argv[2], false);
            return 0;
        }
        if (argc == 2 && std::string{argv[1]} == "--scan-prefixes")
        {
            CheckScenarioInputs({}, true);
            return 0;
        }
        const auto scenarios = LoadScenarioManifest("docs/parallel-roam/cpu-pilot-scenarios-v1.csv",
            std::filesystem::current_path(), {"peking547-a-b20000"});
        const auto cameras = GenerateCameraSamples(scenarios);
        const auto& scenario = scenarios.front();
        Terrain::HeightMap height;
        std::string error;
        Require(height.LoadFromFile(scenario.HeightMapPath, &error), error);
        DataOrientedRoamPipeline pipeline;
        const auto settings = Benchmark::Formal::MakeCpuSourceSettings(scenario);
        for (std::size_t frame = 0U; frame <= 14U; ++frame)
            (void)pipeline.BuildWithPassObserver(height, scenario.Settings.TerrainSize, scenario.Settings.HeightScale,
                BuildCameraView(cameras[frame]), settings, [&](const auto& input, auto pass) {
                    if (frame != 14U || pass != SplitPass) return;
                    CheckAnchor(input);
                    CheckControlledNonemptyPrefix(input);
                });
        CheckFollowingFrames(scenario, cameras, height, 2U, false);
        CheckFollowingFrames(scenario, cameras, height, 8U, true);
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
