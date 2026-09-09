#include "benchmark/formal/FormalTimingCalibration.h"
#include "experiment/ExperimentCsvCodec.h"
#include "experiment/formal/FormalCpuPairCsv.h"
#include "experiment/formal/FormalExperimentCamera.h"
#include "experiment/formal/FormalExperimentManifest.h"

#include <algorithm>
#include <array>
#include <iostream>
#include <limits>
#include <sstream>
#include <stdexcept>

namespace
{
using namespace ParallelRoam::Experiment;
using namespace ParallelRoam::Experiment::Formal;
using namespace ParallelRoam::Algorithms;

void Require(bool condition, const char* message)
{
    if (!condition) throw std::runtime_error{message};
}

template<class Action>
void Reject(Action action, const char* message)
{
    try { action(); }
    catch (const std::exception&) { return; }
    throw std::runtime_error{message};
}

TargetStateRef Target(TerrainLodPassId pass)
{
    TargetStateRef target;
    target.ScenarioId = "test129-a-b4096";
    target.PassId = pass;
    target.SampleIndex = 7U;
    target.SelectionRank = 3U;
    target.SelectionStratum = "high";
    target.PrimaryWorkValue = 80.0F;
    target.FeatureVector = "80;24";
    target.ReplayInputHash = 123U;
    target.SelectionFeatureHash = 124U;
    target.CameraPoseHash = 125U;
    target.ViewInputHash = 126U;
    return target;
}

std::vector<CpuPairRecord> Records(const TargetStateRef& target, const CpuPairConfiguration& configuration)
{
    const auto actions = CpuPairActions(target.PassId);
    constexpr std::array meshOrders{"ABC", "ACB", "BAC", "BCA", "CAB", "CBA"};
    std::vector<CpuPairRecord> records;
    for (std::uint32_t block = 0U; block < configuration.WarmupCount + configuration.MeasuredRepeatCount; ++block)
    {
        const std::string order = actions.size() == 3U ? meshOrders[block % 6U] : (block % 2U == 0U ? "AB" : "BA");
        for (std::uint32_t position = 0U; position < actions.size(); ++position)
        {
            CpuPairRecord row;
            row.RunId = "record-fixture";
            row.ScenarioId = target.ScenarioId;
            row.TerrainId = "test129";
            row.AnalysisSplit = target.AnalysisSplit;
            row.SampleIndex = target.SampleIndex;
            row.PassId = target.PassId;
            row.SelectionRank = target.SelectionRank;
            row.SelectionStratum = target.SelectionStratum;
            row.SelectionSeed = target.SelectionSeed;
            row.SelectorVersion = row.PassInputVersion = 1U;
            row.ReplayInputHash = target.ReplayInputHash;
            row.SelectionFeatureHash = target.SelectionFeatureHash;
            row.CameraPoseHash = target.CameraPoseHash;
            row.ViewInputHash = target.ViewInputHash;
            row.PrimaryWorkValue = target.PrimaryWorkValue;
            row.FeatureVector = target.FeatureVector;
            row.Configuration = configuration;
            row.AbsoluteBlockIndex = block;
            row.IsWarmup = block < configuration.WarmupCount;
            row.RepeatIndex = row.IsWarmup ? block : block - configuration.WarmupCount;
            row.BlockOrder = order;
            row.OrderIndex = position;
            row.RequestedAction = row.ActualAction = actions[order[position] - 'A'];
            const bool parallel = order[position] == 'B';
            row.RequestedWorkerCount = row.ActualWorkerCount = parallel ? configuration.ParallelWorkerCount : 1U;
            row.ExecutionPath = parallel ? "thread_pool" : "caller_thread";
            row.ResultHash = 128U;
            row.ValidationPerformed = row.DiagnosticsDisabledAtExecution = row.Correct = row.Equivalent = true;
            row.Status = CpuRecordStatus::Valid;
            row.WallMilliseconds = position + 0.125;
            records.push_back(std::move(row));
        }
    }
    return records;
}

void CheckRoundTrips()
{
    for (const auto pass : CpuPilotPassIds)
        for (const std::uint32_t warmups : {0U, 1U, 5U})
            for (const std::uint32_t repeats : {1U, 2U, 6U, 30U})
            {
                CpuPairConfiguration config;
                config.WarmupCount = warmups;
                config.MeasuredRepeatCount = repeats;
                const auto target = Target(pass);
                auto records = Records(target, config);
                std::reverse(records.begin(), records.end());
                std::stringstream csv;
                WriteCpuPairCsvHeader(csv);
                for (const auto& record : records) WriteCpuPairCsvRow(csv, record);
                const auto restored = ReadCpuPairCsv(csv, "pair-round-trip");
                ValidateCpuPairTarget(restored, target, "record-fixture", config);
                Require(restored.size() == records.size() && restored.front().WallMilliseconds == records.front().WallMilliseconds,
                    "Pair CSV round trip must retain values and all rows");
            }
}

void CheckCorruptions()
{
    CpuPairConfiguration config;
    config.WarmupCount = 1U;
    config.MeasuredRepeatCount = 6U;
    const auto target = Target(TerrainLodPassId::MeshEmit);
    const auto original = Records(target, config);
    auto rows = original;
    rows.pop_back();
    Reject([&] { ValidateCpuPairTarget(rows, target, "record-fixture", config); }, "Missing action accepted");
    rows = original;
    rows.back() = rows.front();
    Reject([&] { ValidateCpuPairTarget(rows, target, "record-fixture", config); }, "Duplicate block position accepted");
    for (const auto mutation : {0, 1, 2, 3, 4, 5, 6, 7})
    {
        rows = original;
        auto& row = rows.front();
        if (mutation == 0) ++row.ReplayInputHash;
        if (mutation == 1) ++row.ResultHash;
        if (mutation == 2) row.ValidationPerformed = false;
        if (mutation == 3) row.DiagnosticsDisabledAtExecution = false;
        if (mutation == 4) row.WallMilliseconds = std::numeric_limits<double>::quiet_NaN();
        if (mutation == 5) row.Status = CpuRecordStatus::Failed;
        if (mutation == 6) ++row.PreTopologyEditCount;
        if (mutation == 7)
        {
            row.ActualWorkerCount = 0U;
            row.ExecutionPath = "no_work";
            row.Fallback = TerrainLodPassFallbackReason::NoWork;
            row.FallbackDetail = "no_work";
        }
        Reject([&] { ValidateCpuPairTarget(rows, target, "record-fixture", config); }, "Corrupted pair accepted");
    }
    std::stringstream valid;
    WriteCpuPairCsvHeader(valid);
    WriteCpuPairCsvRow(valid, original.front());
    const auto table = ReadExperimentCsv(valid, "version-fixture");
    for (const auto column : {0U, 2U, 3U})
    {
        auto modified = table;
        modified.Rows.front()[column] = "unknown";
        std::stringstream invalid;
        WriteExperimentCsvRow(invalid, modified.Header);
        WriteExperimentCsvRow(invalid, modified.Rows.front());
        Reject([&] { (void)ReadCpuPairCsv(invalid, "bad-version"); }, "Unknown schema or protocol accepted");
    }
    config.WarmupCount = std::numeric_limits<std::uint32_t>::max();
    Reject([&] { ValidateCpuPairConfiguration(config); }, "Count overflow accepted");
    Reject([] { (void)CpuPairActions(TerrainLodPassId::CpuUpload); }, "Upload action accepted");
}

void CheckCalibration()
{
    const auto scenes = LoadScenarioManifest("docs/parallel-roam/cpu-pilot-scenarios-v1.csv",
        std::filesystem::current_path(), {"test129-a-b4096"});
    const auto cameras = GenerateCameraSamples(scenes);
    for (const std::string position : {"before", "after"})
    {
        const auto result = ParallelRoam::Benchmark::Formal::CalibrateCpuPassTiming(scenes.front(), cameras.front(), "calibration-fixture", position);
        Require(result.Complete && result.Records.size() == 2000U, "Real root calibration complete");
        std::stringstream csv;
        WriteCpuTimingCalibration(csv, result.Records);
        for (const auto& row : result.Records)
            Require(row.Correct && row.ClockResolutionNanoseconds > 0.0 && row.P50Milliseconds <= row.P95Milliseconds &&
                row.P95Milliseconds <= row.P99Milliseconds && (row.Kind != "no_work" ||
                    (row.ValidationPerformed && row.DiagnosticsDisabledAtExecution && row.ReplayInputHash != 0U)),
                "Calibration retains real execution and ordered quantiles");
    }
}
}

int main()
{
    try { CheckRoundTrips(); CheckCorruptions(); CheckCalibration(); return 0; }
    catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
