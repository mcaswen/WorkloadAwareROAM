#include "DataOrientedRoamExperimentTestSupport.h"
#include "algorithms/data_oriented_roam/DataOrientedRoamPassEvidence.h"
#include "algorithms/data_oriented_roam/DataOrientedRoamPassExperiment.h"
#include "algorithms/data_oriented_roam/DataOrientedRoamPassInput.h"
#include "algorithms/data_oriented_roam/DataOrientedRoamPassMeasurement.h"
#include "algorithms/data_oriented_roam/DataOrientedRoamPipeline.h"
#include "algorithms/data_oriented_roam/DataOrientedRoamThreadPool.h"
#include "algorithms/data_oriented_roam/DataOrientedRoamWorkloadProbe.h"

#include <array>
#include <barrier>
#include <cmath>
#include <iostream>
#include <limits>
#include <memory>
#include <mutex>
#include <set>
#include <thread>
#include <vector>

using namespace ParallelRoam;
using namespace Tests;

namespace
{
// 时钟替身只验证边界顺序，不依赖本机调度或微秒时间断言
std::vector<std::string> MeasurementEvents;

struct SequenceTimer
{
    SequenceTimer() { MeasurementEvents.push_back("start"); }
    float Stop() { MeasurementEvents.push_back("stop"); return 7.0F; }
};

void CheckMeasurementSequence()
{
    MeasurementEvents.clear();
    const auto result = Algorithms::DataOrientedRoam::Detail::MeasurePreparedCpuPass<SequenceTimer>(
        [] {
            MeasurementEvents.push_back("clone");
            auto state = std::make_unique<int>(2);
            MeasurementEvents.push_back("prepare");
            return state;
        },
        [](int& state) { MeasurementEvents.push_back("execute"); state += 3; },
        [](int& state, float wall) {
            MeasurementEvents.push_back("evidence");
            Require(state == 5 && wall == 7.0F, "capture sees completed execution and stopped interval");
            return state;
        });
    Require(result == 5 && MeasurementEvents ==
        std::vector<std::string>{"clone", "prepare", "start", "execute", "stop", "evidence"},
        "clone, preparation and evidence must stay outside execution interval");
}

void CheckActualThreads()
{
    DataOrientedRoamThreadPool pool;
    pool.EnsureWorkerCount(2U);
    std::barrier arrival{2};
    std::mutex mutex;
    std::set<std::thread::id> observed;
    const auto caller = std::this_thread::get_id();
    pool.ParallelFor(2U, [&](std::size_t) {
        {
            std::scoped_lock lock{mutex};
            observed.insert(std::this_thread::get_id());
        }
        // 两个任务同时到达才能返回，单个线程顺序模拟任务不能通过
        arrival.arrive_and_wait();
    });
    Require(observed.size() == 2U && !observed.contains(caller), "real thread-pool dispatch");
}

DataOrientedRoamPassExperimentConfig PilotConfig()
{
    DataOrientedRoamPassExperimentConfig config;
    config.WarmupCount = 0U;
    config.MeasuredRepeatCount = 1U;
    config.ParallelWorkerCount = 2U;
    config.Mode = DataOrientedRoamPassExperimentMode::PilotMeasurement;
    return config;
}

void CheckOrders(const DataOrientedRoamState& input)
{
    const std::array<const char*, 6> orders{"ABC", "ACB", "BAC", "BCA", "CAB", "CBA"};
    for (const std::size_t warmups : {0U, 1U, 5U})
        for (const std::size_t repeats : {1U, 2U, 6U, 30U})
        {
            auto config = PilotConfig();
            config.WarmupCount = warmups;
            config.MeasuredRepeatCount = repeats;
            const auto result = RunFrozenDataOrientedRoamPassExperiment(input, TerrainLodPassId::MeshEmit, config);
            Require(result.Passed, result.FailureMessage);
            Require(result.Samples.size() == repeats * 3U && result.WarmupSamples.size() == warmups * 3U &&
                result.WarmupExecutionCount == warmups * 3U, "actual warmup and measured counts");
            for (const auto& sample : result.Samples)
                Require(!sample.IsWarmup && sample.AbsoluteBlockIndex == warmups + sample.RepeatIndex &&
                    sample.BlockOrder == orders[sample.AbsoluteBlockIndex % 6U] &&
                    sample.ExecutionOrder < 3U, "absolute six-permutation order after warmups");
            for (const auto& sample : result.WarmupSamples)
                Require(sample.IsWarmup && sample.AbsoluteBlockIndex < warmups &&
                    sample.BlockOrder == orders[sample.AbsoluteBlockIndex % 6U], "warmup evidence retained");
        }
}

void CheckInvalidConfig(const DataOrientedRoamState& input, TerrainLodPassId pass)
{
    auto config = PilotConfig();
    config.MeasuredRepeatCount = 0U;
    Require(!RunFrozenDataOrientedRoamPassExperiment(input, pass, config).Passed, "zero repeats rejected");
    config = PilotConfig();
    config.WarmupCount = std::numeric_limits<std::size_t>::max();
    Require(!RunFrozenDataOrientedRoamPassExperiment(input, pass, config).Passed, "absolute block overflow rejected");
    config = PilotConfig();
    config.Passes.push_back(pass);
    Require(!RunFrozenDataOrientedRoamPassExperiment(input, pass, config).Passed, "duplicate pass rejected");
    config = PilotConfig();
    Require(!RunFrozenDataOrientedRoamPassExperiment(input, TerrainLodPassId::CpuUpload, config).Passed,
        "upload cannot enter CPU experiment");
}

void CheckTwoActionOrders(const DataOrientedRoamState& input)
{
    for (const std::size_t warmups : {0U, 1U, 5U})
        for (const std::size_t repeats : {1U, 2U, 6U, 30U})
        {
            auto config = PilotConfig();
            config.WarmupCount = warmups;
            config.MeasuredRepeatCount = repeats;
            const auto result = RunFrozenDataOrientedRoamPassExperiment(input, TerrainLodPassId::SplitScore, config);
            Require(result.Passed && result.Samples.size() == repeats * 2U &&
                result.WarmupSamples.size() == warmups * 2U, "complete two-action blocks");
            for (const auto* samples : {&result.Samples, &result.WarmupSamples})
                for (const auto& sample : *samples)
                {
                    const auto block = sample.AbsoluteBlockIndex;
                    const auto actionIndex = (sample.ExecutionOrder + block % 2U) % 2U;
                    Require(sample.BlockOrder == (block % 2U == 0U ? "AB" : "BA") &&
                        sample.RequestedAction == (actionIndex == 0U ? TerrainLodPassAction::SerialFullRefresh :
                            TerrainLodPassAction::ParallelFullRefresh), "absolute AB/BA identity");
                }
        }
}

void CheckWrapperSelection(const DataOrientedRoamState& input, const DataOrientedRoamSettings& settings)
{
    const StateSnapshot before{input};
    auto config = PilotConfig();
    const auto all = RunDataOrientedRoamPassExperiment(input, ExperimentView(13U), settings, config);
    Require(all.Passed && all.Samples.size() == 11U, "legacy wrapper uses all five production stages");
    for (const auto pass : config.Passes)
    {
        auto selected = config;
        selected.Passes = {pass};
        const auto filtered = RunDataOrientedRoamPassExperiment(input, ExperimentView(13U), settings, selected);
        Require(filtered.Passed, filtered.FailureMessage);
        for (const auto& sample : filtered.Samples)
        {
            const auto reference = std::find_if(all.Samples.begin(), all.Samples.end(), [&](const auto& other) {
                return other.PassId == pass && other.RequestedAction == sample.RequestedAction;
            });
            Require(reference != all.Samples.end() && sample.StageInputHash == reference->StageInputHash &&
                sample.ResultHash == reference->ResultHash, "selection preserves stage input and output");
        }
    }
    Require(before == StateSnapshot{input}, "wrapper leaves the previous frame intact");
    DataOrientedRoamState budget{input};
    budget.Settings.TriangleBudget = 1U;
    Require(!CaptureDataOrientedRoamPassEvidence(budget, TerrainLodPassId::MergeTopology).Correct,
        "topology budget corruption rejected");
    DataOrientedRoamState neighbor{input};
    neighbor.Nodes.BaseNeighbors[neighbor.ActiveLeafNodes.front()] =
        static_cast<DataOrientedRoamNodeIndex>(neighbor.Nodes.size() + 3U);
    Require(!CaptureDataOrientedRoamPassEvidence(neighbor, TerrainLodPassId::SplitTopology).Correct,
        "invalid neighbor rejected");
}

void CheckScoreVariants(const DataOrientedRoamState& input)
{
    for (const std::size_t workers : {1U, 2U, 8U})
    {
        auto config = PilotConfig();
        config.ParallelWorkerCount = workers;
        const auto result = RunFrozenDataOrientedRoamPassExperiment(input, TerrainLodPassId::SplitScore, config);
        Require(result.Passed, result.FailureMessage);
        const auto& parallel = result.Samples[1];
        Require(parallel.RequestedWorkerCount == workers && parallel.EffectiveWorkerCount <= workers,
            "explicit worker limit must survive measurement");
        if (workers > 1U && input.SplitQueue.size() >= workers)
            Require(parallel.EffectiveWorkerCount == workers && parallel.ExecutionPath == "thread_pool",
                "low-work explicit dispatch uses the available pool");
    }
    auto config = PilotConfig();
    DataOrientedRoamState missingPool{input};
    missingPool.ThreadPool = nullptr;
    const auto sequentialTasks = MeasureDataOrientedRoamPass(missingPool, TerrainLodPassId::SplitScore,
        TerrainLodPassAction::ParallelFullRefresh, config);
    Require(sequentialTasks.EffectiveWorkerCount == 1U && sequentialTasks.ExecutionPath == "caller_thread" &&
        sequentialTasks.FallbackDetail == "missing_thread_pool", "task count is not physical parallelism");
    DataOrientedRoamState diagnostics{input};
    diagnostics.Settings.EnablePassEvidence = diagnostics.Settings.EnableTopologyValidation =
        diagnostics.Settings.EnableTopologyPairEvidence = true;
    DataOrientedRoamState diagnosticsOff{input};
    diagnosticsOff.Settings.EnablePassEvidence = diagnosticsOff.Settings.EnableTopologyValidation =
        diagnosticsOff.Settings.EnableTopologyPairEvidence = false;
    const auto first = MeasureDataOrientedRoamPass(diagnosticsOff, TerrainLodPassId::SplitScore,
        TerrainLodPassAction::SerialFullRefresh, config);
    const auto second = MeasureDataOrientedRoamPass(diagnostics, TerrainLodPassId::SplitScore,
        TerrainLodPassAction::SerialFullRefresh, config);
    Require(first.StageInputHash == second.StageInputHash && first.ResultHash == second.ResultHash &&
        first.DiagnosticsDisabledAtExecution && second.DiagnosticsDisabledAtExecution, "diagnostic isolation");
    DataOrientedRoamState scored{input};
    ConfigureDataOrientedRoamPassAction(scored, TerrainLodPassId::SplitScore, TerrainLodPassAction::SerialFullRefresh, 1U);
    ExecuteDataOrientedRoamPass(scored, TerrainLodPassId::SplitScore);
    const auto scoreHash = CaptureDataOrientedRoamPassEvidence(scored, TerrainLodPassId::SplitScore).ResultHash;
    scored.SplitQueue.front().Score += 0.5F;
    Require(CaptureDataOrientedRoamPassEvidence(scored, TerrainLodPassId::SplitScore).ResultHash != scoreHash,
        "finite per-node score changes must affect equivalence");
    scored.SplitQueue.front().Score = std::numeric_limits<float>::quiet_NaN();
    Require(!CaptureDataOrientedRoamPassEvidence(scored, TerrainLodPassId::SplitScore).Correct,
        "non-finite scores cannot pass evidence");
}

void CheckMeshCorruption(const DataOrientedRoamState& input)
{
    DataOrientedRoamState mesh{input};
    mesh.Stats = {};
    ConfigureDataOrientedRoamPassAction(mesh, TerrainLodPassId::MeshEmit, TerrainLodPassAction::SerialFull, 1U);
    ExecuteDataOrientedRoamPass(mesh, TerrainLodPassId::MeshEmit);
    const auto valid = CaptureDataOrientedRoamPassEvidence(mesh, TerrainLodPassId::MeshEmit);
    Require(valid.Correct, "valid full mesh");
    mesh.IncrementalMesh.Data.Vertices.front().DebugColor.x += 0.5F;
    Require(CaptureDataOrientedRoamPassEvidence(mesh, TerrainLodPassId::MeshEmit).ResultHash != valid.ResultHash,
        "all effective vertex attributes participate in equivalence");
    mesh.IncrementalMesh.Data.Indices[1] = mesh.IncrementalMesh.Data.Indices[0];
    Require(!CaptureDataOrientedRoamPassEvidence(mesh, TerrainLodPassId::MeshEmit).Correct,
        "duplicate triangle index rejected");
}
}

