#pragma once

#include "terrain/HeightMap.h"

#include <glm/glm.hpp>

#include <algorithm>
#include <array>
#include <limits>
#include <span>

namespace ParallelRoam::Algorithms::Roam
{
struct TerrainWorldSample
{
    glm::vec3 Position{0.0F};
    float Height{0.0F};
};

template <typename Domain>
struct TriangleDomainChildren
{
    Domain Left;
    Domain Right;
};

/// 沿 A-B base edge 二分 ROAM 三角形，并保持现有绕序约定。
template <typename Domain>
[[nodiscard]] inline TriangleDomainChildren<Domain> SplitTriangleDomain(const Domain& domain)
{
    const glm::vec2 midpoint = (domain.A + domain.B) * 0.5F;
    return TriangleDomainChildren<Domain>{
        Domain{domain.C, domain.A, midpoint},
        Domain{domain.B, domain.C, midpoint},
    };
}

/// 返回 base edge 中点高度相对两端点线性插值的有符号位移。
template <typename Domain>
[[nodiscard]] inline float ComputeBaseMidpointDisplacement(
    const Terrain::HeightMap& heightMap,
    const Domain& domain)
{
    const float heightA = heightMap.SampleBilinear(domain.A.x, domain.A.y);
    const float heightB = heightMap.SampleBilinear(domain.B.x, domain.B.y);
    const glm::vec2 midpoint = (domain.A + domain.B) * 0.5F;
    const float midpointHeight = heightMap.SampleBilinear(midpoint.x, midpoint.y);
    return midpointHeight - (heightA + heightB) * 0.5F;
}

/// 对归一化 HeightMap 坐标采样一次，并同时返回高度与世界坐标。
[[nodiscard]] inline TerrainWorldSample SampleTerrainWorld(
    const Terrain::HeightMap& heightMap,
    const glm::vec2& uv,
    float terrainSize,
    float heightScale)
{
    const float height = heightMap.SampleBilinear(uv.x, uv.y);
    return TerrainWorldSample{
        glm::vec3{
            (uv.x - 0.5F) * terrainSize,
            height * heightScale,
            (uv.y - 0.5F) * terrainSize,
        },
        height,
    };
}

/// 将归一化 HeightMap 坐标映射到地形世界坐标。
[[nodiscard]] inline glm::vec3 DomainToWorld(
    const Terrain::HeightMap& heightMap,
    const glm::vec2& uv,
    float terrainSize,
    float heightScale)
{
    return SampleTerrainWorld(heightMap, uv, terrainSize, heightScale).Position;
}

/// 使用 HeightMap 四点差分估计世界空间地形法线。
[[nodiscard]] inline glm::vec3 SampleHeightGradientNormal(
    const Terrain::HeightMap& heightMap,
    const glm::vec2& uv,
    float terrainSize,
    float heightScale)
{
    const float stepU = 1.0F / static_cast<float>(std::max(heightMap.Width() - 1, 1));
    const float stepV = 1.0F / static_cast<float>(std::max(heightMap.Height() - 1, 1));
    const float left = heightMap.SampleBilinear(uv.x - stepU, uv.y);
    const float right = heightMap.SampleBilinear(uv.x + stepU, uv.y);
    const float down = heightMap.SampleBilinear(uv.x, uv.y - stepV);
    const float up = heightMap.SampleBilinear(uv.x, uv.y + stepV);

    const glm::vec3 tangentX{stepU * 2.0F * terrainSize, (right - left) * heightScale, 0.0F};
    const glm::vec3 tangentZ{0.0F, (up - down) * heightScale, stepV * 2.0F * terrainSize};
    const glm::vec3 normal = glm::cross(tangentZ, tangentX);
    if (glm::dot(normal, normal) <= std::numeric_limits<float>::epsilon())
    {
        return glm::vec3{0.0F, 1.0F, 0.0F};
    }

    return glm::normalize(normal);
}

/// 使用误差扩张后的三角形 AABB 执行 split/merge 共用的保守视锥测试。
[[nodiscard]] inline bool IsTriangleVisible(
    const std::array<glm::vec3, 3U>& triangle,
    float worldError,
    std::span<const glm::vec4> frustumPlanes)
{
    glm::vec3 minimum = glm::min(triangle[0], glm::min(triangle[1], triangle[2]));
    glm::vec3 maximum = glm::max(triangle[0], glm::max(triangle[1], triangle[2]));
    minimum.y -= worldError;
    maximum.y += worldError;
    const glm::vec3 center = (minimum + maximum) * 0.5F;
    const glm::vec3 extents = (maximum - minimum) * 0.5F;

    for (const glm::vec4& plane : frustumPlanes)
    {
        const glm::vec3 normal{plane};
        const float centerDistance = glm::dot(normal, center) + plane.w;
        const float projectedRadius = glm::dot(glm::abs(normal), extents);
        if (centerDistance + projectedRadius < 0.0F)
        {
            return false;
        }
    }

    return true;
}
} // namespace ParallelRoam::Algorithms::Roam
