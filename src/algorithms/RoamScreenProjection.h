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
/// ROAM 论文公式 (2)/(3) 所需的三角形、投影和屏幕参数
/// 项目以 Y 轴表示高度，因此 WorldThickness 对应世界空间向量 (0, WorldThickness, 0)
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
/// 检查误差包络是否触碰或穿过近裁剪面；此时常规投影误差公式不再可靠
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
/// 按论文公式 (2)/(3) 计算误差包络投影到屏幕后的像素上界
/// 公式直接使用齐次裁剪坐标的 x/y/w 分量，因此无需单独处理视场角、宽高比、偏移或投影类型
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
/// 计算三角形最长边在屏幕上的实际像素长度，用来补充论文几何误差对大而平坦三角形不敏感的问题
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
} // 命名空间 ParallelRoam::Algorithms::Roam
