#include "experiment/formal/FormalExperimentCamera.h"

#include "algorithms/TerrainLodView.h"

#include <glm/ext/matrix_clip_space.hpp>
#include <glm/ext/matrix_transform.hpp>
#include <glm/trigonometric.hpp>

#include <bit>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace ParallelRoam::Experiment::Formal
{
namespace
{
static_assert(sizeof(float) == 4 && std::numeric_limits<float>::is_iec559);
static_assert(std::endian::native == std::endian::little);

bool IsFinite(const glm::vec3& value)
{
    return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
}

void AppendVector(std::uint64_t& hash, const glm::vec3& value)
{
    Algorithms::AppendTerrainLodHash(hash, value.x);
    Algorithms::AppendTerrainLodHash(hash, value.y);
    Algorithms::AppendTerrainLodHash(hash, value.z);
}

std::uint64_t HashMatrix(const glm::mat4& matrix)
{
    std::uint64_t hash = Algorithms::TerrainLodHashOffset;
    for (glm::length_t column = 0; column < 4; ++column)
    {
        for (glm::length_t row = 0; row < 4; ++row)
        {
            Algorithms::AppendTerrainLodHash(hash, matrix[column][row]);
        }
    }
    return hash;
}

void FillHashes(CameraSample& sample)
{
    const auto input = BuildCameraView(sample);
    sample.CameraPoseHash = Algorithms::TerrainLodHashOffset;
    AppendVector(sample.CameraPoseHash, sample.Position);
    AppendVector(sample.CameraPoseHash, sample.Target);
    AppendVector(sample.CameraPoseHash, sample.Up);
    sample.ViewMatrixHash = HashMatrix(input.View);
    sample.ProjectionNoHash = HashMatrix(input.Projection);
    // 场景名称和预算不参与视图身份，同一地形的不同预算共享逻辑输入
    sample.ViewInputHash = Algorithms::TerrainLodHashOffset;
    Algorithms::AppendTerrainLodHash(sample.ViewInputHash, std::string_view{"cpu-pilot-view-no-v1"});
    Algorithms::AppendTerrainLodHash(sample.ViewInputHash, sample.DrawableWidth);
    Algorithms::AppendTerrainLodHash(sample.ViewInputHash, sample.DrawableHeight);
    Algorithms::AppendTerrainLodHash(sample.ViewInputHash, sample.FovDegrees);
    Algorithms::AppendTerrainLodHash(sample.ViewInputHash, sample.NearPlane);
    Algorithms::AppendTerrainLodHash(sample.ViewInputHash, sample.FarPlane);
    Algorithms::AppendTerrainLodHash(sample.ViewInputHash, sample.CameraPoseHash);
    Algorithms::AppendTerrainLodHash(sample.ViewInputHash, sample.ViewMatrixHash);
    Algorithms::AppendTerrainLodHash(sample.ViewInputHash, sample.ProjectionNoHash);
}
} // 匿名命名空间

Algorithms::TerrainLodViewInput BuildCameraView(const CameraSample& sample)
{
    const glm::vec3 delta = sample.Target - sample.Position;
    if (!IsFinite(sample.Position) || !IsFinite(sample.Target) || !IsFinite(sample.Up) ||
        !std::isfinite(sample.FovDegrees) || !std::isfinite(sample.NearPlane) ||
        !std::isfinite(sample.FarPlane) || sample.FovDegrees <= 0 || sample.FovDegrees >= 180 ||
        sample.NearPlane <= 0 || sample.FarPlane <= sample.NearPlane ||
        sample.DrawableWidth == 0 || sample.DrawableHeight == 0 ||
        glm::dot(delta, delta) < 0.000001F ||
        glm::length(glm::cross(delta, sample.Up)) < 0.000001F)
    {
        throw std::runtime_error("Invalid camera pose or projection");
    }
    // 直接消费冻结行，显式 NO 不受后端的默认深度宏影响
    const auto view = glm::lookAtRH(sample.Position, sample.Target, sample.Up);
    const auto projection = glm::perspectiveRH_NO(glm::radians(sample.FovDegrees),
        static_cast<float>(sample.DrawableWidth) / static_cast<float>(sample.DrawableHeight),
        sample.NearPlane, sample.FarPlane);
    return Algorithms::BuildTerrainLodViewInput(view, projection, sample.Position,
        glm::normalize(delta), sample.DrawableWidth, sample.DrawableHeight, false);
}

CameraSample GenerateCameraSample(const FormalScenario& scenario, std::uint32_t sampleIndex)
{
    if (scenario.TrajectoryId != "A" || scenario.SampleCount != CpuPilotSampleCount ||
        sampleIndex >= CpuPilotSampleCount || !std::isfinite(scenario.Settings.TerrainSize) ||
        scenario.Settings.TerrainSize <= 0)
    {
        throw std::runtime_error("CPU pilot requires trajectory A and sampleIndex 0..63");
    }
    CameraSample sample;
    sample.ScenarioId = scenario.ScenarioId;
    sample.TerrainId = scenario.TerrainId;
    sample.TrajectoryId = scenario.TrajectoryId;
    sample.SampleIndex = sampleIndex;
    sample.DrawableWidth = scenario.DrawableWidth;
    sample.DrawableHeight = scenario.DrawableHeight;
    sample.FovDegrees = scenario.FovDegrees;
    sample.NearPlane = scenario.NearPlane;
    sample.FarPlane = scenario.FarPlane;
    const float radius = scenario.Settings.TerrainSize;
    const float time = static_cast<float>(sampleIndex) / 63.0F;
    const float theta = glm::radians(-5.0F + 10.0F * time);
    sample.Position = {0.85F * radius * std::sin(theta), 0.30F * radius, 0.85F * radius * std::cos(theta)};
    sample.Target = {0.0F, 0.05F * radius, 0.0F};
    const auto forward = glm::normalize(sample.Target - sample.Position);
    sample.Up = std::abs(glm::dot(forward, glm::vec3{0.0F, 1.0F, 0.0F})) > 0.99F
        ? glm::vec3{0.0F, 0.0F, -1.0F} : glm::vec3{0.0F, 1.0F, 0.0F};
    FillHashes(sample);
    return sample;
}

std::vector<CameraSample> GenerateCameraSamples(const std::vector<FormalScenario>& scenarios)
{
    std::vector<CameraSample> samples;
    for (const auto& scenario : scenarios)
    {
        for (std::uint32_t index = 0; index < CpuPilotSampleCount; ++index)
        {
            samples.push_back(GenerateCameraSample(scenario, index));
        }
    }
    return samples;
}

void ValidateCameraSample(const CameraSample& sample, const FormalScenario& scenario)
{
    // 先重算冻结行的派生身份，再比对轨迹预期，避免同时改写姿态和哈希后通过自洽检查
    auto rebuilt = sample;
    FillHashes(rebuilt);
    if (sample != rebuilt || sample != GenerateCameraSample(scenario, sample.SampleIndex))
    {
        throw std::runtime_error("Frozen camera differs from its pose, hashes or trajectory: " +
            sample.ScenarioId + "/" + std::to_string(sample.SampleIndex));
    }
}
} // 命名空间 ParallelRoam::Experiment::Formal
