#include "render/TerrainRenderer.h"

#include "algorithms/TerrainLodView.h"
#include "algorithms/classic_roam/ClassicRoamTerrainLodAlgorithm.h"
#include "algorithms/data_oriented_roam/DataOrientedRoamTerrainLodAlgorithm.h"
#include "render/D3D12GraphicsBackend.h"
#include "tools/PerformanceTimer.h"

#include <stb_image.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <iostream>
#include <limits>
#include <memory>
#include <utility>
#include <vector>

namespace ParallelRoam::Render
{
namespace
{
constexpr float MinRoamRebuildDistance = 0.30F;
constexpr float RoamRebuildTerrainScale = 0.01F;
// D3D12 常量缓冲地址按 256 字节对齐
constexpr std::size_t ConstantBufferSize = 256U;

/// <summary>
/// 单个交换链帧使用的 CPU 网格上传资源
/// </summary>
struct D3D12MeshFrameResources
{
    // 每帧独立持有上传缓冲，避免覆盖 GPU 尚未消费的数据
    Microsoft::WRL::ComPtr<ID3D12Resource> VertexBuffer;
    Microsoft::WRL::ComPtr<ID3D12Resource> IndexBuffer;
    std::uint8_t* MappedVertices{nullptr};
    std::uint8_t* MappedIndices{nullptr};
    // 缓冲只在容量不足时增长
    std::size_t VertexCapacityBytes{0};
    std::size_t IndexCapacityBytes{0};
    std::uint64_t MeshGeneration{0};
    bool PendingFullUpload{true};
    std::vector<Algorithms::TerrainLodCpuMeshUpdateRange> PendingUpdateRanges;
    D3D12_VERTEX_BUFFER_VIEW VertexView{};
    D3D12_INDEX_BUFFER_VIEW IndexView{};
};

struct MeshUpdateInterval
{
    std::size_t First{0};
    std::size_t Count{0};
};

void CoalescePendingMeshUpdateRanges(
    std::vector<Algorithms::TerrainLodCpuMeshUpdateRange>& ranges)
{
    std::vector<MeshUpdateInterval> vertexIntervals;
    std::vector<MeshUpdateInterval> indexIntervals;
    vertexIntervals.reserve(ranges.size());
    indexIntervals.reserve(ranges.size());
    for (const Algorithms::TerrainLodCpuMeshUpdateRange& range : ranges)
    {
        if (range.VertexCount > 0U)
        {
            vertexIntervals.push_back(MeshUpdateInterval{range.FirstVertex, range.VertexCount});
        }
        if (range.IndexCount > 0U)
        {
            indexIntervals.push_back(MeshUpdateInterval{range.FirstIndex, range.IndexCount});
        }
    }

    const auto coalesce = [](std::vector<MeshUpdateInterval>& intervals) {
        std::sort(
            intervals.begin(),
            intervals.end(),
            [](const MeshUpdateInterval& left, const MeshUpdateInterval& right) {
                return left.First < right.First;
            });
        std::size_t outputCount = 0U;
        for (const MeshUpdateInterval& interval : intervals)
        {
            if (outputCount == 0U)
            {
                intervals[outputCount++] = interval;
                continue;
            }

            MeshUpdateInterval& previous = intervals[outputCount - 1U];
            const std::size_t previousEnd = previous.First + previous.Count;
            if (interval.First <= previousEnd)
            {
                const std::size_t intervalEnd = interval.First + interval.Count;
                previous.Count = std::max(previousEnd, intervalEnd) - previous.First;
                continue;
            }
            intervals[outputCount++] = interval;
        }
        intervals.resize(outputCount);
    };
    coalesce(vertexIntervals);
    coalesce(indexIntervals);

    // 两类区间允许独立为空；按序配对只用于压缩存储，不改变各自的复制范围。
    ranges.assign(std::max(vertexIntervals.size(), indexIntervals.size()), {});
    for (std::size_t index = 0U; index < vertexIntervals.size(); ++index)
    {
        ranges[index].FirstVertex = vertexIntervals[index].First;
        ranges[index].VertexCount = vertexIntervals[index].Count;
    }
    for (std::size_t index = 0U; index < indexIntervals.size(); ++index)
    {
        ranges[index].FirstIndex = indexIntervals[index].First;
        ranges[index].IndexCount = indexIntervals[index].Count;
    }
}

/// <summary>
/// 与 Terrain.hlsl 保持相同布局的帧常量
/// </summary>
struct TerrainConstants
{
    glm::mat4 View{1.0F};
    glm::mat4 Projection{1.0F};
    glm::vec4 CameraPosition{0.0F, 0.0F, 0.0F, 1.0F};
    glm::vec4 LightDirection{0.0F, -1.0F, 0.0F, 0.0F};
    glm::vec4 LightColor{1.0F};
    // 参数顺序必须与 HLSL cbuffer 保持一致
    glm::vec4 LightingParameters{0.0F};
    glm::ivec4 DebugParameters{0};
    // 补齐 D3D12 常量缓冲的 256 字节对齐
    std::array<std::uint32_t, 12> Padding{};
};

// 编译期锁定 CPU 与 HLSL 的常量缓冲布局契约
static_assert(sizeof(TerrainConstants) == ConstantBufferSize);

D3D12_HEAP_PROPERTIES HeapProperties(D3D12_HEAP_TYPE type)
{
    D3D12_HEAP_PROPERTIES properties{};
    properties.Type = type;
    properties.CreationNodeMask = 1;
    properties.VisibleNodeMask = 1;
    return properties;
}

D3D12_RESOURCE_DESC BufferDescription(std::uint64_t size)
{
    D3D12_RESOURCE_DESC description{};
    description.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    // D3D12 不允许创建零字节资源，空数据在上层单独拒绝
    description.Width = std::max<std::uint64_t>(size, 1U);
    description.Height = 1;
    description.DepthOrArraySize = 1;
    description.MipLevels = 1;
    description.SampleDesc.Count = 1;
    description.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    return description;
}

void SetError(std::string* errorMessage, const std::string& message)
{
    if (errorMessage != nullptr)
    {
        *errorMessage = message;
    }
}

bool NeedsMeshRebuild(const TerrainRenderSettings& previous, const TerrainRenderSettings& next)
{
    // 仅筛选会改变几何或 LOD 拓扑的设置，材质参数不应触发重建
    return previous.TerrainSize != next.TerrainSize ||
           previous.HeightScale != next.HeightScale ||
           previous.UseTerrainLod != next.UseTerrainLod ||
           previous.TerrainLodAlgorithm != next.TerrainLodAlgorithm ||
           previous.RoamMaxDepth != next.RoamMaxDepth ||
           previous.RoamScreenSpaceSplitThresholdPixels != next.RoamScreenSpaceSplitThresholdPixels ||
           previous.RoamScreenSpaceMergeThresholdPixels != next.RoamScreenSpaceMergeThresholdPixels ||
           previous.RoamTriangleBudget != next.RoamTriangleBudget ||
           previous.RoamEnableParallelSplit != next.RoamEnableParallelSplit ||
           previous.RoamPassPolicy != next.RoamPassPolicy ||
           previous.RoamEnableLocalConstraints != next.RoamEnableLocalConstraints ||
           previous.RoamEnableTopologyValidation != next.RoamEnableTopologyValidation ||
           previous.RoamEnablePassEvidence != next.RoamEnablePassEvidence;
}

bool RoamViewInputsChanged(const RenderContext& previous, const RenderContext& next)
{
    if (previous.DrawableWidth != next.DrawableWidth ||
        previous.DrawableHeight != next.DrawableHeight ||
        previous.UsesZeroToOneDepth != next.UsesZeroToOneDepth)
    {
        return true;
    }

    for (glm::length_t column = 0; column < 4; ++column)
    {
        for (glm::length_t row = 0; row < 4; ++row)
        {
            if (previous.Projection[column][row] != next.Projection[column][row])
            {
                return true;
            }
        }
    }
    for (glm::length_t column = 0; column < 3; ++column)
    {
        for (glm::length_t row = 0; row < 3; ++row)
        {
            if (previous.View[column][row] != next.View[column][row])
            {
                return true;
            }
        }
    }
    return false;
}

glm::vec3 NormalizeLightDirection(const glm::vec3& lightDirection)
{
    if (glm::dot(lightDirection, lightDirection) <= 0.000001F)
    {
        // 零向量无法归一化，使用稳定默认方向避免向着色器写入 NaN
        return glm::normalize(glm::vec3{-0.45F, -1.0F, -0.35F});
    }
    return glm::normalize(lightDirection);
}

std::unique_ptr<Algorithms::ITerrainLodAlgorithm> CreateTerrainLodAlgorithm(
    Algorithms::TerrainLodAlgorithmId algorithmId)
{
    // 两种 CPU 算法与图形 API 无关，D3D12 只消费其共享 mesh 输出。
    if (algorithmId == Algorithms::TerrainLodAlgorithmId::ClassicCpuRoam)
    {
        return std::make_unique<Algorithms::ClassicRoam::ClassicRoamTerrainLodAlgorithm>();
    }
    if (algorithmId == Algorithms::TerrainLodAlgorithmId::DataOrientedCpuRoam)
    {
        return std::make_unique<Algorithms::DataOrientedRoam::DataOrientedRoamTerrainLodAlgorithm>();
    }
    return nullptr;
}

std::vector<std::uint8_t> ReadBinaryFile(const std::filesystem::path& path, std::string* errorMessage)
{
    std::ifstream stream{path, std::ios::binary | std::ios::ate};
    if (!stream)
    {
        SetError(errorMessage, "Failed to open compiled D3D12 shader: " + path.string());
        return {};
    }
    const std::streamsize size = stream.tellg();
    if (size <= 0)
    {
        SetError(errorMessage, "Compiled D3D12 shader is empty: " + path.string());
        return {};
    }
    std::vector<std::uint8_t> bytes(static_cast<std::size_t>(size));
    stream.seekg(0, std::ios::beg);
    if (!stream.read(reinterpret_cast<char*>(bytes.data()), size))
    {
        SetError(errorMessage, "Failed to read compiled D3D12 shader: " + path.string());
        return {};
    }
    return bytes;
}

bool CreateMappedUploadBuffer(
    ID3D12Device* device,
    std::size_t size,
    Microsoft::WRL::ComPtr<ID3D12Resource>& resource,
    std::uint8_t*& mappedMemory,
    std::string* errorMessage)
{
    // 上传堆支持 CPU 持久映射，生命周期内不重复 Map 和 Unmap
    const D3D12_HEAP_PROPERTIES heapProperties = HeapProperties(D3D12_HEAP_TYPE_UPLOAD);
    const D3D12_RESOURCE_DESC description = BufferDescription(size);
    const HRESULT result = device->CreateCommittedResource(
        &heapProperties,
        D3D12_HEAP_FLAG_NONE,
        &description,
        D3D12_RESOURCE_STATE_GENERIC_READ,
        nullptr,
        IID_PPV_ARGS(&resource));
    if (FAILED(result))
    {
        SetError(errorMessage, "D3D12 upload buffer allocation failed");
        return false;
    }
    // CPU 只写该资源，空读取范围允许驱动省略回读同步
    const D3D12_RANGE noReadRange{0, 0};
    void* mapped = nullptr;
    if (FAILED(resource->Map(0, &noReadRange, &mapped)))
    {
        SetError(errorMessage, "D3D12 upload buffer mapping failed");
        resource.Reset();
        return false;
    }
    mappedMemory = static_cast<std::uint8_t*>(mapped);
    return true;
}

D3D12_BLEND_DESC OpaqueBlendDescription()
{
    D3D12_BLEND_DESC description{};
    description.RenderTarget[0].BlendEnable = FALSE;
    description.RenderTarget[0].LogicOpEnable = FALSE;
    description.RenderTarget[0].SrcBlend = D3D12_BLEND_ONE;
    description.RenderTarget[0].DestBlend = D3D12_BLEND_ZERO;
    description.RenderTarget[0].BlendOp = D3D12_BLEND_OP_ADD;
    description.RenderTarget[0].SrcBlendAlpha = D3D12_BLEND_ONE;
    description.RenderTarget[0].DestBlendAlpha = D3D12_BLEND_ZERO;
    description.RenderTarget[0].BlendOpAlpha = D3D12_BLEND_OP_ADD;
    description.RenderTarget[0].LogicOp = D3D12_LOGIC_OP_NOOP;
    description.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
    return description;
}

D3D12_RASTERIZER_DESC RasterizerDescription(D3D12_FILL_MODE fillMode)
{
    // 禁用剔除以容忍 CPU 和 GPU 网格在调试期间的绕序差异
    D3D12_RASTERIZER_DESC description{};
    description.FillMode = fillMode;
    description.CullMode = D3D12_CULL_MODE_NONE;
    description.DepthClipEnable = TRUE;
    return description;
}

D3D12_DEPTH_STENCIL_DESC DepthStencilDescription()
{
    // LESS_EQUAL 允许共享边界上的等深度片元保持稳定
    D3D12_DEPTH_STENCIL_DESC description{};
    description.DepthEnable = TRUE;
    description.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;
    description.DepthFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL;
    description.StencilEnable = FALSE;
    return description;
}
} // namespace

/// <summary>
/// D3D12 地形渲染器持有的管线对象和逐帧资源
/// </summary>
struct D3D12TerrainRendererState
{
    // Backend 由 Application 持有，本状态只借用
    D3D12GraphicsBackend* Backend{nullptr};
    Microsoft::WRL::ComPtr<ID3D12RootSignature> RootSignature;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> FillPipelineState;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> WireframePipelineState;

