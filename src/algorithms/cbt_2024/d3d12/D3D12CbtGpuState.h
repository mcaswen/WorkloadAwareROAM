#pragma once

#include "algorithms/cbt_2024/CbtBisectorTopology.h"

#include <array>
#include <cstdint>
#include <memory>
#include <string>

struct ID3D12Resource;

namespace ParallelRoam::Render
{
class D3D12GraphicsBackend;
}

namespace ParallelRoam::Algorithms::Cbt2024::D3D12
{
/// <summary>
/// 不拥有资源的 D3D12 CBT 缓冲视图，供后续更新和绘制 pass 绑定
/// </summary>
struct D3D12CbtGpuResourceView
{
    ID3D12Resource* OccupancyTree{nullptr};
    ID3D12Resource* OccupancyBitfield{nullptr};
    ID3D12Resource* HeapIds{nullptr};
    std::array<ID3D12Resource*, 2> Neighbors{};
    ID3D12Resource* BisectorData{nullptr};
    ID3D12Resource* Classification{nullptr};
    ID3D12Resource* Simplification{nullptr};
    ID3D12Resource* Allocation{nullptr};
    ID3D12Resource* Propagation{nullptr};
    ID3D12Resource* Memory{nullptr};
    ID3D12Resource* Validation{nullptr};
    ID3D12Resource* ActiveIndices{nullptr};
    ID3D12Resource* VisibleIndices{nullptr};
    ID3D12Resource* ModifiedIndices{nullptr};
    ID3D12Resource* TopologyDispatchCommands{nullptr};
    ID3D12Resource* IndirectDrawState{nullptr};
    ID3D12Resource* GeometryDispatchCommands{nullptr};
    ID3D12Resource* BaseControlPoints{nullptr};
};

/// <summary>
/// 持有方形基础二分器和 CBT 更新链所需的全部 D3D12 常驻资源
/// </summary>
class D3D12CbtGpuState
{
public:
    D3D12CbtGpuState();
    ~D3D12CbtGpuState();

    D3D12CbtGpuState(const D3D12CbtGpuState&) = delete;
    D3D12CbtGpuState& operator=(const D3D12CbtGpuState&) = delete;

    /// <summary>
    /// 在帧外等待 GPU 完成旧资源访问，再替换整套容量资源
    /// </summary>
    [[nodiscard]] bool Rebuild(
        Render::D3D12GraphicsBackend& backend,
        CbtOccupancyCapacity capacity,
        std::string* errorMessage);

    /// <summary>
    /// 在帧外等待 GPU 后释放资源，调用期间后端必须有效
    /// </summary>
    [[nodiscard]] bool Reset(Render::D3D12GraphicsBackend& backend, std::string* errorMessage);

    [[nodiscard]] bool IsInitialized() const;
    [[nodiscard]] std::uint64_t Generation() const;
    [[nodiscard]] const CbtBaseTopology& Topology() const;

    /// <summary>
    /// 借用当前缓冲，返回的视图在资源重建、重置或析构后失效
    /// </summary>
    [[nodiscard]] D3D12CbtGpuResourceView Resources() const;

private:
    struct Impl;
    std::unique_ptr<Impl> _impl;
};

/// <summary>
/// 验证四档容量的完整资源创建、初值上传、读回和重建生命周期
/// </summary>
[[nodiscard]] bool RunD3D12CbtBaseTopologySmokeTest(
    Render::D3D12GraphicsBackend& backend,
    std::string* errorMessage);
} // namespace ParallelRoam::Algorithms::Cbt2024::D3D12
