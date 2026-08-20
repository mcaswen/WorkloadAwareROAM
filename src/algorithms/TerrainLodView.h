#pragma once

#include "algorithms/ITerrainLodAlgorithm.h"

namespace ParallelRoam::Algorithms
{
/// <summary>
/// 构建归一化视锥平面，usesZeroToOneDepth 指定投影矩阵使用零到一或负一到一深度范围
/// </summary>
[[nodiscard]] TerrainLodViewInput BuildTerrainLodViewInput(
    const glm::mat4& view,
    const glm::mat4& projection,
    const glm::vec3& cameraPosition,
    const glm::vec3& cameraForward,
    std::uint32_t drawableWidth,
    std::uint32_t drawableHeight,
    bool usesZeroToOneDepth);
} // namespace ParallelRoam::Algorithms
