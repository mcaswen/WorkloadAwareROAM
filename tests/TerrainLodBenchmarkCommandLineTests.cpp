#include "benchmark/TerrainLodBenchmarkCommandLine.h"

#include <array>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace
{
using namespace ParallelRoam::Benchmark;

void Require(bool condition, const std::string& message)
{
    if (!condition)
        throw std::runtime_error{message};
}

TerrainLodBenchmarkCommandLineParseResult Parse(std::vector<std::string> arguments)
{
    arguments.insert(arguments.begin(), "ParallelROAM");
    std::vector<char*> values;
    for (auto& argument : arguments)
        values.push_back(argument.data());
    return ParseTerrainLodBenchmarkCommandLine(static_cast<int>(values.size()), values.data());
}
} // 匿名命名空间

int main()
{
    try
    {
        const auto defaults = Parse({});
        Require(defaults.Succeeded() && !defaults.ShowHelp, "default parse");
        Require(defaults.Options.Algorithm == BenchmarkAlgorithmSelection::All &&
            defaults.Options.Profile == BenchmarkProfile::Smoke &&
            defaults.Options.PassPolicy == BenchmarkPassPolicySelection::Default, "default selections");
        Require(defaults.Options.MergeScoreMinParallelEntryCount == 256U &&
            defaults.Options.SplitScoreMinParallelEntryCount == 256U &&
            defaults.Options.MeshEmitMinParallelTriangleCount == 256U, "default minimums");

        const std::array<std::string, 3> flags{
            "--merge-score-min-parallel-entries", "--split-score-min-parallel-entries",
            "--mesh-min-parallel-triangles"};
        const std::array<std::size_t BenchmarkOptions::*, 3> members{
            &BenchmarkOptions::MergeScoreMinParallelEntryCount,
            &BenchmarkOptions::SplitScoreMinParallelEntryCount,
            &BenchmarkOptions::MeshEmitMinParallelTriangleCount};
        const auto maximum = std::numeric_limits<std::size_t>::max();
        for (std::size_t index = 0U; index < flags.size(); ++index)
        {
            for (std::size_t value : {std::size_t{0U}, std::size_t{1U}, std::size_t{255U},
                    std::size_t{256U}, std::size_t{257U}, maximum})
            {
                const auto parsed = Parse({flags[index], std::to_string(value)});
                Require(parsed.Succeeded() && parsed.Options.*members[index] == value,
                    flags[index] + " valid boundary");
            }
            // 下限允许 SIZE_MAX，但拒绝符号、空白、非十进制和溢出
            for (const std::string& value : std::vector<std::string>{
                    "", "-1", "+1", " 1", "1 ", "1x", "1.5", "0x10", "abc",
                    std::to_string(maximum) + "0"})
                Require(!Parse({flags[index], value}).Succeeded(), flags[index] + " invalid " + value);
            Require(!Parse({flags[index]}).Succeeded(), flags[index] + " missing");
            const auto repeated = Parse({flags[index], "1", flags[index], "257"});
            Require(repeated.Succeeded() && repeated.Options.*members[index] == 257U,
                "last repeated value wins");
        }

        for (bool policyFirst : {false, true})
        {
            std::vector<std::string> args{
                flags[0], "0", flags[1], "255", flags[2], "257", "--csv", "output dir/data.csv"};
            const std::vector<std::string> policy{"--pass-policy", "parallel"};
            args.insert(policyFirst ? args.begin() : args.end(), policy.begin(), policy.end());
            const auto parsed = Parse(args);
            Require(parsed.Succeeded() && parsed.Options.MergeScoreMinParallelEntryCount == 0U &&
                parsed.Options.SplitScoreMinParallelEntryCount == 255U &&
                parsed.Options.MeshEmitMinParallelTriangleCount == 257U &&
                parsed.Options.PassPolicy == BenchmarkPassPolicySelection::MaximumParallelIncremental &&
                parsed.Options.CsvPath.generic_string() == "output dir/data.csv", "order and path");
        }

        for (const auto& [name, selection] : std::vector<std::pair<std::string, BenchmarkAlgorithmSelection>>{
                {"classic", BenchmarkAlgorithmSelection::Classic},
                {"classic_cpu_roam", BenchmarkAlgorithmSelection::Classic},
                {"dod", BenchmarkAlgorithmSelection::DataOriented},
                {"data-oriented", BenchmarkAlgorithmSelection::DataOriented},
                {"data_oriented", BenchmarkAlgorithmSelection::DataOriented},
                {"all", BenchmarkAlgorithmSelection::All}})
        {
            const auto parsed = Parse({"--algorithm", name});
            Require(parsed.Succeeded() && parsed.Options.Algorithm == selection, "algorithm alias " + name);
        }
        for (const auto& [name, selection] : std::vector<std::pair<std::string, BenchmarkPassPolicySelection>>{
                {"default", BenchmarkPassPolicySelection::Default},
                {"serial", BenchmarkPassPolicySelection::SerialIncremental},
                {"serial-incremental", BenchmarkPassPolicySelection::SerialIncremental},
                {"parallel", BenchmarkPassPolicySelection::MaximumParallelIncremental},
                {"maximum-parallel", BenchmarkPassPolicySelection::MaximumParallelIncremental},
                {"maximum-parallel-incremental", BenchmarkPassPolicySelection::MaximumParallelIncremental},
                {"serial-full", BenchmarkPassPolicySelection::SerialFull},
                {"parallel-full", BenchmarkPassPolicySelection::MaximumParallelFull},
                {"maximum-parallel-full", BenchmarkPassPolicySelection::MaximumParallelFull}})
        {
            const auto parsed = Parse({"--pass-policy", name});
            Require(parsed.Succeeded() && parsed.Options.PassPolicy == selection, "policy alias " + name);
        }
        for (const auto& [name, profile] : std::vector<std::pair<std::string, BenchmarkProfile>>{
                {"smoke", BenchmarkProfile::Smoke}, {"standard", BenchmarkProfile::Standard},
                {"budget-reentry", BenchmarkProfile::BudgetReentry},
                {"budget-saturation", BenchmarkProfile::BudgetSaturation},
                {"incremental-emit", BenchmarkProfile::IncrementalEmit},
                {"pass-trace-replay", BenchmarkProfile::PassTraceReplay},
                {"pass-policy-replay", BenchmarkProfile::PassPolicyReplay},
                {"topology-pair-replay", BenchmarkProfile::TopologyPairReplay},
                {"classic-dod-contract", BenchmarkProfile::ClassicDodContract},
                {"pass-crossover-replay", BenchmarkProfile::PassCrossoverReplay},
                {"pass-crossover-stress-replay", BenchmarkProfile::PassCrossoverStressReplay}})
        {
            const auto parsed = Parse({"--profile", name});
            Require(parsed.Succeeded() && parsed.Options.Profile == profile, "profile " + name);
        }

        for (const std::string flag : {"--algorithm", "--profile", "--pass-policy",
                "--parallel-topology-phase", "--csv", "--split-topology-min-candidates",
                "--merge-topology-min-candidates", "--parallel-topology-target-build",
                "--pass-warmups", "--pass-repeats", "--pass-workers", "--pass-targets"})
            Require(!Parse({flag}).Succeeded(), "legacy missing " + flag);
        Require(Parse({"--benchmark", "--benchmark", "--help", "--unknown"}).ShowHelp, "help short circuit");
        Require(Parse({"-h"}).Succeeded() && Parse({"-h"}).ShowHelp, "short help");
        Require(!Parse({"--unknown", "--help"}).Succeeded(), "error before help");
        Require(!Parse({"--profile", "pass-crossover-formal"}).Succeeded(), "formal profile");
        Require(!Parse({"--profile", "workload-discovery"}).Succeeded(), "discovery profile");
        Require(!Parse({"--scene", "anything"}).Succeeded(), "formal option");
        Require(Parse({"--split-topology-min-candidates", "0",
            "--merge-topology-min-candidates", "0", "--parallel-topology-target-build", "3",
            "--parallel-topology-phase", "merge"}).Succeeded(), "legacy topology options");
        for (const std::string flag : {"--pass-warmups", "--pass-repeats", "--pass-workers", "--pass-targets"})
        {
            // 旧入口的零值由运行器判断适用性，原先的 SIZE_MAX 哨兵仍不接受
            Require(Parse({flag, "0"}).Succeeded(), "legacy zero " + flag);
            Require(!Parse({flag, std::to_string(maximum)}).Succeeded(), "legacy sentinel " + flag);
        }
        for (const auto& flag : flags)
            Require(BenchmarkUsage().find(flag) != std::string::npos, "help contains " + flag);
        std::cout << "Benchmark command-line contracts passed\n";
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
