#include "experiment/formal/FormalCpuPairCsv.h"

#include "experiment/ExperimentCsvCodec.h"
#include "experiment/formal/FormalExperimentTargetSelector.h"

#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <limits>
#include <map>
#include <stdexcept>
#include <type_traits>

namespace ParallelRoam::Experiment::Formal
{
namespace
{
using namespace Algorithms;

// 同一字段声明供读、写及共同输入比较使用，避免列顺序在三个实现中漂移
template<class Record, class Visitor>
void VisitPairFields(Record& r, Visitor visit)
{
    visit("runId", r.RunId);
    visit("scenarioId", r.ScenarioId);
    visit("terrainId", r.TerrainId);
    visit("analysisSplit", r.AnalysisSplit);
    visit("sampleIndex", r.SampleIndex);
    visit("passId", r.PassId);
    visit("selectionRank", r.SelectionRank);
    visit("selectionStratum", r.SelectionStratum);
    visit("selectionSeed", r.SelectionSeed);
    visit("selectorVersion", r.SelectorVersion);
    visit("passInputVersion", r.PassInputVersion);
    visit("cameraPoseHash", r.CameraPoseHash);
    visit("viewInputHash", r.ViewInputHash);
    visit("replayInputHash", r.ReplayInputHash);
    visit("selectionFeatureHash", r.SelectionFeatureHash);
    visit("primaryWorkValue", r.PrimaryWorkValue);
    visit("featureVector", r.FeatureVector);
    visit("manifestWarmupCount", r.Configuration.ManifestWarmupCount);
    visit("manifestRepeatCount", r.Configuration.ManifestRepeatCount);
    visit("manifestParallelWorkerCount", r.Configuration.ManifestParallelWorkerCount);
    visit("warmupCount", r.Configuration.WarmupCount);
    visit("measuredRepeatCount", r.Configuration.MeasuredRepeatCount);
    visit("parallelWorkerCount", r.Configuration.ParallelWorkerCount);
    visit("absoluteBlockIndex", r.AbsoluteBlockIndex);
    visit("repeatIndex", r.RepeatIndex);
    visit("isWarmup", r.IsWarmup);
    visit("blockOrder", r.BlockOrder);
    visit("orderIndex", r.OrderIndex);
    visit("requestedAction", r.RequestedAction);
    visit("actualAction", r.ActualAction);
    visit("requestedWorkerCount", r.RequestedWorkerCount);
    visit("actualWorkerCount", r.ActualWorkerCount);
    visit("fallbackReason", r.Fallback);
    visit("executionPath", r.ExecutionPath);
    visit("fallbackDetail", r.FallbackDetail);
    visit("resultHash", r.ResultHash);
    visit("validationPerformed", r.ValidationPerformed);
    visit("diagnosticsDisabledAtExecution", r.DiagnosticsDisabledAtExecution);
    visit("correct", r.Correct);
    visit("equivalent", r.Equivalent);
    visit("status", r.Status);
    visit("failure", r.Failure);
    visit("wallMs", r.WallMilliseconds);
    visit("stateCloneMs", r.StateCloneMilliseconds);
    visit("inputCheckMs", r.InputCheckMilliseconds);
    visit("validationMs", r.ValidationMilliseconds);
    visit("workerPreparationMs", r.WorkerPreparationMilliseconds);
    visit("featureCollectionMs", r.FeatureCollectionMilliseconds);
    visit("pre_mergeQueueEntryCount", r.PreMergeQueueEntryCount);
    visit("pre_splitQueueEntryCount", r.PreSplitQueueEntryCount);
    visit("pre_activeTriangleCount", r.PreActiveTriangleCount);
    visit("pre_triangleBudget", r.PreTriangleBudget);
    visit("pre_remainingTriangleBudget", r.PreRemainingTriangleBudget);
    visit("pre_topologyEditCount", r.PreTopologyEditCount);
    visit("pre_maxActiveDepth", r.PreMaxActiveDepth);
    visit("pre_maxDepth", r.PreMaxDepth);
    visit("planning_interiorCandidateCount", r.PlanningInteriorCandidateCount);
    visit("planning_boundaryCandidateCount", r.PlanningBoundaryCandidateCount);
    visit("planning_scheduledCandidateCount", r.PlanningScheduledCandidateCount);
    visit("planning_nonEmptyChunkCount", r.PlanningNonEmptyChunkCount);
    visit("planning_dirtyTriangleCount", r.PlanningDirtyTriangleCount);
    visit("planning_dirtyRangeCount", r.PlanningDirtyRangeCount);
    visit("planning_meshReason", r.PlanningMeshReason);
    visit("post_earlyCommitCount", r.PostEarlyCommitCount);
    visit("post_activeTriangleCount", r.PostActiveTriangleCount);
    visit("post_dirtyTriangleCount", r.PostDirtyTriangleCount);
    visit("post_dirtyRangeCount", r.PostDirtyRangeCount);
    visit("scoreMs", r.ScoreMilliseconds);
    visit("heapifyMs", r.HeapifyMilliseconds);
    visit("candidateSnapshotMs", r.CandidateSnapshotMilliseconds);
    visit("chunkBuildMs", r.ChunkBuildMilliseconds);
    visit("queueInvalidationMs", r.QueueInvalidationMilliseconds);
    visit("commitMs", r.CommitMilliseconds);
    visit("resultMergeMs", r.ResultMergeMilliseconds);
    visit("indexQueueRefreshMs", r.IndexQueueRefreshMilliseconds);
    visit("serialConvergenceMs", r.SerialConvergenceMilliseconds);
}

std::string StatusName(CpuRecordStatus status)
{
    switch (status)
    {
    case CpuRecordStatus::Incomplete: return "incomplete";
    case CpuRecordStatus::Valid: return "valid";
    case CpuRecordStatus::NoWork: return "no_work";
    case CpuRecordStatus::Failed: return "failed";
    }
    throw std::runtime_error{"Unknown CPU pair status"};
}

template<class T>
std::string FormatValue(const T& value)
{
    if constexpr (std::is_same_v<T, std::string>) return value;
    else if constexpr (std::is_same_v<T, bool>) return value ? "true" : "false";
    else if constexpr (std::is_same_v<T, CpuRecordStatus>) return StatusName(value);
    else if constexpr (std::is_enum_v<T>) return std::string{ToString(value)};
    else if constexpr (std::is_same_v<T, float>) return FormatExperimentCsvFloat(value);
    else if constexpr (std::is_floating_point_v<T>) return FormatExperimentCsvDouble(value);
    else return std::to_string(value);
}

template<class T>
void ParseValue(const std::string& text, T& value)
{
    if constexpr (std::is_same_v<T, std::string>) value = text;
    else if constexpr (std::is_same_v<T, bool>)
    {
        if (text != "true" && text != "false") throw std::runtime_error{"Invalid pair boolean"};
        value = text == "true";
    }
    else if constexpr (std::is_enum_v<T>)
    {
        // 枚举名字必须完整匹配；仅扫描已有枚举值，不把任意数字强转为合法动作
        bool found = false;
        if constexpr (std::is_same_v<T, CpuRecordStatus>)
        {
            for (const auto item : {CpuRecordStatus::Incomplete, CpuRecordStatus::Valid, CpuRecordStatus::NoWork, CpuRecordStatus::Failed})
                if (StatusName(item) == text) { value = item; found = true; }
        }
        else if constexpr (std::is_same_v<T, TerrainLodPassId>)
        {
            for (const auto item : CpuPilotPassIds)
                if (ToString(item) == text) { value = item; found = true; }
        }
        else if constexpr (std::is_same_v<T, TerrainLodPassAction>)
        {
            for (const auto item : {TerrainLodPassAction::SerialFullRefresh, TerrainLodPassAction::ParallelFullRefresh,
                TerrainLodPassAction::SerialImmediate, TerrainLodPassAction::ParallelAssisted,
                TerrainLodPassAction::SerialDirty, TerrainLodPassAction::ParallelDirty, TerrainLodPassAction::SerialFull})
                if (ToString(item) == text) { value = item; found = true; }
        }
        else
        {
            for (const auto item : {TerrainLodPassFallbackReason::None, TerrainLodPassFallbackReason::NoWork,
                TerrainLodPassFallbackReason::ParallelDisabled, TerrainLodPassFallbackReason::BelowParallelThreshold,
                TerrainLodPassFallbackReason::ResourceCapacity})
                if (ToString(item) == text) { value = item; found = true; }
        }
        if (!found) throw std::runtime_error{"Unknown pair enum: " + text};
    }
    else
    {
        const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value);
        if (error != std::errc{} || end != text.data() + text.size() || text.empty() || value < 0)
            throw std::runtime_error{"Invalid pair number: " + text};
        if constexpr (std::is_floating_point_v<T>)
            if (!std::isfinite(value)) throw std::runtime_error{"Non-finite pair number"};
    }
}

ExperimentCsvRow PairHeader()
{
    ExperimentCsvRow row{"schemaVersion", "dataPurpose", "measurementMode", "measurementProtocolVersion"};
    const CpuPairRecord record;
    VisitPairFields(record, [&](const char* name, const auto&) { row.emplace_back(name); });
    return row;
}

ExperimentCsvRow CommonInput(const CpuPairRecord& record)
{
    ExperimentCsvRow values;
    bool identity = true;
    VisitPairFields(record, [&](std::string_view name, const auto& value) {
        if (name == "absoluteBlockIndex") identity = false;
        if (identity || name.starts_with("pre_") || name.starts_with("planning_"))
            values.push_back(FormatValue(value));
    });
    return values;
}

std::string ExpectedOrder(TerrainLodPassId pass, std::uint32_t block)
{
    constexpr std::array mesh{"ABC", "ACB", "BAC", "BCA", "CAB", "CBA"};
    return pass == TerrainLodPassId::MeshEmit ? mesh[block % mesh.size()] : (block % 2U == 0U ? "AB" : "BA");
}

void ValidateRecord(const CpuPairRecord& record)
{
    ValidateCpuPairConfiguration(record.Configuration);
    const auto actions = CpuPairActions(record.PassId);
    const auto& c = record.Configuration;
    if (record.RunId.empty() || record.ScenarioId.empty() || record.TerrainId.empty() ||
        record.SampleIndex == 0U || record.SampleIndex >= CpuPilotSampleCount ||
        record.PassInputVersion != 1U || record.SelectorVersion != CpuTargetSelectorVersion ||
        record.SelectionSeed != CpuTargetSelectionSeed || record.SelectionRank >= 8U ||
        (record.SelectionStratum != "low" && record.SelectionStratum != "middle" &&
            record.SelectionStratum != "high" && record.SelectionStratum != "coverage") ||
        (record.AnalysisSplit != "train" && record.AnalysisSplit != "test") ||
        record.CameraPoseHash == 0U || record.ViewInputHash == 0U || record.ReplayInputHash == 0U ||
        record.SelectionFeatureHash == 0U || record.FeatureVector.empty() ||
        !std::isfinite(record.PrimaryWorkValue) || record.PrimaryWorkValue <= 0.0F)
        throw std::runtime_error{"Pair record lacks frozen target identity"};
    if (record.AbsoluteBlockIndex >= c.WarmupCount + c.MeasuredRepeatCount ||
        record.IsWarmup != (record.AbsoluteBlockIndex < c.WarmupCount) ||
        record.RepeatIndex != (record.IsWarmup ? record.AbsoluteBlockIndex : record.AbsoluteBlockIndex - c.WarmupCount) ||
        record.BlockOrder != ExpectedOrder(record.PassId, record.AbsoluteBlockIndex) ||
        record.OrderIndex >= actions.size() ||
        record.RequestedAction != actions[record.BlockOrder[record.OrderIndex] - 'A'] ||
        std::find(actions.begin(), actions.end(), record.ActualAction) == actions.end())
        throw std::runtime_error{"Invalid pair order or action"};
    const bool parallel = record.RequestedAction == TerrainLodPassAction::ParallelFullRefresh ||
        record.RequestedAction == TerrainLodPassAction::ParallelAssisted || record.RequestedAction == TerrainLodPassAction::ParallelDirty;
    if (record.RequestedWorkerCount != (parallel ? c.ParallelWorkerCount : 1U) ||
        record.ActualWorkerCount > record.RequestedWorkerCount ||
        (record.ActualWorkerCount > 1U && (record.ExecutionPath != "thread_pool" || record.ActualAction != record.RequestedAction ||
            record.Fallback != TerrainLodPassFallbackReason::None)) ||
        (record.ActualWorkerCount == 1U && record.ExecutionPath != "caller_thread") ||
        (record.ActualWorkerCount == 0U && (record.ExecutionPath != "no_work" || record.Fallback != TerrainLodPassFallbackReason::NoWork)) ||
        (record.Fallback == TerrainLodPassFallbackReason::None && (!record.FallbackDetail.empty() || record.ActualAction != record.RequestedAction ||
            (parallel && record.ActualWorkerCount < 2U))) ||
        (record.Fallback != TerrainLodPassFallbackReason::None && record.FallbackDetail.empty()))
        throw std::runtime_error{"Invalid worker, execution path or fallback evidence"};
    VisitPairFields(record, [](const char*, const auto& value) {
        using T = std::remove_cvref_t<decltype(value)>;
        if constexpr (std::is_floating_point_v<T>)
            if (!std::isfinite(value) || value < 0) throw std::runtime_error{"Invalid pair timing or workload"};
    });
    const bool usable = record.Status == CpuRecordStatus::Valid || record.Status == CpuRecordStatus::NoWork;
    if (usable && (!record.Correct || !record.Equivalent || !record.ValidationPerformed ||
        !record.DiagnosticsDisabledAtExecution || record.ResultHash == 0U || !record.Failure.empty()))
        throw std::runtime_error{"Valid pair record lacks completed correctness evidence"};
    if (usable && ((record.Status == CpuRecordStatus::NoWork) != (record.Fallback == TerrainLodPassFallbackReason::NoWork)))
        throw std::runtime_error{"No-work status conflicts with execution"};
}
}

