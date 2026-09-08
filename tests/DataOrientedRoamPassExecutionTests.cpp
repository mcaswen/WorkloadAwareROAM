#include "DataOrientedRoamExperimentTestSupport.h"
#include "algorithms/data_oriented_roam/DataOrientedRoamPipeline.h"
#include "algorithms/data_oriented_roam/DataOrientedRoamPassInput.h"
#include "algorithms/data_oriented_roam/DataOrientedRoamQueues.h"
#include "algorithms/data_oriented_roam/DataOrientedRoamWorkloadProbe.h"
#include "experiment/formal/FormalExperimentCamera.h"
#include "experiment/formal/FormalExperimentManifest.h"
#include "experiment/formal/FormalExperimentTargetSelector.h"
#include "benchmark/formal/FormalWorkloadDiscovery.h"

#include <iostream>

using namespace ParallelRoam;
using namespace Tests;

namespace
{
void CheckMidTrajectoryFailure()
{
    using namespace Experiment::Formal;
    const auto scenarios = LoadScenarioManifest("docs/parallel-roam/cpu-pilot-scenarios-v1.csv",
        std::filesystem::current_path(), {"test129-a-b512"});
    auto cameras = GenerateCameraSamples(scenarios);
    ++cameras[10].ViewInputHash;
    std::size_t written = 0U;
    const auto result = Benchmark::Formal::DiscoverCpuScenarioWorkloads(scenarios[0], cameras, {}, [&](const auto& record) {
        Require(record.SampleIndex < 10U && record.ValidationPerformed, "only completed frames published");
        ++written;
    });
    Require(!result.Complete && result.Targets.empty() && result.RecordCount == 50U && written == 50U &&
        !result.Failure.empty(), "mid-trajectory failure preserves evidence without targets");
}

void CheckFrozenTargets(const char* scenarioPath, const char* cameraPath, const char* targetPath)
{
    using namespace Experiment::Formal;
    const auto scenarios = LoadScenarioManifest(scenarioPath, std::filesystem::current_path());
    const auto cameras = LoadCameraManifest(cameraPath, scenarios);
    const auto targets = LoadTargetManifest(targetPath, scenarios, cameras);
    std::size_t verified = 0U;
    for (const auto& scenario : scenarios)
    {
        Terrain::HeightMap height;
        std::string error;
        Require(height.LoadFromFile(scenario.HeightMapPath, &error), error);
        DataOrientedRoamSettings settings;
        settings.MaxDepth = scenario.Settings.MaxDepth;
        settings.TriangleBudget = scenario.Settings.TriangleBudget;
        settings.SplitThreshold = scenario.Settings.ScreenSpaceSplitThresholdPixels;
        settings.MergeThreshold = scenario.Settings.ScreenSpaceMergeThresholdPixels;
        settings.EnableLocalConstraints = scenario.Settings.EnableLocalConstraints;
        settings.PassPolicy = scenario.Settings.PassPolicy;
        settings.EnablePassEvidence = settings.EnableTopologyValidation = true;
        DataOrientedRoamPipeline pipeline;
        for (const auto& camera : cameras)
        {
            if (camera.ScenarioId != scenario.ScenarioId) continue;
            (void)pipeline.BuildWithPassObserver(height, scenario.Settings.TerrainSize, scenario.Settings.HeightScale,
                BuildCameraView(camera), settings, [&](const auto& state, auto pass) {
                    for (const auto& target : targets)
                    {
                        if (target.ScenarioId != scenario.ScenarioId || target.SampleIndex != camera.SampleIndex ||
                            target.PassId != pass) continue;
                        const auto work = ProbeDataOrientedRoamPassWorkload(state, pass);
                        Require(HashDataOrientedRoamPassInput(state, pass) == target.ReplayInputHash,
                            "independently rebuilt target input mismatch");
                        Require(work.PrimaryWorkValue == target.PrimaryWorkValue &&
                            FormatTargetSelectionFeatures(work.AuxiliaryFeatures) == target.FeatureVector &&
                            HashTargetSelectionFeatures({scenario.ScenarioId, pass, camera.SampleIndex,
                                work.PrimaryWorkValue, work.AuxiliaryFeatures}) == target.SelectionFeatureHash,
                            "independently rebuilt target features mismatch");
                        ++verified;
                    }
                });
        }
    }
    Require(verified == targets.size(), "every published target rebuilt");
    std::cout << "Independently rebuilt " << verified << " targets from roots\n";
}
}

int main(int argc, char** argv)
{
    try
    {
        if (argc == 4)
        {
            CheckFrozenTargets(argv[1], argv[2], argv[3]);
            return 0;
        }
        CheckMidTrajectoryFailure();
        Terrain::HeightMap height;
        std::string loadError;
        Require(height.LoadFromFile("assets/heightmaps/Hm_Terrain_Test_129.pgm", &loadError), loadError);
        constexpr TerrainLodPassId order[]{TerrainLodPassId::MergeScore, TerrainLodPassId::MergeTopology,
            TerrainLodPassId::SplitScore, TerrainLodPassId::SplitTopology, TerrainLodPassId::MeshEmit};
        for (const auto policy : {MakeTerrainLodSerialIncrementalPolicy(),
                MakeTerrainLodMaximumSafeParallelIncrementalPolicy()})
        {
            DataOrientedRoamSettings settings;
            settings.MaxDepth = 9;
            settings.TriangleBudget = 512U;
            settings.PassPolicy = policy;
            settings.EnablePassEvidence = settings.EnableTopologyValidation = true;
            DataOrientedRoamPipeline observed;
            DataOrientedRoamPipeline ordinary;
            for (std::size_t frame = 0; frame < 12U; ++frame)
            {
                const auto view = ExperimentView(frame);
                // 上一帧副本沿公共入口推进以独立核对每个真实输入边界
                DataOrientedRoamState replay{observed.State()};
                Require(PrepareDataOrientedRoamFrame(replay, height, 32.0F, 8.0F, view, settings), "prepare");
                std::size_t index = 0;
                (void)observed.BuildWithPassObserver(height, 32.0F, 8.0F, view, settings,
                    [&](const auto& state, auto pass) {
                        Require(index < 5U && order[index++] == pass, "physical pass order");
                        Require(HashDataOrientedRoamPassInput(state, pass) ==
                            HashDataOrientedRoamPassInput(replay, pass), "replay boundary identity");
                        if (pass == TerrainLodPassId::MergeTopology)
                            Require(state.Stats.MergeScoreEntryCount == state.MergeQueue.size(), "merge scored once");
                        if (pass == TerrainLodPassId::SplitTopology)
                            Require(state.Stats.SplitScoreEntryCount == state.SplitQueue.size(), "split scored once");
                        ExecuteDataOrientedRoamPass(replay, pass);
                    });
                (void)ordinary.Build(height, 32.0F, 8.0F, view, settings);
                Require(index == 5U, "five callbacks");
                Require(observed.Stats().TopologyHash == ordinary.Stats().TopologyHash &&
                    observed.Stats().NormalizedMeshHash == ordinary.Stats().NormalizedMeshHash &&
                    observed.Stats().MeshHash == ordinary.Stats().MeshHash, "observer leaves production unchanged");
                Require(CountPersistentQueueInvariantViolations(observed.State()) == 0U, "queue invariants");
            }
        }
        std::cout << "Shared five-pass boundaries: 24 frames verified\n";
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
