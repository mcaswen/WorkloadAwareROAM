#include "algorithms/RoamGeometry.h"
#include "algorithms/cbt_2024/CbtTerrainGeometry.h"

#include <glm/geometric.hpp>

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <iostream>
#include <string>

namespace
{
using ParallelRoam::Algorithms::Cbt2024::BuildSquareCbtBaseTopology;
using ParallelRoam::Algorithms::Cbt2024::CbtOccupancyCapacity;
using ParallelRoam::Algorithms::Cbt2024::EvaluateCbtLebTriangle;
using ParallelRoam::Algorithms::Cbt2024::EvaluateCbtTerrainGeometry;
using ParallelRoam::Algorithms::Roam::SampleHeightGradientNormal;
using ParallelRoam::Terrain::HeightMap;

bool Expect(bool condition, const std::string& message)
{
    if (!condition)
    {
        std::cerr << message << '\n';
    }
    return condition;
}

bool Near(float lhs, float rhs, float tolerance = 1.0e-5F)
{
    return std::abs(lhs - rhs) <= tolerance;
}

bool Near(const glm::vec3& lhs, const glm::vec3& rhs, float tolerance = 1.0e-5F)
{
    return glm::length(lhs - rhs) <= tolerance;
}
} // namespace

int main()
{
    HeightMap heightMap;
    std::string error;
    bool passed = Expect(
        heightMap.LoadFromFile("assets/heightmaps/Hm_Terrain_Test_129.pgm", &error),
        error.empty() ? "failed to load CBT geometry height map" : error);
    if (!passed)
    {
        return 1;
    }
    passed &= Expect(heightMap.Width() == 129 && heightMap.Height() == 129, "unexpected test height-map dimensions");
    passed &= Expect(
        heightMap.Values().size() ==
            static_cast<std::size_t>(heightMap.Width()) * static_cast<std::size_t>(heightMap.Height()),
        "height-map upload span has the wrong size");
    passed &= Expect(Near(heightMap.Values().front(), heightMap.SamplePixel(0, 0)), "height-map top-left orientation changed");
    passed &= Expect(
        Near(heightMap.Values().back(), heightMap.SamplePixel(heightMap.Width() - 1, heightMap.Height() - 1)),
        "height-map bottom-right orientation changed");

    const auto topology = BuildSquareCbtBaseTopology(CbtOccupancyCapacity::Capacity128K);
    constexpr float TerrainSize = 30.0F;
    constexpr float HeightScale = 7.0F;
    for (const std::uint64_t heapId : {8ULL, 13ULL, 16ULL, 35ULL, 71ULL})
    {
        const auto leb = EvaluateCbtLebTriangle(heapId, topology.BaseDepth, topology.ControlPoints);
        const auto geometry = EvaluateCbtTerrainGeometry(
            heapId,
            topology.BaseDepth,
            topology.ControlPoints,
            heightMap,
            TerrainSize,
            HeightScale);
        passed &= Expect(leb.Valid && geometry.Valid, "valid heapID did not produce terrain geometry");
        if (!leb.Valid || !geometry.Valid)
        {
            continue;
        }
        for (std::size_t vertexIndex = 0U; vertexIndex < geometry.Vertices.size(); ++vertexIndex)
        {
            const glm::vec2 uv{leb.Child[vertexIndex].U, leb.Child[vertexIndex].V};
            const float expectedHeight = heightMap.SampleBilinear(uv.x, uv.y);
            const auto& vertex = geometry.Vertices[vertexIndex];
            passed &= Expect(Near(vertex.TexCoord.x, uv.x) && Near(vertex.TexCoord.y, uv.y), "terrain UV changed orientation");
            passed &= Expect(Near(vertex.Height, expectedHeight), "terrain height differs from HeightMap::SampleBilinear");
            passed &= Expect(Near(vertex.Position.y, expectedHeight * HeightScale), "terrain height scale was not applied");
            passed &= Expect(
                Near(vertex.Normal, SampleHeightGradientNormal(heightMap, uv, TerrainSize, HeightScale), 2.0e-5F),
                "terrain normal differs from the four-sample CPU reference");
            passed &= Expect(std::isfinite(vertex.Position.x) && std::isfinite(vertex.Normal.y), "terrain geometry contains non-finite data");
        }
        const glm::vec3 faceNormal = glm::cross(
            geometry.Vertices[1].Position - geometry.Vertices[0].Position,
            geometry.Vertices[2].Position - geometry.Vertices[0].Position);
        passed &= Expect(faceNormal.y > 0.0F, "terrain triangle winding does not face positive Y");
        const std::size_t parentVertex = (heapId & 1U) == 0U ? 0U : 2U;
        const glm::vec2 parentUv{leb.Parent[parentVertex].U, leb.Parent[parentVertex].V};
        const float parentHeight = heightMap.SampleBilinear(parentUv.x, parentUv.y);
        const glm::vec3 expectedParent{
            (parentUv.x - 0.5F) * TerrainSize,
            parentHeight * HeightScale,
            (parentUv.y - 0.5F) * TerrainSize,
        };
        passed &= Expect(Near(geometry.ParentClassificationPosition, expectedParent), "parent classification position mismatch");
    }

    const auto flat = EvaluateCbtTerrainGeometry(
        8U,
        topology.BaseDepth,
        topology.ControlPoints,
        heightMap,
        TerrainSize,
        0.0F);
    passed &= Expect(flat.Valid, "zero-height-scale geometry was rejected");
    for (const auto& vertex : flat.Vertices)
    {
        passed &= Expect(Near(vertex.Position.y, 0.0F), "zero height scale produced a raised vertex");
        passed &= Expect(Near(vertex.Normal, glm::vec3{0.0F, 1.0F, 0.0F}), "zero height scale produced a tilted normal");
    }
    passed &= Expect(
        !EvaluateCbtTerrainGeometry(0U, topology.BaseDepth, topology.ControlPoints, heightMap, TerrainSize, HeightScale).Valid,
        "zero heapID unexpectedly produced geometry");
    return passed ? 0 : 1;
}
