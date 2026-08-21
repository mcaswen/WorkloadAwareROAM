#pragma once

#include "algorithms/ITerrainLodAlgorithm.h"

namespace ParallelRoam::Algorithms
{
/// <summary>
/// 整理相机输入并提取归一化视锥平面；usesZeroToOneDepth 用于区分 D3D 与 OpenGL 的裁剪空间深度范围
/// </summary>
[[nodiscard]] TerrainLodViewInput BuildTerrainLodViewInput(
    const glm::mat4& view,
    const glm::mat4& projection,
    const glm::vec3& cameraPosition,
    const glm::vec3& cameraForward,
    std::uint32_t drawableWidth,
    std::uint32_t drawableHeight,
    bool usesZeroToOneDepth);
} // 命名空间 ParallelRoam::Algorithms
