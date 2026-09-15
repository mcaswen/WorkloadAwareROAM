#pragma once

#include "algorithms/ITerrainLodAlgorithm.h"
#include "algorithms/cbt_2024/CbtBisectorTopology.h"

#include <glm/glm.hpp>

#include <array>
#include <cstdint>

namespace ParallelRoam::Algorithms::Cbt2024
{
/// <summary>
/// 与上游 ClassifyBisector 返回值保持一致的分类状态
/// </summary>
enum class CbtClassificationResult : std::int32_t
{
    BackFaceCulled = CBT_GPU_CLASSIFICATION_BACK_FACE_CULLED,
    FrustumCulled = CBT_GPU_CLASSIFICATION_FRUSTUM_CULLED,
    TooSmall = CBT_GPU_CLASSIFICATION_TOO_SMALL,
    Unchanged = CBT_GPU_CLASSIFICATION_UNCHANGED,
    Bisect = CBT_GPU_CLASSIFICATION_BISECT,
};

/// <summary>
/// 三个当前顶点加一个父级辅助位置组成的分类输入
/// </summary>
struct CbtClassificationTriangle
{
    std::array<glm::vec3, 4> Positions{};
};

/// <summary>
/// CPU 参考分类同时暴露像素面积，供分辨率缩放和迟滞测试使用
/// </summary>
struct CbtClassificationEvaluation
{
    CbtClassificationResult Result{CbtClassificationResult::Unchanged};
    float TriangleAreaPixels{0.0F};
    float ParentAreaPixels{0.0F};
};

[[nodiscard]] CbtClassificationEvaluation EvaluateCbtClassification(
    const CbtClassificationTriangle& triangle,
    const TerrainLodViewInput& view,
    float triangleAreaPixels,
    std::uint32_t depth,
    std::uint32_t maxDepth);

[[nodiscard]] std::array<CbtClassificationTriangle, CbtBaseBisectorCount>
BuildCbtBaseClassificationTriangles(
    const CbtBaseTopology& topology,
    float terrainSize);

/// <summary>
/// 从真实高度图构造基础面的四个分类位置，供首帧 GPU 结果核对
/// </summary>
[[nodiscard]] std::array<CbtClassificationTriangle, CbtBaseBisectorCount>
BuildCbtBaseClassificationTriangles(
    const CbtBaseTopology& topology,
    const Terrain::HeightMap& heightMap,
    float terrainSize,
    float heightScale);
} // namespace ParallelRoam::Algorithms::Cbt2024
