#include "algorithms/cbt_2024/d3d12/D3D12CbtGeometryPipeline.h"

#include "render/D3D12GraphicsBackend.h"
#include "terrain/TerrainMeshBuilder.h"

#include <filesystem>
#include <fstream>
#include <cstring>
#include <vector>

namespace ParallelRoam::Algorithms::Cbt2024::D3D12
{
namespace
{
using Microsoft::WRL::ComPtr;

void SetError(std::string* errorMessage, const std::string& message)
{
    if (errorMessage != nullptr)
    {
        *errorMessage = message;
    }
}

std::vector<std::uint8_t> ReadBinaryFile(
    const std::filesystem::path& path,
    std::string* errorMessage)
{
    // shader 由 CMake/DXC 在构建期生成，运行期只装载固定 CSO。
    std::ifstream stream{path, std::ios::binary | std::ios::ate};
    if (!stream)
    {
        SetError(errorMessage, "Failed to open compiled CBT geometry shader: " + path.string());
        return {};
    }
    const std::streamsize size = stream.tellg();
    if (size <= 0)
    {
        SetError(errorMessage, "Compiled CBT geometry shader is empty: " + path.string());
        return {};
    }
    std::vector<std::uint8_t> bytes(static_cast<std::size_t>(size));
    stream.seekg(0, std::ios::beg);
    if (!stream.read(reinterpret_cast<char*>(bytes.data()), size))
    {
        SetError(errorMessage, "Failed to read compiled CBT geometry shader: " + path.string());
        return {};
    }
    return bytes;
}

bool CreateBuffer(
    ID3D12Device* device,
    std::size_t bytes,
    ComPtr<ID3D12Resource>& resource,
    std::string* errorMessage)
{
    // 分类位置和最终顶点都由 compute 写入，因此常驻 default heap UAV。
    D3D12_HEAP_PROPERTIES heap{};
    heap.Type = D3D12_HEAP_TYPE_DEFAULT;
    D3D12_RESOURCE_DESC description{};
    description.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    description.Width = bytes;
    description.Height = 1U;
    description.DepthOrArraySize = 1U;
    description.MipLevels = 1U;
    description.SampleDesc.Count = 1U;
    description.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    description.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
    if (FAILED(device->CreateCommittedResource(
            &heap,
            D3D12_HEAP_FLAG_NONE,
            &description,
            D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
            nullptr,
            IID_PPV_ARGS(&resource))))
    {
        SetError(errorMessage, "Failed to allocate CBT geometry buffer");
        return false;
    }
    return true;
}

bool CreateUploadBuffer(
    ID3D12Device* device,
    std::size_t bytes,
    ComPtr<ID3D12Resource>& resource,
    std::string* errorMessage)
{
    D3D12_HEAP_PROPERTIES heap{};
    heap.Type = D3D12_HEAP_TYPE_UPLOAD;
    D3D12_RESOURCE_DESC description{};
    description.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    description.Width = bytes;
    description.Height = 1U;
    description.DepthOrArraySize = 1U;
    description.MipLevels = 1U;
    description.SampleDesc.Count = 1U;
    description.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    if (FAILED(device->CreateCommittedResource(
            &heap,
            D3D12_HEAP_FLAG_NONE,
            &description,
            D3D12_RESOURCE_STATE_GENERIC_READ,
            nullptr,
            IID_PPV_ARGS(&resource))))
    {
        SetError(errorMessage, "Failed to allocate CBT height-map upload buffer");
        return false;
    }
    return true;
}

bool CreatePipeline(
    ID3D12Device* device,
    ID3D12RootSignature* rootSignature,
    const std::filesystem::path& shaderPath,
    ComPtr<ID3D12PipelineState>& pipeline,
    std::string* errorMessage)
{
    // 两条几何路径共享 bootstrap root signature，但拥有独立入口 PSO。
    const std::vector<std::uint8_t> shader = ReadBinaryFile(shaderPath, errorMessage);
    if (shader.empty())
    {
        return false;
    }
    D3D12_COMPUTE_PIPELINE_STATE_DESC description{};
    description.pRootSignature = rootSignature;
    description.CS = {shader.data(), shader.size()};
    if (FAILED(device->CreateComputePipelineState(&description, IID_PPV_ARGS(&pipeline))))
    {
        SetError(errorMessage, "Failed to create CBT geometry pipeline: " + shaderPath.string());
        return false;
    }
    return true;
}

void Transition(
    ID3D12GraphicsCommandList* commandList,
    ID3D12Resource* resource,
    D3D12_RESOURCE_STATES& currentState,
    D3D12_RESOURCE_STATES nextState)
{
    // 状态镜像与 barrier 同步更新，重复请求不会产生冗余 transition。
    if (resource == nullptr || currentState == nextState)
    {
        return;
    }
    D3D12_RESOURCE_BARRIER barrier{};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition.pResource = resource;
    barrier.Transition.StateBefore = currentState;
    barrier.Transition.StateAfter = nextState;
    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    commandList->ResourceBarrier(1U, &barrier);
    currentState = nextState;
}
} // namespace

bool D3D12CbtGeometryPipeline::Initialize(
    Render::D3D12GraphicsBackend& backend,
    ID3D12RootSignature* rootSignature,
    const CbtBaseTopology& topology,
    const Terrain::HeightMap& heightMap,
    std::string* errorMessage)
{
    // 重建先释放旧几何代，避免容量变化后保留不匹配的顶点缓冲。
    Shutdown();
    _backend = &backend;
    if (!heightMap.IsValid() || heightMap.Values().size() !=
        static_cast<std::size_t>(heightMap.Width()) * static_cast<std::size_t>(heightMap.Height()))
    {
        SetError(errorMessage, "CBT geometry requires a valid contiguous height map");
        Shutdown();
        return false;
    }
#if defined(PARALLEL_ROAM_DX12_SHADER_DIR)
    const std::filesystem::path shaderDirectory{PARALLEL_ROAM_DX12_SHADER_DIR};
#else
    const std::filesystem::path shaderDirectory{"assets/shaders/dx12"};
#endif
    // 每个槽保留四组三点分类位置；绘制缓冲则是每槽一个最终三角形。
    const std::size_t totalCount = topology.Layout.TotalElementCount;
    _classificationPositionCapacityBytes = totalCount * 4U * sizeof(float) * 3U;
    _renderVertexCapacityBytes = totalCount * 3U * sizeof(Terrain::TerrainMeshVertex);
    if (!CreatePipeline(
            backend.Device(),
            rootSignature,
            shaderDirectory / "CbtBootstrapBuildActiveGeometry.cso",
            _activePipeline,
            errorMessage) ||
        !CreatePipeline(
            backend.Device(),
            rootSignature,
            shaderDirectory / "CbtBootstrapBuildModifiedGeometry.cso",
            _modifiedPipeline,
            errorMessage) ||
        !CreatePipeline(
            backend.Device(),
            rootSignature,
            shaderDirectory / "CbtBootstrapValidateGeometryG.cso",
            _validatePipeline,
            errorMessage) ||
        !CreateBuffer(
            backend.Device(),
            _classificationPositionCapacityBytes,
            _classificationPositions,
            errorMessage) ||
        !CreateBuffer(
            backend.Device(),
            _renderVertexCapacityBytes,
            _renderVertices,
            errorMessage))
    {
        Shutdown();
        return false;
    }

    // R32_FLOAT 避免 UNORM 量化，使 GPU 双线性结果可与 HeightMap::SampleBilinear 逐值对照。
    // 纹理由算法状态持有，而不是借用 renderer 的颜色纹理。
    // 这样高度图重载能够与 topology、几何 PSO 一起原子替换。
    // 仅创建单 mip；CBT 几何要求确定的原始高度值而非 LOD 过滤结果。
    // 初始 COPY_DEST 状态覆盖帧内和 immediate 两种上传入口。
    D3D12_RESOURCE_DESC textureDescription{};
    textureDescription.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    textureDescription.Width = static_cast<UINT64>(heightMap.Width());
    textureDescription.Height = static_cast<UINT>(heightMap.Height());
    textureDescription.DepthOrArraySize = 1U;
    textureDescription.MipLevels = 1U;
    textureDescription.Format = DXGI_FORMAT_R32_FLOAT;
    textureDescription.SampleDesc.Count = 1U;
    textureDescription.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
    D3D12_HEAP_PROPERTIES defaultHeap{};
    defaultHeap.Type = D3D12_HEAP_TYPE_DEFAULT;
    if (FAILED(backend.Device()->CreateCommittedResource(
            &defaultHeap,
            D3D12_HEAP_FLAG_NONE,
            &textureDescription,
            D3D12_RESOURCE_STATE_COPY_DEST,
            nullptr,
            IID_PPV_ARGS(&_heightTexture))))
    {
        SetError(errorMessage, "Failed to allocate CBT height texture");
        Shutdown();
        return false;
    }

    D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint{};
    // GetCopyableFootprints 提供设备要求的行对齐，不能按紧密 CPU 行宽推导。
    // footprint.Offset 也必须计入映射地址，即使单子资源通常从零开始。
    // rowCount 来自 D3D12 布局查询，保持循环与目标子资源定义一致。
    // uploadBytes 覆盖尾部 padding，防止驱动读取越过 upload buffer。
    UINT rowCount = 0U;
    UINT64 uploadBytes = 0U;
    backend.Device()->GetCopyableFootprints(
        &textureDescription,
        0U,
        1U,
        0U,
        &footprint,
        &rowCount,
        nullptr,
        &uploadBytes);
    if (!CreateUploadBuffer(
            backend.Device(),
            static_cast<std::size_t>(uploadBytes),
            _heightUpload,
            errorMessage))
    {
        Shutdown();
        return false;
    }
    const D3D12_RANGE noRead{0U, 0U};
    void* mapped = nullptr;
    if (FAILED(_heightUpload->Map(0U, &noRead, &mapped)))
    {
        SetError(errorMessage, "Failed to map CBT height upload buffer");
        Shutdown();
        return false;
    }
    const std::size_t sourceRowBytes = static_cast<std::size_t>(heightMap.Width()) * sizeof(float);
    const auto values = heightMap.Values();
    // CPU 高度值按 row-major 紧密存储，目标行则使用 D3D12 RowPitch。
    // 每行只复制有效 float，padding 保持未定义且不会被纹理解释。
    // 上传资源在 geometry pipeline 生命周期内保留，覆盖帧内异步提交。
    // 高度图重载会等待旧算法资源空闲，再释放上一代 upload 引用。
    for (UINT row = 0U; row < rowCount; ++row)
    {
        std::memcpy(
            static_cast<std::uint8_t*>(mapped) + footprint.Offset +
                static_cast<std::size_t>(row) * footprint.Footprint.RowPitch,
            values.data() + static_cast<std::size_t>(row) * static_cast<std::size_t>(heightMap.Width()),
            sourceRowBytes);
    }
    _heightUpload->Unmap(0U, nullptr);

    _heightSrv = backend.AllocateSrvDescriptor();
    // SRV 来自后端统一 shader-visible heap，bootstrap 可直接绑定其 GPU handle。
    // 描述符与纹理同寿命，并在 Shutdown 中通过原始 allocation 精确归还。
    // R32_FLOAT 视图与资源格式一致，不依赖 typeless 重解释。
    if (!_heightSrv.IsValid())
    {
        SetError(errorMessage, "D3D12 descriptor heap has no slot for the CBT height map");
        Shutdown();
        return false;
    }
    D3D12_SHADER_RESOURCE_VIEW_DESC srv{};
    srv.Format = DXGI_FORMAT_R32_FLOAT;
    srv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    srv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srv.Texture2D.MipLevels = 1U;
    backend.Device()->CreateShaderResourceView(_heightTexture.Get(), &srv, _heightSrv.Cpu);

    const auto recordUpload = [&](ID3D12GraphicsCommandList* commandList, std::string*) {
        // 初始化发生在 BuildRenderData 时会复用当前帧 command list。
        // 独立初始化则通过 ExecuteImmediate 提交相同的 copy 和 barrier。
        // 两条路径最终都发布 NON_PIXEL_SHADER_RESOURCE，供 compute Load 读取。
        // 本阶段不创建 sampler，因为 shader 使用 Texture2D.Load 手动插值。
        D3D12_TEXTURE_COPY_LOCATION destination{};
        destination.pResource = _heightTexture.Get();
        destination.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        destination.SubresourceIndex = 0U;
        D3D12_TEXTURE_COPY_LOCATION source{};
        source.pResource = _heightUpload.Get();
        source.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
        source.PlacedFootprint = footprint;
        commandList->CopyTextureRegion(&destination, 0U, 0U, 0U, &source, nullptr);
        D3D12_RESOURCE_BARRIER barrier{};
        barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barrier.Transition.pResource = _heightTexture.Get();
        barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
        barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
        barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        commandList->ResourceBarrier(1U, &barrier);
        return true;
    };
    if (backend.FrameOpen())
    {
        if (backend.CommandList() == nullptr || !recordUpload(backend.CommandList(), errorMessage))
        {
            SetError(errorMessage, "CBT height upload requires an open command list");
            Shutdown();
            return false;
        }
    }
    else if (!backend.ExecuteImmediate(recordUpload, errorMessage))
    {
        Shutdown();
        return false;
    }
    return true;
}

void D3D12CbtGeometryPipeline::Shutdown()
{
    // 资源释放后把状态镜像恢复为 CreateBuffer 契约的 UAV 初值。
    _activePipeline.Reset();
    _modifiedPipeline.Reset();
    _validatePipeline.Reset();
    _heightTexture.Reset();
    _heightUpload.Reset();
    if (_backend != nullptr)
    {
        _backend->ReleaseSrvDescriptor(_heightSrv);
    }
    _classificationPositions.Reset();
    _renderVertices.Reset();
    _classificationPositionCapacityBytes = 0U;
    _renderVertexCapacityBytes = 0U;
    _classificationState = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    _renderVertexState = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    _backend = nullptr;
}

void D3D12CbtGeometryPipeline::TransitionClassification(
    ID3D12GraphicsCommandList* commandList,
    D3D12_RESOURCE_STATES nextState)
{
    // 分类位置在 classify 时作为 SRV，在几何更新时作为 UAV。
    Transition(commandList, _classificationPositions.Get(), _classificationState, nextState);
}

void D3D12CbtGeometryPipeline::TransitionVertices(
    ID3D12GraphicsCommandList* commandList,
    D3D12_RESOURCE_STATES nextState)
{
    // 最终顶点在 compute 写入与 renderer 顶点读取之间切换。
    Transition(commandList, _renderVertices.Get(), _renderVertexState, nextState);
}

ID3D12PipelineState* D3D12CbtGeometryPipeline::ActivePipeline() const
{
    return _activePipeline.Get();
}

ID3D12PipelineState* D3D12CbtGeometryPipeline::ModifiedPipeline() const
{
    return _modifiedPipeline.Get();
}

ID3D12PipelineState* D3D12CbtGeometryPipeline::ValidatePipeline() const
{
    return _validatePipeline.Get();
}

D3D12_GPU_DESCRIPTOR_HANDLE D3D12CbtGeometryPipeline::HeightMapSrv() const
{
    return _heightSrv.Gpu;
}

ID3D12Resource* D3D12CbtGeometryPipeline::ClassificationPositions() const
{
    return _classificationPositions.Get();
}

ID3D12Resource* D3D12CbtGeometryPipeline::RenderVertices() const
{
    return _renderVertices.Get();
}

std::size_t D3D12CbtGeometryPipeline::ClassificationPositionCapacityBytes() const
{
    return _classificationPositionCapacityBytes;
}

std::size_t D3D12CbtGeometryPipeline::RenderVertexCapacityBytes() const
{
    return _renderVertexCapacityBytes;
}
} // namespace ParallelRoam::Algorithms::Cbt2024::D3D12