void ValidateCpuPairConfiguration(const CpuPairConfiguration& c)
{
    if (c.ManifestWarmupCount != 5U || c.ManifestRepeatCount != 30U || c.ManifestParallelWorkerCount != 8U ||
        c.MeasuredRepeatCount == 0U || c.ParallelWorkerCount == 0U ||
        c.WarmupCount > std::numeric_limits<std::uint32_t>::max() - c.MeasuredRepeatCount ||
        static_cast<std::size_t>(c.WarmupCount) + c.MeasuredRepeatCount > std::vector<CpuPairRecord>{}.max_size() / 3U)
        throw std::runtime_error{"Invalid pair configuration or row-count overflow"};
}

std::vector<TerrainLodPassAction> CpuPairActions(TerrainLodPassId pass)
{
    switch (pass)
    {
    case TerrainLodPassId::MergeScore:
    case TerrainLodPassId::SplitScore:
        return {TerrainLodPassAction::SerialFullRefresh, TerrainLodPassAction::ParallelFullRefresh};
    case TerrainLodPassId::MergeTopology:
    case TerrainLodPassId::SplitTopology:
        return {TerrainLodPassAction::SerialImmediate, TerrainLodPassAction::ParallelAssisted};
    case TerrainLodPassId::MeshEmit:
        return {TerrainLodPassAction::SerialDirty, TerrainLodPassAction::ParallelDirty, TerrainLodPassAction::SerialFull};
    default: throw std::runtime_error{"Expected a CPU pair pass"};
    }
}

