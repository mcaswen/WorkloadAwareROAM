#include "experiment/formal/FormalExperimentCamera.h"
#include "experiment/formal/FormalExperimentManifest.h"
#include "experiment/ExperimentCsvCodec.h"

#include <bit>
#include <cmath>
#include <iostream>
#include <map>
#include <set>
#include <stdexcept>

namespace
{
void Expect(bool condition, const char* message)
{
    if (!condition) throw std::runtime_error(message);
}
} // 匿名命名空间

int main(int argc, char** argv)
{
    using namespace ParallelRoam::Experiment;
    using namespace ParallelRoam::Experiment::Formal;
    try
    {
        Expect(argc == 2, "Source directory argument required");
        const std::filesystem::path root{argv[1]};
        const auto scenarios = LoadScenarioManifest(root / "docs/parallel-roam/cpu-pilot-scenarios-v1.csv", root);
        const auto samples = GenerateCameraSamples(scenarios);
        Expect(samples.size() == 384, "Six trajectories must contain 384 samples");
        std::map<std::pair<std::string, std::uint32_t>, CameraSample> byTerrain;
        for (const auto& scenario : scenarios)
        {
            std::set<std::uint64_t> poses;
            for (std::uint32_t index = 0; index < 64; ++index)
            {
                const auto sample = GenerateCameraSample(scenario, index);
                ValidateCameraSample(sample, scenario);
                Expect(poses.insert(sample.CameraPoseHash).second, "Trajectory A repeats a pose");
                const auto [previous, inserted] = byTerrain.emplace(std::make_pair(scenario.TerrainId, index), sample);
                if (!inserted)
                {
                    Expect(previous->second.CameraPoseHash == sample.CameraPoseHash &&
                        previous->second.ViewInputHash == sample.ViewInputHash, "Budget changed camera identity");
                }
                const auto view = BuildCameraView(sample);
                Expect(view.DrawableWidth == 1280 && view.DrawableHeight == 720, "Drawable dimensions changed");
                const glm::vec4 nearClip = view.Projection * glm::vec4{0, 0, -0.1F, 1};
                const glm::vec4 farClip = view.Projection * glm::vec4{0, 0, -500.0F, 1};
                Expect(std::abs(nearClip.z / nearClip.w + 1.0F) < 0.00001F, "Near plane is not NO -1");
                Expect(std::abs(farClip.z / farClip.w - 1.0F) < 0.00001F, "Far plane is not NO +1");
                const glm::vec4 origin = view.View * glm::vec4{sample.Position, 1};
                Expect(glm::length(glm::vec3{origin}) < 0.00001F, "Camera position does not map to view origin");
                for (const float value : {sample.Position.x, sample.Position.y, sample.Position.z,
                    sample.Target.x, sample.Target.y, sample.Target.z, sample.NearPlane})
                {
                    const float parsed = ParseExperimentCsvFloat(FormatExperimentCsvFloat(value));
                    Expect(std::bit_cast<std::uint32_t>(parsed) == std::bit_cast<std::uint32_t>(value),
                        "Frozen float round trip changed bits");
                }
            }
            const bool small = scenario.TerrainId == "test129";
            const auto first = GenerateCameraSample(scenario, 0);
            const auto last = GenerateCameraSample(scenario, 63);
            // 独立协议坐标用于检查比例、弧度和首末采样，不复写生成公式
            Expect(std::abs(first.Position.x - (small ? -2.222471F : -5.926591F)) < 0.00001F,
                "Protocol first x mismatch");
            Expect(std::abs(first.Position.z - (small ? 25.402965F : 67.741239F)) < 0.00002F,
                "Protocol first z mismatch");
            Expect(first.Position.y == (small ? 9.0F : 24.0F) && first.Target.y == (small ? 1.5F : 4.0F),
                "Protocol height mismatch");
            Expect(last.Position.x == -first.Position.x && last.Position.z == first.Position.z,
                "Trajectory endpoints are not symmetric");
            auto corrupted = first;
            corrupted.Position.x = std::nextafter(corrupted.Position.x, 0.0F);
            bool rejected = false;
            try { ValidateCameraSample(corrupted, scenario); }
            catch (const std::exception&) { rejected = true; }
            Expect(rejected, "One-ULP camera edit accepted");
        }
        for (const std::uint32_t bits : {0U, 0x80000000U, 1U, 0x007fffffU, 0x00800000U, 0x7f7fffffU, 0xff7fffffU})
        {
            const auto value = std::bit_cast<float>(bits);
            Expect(std::bit_cast<std::uint32_t>(ParseExperimentCsvFloat(FormatExperimentCsvFloat(value))) == bits,
                "Boundary float round trip changed bits");
        }
        std::cout << "384 camera inputs, protocol coordinates, NO depth and binary32 round trips verified.\n";
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
