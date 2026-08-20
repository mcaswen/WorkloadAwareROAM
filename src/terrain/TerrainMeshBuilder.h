#pragma once

#include "terrain/HeightMap.h"

#include <glm/glm.hpp>

#include <cstdint>
#include <vector>

namespace ParallelRoam::Terrain
{
/// <summary>
/// terrain mesh 的单个顶点，携带渲染、采样和 LOD debug overlay 所需属性
/// </summary>
struct TerrainMeshVertex
{
    glm::vec3 Position{0.0F};
    glm::vec3 Normal{0.0F, 1.0F, 0.0F};
    glm::vec2 TexCoord{0.0F};
    float Height{0.0F};
    glm::vec3 DebugColor{0.28F, 0.34F, 0.30F};
    float DebugHighlight{0.35F};
};

/// <summary>
/// CPU 侧 terrain 网格数据，供规则网格、ROAM 输出和 OpenGL 上传路径共用
/// </summary>
struct TerrainMeshData
{
    std::vector<TerrainMeshVertex> Vertices;
    std::vector<std::uint32_t> Indices;
    int GridWidth{0};
    int GridHeight{0};
    float TerrainSize{0.0F};
    float HeightScale{0.0F};
};

/// <summary>
/// 将 Height Map 转换为规则网格 terrain mesh
/// </summary>
class TerrainMeshBuilder
{
public:
    [[nodiscard]] static TerrainMeshData Build(const HeightMap& heightMap, float terrainSize, float heightScale);

private:
    static void AccumulateNormals(TerrainMeshData& meshData);
};
} // 命名空间 ParallelRoam::Terrain
