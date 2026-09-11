#pragma once

#include "algorithms/cbt_2024/CbtBisectorTopology.h"

#include <string>
#include <vector>

namespace ParallelRoam::Algorithms::CpuCbt
{
/// <summary>
/// 独立 CPU 原型的固定参数；深度采用逻辑编号位长，不复用 ROAM 层数
/// 三角形预算约束全部活动叶，与物理槽池容量分开
/// </summary>
struct CpuCbtSettings
{
    float TerrainSize{30.0F};
    float HeightScale{4.0F};
    float TriangleAreaPixels{50.0F};
    std::uint32_t MaxHeapBitDepth{20U};
    std::uint32_t TriangleBudget{4096U};
};

/// <summary>
/// 调用者独占的已发布拓扑；数组使用物理槽编号，活动列表按物理槽升序排列
/// 六个永久槽紧随动态池，空闲槽的逻辑编号为零
/// </summary>
struct CpuCbtState
{
    static constexpr std::uint32_t DynamicCapacity = 131072U;
    CpuCbtSettings Settings;
    Cbt2024::CbtOccupancyTree Occupancy{Cbt2024::CbtOccupancyCapacity::Capacity128K};
    std::array<Cbt2024::CbtBaseControlPoint, Cbt2024::CbtBaseControlPointCount> ControlPoints{};
    std::vector<std::uint64_t> HeapIds;
    std::vector<Cbt2024::CbtBisectorNeighbors> Neighbors;
    std::vector<Cbt2024::CbtBisectorData> Data;
    std::vector<std::uint32_t> ActiveIndices;
    std::uint64_t Generation{0U};
};

/// <summary>
/// 先验证配置再创建六基础状态，失败时保留调用者原状态并给出原因
/// </summary>
[[nodiscard]] bool InitializeCpuCbt(CpuCbtState& state, const CpuCbtSettings& settings, std::string& error);
[[nodiscard]] bool ValidateCpuCbtSettings(const CpuCbtSettings& settings, std::string& error);
} // 命名空间 ParallelRoam::Algorithms::CpuCbt
