#include "algorithms/cbt_2024/CbtClassification.h"

#include <glm/common.hpp>
#include <glm/geometric.hpp>

#include <algorithm>
#include <cmath>

namespace ParallelRoam::Algorithms::Cbt2024
{
namespace
{
bool FrustumAabbIntersects(
    const TerrainLodViewInput& view,
    const glm::vec3& minimum,
    const glm::vec3& maximum)
{
    const glm::vec3 center = (maximum + minimum) * 0.5F;
    const glm::vec3 extents = (maximum - minimum) * 0.5F;
    // 固定上游提交只测试左右上下四个平面 参考实现必须保持相同裁剪边界
    for (std::size_t planeIndex = 0U; planeIndex < 4U; ++planeIndex)
    {
        const glm::vec4& plane = view.FrustumPlanes[planeIndex];
        const glm::vec3 positiveVertex = center + extents * glm::sign(glm::vec3{plane});
        if (glm::dot(positiveVertex, glm::vec3{plane}) + plane.w < 0.0F)
        {
            return false;
        }
    }
    return true;
}

glm::vec2 ProjectToUnitScreen(const TerrainLodViewInput& view, const glm::vec3& position)
{
    const glm::vec4 projected = view.ViewProjection * glm::vec4{position, 1.0F};
    // 与 HLSL 一致先透视除法再从 NDC 映射到零到一屏幕坐标
    return glm::vec2{projected} / projected.w * 0.5F + 0.5F;
}

float ProjectedAreaPixels(
    const glm::vec2& point0,
    const glm::vec2& point1,
    const glm::vec2& point2,
    const TerrainLodViewInput& view)
{
    const float normalizedArea = 0.5F * std::abs(
        point0.x * (point2.y - point1.y) +
        point1.x * (point0.y - point2.y) +
        point2.x * (point1.y - point0.y));
    return normalizedArea * static_cast<float>(view.DrawableWidth) *
           static_cast<float>(view.DrawableHeight);
}
} // namespace

CbtClassificationEvaluation EvaluateCbtClassification(
    const CbtClassificationTriangle& triangle,
    const TerrainLodViewInput& view,
    float triangleAreaPixels,
    std::uint32_t depth,
    std::uint32_t maxDepth)
{
    const glm::vec3& point0 = triangle.Positions[0];
    const glm::vec3& point1 = triangle.Positions[1];
    const glm::vec3& point2 = triangle.Positions[2];
    const glm::vec3 triangleNormal = glm::normalize(glm::cross(point2 - point1, point0 - point1));
    const glm::vec3 triangleCenter = (point0 + point1 + point2) / 3.0F;
    // 上游使用相机相对顶点 因而负中心等价于世界坐标中的相机减中心
    const glm::vec3 viewDirection = glm::normalize(view.CameraPosition - triangleCenter);
    const float forwardDotView = glm::dot(viewDirection, view.CameraForward);
    const float viewDotNormal = glm::dot(viewDirection, triangleNormal);

    CbtClassificationEvaluation evaluation{};
    if (forwardDotView < 0.0F && viewDotNormal < -1.0e-3F)
    {
        evaluation.Result = CbtClassificationResult::BackFaceCulled;
        return evaluation;
    }

    const glm::vec3 aabbMinimum = glm::min(glm::min(point0, point1), point2);
    const glm::vec3 aabbMaximum = glm::max(glm::max(point0, point1), point2);
    if (!FrustumAabbIntersects(view, aabbMinimum, aabbMaximum))
    {
        evaluation.Result = CbtClassificationResult::FrustumCulled;
        return evaluation;
    }

    const glm::vec2 projected0 = ProjectToUnitScreen(view, point0);
    const glm::vec2 projected1 = ProjectToUnitScreen(view, point1);
    const glm::vec2 projected2 = ProjectToUnitScreen(view, point2);
    // 掠射角估算保持上游 pow 指数和 2 到 1 的插值 不与 CPU ROAM 厚度误差混用
    const float areaOverestimation = 2.0F - std::pow(viewDotNormal, 0.2F);
    evaluation.TriangleAreaPixels =
        ProjectedAreaPixels(projected0, projected1, projected2, view) * areaOverestimation;
    const glm::vec2 projectedParent = ProjectToUnitScreen(view, triangle.Positions[3]);
    evaluation.ParentAreaPixels =
        ProjectedAreaPixels(projected0, projectedParent, projected2, view) * areaOverestimation;

    if (triangleAreaPixels < evaluation.TriangleAreaPixels && depth < maxDepth)
    {
        evaluation.Result = CbtClassificationResult::Bisect;
    }
    else if ((triangleAreaPixels * 0.5F > evaluation.TriangleAreaPixels) || (depth > maxDepth))
    {
        // 父级面积门槛避免两个子节点在阈值附近反复 split 和 simplify
        evaluation.Result =
            (triangleAreaPixels >= evaluation.ParentAreaPixels || depth > maxDepth)
            ? CbtClassificationResult::TooSmall
            : CbtClassificationResult::Unchanged;
    }
    return evaluation;
}

std::array<CbtClassificationTriangle, CbtBaseBisectorCount>
BuildCbtBaseClassificationTriangles(
    const CbtBaseTopology& topology,
    float terrainSize)
{
    std::array<CbtClassificationTriangle, CbtBaseBisectorCount> triangles{};
    for (std::size_t bisector = 0U; bisector < triangles.size(); ++bisector)
    {
        glm::vec3 center{0.0F};
        for (std::size_t vertex = 0U; vertex < 3U; ++vertex)
        {
            const CbtBaseControlPoint& control = topology.ControlPoints[bisector * 3U + vertex];
            const glm::vec3 position{
                (control.U - 0.5F) * terrainSize,
                0.0F,
                (control.V - 0.5F) * terrainSize,
            };
            triangles[bisector].Positions[vertex] = position;
            center += position;
        }
        // Bootstrap 对基础深度写入有限中心值 动态阶段再用真实父级 LEB 位置替换
        triangles[bisector].Positions[3] = center / 3.0F;
    }
    return triangles;
}

std::array<CbtClassificationTriangle, CbtBaseBisectorCount>
BuildCbtBaseClassificationTriangles(
    const CbtBaseTopology& topology,
    const Terrain::HeightMap& heightMap,
    float terrainSize,
    float heightScale)
{
    std::array<CbtClassificationTriangle, CbtBaseBisectorCount> triangles{};
    for (std::size_t bisector = 0U; bisector < triangles.size(); ++bisector)
    {
        const std::size_t controlOffset = bisector * 3U;
        for (std::size_t vertex = 0U; vertex < 3U; ++vertex)
        {
            const CbtBaseControlPoint& control = topology.ControlPoints[controlOffset + vertex];
            const float height = heightMap.SampleBilinear(control.U, control.V);
            triangles[bisector].Positions[vertex] = {
                (control.U - 0.5F) * terrainSize,
                height * heightScale,
                (control.V - 0.5F) * terrainSize,
            };
        }
        // 基础 heapID 没有子路径，GPU LEB 解码会按奇偶取 parent[2]/parent[0]。
        const std::size_t parentVertex = (topology.HeapIds[bisector] & 1U) == 0U ? 0U : 2U;
        triangles[bisector].Positions[3] = triangles[bisector].Positions[parentVertex];
    }
    return triangles;
}
} // namespace ParallelRoam::Algorithms::Cbt2024
