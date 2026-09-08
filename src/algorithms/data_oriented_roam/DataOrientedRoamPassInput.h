#pragma once

#include "algorithms/data_oriented_roam/DataOrientedRoamTypes.h"

namespace ParallelRoam::Algorithms::DataOrientedRoam
{
struct DataOrientedRoamState;
inline constexpr std::uint32_t DataOrientedRoamPassInputVersion = 1U;

/// <summary>
/// 编码真实阶段输入并保留有序状态且排除地址容量计时与待测策略
/// </summary>
[[nodiscard]] std::uint64_t HashDataOrientedRoamPassInput(
    const DataOrientedRoamState& state, TerrainLodPassId passId);
}
