#include "algorithms/TerrainLodView.h"

#include <glm/geometric.hpp>

#include <algorithm>

namespace ParallelRoam::Algorithms
{
namespace
{
glm::vec4 MatrixRow(const glm::mat4& matrix, glm::length_t row)
{
    // GLM 使用列主序索引，视锥提取公式需要显式重建矩阵行
    return glm::vec4{
        matrix[0][row],
        matrix[1][row],
        matrix[2][row],
        matrix[3][row]};
}

glm::vec4 NormalizePlane(const glm::vec4& plane)
{
    // 平面归一化后距离测试可以直接使用世界空间长度
    // 退化输入保持原值，避免产生 NaN 污染后续候选分类
    const float normalLength = glm::length(glm::vec3{plane});
    return normalLength > 0.000001F ? plane / normalLength : plane;
}
} // namespace

TerrainLodViewInput BuildTerrainLodViewInput(
    const glm::mat4& view,
    const glm::mat4& projection,
    const glm::vec3& cameraPosition,
    const glm::vec3& cameraForward,
    std::uint32_t drawableWidth,
    std::uint32_t drawableHeight,
    bool usesZeroToOneDepth)
{
    TerrainLodViewInput input{};
    input.View = view;
    input.Projection = projection;
    input.ViewProjection = projection * view;
    input.CameraPosition = cameraPosition;
    // 零方向来自尚未初始化的相机输入，统一回退到项目默认朝向
    input.CameraForward = glm::dot(cameraForward, cameraForward) > 0.000001F
        ? glm::normalize(cameraForward)
        : glm::vec3{0.0F, 0.0F, -1.0F};
    input.DrawableWidth = std::max(drawableWidth, 1U);
    input.DrawableHeight = std::max(drawableHeight, 1U);

    // 投影与视图相乘后，clip 空间六个不等式可直接组合矩阵行得到平面
    // 所有平面来自同一个 ViewProjection，避免算法层重复计算或混用矩阵约定
    const glm::vec4 row0 = MatrixRow(input.ViewProjection, 0U);
    const glm::vec4 row1 = MatrixRow(input.ViewProjection, 1U);
    const glm::vec4 row2 = MatrixRow(input.ViewProjection, 2U);
    const glm::vec4 row3 = MatrixRow(input.ViewProjection, 3U);
    // 组合顺序使所有法线朝向视锥内部，调用方可统一使用非负半空间测试
    input.FrustumPlanes[static_cast<std::size_t>(TerrainLodFrustumPlane::Left)] = NormalizePlane(row3 + row0);
    input.FrustumPlanes[static_cast<std::size_t>(TerrainLodFrustumPlane::Right)] = NormalizePlane(row3 - row0);
    input.FrustumPlanes[static_cast<std::size_t>(TerrainLodFrustumPlane::Bottom)] = NormalizePlane(row3 + row1);
    input.FrustumPlanes[static_cast<std::size_t>(TerrainLodFrustumPlane::Top)] = NormalizePlane(row3 - row1);
    // D3D 深度范围使用 z >= 0，OpenGL 深度范围使用 z + w >= 0
    input.FrustumPlanes[static_cast<std::size_t>(TerrainLodFrustumPlane::Near)] =
        NormalizePlane(usesZeroToOneDepth ? row2 : row3 + row2);
    input.FrustumPlanes[static_cast<std::size_t>(TerrainLodFrustumPlane::Far)] = NormalizePlane(row3 - row2);
    return input;
}
} // namespace ParallelRoam::Algorithms
