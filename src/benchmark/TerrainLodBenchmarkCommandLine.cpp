#include "benchmark/TerrainLodBenchmarkCommandLine.h"

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
    // 接受少量别名
    // 方便脚本里使用短名
    // 也兼容接口内部算法名
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
    if (value == "cpu-pilot-inputs")
    {
        outProfile = BenchmarkProfile::CpuPilotInputs;
        return true;
    }
    // budget-reentry 专门覆盖硬预算满载后的原地转向
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

        // 数量选项共用完整十进制解析，仅 --pass-* 计数拒绝 size_t 最大值
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
            hasOrdinaryOverrides = true;
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
    if (options.Profile == BenchmarkProfile::CpuPilotInputs)
    {
        const auto& input = options.FormalInput;
        const std::set<std::string> selected{input.ScenarioIds.begin(), input.ScenarioIds.end()};
        if (hasOrdinaryOverrides || input.ScenarioManifest.empty() || input.OutputDirectory.empty() ||
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
           "[--scenario-id id]... [--camera-manifest path] [--target-manifest path]\n";
}
} // 命名空间 ParallelRoam::Benchmark
