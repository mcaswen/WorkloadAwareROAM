#include "benchmark/formal/FormalExperimentRunner.h"
#include "benchmark/formal/FormalWorkloadDiscovery.h"
#include "algorithms/data_oriented_roam/DataOrientedRoamPassInput.h"

#include "experiment/formal/FormalExperimentCamera.h"
#include "experiment/formal/FormalExperimentCsv.h"
#include "experiment/formal/FormalExperimentManifest.h"

#include <fstream>
#include <iostream>
#include <stdexcept>

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
} // 命名空间 ParallelRoam::Benchmark::Formal
