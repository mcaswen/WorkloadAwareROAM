#pragma once

#include <glm/glm.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>

namespace ParallelRoam::Algorithms::Roam
{
constexpr float ArtificialMaximumScreenError = std::numeric_limits<float>::max();

/// <summary>
/// 论文公式 (2)/(3) 的输入。项目的世界高度轴为 Y，因此 WorldThickness
/// 表示 world-space thickness vector (0, WorldThickness, 0) 的长度。
/// </summary>
struct ConservativeScreenProjectionInput
{
    std::array<glm::vec3, 3U> Triangle{};
    glm::mat4 ViewProjection{1.0F};
    glm::vec4 NearPlane{0.0F};
    float WorldThickness{0.0F};
    std::uint32_t DrawableWidth{1U};
    std::uint32_t DrawableHeight{1U};
};

/// <summary>
/// 检查整个 nested wedgie 是否触碰或穿过 near plane。
/// </summary>
[[nodiscard]] inline bool WedgieIntersectsNearPlane(
    const std::array<glm::vec3, 3U>& triangle,
    const glm::vec4& nearPlane,
    float worldThickness)
{
    const glm::vec3 normal{nearPlane};
    if (glm::dot(normal, normal) <= std::numeric_limits<float>::epsilon())
    {
        return false;
    }

    const float thicknessRadius = std::abs(nearPlane.y) * std::abs(worldThickness);
    for (const glm::vec3& vertex : triangle)
    {
        const float planeDistance = glm::dot(normal, vertex) + nearPlane.w;
        if (planeDistance <= thicknessRadius)
        {
            return true;
        }
    }
    return false;
}

/// <summary>
/// 使用论文公式 (2)/(3) 计算 projected wedgie thickness 的像素上界。
/// p/q/r 与 a/b/c 直接取齐次裁剪坐标的 x/y/w 分量，因此投影矩阵的
/// FOV、aspect、offset 以及透视/正交形式都包含在同一代数中。
/// </summary>
[[nodiscard]] inline float ComputeConservativeScreenDistortionPixels(
    const ConservativeScreenProjectionInput& input)
{
    const float worldThickness = std::abs(input.WorldThickness);
    if (WedgieIntersectsNearPlane(input.Triangle, input.NearPlane, worldThickness))
    {
        return ArtificialMaximumScreenError;
    }

    const glm::vec4 thicknessClip =
        input.ViewProjection * glm::vec4{0.0F, worldThickness, 0.0F, 0.0F};
    const float halfWidth = static_cast<float>(std::max(input.DrawableWidth, 1U)) * 0.5F;
    const float halfHeight = static_cast<float>(std::max(input.DrawableHeight, 1U)) * 0.5F;
    float minimumDenominator = std::numeric_limits<float>::max();
    float maximumNumeratorSquared = 0.0F;

    for (const glm::vec3& vertex : input.Triangle)
    {
        const glm::vec4 clip = input.ViewProjection * glm::vec4{vertex, 1.0F};
        const float denominator = clip.w * clip.w - thicknessClip.w * thicknessClip.w;
        if (!std::isfinite(denominator) || denominator <= std::numeric_limits<float>::epsilon())
        {
            return ArtificialMaximumScreenError;
        }

        const float horizontal = halfWidth * (thicknessClip.x * clip.w - thicknessClip.w * clip.x);
        const float vertical = halfHeight * (thicknessClip.y * clip.w - thicknessClip.w * clip.y);
        const float numeratorSquared = horizontal * horizontal + vertical * vertical;
        if (!std::isfinite(numeratorSquared))
        {
            return ArtificialMaximumScreenError;
        }

        minimumDenominator = std::min(minimumDenominator, denominator);
        maximumNumeratorSquared = std::max(maximumNumeratorSquared, numeratorSquared);
    }

    const float bound = 2.0F * std::sqrt(maximumNumeratorSquared) / minimumDenominator;
    return std::isfinite(bound) ? bound : ArtificialMaximumScreenError;
}

/// <summary>
/// 项目额外 edge-density 项的精确端点投影长度；它不是论文 geometric bound。
/// </summary>
[[nodiscard]] inline float ComputeProjectedLongestEdgePixels(
    const std::array<glm::vec3, 3U>& triangle,
    const glm::mat4& viewProjection,
    std::uint32_t drawableWidth,
    std::uint32_t drawableHeight)
{
    std::array<glm::vec2, 3U> screenPositions{};
    const float halfWidth = static_cast<float>(std::max(drawableWidth, 1U)) * 0.5F;
    const float halfHeight = static_cast<float>(std::max(drawableHeight, 1U)) * 0.5F;
    for (std::size_t index = 0U; index < triangle.size(); ++index)
    {
        const glm::vec4 clip = viewProjection * glm::vec4{triangle[index], 1.0F};
        if (!std::isfinite(clip.w) || std::abs(clip.w) <= std::numeric_limits<float>::epsilon())
        {
            return ArtificialMaximumScreenError;
        }
        screenPositions[index] = glm::vec2{
            halfWidth * clip.x / clip.w,
            halfHeight * clip.y / clip.w,
        };
        if (!std::isfinite(screenPositions[index].x) || !std::isfinite(screenPositions[index].y))
        {
            return ArtificialMaximumScreenError;
        }
    }

    return std::max({
        glm::length(screenPositions[0] - screenPositions[1]),
        glm::length(screenPositions[1] - screenPositions[2]),
        glm::length(screenPositions[2] - screenPositions[0]),
    });
}
} // namespace ParallelRoam::Algorithms::Roam