int main()
{
    try
    {
        CheckMeasurementSequence();
        CheckActualThreads();
        Terrain::HeightMap height;
        std::string error;
        Require(height.LoadFromFile("assets/heightmaps/Hm_Terrain_Test_129.pgm", &error), error);
        DataOrientedRoamSettings settings;
        settings.MaxDepth = 12;
        settings.TriangleBudget = 4096U;
        settings.PassPolicy = MakeTerrainLodSerialIncrementalPolicy();
        settings.PassPolicy.MergeScoreMinParallelEntryCount = 0U;
        settings.PassPolicy.SplitScoreMinParallelEntryCount = 0U;
        settings.PassPolicy.MeshEmitMinParallelTriangleCount = 0U;
        settings.PassPolicy.MergeTopologyMinParallelCandidateCount = 0U;
        settings.PassPolicy.SplitTopologyMinParallelCandidateCount = 0U;
        settings.EnablePassEvidence = settings.EnableTopologyValidation = true;
        DataOrientedRoamPipeline pipeline;
        bool checkedScore = false;
        bool checkedMesh = false;
        bool sawPriorEdits = false;
        bool sawSingleChunkFallback = false;
        bool sawMultipleChunkDispatch = false;
        for (std::size_t frame = 0U; frame < 12U; ++frame)
        {
            (void)pipeline.BuildWithPassObserver(height, 32.0F, 8.0F, ExperimentView(frame), settings,
                [&](const auto& input, auto pass) {
                    const StateSnapshot before{input};
                    const auto config = PilotConfig();
                    const auto result = RunFrozenDataOrientedRoamPassExperiment(input, pass, config);
                    Require(result.Passed, std::string{ToString(pass)} + ": " + result.FailureMessage);
                    Require(before == StateSnapshot{input}, "pairing must not mutate source state");
                    Require(result.Samples.size() == (pass == TerrainLodPassId::MeshEmit ? 3U : 2U),
                        "all legal strategies measured");
                    for (const auto& sample : result.Samples)
                    {
                        Require(sample.Correct && sample.Equivalent && sample.ValidationPerformed &&
                            sample.DiagnosticsDisabledAtExecution && std::isfinite(sample.WallMilliseconds) &&
                            sample.WallMilliseconds >= 0.0F, "usable measurement evidence");
                        if (sample.RequestedAction == TerrainLodPassAction::SerialImmediate)
                            Require(sample.CandidateSnapshotMilliseconds == 0.0F &&
                                sample.ChunkBuildMilliseconds == 0.0F, "serial topology does not build a parallel plan");
                    }
                    if (frame == 0U && pass == TerrainLodPassId::MergeScore)
                        Require(result.Samples[0].EffectiveWorkerCount == 0U &&
                            result.Samples[0].FallbackReason == TerrainLodPassFallbackReason::NoWork, "true root no-work phase");
                    if (pass == TerrainLodPassId::SplitTopology && !input.IncrementalMesh.Metadata.TopologyEdits.empty())
                    {
                        sawPriorEdits = true;
                        DataOrientedRoamState invalidEdits{input};
                        invalidEdits.IncrementalMesh.Metadata.TopologyEdits.push_back(
                            invalidEdits.IncrementalMesh.Metadata.TopologyEdits.front());
                        Require(!CaptureDataOrientedRoamPassEvidence(invalidEdits, pass).Correct,
                            "invalid edit sequence cannot be repaired silently during evidence");
                    }
                    if (pass == TerrainLodPassId::MergeTopology || pass == TerrainLodPassId::SplitTopology)
                    {
                        const auto& parallel = result.Samples[1];
                        if (parallel.NonEmptyChunkCount == 1U)
                        {
                            Require(parallel.EffectiveWorkerCount == 1U && parallel.FallbackDetail == "insufficient_safe_chunks",
                                "one safe chunk cannot be reported as parallel dispatch");
                            sawSingleChunkFallback = true;
                        }
                        if (parallel.NonEmptyChunkCount > 1U)
                        {
                            Require(parallel.EffectiveWorkerCount == 2U && parallel.ExecutionPath == "thread_pool",
                                "multiple safe chunks honor the explicit two-worker request");
                            sawMultipleChunkDispatch = true;
                            if (!sawSingleChunkFallback && pass == TerrainLodPassId::SplitTopology)
                            {
                                // 保留真实节点和邻接，只限制提前细分预算，使计划恰好占用一个安全块
                                DataOrientedRoamState oneChunk{input};
                                oneChunk.RemainingSerialSplitBudget = 1U;
                                const auto limited = RunFrozenDataOrientedRoamPassExperiment(oneChunk, pass, config);
                                Require(limited.Passed, limited.FailureMessage);
                                const auto& limitedParallel = limited.Samples[1];
                                Require(limitedParallel.NonEmptyChunkCount == 1U && limitedParallel.EffectiveWorkerCount == 1U &&
                                    limitedParallel.FallbackDetail == "insufficient_safe_chunks", "single safe chunk fallback");
                                sawSingleChunkFallback = true;
                            }
                        }
                    }
                    if (!checkedScore && pass == TerrainLodPassId::SplitScore && input.SplitQueue.size() >= 8U)
                    {
                        CheckScoreVariants(input);
                        CheckTwoActionOrders(input);
                        CheckInvalidConfig(input, pass);
                        checkedScore = true;
                    }
                    if (!checkedMesh && frame > 0U && pass == TerrainLodPassId::MeshEmit)
                    {
                        CheckOrders(input);
                        CheckMeshCorruption(input);
                        checkedMesh = true;
                    }
                });
        }
        Require(checkedScore && checkedMesh && sawPriorEdits, "fixtures cover threads, mesh and preceding merge edits");
        CheckWrapperSelection(pipeline.State(), settings);
        Require(sawSingleChunkFallback, "real stages must cover one safe chunk");
        Require(sawMultipleChunkDispatch, "real stages must cover multiple safe chunks");
        std::cout << "CPU pairing, source isolation, interval boundaries and result evidence verified\n";
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
