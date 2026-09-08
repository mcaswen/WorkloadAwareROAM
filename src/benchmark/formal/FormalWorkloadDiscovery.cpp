#include "benchmark/formal/FormalWorkloadDiscovery.h"

#include "algorithms/data_oriented_roam/DataOrientedRoamPassInput.h"
#include "algorithms/data_oriented_roam/DataOrientedRoamPipeline.h"
#include "algorithms/data_oriented_roam/DataOrientedRoamWorkloadProbe.h"
#include "experiment/formal/FormalExperimentCamera.h"
#include "tools/PerformanceTimer.h"

#include <algorithm>
#include <stdexcept>

namespace ParallelRoam::Benchmark::Formal
{
namespace
{
using namespace Algorithms;
using namespace Algorithms::DataOrientedRoam;
using namespace Experiment::Formal;

/// <summary>
/// 显式映射窄探测结果且编排层不读取节点队列或槽位容器
/// </summary>
CpuDiscoveryRecord MakeRecord(const CameraSample& camera, TerrainLodPassId pass,
    const DataOrientedRoamPassWorkload& work, std::uint64_t inputHash, double hashMilliseconds)
{
    CpuDiscoveryRecord record;
    record.ScenarioId = camera.ScenarioId;
    record.SampleIndex = camera.SampleIndex;
    record.PassId = pass;
    record.CameraPoseHash = camera.CameraPoseHash;
    record.ViewInputHash = camera.ViewInputHash;
    record.ReplayInputHash = inputHash;
    record.PassInputVersion = DataOrientedRoamPassInputVersion;
    record.InputHashMilliseconds = hashMilliseconds;
    record.FeatureCollectionMilliseconds = work.FeatureCollectionMilliseconds;
    record.PreMergeQueueEntryCount = work.PreMergeQueueEntryCount;
    record.PreSplitQueueEntryCount = work.PreSplitQueueEntryCount;
    record.PreActiveTriangleCount = work.PreActiveTriangleCount;
    record.PreTriangleBudget = work.PreTriangleBudget;
    record.PreRemainingTriangleBudget = work.PreRemainingTriangleBudget;
    record.PreTopologyEditCount = work.PreTopologyEditCount;
    record.PreMaxActiveDepth = work.PreMaxActiveDepth;
    record.PreMaxDepth = work.PreMaxDepth;
    record.PlanningInteriorCandidateCount = work.PlanningInteriorCandidateCount;
    record.PlanningBoundaryCandidateCount = work.PlanningBoundaryCandidateCount;
    record.PlanningScheduledCandidateCount = work.PlanningScheduledCandidateCount;
    record.PlanningNonEmptyChunkCount = work.PlanningNonEmptyChunkCount;
    record.PlanningDirtyTriangleCount = work.PlanningDirtyTriangleCount;
    record.PlanningDirtyRangeCount = work.PlanningDirtyRangeCount;
    if (pass == TerrainLodPassId::MeshEmit)
        record.PlanningMeshReason = work.PlanningMeshInitialization ? "initialization" :
            (work.PlanningMeshFallback ? "inconsistent_edits_reinitialized" : "incremental");
    record.PreWorkCount = pass == TerrainLodPassId::MergeScore ? work.PreMergeQueueEntryCount :
        (pass == TerrainLodPassId::SplitScore ? work.PreSplitQueueEntryCount : work.PreActiveTriangleCount);
    record.PlanningWorkCount = pass == TerrainLodPassId::MeshEmit ? work.PlanningDirtyTriangleCount :
        work.PlanningInteriorCandidateCount + work.PlanningBoundaryCandidateCount;
    record.PrimaryWorkValue = work.PrimaryWorkValue;
    record.FeatureVector = FormatTargetSelectionFeatures(work.AuxiliaryFeatures);
    record.SelectionFeatureHash = HashTargetSelectionFeatures(
        {camera.ScenarioId, pass, camera.SampleIndex, work.PrimaryWorkValue, work.AuxiliaryFeatures});
    return record;
}
}

FormalWorkloadDiscoveryResult DiscoverCpuScenarioWorkloads(
    const Experiment::Formal::FormalScenario& scenario,
    const std::vector<Experiment::Formal::CameraSample>& cameras,
    const Experiment::Formal::TargetSelectionConfig& config,
    const std::function<void(const Experiment::Formal::CpuDiscoveryRecord&)>& writeRecord)
{
    FormalWorkloadDiscoveryResult result;
    std::vector<CpuDiscoveryRecord> records;
    std::vector<TargetSelectionCandidate> candidates;
    try
    {
        std::vector<const CameraSample*> ordered;
        for (const auto& camera : cameras)
            if (camera.ScenarioId == scenario.ScenarioId)
                ordered.push_back(&camera);
        std::sort(ordered.begin(), ordered.end(), [](const auto* left, const auto* right) {
            return left->SampleIndex < right->SampleIndex;
        });
        if (ordered.size() != CpuPilotSampleCount)
            throw std::runtime_error{"Discovery requires the complete frozen trajectory"};
        Terrain::HeightMap height;
        std::string error;
        if (!height.LoadFromFile(scenario.HeightMapPath, &error))
            throw std::runtime_error{error};
        DataOrientedRoamSettings settings;
        settings.MaxDepth = scenario.Settings.MaxDepth;
        settings.TriangleBudget = scenario.Settings.TriangleBudget;
        settings.SplitThreshold = scenario.Settings.ScreenSpaceSplitThresholdPixels;
        settings.MergeThreshold = scenario.Settings.ScreenSpaceMergeThresholdPixels;
        settings.EnableLocalConstraints = scenario.Settings.EnableLocalConstraints;
        settings.PassPolicy = scenario.Settings.PassPolicy;
        settings.EnablePassEvidence = settings.EnableTopologyValidation = true;
        DataOrientedRoamPipeline pipeline;
        for (std::size_t sample = 0U; sample < ordered.size(); ++sample)
        {
            const auto& camera = *ordered[sample];
            if (camera.SampleIndex != sample)
                throw std::runtime_error{"Duplicate or missing frozen camera sample"};
            ValidateCameraSample(camera, scenario);
            const auto begin = records.size();
            (void)pipeline.BuildWithPassObserver(height, scenario.Settings.TerrainSize, scenario.Settings.HeightScale,
                BuildCameraView(camera), settings, [&](const auto& state, auto pass) {
                    const auto work = ProbeDataOrientedRoamPassWorkload(state, pass);
                    Tools::PerformanceTimer hashTimer;
                    const auto inputHash = HashDataOrientedRoamPassInput(state, pass);
                    records.push_back(MakeRecord(camera, pass, work, inputHash, hashTimer.Stop()));
                    candidates.push_back({camera.ScenarioId, pass, camera.SampleIndex,
                        work.PrimaryWorkValue, work.AuxiliaryFeatures});
                });
            const auto& stats = pipeline.Stats();
            // 这些诊断已由流水线完整执行且在五个探测边界之外
            const bool valid = records.size() - begin == CpuPilotPassIds.size() &&
                stats.BuildSequence == sample + 1U && stats.ActiveTriangleCount <= settings.TriangleBudget &&
                stats.PersistentSplitQueueSize == stats.ActiveTriangleCount && stats.QueueInvariantViolationCount == 0U &&
                stats.InvalidTopologyCount == 0U && stats.InvalidNeighborCount == 0U && stats.TjunctionCount == 0U &&
                stats.TopologyHash != 0U && stats.NormalizedMeshHash != 0U;
            for (std::size_t index = begin; index < records.size(); ++index)
            {
                auto& record = records[index];
                record.ValidationPerformed = true;
                record.ValidationPassed = valid;
                record.ValidationMilliseconds = stats.ValidateMilliseconds;
                record.Status = !valid ? CpuRecordStatus::Failed :
                    (record.PrimaryWorkValue == 0.0F ? CpuRecordStatus::NoWork : CpuRecordStatus::Valid);
                record.SelectionEligible = valid && sample != 0U && record.PrimaryWorkValue > 0.0F;
                record.SelectionExclusionReason = !valid ? "frame_validation_failed" :
                    (sample == 0U ? "root_initialization" : (record.PrimaryWorkValue == 0.0F ? "no_work" : ""));
                if (!valid)
                    record.Failure = "frame_validation_failed";
                writeRecord(record);
                ++result.RecordCount;
            }
            if (!valid)
                throw std::runtime_error{"Frame correctness validation failed"};
        }
        for (const auto pass : CpuPilotPassIds)
        {
            std::vector<TargetSelectionCandidate> eligible;
            for (std::size_t index = 0U; index < records.size(); ++index)
                if (records[index].PassId == pass && records[index].SelectionEligible)
                    eligible.push_back(candidates[index]);
            const auto selection = SelectTargetStates(eligible, config);
            result.Coverage.push_back({scenario.ScenarioId, pass, CpuTargetSelectorVersion, CpuTargetSelectionSeed,
                selection.RequestedCount, selection.EligibleCount, selection.Selected.size(),
                selection.StratumCounts, selection.InsufficiencyReason});
            for (const auto& selected : selection.Selected)
            {
                const auto& identity = selected.Candidate;
                const auto found = std::find_if(records.begin(), records.end(), [&](const auto& record) {
                    return record.PassId == pass && record.SampleIndex == identity.SampleIndex;
                });
                result.Targets.push_back({scenario.ScenarioId, pass, identity.SampleIndex,
                    selected.Stratum, selected.Rank, CpuTargetSelectionSeed, identity.PrimaryWorkValue,
                    FormatTargetSelectionFeatures(identity.AuxiliaryFeatures), found->ReplayInputHash,
                    selected.SelectionFeatureHash, scenario.AnalysisSplit, found->CameraPoseHash, found->ViewInputHash});
            }
        }
        result.Complete = true;
    }
    catch (const std::exception& error)
    {
        // 异常发生在帧中时把尚未诊断的部分明确记为失败
        for (std::size_t index = result.RecordCount; index < records.size(); ++index)
        {
            auto& record = records[index];
            record.Status = CpuRecordStatus::Failed;
            record.Failure = error.what();
            record.SelectionEligible = false;
            record.SelectionExclusionReason = "scenario_failed";
            writeRecord(record);
            ++result.RecordCount;
        }
        result.Targets.clear();
        result.Coverage.clear();
        result.Failure = error.what();
    }
    return result;
}
}
