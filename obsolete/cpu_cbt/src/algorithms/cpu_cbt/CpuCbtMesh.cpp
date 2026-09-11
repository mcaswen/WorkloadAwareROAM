#include "algorithms/cpu_cbt/CpuCbtMesh.h"
#include "algorithms/cbt_2024/CbtTerrainGeometry.h"

#include <cmath>
#include <stdexcept>
#include <utility>

namespace ParallelRoam::Algorithms::CpuCbt
{
namespace
{
Cbt2024::CbtTerrainGeometryResult Geometry(const CpuCbtState& state,
    const Terrain::HeightMap& heightMap, std::uint32_t slot)
{
    if (!heightMap.IsValid() || slot >= state.HeapIds.size())
        throw std::runtime_error("CPU CBT 几何输入或活动槽无效");
    const auto result = Cbt2024::EvaluateCbtTerrainGeometry(state.HeapIds[slot],
        Cbt2024::CbtBaseDepth, state.ControlPoints, heightMap,
        state.Settings.TerrainSize, state.Settings.HeightScale);
    if (!result.Valid) throw std::runtime_error("CPU CBT 逻辑编号无法解码");
    // 非有限坐标不能进入投影分类，也不能被最终网格输出掩盖
    for (const auto& vertex : result.Vertices)
        for (int axis = 0; axis < 3; ++axis)
            if (!std::isfinite(vertex.Position[axis]) || !std::isfinite(vertex.Normal[axis]))
                throw std::runtime_error("CPU CBT 几何包含非有限值");
    return result;
}
}

std::vector<Cbt2024::CbtClassificationTriangle> BuildCpuCbtClassificationGeometry(
    const CpuCbtState& state, const Terrain::HeightMap& heightMap)
{
    std::vector<Cbt2024::CbtClassificationTriangle> result(state.ActiveIndices.size());
    for (std::size_t i = 0U; i < result.size(); ++i)
    {
        const auto geometry = Geometry(state, heightMap, state.ActiveIndices[i]);
        for (std::size_t vertex = 0U; vertex < 3U; ++vertex)
            result[i].Positions[vertex] = geometry.Vertices[vertex].Position;
        // 第四点沿用参考父级解码，不能以当前三角形中心代替
        result[i].Positions[3] = geometry.ParentClassificationPosition;
    }
    return result;
}

bool BuildCpuCbtMesh(const CpuCbtState& state, const Terrain::HeightMap& heightMap,
    Terrain::TerrainMeshData& mesh, std::string& error)
{
    try
    {
        Terrain::TerrainMeshData next;
        next.TerrainSize = state.Settings.TerrainSize;
        next.HeightScale = state.Settings.HeightScale;
        next.GridWidth = heightMap.Width();
        next.GridHeight = heightMap.Height();
        next.Vertices.resize(state.ActiveIndices.size() * 3U);
        next.Indices.resize(next.Vertices.size());
        for (std::size_t i = 0U; i < state.ActiveIndices.size(); ++i)
        {
            const auto geometry = Geometry(state, heightMap, state.ActiveIndices[i]);
            for (std::size_t vertex = 0U; vertex < 3U; ++vertex)
            {
                const auto index = i * 3U + vertex;
                next.Vertices[index] = geometry.Vertices[vertex];
                next.Indices[index] = static_cast<std::uint32_t>(index);
            }
        }
        mesh = std::move(next);
        error.clear();
        return true;
    }
    catch (const std::exception& exception)
    {
        error = exception.what();
        return false;
    }
}
} // 命名空间 ParallelRoam::Algorithms::CpuCbt
