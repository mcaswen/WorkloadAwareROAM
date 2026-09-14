#include "benchmark/TerrainLodBenchmarkCommandLine.h"
#include "algorithms/TerrainLodAlgorithmRegistry.h"

#include <charconv>
#include <limits>
#include <set>
#include <string_view>

namespace ParallelRoam::Benchmark
{
namespace
{
bool ParseSize(std::string_view value, std::size_t& output)
{
    if (value.empty())
    {
        return false;
    }
    // 必须完整消费非负十进制文本，不能把带符号、空白或尾随字符的值当作有效数量
    const auto [end, error] = std::from_chars(value.data(), value.data() + value.size(), output);
    return error == std::errc{} && end == value.data() + value.size();
}

bool ParseParallelTopologyPhase(
    std::string_view value,
    Algorithms::TerrainLodParallelTopologyPhase& output)
{
    if (value == "both")
    {
        output = Algorithms::TerrainLodParallelTopologyPhase::Both;
        return true;
    }
    if (value == "split")
    {
        output = Algorithms::TerrainLodParallelTopologyPhase::SplitOnly;
        return true;
    }
    if (value == "merge")
    {
        output = Algorithms::TerrainLodParallelTopologyPhase::MergeOnly;
        return true;
    }
    return false;
}

bool ParseAlgorithm(std::string_view value, BenchmarkAlgorithmSelection& outSelection)
{
    if (value == "transactional" && Algorithms::IsTerrainLodAlgorithmAvailable(Algorithms::TerrainLodAlgorithmId::TransactionalCpuLod))
    { outSelection = BenchmarkAlgorithmSelection::Transactional; return true; }

    // 保留脚本常用短名和已有算法名，使旧命令仍能选择同一个实现
    if (value == "classic" || value == "classic_cpu_roam")
    {
        outSelection = BenchmarkAlgorithmSelection::Classic;
        return true;
    }

    if (value == "dod" || value == "data-oriented" || value == "data_oriented")
    {
        outSelection = BenchmarkAlgorithmSelection::DataOriented;
        return true;
    }

    if (value == "all")
    {
        outSelection = BenchmarkAlgorithmSelection::All;
        return true;
    }

    return false;
}

bool ParsePassPolicy(std::string_view value, BenchmarkPassPolicySelection& outSelection)
{
    if (value == "default")
    {
        outSelection = BenchmarkPassPolicySelection::Default;
        return true;
    }
    if (value == "serial-incremental" || value == "serial")
    {
        outSelection = BenchmarkPassPolicySelection::SerialIncremental;
        return true;
    }
    if (value == "maximum-parallel-incremental" ||
        value == "maximum-parallel" || value == "parallel")
    {
        outSelection = BenchmarkPassPolicySelection::MaximumParallelIncremental;
        return true;
    }
    if (value == "serial-full")
    {
        outSelection = BenchmarkPassPolicySelection::SerialFull;
        return true;
    }
    if (value == "maximum-parallel-full" || value == "parallel-full")
    {
        outSelection = BenchmarkPassPolicySelection::MaximumParallelFull;
        return true;
    }
    return false;
}

bool ParseProfile(std::string_view value, BenchmarkProfile& outProfile)
{
    if (value == "cpu-pass-pair-pilot")
    {
        outProfile = BenchmarkProfile::CpuPassPairPilot;
        return true;
    }
    if (value == "cpu-workload-discovery")
    {
        outProfile = BenchmarkProfile::CpuWorkloadDiscovery;
        return true;
    }
    if (value == "cpu-pilot-inputs")
    {
        outProfile = BenchmarkProfile::CpuPilotInputs;
        return true;
    }
    if (value == "smoke")
    {
        outProfile = BenchmarkProfile::Smoke;
        return true;
    }

    if (value == "standard")
    {
        outProfile = BenchmarkProfile::Standard;
        return true;
    }

    if (value == "budget-reentry")
    {
        // 该场景在预算饱和后原地转向，用于观察预算能否重新分配到新视野
        outProfile = BenchmarkProfile::BudgetReentry;
        return true;
    }

    if (value == "budget-saturation")
    {
        outProfile = BenchmarkProfile::BudgetSaturation;
        return true;
    }

    if (value == "incremental-emit")
    {
        outProfile = BenchmarkProfile::IncrementalEmit;
        return true;
    }

    if (value == "pass-trace-replay")
    {
        outProfile = BenchmarkProfile::PassTraceReplay;
        return true;
    }

    if (value == "pass-policy-replay")
    {
        outProfile = BenchmarkProfile::PassPolicyReplay;
        return true;
    }

    if (value == "topology-pair-replay")
    {
        outProfile = BenchmarkProfile::TopologyPairReplay;
        return true;
    }

    if (value == "classic-dod-contract")
    {
        outProfile = BenchmarkProfile::ClassicDodContract;
        return true;
    }

    if (value == "pass-crossover-replay")
    {
        outProfile = BenchmarkProfile::PassCrossoverReplay;
        return true;
    }

    if (value == "pass-crossover-stress-replay")
    {
        outProfile = BenchmarkProfile::PassCrossoverStressReplay;
        return true;
    }

    return false;
}

} // 匿名命名空间

TerrainLodBenchmarkCommandLineParseResult ParseTerrainLodBenchmarkCommandLine(
    int argc,
    char** argv)
{
    TerrainLodBenchmarkCommandLineParseResult result{};
    auto& options = result.Options;
    bool hasOrdinaryOverrides = false;
    bool hasInputArguments = false;
    bool hasTargetSelectionArgument = false;
    bool hasExperimentCounts = false;
    bool hasPairSelection = false;
    bool duplicateOption = false;
    std::set<std::string_view> seenOptions;
    for (int index = 1; index < argc; ++index)
    {
        const std::string_view argument{argv[index]};
        if (argument == "--benchmark")
        {
            continue;
        }
        if (argument == "--help" || argument == "-h")
        {
            // 保留按位置处理帮助的行为：此前的错误优先，此后的参数不再读取
            result.ShowHelp = true;
            return result;
        }

        if (argument != "--scenario-id" && !seenOptions.insert(argument).second) duplicateOption = true;
        if (argument == "--pass-id" || argument == "--sample-index")
        {
            hasPairSelection = true;
            if (index + 1 >= argc)
            {
                result.Error = std::string{argument} + " requires a value.";
                return result;
            }
            const std::string_view value{argv[++index]};
            if (argument == "--pass-id")
            {
                bool valid = value == "all";
                options.CpuPair.PassId.reset();
                for (const auto pass : Experiment::Formal::CpuPilotPassIds)
                    if (Algorithms::ToString(pass) == value) { options.CpuPair.PassId = pass; valid = true; }
                if (!valid) { result.Error = "--pass-id requires all or a CPU pass name."; return result; }
            }
            else
            {
                std::size_t sample = 0U;
                if (!ParseSize(value, sample) || sample == 0U || sample >= Experiment::Formal::CpuPilotSampleCount)
                { result.Error = "--sample-index requires 1..63."; return result; }
                options.CpuPair.SampleIndex = static_cast<std::uint32_t>(sample);
            }
            continue;
        }

        if (argument == "--scenario-manifest" || argument == "--camera-manifest" ||
            argument == "--target-manifest" || argument == "--output-dir" || argument == "--scenario-id")
        {
            hasInputArguments = true;
            if (index + 1 >= argc || std::string_view{argv[index + 1]}.empty() ||
                std::string_view{argv[index + 1]}.starts_with("--"))
            {
                result.Error = std::string{argument} + " requires a value.";
                return result;
            }
            const std::string value{argv[++index]};
            if (argument == "--scenario-manifest") options.FormalInput.ScenarioManifest = value;
            else if (argument == "--camera-manifest") options.FormalInput.CameraManifest = value;
            else if (argument == "--target-manifest") options.FormalInput.TargetManifest = value;
            else if (argument == "--output-dir") options.FormalInput.OutputDirectory = value;
            else options.FormalInput.ScenarioIds.push_back(value);
            continue;
        }

        if (argument == "--targets-per-pass")
        {
            hasTargetSelectionArgument = true;
            if (index + 1 >= argc || !ParseSize(argv[++index], options.TargetsPerPass) ||
                (options.TargetsPerPass != 4U && options.TargetsPerPass != 8U))
            {
                result.Error = "--targets-per-pass requires 4 or 8.";
                return result;
            }
            continue;
        }

        // 各数量选项共用整数解析，允许范围仍按参数含义分别检查
        std::size_t* sizeOption = nullptr;
        if (argument == "--merge-score-min-parallel-entries")
        {
            sizeOption = &options.MergeScoreMinParallelEntryCount;
        }
        else if (argument == "--split-score-min-parallel-entries")
        {
            sizeOption = &options.SplitScoreMinParallelEntryCount;
        }
        else if (argument == "--mesh-min-parallel-triangles")
        {
            sizeOption = &options.MeshEmitMinParallelTriangleCount;
        }
        else if (argument == "--split-topology-min-candidates")
        {
            sizeOption = &options.SplitTopologyMinParallelCandidateCount;
        }
        else if (argument == "--merge-topology-min-candidates")
        {
            sizeOption = &options.MergeTopologyMinParallelCandidateCount;
        }
        else if (argument == "--parallel-topology-target-build")
        {
            sizeOption = &options.ParallelTopologyTargetBuild;
        }
        else if (argument == "--pass-warmups")
        {
            sizeOption = &options.PassExperimentWarmupCount;
        }
        else if (argument == "--pass-repeats")
        {
            sizeOption = &options.PassExperimentRepeatCount;
        }
        else if (argument == "--pass-workers")
        {
            sizeOption = &options.PassExperimentWorkerCount;
        }
        else if (argument == "--pass-targets")
        {
            sizeOption = &options.PassExperimentTargetCount;
        }
        const bool passExperimentSize = argument == "--pass-warmups" || argument == "--pass-repeats" ||
            argument == "--pass-workers" || argument == "--pass-targets";

        if (sizeOption != nullptr)
        {
            const bool sharedCount = passExperimentSize && argument != "--pass-targets";
            hasExperimentCounts |= sharedCount;
            hasOrdinaryOverrides |= !sharedCount;
            // 预热、重复、线程和目标数量保留最大值作为无效哨兵，下限参数可以使用该值
            if (index + 1 >= argc || !ParseSize(argv[++index], *sizeOption) ||
                (passExperimentSize && *sizeOption == std::numeric_limits<std::size_t>::max()))
            {
                result.Error = "Invalid or missing " + std::string{argument} + " value.";
                return result;
            }
            continue;
        }

        if (argument == "--algorithm" || argument == "--profile" ||
            argument == "--pass-policy" || argument == "--parallel-topology-phase" ||
            argument == "--csv")
        {
            hasOrdinaryOverrides |= argument != "--profile";
            if (index + 1 >= argc)
            {
                result.Error = std::string{argument} + " requires a value.";
                return result;
            }
            const std::string_view value{argv[++index]};
            bool valid = true;
            if (argument == "--algorithm")
            {
                valid = ParseAlgorithm(value, options.Algorithm);
            }
            else if (argument == "--profile")
            {
                valid = ParseProfile(value, options.Profile);
            }
            else if (argument == "--pass-policy")
            {
                valid = ParsePassPolicy(value, options.PassPolicy);
            }
            else if (argument == "--parallel-topology-phase")
            {
                valid = ParseParallelTopologyPhase(value, options.ParallelTopologyPhase);
            }
            else
            {
                options.CsvPath = value;
            }
            if (!valid)
            {
                result.Error = "Invalid " + std::string{argument} + " value: " + std::string{value};
                return result;
            }
            continue;
        }

        result.Error = "Unknown benchmark argument: " + std::string{argument};
        return result;
    }
    if (options.Profile == BenchmarkProfile::CpuPassPairPilot)
    {
        const auto& input = options.FormalInput;
        const std::set<std::string> selected{input.ScenarioIds.begin(), input.ScenarioIds.end()};
        const auto maximum = std::numeric_limits<std::uint32_t>::max();
        if (hasOrdinaryOverrides || hasTargetSelectionArgument || duplicateOption || input.ScenarioManifest.empty() ||
            input.CameraManifest.empty() || input.TargetManifest.empty() || input.OutputDirectory.empty() ||
            selected.size() != input.ScenarioIds.size() ||
            (options.CpuPair.SampleIndex && (selected.size() != 1U || !options.CpuPair.PassId)) ||
            options.PassExperimentRepeatCount == 0U || options.PassExperimentWorkerCount == 0U ||
            options.PassExperimentRepeatCount > maximum || options.PassExperimentWorkerCount > maximum ||
            options.PassExperimentWarmupCount > maximum - options.PassExperimentRepeatCount)
            result.Error = "cpu-pass-pair-pilot requires three manifests, a new directory, unique selections and valid counts without ordinary overrides.";
        else
        {
            // 显式零预热与未出现参数必须区分，避免默认值覆盖冻结场景
            if (seenOptions.contains("--pass-warmups")) options.CpuPair.WarmupCount = static_cast<std::uint32_t>(options.PassExperimentWarmupCount);
            if (seenOptions.contains("--pass-repeats")) options.CpuPair.MeasuredRepeatCount = static_cast<std::uint32_t>(options.PassExperimentRepeatCount);
            if (seenOptions.contains("--pass-workers")) options.CpuPair.ParallelWorkerCount = static_cast<std::uint32_t>(options.PassExperimentWorkerCount);
        }
    }
    else if (hasPairSelection)
    {
        result.Error = "--pass-id and --sample-index require --profile cpu-pass-pair-pilot.";
    }
    else if (options.Profile == BenchmarkProfile::CpuWorkloadDiscovery)
    {
        const auto& input = options.FormalInput;
        if (hasOrdinaryOverrides || hasExperimentCounts || input.ScenarioManifest.empty() || input.CameraManifest.empty() ||
            input.OutputDirectory.empty() || !input.TargetManifest.empty() || !input.ScenarioIds.empty())
            result.Error = "cpu-workload-discovery requires frozen scenarios/cameras and a new output directory without overrides.";
    }
    else if (hasTargetSelectionArgument)
    {
        result.Error = "--targets-per-pass requires --profile cpu-workload-discovery.";
    }
    else if (options.Profile == BenchmarkProfile::CpuPilotInputs)
    {
        const auto& input = options.FormalInput;
        const std::set<std::string> selected{input.ScenarioIds.begin(), input.ScenarioIds.end()};
        if (hasOrdinaryOverrides || hasExperimentCounts || input.ScenarioManifest.empty() || input.OutputDirectory.empty() ||
            selected.size() != input.ScenarioIds.size())
        {
            result.Error = "cpu-pilot-inputs requires scenario-manifest/output-dir, unique scenario IDs and no ordinary overrides.";
        }
    }
    else if (hasInputArguments)
    {
        result.Error = "Manifest and output-dir arguments require --profile cpu-pilot-inputs.";
    }
    return result;
}

std::string BenchmarkUsage()
{
    return "Usage: ParallelROAM --benchmark [--algorithm classic|dod|all] "
           "[--profile smoke|budget-reentry|budget-saturation|incremental-emit|pass-trace-replay|pass-policy-replay|topology-pair-replay|classic-dod-contract|pass-crossover-replay|pass-crossover-stress-replay|standard] "
           "[--pass-policy default|serial-incremental|maximum-parallel-incremental|serial-full|maximum-parallel-full] "
           "[--merge-score-min-parallel-entries count] [--split-score-min-parallel-entries count] "
           "[--mesh-min-parallel-triangles count] "
           "[--split-topology-min-candidates count] [--merge-topology-min-candidates count] "
           "[--parallel-topology-target-build build] [--parallel-topology-phase both|split|merge] "
           "[--pass-warmups count] [--pass-repeats count] [--pass-workers count] [--pass-targets count] "
           "[--csv path]\n"
           "CPU pilot input preparation: --benchmark --profile cpu-pilot-inputs "
           "--scenario-manifest path --output-dir new-directory "
           "[--scenario-id id]... [--camera-manifest path] [--target-manifest path]\n"
           "CPU workload discovery: --benchmark --profile cpu-workload-discovery "
           "--scenario-manifest frozen-path --camera-manifest frozen-path --output-dir new-directory "
           "[--targets-per-pass 4|8]\n"
           "CPU pass pairing: --benchmark --profile cpu-pass-pair-pilot "
           "--scenario-manifest frozen-path --camera-manifest frozen-path --target-manifest frozen-path "
           "--output-dir new-directory [--scenario-id id]... "
           "[--pass-id all|mergeScore|mergeTopology|splitScore|splitTopology|meshEmit] "
           "[--sample-index 1..63] [--pass-warmups count] [--pass-repeats count] [--pass-workers count]\n";
}
} // 命名空间 ParallelRoam::Benchmark
