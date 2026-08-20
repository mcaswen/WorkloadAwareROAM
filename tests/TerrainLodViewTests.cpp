#include "algorithms/TerrainLodView.h"

#include <glm/glm.hpp>

#include <cmath>
#include <cstddef>
#include <iostream>

namespace
{
constexpr float Epsilon = 0.00001F;

bool NearlyEqual(float left, float right)
{
    return std::abs(left - right) <= Epsilon;
}

bool ExpectVector(const glm::vec4& actual, const glm::vec4& expected, const char* label)
{
    if (NearlyEqual(actual.x, expected.x) &&
        NearlyEqual(actual.y, expected.y) &&
        NearlyEqual(actual.z, expected.z) &&
        NearlyEqual(actual.w, expected.w))
    {
        return true;
    }

    std::cerr << label << " mismatch: actual=("
              << actual.x << ", " << actual.y << ", " << actual.z << ", " << actual.w
              << ") expected=("
              << expected.x << ", " << expected.y << ", " << expected.z << ", " << expected.w
              << ")\n";
    return false;
}
} // namespace

int main()
{
    using ParallelRoam::Algorithms::BuildTerrainLodViewInput;
    using ParallelRoam::Algorithms::TerrainLodFrustumPlane;

    const auto planeIndex = [](TerrainLodFrustumPlane plane)
    {
        return static_cast<std::size_t>(plane);
    };

    const glm::mat4 identity{1.0F};
    const auto negativeOneToOne = BuildTerrainLodViewInput(
        identity,
        identity,
        glm::vec3{1.0F, 2.0F, 3.0F},
        glm::vec3{0.0F, 0.0F, -4.0F},
        0U,
        0U,
        false);

    bool passed = true;
    passed &= ExpectVector(
        negativeOneToOne.FrustumPlanes[planeIndex(TerrainLodFrustumPlane::Left)],
        glm::vec4{1.0F, 0.0F, 0.0F, 1.0F},
        "left plane");
    passed &= ExpectVector(
        negativeOneToOne.FrustumPlanes[planeIndex(TerrainLodFrustumPlane::Right)],
        glm::vec4{-1.0F, 0.0F, 0.0F, 1.0F},
        "right plane");
    passed &= ExpectVector(
        negativeOneToOne.FrustumPlanes[planeIndex(TerrainLodFrustumPlane::Near)],
        glm::vec4{0.0F, 0.0F, 1.0F, 1.0F},
        "negative-one-to-one near plane");
    passed &= ExpectVector(
        negativeOneToOne.FrustumPlanes[planeIndex(TerrainLodFrustumPlane::Far)],
        glm::vec4{0.0F, 0.0F, -1.0F, 1.0F},
        "far plane");
    passed &= negativeOneToOne.DrawableWidth == 1U && negativeOneToOne.DrawableHeight == 1U;
    passed &= NearlyEqual(negativeOneToOne.CameraForward.z, -1.0F);

    const auto zeroToOne = BuildTerrainLodViewInput(
        identity,
        identity,
        glm::vec3{0.0F},
        glm::vec3{0.0F},
        1920U,
        1080U,
        true);
    passed &= ExpectVector(
        zeroToOne.FrustumPlanes[planeIndex(TerrainLodFrustumPlane::Near)],
        glm::vec4{0.0F, 0.0F, 1.0F, 0.0F},
        "zero-to-one near plane");
    passed &= NearlyEqual(zeroToOne.CameraForward.x, 0.0F) &&
              NearlyEqual(zeroToOne.CameraForward.y, 0.0F) &&
              NearlyEqual(zeroToOne.CameraForward.z, -1.0F);

    return passed ? 0 : 1;
}
