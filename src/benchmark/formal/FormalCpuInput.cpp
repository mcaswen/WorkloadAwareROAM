#include "benchmark/formal/FormalCpuInput.h"

#include "algorithms/data_oriented_roam/DataOrientedRoamPassInput.h"
#include "algorithms/data_oriented_roam/DataOrientedRoamWorkloadProbe.h"
#include "experiment/formal/FormalExperimentTargetSelector.h"

namespace ParallelRoam::Benchmark::Formal
{
namespace
{
using namespace Algorithms;
using namespace Algorithms::DataOrientedRoam;
using namespace Experiment::Formal;

template<class Record>
Record MakeInputRecord(const CameraSample& camera, TerrainLodPassId pass,
    const DataOrientedRoamPassWorkload& work, std::uint64_t inputHash)
{
    Record record;
    record.ScenarioId = camera.ScenarioId;
    record.SampleIndex = camera.SampleIndex;
    record.PassId = pass;
    record.CameraPoseHash = camera.CameraPoseHash;
    record.ViewInputHash = camera.ViewInputHash;
    record.ReplayInputHash = inputHash;
    record.PassInputVersion = DataOrientedRoamPassInputVersion;
    record.FeatureCollectionMilliseconds = work.FeatureCollectionMilliseconds;
    // 两种 CSV 的选择字段来自同一映射，不能在配对时改用执行后统计
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
    record.PrimaryWorkValue = work.PrimaryWorkValue;
    record.FeatureVector = FormatTargetSelectionFeatures(work.AuxiliaryFeatures);
    record.SelectionFeatureHash = HashTargetSelectionFeatures(
        {camera.ScenarioId, pass, camera.SampleIndex, work.PrimaryWorkValue, work.AuxiliaryFeatures});
    return record;
}
}

DataOrientedRoamSettings MakeCpuSourceSettings(const FormalScenario& scenario)
{
    DataOrientedRoamSettings settings;
    settings.MaxDepth = scenario.Settings.MaxDepth;
    settings.TriangleBudget = scenario.Settings.TriangleBudget;
    settings.SplitThreshold = scenario.Settings.ScreenSpaceSplitThresholdPixels;
    settings.MergeThreshold = scenario.Settings.ScreenSpaceMergeThresholdPixels;
    settings.EnableLocalConstraints = scenario.Settings.EnableLocalConstraints;
    settings.PassPolicy = scenario.Settings.PassPolicy;
    settings.EnablePassEvidence = settings.EnableTopologyValidation = true;
    settings.EnableTopologyPairEvidence = false;
    return settings;
}

CpuDiscoveryRecord MakeCpuDiscoveryInputRecord(const CameraSample& camera, TerrainLodPassId pass,
    const DataOrientedRoamPassWorkload& work, std::uint64_t inputHash, double hashMilliseconds)
{
    auto record = MakeInputRecord<CpuDiscoveryRecord>(camera, pass, work, inputHash);
    record.InputHashMilliseconds = hashMilliseconds;
    record.PreWorkCount = pass == TerrainLodPassId::MergeScore ? work.PreMergeQueueEntryCount :
        (pass == TerrainLodPassId::SplitScore ? work.PreSplitQueueEntryCount : work.PreActiveTriangleCount);
    record.PlanningWorkCount = pass == TerrainLodPassId::MeshEmit ? work.PlanningDirtyTriangleCount :
        work.PlanningInteriorCandidateCount + work.PlanningBoundaryCandidateCount;
    return record;
}

CpuPairRecord MakeCpuPairInputRecord(const CameraSample& camera, TerrainLodPassId pass,
    const DataOrientedRoamPassWorkload& work, std::uint64_t inputHash)
{
    return MakeInputRecord<CpuPairRecord>(camera, pass, work, inputHash);
}

bool IsCpuSourceFrameValid(const DataOrientedRoamStats& stats, const DataOrientedRoamSettings& settings,
    std::uint32_t sampleIndex)
{
    return settings.EnablePassEvidence && settings.EnableTopologyValidation &&
        stats.BuildSequence == sampleIndex + 1U && stats.ActiveTriangleCount <= settings.TriangleBudget &&
        stats.PersistentSplitQueueSize == stats.ActiveTriangleCount && stats.QueueInvariantViolationCount == 0U &&
        stats.InvalidTopologyCount == 0U && stats.InvalidNeighborCount == 0U && stats.TjunctionCount == 0U &&
        stats.TopologyHash != 0U && stats.NormalizedMeshHash != 0U;
}
}
