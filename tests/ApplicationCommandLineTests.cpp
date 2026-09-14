#include "app/ApplicationCommandLine.h"

#include <initializer_list>
#include <iostream>
#include <string>
#include <string_view>
#include <vector>

namespace
{
// 构造与真实进程入口相同的可写参数数组
ParallelRoam::App::ApplicationCommandLineParseResult Parse(
    std::initializer_list<std::string_view> arguments)
{
    std::vector<std::string> storage;
    storage.reserve(arguments.size() + 1U);
    storage.emplace_back("ParallelROAM");
    for (const std::string_view argument : arguments)
    {
        storage.emplace_back(argument);
    }

    std::vector<char*> values;
    values.reserve(storage.size());
    for (std::string& argument : storage)
    {
        values.push_back(argument.data());
    }
    return ParallelRoam::App::ParseApplicationCommandLine(
        static_cast<int>(values.size()),
        values.data());
}

bool Require(bool condition, std::string_view message)
{
    if (!condition)
    {
        std::cerr << message << '\n';
    }
    return condition;
}

bool TestLaunchModes()
{
    // 参数值与入口同名时仍应被当作普通值消费
    const auto replay = Parse({"--transactional-platform-replay", "peking", "transactional", "8", "output", "normal"});
    const auto defaultMode = Parse({});
    const auto probeMode = Parse({"--roam-probe"});
    const auto benchmarkMode = Parse({"--algorithm", "dod", "--benchmark"});
    const auto routeLikeLabel = Parse({"--runtime-benchmark-label", "--benchmark"});
    return Require(replay.Options.LaunchMode == ParallelRoam::App::ApplicationLaunchMode::TransactionalPlatformReplay, "Platform replay routing failed") &&
        Require(defaultMode.Succeeded(), "Default command line should parse") &&
        Require(
            defaultMode.Options.LaunchMode ==
                ParallelRoam::App::ApplicationLaunchMode::Application,
            "Default launch mode mismatch") &&
        Require(
            probeMode.Options.LaunchMode == ParallelRoam::App::ApplicationLaunchMode::RoamProbe,
            "Probe launch mode mismatch") &&
        Require(
            benchmarkMode.Options.LaunchMode ==
                ParallelRoam::App::ApplicationLaunchMode::TerrainLodBenchmark,
            "Benchmark launch mode mismatch") &&
        Require(
            routeLikeLabel.Options.LaunchMode ==
                ParallelRoam::App::ApplicationLaunchMode::Application,
            "Option value should not be treated as a launch mode");
}

bool TestRuntimeBenchmarkOptions()
{
    // 一次覆盖所有带值参数，同时验证仍在使用的旧别名
    const auto result = Parse({
        "--runtime-benchmark",
        "--runtime-benchmark-path", "stress",
        "--runtime-benchmark-policy", "parallel-full",
        "--runtime-benchmark-split-topology-min-candidates", "7",
        "--runtime-benchmark-merge-topology-min-candidates", "11",
        "--runtime-benchmark-parallel-topology-target-build", "3",
        "--runtime-benchmark-parallel-topology-phase", "merge",
        "--runtime-benchmark-heightmap", "peking513",
        "--runtime-benchmark-terrain-size", "42.5",
        "--runtime-benchmark-height-scale", "6.25",
        "--runtime-benchmark-depth", "16",
        "--runtime-benchmark-split-threshold", "5.5",
        "--runtime-benchmark-merge-threshold", "2.5",
        "--runtime-benchmark-samples", "64",
        "--runtime-benchmark-warmup-samples", "4",
        "--runtime-benchmark-order-rotation", "1",
        "--runtime-benchmark-upload-pair",
        "--runtime-benchmark-upload-warmups", "2",
        "--runtime-benchmark-upload-repeats", "7",
        "--runtime-benchmark-upload-targets", "3",
        "--runtime-benchmark-label", "parser-test",
    });
    const auto& options = result.Options;
    const auto& runtime = options.RuntimeBenchmark;
    return Require(result.Succeeded(), "Runtime benchmark command line should parse") &&
        Require(options.AutomaticRuntimeBenchmark, "Runtime benchmark flag was not recorded") &&
        Require(options.HasRuntimeBenchmarkOverrides, "Runtime benchmark overrides were not marked") &&
        Require(
            runtime.Path == ParallelRoam::App::RuntimeBenchmarkPath::BudgetSaturation,
            "Runtime benchmark path mismatch") &&
        Require(
            runtime.PassPolicy ==
                ParallelRoam::Algorithms::MakeTerrainLodMaximumSafeParallelFullOutputPolicy(),
            "Runtime benchmark policy alias mismatch") &&
        Require(runtime.SplitTopologyMinParallelCandidateCount == 7U, "Split minimum mismatch") &&
        Require(runtime.MergeTopologyMinParallelCandidateCount == 11U, "Merge minimum mismatch") &&
        Require(runtime.ParallelTopologyTargetBuild == 3U, "Target build mismatch") &&
        Require(
            runtime.ParallelTopologyPhase ==
                ParallelRoam::Algorithms::TerrainLodParallelTopologyPhase::MergeOnly,
            "Parallel topology phase mismatch") &&
        Require(runtime.HeightMapIndex == 1, "Height map alias mismatch") &&
        Require(runtime.TerrainSize == 42.5F, "Terrain size mismatch") &&
        Require(runtime.HeightScale == 6.25F, "Height scale mismatch") &&
        Require(runtime.MaxDepth == 16, "Maximum depth alias mismatch") &&
        Require(runtime.ScreenSpaceSplitThresholdPixels == 5.5F, "Split threshold mismatch") &&
        Require(runtime.ScreenSpaceMergeThresholdPixels == 2.5F, "Merge threshold mismatch") &&
        Require(runtime.SampleCount == 64U, "Sample count mismatch") &&
        Require(runtime.WarmupSampleCount == 4U, "Warmup count mismatch") &&
        Require(runtime.AlgorithmOrderRotation == 1U, "Order rotation mismatch") &&
        Require(runtime.EnableCpuUploadPairReplay, "Upload pair flag mismatch") &&
        Require(runtime.CpuUploadWarmupCount == 2U, "Upload warmup count mismatch") &&
        Require(runtime.CpuUploadRepeatCount == 7U, "Upload repeat count mismatch") &&
        Require(runtime.CpuUploadTargetCount == 3U, "Upload target count mismatch") &&
        Require(runtime.Label == "parser-test", "Label mismatch");
}

bool TestCompatibilityRules()
{
    // 这些行为虽然不够严格，但改变后会破坏已有实验脚本
    const auto duration = Parse({"--runtime-benchmark-duration", "1.5"});
    const auto smallSampleCount = Parse({"--runtime-benchmark-samples", "-9"});
    const auto unknown = Parse({"--future-option", "future-value"});
    return Require(duration.Succeeded(), "Legacy duration should parse") &&
        Require(duration.Options.RuntimeBenchmark.SampleCount == 90U, "Duration conversion mismatch") &&
        Require(smallSampleCount.Succeeded(), "Legacy negative sample count should remain accepted") &&
        Require(smallSampleCount.Options.RuntimeBenchmark.SampleCount == 2U, "Sample count minimum mismatch") &&
        Require(unknown.Succeeded(), "Unknown application options should remain ignored");
}

bool TestValidationErrors()
{
    // 错误必须在创建窗口前返回，避免自动化任务进入主循环
    const auto missingValue = Parse({"--runtime-benchmark-heightmap"});
    const auto invalidInteger = Parse({"--runtime-benchmark-warmup-samples", "many"});
    const auto negativeInteger = Parse({"--runtime-benchmark-order-rotation", "-1"});
    const auto invalidPolicy = Parse({"--runtime-benchmark-policy", "mixed"});
    const auto removedOption = Parse({"--runtime-benchmark-distance-scale", "1"});
    const auto zeroUploadRepeats = Parse({"--runtime-benchmark-upload-repeats", "0"});
    const auto conflictingModes = Parse({"--runtime-benchmark", "--smoke-test"});
    return Require(!missingValue.Succeeded(), "Missing option value should fail") &&
        Require(!invalidInteger.Succeeded(), "Invalid integer should fail") &&
        Require(!negativeInteger.Succeeded(), "Negative non-negative option should fail") &&
        Require(!invalidPolicy.Succeeded(), "Invalid policy should fail") &&
        Require(!removedOption.Succeeded(), "Removed option should fail") &&
        Require(!zeroUploadRepeats.Succeeded(), "Zero upload repeats should fail") &&
        Require(!conflictingModes.Succeeded(), "Conflicting run modes should fail");
}
bool TestAlgorithmSelection()
{
    const auto legacy = Parse({"--runtime-benchmark-algorithms", "dod,classic", "--runtime-benchmark-budget", "50000"});
    const auto duplicate = Parse({"--runtime-benchmark-algorithms", "dod,dod"});
    const auto limit = Parse({"--transactional-prefix", "641"});
    const auto explicitNew = Parse({"--algorithm", "transactional", "--transactional-workers", "8"});
#if defined(PARALLEL_ROAM_TRANSACTIONAL_LOD_RUNTIME)
    const bool availability = explicitNew.Succeeded() &&
        explicitNew.Options.RuntimeBenchmark.Transactional.WorkerCount == 8;
#else
    const bool availability = !explicitNew.Succeeded();
#endif
    return Require(legacy.Succeeded() && legacy.Options.RuntimeBenchmark.AlgorithmSequence.size() == 2 &&
        legacy.Options.RuntimeBenchmark.TriangleBudget == 50000, "Explicit algorithm selection failed") &&
        Require(!duplicate.Succeeded() && !limit.Succeeded() && availability, "Availability or limits invalid");
}
} // 匿名命名空间

int main()
{
    return TestAlgorithmSelection() && TestLaunchModes() &&
        TestRuntimeBenchmarkOptions() &&
        TestCompatibilityRules() &&
        TestValidationErrors()
        ? 0
        : 1;
}
