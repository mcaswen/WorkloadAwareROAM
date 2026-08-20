#include "terrain/TerrainMeshBuilder.h"

#include <algorithm>

namespace ParallelRoam::Terrain
{
// 规则网格 builder 是视觉 baseline
// 它不做任何 LOD
// ROAM 输出的世界坐标、UV 和高度缩放必须和这里保持一致
TerrainMeshData TerrainMeshBuilder::Build(const HeightMap& heightMap, float terrainSize, float heightScale)
{
    TerrainMeshData meshData{};
    meshData.GridWidth = heightMap.Width();
    meshData.GridHeight = heightMap.Height();
    meshData.TerrainSize = terrainSize;
    meshData.HeightScale = heightScale;

    if (!heightMap.IsValid() || meshData.GridWidth < 2 || meshData.GridHeight < 2)
    {
        // 少于 2x2 无法形成三角网格
        return meshData;
    }

    const auto vertexCount = static_cast<std::size_t>(meshData.GridWidth) * static_cast<std::size_t>(meshData.GridHeight);
    meshData.Vertices.resize(vertexCount);

    // Height Map 的 UV 直接映射到 XZ 平面，地形中心落在世界原点
    // ROAM 的 DomainToWorld 使用相同公式
    // benchmark 才能比较规则网格和 LOD mesh 的视觉位置
    for (int y = 0; y < meshData.GridHeight; ++y)
    {
        for (int x = 0; x < meshData.GridWidth; ++x)
        {
            const float u = static_cast<float>(x) / static_cast<float>(meshData.GridWidth - 1);
            const float v = static_cast<float>(y) / static_cast<float>(meshData.GridHeight - 1);
            const float height = heightMap.SamplePixel(x, y);
            const auto index = static_cast<std::size_t>(y) * static_cast<std::size_t>(meshData.GridWidth) +
                               static_cast<std::size_t>(x);

            TerrainMeshVertex& vertex = meshData.Vertices[index];
            vertex.Position.x = (u - 0.5F) * terrainSize;
            vertex.Position.y = height * heightScale;
            vertex.Position.z = (v - 0.5F) * terrainSize;
            vertex.TexCoord = glm::vec2{u, v};
            vertex.Height = height;
            vertex.DebugColor = glm::vec3{0.28F, 0.34F, 0.30F};
            vertex.DebugHighlight = 0.35F;
        }
    }

    const auto quadCount = static_cast<std::size_t>(meshData.GridWidth - 1) *
                           static_cast<std::size_t>(meshData.GridHeight - 1);
    meshData.Indices.reserve(quadCount * 6U);

    // 每个 heightmap cell 拆成两个三角形
    // 索引顺序要和 ROAM mesh 输出统一朝向正 Y
    for (int y = 0; y < meshData.GridHeight - 1; ++y)
    {
        for (int x = 0; x < meshData.GridWidth - 1; ++x)
        {
            const auto i0 = static_cast<std::uint32_t>(y * meshData.GridWidth + x);
            const auto i1 = static_cast<std::uint32_t>(y * meshData.GridWidth + x + 1);
            const auto i2 = static_cast<std::uint32_t>((y + 1) * meshData.GridWidth + x);
            const auto i3 = static_cast<std::uint32_t>((y + 1) * meshData.GridWidth + x + 1);

            // 采用逆时针绕序，让法线朝向正 Y
            meshData.Indices.push_back(i0);
            meshData.Indices.push_back(i2);
            meshData.Indices.push_back(i1);
            meshData.Indices.push_back(i1);
            meshData.Indices.push_back(i2);
            meshData.Indices.push_back(i3);
        }
    }

    AccumulateNormals(meshData);
    return meshData;
}

void TerrainMeshBuilder::AccumulateNormals(TerrainMeshData& meshData)
{
    // 顶点法线按相邻三角形面积加权累积
    // 规则网格路径因此比单三角面法线更平滑
    for (TerrainMeshVertex& vertex : meshData.Vertices)
    {
        vertex.Normal = glm::vec3{0.0F};
    }

    for (std::size_t index = 0; index + 2U < meshData.Indices.size(); index += 3U)
    {
        TerrainMeshVertex& a = meshData.Vertices[meshData.Indices[index]];
        TerrainMeshVertex& b = meshData.Vertices[meshData.Indices[index + 1U]];
        TerrainMeshVertex& c = meshData.Vertices[meshData.Indices[index + 2U]];

        const glm::vec3 edge0 = b.Position - a.Position;
        const glm::vec3 edge1 = c.Position - a.Position;
        const glm::vec3 normal = glm::cross(edge0, edge1);

        // 未归一化法线保留三角形面积权重
        a.Normal += normal;
        b.Normal += normal;
        c.Normal += normal;
    }

    for (TerrainMeshVertex& vertex : meshData.Vertices)
    {
        // 极端平坦或退化三角形时保底使用竖直法线
        if (glm::dot(vertex.Normal, vertex.Normal) <= 0.000001F)
        {
            vertex.Normal = glm::vec3{0.0F, 1.0F, 0.0F};
        }
        else
        {
            vertex.Normal = glm::normalize(vertex.Normal);
        }
    }
}
} // 命名空间 ParallelRoam::Terrain
