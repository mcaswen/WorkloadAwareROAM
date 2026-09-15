#pragma once

#include <string>

namespace ParallelRoam::Render
{
class D3D12GraphicsBackend;
}

namespace ParallelRoam::Algorithms::Cbt2024::D3D12
{
/// <summary>
/// 在 D3D12 上执行 OCBT 位操作、归约和 rank-select 的 CPU/GPU 对照验证
/// </summary>
[[nodiscard]] bool RunD3D12CbtOccupancyTreeSmokeTest(
    Render::D3D12GraphicsBackend& backend,
    std::string* errorMessage);
} // namespace ParallelRoam::Algorithms::Cbt2024::D3D12
