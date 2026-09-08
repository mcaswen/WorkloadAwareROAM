#include "experiment/formal/FormalExperimentCsv.h"

#include "experiment/ExperimentCsvCodec.h"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace ParallelRoam::Experiment::Formal
{
namespace
{
std::string StatusName(CpuRecordStatus status)
{
    switch (status)
    {
    case CpuRecordStatus::Incomplete: return "incomplete";
    case CpuRecordStatus::Valid: return "valid";
    case CpuRecordStatus::NoWork: return "no_work";
    case CpuRecordStatus::Failed: return "failed";
    }
    throw std::runtime_error("Unknown CPU record status");
}

void ValidateIdentity(const std::string& scenario, Algorithms::TerrainLodPassId pass, std::uint32_t sample)
{
    if (scenario.empty() || sample >= CpuPilotSampleCount ||
        std::find(CpuPilotPassIds.begin(), CpuPilotPassIds.end(), pass) == CpuPilotPassIds.end())
    {
        throw std::runtime_error("Invalid CPU record identity");
    }
}
} // 匿名命名空间

void WriteInputPreparationSummary(std::ostream& output, const InputPreparationSummary& summary)
{
    WriteExperimentCsvRow(output, {"schemaVersion", "dataPurpose", "status", "scenarioCount", "expectedCameraCount",
        "cameraCount", "targetCount", "targetStatus", "assetDigestStatus", "backend", "buildConfiguration", "compiler", "error"});
    WriteExperimentCsvRow(output, {"1", "exploratory", summary.Status, std::to_string(summary.ScenarioCount),
        std::to_string(summary.ExpectedCameraCount), std::to_string(summary.CameraCount), std::to_string(summary.TargetCount),
        summary.TargetStatus, "external_verification_required", summary.Backend, summary.BuildConfiguration,
        summary.Compiler, summary.Error});
}

void WriteCpuDiscoveryCsvHeader(std::ostream& output)
{
    WriteExperimentCsvRow(output, {"schemaVersion", "dataPurpose", "scenarioId", "sampleIndex", "passId",
        "viewInputHash", "replayInputHash", "pre_workCount", "planning_workCount", "featureCollectionMs", "status", "failure",
        "cameraPoseHash", "passInputVersion", "pre_mergeQueueEntryCount", "pre_splitQueueEntryCount",
        "pre_activeTriangleCount", "pre_triangleBudget", "pre_remainingTriangleBudget", "pre_topologyEditCount",
        "pre_maxActiveDepth", "pre_maxDepth", "planning_interiorCandidateCount", "planning_boundaryCandidateCount",
        "planning_scheduledCandidateCount", "planning_nonEmptyChunkCount", "planning_dirtyTriangleCount", "planning_dirtyRangeCount",
        "planning_meshReason", "primaryWorkValue", "featureVector", "selectionFeatureHash",
        "validationPerformed", "validationPassed", "selectionEligible", "selectionExclusionReason",
        "inputHashMs", "validationMs"});
}

void WriteCpuDiscoveryCsvRow(std::ostream& output, const CpuDiscoveryRecord& record)
{
    ValidateIdentity(record.ScenarioId, record.PassId, record.SampleIndex);
    if (!std::isfinite(record.FeatureCollectionMilliseconds) || record.FeatureCollectionMilliseconds < 0 ||
        !std::isfinite(record.InputHashMilliseconds) || record.InputHashMilliseconds < 0 ||
        !std::isfinite(record.ValidationMilliseconds) || record.ValidationMilliseconds < 0 ||
        !std::isfinite(record.PrimaryWorkValue) || record.PrimaryWorkValue < 0 ||
        ((record.Status == CpuRecordStatus::Valid || record.Status == CpuRecordStatus::NoWork) &&
            (record.ViewInputHash == 0 || record.ReplayInputHash == 0 || !record.ValidationPerformed ||
                !record.ValidationPassed || record.PassInputVersion == 0U || record.SelectionFeatureHash == 0U)) ||
        (record.SelectionEligible && (record.Status != CpuRecordStatus::Valid || record.SampleIndex == 0U ||
            record.PrimaryWorkValue <= 0.0F || !record.SelectionExclusionReason.empty())))
        throw std::runtime_error("Discovery record lacks valid input evidence");
    WriteExperimentCsvRow(output, {"2", "exploratory", record.ScenarioId, std::to_string(record.SampleIndex),
        std::string{Algorithms::ToString(record.PassId)}, std::to_string(record.ViewInputHash),
        std::to_string(record.ReplayInputHash), std::to_string(record.PreWorkCount), std::to_string(record.PlanningWorkCount),
        FormatExperimentCsvDouble(record.FeatureCollectionMilliseconds), StatusName(record.Status), record.Failure,
        std::to_string(record.CameraPoseHash),
        std::to_string(record.PassInputVersion),
        std::to_string(record.PreMergeQueueEntryCount),
        std::to_string(record.PreSplitQueueEntryCount),
        std::to_string(record.PreActiveTriangleCount),
        std::to_string(record.PreTriangleBudget),
        std::to_string(record.PreRemainingTriangleBudget),
        std::to_string(record.PreTopologyEditCount),
        std::to_string(record.PreMaxActiveDepth),
        std::to_string(record.PreMaxDepth),
        std::to_string(record.PlanningInteriorCandidateCount),
        std::to_string(record.PlanningBoundaryCandidateCount),
        std::to_string(record.PlanningScheduledCandidateCount),
        std::to_string(record.PlanningNonEmptyChunkCount),
        std::to_string(record.PlanningDirtyTriangleCount),
        std::to_string(record.PlanningDirtyRangeCount),
        record.PlanningMeshReason,
        FormatExperimentCsvFloat(record.PrimaryWorkValue),
        record.FeatureVector,
        std::to_string(record.SelectionFeatureHash),
        record.ValidationPerformed ? "true" : "false",
        record.ValidationPassed ? "true" : "false",
        record.SelectionEligible ? "true" : "false",
        record.SelectionExclusionReason,
        FormatExperimentCsvDouble(record.InputHashMilliseconds),
        FormatExperimentCsvDouble(record.ValidationMilliseconds)});
}

void WriteCpuTargetCoverageCsv(std::ostream& output, const std::vector<CpuTargetCoverageRecord>& records)
{
    WriteExperimentCsvRow(output, {"schemaVersion", "dataPurpose", "scenarioId", "passId", "selectorVersion",
        "selectionSeed", "requestedCount", "eligibleCount", "selectedCount", "lowCount", "middleCount", "highCount",
        "coverageCount", "missingCount", "insufficiencyReason"});
    for (const auto& record : records)
        WriteExperimentCsvRow(output, {"1", "exploratory", record.ScenarioId, std::string{Algorithms::ToString(record.PassId)},
            std::to_string(record.SelectorVersion), std::to_string(record.SelectionSeed), std::to_string(record.RequestedCount),
            std::to_string(record.EligibleCount), std::to_string(record.SelectedCount), std::to_string(record.StratumCounts[0]),
            std::to_string(record.StratumCounts[1]), std::to_string(record.StratumCounts[2]), std::to_string(record.StratumCounts[3]),
            std::to_string(record.RequestedCount - record.SelectedCount), record.InsufficiencyReason});
}

void WriteCpuDiscoverySummary(std::ostream& output, const CpuDiscoverySummary& summary)
{
    WriteExperimentCsvRow(output, {"schemaVersion", "dataPurpose", "status", "scenarioCount", "completedScenarioCount",
        "expectedRecordCount", "recordCount", "targetCount", "insufficientGroupCount", "targetsPerPass", "selectorVersion",
        "passInputVersion", "selectionSeed", "targetStatus", "error", "validRecordCount", "noWorkRecordCount", "failedRecordCount"});
    WriteExperimentCsvRow(output, {"1", "exploratory", summary.Status, std::to_string(summary.ScenarioCount),
        std::to_string(summary.CompletedScenarioCount), std::to_string(summary.ExpectedRecordCount),
        std::to_string(summary.RecordCount), std::to_string(summary.TargetCount), std::to_string(summary.InsufficientGroupCount),
        std::to_string(summary.TargetsPerPass), std::to_string(summary.SelectorVersion), std::to_string(summary.PassInputVersion),
        std::to_string(summary.SelectionSeed), summary.TargetStatus, summary.Error,
        std::to_string(summary.ValidRecordCount), std::to_string(summary.NoWorkRecordCount),
        std::to_string(summary.FailedRecordCount)});
}

void WriteCpuPairCsvHeader(std::ostream& output)
{
    WriteExperimentCsvRow(output, {"schemaVersion", "dataPurpose", "scenarioId", "sampleIndex", "passId", "repeatIndex",
        "isWarmup", "blockOrder", "orderIndex", "requestedAction", "actualAction", "requestedWorkerCount",
        "actualWorkerCount", "fallbackReason", "replayInputHash", "resultHash", "wallMs", "correct", "equivalent", "status", "failure"});
}

void WriteCpuPairCsvRow(std::ostream& output, const CpuPairRecord& record)
{
    ValidateIdentity(record.ScenarioId, record.PassId, record.SampleIndex);
    if (record.SampleIndex == 0 || record.OrderIndex > 1 || (record.BlockOrder != "AB" && record.BlockOrder != "BA") ||
        record.WallMilliseconds < 0 || (record.Status == CpuRecordStatus::Valid &&
        (!record.Correct || !record.Equivalent || record.ReplayInputHash == 0 || record.ResultHash == 0 ||
        record.RequestedWorkerCount == 0 || record.ActualWorkerCount == 0)))
    {
        throw std::runtime_error("CPU pair record lacks valid pairing or result evidence");
    }
    WriteExperimentCsvRow(output, {"1", "exploratory", record.ScenarioId, std::to_string(record.SampleIndex),
        std::string{Algorithms::ToString(record.PassId)}, std::to_string(record.RepeatIndex), record.IsWarmup ? "true" : "false",
        record.BlockOrder, std::to_string(record.OrderIndex), std::string{Algorithms::ToString(record.RequestedAction)},
        std::string{Algorithms::ToString(record.ActualAction)}, std::to_string(record.RequestedWorkerCount),
        std::to_string(record.ActualWorkerCount), std::string{Algorithms::ToString(record.Fallback)},
        std::to_string(record.ReplayInputHash), std::to_string(record.ResultHash), FormatExperimentCsvDouble(record.WallMilliseconds),
        record.Correct ? "true" : "false", record.Equivalent ? "true" : "false", StatusName(record.Status), record.Failure});
}
} // 命名空间 ParallelRoam::Experiment::Formal