void WriteCpuPairCsvHeader(std::ostream& output) { WriteExperimentCsvRow(output, PairHeader()); }

void WriteCpuPairCsvRow(std::ostream& output, const CpuPairRecord& record)
{
    ValidateRecord(record);
    ExperimentCsvRow row{"2", "exploratory", "pilot_measurement", "1"};
    VisitPairFields(record, [&](const char*, const auto& value) { row.push_back(FormatValue(value)); });
    WriteExperimentCsvRow(output, row);
}

std::vector<CpuPairRecord> ReadCpuPairCsv(std::istream& input, std::string_view source)
{
    const auto table = ReadExperimentCsv(input, source);
    RequireExperimentCsvHeader(table, PairHeader());
    std::vector<CpuPairRecord> records;
    records.reserve(table.Rows.size());
    for (const auto& row : table.Rows)
    {
        if (row[0] != "2" || row[1] != "exploratory" || row[2] != "pilot_measurement" || row[3] != "1")
            throw std::runtime_error{"Unsupported CPU pair schema or measurement protocol"};
        CpuPairRecord record;
        std::size_t column = 4U;
        VisitPairFields(record, [&](const char*, auto& value) { ParseValue(row[column++], value); });
        ValidateRecord(record);
        records.push_back(std::move(record));
    }
    return records;
}

