#pragma once

#include "algorithms/RoamGeometry.h"
#include "algorithms/RoamScreenProjection.h"

#include <glm/glm.hpp>

#include <algorithm>
#include <array>
#include <cstdint>
#include <span>

namespace ParallelRoam::Algorithms::Roam
{
constexpr float DefaultProjectedEdgeWeight = 0.20F;

struct ScreenErrorScoreInput
{
    const std::array<glm::vec3, 3U>& Triangle;
    float WorldError;
    const glm::mat4& ViewProjection;
    const glm::vec4& NearPlane;
    std::span<const glm::vec4> FrustumPlanes;
    std::uint32_t DrawableWidth;
    std::uint32_t DrawableHeight;
    float ProjectedEdgeWeight{DefaultProjectedEdgeWeight};
};

/// 组合视锥可见性、保守几何误差和投影长边密度，返回统一的候选优先级分数。
[[nodiscard]] inline float ComputeScreenErrorScore(const ScreenErrorScoreInput& input)
{
    if (!IsTriangleVisible(input.Triangle, input.WorldError, input.FrustumPlanes))
    {
        return 0.0F;
    }

    const float geometricBoundPixels = ComputeConservativeScreenDistortionPixels({
        input.Triangle,
        input.ViewProjection,
        input.NearPlane,
        input.WorldError,
        input.DrawableWidth,
        input.DrawableHeight,
    });
    if (geometricBoundPixels == ArtificialMaximumScreenError)
    {
        return geometricBoundPixels;
    }

    const float edgeDensityPixels = ComputeProjectedLongestEdgePixels(
        input.Triangle,
        input.ViewProjection,
        input.DrawableWidth,
        input.DrawableHeight) * input.ProjectedEdgeWeight;
    return std::max(geometricBoundPixels, edgeDensityPixels);
}
} // namespace ParallelRoam::Algorithms::Roam
