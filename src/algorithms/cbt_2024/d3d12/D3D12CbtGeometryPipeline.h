#pragma once

#include "algorithms/cbt_2024/CbtBisectorTopology.h"
#include "render/D3D12GraphicsBackend.h"
#include "terrain/HeightMap.h"

#if !defined(NOMINMAX)
#define NOMINMAX
#endif

#include <d3d12.h>
#include <wrl/client.h>

#include <cstddef>
#include <cstdint>
#include <string>

namespace ParallelRoam::Algorithms::Cbt2024::D3D12
{
/// <summary>
/// 持有 CBT 参数几何、最终顶点和对应 compute PSO，并独立维护两项资源状态。
/// 帧编排器只决定何时执行 active/modified 路径，不拥有几何资源。
/// </summary>
class D3D12CbtGeometryPipeline
{
public:
    [[nodiscard]] bool Initialize(
        Render::D3D12GraphicsBackend& backend,
        ID3D12RootSignature* rootSignature,
        const CbtBaseTopology& topology,
        const Terrain::HeightMap& heightMap,
        std::string* errorMessage);
    void Shutdown();

    void TransitionClassification(
        ID3D12GraphicsCommandList* commandList,
        D3D12_RESOURCE_STATES nextState);
    void TransitionVertices(
        ID3D12GraphicsCommandList* commandList,
        D3D12_RESOURCE_STATES nextState);

    [[nodiscard]] ID3D12PipelineState* ActivePipeline() const;
    [[nodiscard]] ID3D12PipelineState* ModifiedPipeline() const;
    [[nodiscard]] ID3D12PipelineState* ValidatePipeline() const;
    [[nodiscard]] D3D12_GPU_DESCRIPTOR_HANDLE HeightMapSrv() const;
    [[nodiscard]] ID3D12Resource* ClassificationPositions() const;
    [[nodiscard]] ID3D12Resource* RenderVertices() const;
    [[nodiscard]] std::size_t ClassificationPositionCapacityBytes() const;
    [[nodiscard]] std::size_t RenderVertexCapacityBytes() const;

private:
    Render::D3D12GraphicsBackend* _backend{nullptr}; // 借用 descriptor 所有者，不拥有 device。
    // active PSO 在全量失效时重建所有活动槽几何。
    Microsoft::WRL::ComPtr<ID3D12PipelineState> _activePipeline;
    // modified PSO 在常规帧只更新本次提交触及的槽位。
    Microsoft::WRL::ComPtr<ID3D12PipelineState> _modifiedPipeline;
    // 验证 PSO 检查活动顶点的高度、法线、绕序和父位置。
    Microsoft::WRL::ComPtr<ID3D12PipelineState> _validatePipeline;
    // R32_FLOAT 高度纹理由算法状态持有，不借用 renderer 的颜色纹理。
    Microsoft::WRL::ComPtr<ID3D12Resource> _heightTexture;
    // 帧内初始化时保留 upload 引用，确保提交前数据不被释放。
    Microsoft::WRL::ComPtr<ID3D12Resource> _heightUpload;
    Render::D3D12DescriptorAllocation _heightSrv;
    // 分类位置为每个槽保存四组候选三角形顶点。
    Microsoft::WRL::ComPtr<ID3D12Resource> _classificationPositions;
    // 渲染顶点遵循 TerrainMeshVertex 的 52-byte 输入布局。
    Microsoft::WRL::ComPtr<ID3D12Resource> _renderVertices;
    // 容量字节数由算法层写入借用资源契约并由 renderer 校验。
    std::size_t _classificationPositionCapacityBytes{0U};
    std::size_t _renderVertexCapacityBytes{0U};
    D3D12_RESOURCE_STATES _classificationState{D3D12_RESOURCE_STATE_UNORDERED_ACCESS};
    D3D12_RESOURCE_STATES _renderVertexState{D3D12_RESOURCE_STATE_UNORDERED_ACCESS};
};
} // namespace ParallelRoam::Algorithms::Cbt2024::D3D12