void ValidateCpuPairTarget(const std::vector<CpuPairRecord>& records, const TargetStateRef& target,
    const std::string& runId, const CpuPairConfiguration& configuration)
{
    ValidateCpuPairConfiguration(configuration);
    const auto actions = CpuPairActions(target.PassId);
    const auto expected = (static_cast<std::size_t>(configuration.WarmupCount) + configuration.MeasuredRepeatCount) * actions.size();
    if (records.size() != expected) throw std::runtime_error{"Incomplete target strategy rows"};
    const auto common = CommonInput(records.front());
    const auto resultHash = records.front().ResultHash;
    std::map<std::pair<std::uint32_t, std::uint32_t>, const CpuPairRecord*> unique;
    for (const auto& record : records)
    {
        ValidateRecord(record);
        if (record.RunId != runId || record.ScenarioId != target.ScenarioId || record.PassId != target.PassId ||
            record.SampleIndex != target.SampleIndex || record.SelectionRank != target.SelectionRank ||
            record.SelectionStratum != target.SelectionStratum || record.SelectionSeed != target.SelectionSeed ||
            record.AnalysisSplit != target.AnalysisSplit || record.Configuration != configuration ||
            record.CameraPoseHash != target.CameraPoseHash || record.ViewInputHash != target.ViewInputHash ||
            record.ReplayInputHash != target.ReplayInputHash || record.SelectionFeatureHash != target.SelectionFeatureHash ||
            record.PrimaryWorkValue != target.PrimaryWorkValue || record.FeatureVector != target.FeatureVector ||
            CommonInput(record) != common || record.ResultHash != resultHash ||
            (record.Status != CpuRecordStatus::Valid && record.Status != CpuRecordStatus::NoWork))
            throw std::runtime_error{"Target input, output or completion identity conflict"};
        if (!unique.emplace(std::pair{record.AbsoluteBlockIndex, record.OrderIndex}, &record).second)
            throw std::runtime_error{"Duplicate strategy position in target"};
    }
    // 行数、合法范围和唯一键共同证明全部块完整，不依赖 CSV 的物理行顺序
    if (unique.size() != expected) throw std::runtime_error{"Incomplete target blocks"};
}

