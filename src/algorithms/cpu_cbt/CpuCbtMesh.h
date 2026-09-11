#pragma once

#include "algorithms/cpu_cbt/CpuCbtState.h"
#include "algorithms/cbt_2024/CbtClassification.h"
#include "terrain/TerrainMeshBuilder.h"

namespace ParallelRoam::Algorithms::CpuCbt
{
/// <summary>
/// 按当前活动顺序生成分类四点，返回值中的序号不代表物理槽编号
/// 调用期间状态与高度图保持只读；非法几何通过异常交给轮次边界处理
/// </summary>
[[nodiscard]] std::vector<Cbt2024::CbtClassificationTriangle> BuildCpuCbtClassificationGeometry(
    const CpuCbtState& state, const Terrain::HeightMap& heightMap);

/// <summary>
/// 全量输出每三角形三个独立顶点；失败不覆盖此前网格
/// </summary>
[[nodiscard]] bool BuildCpuCbtMesh(const CpuCbtState& state, const Terrain::HeightMap& heightMap,
    Terrain::TerrainMeshData& mesh, std::string& error);
} // 命名空间 ParallelRoam::Algorithms::CpuCbt
