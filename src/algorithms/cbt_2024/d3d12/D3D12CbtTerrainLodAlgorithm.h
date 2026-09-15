#pragma once

#include "algorithms/ITerrainLodAlgorithm.h"

#include <cstdint>
#include <memory>
#include <string>

namespace ParallelRoam::Render
{
class D3D12GraphicsBackend;
}

namespace ParallelRoam::Algorithms::Cbt2024::D3D12
{
struct D3D12CbtTerrainState;

/// <summary>
/// 使用 CBT 基础二分器状态生成程序化间接地形绘制
/// </summary>
class D3D12CbtTerrainLodAlgorithm final : public ITerrainLodAlgorithm
{
public:
    explicit D3D12CbtTerrainLodAlgorithm(Render::D3D12GraphicsBackend& backend);
    ~D3D12CbtTerrainLodAlgorithm() override;

    [[nodiscard]] TerrainLodAlgorithmInfo Info() const override;
    [[nodiscard]] TerrainLodAlgorithmCapabilities Capabilities() const override;
    [[nodiscard]] bool BuildRenderData(
        const TerrainLodBuildInput& input,
        TerrainLodRenderPacket& outPacket,
        std::string* errorMessage) override;
    [[nodiscard]] const TerrainLodStats& Stats() const override;
    void Reset() override;

private:
    // 后端由 Application 持有，算法仅借用设备和同步入口
    Render::D3D12GraphicsBackend* _backend{nullptr};
    std::unique_ptr<D3D12CbtTerrainState> _state;
    TerrainLodStats _stats{};
    std::uint64_t _recoveryCount{0U};
    std::uint64_t _lastPublishedDiagnosticGeneration{0U};
    std::string _lastRecoveryMessage;
};
} // namespace ParallelRoam::Algorithms::Cbt2024::D3D12