void WriteCpuTargetPairSummaries(std::ostream& output, const std::vector<CpuTargetPairSummary>& summaries)
{
    WriteExperimentCsvRow(output, {"schemaVersion", "dataPurpose", "runId", "scenarioId", "passId", "sampleIndex",
        "selectionRank", "selectionStratum", "replayInputHash", "selectionFeatureHash", "status",
        "warmupCount", "measuredRepeatCount", "parallelWorkerCount", "expectedWarmupRowCount", "expectedMeasuredRowCount",
        "warmupRowCount", "measuredRowCount", "sourceValidationPerformed", "sourceValidationPassed",
        "workerPreparationMs", "rebuildMs", "failure"});
    for (const auto& s : summaries)
    {
        const auto actions = CpuPairActions(s.Target.PassId).size();
        const bool unavailable = s.Target.SampleIndex == 0U;
        WriteExperimentCsvRow(output, {"1", "exploratory", s.RunId, s.Target.ScenarioId,
            std::string{ToString(s.Target.PassId)}, std::to_string(s.Target.SampleIndex),
            std::to_string(s.Target.SelectionRank), s.Target.SelectionStratum, std::to_string(s.Target.ReplayInputHash),
            std::to_string(s.Target.SelectionFeatureHash), s.Status, std::to_string(s.Configuration.WarmupCount),
            std::to_string(s.Configuration.MeasuredRepeatCount), std::to_string(s.Configuration.ParallelWorkerCount),
            std::to_string(unavailable ? 0U : s.Configuration.WarmupCount * actions),
            std::to_string(unavailable ? 0U : s.Configuration.MeasuredRepeatCount * actions),
            std::to_string(s.WarmupRowCount), std::to_string(s.MeasuredRowCount),
            FormatValue(s.SourceValidationPerformed), FormatValue(s.SourceValidationPassed),
            FormatValue(s.WorkerPreparationMilliseconds), FormatValue(s.RebuildMilliseconds), s.Failure});
    }
}

