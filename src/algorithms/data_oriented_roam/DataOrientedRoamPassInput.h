#pragma once

#include "algorithms/data_oriented_roam/DataOrientedRoamTypes.h"

namespace ParallelRoam::Algorithms::DataOrientedRoam
{
struct DataOrientedRoamState;
inline constexpr std::uint32_t DataOrientedRoamPassInputVersion = 1U;

/// <summary>
/// 显式编码真实阶段输入，保留会影响后续执行的状态顺序
/// 地址、预留容量、计时和待测策略不参与输入身份
/// </summary>
[[nodiscard]] std::uint64_t HashDataOrientedRoamPassInput(
    const DataOrientedRoamState& state, TerrainLodPassId passId);
}
