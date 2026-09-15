#pragma once

#include "render/D3D12GraphicsBackend.h"

#include <d3d12.h>
#include <wrl/client.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>

namespace ParallelRoam::Render
{
/// <summary>
/// 封装程序化地形绘制的根签名、PSO、间接命令和逐帧资源描述符
/// </summary>
class D3D12CbtRenderPass
{
public:
    static constexpr std::size_t GpuTimestampReadbackBytes = sizeof(std::uint64_t) * 2U;

    D3D12CbtRenderPass() = default;
    ~D3D12CbtRenderPass();

    D3D12CbtRenderPass(const D3D12CbtRenderPass&) = delete;
    D3D12CbtRenderPass& operator=(const D3D12CbtRenderPass&) = delete;

    /// <summary>
    /// 初始化失败时释放已分配描述符，并通过 errorMessage 返回具体 D3D12 阶段
    /// </summary>
    [[nodiscard]] bool Initialize(D3D12GraphicsBackend& backend, std::string* errorMessage);
    void Shutdown();
    void InvalidateResourceDescriptors();

    /// <summary>
    /// 只改写当前已等待帧的 SRV，避免覆盖其他在途帧正在使用的描述符
    /// </summary>
    [[nodiscard]] bool ConfigureResourceDescriptors(
        std::uint32_t frameIndex,
        ID3D12Resource* vertexBuffer,
        std::size_t vertexCapacityBytes,
        std::size_t vertexStrideBytes,
        ID3D12Resource* activeElementBuffer,
        std::size_t activeElementCapacityBytes,
        std::size_t activeElementStrideBytes,
        ID3D12Resource* lodStateBuffer,
        std::size_t lodStateCapacityBytes,
        std::size_t lodStateStrideBytes,
        std::uint64_t resourceGeneration,
        std::string* errorMessage);

    /// <summary>
    /// 当前帧 ConfigureResourceDescriptors 成功后记录根绑定和单条 DRAW 间接命令
    /// </summary>
    void RecordDraw(
        ID3D12GraphicsCommandList* commandList,
        std::uint32_t frameIndex,
        D3D12_GPU_VIRTUAL_ADDRESS constantBufferAddress,
        D3D12_GPU_DESCRIPTOR_HANDLE textureSrv,
        ID3D12Resource* indirectBuffer,
        std::size_t indirectArgumentOffsetBytes,
        bool wireframe,
        std::uint64_t topologyGeneration);

    [[nodiscard]] bool IsReady() const;
    [[nodiscard]] float LastGpuDrawMilliseconds() const;
    [[nodiscard]] std::uint64_t LastGpuDrawSampleGeneration() const;

private:
    [[nodiscard]] bool CreateRootSignature(std::string* errorMessage);
    [[nodiscard]] bool CreatePipelineStates(std::string* errorMessage);
    [[nodiscard]] bool CreateCommandSignature(std::string* errorMessage);
    [[nodiscard]] bool AllocateFrameDescriptors(std::string* errorMessage);
    [[nodiscard]] bool CreateTimingResources(std::string* errorMessage);
    void ConsumeCompletedTiming(std::uint32_t frameIndex);

    // 后端只借用，管线对象和逐帧描述符槽位由本类持有
    D3D12GraphicsBackend* _backend{nullptr};
    Microsoft::WRL::ComPtr<ID3D12RootSignature> _rootSignature;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> _fillPipelineState;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> _wireframePipelineState;
    Microsoft::WRL::ComPtr<ID3D12CommandSignature> _drawCommandSignature;
    std::array<D3D12DescriptorAllocation, D3D12GraphicsBackend::FrameCount> _activeElementSrvs;
    std::array<D3D12DescriptorAllocation, D3D12GraphicsBackend::FrameCount> _vertexSrvs;
    std::array<D3D12DescriptorAllocation, D3D12GraphicsBackend::FrameCount> _lodStateSrvs;
    std::array<std::uint64_t, D3D12GraphicsBackend::FrameCount> _descriptorGenerations{};
    Microsoft::WRL::ComPtr<ID3D12QueryHeap> _timestampQueryHeap;
    std::array<Microsoft::WRL::ComPtr<ID3D12Resource>, D3D12GraphicsBackend::FrameCount> _timestampReadbacks;
    std::array<bool, D3D12GraphicsBackend::FrameCount> _timestampPending{};
    std::array<std::uint64_t, D3D12GraphicsBackend::FrameCount> _timestampGenerations{};
    std::uint64_t _timestampFrequency{0U};
    std::uint64_t _lastGpuDrawSampleGeneration{0U};
    float _lastGpuDrawMilliseconds{0.0F};
};
} // namespace ParallelRoam::Render
