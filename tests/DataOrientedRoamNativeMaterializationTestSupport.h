#pragma once

#include "algorithms/data_oriented_roam/DataOrientedRoamPipeline.h"
#include "algorithms/data_oriented_roam/DataOrientedRoamState.h"
#include "algorithms/data_oriented_roam/DataOrientedRoamTopology.h"
#include "algorithms/data_oriented_roam/DataOrientedRoamPassInput.h"
#include "algorithms/data_oriented_roam/DataOrientedRoamTopologyExperiment.h"
#include "experiment/formal/FormalExperimentCamera.h"
#include "experiment/formal/FormalExperimentManifest.h"
#include "tools/PerformanceTimer.h"

#include <cmath>
#include <algorithm>
#include <filesystem>
#include <memory>
#include <stdexcept>

namespace NativeMaterializationTests
{
using namespace ParallelRoam;
namespace Dod = Algorithms::DataOrientedRoam;
namespace Formal = Experiment::Formal;

/// <summary>
/// 外层测试持有来源及其借用资源，恢复副本不进入规划器
/// 字段析构顺序保证状态先于流水线和高度图释放
/// </summary>
struct SourceCase
{
    std::string Name;
    Terrain::HeightMap Height;
    Dod::DataOrientedRoamPipeline Pipeline;
    std::unique_ptr<Dod::DataOrientedRoamState> Initial;
    std::uint64_t InputHash{0};
    double SourceMs{0};
};

inline Formal::CameraSample StressCamera(std::uint32_t index, std::uint32_t count)
{
    Formal::CameraSample camera;
    camera.TrajectoryId = "budget-orbit"; camera.SampleIndex = index;
    const float t = static_cast<float>(index) / static_cast<float>(count - 1);
    const float angle = t * 6.28318530718F;
    camera.Position = {std::cos(angle) * 58.0F, 20.0F + std::sin(angle * 2.0F) * 3.0F,
        std::sin(angle) * 58.0F};
    camera.Target = {std::cos(angle + 0.55F) * 10.0F, 4.0F, std::sin(angle + 0.55F) * 10.0F};
    return camera;
}

inline void DisableDiagnostics(Dod::DataOrientedRoamState& state)
{
    state.Settings.EnableTopologyValidation = false;
    state.Settings.EnablePassEvidence = false;
    state.Settings.EnableTopologyPairEvidence = false;
    state.Settings.MirrorSplitScoresToNodePool = false;
    state.Settings.PassPolicy = Algorithms::MakeTerrainLodSerialIncrementalPolicy();
}

// 固定三项输入；压力案例只借用 Peking 资产参数，不改变轨迹 A 的定义
inline std::unique_ptr<SourceCase> PrepareSource(const std::filesystem::path& root,
    const std::filesystem::path& cameraPath, std::size_t caseIndex)
{
    if (caseIndex > 2) throw std::invalid_argument("未知来源案例");
    const auto scenarios = Formal::LoadScenarioManifest(root / "docs/parallel-roam/cpu-pilot-scenarios-v1.csv",
        root, {"test129-a-b4096", "peking547-a-b20000"});
    const auto cameras = Formal::LoadCameraManifest(cameraPath, scenarios);
    const auto scenarioId = caseIndex == 0 ? "test129-a-b4096" : "peking547-a-b20000";
    const auto foundScenario = std::find_if(scenarios.begin(), scenarios.end(), [&](const auto& candidate) {
        return candidate.ScenarioId == scenarioId;
    });
    if (foundScenario == scenarios.end()) throw std::runtime_error("缺少冻结场景");
    const auto& scenario = *foundScenario;
    auto item = std::make_unique<SourceCase>();
    item->Name = caseIndex == 2 ? "peking547-budget-orbit64-b200000-sample14" : scenario.ScenarioId + "-sample14";
    Tools::PerformanceTimer sourceTimer;
    std::string error;
    if (!item->Height.LoadFromFile(scenario.HeightMapPath, &error)) throw std::runtime_error(error);
    Dod::DataOrientedRoamSettings settings;
    settings.MaxDepth = scenario.Settings.MaxDepth;
    settings.TriangleBudget = caseIndex == 2 ? 200000 : scenario.Settings.TriangleBudget;
    settings.SplitThreshold = scenario.Settings.ScreenSpaceSplitThresholdPixels;
    settings.MergeThreshold = scenario.Settings.ScreenSpaceMergeThresholdPixels;
    settings.EnableLocalConstraints = scenario.Settings.EnableLocalConstraints;
    settings.PassPolicy = Algorithms::MakeTerrainLodSerialIncrementalPolicy();
    settings.EnablePassEvidence = settings.EnableTopologyValidation = true;
    settings.EnableTopologyPairEvidence = false;
    if (caseIndex == 2)
    {
        Dod::DataOrientedRoamPipeline warmup;
        for (std::uint32_t i = 0; i < 4; ++i)
            static_cast<void>(warmup.Build(item->Height, 80.0F, 12.0F,
                Formal::BuildCameraView(StressCamera(i, 4)), settings));
    }
    for (std::uint32_t i = 0; i <= 14; ++i)
    {
        Formal::CameraSample camera;
        if (caseIndex == 2) camera = StressCamera(i, 64);
        else
        {
            const auto found = std::find_if(cameras.begin(), cameras.end(), [&](const auto& sample) {
                return sample.ScenarioId == scenario.ScenarioId && sample.SampleIndex == i;
            });
            if (found == cameras.end()) throw std::runtime_error("冻结相机缺少采样");
            camera = *found;
        }
        static_cast<void>(item->Pipeline.BuildWithPassObserver(item->Height, scenario.Settings.TerrainSize,
            scenario.Settings.HeightScale, Formal::BuildCameraView(camera), settings,
            [&](const auto& state, Algorithms::TerrainLodPassId pass) {
                if (i != 14 || pass != Algorithms::TerrainLodPassId::SplitTopology) return;
                if (item->Initial) throw std::logic_error("重复截取来源");
                item->InputHash = Dod::HashDataOrientedRoamPassInput(state, pass);
                item->Initial = std::make_unique<Dod::DataOrientedRoamState>(state);
            }));
    }
    item->SourceMs = sourceTimer.Stop();
    if (!item->Initial) throw std::runtime_error("未截取来源");
    return item;
}
}
