#include "experiment/formal/FormalExperimentCsv.h"

#include "experiment/ExperimentCsvCodec.h"

#include <algorithm>
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
        "viewInputHash", "replayInputHash", "pre_workCount", "planning_workCount", "featureCollectionMs", "status", "failure"});
}

void WriteCpuDiscoveryCsvRow(std::ostream& output, const CpuDiscoveryRecord& record)
{
    ValidateIdentity(record.ScenarioId, record.PassId, record.SampleIndex);
    if (record.FeatureCollectionMilliseconds < 0 || (record.Status == CpuRecordStatus::Valid &&
        (record.ViewInputHash == 0 || record.ReplayInputHash == 0)))
    {
        throw std::runtime_error("Discovery record lacks valid input evidence");
    }
    WriteExperimentCsvRow(output, {"1", "exploratory", record.ScenarioId, std::to_string(record.SampleIndex),
        std::string{Algorithms::ToString(record.PassId)}, std::to_string(record.ViewInputHash),
        std::to_string(record.ReplayInputHash), std::to_string(record.PreWorkCount), std::to_string(record.PlanningWorkCount),
        FormatExperimentCsvDouble(record.FeatureCollectionMilliseconds), StatusName(record.Status), record.Failure});
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
