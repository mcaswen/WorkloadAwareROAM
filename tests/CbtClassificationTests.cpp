#include "algorithms/TerrainLodView.h"
#include "algorithms/cbt_2024/CbtClassification.h"

#include <glm/ext/matrix_clip_space.hpp>
#include <glm/ext/matrix_transform.hpp>
#include <glm/geometric.hpp>
#include <glm/trigonometric.hpp>

#include <cmath>
#include <cstdint>
#include <iostream>
#include <string>
#include <utility>

namespace
{
using ParallelRoam::Algorithms::BuildTerrainLodViewInput;
using ParallelRoam::Algorithms::Cbt2024::BuildCbtBaseClassificationTriangles;
using ParallelRoam::Algorithms::Cbt2024::BuildSquareCbtBaseTopology;
using ParallelRoam::Algorithms::Cbt2024::CbtClassificationResult;
using ParallelRoam::Algorithms::Cbt2024::CbtOccupancyCapacity;
using ParallelRoam::Algorithms::Cbt2024::EvaluateCbtClassification;

bool Expect(bool condition, const std::string& message)
{
    if (!condition)
    {
        std::cerr << message << '\n';
    }
    return condition;
}

ParallelRoam::Algorithms::TerrainLodViewInput BuildView(
    std::uint32_t width,
    std::uint32_t height)
{
    const glm::vec3 camera{0.0F, 20.0F, 0.0F};
    const glm::vec3 forward{0.0F, -1.0F, 0.0F};
    const glm::mat4 view = glm::lookAtRH(camera, glm::vec3{0.0F}, glm::vec3{0.0F, 0.0F, -1.0F});
    const glm::mat4 projection = glm::perspectiveRH_ZO(
        glm::radians(60.0F),
        static_cast<float>(width) / static_cast<float>(height),
        0.05F,
        1000.0F);
    return BuildTerrainLodViewInput(view, projection, camera, forward, width, height, true);
}
} // namespace

int main()
{
    const auto topology = BuildSquareCbtBaseTopology(CbtOccupancyCapacity::Capacity128K);
    auto triangles = BuildCbtBaseClassificationTriangles(topology, 10.0F);
    const auto lowResolution = BuildView(320U, 180U);
    const auto highResolution = BuildView(1280U, 720U);

    const auto low = EvaluateCbtClassification(triangles[0], lowResolution, 0.0F, 4U, 14U);
    const auto high = EvaluateCbtClassification(triangles[0], highResolution, 0.0F, 4U, 14U);
    bool passed = true;
    passed &= Expect(low.Result == CbtClassificationResult::Bisect, "visible base triangle was not classified for split");
    passed &= Expect(
        std::abs(high.TriangleAreaPixels / low.TriangleAreaPixels - 16.0F) < 0.01F,
        "pixel area did not scale with drawable area");

    const float resolutionThreshold = (low.TriangleAreaPixels + high.TriangleAreaPixels) * 0.5F;
    const auto lowThresholded = EvaluateCbtClassification(
        triangles[0], lowResolution, resolutionThreshold, 4U, 14U);
    const auto highThresholded = EvaluateCbtClassification(
        triangles[0], highResolution, resolutionThreshold, 4U, 14U);
    passed &= Expect(lowThresholded.Result != CbtClassificationResult::Bisect, "low resolution ignored the area threshold");
    passed &= Expect(highThresholded.Result == CbtClassificationResult::Bisect, "high resolution did not increase classification area");

    std::swap(triangles[0].Positions[0], triangles[0].Positions[2]);
    const auto backFace = EvaluateCbtClassification(triangles[0], highResolution, 1.0F, 4U, 14U);
    passed &= Expect(backFace.Result == CbtClassificationResult::BackFaceCulled, "reverse winding was not back-face culled");
    std::swap(triangles[0].Positions[0], triangles[0].Positions[2]);

    for (glm::vec3& position : triangles[0].Positions)
    {
        position.x += 1000.0F;
    }
    const auto outside = EvaluateCbtClassification(triangles[0], highResolution, 1.0F, 4U, 14U);
    passed &= Expect(outside.Result == CbtClassificationResult::FrustumCulled, "off-screen triangle was not frustum culled");

    const auto baseTriangles = BuildCbtBaseClassificationTriangles(topology, 10.0F);
    const auto depthClamp = EvaluateCbtClassification(baseTriangles[0], highResolution, 0.0F, 4U, 3U);
    passed &= Expect(depthClamp.Result == CbtClassificationResult::TooSmall, "max-depth reduction did not request simplify");
    return passed ? 0 : 1;
}
