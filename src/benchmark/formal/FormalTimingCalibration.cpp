#include "benchmark/formal/FormalTimingCalibration.h"

#include "algorithms/data_oriented_roam/DataOrientedRoamPassMeasurement.h"
#include "algorithms/data_oriented_roam/DataOrientedRoamPipeline.h"
#include "benchmark/formal/FormalCpuInput.h"
#include "experiment/formal/FormalExperimentCamera.h"
#include "tools/PerformanceTimer.h"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace ParallelRoam::Benchmark::Formal
{
namespace
{
using namespace Algorithms;
using namespace Algorithms::DataOrientedRoam;
using namespace Experiment::Formal;

double Quantile(const std::vector<double>& ordered, double fraction)
{
    const auto index = static_cast<std::size_t>(std::ceil(fraction * ordered.size())) - 1U;
    return ordered.at(index);
}

double FinishBatch(std::vector<CpuTimingCalibrationRecord>& records, std::size_t begin)
{
    std::vector<double> values;
    for (std::size_t index = begin; index < records.size(); ++index)
    {
        const auto& row = records[index];
        if (!row.Correct || !std::isfinite(row.WallMilliseconds) || row.WallMilliseconds < 0.0)
            throw std::runtime_error{"Calibration execution failed"};
        values.push_back(row.WallMilliseconds);
    }
    if (values.size() != CpuTimingCalibrationSampleCount)
        throw std::runtime_error{"Incomplete calibration batch"};
    std::sort(values.begin(), values.end());
    for (std::size_t index = begin; index < records.size(); ++index)
    {
        records[index].P50Milliseconds = Quantile(values, 0.50);
        records[index].P95Milliseconds = Quantile(values, 0.95);
        records[index].P99Milliseconds = Quantile(values, 0.99);
    }
    return Quantile(values, 0.99);
}
}

FormalTimingCalibrationResult CalibrateCpuPassTiming(const FormalScenario& scenario,
    const CameraSample& rootCamera, const std::string& runId, const std::string& position)
{
    FormalTimingCalibrationResult result;
    try
    {
        if (rootCamera.SampleIndex != 0U || rootCamera.ScenarioId != scenario.ScenarioId || runId.empty() ||
            (position != "before" && position != "after"))
            throw std::runtime_error{"Calibration requires the first selected scene's frozen root camera"};
        ValidateCameraSample(rootCamera, scenario);
        for (std::uint32_t index = 0U; index < CpuTimingCalibrationSampleCount; ++index)
        {
            // 只测时钟本身，记录构造与容器追加位于停表之后
            Tools::PerformanceTimer timer;
            const auto wall = timer.Stop();
            CpuTimingCalibrationRecord row;
            row.RunId = runId;
            row.Position = position;
            row.Kind = "empty";
            row.ScenarioId = scenario.ScenarioId;
            row.Index = index;
            row.WallMilliseconds = wall;
            row.ClockResolutionNanoseconds = 1.0e9 * Tools::PerformanceTimer::Clock::period::num /
                Tools::PerformanceTimer::Clock::period::den;
            row.Correct = true;
            result.Records.push_back(std::move(row));
        }
        result.EmptyP99Milliseconds = FinishBatch(result.Records, 0U);
        Terrain::HeightMap height;
        std::string error;
        if (!height.LoadFromFile(scenario.HeightMapPath, &error)) throw std::runtime_error{error};
        const auto settings = MakeCpuSourceSettings(scenario);
        DataOrientedRoamPipeline pipeline;
        const auto begin = result.Records.size();
        (void)pipeline.BuildWithPassObserver(height, scenario.Settings.TerrainSize, scenario.Settings.HeightScale,
            BuildCameraView(rootCamera), settings, [&](const auto& state, auto pass) {
                if (pass != TerrainLodPassId::MergeScore) return;
                DataOrientedRoamPassExperimentConfig config;
                config.Mode = DataOrientedRoamPassExperimentMode::PilotMeasurement;
                config.ParallelWorkerCount = 1U;
                for (std::uint32_t index = 0U; index < CpuTimingCalibrationSampleCount; ++index)
                {
                    const auto sample = MeasureDataOrientedRoamPass(state, pass,
                        TerrainLodPassAction::SerialFullRefresh, config);
                    CpuTimingCalibrationRecord row;
                    row.RunId = runId;
                    row.Position = position;
                    row.Kind = "no_work";
                    row.ScenarioId = scenario.ScenarioId;
                    row.Index = index;
                    row.WallMilliseconds = sample.WallMilliseconds;
                    row.ClockResolutionNanoseconds = 1.0e9 * Tools::PerformanceTimer::Clock::period::num /
                        Tools::PerformanceTimer::Clock::period::den;
                    row.ReplayInputHash = sample.StageInputHash;
                    row.ResultHash = sample.ResultHash;
                    row.ValidationPerformed = sample.ValidationPerformed;
                    row.DiagnosticsDisabledAtExecution = sample.DiagnosticsDisabledAtExecution;
                    row.Correct = sample.Correct && sample.ValidationPerformed && sample.DiagnosticsDisabledAtExecution &&
                        sample.CandidateCount == 0U && sample.EffectiveWorkerCount == 0U &&
                        sample.FallbackReason == TerrainLodPassFallbackReason::NoWork;
                    result.Records.push_back(std::move(row));
                }
            });
        if (!IsCpuSourceFrameValid(pipeline.Stats(), settings, 0U))
            throw std::runtime_error{"Calibration source frame validation failed"};
        result.NoWorkP99Milliseconds = FinishBatch(result.Records, begin);
        result.EnvironmentValid = result.EmptyP99Milliseconds <= 0.01;
        result.Complete = true;
    }
    catch (const std::exception& error) { result.Failure = error.what(); }
    return result;
}
}