    // CPU 网格和常量缓冲按交换链帧隔离
    std::array<D3D12MeshFrameResources, D3D12GraphicsBackend::FrameCount> MeshFrames;
    std::array<Microsoft::WRL::ComPtr<ID3D12Resource>, D3D12GraphicsBackend::FrameCount> ConstantBuffers;
    std::array<std::uint8_t*, D3D12GraphicsBackend::FrameCount> MappedConstants{};
    Microsoft::WRL::ComPtr<ID3D12Resource> Texture;
    D3D12DescriptorAllocation TextureSrv;
    std::uint64_t MeshGeneration{0};
};

namespace
{
bool CreateRootSignature(D3D12TerrainRendererState& state, std::string* errorMessage)
{
    // 根参数固定为帧常量 b0 和地形纹理 t0
    D3D12_DESCRIPTOR_RANGE srvRange{};
    srvRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    srvRange.NumDescriptors = 1;
    srvRange.BaseShaderRegister = 0;
    srvRange.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

    // 根 CBV 避免为每帧常量额外分配 CBV 描述符
    std::array<D3D12_ROOT_PARAMETER, 2> parameters{};
    parameters[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    parameters[0].Descriptor.ShaderRegister = 0;
    parameters[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    parameters[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    parameters[1].DescriptorTable.NumDescriptorRanges = 1;
    parameters[1].DescriptorTable.pDescriptorRanges = &srvRange;
    parameters[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

    // 地形纹理使用静态线性环绕采样器，避免额外占用描述符
    D3D12_STATIC_SAMPLER_DESC sampler{};
    sampler.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
    sampler.AddressU = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    sampler.AddressV = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    sampler.AddressW = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    sampler.ComparisonFunc = D3D12_COMPARISON_FUNC_ALWAYS;
    sampler.MaxLOD = D3D12_FLOAT32_MAX;
    sampler.ShaderRegister = 0;
    sampler.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

    D3D12_ROOT_SIGNATURE_DESC description{};
    description.NumParameters = static_cast<UINT>(parameters.size());
    description.pParameters = parameters.data();
    description.NumStaticSamplers = 1;
    description.pStaticSamplers = &sampler;
    // 未使用的可编程阶段明确拒绝根访问以缩小驱动验证范围
    description.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT |
                        D3D12_ROOT_SIGNATURE_FLAG_DENY_HULL_SHADER_ROOT_ACCESS |
                        D3D12_ROOT_SIGNATURE_FLAG_DENY_DOMAIN_SHADER_ROOT_ACCESS |
                        D3D12_ROOT_SIGNATURE_FLAG_DENY_GEOMETRY_SHADER_ROOT_ACCESS;

    Microsoft::WRL::ComPtr<ID3DBlob> serialized;
    Microsoft::WRL::ComPtr<ID3DBlob> errors;
    // 序列化错误 blob 提供比 HRESULT 更具体的根签名诊断
    HRESULT result = D3D12SerializeRootSignature(
        &description,
        D3D_ROOT_SIGNATURE_VERSION_1,
        &serialized,
        &errors);
    if (FAILED(result))
    {
        const char* detail = errors != nullptr ? static_cast<const char*>(errors->GetBufferPointer()) : "unknown error";
        SetError(errorMessage, std::string{"D3D12 root signature serialization failed: "} + detail);
        return false;
    }
    result = state.Backend->Device()->CreateRootSignature(
        0,
        serialized->GetBufferPointer(),
        serialized->GetBufferSize(),
        IID_PPV_ARGS(&state.RootSignature));
    if (FAILED(result))
    {
        SetError(errorMessage, "D3D12 root signature creation failed");
        return false;
    }
    return true;
}

bool CreatePipelineStates(D3D12TerrainRendererState& state, std::string* errorMessage)
{
    // 着色器目录由构建系统注入，回退路径只服务于手动运行
#if defined(PARALLEL_ROAM_DX12_SHADER_DIR)
    const std::filesystem::path shaderDirectory{PARALLEL_ROAM_DX12_SHADER_DIR};
#else
    const std::filesystem::path shaderDirectory{"assets/shaders/dx12"};
#endif
    // 顶点和像素字节码必须来自同一构建配置
    const std::vector<std::uint8_t> vertexShader = ReadBinaryFile(shaderDirectory / "TerrainVS.cso", errorMessage);
    if (vertexShader.empty())
    {
        return false;
    }
    const std::vector<std::uint8_t> pixelShader = ReadBinaryFile(shaderDirectory / "TerrainPS.cso", errorMessage);
    if (pixelShader.empty())
    {
        return false;
    }

    // 输入布局必须与跨后端共享的 TerrainMeshVertex 内存布局一致
    const std::array<D3D12_INPUT_ELEMENT_DESC, 6> inputElements{{
        {"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, static_cast<UINT>(offsetof(Terrain::TerrainMeshVertex, Position)), D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
        {"NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, static_cast<UINT>(offsetof(Terrain::TerrainMeshVertex, Normal)), D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
        {"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, static_cast<UINT>(offsetof(Terrain::TerrainMeshVertex, TexCoord)), D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
        {"TEXCOORD", 1, DXGI_FORMAT_R32_FLOAT, 0, static_cast<UINT>(offsetof(Terrain::TerrainMeshVertex, Height)), D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
        {"COLOR", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, static_cast<UINT>(offsetof(Terrain::TerrainMeshVertex, DebugColor)), D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
        {"TEXCOORD", 2, DXGI_FORMAT_R32_FLOAT, 0, static_cast<UINT>(offsetof(Terrain::TerrainMeshVertex, DebugHighlight)), D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
    }};

    // PSO 格式必须与后端交换链和共享深度资源一致
    D3D12_GRAPHICS_PIPELINE_STATE_DESC description{};
    description.pRootSignature = state.RootSignature.Get();
    description.VS = {vertexShader.data(), vertexShader.size()};
    description.PS = {pixelShader.data(), pixelShader.size()};
    description.BlendState = OpaqueBlendDescription();
    description.SampleMask = std::numeric_limits<UINT>::max();
    description.RasterizerState = RasterizerDescription(D3D12_FILL_MODE_SOLID);
    description.DepthStencilState = DepthStencilDescription();
    description.InputLayout = {inputElements.data(), static_cast<UINT>(inputElements.size())};
    description.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    description.NumRenderTargets = 1;
    description.RTVFormats[0] = state.Backend->RenderTargetFormat();
    description.DSVFormat = state.Backend->DepthStencilFormat();
    description.SampleDesc.Count = 1;
    // 先创建默认实体 PSO，失败时不尝试派生线框版本
    HRESULT result = state.Backend->Device()->CreateGraphicsPipelineState(
        &description,
        IID_PPV_ARGS(&state.FillPipelineState));
    if (FAILED(result))
    {
        SetError(errorMessage, "D3D12 solid terrain pipeline creation failed");
        return false;
    }
    // 线框模式只替换光栅化状态，其余着色器和资源契约完全复用
    description.RasterizerState = RasterizerDescription(D3D12_FILL_MODE_WIREFRAME);
    result = state.Backend->Device()->CreateGraphicsPipelineState(
        &description,
        IID_PPV_ARGS(&state.WireframePipelineState));
    if (FAILED(result))
    {
        SetError(errorMessage, "D3D12 wireframe terrain pipeline creation failed");
        return false;
    }
    return true;
}

bool InitializeState(D3D12TerrainRendererState& state, std::string* errorMessage)
{
    if (!CreateRootSignature(state, errorMessage) || !CreatePipelineStates(state, errorMessage))
    {
        return false;
    }
    // 常量缓冲按交换链帧拆分，写入前无需等待整个 GPU 队列空闲
    for (std::uint32_t frameIndex = 0; frameIndex < D3D12GraphicsBackend::FrameCount; ++frameIndex)
    {
        if (!CreateMappedUploadBuffer(
                state.Backend->Device(),
                ConstantBufferSize,
                state.ConstantBuffers[frameIndex],
                state.MappedConstants[frameIndex],
                errorMessage))
        {
            return false;
        }
    }

    return true;
}

void ReleaseMappedResource(Microsoft::WRL::ComPtr<ID3D12Resource>& resource, std::uint8_t*& mapped)
{
    // 映射指针只在资源仍存活时有效，释放后同时清空防止重复 Unmap
    if (resource != nullptr && mapped != nullptr)
    {
        resource->Unmap(0, nullptr);
    }
    mapped = nullptr;
    resource.Reset();
}

bool UploadMeshForFrame(
    D3D12TerrainRendererState& state,
    const Terrain::TerrainMeshData& meshData,
    std::uint32_t frameIndex,
    std::size_t* uploadedBytes,
    bool* uploadedAllVertices,
    bool* uploadedAllIndices,
    std::string* errorMessage)
{
    if (uploadedAllVertices != nullptr)
    {
        *uploadedAllVertices = false;
    }
    if (uploadedAllIndices != nullptr)
    {
        *uploadedAllIndices = false;
    }
    D3D12MeshFrameResources& frame = state.MeshFrames[frameIndex];
    // 当前交换链帧已拥有该版本时不重复复制大型网格
    if (frame.MeshGeneration == state.MeshGeneration)
    {
        return true;
    }

    const std::size_t vertexBytes = meshData.Vertices.size() * sizeof(Terrain::TerrainMeshVertex);
    const std::size_t indexBytes = meshData.Indices.size() * sizeof(std::uint32_t);
    if (vertexBytes == 0 || indexBytes == 0)
    {
        SetError(errorMessage, "D3D12 terrain mesh upload received empty data");
        return false;
    }

    // 缓冲采用只增长策略，避免相机移动触发 LOD 更新时频繁分配
    bool uploadAllVertices = frame.PendingFullUpload;
    bool uploadAllIndices = frame.PendingFullUpload;
    if (frame.VertexCapacityBytes < vertexBytes)
    {
        ReleaseMappedResource(frame.VertexBuffer, frame.MappedVertices);
        if (!CreateMappedUploadBuffer(
                state.Backend->Device(), vertexBytes, frame.VertexBuffer, frame.MappedVertices, errorMessage))
        {
            return false;
        }
        frame.VertexCapacityBytes = vertexBytes;
        uploadAllVertices = true;
    }
    if (frame.IndexCapacityBytes < indexBytes)
    {
        ReleaseMappedResource(frame.IndexBuffer, frame.MappedIndices);
        if (!CreateMappedUploadBuffer(
                state.Backend->Device(), indexBytes, frame.IndexBuffer, frame.MappedIndices, errorMessage))
        {
            return false;
        }
        frame.IndexCapacityBytes = indexBytes;
        uploadAllIndices = true;
    }
    if (uploadedAllVertices != nullptr)
    {
        *uploadedAllVertices = uploadAllVertices;
    }
    if (uploadedAllIndices != nullptr)
    {
        *uploadedAllIndices = uploadAllIndices;
    }

    // 后端在复用帧索引前已经等待对应 fence，因此此处可直接写入
    std::size_t copiedBytes = 0U;
    if (uploadAllVertices)
    {
        std::memcpy(frame.MappedVertices, meshData.Vertices.data(), vertexBytes);
        copiedBytes += vertexBytes;
    }
    else
    {
        for (const Algorithms::TerrainLodCpuMeshUpdateRange& range : frame.PendingUpdateRanges)
        {
            if (range.FirstVertex > meshData.Vertices.size())
            {
                continue;
            }
            const std::size_t count = std::min(
                range.VertexCount,
                meshData.Vertices.size() - range.FirstVertex);
            const std::size_t rangeBytes = count * sizeof(Terrain::TerrainMeshVertex);
            std::memcpy(
                frame.MappedVertices + range.FirstVertex * sizeof(Terrain::TerrainMeshVertex),
                meshData.Vertices.data() + range.FirstVertex,
                rangeBytes);
            copiedBytes += rangeBytes;
        }
    }
    if (uploadAllIndices)
    {
        std::memcpy(frame.MappedIndices, meshData.Indices.data(), indexBytes);
        copiedBytes += indexBytes;
    }
    else
    {
        for (const Algorithms::TerrainLodCpuMeshUpdateRange& range : frame.PendingUpdateRanges)
        {
            if (range.FirstIndex > meshData.Indices.size())
            {
                continue;
            }
            const std::size_t count = std::min(range.IndexCount, meshData.Indices.size() - range.FirstIndex);
            const std::size_t rangeBytes = count * sizeof(std::uint32_t);
            std::memcpy(
                frame.MappedIndices + range.FirstIndex * sizeof(std::uint32_t),
                meshData.Indices.data() + range.FirstIndex,
                rangeBytes);
            copiedBytes += rangeBytes;
        }
    }
    frame.VertexView.BufferLocation = frame.VertexBuffer->GetGPUVirtualAddress();
    frame.VertexView.SizeInBytes = static_cast<UINT>(vertexBytes);
    frame.VertexView.StrideInBytes = sizeof(Terrain::TerrainMeshVertex);
    frame.IndexView.BufferLocation = frame.IndexBuffer->GetGPUVirtualAddress();
    frame.IndexView.SizeInBytes = static_cast<UINT>(indexBytes);
    frame.IndexView.Format = DXGI_FORMAT_R32_UINT;
    frame.MeshGeneration = state.MeshGeneration;
    frame.PendingFullUpload = false;
    frame.PendingUpdateRanges.clear();
    if (uploadedBytes != nullptr)
    {
        *uploadedBytes = copiedBytes;
    }
    return true;
}

} // namespace

TerrainRenderer::TerrainRenderer() = default;

TerrainRenderer::~TerrainRenderer()
{
    Shutdown();
}

bool TerrainRenderer::Initialize(
    IGraphicsBackend& graphicsBackend,
    const std::filesystem::path& heightMapPath,
    const std::filesystem::path& texturePath,
    const TerrainRenderSettings& settings,
    std::string* errorMessage)
{
    if (graphicsBackend.Api() != GraphicsApi::Direct3D12)
    {
        SetError(errorMessage, "D3D12 terrain renderer received a non-D3D12 graphics backend");
        return false;
    }
    // API 枚举通过后仍验证动态类型，避免错误工厂实现造成未定义行为
    auto* backend = dynamic_cast<D3D12GraphicsBackend*>(&graphicsBackend);
    if (backend == nullptr)
    {
        SetError(errorMessage, "D3D12 terrain renderer could not access the concrete backend");
        return false;
    }

    _graphicsBackend = &graphicsBackend;
    _d3d12State = std::make_unique<D3D12TerrainRendererState>();
    _d3d12State->Backend = backend;
    _settings = settings;
    _heightMapPath = heightMapPath;
    _texturePath = texturePath;
    // 先建立管线和高度数据，再生成几何并上传纹理
    if (!InitializeState(*_d3d12State, errorMessage) ||
        !_heightMap.LoadFromFile(heightMapPath, errorMessage))
    {
        Shutdown();
        return false;
    }
    if (!RebuildMesh(errorMessage))
    {
        Shutdown();
        return false;
    }
    if (!LoadTexture(texturePath, errorMessage))
    {
        Shutdown();
        return false;
    }
    _initialized = true;
    return true;
}

bool TerrainRenderer::ApplySettings(const TerrainRenderSettings& settings, std::string* errorMessage)
{
    // 光照和线框设置立即生效，只有几何相关设置会标记网格过期
    const bool rebuildMesh = NeedsMeshRebuild(_settings, settings);
    _settings = settings;
    _meshDirty = _meshDirty || rebuildMesh;
    return !_meshDirty || RebuildMesh(errorMessage);
}

bool TerrainRenderer::LoadHeightMap(const std::filesystem::path& heightMapPath, std::string* errorMessage)
{
    Terrain::HeightMap nextHeightMap;
    if (!nextHeightMap.LoadFromFile(heightMapPath, errorMessage))
    {
        return false;
    }
    // 加载完整后一次性交换，失败不会破坏现有高度数据
    _heightMap = std::move(nextHeightMap);
    _heightMapPath = heightMapPath;
    // 高度图改变会使跨帧拓扑和 CPU mesh 同时失效。
    ResetTerrainLodAlgorithm();
    return RebuildMesh(errorMessage);
}

bool TerrainRenderer::UpdateForView(const RenderContext& context, std::string* errorMessage)
{
    _lastRenderContext = context;
    if (!_settings.UseTerrainLod && !_meshDirty)
    {
        return true;
    }
    if (_settings.UseTerrainLod)
    {
        // 阈值随地形尺度增长，同时保留小地形的最小移动距离
        const float rebuildDistance = std::max(
            _settings.TerrainSize * RoamRebuildTerrainScale,
            MinRoamRebuildDistance);
        const glm::vec3 buildDelta = context.CameraPosition - _lastRoamBuildContext.CameraPosition;
        // 使用平方距离避免每帧为判定执行开方
        const bool cameraMovedEnough = !_hasRoamBuildView ||
            glm::dot(buildDelta, buildDelta) >= rebuildDistance * rebuildDistance;
        const bool usesRoamView =
            _settings.TerrainLodAlgorithm == Algorithms::TerrainLodAlgorithmId::ClassicCpuRoam ||
            _settings.TerrainLodAlgorithm == Algorithms::TerrainLodAlgorithmId::DataOrientedCpuRoam;
        const bool roamViewChanged = usesRoamView &&
            (!_hasRoamBuildView || RoamViewInputsChanged(_lastRoamBuildContext, context));
        if (!_meshDirty && !cameraMovedEnough && !roamViewChanged)
        {
            return true;
        }
        return RebuildTerrainLod(context, errorMessage);
    }
    return RebuildMesh(errorMessage);
}

void TerrainRenderer::RequestMeshRebuild()
{
    _meshDirty = true;
}

void TerrainRenderer::ResetTerrainLodAlgorithm()
{
    _terrainLodAlgorithm.reset();
    _borrowedCpuMeshData = nullptr;
    _terrainLodStats = {};
    _terrainLodStatusMessage.clear();
    _terrainLodTotalMilliseconds = 0.0F;
    _terrainLodCpuUploadMilliseconds = 0.0F;
    _lastCpuMeshUpdateRanges.clear();
    _lastCpuMeshRequiresFullUpload = true;
    _drawVertexCount = 0U;
    _drawIndexCount = 0U;
    _drawTriangleCount = 0U;
    _renderMode = Algorithms::TerrainLodRenderMode::CpuMesh;
    _hasRoamBuildView = false;
    _meshDirty = true;
}

void TerrainRenderer::Shutdown()
{
    _terrainLodAlgorithm.reset();
    _borrowedCpuMeshData = nullptr;
    _terrainLodStats = {};
    _terrainLodStatusMessage.clear();
    if (_d3d12State != nullptr)
    {
        // 持久映射资源必须在释放 COM 引用前显式解除映射
        for (D3D12MeshFrameResources& frame : _d3d12State->MeshFrames)
        {
            ReleaseMappedResource(frame.VertexBuffer, frame.MappedVertices);
            ReleaseMappedResource(frame.IndexBuffer, frame.MappedIndices);
        }
        for (std::uint32_t index = 0; index < D3D12GraphicsBackend::FrameCount; ++index)
        {
            ReleaseMappedResource(_d3d12State->ConstantBuffers[index], _d3d12State->MappedConstants[index]);
        }
        // SRV 槽位归后端分配器管理，纹理 COM 引用由状态对象自行释放
        if (_d3d12State->Backend != nullptr)
        {
            _d3d12State->Backend->ReleaseSrvDescriptor(_d3d12State->TextureSrv);
        }
        _d3d12State.reset();
    }
    _drawVertexCount = 0U;
    _drawIndexCount = 0U;
    _drawTriangleCount = 0U;
    _renderMode = Algorithms::TerrainLodRenderMode::CpuMesh;
    _graphicsBackend = nullptr;
    _initialized = false;
}

void TerrainRenderer::Render(const RenderContext& context)
{
    if (!_initialized || !HasDrawableTerrain() || _d3d12State == nullptr)
    {
        return;
    }
    D3D12GraphicsBackend& backend = *_d3d12State->Backend;
    ID3D12GraphicsCommandList* commandList = backend.CommandList();
    if (commandList == nullptr)
    {
        return;
    }

    // 后端保证当前帧资源的 fence 已完成，可更新对应上传缓冲
    const std::uint32_t frameIndex = backend.CurrentFrameIndex();
    if (_renderMode == Algorithms::TerrainLodRenderMode::CpuMesh)
    {
        // 网格版本按帧懒同步，首次轮转到该帧时才复制数据
        std::string uploadError;
        const Terrain::TerrainMeshData* cpuMesh = _borrowedCpuMeshData != nullptr
            ? _borrowedCpuMeshData
            : &_meshData;
        if (cpuMesh == nullptr ||
            !UploadMeshForFrame(
                *_d3d12State,
                *cpuMesh,
                frameIndex,
                nullptr,
                nullptr,
                nullptr,
                &uploadError))
        {
            std::cerr << uploadError << '\n';
            return;
        }
    }

    // 常量缓冲逐帧独立，CPU 写入不会覆盖尚未执行的前序绘制
    TerrainConstants constants{};
    constants.View = context.View;
    constants.Projection = context.Projection;
    constants.CameraPosition = glm::vec4{context.CameraPosition, 1.0F};
    constants.LightDirection = glm::vec4{NormalizeLightDirection(_settings.LightDirection), 0.0F};
    constants.LightColor = glm::vec4{_settings.LightColor, 1.0F};
    constants.LightingParameters = glm::vec4{
        _settings.AmbientStrength,
        _settings.DiffuseStrength,
        _settings.SpecularStrength,
        _settings.DebugOverlayStrength};
    constants.DebugParameters.x = static_cast<int>(_settings.DebugColorMode);
    std::memcpy(_d3d12State->MappedConstants[frameIndex], &constants, sizeof(constants));

    // 根描述符表中的 GPU 句柄只对当前绑定的共享堆有效
    ID3D12DescriptorHeap* graphicsHeaps[] = {backend.ShaderVisibleSrvHeap()};
    commandList->SetDescriptorHeaps(1, graphicsHeaps);
    commandList->SetPipelineState(
        _settings.Wireframe ? _d3d12State->WireframePipelineState.Get() : _d3d12State->FillPipelineState.Get());
    commandList->SetGraphicsRootSignature(_d3d12State->RootSignature.Get());
    commandList->SetGraphicsRootConstantBufferView(
        0,
        _d3d12State->ConstantBuffers[frameIndex]->GetGPUVirtualAddress());
    commandList->SetGraphicsRootDescriptorTable(1, _d3d12State->TextureSrv.Gpu);
    commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    // CPU 算法走本帧上传缓冲并直接提交固定参数绘制
    D3D12MeshFrameResources& frame = _d3d12State->MeshFrames[frameIndex];
    commandList->IASetVertexBuffers(0, 1, &frame.VertexView);
    commandList->IASetIndexBuffer(&frame.IndexView);
    commandList->DrawIndexedInstanced(static_cast<UINT>(_drawIndexCount), 1, 0, 0, 0);
}

TerrainRenderStats TerrainRenderer::Stats() const
{
    TerrainRenderStats stats{};
    stats.RoamLodStats = _terrainLodStats;
    stats.HeightMapPath = _heightMapPath;
    stats.HeightMapWidth = _heightMap.Width();
    stats.HeightMapHeight = _heightMap.Height();
    stats.VertexCount = _drawVertexCount;
    stats.TriangleCount = _drawTriangleCount;
    stats.DrawCallCount = _initialized && HasDrawableTerrain() ? 1 : 0;
    stats.TerrainSize = _settings.TerrainSize;
    stats.HeightScale = _settings.HeightScale;
    stats.UseTerrainLod = _settings.UseTerrainLod;
    stats.TerrainLodAlgorithm = _settings.TerrainLodAlgorithm;
    stats.TerrainLodStatusMessage = _terrainLodStatusMessage;
    stats.RoamPassTraces = _terrainLodStats.PassTraces;
    stats.RoamBuildSequence = _terrainLodStats.BuildSequence;
    stats.RoamReplayInputHash = _terrainLodStats.ReplayInputHash;
    stats.RoamTopologyHash = _terrainLodStats.TopologyHash;
    stats.RoamActiveLeafHash = _terrainLodStats.ActiveLeafHash;
    stats.RoamMeshHash = _terrainLodStats.MeshHash;
    stats.RoamNormalizedMeshHash = _terrainLodStats.NormalizedMeshHash;
    stats.RoamEvidenceTriangleBudget = _terrainLodStats.TriangleBudget;
    stats.RoamBudgetViolationCount = _terrainLodStats.BudgetViolationCount;
    stats.RoamQueueInvariantViolationCount = _terrainLodStats.QueueInvariantViolationCount;
    stats.RoamResourceValidationFailureCount = _terrainLodStats.ResourceValidationFailureCount;
    stats.RoamPassEvidenceMilliseconds = _terrainLodStats.PassEvidenceMilliseconds;
    stats.RoamMaxDepthSetting = _settings.RoamMaxDepth;
    stats.RoamScreenSpaceSplitThresholdPixels = _settings.RoamScreenSpaceSplitThresholdPixels;
    stats.RoamScreenSpaceMergeThresholdPixels = _settings.RoamScreenSpaceMergeThresholdPixels;
    stats.RoamTriangleBudgetSetting = _settings.RoamTriangleBudget;
    stats.RoamParallelSplitEnabled = _settings.RoamEnableParallelSplit;
    stats.RoamNodeCount = _terrainLodStats.ActiveNodeCount;
    stats.RoamOriginalTriangleCount = _terrainLodStats.OriginalTriangleCount;
    stats.RoamSubdividedTriangleCount = _terrainLodStats.SubdividedTriangleCount;
    stats.RoamRebuiltTriangleCount = _terrainLodStats.RebuiltTriangleCount;
    stats.RoamActiveSplitCount = _terrainLodStats.ActiveSplitCount;
    stats.RoamSplitCount = _terrainLodStats.SplitCount;
    stats.RoamForcedSplitCount = _terrainLodStats.ForcedSplitCount;
    stats.RoamMergeCount = _terrainLodStats.MergeCount;
    stats.RoamCrackRiskCount = _terrainLodStats.CrackRiskCount;
    stats.RoamConstraintPassCount = _terrainLodStats.ConstraintPassCount;
    stats.RoamCandidatePeakCount = _terrainLodStats.CandidatePeakCount;
    stats.RoamPersistentSplitQueueSize = _terrainLodStats.PersistentSplitQueueSize;
    stats.RoamPersistentMergeQueueSize = _terrainLodStats.PersistentMergeQueueSize;
    stats.RoamQueueCrossoverCount = _terrainLodStats.QueueCrossoverCount;
    stats.RoamQueueMembershipUpdateCount = _terrainLodStats.QueueMembershipUpdateCount;
    stats.RoamCpuMeshFullRebuildCount = _terrainLodStats.CpuMeshFullRebuildCount;
    stats.RoamCpuMeshUpdatedTriangleCount = _terrainLodStats.CpuMeshUpdatedTriangleCount;
    stats.RoamCpuMeshReusedTriangleCount = _terrainLodStats.CpuMeshReusedTriangleCount;
    stats.RoamCpuMeshDirtyRangeCount = _terrainLodStats.CpuMeshDirtyRangeCount;
    stats.RoamRejectedSplitCount = _terrainLodStats.RejectedSplitCount;
    stats.RoamBudgetRejectedSplitCount = _terrainLodStats.BudgetRejectedSplitCount;
    stats.RoamRejectedMergeCount = _terrainLodStats.RejectedMergeCount;
    stats.RoamTjunctionCount = _terrainLodStats.TjunctionCount;
    stats.RoamInvalidNeighborCount = _terrainLodStats.InvalidNeighborCount;
    stats.RoamInvalidTopologyCount = _terrainLodStats.InvalidTopologyCount;
    stats.RoamCpuWorkerCount = _terrainLodStats.CpuWorkerCount;
    stats.RoamCpuUtilizationPercent = _terrainLodStats.CpuUtilizationPercent;
    stats.RoamTotalMilliseconds = _terrainLodTotalMilliseconds;
    stats.RoamUpdateMilliseconds = _terrainLodStats.CpuUpdateMilliseconds;
    stats.RoamCpuPrepareMilliseconds = _terrainLodStats.CpuPrepareMilliseconds;
    stats.RoamCpuMergeCandidateMarkMilliseconds = _terrainLodStats.CpuMergeCandidateMarkMilliseconds;
    stats.RoamCpuMergeTopologyMilliseconds = _terrainLodStats.CpuMergeTopologyMilliseconds;
    stats.RoamCpuSplitTopologyChunkBuildMilliseconds =
        _terrainLodStats.CpuSplitTopologyChunkBuildMilliseconds;
    stats.RoamCpuSplitTopologyQueueInvalidationMilliseconds =
        _terrainLodStats.CpuSplitTopologyQueueInvalidationMilliseconds;
    stats.RoamCpuSplitTopologyParallelCommitMilliseconds =
        _terrainLodStats.CpuSplitTopologyParallelCommitMilliseconds;
    stats.RoamCpuSplitTopologyResultMergeMilliseconds =
        _terrainLodStats.CpuSplitTopologyResultMergeMilliseconds;
    stats.RoamCpuSplitTopologyIndexQueueRefreshMilliseconds =
        _terrainLodStats.CpuSplitTopologyIndexQueueRefreshMilliseconds;
    stats.RoamCpuSplitTopologySerialConvergenceMilliseconds =
        _terrainLodStats.CpuSplitTopologySerialConvergenceMilliseconds;
    stats.RoamCpuMergeTopologyChunkBuildMilliseconds =
        _terrainLodStats.CpuMergeTopologyChunkBuildMilliseconds;
    stats.RoamCpuMergeTopologyQueueInvalidationMilliseconds =
        _terrainLodStats.CpuMergeTopologyQueueInvalidationMilliseconds;
    stats.RoamCpuMergeTopologyParallelCommitMilliseconds =
        _terrainLodStats.CpuMergeTopologyParallelCommitMilliseconds;
    stats.RoamCpuMergeTopologyResultMergeMilliseconds =
        _terrainLodStats.CpuMergeTopologyResultMergeMilliseconds;
    stats.RoamCpuMergeTopologyIndexQueueRefreshMilliseconds =
        _terrainLodStats.CpuMergeTopologyIndexQueueRefreshMilliseconds;
    stats.RoamCpuMergeTopologySerialConvergenceMilliseconds =
        _terrainLodStats.CpuMergeTopologySerialConvergenceMilliseconds;
    stats.RoamCpuBudgetLeafCollectMilliseconds = _terrainLodStats.CpuBudgetLeafCollectMilliseconds;
    stats.RoamCpuErrorEvalMilliseconds = _terrainLodStats.CpuErrorEvalMilliseconds;
    stats.RoamCpuSplitCandidateMarkMilliseconds = _terrainLodStats.CpuSplitCandidateMarkMilliseconds;
    stats.RoamCpuSplitTopologyMilliseconds = _terrainLodStats.CpuSplitTopologyMilliseconds;
    stats.RoamCpuFinalLeafCollectMilliseconds = _terrainLodStats.CpuFinalLeafCollectMilliseconds;
    stats.RoamCpuMeshEmitMilliseconds = _terrainLodStats.CpuMeshEmitMilliseconds;
    stats.RoamCpuFinalizeMilliseconds = _terrainLodStats.CpuFinalizeMilliseconds;
    stats.RoamCpuUploadMilliseconds = _terrainLodCpuUploadMilliseconds;
    stats.RoamSplitMilliseconds = _terrainLodStats.SplitMilliseconds;
    stats.RoamMergeMilliseconds = _terrainLodStats.MergeMilliseconds;
    stats.RoamEmitMilliseconds = _terrainLodStats.EmitMilliseconds;
    stats.RoamValidateMilliseconds = _terrainLodStats.ValidateMilliseconds;
    stats.RoamFrameFenceWaitMilliseconds =
        _d3d12State != nullptr ? _d3d12State->Backend->LastGpuWaitMilliseconds() : 0.0F;
    stats.RoamRenderMilliseconds = _d3d12State != nullptr ? _d3d12State->Backend->LastGpuFrameMilliseconds() : 0.0F;
    stats.RoamCpuGpuUploadBytes = _terrainLodStats.CpuGpuUploadBytes;
    stats.RoamCpuGpuReadbackBytes = _terrainLodStats.CpuGpuReadbackBytes;
    stats.RoamMaxDepthReached = _terrainLodStats.MaxActiveDepth;
    return stats;
}

const std::filesystem::path& TerrainRenderer::HeightMapPath() const
{
    return _heightMapPath;
}

const std::filesystem::path& TerrainRenderer::TexturePath() const
{
    return _texturePath;
}

bool TerrainRenderer::RebuildMesh(std::string* errorMessage)
{
    return _settings.UseTerrainLod
        ? RebuildTerrainLod(_lastRenderContext, errorMessage)
        : RebuildRegularGrid(errorMessage);
}

bool TerrainRenderer::RebuildRegularGrid(std::string* errorMessage)
{
    // 规则网格不保留任何 ROAM 算法状态或相机重建历史
    _meshData = Terrain::TerrainMeshBuilder::Build(_heightMap, _settings.TerrainSize, _settings.HeightScale);
    _borrowedCpuMeshData = nullptr;
    _terrainLodStats = {};
    _terrainLodStatusMessage.clear();
    _terrainLodTotalMilliseconds = 0.0F;
    _terrainLodCpuUploadMilliseconds = 0.0F;
    _terrainLodAlgorithm.reset();
    _hasRoamBuildView = false;
    if (_meshData.Vertices.empty() || _meshData.Indices.empty())
    {
        SetError(errorMessage, "Terrain mesh build failed: invalid height map or grid size");
        return false;
    }
    _meshDirty = false;
    return UploadMesh(errorMessage);
}

bool TerrainRenderer::RebuildTerrainLod(const RenderContext& context, std::string* errorMessage)
{
    // 总耗时覆盖算法创建 拓扑更新和可能的 CPU 上传
    Tools::PerformanceTimer rebuildTimer;
    _terrainLodTotalMilliseconds = 0.0F;
    _terrainLodCpuUploadMilliseconds = 0.0F;
    if (_terrainLodAlgorithm == nullptr || _terrainLodAlgorithm->Info().Id != _settings.TerrainLodAlgorithm)
    {
        // 算法对象拥有跨帧 CPU 拓扑和增量 mesh 状态。
        _borrowedCpuMeshData = nullptr;
        _terrainLodAlgorithm = CreateTerrainLodAlgorithm(_settings.TerrainLodAlgorithm);
        _hasRoamBuildView = false;
    }
    if (_terrainLodAlgorithm == nullptr)
    {
        _terrainLodStatusMessage = "Selected D3D12 terrain LOD algorithm is unavailable";
        SetError(errorMessage, _terrainLodStatusMessage);
        return false;
    }

    // 通过统一输入结构保持 Classic 和 DOD 的实验参数一致。
    Algorithms::TerrainLodSettings lodSettings{};
    lodSettings.TerrainSize = _settings.TerrainSize;
    lodSettings.HeightScale = _settings.HeightScale;
    lodSettings.MaxDepth = _settings.RoamMaxDepth;
    lodSettings.ScreenSpaceSplitThresholdPixels = _settings.RoamScreenSpaceSplitThresholdPixels;
    lodSettings.ScreenSpaceMergeThresholdPixels = _settings.RoamScreenSpaceMergeThresholdPixels;
    lodSettings.TriangleBudget = _settings.RoamTriangleBudget;
    lodSettings.EnableParallelSplit = _settings.RoamEnableParallelSplit;
    lodSettings.PassPolicy = _settings.RoamPassPolicy;
    lodSettings.EnableLocalConstraints = _settings.RoamEnableLocalConstraints;
    lodSettings.EnableTopologyValidation = _settings.RoamEnableTopologyValidation;
    lodSettings.EnablePassEvidence = _settings.RoamEnablePassEvidence;
    Algorithms::TerrainLodBuildInput buildInput{};
    buildInput.HeightMap = &_heightMap;
    buildInput.View = Algorithms::BuildTerrainLodViewInput(
        context.View,
        context.Projection,
        context.CameraPosition,
        context.CameraForward,
        static_cast<std::uint32_t>(std::max(context.DrawableWidth, 1)),
        static_cast<std::uint32_t>(std::max(context.DrawableHeight, 1)),
        context.UsesZeroToOneDepth);
    buildInput.Settings = lodSettings;
    Algorithms::TerrainLodRenderPacket renderPacket{};
    std::string localError;
    std::string* buildError = errorMessage != nullptr ? errorMessage : &localError;
    buildError->clear();
    if (!_terrainLodAlgorithm->BuildRenderData(buildInput, renderPacket, buildError))
    {
        _terrainLodStatusMessage = buildError->empty() ? "Terrain LOD build failed" : *buildError;
        return false;
    }
    // D3D12 renderer 只消费完整或借用的 CPU mesh。
    if (!renderPacket.HasConsistentResourceContract())
    {
        _terrainLodStatusMessage = "D3D12 terrain LOD returned an inconsistent render packet";
        SetError(errorMessage, _terrainLodStatusMessage);
        return false;
    }

    _terrainLodStats = _terrainLodAlgorithm->Stats();
    _terrainLodStatusMessage = renderPacket.StatusMessage;
    _lastRoamBuildContext = context;
    _hasRoamBuildView = true;
    if (renderPacket.Mode != Algorithms::TerrainLodRenderMode::CpuMesh)
    {
        _terrainLodStatusMessage = "D3D12 terrain LOD returned an unsupported render mode";
        SetError(errorMessage, _terrainLodStatusMessage);
        return false;
    }

    const Terrain::TerrainMeshData* cpuMesh = renderPacket.ResolveCpuMesh();
    if (renderPacket.BorrowedCpuMesh != nullptr)
    {
        _meshData = {};
        _borrowedCpuMeshData = renderPacket.BorrowedCpuMesh;
    }
    else
    {
        _meshData = std::move(renderPacket.CpuMesh);
        _borrowedCpuMeshData = nullptr;
        cpuMesh = &_meshData;
    }
    if (cpuMesh == nullptr || cpuMesh->Vertices.empty() || cpuMesh->Indices.empty())
    {
        _terrainLodStatusMessage = "Terrain LOD mesh build failed";
        SetError(errorMessage, _terrainLodStatusMessage);
        return false;
    }

    if (_cpuUploadExperimentCaptureEnabled)
    {
        _lastCpuMeshUpdateRanges = renderPacket.CpuMeshUpdateRanges;
        _lastCpuMeshRequiresFullUpload = renderPacket.CpuMeshRequiresFullUpload;
    }
    // CPU 上传单独计时以便与纯 GPU 数据路径比较
    Tools::PerformanceTimer uploadTimer;
    if (!UploadMeshData(
            *cpuMesh,
            renderPacket.CpuMeshRequiresFullUpload,
            renderPacket.CpuUploadAction,
            renderPacket.CpuMeshUpdateRanges,
            errorMessage))
    {
        _meshDirty = true;
        return false;
    }
    _terrainLodCpuUploadMilliseconds = uploadTimer.Stop();
    _terrainLodStats.CpuUploadMilliseconds = _terrainLodCpuUploadMilliseconds;
    Algorithms::TerrainLodPassTraceFor(
        _terrainLodStats.PassTraces,
        Algorithms::TerrainLodPassId::CpuUpload).WallMilliseconds =
        _terrainLodCpuUploadMilliseconds;
    _meshDirty = false;
    _terrainLodTotalMilliseconds = rebuildTimer.Stop();
    return true;
}

bool TerrainRenderer::UploadMesh(std::string* errorMessage)
{
    if (_d3d12State == nullptr || _meshData.Vertices.empty() || _meshData.Indices.empty())
    {
        SetError(errorMessage, "D3D12 terrain mesh upload state is incomplete");
        return false;
    }
    static const std::vector<Algorithms::TerrainLodCpuMeshUpdateRange> noUpdateRanges;
    return UploadMeshData(
        _meshData,
        true,
        Algorithms::TerrainLodCpuUploadAction::Automatic,
        noUpdateRanges,
        errorMessage);
}

bool TerrainRenderer::UploadMeshData(
    const Terrain::TerrainMeshData& meshData,
    bool meshRequiresFullUpload,
    Algorithms::TerrainLodCpuUploadAction uploadAction,
    const std::vector<Algorithms::TerrainLodCpuMeshUpdateRange>& updateRanges,
    std::string* errorMessage)
{
    if (_d3d12State == nullptr || meshData.Vertices.empty() || meshData.Indices.empty())
    {
        SetError(errorMessage, "D3D12 terrain mesh upload state is incomplete");
        return false;
    }

    const bool forceFullUpload = meshRequiresFullUpload ||
        uploadAction == Algorithms::TerrainLodCpuUploadAction::FullBuffer;
    const bool requestedFullUpload = uploadAction == Algorithms::TerrainLodCpuUploadAction::FullBuffer ||
        (uploadAction == Algorithms::TerrainLodCpuUploadAction::Automatic && meshRequiresFullUpload);
    // 新版本先上传当前帧，其他帧在轮转到来时按版本号补齐
    ++_d3d12State->MeshGeneration;
    for (D3D12MeshFrameResources& frame : _d3d12State->MeshFrames)
    {
        if (forceFullUpload)
        {
            frame.PendingFullUpload = true;
            frame.PendingUpdateRanges.clear();
        }
        else if (!frame.PendingFullUpload)
        {
            frame.PendingUpdateRanges.insert(
                frame.PendingUpdateRanges.end(),
                updateRanges.begin(),
                updateRanges.end());
            // 一个 frame slot 可能跨过多个 Build；先取区间并集，避免追赶版本时重复 memcpy。
            CoalescePendingMeshUpdateRanges(frame.PendingUpdateRanges);
        }
    }
    _renderMode = Algorithms::TerrainLodRenderMode::CpuMesh;
    _drawVertexCount = meshData.Vertices.size();
    _drawIndexCount = meshData.Indices.size();
    _drawTriangleCount = meshData.Indices.size() / 3U;
    std::size_t uploadedBytes = 0U;
    const std::uint32_t currentFrameIndex = _d3d12State->Backend->CurrentFrameIndex();
    const D3D12MeshFrameResources& currentFrame = _d3d12State->MeshFrames[currentFrameIndex];
    const bool pendingFullUpload = currentFrame.PendingFullUpload;
    const std::size_t pendingRangeCount = currentFrame.PendingUpdateRanges.size();
    bool uploadedAllVertices = false;
    bool uploadedAllIndices = false;
    const bool uploaded = UploadMeshForFrame(
        *_d3d12State,
        meshData,
        currentFrameIndex,
        &uploadedBytes,
        &uploadedAllVertices,
        &uploadedAllIndices,
        errorMessage);
    _terrainLodStats.CpuGpuUploadBytes = uploadedBytes;
    if (_settings.UseTerrainLod)
    {
        Algorithms::TerrainLodPassTrace& uploadTrace = Algorithms::TerrainLodPassTraceFor(
            _terrainLodStats.PassTraces,
            Algorithms::TerrainLodPassId::CpuUpload);
        uploadTrace.RequestedAction = requestedFullUpload
            ? Algorithms::TerrainLodPassAction::FullBuffer
            : Algorithms::TerrainLodPassAction::DirtyRange;
        uploadTrace.EffectiveAction = uploadedAllVertices && uploadedAllIndices
            ? Algorithms::TerrainLodPassAction::FullBuffer
            : (!uploadedAllVertices && !uploadedAllIndices
                ? Algorithms::TerrainLodPassAction::DirtyRange
                : Algorithms::TerrainLodPassAction::MixedUpload);
        uploadTrace.FallbackReason = Algorithms::TerrainLodPassFallbackReason::None;
        if (!requestedFullUpload && (uploadedAllVertices || uploadedAllIndices))
        {
            uploadTrace.FallbackReason = meshRequiresFullUpload
                ? Algorithms::TerrainLodPassFallbackReason::MeshInitialization
                : (pendingFullUpload
                    ? Algorithms::TerrainLodPassFallbackReason::FrameSlotBacklog
                    : Algorithms::TerrainLodPassFallbackReason::ResourceCapacity);
        }
        uploadTrace.DataUpdate = uploadedAllVertices && uploadedAllIndices
            ? Algorithms::TerrainLodDataUpdateMode::Full
            : (!uploadedAllVertices && !uploadedAllIndices
                ? Algorithms::TerrainLodDataUpdateMode::Incremental
                : Algorithms::TerrainLodDataUpdateMode::Mixed);
        uploadTrace.RequestedWorkerCount = 1U;
        uploadTrace.EffectiveWorkerCount = uploadedBytes == 0U ? 0U : 1U;
        uploadTrace.DirtyItemCount = pendingRangeCount;
        if (uploadedBytes == 0U)
        {
            uploadTrace.FallbackReason = Algorithms::TerrainLodPassFallbackReason::NoWork;
        }
    }
    return uploaded;
}

TerrainCpuUploadExperimentResult TerrainRenderer::ReplayCurrentCpuUploadPair(
    std::size_t warmupCount,
    std::size_t measuredRepeatCount)
{
    TerrainCpuUploadExperimentResult result{};
    const Terrain::TerrainMeshData* meshData = _borrowedCpuMeshData != nullptr
        ? _borrowedCpuMeshData
        : &_meshData;
    if (_d3d12State == nullptr || measuredRepeatCount == 0U || meshData == nullptr ||
        meshData->Vertices.empty() || meshData->Indices.empty())
    {
        result.FailureMessage = "上传重放需要有效的 D3D12 状态、非空网格和至少一次正式重复";
        return result;
    }

    std::uint64_t packetHash = Algorithms::TerrainLodHashOffset;
    Algorithms::AppendTerrainLodHash(packetHash, _terrainLodStats.NormalizedMeshHash);
    Algorithms::AppendTerrainLodHash(packetHash, meshData->Vertices.size());
    Algorithms::AppendTerrainLodHash(packetHash, meshData->Indices.size());
    for (const Algorithms::TerrainLodCpuMeshUpdateRange& range : _lastCpuMeshUpdateRanges)
    {
        Algorithms::AppendTerrainLodHash(packetHash, range.FirstVertex);
        Algorithms::AppendTerrainLodHash(packetHash, range.VertexCount);
        Algorithms::AppendTerrainLodHash(packetHash, range.FirstIndex);
        Algorithms::AppendTerrainLodHash(packetHash, range.IndexCount);
    }

    struct PendingState
    {
        std::uint64_t MeshGeneration{0U};
        bool FullUpload{false};
        std::vector<Algorithms::TerrainLodCpuMeshUpdateRange> UpdateRanges;
    };
    std::array<PendingState, D3D12GraphicsBackend::FrameCount> pendingStates{};
    for (std::size_t index = 0U; index < pendingStates.size(); ++index)
    {
        pendingStates[index].MeshGeneration = _d3d12State->MeshFrames[index].MeshGeneration;
        pendingStates[index].FullUpload = _d3d12State->MeshFrames[index].PendingFullUpload;
        pendingStates[index].UpdateRanges = _d3d12State->MeshFrames[index].PendingUpdateRanges;
    }
    const std::uint64_t savedMeshGeneration = _d3d12State->MeshGeneration;
    const Algorithms::TerrainLodStats savedStats = _terrainLodStats;
    const float savedUploadMilliseconds = _terrainLodCpuUploadMilliseconds;

    const std::size_t totalBlocks = warmupCount + measuredRepeatCount;
    bool passed = true;
    for (std::size_t block = 0U; block < totalBlocks; ++block)
    {
        const bool warmup = block < warmupCount;
        const std::size_t repeatIndex = warmup ? block : block - warmupCount;
        const std::array<Algorithms::TerrainLodCpuUploadAction, 2U> actions = block % 2U == 0U
            ? std::array{Algorithms::TerrainLodCpuUploadAction::DirtyRange,
                         Algorithms::TerrainLodCpuUploadAction::FullBuffer}
            : std::array{Algorithms::TerrainLodCpuUploadAction::FullBuffer,
                         Algorithms::TerrainLodCpuUploadAction::DirtyRange};
        for (std::size_t order = 0U; order < actions.size(); ++order)
        {
            std::string errorMessage;
            Tools::PerformanceTimer timer;
            const bool uploaded = UploadMeshData(
                *meshData,
                _lastCpuMeshRequiresFullUpload,
                actions[order],
                _lastCpuMeshUpdateRanges,
                &errorMessage);
            const float wallMilliseconds = timer.Stop();
            passed = passed && uploaded;
            if (!uploaded && result.FailureMessage.empty())
            {
                result.FailureMessage = errorMessage;
            }

            const Algorithms::TerrainLodPassTrace& trace = Algorithms::TerrainLodPassTraceFor(
                _terrainLodStats.PassTraces,
                Algorithms::TerrainLodPassId::CpuUpload);
            TerrainCpuUploadExperimentSample sample{};
            sample.RequestedAction = actions[order] == Algorithms::TerrainLodCpuUploadAction::FullBuffer
                ? Algorithms::TerrainLodPassAction::FullBuffer
                : Algorithms::TerrainLodPassAction::DirtyRange;
            sample.EffectiveAction = trace.EffectiveAction;
            sample.FallbackReason = trace.FallbackReason;
            sample.RepeatIndex = repeatIndex;
            sample.ExecutionOrder = order;
            sample.UpdateRangeCount = _lastCpuMeshUpdateRanges.size();
            sample.UploadedBytes = _terrainLodStats.CpuGpuUploadBytes;
            sample.FullBufferBytes =
                meshData->Vertices.size() * sizeof(Terrain::TerrainMeshVertex) +
                meshData->Indices.size() * sizeof(std::uint32_t);
            sample.PacketHash = packetHash;
            sample.WallMilliseconds = wallMilliseconds;
            sample.ValidMeasurement = uploaded &&
                trace.FallbackReason == Algorithms::TerrainLodPassFallbackReason::None &&
                trace.EffectiveAction == sample.RequestedAction;
            if (warmup)
            {
                ++result.WarmupExecutionCount;
            }
            else
            {
                result.Samples.push_back(sample);
            }
        }
    }

    for (std::size_t index = 0U; index < pendingStates.size(); ++index)
    {
        _d3d12State->MeshFrames[index].MeshGeneration = pendingStates[index].MeshGeneration;
        _d3d12State->MeshFrames[index].PendingFullUpload = pendingStates[index].FullUpload;
        _d3d12State->MeshFrames[index].PendingUpdateRanges =
            std::move(pendingStates[index].UpdateRanges);
    }
    _d3d12State->MeshGeneration = savedMeshGeneration;
    _terrainLodStats = savedStats;
    _terrainLodCpuUploadMilliseconds = savedUploadMilliseconds;
    result.Passed = passed;
    return result;
}

bool TerrainRenderer::LoadTexture(const std::filesystem::path& texturePath, std::string* errorMessage)
{
    int width = 0;
    int height = 0;
    int channels = 0;
    // 统一转换为 RGBA8，简化纹理格式和行跨度契约
    unsigned char* pixels = stbi_load(texturePath.string().c_str(), &width, &height, &channels, 4);
    if (pixels == nullptr)
    {
        SetError(errorMessage, "Terrain texture load failed: " + texturePath.string());
        return false;
    }

    D3D12_RESOURCE_DESC textureDescription{};
    textureDescription.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    textureDescription.Width = static_cast<UINT64>(width);
    textureDescription.Height = static_cast<UINT>(height);
    textureDescription.DepthOrArraySize = 1;
    textureDescription.MipLevels = 1;
    textureDescription.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    textureDescription.SampleDesc.Count = 1;
    textureDescription.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
    // 最终纹理驻留默认堆，只通过一次性上传缓冲写入
    const D3D12_HEAP_PROPERTIES defaultHeap = HeapProperties(D3D12_HEAP_TYPE_DEFAULT);
    HRESULT result = _d3d12State->Backend->Device()->CreateCommittedResource(
        &defaultHeap,
        D3D12_HEAP_FLAG_NONE,
        &textureDescription,
        D3D12_RESOURCE_STATE_COPY_DEST,
        nullptr,
        IID_PPV_ARGS(&_d3d12State->Texture));
    if (FAILED(result))
    {
        stbi_image_free(pixels);
        SetError(errorMessage, "D3D12 terrain texture allocation failed");
        return false;
    }

    D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint{};
    UINT rowCount = 0;
    UINT64 rowSize = 0;
    UINT64 uploadSize = 0;
    // 由设备计算行对齐，不能假定源图像行宽满足 D3D12 要求
    _d3d12State->Backend->Device()->GetCopyableFootprints(
        &textureDescription, 0, 1, 0, &footprint, &rowCount, &rowSize, &uploadSize);
    Microsoft::WRL::ComPtr<ID3D12Resource> uploadBuffer;
    std::uint8_t* mappedUpload = nullptr;
    if (!CreateMappedUploadBuffer(
            _d3d12State->Backend->Device(),
            static_cast<std::size_t>(uploadSize),
            uploadBuffer,
            mappedUpload,
            errorMessage))
    {
        stbi_image_free(pixels);
        return false;
    }
    // 逐行复制并保留目标 RowPitch 的填充字节
    const std::size_t sourceRowBytes = static_cast<std::size_t>(width) * 4U;
    for (UINT row = 0; row < rowCount; ++row)
    {
        std::memcpy(
            mappedUpload + footprint.Offset + static_cast<std::size_t>(row) * footprint.Footprint.RowPitch,
            pixels + static_cast<std::size_t>(row) * sourceRowBytes,
            sourceRowBytes);
    }
    stbi_image_free(pixels);

    // 立即提交会等待复制完成，局部上传缓冲可在返回后安全释放
    const bool uploaded = _d3d12State->Backend->ExecuteImmediate(
        [&](ID3D12GraphicsCommandList* commandList, std::string*) {
            D3D12_TEXTURE_COPY_LOCATION source{};
            source.pResource = uploadBuffer.Get();
            source.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
            source.PlacedFootprint = footprint;
            D3D12_TEXTURE_COPY_LOCATION destination{};
            destination.pResource = _d3d12State->Texture.Get();
            destination.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
            commandList->CopyTextureRegion(&destination, 0, 0, 0, &source, nullptr);
            // 复制完成后转为像素着色器只读状态
            D3D12_RESOURCE_BARRIER barrier{};
            barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
            barrier.Transition.pResource = _d3d12State->Texture.Get();
            barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
            barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
            barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
            commandList->ResourceBarrier(1, &barrier);
            return true;
        },
        errorMessage);
    ReleaseMappedResource(uploadBuffer, mappedUpload);
    if (!uploaded)
    {
        return false;
    }

    // SRV 需要在纹理整个生命周期内保持同一堆索引
    _d3d12State->TextureSrv = _d3d12State->Backend->AllocateSrvDescriptor();
    if (!_d3d12State->TextureSrv.IsValid())
    {
        SetError(errorMessage, "D3D12 SRV heap has no descriptor available for the terrain texture");
        return false;
    }
    D3D12_SHADER_RESOURCE_VIEW_DESC srvDescription{};
    srvDescription.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srvDescription.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    srvDescription.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    srvDescription.Texture2D.MipLevels = 1;
    _d3d12State->Backend->Device()->CreateShaderResourceView(
        _d3d12State->Texture.Get(),
        &srvDescription,
        _d3d12State->TextureSrv.Cpu);
    return true;
}

bool TerrainRenderer::HasDrawableTerrain() const
{
    if (_d3d12State == nullptr)
    {
        return false;
    }
    return _renderMode == Algorithms::TerrainLodRenderMode::CpuMesh && _drawIndexCount > 0U;
}
} // namespace ParallelRoam::Render
