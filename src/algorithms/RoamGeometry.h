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

/// 从 A-B 底边的中点细分三角形，并保持 ROAM 邻接代码所依赖的顶点绕序
template <typename Domain>
[[nodiscard]] inline TriangleDomainChildren<Domain> SplitTriangleDomain(const Domain& domain)
{
    const glm::vec2 midpoint = (domain.A + domain.B) * 0.5F;
    return TriangleDomainChildren<Domain>{
        Domain{domain.C, domain.A, midpoint},
        Domain{domain.B, domain.C, midpoint},
    };
}

/// 计算底边中点的真实高度与线性插值高度之差，作为该节点的局部几何误差
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

/// 在归一化高度图坐标处采样，并一次性返回原始高度和换算后的世界坐标
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

/// 将归一化高度图坐标换算为当前地形尺寸和高度比例下的世界坐标
[[nodiscard]] inline glm::vec3 DomainToWorld(
    const Terrain::HeightMap& heightMap,
    const glm::vec2& uv,
    float terrainSize,
    float heightScale)
{
    return SampleTerrainWorld(heightMap, uv, terrainSize, heightScale).Position;
}

/// 用高度图相邻四点的中心差分估算世界空间法线，供网格顶点着色使用
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

/// 用几何误差扩张三角形包围盒后再做视锥测试，避免误删仍可能影响画面的细分或合并候选
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
} // 命名空间 ParallelRoam::Algorithms::Roam