void WriteCpuPairSummary(std::ostream& output, const CpuPairSummary& s)
{
    WriteExperimentCsvRow(output, {"schemaVersion", "dataPurpose", "measurementProtocolVersion", "runId", "status",
        "backend", "buildConfiguration", "scenarioCount", "targetCount", "completedTargetCount", "unavailableGroupCount",
        "warmupRowCount", "measuredRowCount", "calibrationComplete", "timingEnvironmentValid", "totalMs", "failure"});
    WriteExperimentCsvRow(output, {"1", "exploratory", "1", s.RunId, s.Status, s.Backend, s.BuildConfiguration,
        std::to_string(s.ScenarioCount), std::to_string(s.TargetCount), std::to_string(s.CompletedTargetCount),
        std::to_string(s.UnavailableGroupCount), std::to_string(s.WarmupRowCount), std::to_string(s.MeasuredRowCount),
        FormatValue(s.CalibrationComplete), FormatValue(s.TimingEnvironmentValid), FormatValue(s.TotalMilliseconds), s.Failure});
}

void WriteCpuTimingCalibration(std::ostream& output, const std::vector<CpuTimingCalibrationRecord>& records)
{
    WriteExperimentCsvRow(output, {"schemaVersion", "dataPurpose", "measurementProtocolVersion", "runId", "position",
        "kind", "scenarioId", "passId", "sampleIndex", "index", "wallMs", "p50Ms", "p95Ms", "p99Ms",
        "replayInputHash", "resultHash", "correct", "validationPerformed", "diagnosticsDisabledAtExecution", "clockResolutionNs"});
    for (const auto& r : records)
    {
        if ((r.Position != "before" && r.Position != "after") || (r.Kind != "empty" && r.Kind != "no_work") ||
            r.Index >= CpuTimingCalibrationSampleCount || !std::isfinite(r.WallMilliseconds) || r.WallMilliseconds < 0 ||
            r.RunId.empty() || r.ScenarioId.empty())
            throw std::runtime_error{"Invalid timing calibration record"};
        WriteExperimentCsvRow(output, {"1", "exploratory", "1", r.RunId, r.Position, r.Kind, r.ScenarioId,
            r.Kind == "no_work" ? "mergeScore" : "not_applicable", "0", std::to_string(r.Index),
            FormatValue(r.WallMilliseconds), FormatValue(r.P50Milliseconds), FormatValue(r.P95Milliseconds),
            FormatValue(r.P99Milliseconds), std::to_string(r.ReplayInputHash), std::to_string(r.ResultHash),
            FormatValue(r.Correct), FormatValue(r.ValidationPerformed), FormatValue(r.DiagnosticsDisabledAtExecution),
            FormatValue(r.ClockResolutionNanoseconds)});
    }
}
}
