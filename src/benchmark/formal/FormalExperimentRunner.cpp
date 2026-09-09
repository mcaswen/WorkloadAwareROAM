#include "benchmark/formal/FormalExperimentRunner.h"
#include "benchmark/formal/FormalCpuPassBenchmark.h"
#include "benchmark/formal/FormalTimingCalibration.h"
#include "benchmark/formal/FormalWorkloadDiscovery.h"
#include "algorithms/data_oriented_roam/DataOrientedRoamPassInput.h"

#include "experiment/formal/FormalExperimentCamera.h"
#include "experiment/formal/FormalExperimentCsv.h"
#include "experiment/formal/FormalExperimentManifest.h"
#include "tools/PerformanceTimer.h"

#include <algorithm>
#include <chrono>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <map>
#include <random>
#include <set>
#include <sstream>
#include <stdexcept>
#include <tuple>

namespace ParallelRoam::Benchmark::Formal
{
namespace
{
template <typename Writer>
void WriteFile(const std::filesystem::path& path, Writer writer)
{
    std::ofstream output{path, std::ios::binary};
    if (!output) throw std::runtime_error("Cannot create input output: " + path.string());
    writer(output);
    output.close();
    if (!output) throw std::runtime_error("Cannot finish input output: " + path.string());
}
} // 匿名命名空间

int PrepareCpuPilotInputs(const Experiment::Formal::FormalInputRequest& request)
{
    using namespace Experiment::Formal;
    InputPreparationSummary summary;
    summary.TargetStatus = request.TargetManifest.empty() ? "not_provided" : "provided_unchecked";
#if defined(PARALLEL_ROAM_GRAPHICS_API_D3D12)
    summary.Backend = "D3D12";
#else
    summary.Backend = "OpenGL";
#endif
    summary.BuildConfiguration = PARALLEL_ROAM_BUILD_CONFIG;
#if defined(_MSC_FULL_VER)
    summary.Compiler = "MSVC " + std::to_string(_MSC_FULL_VER);
#elif defined(__clang_version__)
    summary.Compiler = "Clang " __clang_version__;
#else
    summary.Compiler = "GCC " __VERSION__;
#endif
    bool ownsDirectory = false;
    const auto writeSummary = [&] {
        WriteFile(request.OutputDirectory / "input-summary.csv", [&](auto& output) {
            WriteInputPreparationSummary(output, summary);
        });
    };
    try
    {
        if (request.ScenarioManifest.empty() || request.OutputDirectory.empty())
            throw std::runtime_error("Scenario manifest and new output directory are required");
        // 仅获得新目录后才允许写入，重复调用不得截断已有输入或失败证据
        ownsDirectory = std::filesystem::create_directory(request.OutputDirectory);
        if (!ownsDirectory) throw std::runtime_error("Output directory already exists");
        writeSummary();
        const auto scenarios = LoadScenarioManifest(request.ScenarioManifest,
            std::filesystem::current_path(), request.ScenarioIds);
        summary.ScenarioCount = scenarios.size();
        summary.ExpectedCameraCount = scenarios.size() * CpuPilotSampleCount;
        const auto cameras = request.CameraManifest.empty()
            ? GenerateCameraSamples(scenarios) : LoadCameraManifest(request.CameraManifest, scenarios);
        summary.CameraCount = cameras.size();
        std::vector<TargetStateRef> targets;
        if (!request.TargetManifest.empty())
        {
            targets = LoadTargetManifest(request.TargetManifest, scenarios, cameras);
            summary.TargetStatus = "references_validated";
            summary.TargetCount = targets.size();
        }
        WriteFile(request.OutputDirectory / "scenarios.csv", [&](auto& output) { WriteScenarioManifest(output, scenarios); });
        WriteFile(request.OutputDirectory / "camera-samples.csv", [&](auto& output) { WriteCameraManifest(output, cameras); });
        // 重新读取实际落盘字节，浮点往返和完整性检查不能只检查内存生成结果
        const auto frozen = LoadCameraManifest(request.OutputDirectory / "camera-samples.csv", scenarios);
        if (frozen != cameras) throw std::runtime_error("Camera serialization changed frozen inputs");
        if (!request.TargetManifest.empty())
        {
            WriteFile(request.OutputDirectory / "target-states.csv", [&](auto& output) { WriteTargetManifest(output, targets); });
            (void)LoadTargetManifest(request.OutputDirectory / "target-states.csv", scenarios, frozen);
        }
        summary.Status = "inputs_validated";
        writeSummary();
        std::cout << "CPU pilot inputs validated: " << summary.ScenarioCount << " scenarios, "
            << summary.CameraCount << " NO camera samples; external asset digest verification required.\n";
        return 0;
    }
    catch (const std::exception& error)
    {
        summary.Status = "failed";
        summary.Error = error.what();
        if (ownsDirectory)
        {
            try { writeSummary(); }
            catch (const std::exception& writeError) { std::cerr << writeError.what() << '\n'; }
        }
        std::cerr << "CPU pilot input preparation failed: " << error.what() << '\n';
        return 1;
    }
}
int DiscoverCpuPilotWorkloads(const Experiment::Formal::FormalInputRequest& request, std::uint32_t targetsPerPass)
{
    using namespace Experiment::Formal;
    CpuDiscoverySummary summary;
    summary.TargetsPerPass = targetsPerPass;
    summary.SelectorVersion = CpuTargetSelectorVersion;
    summary.SelectionSeed = CpuTargetSelectionSeed;
    summary.PassInputVersion = Algorithms::DataOrientedRoam::DataOrientedRoamPassInputVersion;
    bool ownsDirectory = false;
    const auto writeSummary = [&] {
        WriteFile(request.OutputDirectory / "discovery-summary.csv", [&](auto& output) {
            WriteCpuDiscoverySummary(output, summary);
        });
    };
    try
    {
        if (request.ScenarioManifest.empty() || request.CameraManifest.empty() || request.OutputDirectory.empty() ||
            !request.TargetManifest.empty() || !request.ScenarioIds.empty() ||
            (targetsPerPass != 4U && targetsPerPass != 8U))
            throw std::runtime_error{"Discovery requires frozen scenarios/cameras, a new output directory and 4 or 8 targets"};
        ownsDirectory = std::filesystem::create_directory(request.OutputDirectory);
        if (!ownsDirectory)
            throw std::runtime_error{"Output directory already exists"};
        writeSummary();
        const auto scenarios = LoadScenarioManifest(request.ScenarioManifest, std::filesystem::current_path());
        const auto cameras = LoadCameraManifest(request.CameraManifest, scenarios);
        summary.ScenarioCount = scenarios.size();
        summary.ExpectedRecordCount = scenarios.size() * CpuPilotSampleCount * CpuPilotPassIds.size();
        std::ofstream discovery{request.OutputDirectory / "discovery.csv", std::ios::binary};
        if (!discovery)
            throw std::runtime_error{"Cannot create discovery records"};
        WriteCpuDiscoveryCsvHeader(discovery);
        std::vector<TargetStateRef> targets;
        std::vector<CpuTargetCoverageRecord> coverage;
        for (const auto& scenario : scenarios)
        {
            const auto result = DiscoverCpuScenarioWorkloads(scenario, cameras, {targetsPerPass}, [&](const auto& record) {
                WriteCpuDiscoveryCsvRow(discovery, record);
                discovery.flush();
                if (!discovery)
                    throw std::runtime_error{"Cannot finish discovery record"};
                ++summary.RecordCount;
                summary.ValidRecordCount += record.Status == CpuRecordStatus::Valid ? 1U : 0U;
                summary.NoWorkRecordCount += record.Status == CpuRecordStatus::NoWork ? 1U : 0U;
                summary.FailedRecordCount += record.Status == CpuRecordStatus::Failed ? 1U : 0U;
            });
            if (!result.Complete)
                throw std::runtime_error{scenario.ScenarioId + ": " + result.Failure};
            ++summary.CompletedScenarioCount;
            targets.insert(targets.end(), result.Targets.begin(), result.Targets.end());
            coverage.insert(coverage.end(), result.Coverage.begin(), result.Coverage.end());
            writeSummary();
        }
        discovery.close();
        if (!discovery || summary.RecordCount != summary.ExpectedRecordCount)
            throw std::runtime_error{"Discovery records incomplete"};
        for (const auto& group : coverage)
            summary.InsufficientGroupCount += group.SelectedCount < group.RequestedCount ? 1U : 0U;
        WriteFile(request.OutputDirectory / "target-coverage.csv", [&](auto& output) {
            WriteCpuTargetCoverageCsv(output, coverage);
        });
        // 零目标属于覆盖不足且不生成现有加载器无法接受的空清单
        if (!targets.empty())
        {
            const auto pending = request.OutputDirectory / "target-states.pending.csv";
            WriteFile(pending, [&](auto& output) { WriteTargetManifest(output, targets); });
            (void)LoadTargetManifest(pending, scenarios, cameras);
            std::filesystem::rename(pending, request.OutputDirectory / "target-states.csv");
        }
        summary.TargetCount = targets.size();
        summary.TargetStatus = targets.empty() ? "targets_unavailable" : "references_validated";
        summary.Status = "discovery_complete";
        writeSummary();
        std::cout << "CPU workload discovery complete: " << summary.RecordCount << " records, "
            << summary.TargetCount << " targets, " << summary.InsufficientGroupCount << " insufficient groups.\n";
        return 0;
    }
    catch (const std::exception& error)
    {
        summary.Status = "failed";
        summary.Error = error.what();
        if (ownsDirectory)
        {
            try { writeSummary(); }
            catch (const std::exception& writeError) { std::cerr << writeError.what() << '\n'; }
        }
        if (ownsDirectory)
        {
            // 摘要或最终校验失败时保留证据但撤下可供下阶段读取的标准名称
            try
            {
                const auto target = request.OutputDirectory / "target-states.csv";
                if (std::filesystem::exists(target))
                    std::filesystem::rename(target, request.OutputDirectory / "rejected-target-states.csv");
            }
            catch (const std::exception& writeError) { std::cerr << writeError.what() << '\n'; }
        }
        std::cerr << "CPU workload discovery failed: " << error.what() << '\n';
        return 1;
    }
}

int RunCpuPilotPairing(const Experiment::Formal::FormalInputRequest& request,
    const Experiment::Formal::CpuPairSelection& selection)
{
    using namespace Experiment::Formal;
    using TargetKey = std::tuple<std::string, Algorithms::TerrainLodPassId, std::uint32_t>;
    CpuPairSummary summary;
    // 身份在真实进程内生成，脚本再绑定 PID、命令和文件身份；复制目录不能生成新运行
    std::random_device random;
    std::ostringstream identity;
    identity << std::hex << std::chrono::high_resolution_clock::now().time_since_epoch().count()
        << '-' << random() << '-' << random();
    summary.RunId = identity.str();
#if defined(PARALLEL_ROAM_GRAPHICS_API_D3D12)
    summary.Backend = "D3D12";
#else
    summary.Backend = "OpenGL";
#endif
    summary.BuildConfiguration = PARALLEL_ROAM_BUILD_CONFIG;
    bool ownsDirectory = false;
    std::vector<CpuTargetPairSummary> targetSummaries;
    std::vector<CpuTimingCalibrationRecord> calibration;
    Tools::PerformanceTimer total;
    const auto path = [&](const std::string& name) { return request.OutputDirectory / name; };
    const auto writeProgress = [&] {
        summary.TotalMilliseconds = total.ElapsedMilliseconds();
        WriteFile(path("pair-summary.pending.csv"), [&](auto& output) { WriteCpuPairSummary(output, summary); });
        WriteFile(path("target-summary.pending.csv"), [&](auto& output) { WriteCpuTargetPairSummaries(output, targetSummaries); });
    };
    try
    {
        if (request.ScenarioManifest.empty() || request.CameraManifest.empty() || request.TargetManifest.empty() ||
            request.OutputDirectory.empty()) throw std::runtime_error{"Pairing requires all three manifests and a new output directory"};
        ownsDirectory = std::filesystem::create_directory(request.OutputDirectory);
        if (!ownsDirectory) throw std::runtime_error{"Output directory already exists"};
        writeProgress();
        // 完整加载先于任何过滤，防止子集请求绕过冻结相机或目标的来源校验
        const auto allScenarios = LoadScenarioManifest(request.ScenarioManifest, std::filesystem::current_path());
        const auto cameras = LoadCameraManifest(request.CameraManifest, allScenarios);
        const auto allTargets = LoadTargetManifest(request.TargetManifest, allScenarios, cameras);
        const std::set<std::string> requestedIds{request.ScenarioIds.begin(), request.ScenarioIds.end()};
        if (requestedIds.size() != request.ScenarioIds.size()) throw std::runtime_error{"Duplicate scenario selection"};
        if (selection.PassId) (void)CpuPairActions(*selection.PassId);
        if (selection.SampleIndex && (*selection.SampleIndex == 0U || *selection.SampleIndex >= CpuPilotSampleCount ||
                request.ScenarioIds.size() != 1U || !selection.PassId))
            throw std::runtime_error{"Single sample requires one scene and one CPU pass, with index 1..63"};
        std::vector<FormalScenario> scenarios;
        for (const auto& scenario : allScenarios)
            if (requestedIds.empty() || requestedIds.contains(scenario.ScenarioId)) scenarios.push_back(scenario);
        if (scenarios.empty() || (!requestedIds.empty() && scenarios.size() != requestedIds.size()))
            throw std::runtime_error{"Unknown scenario selection"};
        std::set<std::string> selectedIds;
        std::map<std::string, CpuPairConfiguration> configurations;
        for (const auto& scenario : scenarios)
        {
            selectedIds.insert(scenario.ScenarioId);
            configurations.emplace(scenario.ScenarioId, MakeCpuPairConfiguration(scenario, selection));
        }
        std::vector<TargetStateRef> targets;
        for (const auto& target : allTargets)
            if (selectedIds.contains(target.ScenarioId) && (!selection.PassId || target.PassId == *selection.PassId) &&
                (!selection.SampleIndex || target.SampleIndex == *selection.SampleIndex)) targets.push_back(target);
        summary.ScenarioCount = scenarios.size();
        summary.TargetCount = targets.size();
        for (const auto& scenario : scenarios)
            for (const auto pass : CpuPilotPassIds)
                if ((!selection.PassId || pass == *selection.PassId) &&
                    std::none_of(targets.begin(), targets.end(), [&](const auto& target) {
                        return target.ScenarioId == scenario.ScenarioId && target.PassId == pass;
                    }))
                {
                    CpuTargetPairSummary unavailable;
                    unavailable.RunId = summary.RunId;
                    unavailable.Target.ScenarioId = scenario.ScenarioId;
                    unavailable.Target.PassId = pass;
                    unavailable.Configuration = configurations.at(scenario.ScenarioId);
                    unavailable.Status = "targets_unavailable";
                    unavailable.Failure = "no_frozen_target_in_selected_group";
                    targetSummaries.push_back(std::move(unavailable));
                    ++summary.UnavailableGroupCount;
                }
        WriteFile(path("selected-targets.pending.csv"), [&](auto& output) { WriteTargetManifest(output, targets); });
        writeProgress();
        if (targets.empty())
        {
            summary.Status = "targets_unavailable";
            throw std::runtime_error{"Selected request has no frozen targets"};
        }
        const auto root = std::find_if(cameras.begin(), cameras.end(), [&](const auto& camera) {
            return camera.ScenarioId == scenarios.front().ScenarioId && camera.SampleIndex == 0U;
        });
        if (root == cameras.end()) throw std::runtime_error{"Missing calibration root"};
        const auto calibrate = [&](const std::string& position) {
            std::cout << "CPU timing calibration: " << position << '\n' << std::flush;
            const auto result = CalibrateCpuPassTiming(scenarios.front(), *root, summary.RunId, position);
            calibration.insert(calibration.end(), result.Records.begin(), result.Records.end());
            WriteFile(path("timing-calibration.pending.csv"), [&](auto& output) { WriteCpuTimingCalibration(output, calibration); });
            if (!result.Complete) throw std::runtime_error{result.Failure};
            return result.EnvironmentValid;
        };
        const bool beforeValid = calibrate("before");
        std::ofstream measured{path("pair-samples.pending.csv"), std::ios::binary};
        std::ofstream warmup{path("warmup-samples.pending.csv"), std::ios::binary};
        if (!measured || !warmup) throw std::runtime_error{"Cannot create pair records"};
        WriteCpuPairCsvHeader(measured);
        WriteCpuPairCsvHeader(warmup);
        for (const auto& scenario : scenarios)
        {
            if (std::none_of(targets.begin(), targets.end(), [&](const auto& target) {
                    return target.ScenarioId == scenario.ScenarioId;
                })) continue;
            const auto result = MeasureCpuScenarioTargets(scenario, cameras, targets, selection, summary.RunId,
                [&](const auto& row) {
                    auto& output = row.IsWarmup ? warmup : measured;
                    WriteCpuPairCsvRow(output, row);
                    output.flush();
                    if (!output) throw std::runtime_error{"Cannot finish CPU pair record"};
                    if (row.IsWarmup) ++summary.WarmupRowCount;
                    else ++summary.MeasuredRowCount;
                });
            targetSummaries.insert(targetSummaries.end(), result.Targets.begin(), result.Targets.end());
            for (const auto& target : result.Targets)
                summary.CompletedTargetCount += target.Status == "valid" ? 1U : 0U;
            writeProgress();
            if (!result.Complete) throw std::runtime_error{scenario.ScenarioId + ": " + result.Failure};
        }
        measured.close();
        warmup.close();
        if (!measured || !warmup) throw std::runtime_error{"Cannot close CPU pair records"};
        const bool afterValid = calibrate("after");
        summary.CalibrationComplete = true;
        summary.TimingEnvironmentValid = beforeValid && afterValid;
        // 重新核对落盘的完整目标集合，不能只信逐行写入时的内存声明
        std::map<TargetKey, std::vector<CpuPairRecord>> grouped;
        std::size_t readWarmups = 0U;
        std::size_t readMeasured = 0U;
        for (const bool isWarmup : {false, true})
        {
            std::ifstream input{path(isWarmup ? "warmup-samples.pending.csv" : "pair-samples.pending.csv")};
            for (auto& row : ReadCpuPairCsv(input, "pending-pair-records"))
            {
                if (row.IsWarmup != isWarmup) throw std::runtime_error{"Warmup file contains measured rows or vice versa"};
                if (isWarmup) ++readWarmups; else ++readMeasured;
                grouped[{row.ScenarioId, row.PassId, row.SampleIndex}].push_back(std::move(row));
            }
        }
        if (grouped.size() != targets.size() || summary.CompletedTargetCount != targets.size() ||
            readWarmups != summary.WarmupRowCount || readMeasured != summary.MeasuredRowCount)
            throw std::runtime_error{"Pair attempt target or row counts incomplete"};
        for (const auto& target : targets)
            ValidateCpuPairTarget(grouped.at({target.ScenarioId, target.PassId, target.SampleIndex}), target,
                summary.RunId, configurations.at(target.ScenarioId));
        summary.Status = "pairing_internal_complete";
        writeProgress();
        for (const auto* name : {"selected-targets", "pair-samples", "warmup-samples", "target-summary", "timing-calibration", "pair-summary"})
            std::filesystem::rename(path(std::string{name} + ".pending.csv"), path(std::string{name} + ".csv"));
        std::cout << "CPU pairing internally complete: " << summary.RunId << ", " << targets.size() << " targets\n";
        return 0;
    }
    catch (const std::exception& error)
    {
        if (summary.Status != "targets_unavailable") summary.Status = "failed";
        summary.Failure = error.what();
        if (ownsDirectory)
        {
            try
            {
                writeProgress();
                for (const auto* name : {"selected-targets", "pair-samples", "warmup-samples", "target-summary", "timing-calibration", "pair-summary"})
                {
                    const auto standard = path(std::string{name} + ".csv");
                    if (std::filesystem::exists(standard))
                        std::filesystem::rename(standard, path("rejected-" + std::string{name} + ".csv"));
                }
            }
            catch (const std::exception& writeError) { std::cerr << writeError.what() << '\n'; }
        }
        std::cerr << "CPU pairing failed: " << error.what() << '\n';
        return 1;
    }
}
} // 命名空间 ParallelRoam::Benchmark::Formal
