#include "algorithms/TerrainLodView.h"

#include <glm/geometric.hpp>

#include <algorithm>

namespace ParallelRoam::Algorithms
{
namespace
{
glm::vec4 MatrixRow(const glm::mat4& matrix, glm::length_t row)
{
    // GLM 按列索引矩阵，而视锥公式按行组合，因此先显式取出指定行
    return glm::vec4{
        matrix[0][row],
        matrix[1][row],
        matrix[2][row],
        matrix[3][row]};
}

glm::vec4 NormalizePlane(const glm::vec4& plane)
{
    // 归一化后，平面值可以直接与世界空间距离比较
    // 退化平面保持原值，避免除零产生 NaN 并污染后续可见性判断
    const float normalLength = glm::length(glm::vec3{plane});
    return normalLength > 0.000001F ? plane / normalLength : plane;
}
} // 匿名命名空间

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
    // 相机尚未初始化时方向可能为零，此时使用项目默认前向，保证评分输入有效
    input.CameraForward = glm::dot(cameraForward, cameraForward) > 0.000001F
        ? glm::normalize(cameraForward)
        : glm::vec3{0.0F, 0.0F, -1.0F};
    input.DrawableWidth = std::max(drawableWidth, 1U);
    input.DrawableHeight = std::max(drawableHeight, 1U);

    // 裁剪空间的六个边界可以直接由视图投影矩阵的行组合得到
    // 在这里统一生成全部平面，避免各算法重复计算或使用不同矩阵约定
    const glm::vec4 row0 = MatrixRow(input.ViewProjection, 0U);
    const glm::vec4 row1 = MatrixRow(input.ViewProjection, 1U);
    const glm::vec4 row2 = MatrixRow(input.ViewProjection, 2U);
    const glm::vec4 row3 = MatrixRow(input.ViewProjection, 3U);
    // 组合顺序保证法线朝内，调用方只需检查平面值是否非负
    input.FrustumPlanes[static_cast<std::size_t>(TerrainLodFrustumPlane::Left)] = NormalizePlane(row3 + row0);
    input.FrustumPlanes[static_cast<std::size_t>(TerrainLodFrustumPlane::Right)] = NormalizePlane(row3 - row0);
    input.FrustumPlanes[static_cast<std::size_t>(TerrainLodFrustumPlane::Bottom)] = NormalizePlane(row3 + row1);
    input.FrustumPlanes[static_cast<std::size_t>(TerrainLodFrustumPlane::Top)] = NormalizePlane(row3 - row1);
    // D3D 的近裁剪条件是 z >= 0，OpenGL 则是 z + w >= 0
    input.FrustumPlanes[static_cast<std::size_t>(TerrainLodFrustumPlane::Near)] =
        NormalizePlane(usesZeroToOneDepth ? row2 : row3 + row2);
    input.FrustumPlanes[static_cast<std::size_t>(TerrainLodFrustumPlane::Far)] = NormalizePlane(row3 - row2);
    return input;
}
} // 命名空间 ParallelRoam::Algorithms
