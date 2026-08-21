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

/// 先剔除视锥外三角形，再合并几何误差与屏幕长边大小，得到细分/合并队列共用的优先级分数
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
} // 命名空间 ParallelRoam::Algorithms::Roam
