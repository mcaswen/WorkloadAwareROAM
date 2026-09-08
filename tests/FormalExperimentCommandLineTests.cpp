#include "benchmark/TerrainLodBenchmarkCommandLine.h"

#include <iostream>
#include <stdexcept>
#include <vector>

namespace
{
using namespace ParallelRoam::Benchmark;

TerrainLodBenchmarkCommandLineParseResult Parse(std::vector<std::string> values)
{
    values.insert(values.begin(), "ParallelROAM");
    std::vector<char*> arguments;
    for (auto& value : values) arguments.push_back(value.data());
    return ParseTerrainLodBenchmarkCommandLine(static_cast<int>(arguments.size()), arguments.data());
}

void Expect(bool condition, const char* message)
{
    if (!condition) throw std::runtime_error(message);
}
} // 匿名命名空间

int main()
{
    try
    {
        const std::vector<std::string> base{"--benchmark", "--profile", "cpu-pilot-inputs",
            "--scenario-manifest", "directory with spaces/scenarios.csv", "--output-dir", "new directory"};
        auto parsed = Parse(base);
        Expect(parsed.Error.empty() && parsed.Options.Profile == BenchmarkProfile::CpuPilotInputs, "Valid preparation rejected");
        Expect(parsed.Options.FormalInput.OutputDirectory == "new directory", "Path with spaces changed");
        auto values = base;
        for (const auto& argument : {"--scenario-id", "test129-a-b512", "--scenario-id", "peking547-a-b20000",
            "--camera-manifest", "camera.csv", "--target-manifest", "targets.csv"}) values.emplace_back(argument);
        parsed = Parse(values);
        Expect(parsed.Error.empty() && parsed.Options.FormalInput.ScenarioIds.size() == 2 &&
            parsed.Options.FormalInput.CameraManifest == "camera.csv" &&
            parsed.Options.FormalInput.TargetManifest == "targets.csv", "Input arguments lost");
        for (const auto& pair : std::vector<std::vector<std::string>>{
            {"--algorithm", "dod"}, {"--pass-workers", "8"}, {"--pass-policy", "serial"},
            {"--csv", "ordinary.csv"}, {"--merge-score-min-parallel-entries", "0"}})
        {
            auto conflict = base;
            conflict.insert(conflict.end(), pair.begin(), pair.end());
            Expect(!Parse(conflict).Error.empty(), "Ordinary override accepted in input preparation");
            conflict = pair;
            conflict.insert(conflict.end(), base.begin(), base.end());
            Expect(!Parse(conflict).Error.empty(), "Option order bypasses conflict check");
        }
        for (const auto& bad : std::vector<std::vector<std::string>>{
            {"--profile", "cpu-pilot-inputs"}, {"--scenario-manifest", "s.csv"},
            {"--profile", "smoke", "--camera-manifest", "c.csv"},
            {"--profile", "formal-cpu-pass-pair"}, {"--output-dir", "--scenario-manifest"}})
            Expect(!Parse(bad).Error.empty(), "Invalid mode or missing value accepted");
        for (const auto& flag : {"--scenario-manifest", "--camera-manifest", "--target-manifest", "--scenario-id", "--output-dir"})
        {
            values = base;
            values.emplace_back(flag);
            Expect(!Parse(values).Error.empty(), "Missing value accepted");
            values.emplace_back("");
            Expect(!Parse(values).Error.empty(), "Empty value accepted");
        }
        values = base;
        values.insert(values.end(), {"--scenario-id", "test129-a-b512", "--scenario-id", "test129-a-b512"});
        Expect(!Parse(values).Error.empty(), "Duplicate scenario ID accepted");
        Expect(Parse({"--profile", "cpu-pilot-inputs", "--help"}).ShowHelp, "Help should not require files");
        Expect(Parse({"--profile", "smoke", "--algorithm", "dod"}).Error.empty(), "Ordinary mode changed");
        const std::vector<std::string> discovery{"--profile", "cpu-workload-discovery", "--scenario-manifest", "s.csv",
            "--camera-manifest", "c.csv", "--output-dir", "new directory"};
        Expect(Parse(discovery).Error.empty() && Parse(discovery).Options.TargetsPerPass == 4U, "Discovery defaults");
        for (const auto count : {"4", "8"})
        {
            values = discovery;
            values.insert(values.end(), {"--targets-per-pass", count});
            Expect(Parse(values).Error.empty(), "Discovery selection count rejected");
        }
        for (const auto& pair : std::vector<std::vector<std::string>>{{"--targets-per-pass", "0"},
            {"--targets-per-pass", "5"}, {"--targets-per-pass", "-4"}, {"--target-manifest", "t.csv"},
            {"--scenario-id", "test129-a-b512"}, {"--algorithm", "dod"}, {"--pass-policy", "serial"}})
        {
            values = discovery;
            values.insert(values.end(), pair.begin(), pair.end());
            Expect(!Parse(values).Error.empty(), "Discovery conflict accepted");
        }
        Expect(!Parse({"--profile", "cpu-pilot-inputs", "--targets-per-pass", "4"}).Error.empty(), "Selection flag leaked");
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
