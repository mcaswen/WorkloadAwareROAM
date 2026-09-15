#include "algorithms/cbt_2024/CbtTerrainGeometry.h"

#include "algorithms/RoamGeometry.h"

#include <algorithm>

namespace ParallelRoam::Algorithms::Cbt2024
{
namespace
{
glm::vec3 DebugColor(std::uint32_t baseBisector)
{
    // 颜色与 HLSL 按基础半边稳定分组，便于观察绕序和共享边。
    constexpr std::array<glm::vec3, CbtBaseBisectorCount> Colors{{
        {0.08F, 0.72F, 0.62F},
        {0.10F, 0.52F, 0.88F},
        {0.45F, 0.76F, 0.24F},
        {1.00F, 0.58F, 0.12F},
        {0.90F, 0.28F, 0.24F},
        {0.70F, 0.34F, 0.82F},
    }};
    return Colors[std::min<std::size_t>(baseBisector, Colors.size() - 1U)];
}

glm::vec2 ControlUv(const CbtBaseControlPoint& control)
{
    return {control.U, control.V};
}
} // namespace

CbtTerrainGeometryResult EvaluateCbtTerrainGeometry(
    std::uint64_t heapId,
    std::uint32_t baseDepth,
    const std::array<CbtBaseControlPoint, CbtBaseControlPointCount>& baseControlPoints,
    const Terrain::HeightMap& heightMap,
    float terrainSize,
    float heightScale)
{
    CbtTerrainGeometryResult result{};
    if (!heightMap.IsValid())
    {
        return result;
    }
    const CbtLebTriangleResult triangle =
        EvaluateCbtLebTriangle(heapId, baseDepth, baseControlPoints);
    if (!triangle.Valid)
    {
        return result;
    }

    result.BaseBisector = triangle.BaseBisector;
    for (std::size_t vertexIndex = 0U; vertexIndex < result.Vertices.size(); ++vertexIndex)
    {
        const glm::vec2 uv = ControlUv(triangle.Child[vertexIndex]);
        const Roam::TerrainWorldSample sample =
            Roam::SampleTerrainWorld(heightMap, uv, terrainSize, heightScale);
        Terrain::TerrainMeshVertex& vertex = result.Vertices[vertexIndex];
        vertex.Position = sample.Position;
        vertex.Normal =
            Roam::SampleHeightGradientNormal(heightMap, uv, terrainSize, heightScale);
        vertex.TexCoord = uv;
        vertex.Height = sample.Height;
        vertex.DebugColor = DebugColor(result.BaseBisector);
        vertex.DebugHighlight = 1.0F;
    }

    // 上游分类只需要与当前最长边相对的旧父顶点。
    const std::size_t parentVertex = (heapId & 1U) == 0U ? 0U : 2U;
    result.ParentClassificationPosition = Roam::SampleTerrainWorld(
        heightMap,
        ControlUv(triangle.Parent[parentVertex]),
        terrainSize,
        heightScale).Position;
    result.Valid = true;
    return result;
}
} // namespace ParallelRoam::Algorithms::Cbt2024
