#include "benchmark/formal/FormalCpuPassBenchmark.h"

#include "algorithms/data_oriented_roam/DataOrientedRoamPassExperiment.h"
#include "algorithms/data_oriented_roam/DataOrientedRoamPassInput.h"
#include "algorithms/data_oriented_roam/DataOrientedRoamPipeline.h"
#include "algorithms/data_oriented_roam/DataOrientedRoamWorkloadProbe.h"
#include "benchmark/formal/FormalCpuInput.h"
#include "experiment/formal/FormalCpuPairCsv.h"
#include "experiment/formal/FormalExperimentCamera.h"
#include "experiment/formal/FormalExperimentTargetSelector.h"
#include "tools/PerformanceTimer.h"

#include <algorithm>
#include <iostream>
#include <map>
#include <stdexcept>

namespace ParallelRoam::Benchmark::Formal
{
namespace
{
using namespace Algorithms;
using namespace Algorithms::DataOrientedRoam;
using namespace Experiment::Formal;

CpuPairRecord MakeSampleRecord(const CpuPairRecord& input, const DataOrientedRoamPassExperimentSample& sample)
{
    auto record = input;
    record.AbsoluteBlockIndex = static_cast<std::uint32_t>(sample.AbsoluteBlockIndex);
    record.RepeatIndex = static_cast<std::uint32_t>(sample.RepeatIndex);
    record.IsWarmup = sample.IsWarmup;
    record.BlockOrder = sample.BlockOrder;
    record.OrderIndex = static_cast<std::uint32_t>(sample.ExecutionOrder);
    record.RequestedAction = sample.RequestedAction;
    record.ActualAction = sample.EffectiveAction;
    record.RequestedWorkerCount = sample.RequestedWorkerCount;
    record.ActualWorkerCount = sample.EffectiveWorkerCount;
    record.Fallback = sample.FallbackReason;
    record.ExecutionPath = sample.ExecutionPath;
    record.FallbackDetail = sample.FallbackDetail;
    record.ResultHash = sample.ResultHash;
    record.ValidationPerformed = sample.ValidationPerformed;
    record.DiagnosticsDisabledAtExecution = sample.DiagnosticsDisabledAtExecution;
    record.Correct = sample.Correct;
    record.Equivalent = sample.Equivalent;
    record.Failure = sample.FailureMessage;
    record.Status = sample.Correct && sample.Equivalent && sample.ValidationPerformed ?
        (sample.FallbackReason == TerrainLodPassFallbackReason::NoWork ? CpuRecordStatus::NoWork : CpuRecordStatus::Valid) :
        CpuRecordStatus::Failed;
    record.WallMilliseconds = sample.WallMilliseconds;
    record.StateCloneMilliseconds = sample.StateCloneMilliseconds;
    record.InputCheckMilliseconds = sample.InputCheckMilliseconds;
    record.ValidationMilliseconds = sample.ValidationMilliseconds;
    record.WorkerPreparationMilliseconds = sample.WorkerPreparationMilliseconds;
    record.PostEarlyCommitCount = sample.EarlyCommitCount;
    record.PostActiveTriangleCount = sample.ActiveTriangleCount;
    record.PostDirtyTriangleCount = sample.DirtyTriangleCount;
    record.PostDirtyRangeCount = sample.DirtyRangeCount;
    record.ScoreMilliseconds = sample.ScoreMilliseconds;
    record.HeapifyMilliseconds = sample.HeapifyMilliseconds;
    record.CandidateSnapshotMilliseconds = sample.CandidateSnapshotMilliseconds;
    record.ChunkBuildMilliseconds = sample.ChunkBuildMilliseconds;
    record.QueueInvalidationMilliseconds = sample.QueueInvalidationMilliseconds;
    record.CommitMilliseconds = sample.CommitMilliseconds;
    record.ResultMergeMilliseconds = sample.ResultMergeMilliseconds;
    record.IndexQueueRefreshMilliseconds = sample.IndexQueueRefreshMilliseconds;
    record.SerialConvergenceMilliseconds = sample.SerialConvergenceMilliseconds;
    if (sample.StageInputHash != input.ReplayInputHash)
    {
        record.Status = CpuRecordStatus::Failed;
        record.Equivalent = false;
        record.Failure = "sample_input_hash_mismatch";
    }
    return record;
}

bool MatchesTarget(const CpuPairRecord& input, const TargetStateRef& target)
{
    return input.ReplayInputHash == target.ReplayInputHash && input.SelectionFeatureHash == target.SelectionFeatureHash &&
        input.CameraPoseHash == target.CameraPoseHash && input.ViewInputHash == target.ViewInputHash &&
        input.PrimaryWorkValue == target.PrimaryWorkValue && input.FeatureVector == target.FeatureVector;
}
}

CpuPairConfiguration MakeCpuPairConfiguration(const FormalScenario& scenario, const CpuPairSelection& selection)
{
    CpuPairConfiguration result;
    result.ManifestWarmupCount = scenario.PassWarmupCount;
    result.ManifestRepeatCount = scenario.PassMeasuredRepeatCount;
    result.ManifestParallelWorkerCount = scenario.ParallelWorkerCount;
    result.WarmupCount = selection.WarmupCount.value_or(scenario.PassWarmupCount);
    result.MeasuredRepeatCount = selection.MeasuredRepeatCount.value_or(scenario.PassMeasuredRepeatCount);
    result.ParallelWorkerCount = selection.ParallelWorkerCount.value_or(scenario.ParallelWorkerCount);
    ValidateCpuPairConfiguration(result);
    return result;
}

FormalCpuPassBenchmarkResult MeasureCpuScenarioTargets(const FormalScenario& scenario,
    const std::vector<CameraSample>& cameras, const std::vector<TargetStateRef>& targets,
    const CpuPairSelection& selection, const std::string& runId,
    const std::function<void(const CpuPairRecord&)>& writeRecord)
{
    FormalCpuPassBenchmarkResult result;
    std::vector<CpuPairRecord> pending;
    std::size_t frameTargetBegin = 0U;
    try
    {
        const auto configuration = MakeCpuPairConfiguration(scenario, selection);
        std::map<std::pair<std::uint32_t, TerrainLodPassId>, const TargetStateRef*> requested;
        std::uint32_t lastSample = 0U;
        for (const auto& target : targets)
        {
            if (target.ScenarioId != scenario.ScenarioId) continue;
            if (target.SampleIndex == 0U || target.SampleIndex >= CpuPilotSampleCount ||
                !requested.emplace(std::pair{target.SampleIndex, target.PassId}, &target).second)
                throw std::runtime_error{"Duplicate or root target requested"};
            lastSample = std::max(lastSample, target.SampleIndex);
        }
        if (requested.empty()) throw std::runtime_error{"Scenario has no selected targets"};
        std::vector<const CameraSample*> ordered;
        for (const auto& camera : cameras)
            if (camera.ScenarioId == scenario.ScenarioId) ordered.push_back(&camera);
        std::sort(ordered.begin(), ordered.end(), [](const auto* left, const auto* right) {
            return left->SampleIndex < right->SampleIndex;
        });
        if (ordered.size() != CpuPilotSampleCount) throw std::runtime_error{"Incomplete frozen camera trajectory"};
        Terrain::HeightMap height;
        std::string error;
        if (!height.LoadFromFile(scenario.HeightMapPath, &error)) throw std::runtime_error{error};
        const auto settings = MakeCpuSourceSettings(scenario);
        DataOrientedRoamPipeline pipeline;
        double rebuildMilliseconds = 0.0;
        for (std::uint32_t index = 0U; index <= lastSample; ++index)
        {
            const auto& camera = *ordered[index];
            if (camera.SampleIndex != index) throw std::runtime_error{"Duplicate or missing frozen camera"};
            ValidateCameraSample(camera, scenario);
            frameTargetBegin = result.Targets.size();
            double observerMilliseconds = 0.0;
            Tools::PerformanceTimer frameTimer;
            (void)pipeline.BuildWithPassObserver(height, scenario.Settings.TerrainSize, scenario.Settings.HeightScale,
                BuildCameraView(camera), settings, [&](const auto& state, auto pass) {
                    const auto found = requested.find({index, pass});
                    if (found == requested.end()) return;
                    const auto& target = *found->second;
                    CpuTargetPairSummary summary;
                    summary.RunId = runId;
                    summary.Target = target;
                    summary.Configuration = configuration;
                    summary.RebuildMilliseconds = rebuildMilliseconds + frameTimer.ElapsedMilliseconds() - observerMilliseconds;
                    result.Targets.push_back(std::move(summary));
                    auto& current = result.Targets.back();
                    Tools::PerformanceTimer observerTimer;
                    // 先核对完整输入与获准特征，匹配失败时不执行其他目标替代它
                    const auto work = ProbeDataOrientedRoamPassWorkload(state, pass);
                    const auto inputHash = HashDataOrientedRoamPassInput(state, pass);
                    auto input = MakeCpuPairInputRecord(camera, pass, work, inputHash);
                    if (!MatchesTarget(input, target)) throw std::runtime_error{"Frozen target identity mismatch"};
                    input.RunId = runId;
                    input.TerrainId = scenario.TerrainId;
                    input.AnalysisSplit = target.AnalysisSplit;
                    input.SelectionRank = target.SelectionRank;
                    input.SelectionStratum = target.SelectionStratum;
                    input.SelectionSeed = target.SelectionSeed;
                    input.SelectorVersion = CpuTargetSelectorVersion;
                    input.Configuration = configuration;
                    DataOrientedRoamPassExperimentConfig config;
                    config.Mode = DataOrientedRoamPassExperimentMode::PilotMeasurement;
                    config.WarmupCount = configuration.WarmupCount;
                    config.MeasuredRepeatCount = configuration.MeasuredRepeatCount;
                    config.ParallelWorkerCount = configuration.ParallelWorkerCount;
                    config.Passes = {pass};
                    const auto measured = RunFrozenDataOrientedRoamPassExperiment(state, pass, config);
                    current.WorkerPreparationMilliseconds = measured.WorkerPreparationMilliseconds;
                    std::vector<CpuPairRecord> rows;
                    for (const auto* samples : {&measured.WarmupSamples, &measured.Samples})
                        for (const auto& sample : *samples) rows.push_back(MakeSampleRecord(input, sample));
                    current.WarmupRowCount = measured.WarmupSamples.size();
                    current.MeasuredRowCount = measured.Samples.size();
                    // 先保留原始结果，失败后由外层将整帧尚未确认的记录标记为失败
                    pending.insert(pending.end(), rows.begin(), rows.end());
                    if (!measured.Passed) throw std::runtime_error{measured.FailureMessage};
                    ValidateCpuPairTarget(rows, target, runId, configuration);
                    observerMilliseconds += observerTimer.Stop();
                });
            rebuildMilliseconds += frameTimer.Stop() - observerMilliseconds;
            const bool valid = IsCpuSourceFrameValid(pipeline.Stats(), settings, index);
            for (std::size_t targetIndex = frameTargetBegin; targetIndex < result.Targets.size(); ++targetIndex)
            {
                auto& target = result.Targets[targetIndex];
                target.SourceValidationPerformed = true;
                target.SourceValidationPassed = valid;
                target.Status = valid ? "valid" : "failed";
            }
            if (!valid) throw std::runtime_error{"Source frame validation failed"};
            for (const auto& row : pending) writeRecord(row);
            pending.clear();
            if (result.Targets.size() > frameTargetBegin)
                std::cout << "CPU targets completed: " << scenario.ScenarioId << " sample " << index << '\n' << std::flush;
        }
        if (result.Targets.size() != requested.size()) throw std::runtime_error{"Selected targets were not all reached"};
        result.Complete = true;
    }
    catch (const std::exception& error)
    {
        result.Failure = error.what();
        for (std::size_t index = frameTargetBegin; index < result.Targets.size(); ++index)
        {
            result.Targets[index].Status = "failed";
            result.Targets[index].Failure = error.what();
        }
        // 一次尝试的部分成功只供审计；异常不能把当前帧的早期目标保留为成功
        for (auto& row : pending)
        {
            row.Status = CpuRecordStatus::Failed;
            row.Equivalent = false;
            row.Failure = error.what();
            writeRecord(row);
        }
    }
    return result;
}
}
