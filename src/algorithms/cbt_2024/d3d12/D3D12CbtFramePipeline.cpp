#include "algorithms/cbt_2024/d3d12/D3D12CbtFramePipeline.h"

#include "algorithms/cbt_2024/CbtClassification.h"
#include "algorithms/cbt_2024/CbtSplitPlanner.h"
#include "algorithms/cbt_2024/CbtTerrainGeometry.h"
#include "terrain/TerrainMeshBuilder.h"
#include "tools/PerformanceTimer.h"

#include <algorithm>
#include <array>
#include <bit>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <limits>
#include <string>
#include <vector>

namespace ParallelRoam::Algorithms::Cbt2024::D3D12
{
namespace
{
using Microsoft::WRL::ComPtr;

// 常量缓冲按 D3D12 CBV 对齐要求分成三个独立的 256 字节槽。
// b0 保存全局视图常量，供后续分类阶段沿用官方协议。
// b1 保存 topology/geometry 的整数边界和深度。
// b2 保存逐帧分类阈值与 drawable 尺寸。
// 每个 swap-chain frame 拥有独立映射区，CPU 不会覆盖尚未消费的数据。
constexpr std::size_t ConstantBufferSlotBytes = 256U;
constexpr std::size_t ConstantBufferBytes = ConstantBufferSlotBytes * 3U;
constexpr std::size_t ClassificationCounterOffset = D3D12CbtDiagnostics::ClassificationCounterOffset;
constexpr std::size_t AllocationCounterOffset = D3D12CbtDiagnostics::AllocationCounterOffset;
constexpr std::size_t MemoryCounterOffset = D3D12CbtDiagnostics::MemoryCounterOffset;
constexpr std::size_t ValidationCounterOffset = D3D12CbtDiagnostics::ValidationCounterOffset;
constexpr std::size_t OccupancyRootOffset = D3D12CbtDiagnostics::OccupancyRootOffset;
constexpr std::size_t DrawStateReadbackOffset = D3D12CbtDiagnostics::DrawStateReadbackOffset;
constexpr std::size_t MaximumActiveDepthReadbackOffset =
    D3D12CbtDiagnostics::MaximumActiveDepthReadbackOffset;
constexpr std::size_t BaseBisectorDataOffset = D3D12CbtDiagnostics::BaseBisectorDataOffset;
constexpr std::size_t BaseRenderVertexOffset = D3D12CbtDiagnostics::BaseRenderVertexOffset;
constexpr std::size_t BaseClassificationPositionOffset =
    D3D12CbtDiagnostics::BaseClassificationPositionOffset;
constexpr std::size_t BaseParentPositionOffset = D3D12CbtDiagnostics::BaseParentPositionOffset;
constexpr std::uint32_t WorkGroupSize = 64U;
constexpr std::uint32_t TopologyUavCount = 17U;

void SetError(std::string* errorMessage, const std::string& message)
{
    if (errorMessage != nullptr)
    {
        *errorMessage = message;
    }
}

// 所有 E0 bootstrap kernel 固定为 64 线程；这里统一向上取整 dispatch 数量。
std::uint32_t DispatchCount(std::uint32_t count)
{
    return (count + WorkGroupSize - 1U) / WorkGroupSize;
}

// 运行时只读取 CMake/DXC 已生成的字节码，不在首帧动态编译 shader。
// 初始化失败保留具体路径，便于诊断部署时遗漏的 cso。
std::vector<std::uint8_t> ReadBinaryFile(const std::filesystem::path& path, std::string* errorMessage)
{
    std::ifstream stream{path, std::ios::binary | std::ios::ate};
    if (!stream)
    {
        SetError(errorMessage, "Failed to open CBT topology shader: " + path.string());
        return {};
    }
    const std::streamsize size = stream.tellg();
    if (size <= 0)
    {
        SetError(errorMessage, "CBT topology shader is empty: " + path.string());
        return {};
    }
    std::vector<std::uint8_t> bytes(static_cast<std::size_t>(size));
    stream.seekg(0, std::ios::beg);
    if (!stream.read(reinterpret_cast<char*>(bytes.data()), size))
    {
        SetError(errorMessage, "Failed to read CBT topology shader: " + path.string());
        return {};
    }
    return bytes;
}

// E0 的常量缓冲位于 upload heap，拓扑和几何输出位于 default heap。
D3D12_HEAP_PROPERTIES HeapProperties(D3D12_HEAP_TYPE type)
{
    D3D12_HEAP_PROPERTIES properties{};
    properties.Type = type;
    properties.CreationNodeMask = 1U;
    properties.VisibleNodeMask = 1U;
    return properties;
}

// 所有 CBT 资源都是线性 buffer；最小四字节宽度避免创建空 D3D12 资源。
D3D12_RESOURCE_DESC BufferDescription(std::size_t bytes, D3D12_RESOURCE_FLAGS flags)
{
    D3D12_RESOURCE_DESC description{};
    description.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    description.Width = std::max<std::size_t>(bytes, 4U);
    description.Height = 1U;
    description.DepthOrArraySize = 1U;
    description.MipLevels = 1U;
    description.SampleDesc.Count = 1U;
    description.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    description.Flags = flags;
    return description;
}

// 集中创建 committed buffer，确保每条资源路径使用相同的 node mask 和布局。
// 调用者显式提供 initialState，状态跟踪器从该值开始维护。
bool CreateBuffer(
    ID3D12Device* device,
    D3D12_HEAP_TYPE heapType,
    std::size_t bytes,
    D3D12_RESOURCE_FLAGS flags,
    D3D12_RESOURCE_STATES initialState,
    ComPtr<ID3D12Resource>& resource,
    std::string* errorMessage)
{
    const D3D12_HEAP_PROPERTIES heap = HeapProperties(heapType);
    const D3D12_RESOURCE_DESC description = BufferDescription(bytes, flags);
    if (FAILED(device->CreateCommittedResource(
            &heap,
            D3D12_HEAP_FLAG_NONE,
            &description,
            initialState,
            nullptr,
            IID_PPV_ARGS(&resource))))
    {
        SetError(errorMessage, "Failed to allocate CBT topology buffer");
        return false;
    }
    return true;
}

// PSO 和 root signature 成对创建；shader 绑定若与签名不符会在此处直接失败。
bool CreateComputePipeline(
    ID3D12Device* device,
    ID3D12RootSignature* rootSignature,
    const std::filesystem::path& shaderPath,
    ComPtr<ID3D12PipelineState>& pipeline,
    std::string* errorMessage)
{
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
        SetError(errorMessage, "Failed to create CBT topology compute pipeline: " + shaderPath.string());
        return false;
    }
    return true;
}

// 使用版本 1.0 root signature，兼容项目当前的最低 D3D12 环境。
// 序列化器的诊断文本比 HRESULT 更有价值，因此原样转入初始化错误。
bool CreateRootSignature(
    ID3D12Device* device,
    const D3D12_ROOT_SIGNATURE_DESC& description,
    ComPtr<ID3D12RootSignature>& rootSignature,
    std::string* errorMessage)
{
    ComPtr<ID3DBlob> serialized;
    ComPtr<ID3DBlob> errors;
    HRESULT result = D3D12SerializeRootSignature(
        &description,
        D3D_ROOT_SIGNATURE_VERSION_1,
        &serialized,
        &errors);
    if (FAILED(result))
    {
        const char* detail = errors != nullptr
            ? static_cast<const char*>(errors->GetBufferPointer())
            : "unknown error";
        SetError(errorMessage, std::string{"Failed to serialize CBT topology root signature: "} + detail);
        return false;
    }
    result = device->CreateRootSignature(
        0U,
        serialized->GetBufferPointer(),
        serialized->GetBufferSize(),
        IID_PPV_ARGS(&rootSignature));
    if (FAILED(result))
    {
        SetError(errorMessage, "Failed to create CBT topology root signature");
        return false;
    }
    return true;
}

// trackedState 是资源状态的单一 CPU 镜像，避免重复 barrier。
// E0 只在同一 direct command list 中记录，不需要跨 queue ownership 转移。
void Transition(
    ID3D12GraphicsCommandList* commandList,
    ID3D12Resource* resource,
    D3D12_RESOURCE_STATES& trackedState,
    D3D12_RESOURCE_STATES nextState)
{
    if (resource == nullptr || trackedState == nextState)
    {
        return;
    }
    D3D12_RESOURCE_BARRIER barrier{};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition.pResource = resource;
    barrier.Transition.StateBefore = trackedState;
    barrier.Transition.StateAfter = nextState;
    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    commandList->ResourceBarrier(1U, &barrier);
    trackedState = nextState;
}

// 空资源 UAV barrier 表示所有先前 UAV 写入对后续 pass 可见。
// Reduce 的中间阶段使用定向 tree barrier，表达真正的数据依赖。
void UavBarrier(ID3D12GraphicsCommandList* commandList, ID3D12Resource* resource = nullptr)
{
    D3D12_RESOURCE_BARRIER barrier{};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
    barrier.UAV.pResource = resource;
    commandList->ResourceBarrier(1U, &barrier);
}

// Bootstrap 使用 root constants，避免为四个只读标量再分配 CBV。
// BaseElementOffset 把固定六个根二分器定位到完整 heap-id 数组尾部。
struct BootstrapConstants
{
    std::uint32_t TotalElementCount{0U};
    std::uint32_t BaseElementOffset{0U};
    std::uint32_t BaseElementCount{0U};
    float TerrainSize{0.0F};
    std::uint32_t BaseDepth{0U};
    std::uint32_t HeightMapWidth{0U};
    std::uint32_t HeightMapHeight{0U};
    float HeightScale{0.0F};
};

struct TopologyUpdateConstants
{
    std::array<float, 4> AreaAndScreen{};
    std::array<float, 4> CameraPosition{};
    std::array<float, 4> CameraForward{};
    std::array<std::array<float, 4>, 6> FrustumPlanes{};
};

static_assert(sizeof(BootstrapConstants) == 32U);
static_assert(sizeof(TopologyUpdateConstants) == 144U);
static_assert(sizeof(Terrain::TerrainMeshVertex) == 52U);
} // namespace

D3D12CbtFramePipeline::~D3D12CbtFramePipeline()
{
    Shutdown();
}

bool D3D12CbtFramePipeline::Initialize(
    Render::D3D12GraphicsBackend& backend,
    const CbtBaseTopology& topology,
    const D3D12CbtGpuResourceView& resources,
    const Terrain::HeightMap& heightMap,
    std::string* errorMessage)
{
    // 初始化采用全有或全无语义：任一资源或 PSO 失败都会释放此前创建的对象。
    // Topology 本身由算法状态拥有，本对象只借用它来创建视图并记录命令。
    Shutdown();
    _backend = &backend;
    _capacity = topology.Layout.Occupancy.Capacity;
    if (!CreateTopologyRootSignature(errorMessage) ||
        !CreateBootstrapRootSignature(errorMessage) ||
        !CreateDispatchCommandSignature(errorMessage) ||
        !CreatePipelines(errorMessage) ||
        !CreateConstantBuffers(errorMessage) ||
        !_diagnostics.Initialize(backend, errorMessage) ||
        !_geometry.Initialize(backend, _bootstrapRootSignature.Get(), topology, heightMap, errorMessage) ||
        !ConfigureTopologyDescriptors(topology, resources, errorMessage))
    {
        Shutdown();
        return false;
    }
    _initialized = true;
    if (errorMessage != nullptr)
    {
        errorMessage->clear();
    }
    return true;
}

void D3D12CbtFramePipeline::Shutdown()
{
    // Frame CB 持久映射；释放资源前必须逐一解除映射。
    for (std::size_t frame = 0U; frame < _constantBuffers.size(); ++frame)
    {
        if (_constantBuffers[frame] != nullptr && _mappedConstants[frame] != nullptr)
        {
            _constantBuffers[frame]->Unmap(0U, nullptr);
        }
        _mappedConstants[frame] = nullptr;
        _constantBuffers[frame].Reset();
    }
    if (_backend != nullptr)
    {
        // 十七个 UAV 是一个连续分配，必须按原始 range 一次归还。
        _backend->ReleaseSrvDescriptor(_topologySrvRange);
        _backend->ReleaseSrvDescriptor(_topologyUavRange);
    }
    _diagnostics.Shutdown();
    _geometry.Shutdown();
    _prepareIndirectPipeline.Reset();
    _indexationPipeline.Reset();
    _pipelines = {};
    _dispatchCommandSignature.Reset();
    _bootstrapRootSignature.Reset();
    _topologyRootSignature.Reset();
    _topologyFrameGeneration = 0U;
    _lastBlockingValidationWaitMilliseconds = 0.0F;
    _neighborReadIndex = 0U;
    _heapIdState = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    _bisectorDataState = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    _baseControlPointState = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    _neighborStates = {
        D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
        D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
    };
    _activeIndexState = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    _visibleIndexState = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    _modifiedIndexState = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    _classificationState = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    _allocationState = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    _memoryState = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    _validationState = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    _occupancyTreeState = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    _topologyDispatchState = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    _drawState = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    _geometryDispatchState = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    // 不缓存外部 backend 指针，销毁后对象可安全地重新初始化。
    _initialized = false;
    _backend = nullptr;
}

bool D3D12CbtFramePipeline::CreateTopologyRootSignature(std::string* errorMessage)
{
    // 生产 topology 签名严格对应 CbtTopologyE0.hlsl：
    // root[0] = b0，全局/视图常量。
    // root[1] = b1，几何和容量常量。
    // root[2] = b2，逐帧更新常量。
    // root[3] = t0..t1，分类位置和上一帧活动索引。
    // root[4] = u0..u16，完整 CBT 可写资源表。
    // root[3] 只由 Classify 消费，Reset/Reduce 不访问该表。
    D3D12_DESCRIPTOR_RANGE srvRange{};
    srvRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    srvRange.NumDescriptors = 2U;
    srvRange.BaseShaderRegister = 0U;
    srvRange.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

    D3D12_DESCRIPTOR_RANGE uavRange{};
    uavRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
    uavRange.NumDescriptors = TopologyUavCount;
    uavRange.BaseShaderRegister = 0U;
    uavRange.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

    std::array<D3D12_ROOT_PARAMETER, 5> parameters{};
    for (std::uint32_t index = 0U; index < 3U; ++index)
    {
        parameters[index].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
        parameters[index].Descriptor.ShaderRegister = index;
        parameters[index].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    }
    parameters[3].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    parameters[3].DescriptorTable.NumDescriptorRanges = 1U;
    parameters[3].DescriptorTable.pDescriptorRanges = &srvRange;
    parameters[4].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    parameters[4].DescriptorTable.NumDescriptorRanges = 1U;
    parameters[4].DescriptorTable.pDescriptorRanges = &uavRange;

    D3D12_ROOT_SIGNATURE_DESC description{};
    description.NumParameters = static_cast<UINT>(parameters.size());
    description.pParameters = parameters.data();
    return CreateRootSignature(_backend->Device(), description, _topologyRootSignature, errorMessage);
}

bool D3D12CbtFramePipeline::CreateBootstrapRootSignature(std::string* errorMessage)
{
    // Bootstrap 是仓库侧适配层，使用 root descriptors 直接借用常驻资源。
    // root[0] 提供容量、高度图尺寸与世界缩放所需的八个 32 位常量。
    // root[1..3] 分别为 heap ids、bisector data、base control points。
    // root[4..10] 分别为三类索引、draw state、geometry dispatch 和两类几何输出。
    // root[11] 为高度纹理 SRV，root[12] 为几何验证头 UAV。
    // 该签名不进入生产 topology ABI，因此不会改变官方 u0..u16 编号。
    D3D12_DESCRIPTOR_RANGE heightMapRange{};
    heightMapRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    heightMapRange.NumDescriptors = 1U;
    heightMapRange.BaseShaderRegister = 3U;
    heightMapRange.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

    std::array<D3D12_ROOT_PARAMETER, 13> parameters{};
    parameters[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
    parameters[0].Constants.Num32BitValues = 8U;
    parameters[0].Constants.ShaderRegister = 0U;
    for (std::uint32_t index = 0U; index < 3U; ++index)
    {
        parameters[1U + index].ParameterType = D3D12_ROOT_PARAMETER_TYPE_SRV;
        parameters[1U + index].Descriptor.ShaderRegister = index;
    }
    for (std::uint32_t index = 0U; index < 7U; ++index)
    {
        parameters[4U + index].ParameterType = D3D12_ROOT_PARAMETER_TYPE_UAV;
        parameters[4U + index].Descriptor.ShaderRegister = index;
    }
    parameters[11].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    parameters[11].DescriptorTable.NumDescriptorRanges = 1U;
    parameters[11].DescriptorTable.pDescriptorRanges = &heightMapRange;
    parameters[12].ParameterType = D3D12_ROOT_PARAMETER_TYPE_UAV;
    parameters[12].Descriptor.ShaderRegister = 7U;

    D3D12_ROOT_SIGNATURE_DESC description{};
    description.NumParameters = static_cast<UINT>(parameters.size());
    description.pParameters = parameters.data();
    return CreateRootSignature(_backend->Device(), description, _bootstrapRootSignature, errorMessage);
}

bool D3D12CbtFramePipeline::CreateDispatchCommandSignature(std::string* errorMessage)
{
    D3D12_INDIRECT_ARGUMENT_DESC argument{};
    argument.Type = D3D12_INDIRECT_ARGUMENT_TYPE_DISPATCH;
    D3D12_COMMAND_SIGNATURE_DESC description{};
    description.ByteStride = sizeof(D3D12_DISPATCH_ARGUMENTS);
    description.NumArgumentDescs = 1U;
    description.pArgumentDescs = &argument;
    if (FAILED(_backend->Device()->CreateCommandSignature(
            &description,
            nullptr,
            IID_PPV_ARGS(&_dispatchCommandSignature))))
    {
        SetError(errorMessage, "Failed to create CBT dispatch command signature");
        return false;
    }
    return true;
}

bool D3D12CbtFramePipeline::CreatePipelines(std::string* errorMessage)
{
#if defined(PARALLEL_ROAM_DX12_SHADER_DIR)
    const std::filesystem::path shaderDirectory{PARALLEL_ROAM_DX12_SHADER_DIR};
#else
    const std::filesystem::path shaderDirectory{"assets/shaders/dx12"};
#endif
    // OCBT reduction 的数组长度由编译期宏决定，不能用一个动态 PSO 混用容量。
    // 初始化只装载当前资源档位，避免为三个未选容量创建无用的 PSO 集合。
    const std::string prefix =
        std::string{"CbtTopology"} + CbtOccupancyCapacityName(_capacity);
    CapacityPipelines& pipelines = _pipelines;
    if (!CreateComputePipeline(_backend->Device(), _topologyRootSignature.Get(), shaderDirectory / (prefix + "ResetE0.cso"), pipelines.Reset, errorMessage) ||
        !CreateComputePipeline(_backend->Device(), _topologyRootSignature.Get(), shaderDirectory / (prefix + "Classify.cso"), pipelines.Classify, errorMessage) ||
        !CreateComputePipeline(_backend->Device(), _topologyRootSignature.Get(), shaderDirectory / (prefix + "PrepareClassificationIndirect.cso"), pipelines.PrepareClassificationIndirect, errorMessage) ||
        !CreateComputePipeline(_backend->Device(), _topologyRootSignature.Get(), shaderDirectory / (prefix + "SplitE2.cso"), pipelines.Split, errorMessage) ||
        !CreateComputePipeline(_backend->Device(), _topologyRootSignature.Get(), shaderDirectory / (prefix + "PrepareAllocationIndirect.cso"), pipelines.PrepareAllocationIndirect, errorMessage) ||
        !CreateComputePipeline(_backend->Device(), _topologyRootSignature.Get(), shaderDirectory / (prefix + "AllocateE2.cso"), pipelines.Allocate, errorMessage) ||
        !CreateComputePipeline(_backend->Device(), _topologyRootSignature.Get(), shaderDirectory / (prefix + "BisectE3.cso"), pipelines.Bisect, errorMessage) ||
        !CreateComputePipeline(_backend->Device(), _topologyRootSignature.Get(), shaderDirectory / (prefix + "PreparePropagationIndirectE3.cso"), pipelines.PreparePropagationIndirect, errorMessage) ||
        !CreateComputePipeline(_backend->Device(), _topologyRootSignature.Get(), shaderDirectory / (prefix + "PropagateBisectE3.cso"), pipelines.PropagateBisect, errorMessage) ||
        !CreateComputePipeline(_backend->Device(), _topologyRootSignature.Get(), shaderDirectory / (prefix + "PrepareSimplifyF.cso"), pipelines.PrepareSimplify, errorMessage) ||
        !CreateComputePipeline(_backend->Device(), _topologyRootSignature.Get(), shaderDirectory / (prefix + "PrepareSimplifyIndirectF.cso"), pipelines.PrepareSimplifyIndirect, errorMessage) ||
        !CreateComputePipeline(_backend->Device(), _topologyRootSignature.Get(), shaderDirectory / (prefix + "SimplifyF.cso"), pipelines.Simplify, errorMessage) ||
        !CreateComputePipeline(_backend->Device(), _topologyRootSignature.Get(), shaderDirectory / (prefix + "PrepareSimplifyPropagationIndirectF.cso"), pipelines.PrepareSimplifyPropagationIndirect, errorMessage) ||
        !CreateComputePipeline(_backend->Device(), _topologyRootSignature.Get(), shaderDirectory / (prefix + "PropagateSimplifyF.cso"), pipelines.PropagateSimplify, errorMessage) ||
        !CreateComputePipeline(_backend->Device(), _topologyRootSignature.Get(), shaderDirectory / (prefix + "ReducePre.cso"), pipelines.ReducePre, errorMessage) ||
        !CreateComputePipeline(_backend->Device(), _topologyRootSignature.Get(), shaderDirectory / (prefix + "ReduceFirst.cso"), pipelines.ReduceFirst, errorMessage) ||
        !CreateComputePipeline(_backend->Device(), _topologyRootSignature.Get(), shaderDirectory / (prefix + "ReduceSecond.cso"), pipelines.ReduceSecond, errorMessage) ||
        !CreateComputePipeline(_backend->Device(), _topologyRootSignature.Get(), shaderDirectory / (prefix + "ValidateF.cso"), pipelines.Validate, errorMessage))
    {
        return false;
    }

    // Bootstrap PSO 与容量无关，边界由 root constants 在运行时提供。
    return CreateComputePipeline(_backend->Device(), _bootstrapRootSignature.Get(), shaderDirectory / "CbtBootstrapIndexation.cso", _indexationPipeline, errorMessage) &&
           CreateComputePipeline(_backend->Device(), _bootstrapRootSignature.Get(), shaderDirectory / "CbtBootstrapPrepareIndirect.cso", _prepareIndirectPipeline, errorMessage);
}

bool D3D12CbtFramePipeline::CreateConstantBuffers(std::string* errorMessage)
{
    // 一个 frame resource 对应一块 upload buffer，生命周期与 swap-chain frame 同步。
    for (std::size_t frame = 0U; frame < _constantBuffers.size(); ++frame)
    {
        if (!CreateBuffer(
                _backend->Device(),
                D3D12_HEAP_TYPE_UPLOAD,
                ConstantBufferBytes,
                D3D12_RESOURCE_FLAG_NONE,
                D3D12_RESOURCE_STATE_GENERIC_READ,
                _constantBuffers[frame],
                errorMessage))
        {
            return false;
        }
        const D3D12_RANGE noRead{0U, 0U};
        // CPU 只写这些常量，空 read range 避免驱动为 CPU 读访问做额外同步。
        void* mapped = nullptr;
        if (FAILED(_constantBuffers[frame]->Map(0U, &noRead, &mapped)))
        {
            SetError(errorMessage, "Failed to map CBT topology frame constants");
            return false;
        }
        _mappedConstants[frame] = static_cast<std::uint8_t*>(mapped);
    }
    return true;
}

bool D3D12CbtFramePipeline::ConfigureTopologyDescriptors(
    const CbtBaseTopology& topology,
    const D3D12CbtGpuResourceView& resources,
    std::string* errorMessage)
{
    // 两个 SRV 和十七个 UAV 分别连续 因为各 root table 只保存首个 GPU handle
    _topologySrvRange = _backend->AllocateSrvDescriptorRange(2U);
    if (!_topologySrvRange.IsValid())
    {
        SetError(errorMessage, "D3D12 descriptor heap has no contiguous 2-SRV range for CBT");
        return false;
    }
    _topologyUavRange = _backend->AllocateSrvDescriptorRange(TopologyUavCount);
    if (!_topologyUavRange.IsValid())
    {
        SetError(errorMessage, "D3D12 descriptor heap has no contiguous 17-UAV range for CBT");
        return false;
    }

    struct UavBinding
    {
        ID3D12Resource* Resource;
        std::uint32_t ElementCount;
        std::uint32_t Stride;
    };
    // 下列顺序是生产 shader ABI，不允许按 C++ 使用频率重排：
    // u0  occupancy tree，保存各层前缀计数。
    // u1  occupancy bitfield，保存 leaf occupancy 位。
    // u2  heap ids，连接稠密 element slot 与二叉 heap id。
    // u3  neighbor ping，当前邻接关系输入。
    // u4  neighbor pong，下一轮邻接关系输出。
    // u5  bisector data，严格 32 字节的拓扑记录。
    // u6  classification，分类结果或任务标记。
    // u7  simplification，merge 任务暂存。
    // u8  allocation，split 槽位分配结果。
    // u9  propagation，邻接传播任务。
    // u10 memory，活动数与容量计数器。
    // u11 topology dispatch，三组 D3D12_DISPATCH_ARGUMENTS。
    // u12 draw state，以 D3D12_DRAW_ARGUMENTS 开头的十字协议。
    // u13 active indices，所有活动二分器的稠密槽位。
    // u14 visible indices，通过可见性筛选的槽位。
    // u15 modified indices，本帧需要重建几何的槽位。
    // u16 validation，错误码和首个问题元素。
    const CbtTopologyBufferLayout& layout = topology.Layout;
    const std::array<UavBinding, TopologyUavCount> bindings{{
        {resources.OccupancyTree, layout.Occupancy.TreeSlotCount, sizeof(std::uint32_t)},
        {resources.OccupancyBitfield, layout.Occupancy.BitfieldSlotCount, sizeof(std::uint64_t)},
        {resources.HeapIds, layout.TotalElementCount, sizeof(std::uint64_t)},
        {resources.Neighbors[0], layout.TotalElementCount, sizeof(CbtBisectorNeighbors)},
        {resources.Neighbors[1], layout.TotalElementCount, sizeof(CbtBisectorNeighbors)},
        {resources.BisectorData, layout.TotalElementCount, sizeof(CbtBisectorData)},
        {resources.Classification, layout.ClassificationElementCount, sizeof(std::uint32_t)},
        {resources.Simplification, layout.SimplificationElementCount, sizeof(std::uint32_t)},
        {resources.Allocation, layout.AllocationElementCount, sizeof(std::int32_t)},
        {resources.Propagation, layout.PropagationElementCount, sizeof(std::int32_t)},
        {resources.Memory, layout.MemoryElementCount, sizeof(std::int32_t)},
        {resources.TopologyDispatchCommands, layout.TopologyDispatchElementCount, sizeof(std::uint32_t)},
        {resources.IndirectDrawState, layout.DrawStateElementCount, sizeof(std::uint32_t)},
        {resources.ActiveIndices, layout.IndexElementCount, sizeof(std::uint32_t)},
        {resources.VisibleIndices, layout.IndexElementCount, sizeof(std::uint32_t)},
        {resources.ModifiedIndices, layout.IndexElementCount, sizeof(std::uint32_t)},
        {resources.Validation, layout.ValidationElementCount, sizeof(std::uint32_t)},
    }};

    const UINT descriptorSize = _backend->Device()->GetDescriptorHandleIncrementSize(
        D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    for (std::uint32_t index = 0U; index < TopologyUavCount; ++index)
    {
        // 任一空资源都会使 descriptor table 部分有效，必须在创建视图前拒绝。
        if (bindings[index].Resource == nullptr)
        {
            SetError(errorMessage, "CBT topology resource view is incomplete");
            return false;
        }
        D3D12_UNORDERED_ACCESS_VIEW_DESC description{};
        description.Format = DXGI_FORMAT_UNKNOWN;
        description.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
        description.Buffer.NumElements = bindings[index].ElementCount;
        description.Buffer.StructureByteStride = bindings[index].Stride;
        // range 的 CPU handle 按设备给出的增量推进，不能假设描述符字节大小。
        D3D12_CPU_DESCRIPTOR_HANDLE handle = _topologyUavRange.Cpu;
        handle.ptr += static_cast<SIZE_T>(index) * descriptorSize;
        _backend->Device()->CreateUnorderedAccessView(
            bindings[index].Resource,
            nullptr,
            &description,
            handle);
    }

    const std::array<UavBinding, 2> srvBindings{{
        {_geometry.ClassificationPositions(), layout.TotalElementCount * 4U, sizeof(float) * 3U},
        {resources.ActiveIndices, layout.IndexElementCount, sizeof(std::uint32_t)},
    }};
    for (std::uint32_t index = 0U; index < srvBindings.size(); ++index)
    {
        D3D12_SHADER_RESOURCE_VIEW_DESC description{};
        description.Format = DXGI_FORMAT_UNKNOWN;
        description.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
        description.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        description.Buffer.NumElements = srvBindings[index].ElementCount;
        description.Buffer.StructureByteStride = srvBindings[index].Stride;
        D3D12_CPU_DESCRIPTOR_HANDLE handle = _topologySrvRange.Cpu;
        handle.ptr += static_cast<SIZE_T>(index) * descriptorSize;
        _backend->Device()->CreateShaderResourceView(srvBindings[index].Resource, &description, handle);
    }
    return true;
}

bool D3D12CbtFramePipeline::RecordFrame(
    const TerrainLodBuildInput& input,
    const CbtBaseTopology& topology,
    const D3D12CbtGpuResourceView& resources,
    bool rebuildGeometry,
    std::string* errorMessage)
{
    if (!_initialized || _backend == nullptr || !_backend->FrameOpen() || _backend->CommandList() == nullptr)
    {
        SetError(errorMessage, "CBT frame pipeline requires initialization and an open D3D12 frame");
        return false;
    }
    if (input.HeightMap == nullptr || input.HeightMap->Width() <= 0 || input.HeightMap->Height() <= 0)
    {
        SetError(errorMessage, "CBT frame pipeline requires a non-empty height map");
        return false;
    }
    ID3D12GraphicsCommandList* commandList = _backend->CommandList();
    const std::uint32_t frameIndex = _backend->CurrentFrameIndex();
    const bool fullValidation =
        input.Settings.CbtValidationMode != TerrainLodCbtValidationMode::Off;
    _lastBlockingValidationWaitMilliseconds = 0.0F;
    if (_diagnostics.IsFaulted())
    {
        SetError(errorMessage, _diagnostics.FaultMessage());
        return false;
    }
    if (input.Settings.CbtValidationMode == TerrainLodCbtValidationMode::BlockingSmoke &&
        _topologyFrameGeneration != 0U)
    {
        // 阻塞模式只用于验证与故障定位，等待时间不混入任何 GPU pass
        // idle 后消费全部 pending 槽，可立即暴露此前延迟的 shader 故障
        Tools::PerformanceTimer waitTimer;
        _backend->WaitForGpuIdle();
        _lastBlockingValidationWaitMilliseconds = waitTimer.Stop();
        if (!_diagnostics.ConsumeAllCompleted(errorMessage))
        {
            return false;
        }
    }
    if (!_diagnostics.ConsumeCompleted(frameIndex, errorMessage))
    {
        return false;
    }

    // Delayed 与 Off 都只消费当前已回收的 frame 槽
    // 该位置不额外等待，因为 BeginFrame 已完成槽位 fence 同步
    // 验证值与本帧 readback 槽绑定；等该 swap-chain 槽完成并再次复用时再比较，
    // 因而不会为统计或验证额外等待 GPU。
    std::uint32_t expectedSplitCandidateCount = 0U;
    std::uint32_t expectedSimplifyCandidateCount = 0U;
    std::array<std::uint32_t, CbtBaseBisectorCount> expectedSubdivisionPatterns{};
    std::uint32_t expectedPlannedSplitNodeCount = 0U;
    std::uint32_t expectedAllocatedSplitSlotCount = 0U;
    std::uint32_t expectedRemainingDynamicSlotCount = 0U;
    std::array<CbtTerrainGeometryResult, CbtBaseBisectorCount> expectedBaseGeometry{};
    const bool exactReference = fullValidation && _topologyFrameGeneration == 0U;
    if (exactReference)
    {
        const auto triangles = BuildCbtBaseClassificationTriangles(
            topology,
            *input.HeightMap,
            input.Settings.TerrainSize,
            input.Settings.HeightScale);
        const std::uint32_t maxDepth = static_cast<std::uint32_t>(std::max(input.Settings.MaxDepth, 0));
        std::array<CbtClassificationResult, CbtBaseBisectorCount> classificationResults{};
        std::vector<std::uint32_t> splitCandidates;
        for (std::size_t node = 0U; node < triangles.size(); ++node)
        {
            const CbtClassificationResult result = EvaluateCbtClassification(
                triangles[node],
                input.View,
                input.Settings.CbtTriangleAreaPixels,
                topology.BaseDepth,
                maxDepth).Result;
            classificationResults[node] = result;
            expectedSplitCandidateCount += result == CbtClassificationResult::Bisect ? 1U : 0U;
            if (result == CbtClassificationResult::Bisect)
            {
                splitCandidates.push_back(static_cast<std::uint32_t>(node));
            }
        }
        // E1 只分类固定 base leaf；官方协议禁止 base depth 进入 simplify 候选表。
        expectedSimplifyCandidateCount = 0U;

        std::vector<CbtSplitPlanningNode> planningNodes(CbtBaseBisectorCount);
        const auto toLocalNeighbor = [&](std::uint32_t physicalNeighbor) {
            if (physicalNeighbor == InvalidCbtBisectorIndex)
            {
                return InvalidCbtBisectorIndex;
            }
            if (physicalNeighbor < topology.Layout.BaseElementOffset ||
                physicalNeighbor >= topology.Layout.TotalElementCount)
            {
                return InvalidCbtBisectorIndex;
            }
            return physicalNeighbor - topology.Layout.BaseElementOffset;
        };
        for (std::size_t node = 0U; node < planningNodes.size(); ++node)
        {
            const CbtBisectorNeighbors& neighbors = topology.Neighbors[node];
            planningNodes[node] = {
                topology.HeapIds[node],
                {
                    toLocalNeighbor(neighbors.Previous),
                    toLocalNeighbor(neighbors.Next),
                    toLocalNeighbor(neighbors.Twin),
                },
                classificationResults[node] == CbtClassificationResult::Bisect
                    ? static_cast<std::int32_t>(CbtClassificationResult::Bisect)
                    : static_cast<std::int32_t>(CbtClassificationResult::Unchanged),
            };
        }
        const CbtSplitPlanningResult plan = PlanCbtSplits(
            planningNodes,
            splitCandidates,
            topology.BaseDepth,
            topology.Layout.DynamicElementCount);
        if (!plan.Valid || plan.SubdivisionPatterns.size() != expectedSubdivisionPatterns.size())
        {
            SetError(errorMessage, "CBT CPU split planning reference rejected the base topology");
            return false;
        }
        std::copy(
            plan.SubdivisionPatterns.begin(),
            plan.SubdivisionPatterns.end(),
            expectedSubdivisionPatterns.begin());
        expectedPlannedSplitNodeCount = static_cast<std::uint32_t>(plan.AllocationNodes.size());
        expectedAllocatedSplitSlotCount = plan.RequiredSlotCount;
        expectedRemainingDynamicSlotCount = plan.RemainingMemory;

        for (std::size_t node = 0U; node < expectedBaseGeometry.size(); ++node)
        {
            // 首帧 split 后原物理槽保留哪个逻辑 child 由四模板协议决定。
            // center 与 left-double 保留一层偶子，right-double 与 triple 保留两层偶子。
            // 该推导只覆盖固定六槽，因此无需复制动态 heapID 表构造 CPU 参考。
            // height-map reload 会重建 pipeline，使新纹理再次进入这条精确路径。
            const std::uint32_t pattern = expectedSubdivisionPatterns[node];
            std::uint64_t retainedHeapId = topology.HeapIds[node];
            if (pattern == CbtCenterSplitPattern || pattern == CbtLeftDoubleSplitPattern)
            {
                retainedHeapId *= 2U;
            }
            else if (pattern == CbtRightDoubleSplitPattern || pattern == CbtTripleSplitPattern)
            {
                retainedHeapId *= 4U;
            }
            expectedBaseGeometry[node] = EvaluateCbtTerrainGeometry(
                retainedHeapId,
                topology.BaseDepth,
                topology.ControlPoints,
                *input.HeightMap,
                input.Settings.TerrainSize,
                input.Settings.HeightScale);
            if (!expectedBaseGeometry[node].Valid)
            {
                SetError(errorMessage, "CBT CPU terrain geometry reference rejected a retained base bisector");
                return false;
            }
        }
    }

    std::memset(_mappedConstants[frameIndex], 0, ConstantBufferBytes);
    std::memcpy(_mappedConstants[frameIndex], &input.View.ViewProjection, sizeof(input.View.ViewProjection));
    const std::array<std::uint32_t, 7> geometryConstants{
        topology.Layout.TotalElementCount,
        topology.Layout.BaseElementOffset,
        CbtBaseBisectorCount,
        topology.BaseDepth,
        topology.Layout.DynamicElementCount,
        static_cast<std::uint32_t>(std::max(input.Settings.MaxDepth, 0)),
        _neighborReadIndex,
    };
    std::memcpy(
        _mappedConstants[frameIndex] + ConstantBufferSlotBytes,
        geometryConstants.data(),
        sizeof(geometryConstants));

    TopologyUpdateConstants updateConstants{};
    updateConstants.AreaAndScreen = {
        input.Settings.CbtTriangleAreaPixels,
        static_cast<float>(input.View.DrawableWidth),
        static_cast<float>(input.View.DrawableHeight),
        0.0F,
    };
    updateConstants.CameraPosition = {
        input.View.CameraPosition.x,
        input.View.CameraPosition.y,
        input.View.CameraPosition.z,
        0.0F,
    };
    updateConstants.CameraForward = {
        input.View.CameraForward.x,
        input.View.CameraForward.y,
        input.View.CameraForward.z,
        0.0F,
    };
    for (std::size_t plane = 0U; plane < updateConstants.FrustumPlanes.size(); ++plane)
    {
        const glm::vec4& source = input.View.FrustumPlanes[plane];
        updateConstants.FrustumPlanes[plane] = {source.x, source.y, source.z, source.w};
    }
    std::memcpy(
        _mappedConstants[frameIndex] + ConstantBufferSlotBytes * 2U,
        &updateConstants,
        sizeof(updateConstants));

    ID3D12DescriptorHeap* descriptorHeaps[] = {_backend->ShaderVisibleSrvHeap()};
    commandList->SetDescriptorHeaps(1U, descriptorHeaps);
    const BootstrapConstants bootstrapConstants{
        topology.Layout.TotalElementCount,
        topology.Layout.BaseElementOffset,
        CbtBaseBisectorCount,
        input.Settings.TerrainSize,
        topology.BaseDepth,
        static_cast<std::uint32_t>(input.HeightMap->Width()),
        static_cast<std::uint32_t>(input.HeightMap->Height()),
        input.Settings.HeightScale,
    };
    const auto bindBootstrapResources = [&]() {
        // 两个 bootstrap 区段共享完全相同的 root ABI，集中绑定可防止槽位漂移。
        // buffer SRV 使用 GPU virtual address，高度 Texture2D 必须使用描述符表。
        // validation 保持独立 u7，避免改变 topology shader 的官方 u0..u16 ABI。
        // 每次 topology root signature 覆盖后都重新调用该绑定函数。
        commandList->SetComputeRootSignature(_bootstrapRootSignature.Get());
        commandList->SetComputeRoot32BitConstants(0U, 8U, &bootstrapConstants, 0U);
        commandList->SetComputeRootShaderResourceView(1U, resources.HeapIds->GetGPUVirtualAddress());
        commandList->SetComputeRootShaderResourceView(2U, resources.BisectorData->GetGPUVirtualAddress());
        commandList->SetComputeRootShaderResourceView(3U, resources.BaseControlPoints->GetGPUVirtualAddress());
        commandList->SetComputeRootUnorderedAccessView(4U, resources.ActiveIndices->GetGPUVirtualAddress());
        commandList->SetComputeRootUnorderedAccessView(5U, resources.VisibleIndices->GetGPUVirtualAddress());
        commandList->SetComputeRootUnorderedAccessView(6U, resources.ModifiedIndices->GetGPUVirtualAddress());
        commandList->SetComputeRootUnorderedAccessView(7U, resources.IndirectDrawState->GetGPUVirtualAddress());
        commandList->SetComputeRootUnorderedAccessView(8U, resources.GeometryDispatchCommands->GetGPUVirtualAddress());
        commandList->SetComputeRootUnorderedAccessView(
            9U,
            _geometry.ClassificationPositions()->GetGPUVirtualAddress());
        commandList->SetComputeRootUnorderedAccessView(
            10U,
            _geometry.RenderVertices()->GetGPUVirtualAddress());
        commandList->SetComputeRootDescriptorTable(11U, _geometry.HeightMapSrv());
        commandList->SetComputeRootUnorderedAccessView(12U, resources.Validation->GetGPUVirtualAddress());
    };
    bindBootstrapResources();

    const auto beginGpuStage = [&](TerrainLodCbtGpuStage stage) {
        _diagnostics.BeginGpuStage(commandList, frameIndex, stage);
    };
    // 包装器使所有 pass 保持统一的 query 索引和 frame 槽归属
    const auto endGpuStage = [&](TerrainLodCbtGpuStage stage) {
        _diagnostics.EndGpuStage(commandList, frameIndex, stage);
    };

    // Classify 必须看到当前参数域中的三子位置和父位置。首次运行和 TerrainSize
    // 变化都按上一代 active list 全量重建，避免动态槽继续携带旧尺度坐标。
    beginGpuStage(TerrainLodCbtGpuStage::ClassificationGeometry);
    if (rebuildGeometry)
    {
        Transition(commandList, resources.HeapIds, _heapIdState, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        Transition(commandList, resources.BisectorData, _bisectorDataState, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        Transition(commandList, resources.BaseControlPoints, _baseControlPointState, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        Transition(commandList, resources.ActiveIndices, _activeIndexState, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        Transition(commandList, resources.GeometryDispatchCommands, _geometryDispatchState, D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT);
        _geometry.TransitionClassification(commandList, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        _geometry.TransitionVertices(commandList, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        commandList->SetPipelineState(_geometry.ActivePipeline());
        commandList->ExecuteIndirect(
            _dispatchCommandSignature.Get(),
            1U,
            resources.GeometryDispatchCommands,
            0U,
            nullptr,
            0U);
        UavBarrier(commandList, _geometry.ClassificationPositions());
        UavBarrier(commandList, _geometry.RenderVertices());
    }
    endGpuStage(TerrainLodCbtGpuStage::ClassificationGeometry);
    _geometry.TransitionClassification(commandList, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    _geometry.TransitionVertices(commandList, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);

    Transition(commandList, resources.ActiveIndices, _activeIndexState, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    Transition(commandList, resources.GeometryDispatchCommands, _geometryDispatchState, D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT);
    Transition(commandList, resources.HeapIds, _heapIdState, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    Transition(commandList, resources.BisectorData, _bisectorDataState, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    Transition(commandList, resources.Classification, _classificationState, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    Transition(commandList, resources.TopologyDispatchCommands, _topologyDispatchState, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    Transition(commandList, resources.IndirectDrawState, _drawState, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

    commandList->SetComputeRootSignature(_topologyRootSignature.Get());
    const D3D12_GPU_VIRTUAL_ADDRESS constantsAddress = _constantBuffers[frameIndex]->GetGPUVirtualAddress();
    commandList->SetComputeRootConstantBufferView(0U, constantsAddress);
    commandList->SetComputeRootConstantBufferView(1U, constantsAddress + ConstantBufferSlotBytes);
    commandList->SetComputeRootConstantBufferView(2U, constantsAddress + ConstantBufferSlotBytes * 2U);
    commandList->SetComputeRootDescriptorTable(3U, _topologySrvRange.Gpu);
    commandList->SetComputeRootDescriptorTable(4U, _topologyUavRange.Gpu);

    const CbtOccupancyLayout& occupancy = topology.Layout.Occupancy;
    CapacityPipelines& pipelines = _pipelines;
    beginGpuStage(TerrainLodCbtGpuStage::Reset);
    commandList->SetPipelineState(pipelines.Reset.Get());
    commandList->Dispatch(1U, 1U, 1U);
    UavBarrier(commandList);
    endGpuStage(TerrainLodCbtGpuStage::Reset);

    // 上一帧 PrepareIndirect 的第一组 dispatch 与 drawState[9] 共同限定活动列表消费范围
    beginGpuStage(TerrainLodCbtGpuStage::Classify);
    commandList->SetPipelineState(pipelines.Classify.Get());
    commandList->ExecuteIndirect(
        _dispatchCommandSignature.Get(),
        1U,
        resources.GeometryDispatchCommands,
        0U,
        nullptr,
        0U);
    UavBarrier(commandList);

    commandList->SetPipelineState(pipelines.PrepareClassificationIndirect.Get());
    commandList->Dispatch(1U, 1U, 1U);
    UavBarrier(commandList, resources.TopologyDispatchCommands);
    Transition(
        commandList,
        resources.TopologyDispatchCommands,
        _topologyDispatchState,
        D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT);
    endGpuStage(TerrainLodCbtGpuStage::Classify);

    // Split 只规划兼容链和预留内存，不复制邻接、不写 heapID、也不修改 OCBT 位
    beginGpuStage(TerrainLodCbtGpuStage::Split);
    commandList->SetPipelineState(pipelines.Split.Get());
    commandList->ExecuteIndirect(
        _dispatchCommandSignature.Get(),
        1U,
        resources.TopologyDispatchCommands,
        0U,
        nullptr,
        0U);
    UavBarrier(commandList);

    Transition(
        commandList,
        resources.TopologyDispatchCommands,
        _topologyDispatchState,
        D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    commandList->SetPipelineState(pipelines.PrepareAllocationIndirect.Get());
    commandList->Dispatch(1U, 1U, 1U);
    UavBarrier(commandList, resources.TopologyDispatchCommands);
    Transition(
        commandList,
        resources.TopologyDispatchCommands,
        _topologyDispatchState,
        D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT);
    endGpuStage(TerrainLodCbtGpuStage::Split);

    // Allocate 从仍未修改的旧 OCBT 补集中取得互异槽位，只写 BisectorData.indices。
    beginGpuStage(TerrainLodCbtGpuStage::Allocate);
    commandList->SetPipelineState(pipelines.Allocate.Get());
    commandList->ExecuteIndirect(
        _dispatchCommandSignature.Get(),
        1U,
        resources.TopologyDispatchCommands,
        sizeof(std::uint32_t) * 6U,
        nullptr,
        0U);
    UavBarrier(commandList);
    endGpuStage(TerrainLodCbtGpuStage::Allocate);

    // 整资源复制由 D3D12 copy 语义完成；随后四模板只覆盖下一代的局部节点。
    beginGpuStage(TerrainLodCbtGpuStage::NeighborCopy);
    const std::uint32_t neighborWriteIndex = _neighborReadIndex ^ 1U;
    ID3D12Resource* currentNeighbors = resources.Neighbors[_neighborReadIndex];
    ID3D12Resource* nextNeighbors = resources.Neighbors[neighborWriteIndex];
    Transition(
        commandList,
        currentNeighbors,
        _neighborStates[_neighborReadIndex],
        D3D12_RESOURCE_STATE_COPY_SOURCE);
    Transition(
        commandList,
        nextNeighbors,
        _neighborStates[neighborWriteIndex],
        D3D12_RESOURCE_STATE_COPY_DEST);
    commandList->CopyResource(nextNeighbors, currentNeighbors);
    Transition(
        commandList,
        currentNeighbors,
        _neighborStates[_neighborReadIndex],
        D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    Transition(
        commandList,
        nextNeighbors,
        _neighborStates[neighborWriteIndex],
        D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    endGpuStage(TerrainLodCbtGpuStage::NeighborCopy);
    beginGpuStage(TerrainLodCbtGpuStage::Bisect);
    commandList->SetPipelineState(pipelines.Bisect.Get());
    commandList->ExecuteIndirect(
        _dispatchCommandSignature.Get(),
        1U,
        resources.TopologyDispatchCommands,
        sizeof(std::uint32_t) * 6U,
        nullptr,
        0U);
    UavBarrier(commandList);
    endGpuStage(TerrainLodCbtGpuStage::Bisect);
    beginGpuStage(TerrainLodCbtGpuStage::PropagateBisect);

    Transition(
        commandList,
        resources.TopologyDispatchCommands,
        _topologyDispatchState,
        D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    commandList->SetPipelineState(pipelines.PreparePropagationIndirect.Get());
    commandList->Dispatch(1U, 1U, 1U);
    UavBarrier(commandList, resources.TopologyDispatchCommands);
    Transition(
        commandList,
        resources.TopologyDispatchCommands,
        _topologyDispatchState,
        D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT);
    commandList->SetPipelineState(pipelines.PropagateBisect.Get());
    commandList->ExecuteIndirect(
        _dispatchCommandSignature.Get(),
        1U,
        resources.TopologyDispatchCommands,
        0U,
        nullptr,
        0U);
    UavBarrier(commandList);
    endGpuStage(TerrainLodCbtGpuStage::PropagateBisect);

    // merge 继续写 Bisect 已发布的 next 邻接代次，Reduce 只在两类提交都完成后执行一次
    // PrepareSimplify 重新检查同帧 split 后的深度、pair 和 facing-twin 关系。
    // Simplify 上移保留 heapID，清除 sibling heapID/OCBT 位并记录传播上下文。
    // PropagateSimplify 追踪同帧删除链，使外部邻居最终指向保留节点。
    // 第二组 indirect 参数先保存 simplify candidate 数，再依次改写为合法 merge 和传播数。
    beginGpuStage(TerrainLodCbtGpuStage::PrepareSimplify);
    commandList->SetPipelineState(pipelines.PrepareSimplify.Get());
    commandList->ExecuteIndirect(
        _dispatchCommandSignature.Get(),
        1U,
        resources.TopologyDispatchCommands,
        sizeof(std::uint32_t) * 3U,
        nullptr,
        0U);
    UavBarrier(commandList);

    Transition(
        commandList,
        resources.TopologyDispatchCommands,
        _topologyDispatchState,
        D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    commandList->SetPipelineState(pipelines.PrepareSimplifyIndirect.Get());
    commandList->Dispatch(1U, 1U, 1U);
    UavBarrier(commandList, resources.TopologyDispatchCommands);
    Transition(
        commandList,
        resources.TopologyDispatchCommands,
        _topologyDispatchState,
        D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT);
    endGpuStage(TerrainLodCbtGpuStage::PrepareSimplify);
    beginGpuStage(TerrainLodCbtGpuStage::Simplify);
    commandList->SetPipelineState(pipelines.Simplify.Get());
    commandList->ExecuteIndirect(
        _dispatchCommandSignature.Get(),
        1U,
        resources.TopologyDispatchCommands,
        sizeof(std::uint32_t) * 3U,
        nullptr,
        0U);
    UavBarrier(commandList);
    endGpuStage(TerrainLodCbtGpuStage::Simplify);

    beginGpuStage(TerrainLodCbtGpuStage::PropagateSimplify);
    Transition(
        commandList,
        resources.TopologyDispatchCommands,
        _topologyDispatchState,
        D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    commandList->SetPipelineState(pipelines.PrepareSimplifyPropagationIndirect.Get());
    commandList->Dispatch(1U, 1U, 1U);
    UavBarrier(commandList, resources.TopologyDispatchCommands);
    Transition(
        commandList,
        resources.TopologyDispatchCommands,
        _topologyDispatchState,
        D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT);
    commandList->SetPipelineState(pipelines.PropagateSimplify.Get());
    commandList->ExecuteIndirect(
        _dispatchCommandSignature.Get(),
        1U,
        resources.TopologyDispatchCommands,
        sizeof(std::uint32_t) * 3U,
        nullptr,
        0U);
    UavBarrier(commandList);
    endGpuStage(TerrainLodCbtGpuStage::PropagateSimplify);

    beginGpuStage(TerrainLodCbtGpuStage::ReducePre);
    commandList->SetPipelineState(pipelines.ReducePre.Get());
    commandList->Dispatch(DispatchCount(occupancy.LastTreeNodeCount), 1U, 1U);
    UavBarrier(commandList, resources.OccupancyTree);
    endGpuStage(TerrainLodCbtGpuStage::ReducePre);
    beginGpuStage(TerrainLodCbtGpuStage::ReduceFirst);
    commandList->SetPipelineState(pipelines.ReduceFirst.Get());
    commandList->Dispatch(occupancy.SubtreeCount, 1U, 1U);
    UavBarrier(commandList, resources.OccupancyTree);
    endGpuStage(TerrainLodCbtGpuStage::ReduceFirst);
    beginGpuStage(TerrainLodCbtGpuStage::ReduceSecond);
    commandList->SetPipelineState(pipelines.ReduceSecond.Get());
    commandList->Dispatch(1U, 1U, 1U);
    UavBarrier(commandList, resources.OccupancyTree);
    endGpuStage(TerrainLodCbtGpuStage::ReduceSecond);

    // 先复制稳定计数；全拓扑验证与 draw state 在 Indexation 后补写同一延迟槽
    // 两种路径都延迟到该 frame index 完成复用时映射，不增加 GPU wait。
    Transition(commandList, resources.Classification, _classificationState, D3D12_RESOURCE_STATE_COPY_SOURCE);
    Transition(commandList, resources.Allocation, _allocationState, D3D12_RESOURCE_STATE_COPY_SOURCE);
    Transition(commandList, resources.Memory, _memoryState, D3D12_RESOURCE_STATE_COPY_SOURCE);
    Transition(commandList, resources.Validation, _validationState, D3D12_RESOURCE_STATE_COPY_SOURCE);
    commandList->CopyBufferRegion(
        _diagnostics.Readback(frameIndex),
        ClassificationCounterOffset,
        resources.Classification,
        0U,
        sizeof(std::uint32_t) * 2U);
    commandList->CopyBufferRegion(
        _diagnostics.Readback(frameIndex),
        AllocationCounterOffset,
        resources.Allocation,
        0U,
        sizeof(std::uint32_t));
    commandList->CopyBufferRegion(
        _diagnostics.Readback(frameIndex),
        MemoryCounterOffset,
        resources.Memory,
        0U,
        sizeof(std::uint32_t) * 2U);
    commandList->CopyBufferRegion(
        _diagnostics.Readback(frameIndex),
        ValidationCounterOffset,
        resources.Validation,
        0U,
        sizeof(std::uint32_t) * CbtValidationWordCount);
    // OCBT 根是常规活动统计的一部分，每帧复制一个 uint 不触发同步等待。
    Transition(commandList, resources.OccupancyTree, _occupancyTreeState, D3D12_RESOURCE_STATE_COPY_SOURCE);
    commandList->CopyBufferRegion(
        _diagnostics.Readback(frameIndex),
        OccupancyRootOffset,
        resources.OccupancyTree,
        0U,
        sizeof(std::uint32_t));
    Transition(commandList, resources.OccupancyTree, _occupancyTreeState, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    if (fullValidation)
    {
        Transition(commandList, resources.BisectorData, _bisectorDataState, D3D12_RESOURCE_STATE_COPY_SOURCE);
        commandList->CopyBufferRegion(
            _diagnostics.Readback(frameIndex),
            BaseBisectorDataOffset,
            resources.BisectorData,
            static_cast<std::uint64_t>(topology.Layout.BaseElementOffset) * sizeof(CbtBisectorData),
            sizeof(CbtBisectorData) * CbtBaseBisectorCount);
        Transition(commandList, resources.BisectorData, _bisectorDataState, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    }
    Transition(commandList, resources.Classification, _classificationState, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    Transition(commandList, resources.Allocation, _allocationState, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    Transition(commandList, resources.Memory, _memoryState, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    Transition(commandList, resources.Validation, _validationState, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    D3D12CbtDiagnosticExpectation expectation{};
    expectation.SplitCandidateCount = expectedSplitCandidateCount;
    expectation.SimplifyCandidateCount = expectedSimplifyCandidateCount;
    expectation.SubdivisionPatterns = expectedSubdivisionPatterns;
    expectation.PlannedSplitNodeCount = expectedPlannedSplitNodeCount;
    expectation.AllocatedSplitSlotCount = expectedAllocatedSplitSlotCount;
    expectation.RemainingDynamicSlotCount = expectedRemainingDynamicSlotCount;
    expectation.BaseGeometry = expectedBaseGeometry;
    _diagnostics.QueueSample(
        frameIndex,
        _topologyFrameGeneration + 1U,
        fullValidation,
        exactReference,
        expectation);

    Transition(commandList, resources.ActiveIndices, _activeIndexState, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    Transition(commandList, resources.VisibleIndices, _visibleIndexState, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    Transition(commandList, resources.ModifiedIndices, _modifiedIndexState, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    Transition(commandList, resources.GeometryDispatchCommands, _geometryDispatchState, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    Transition(commandList, resources.HeapIds, _heapIdState, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    Transition(commandList, resources.BisectorData, _bisectorDataState, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    Transition(commandList, resources.BaseControlPoints, _baseControlPointState, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);

    bindBootstrapResources();

    beginGpuStage(TerrainLodCbtGpuStage::Indexation);
    commandList->SetPipelineState(_indexationPipeline.Get());
    commandList->Dispatch(DispatchCount(topology.Layout.TotalElementCount), 1U, 1U);
    UavBarrier(commandList);
    commandList->SetPipelineState(_prepareIndirectPipeline.Get());
    commandList->Dispatch(1U, 1U, 1U);
    UavBarrier(commandList);
    endGpuStage(TerrainLodCbtGpuStage::Indexation);

    // modified list 中每个节点重新解码当前 child 和父辅助点，供下一帧分类及本帧绘制使用。
    Transition(
        commandList,
        resources.GeometryDispatchCommands,
        _geometryDispatchState,
        D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT);
    _geometry.TransitionClassification(commandList, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    _geometry.TransitionVertices(commandList, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    beginGpuStage(TerrainLodCbtGpuStage::RenderGeometry);
    // ModifiedOnly 消费本帧 compact modified list，FullDebug 重算 compact active list
    // 两种模式写相同 vertex ABI，区别只在调度规模和调试覆盖范围
    const bool fullGeometry =
        input.Settings.CbtGeometryMode == TerrainLodCbtGeometryMode::FullDebug;
    commandList->SetPipelineState(fullGeometry ? _geometry.ActivePipeline() : _geometry.ModifiedPipeline());
    commandList->ExecuteIndirect(
        _dispatchCommandSignature.Get(),
        1U,
        resources.GeometryDispatchCommands,
        fullGeometry ? 0U : sizeof(std::uint32_t) * 6U,
        nullptr,
        0U);
    UavBarrier(commandList, _geometry.ClassificationPositions());
    UavBarrier(commandList, _geometry.RenderVertices());
    endGpuStage(TerrainLodCbtGpuStage::RenderGeometry);

    beginGpuStage(TerrainLodCbtGpuStage::Validation);
    if (fullValidation)
    {
        // 几何验证沿 compact active list 检查每个已发布顶点
        commandList->SetPipelineState(_geometry.ValidatePipeline());
        commandList->ExecuteIndirect(
            _dispatchCommandSignature.Get(),
            1U,
            resources.GeometryDispatchCommands,
            0U,
            nullptr,
            0U);
        UavBarrier(commandList, resources.Validation);

        // 完整验证显式开启时才扫描全部槽位；普通帧只保留前面的轻量错误计数回读。
        Transition(commandList, resources.HeapIds, _heapIdState, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        Transition(commandList, resources.BisectorData, _bisectorDataState, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        commandList->SetComputeRootSignature(_topologyRootSignature.Get());
        commandList->SetComputeRootConstantBufferView(0U, constantsAddress);
        commandList->SetComputeRootConstantBufferView(1U, constantsAddress + ConstantBufferSlotBytes);
        commandList->SetComputeRootConstantBufferView(2U, constantsAddress + ConstantBufferSlotBytes * 2U);
        commandList->SetComputeRootDescriptorTable(3U, _topologySrvRange.Gpu);
        commandList->SetComputeRootDescriptorTable(4U, _topologyUavRange.Gpu);
        commandList->SetPipelineState(pipelines.Validate.Get());
        commandList->Dispatch(DispatchCount(topology.Layout.TotalElementCount), 1U, 1U);
        UavBarrier(commandList, resources.Validation);

        // 早先复制的 counters 保持不变；这里用最终验证头覆盖对应片段。
        Transition(commandList, resources.Validation, _validationState, D3D12_RESOURCE_STATE_COPY_SOURCE);
        commandList->CopyBufferRegion(
            _diagnostics.Readback(frameIndex),
            ValidationCounterOffset,
            resources.Validation,
            0U,
            sizeof(std::uint32_t) * CbtValidationWordCount);
        Transition(commandList, resources.Validation, _validationState, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    }
    else
    {
        // Indexation 已归约全部活动 HeapID；常规诊断只追加复制这个 uint，不启用全容量验证。
        Transition(commandList, resources.Validation, _validationState, D3D12_RESOURCE_STATE_COPY_SOURCE);
        commandList->CopyBufferRegion(
            _diagnostics.Readback(frameIndex),
            MaximumActiveDepthReadbackOffset,
            resources.Validation,
            static_cast<std::uint64_t>(CbtValidationMaxActiveDepthWord) * sizeof(std::uint32_t),
            sizeof(std::uint32_t));
        Transition(commandList, resources.Validation, _validationState, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    }
    endGpuStage(TerrainLodCbtGpuStage::Validation);
    if (exactReference)
    {
        // 只复制固定六个 retained base 槽，诊断成本不随 CBT 容量增长。
        // render buffer 以三顶点连续存放，源偏移需要同时乘物理槽和顶点数。
        // classification child plane 使用相同的三点布局。
        // parent plane 位于 totalCount * 3 之后，每物理槽仅保存一个 vec3。
        // 两个资源复制后立即恢复 UAV，保持末尾发布状态路径统一。
        _geometry.TransitionVertices(commandList, D3D12_RESOURCE_STATE_COPY_SOURCE);
        commandList->CopyBufferRegion(
            _diagnostics.Readback(frameIndex),
            BaseRenderVertexOffset,
            _geometry.RenderVertices(),
            static_cast<std::uint64_t>(topology.Layout.BaseElementOffset) * 3U *
                sizeof(Terrain::TerrainMeshVertex),
            sizeof(Terrain::TerrainMeshVertex) * CbtBaseBisectorCount * 3U);
        _geometry.TransitionVertices(commandList, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

        _geometry.TransitionClassification(commandList, D3D12_RESOURCE_STATE_COPY_SOURCE);
        commandList->CopyBufferRegion(
            _diagnostics.Readback(frameIndex),
            BaseClassificationPositionOffset,
            _geometry.ClassificationPositions(),
            static_cast<std::uint64_t>(topology.Layout.BaseElementOffset) * 3U * sizeof(glm::vec3),
            sizeof(glm::vec3) * CbtBaseBisectorCount * 3U);
        commandList->CopyBufferRegion(
            _diagnostics.Readback(frameIndex),
            BaseParentPositionOffset,
            _geometry.ClassificationPositions(),
            (static_cast<std::uint64_t>(topology.Layout.TotalElementCount) * 3U +
             topology.Layout.BaseElementOffset) * sizeof(glm::vec3),
            sizeof(glm::vec3) * CbtBaseBisectorCount);
        _geometry.TransitionClassification(commandList, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    }
    // draw state 与 OCBT 根配对发布，使关闭完整验证时活动数仍逐样本更新。
    Transition(commandList, resources.IndirectDrawState, _drawState, D3D12_RESOURCE_STATE_COPY_SOURCE);
    commandList->CopyBufferRegion(
        _diagnostics.Readback(frameIndex),
        DrawStateReadbackOffset,
        resources.IndirectDrawState,
        0U,
        sizeof(CbtDrawState));
    Transition(commandList, resources.IndirectDrawState, _drawState, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    // query resolve 放在最后一个被测 compute pass 后，terrain draw 由 renderer 单独包围
    _diagnostics.ResolveGpuTimings(commandList, frameIndex);

    Transition(commandList, resources.ActiveIndices, _activeIndexState, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    Transition(commandList, resources.VisibleIndices, _visibleIndexState, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    Transition(commandList, resources.ModifiedIndices, _modifiedIndexState, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    Transition(commandList, resources.IndirectDrawState, _drawState, D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT);
    Transition(commandList, resources.GeometryDispatchCommands, _geometryDispatchState, D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT);
    // 完整验证会把 BisectorData 切回 UAV；程序化 VS 读取前必须统一发布为 SRV 状态。
    Transition(commandList, resources.BisectorData, _bisectorDataState, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    _geometry.TransitionClassification(commandList, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    _geometry.TransitionVertices(commandList, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);

    // 下一帧的常量会把刚写完的 ping/pong 代次作为只读输入。
    _neighborReadIndex ^= 1U;
    ++_topologyFrameGeneration;
    if (errorMessage != nullptr)
    {
        errorMessage->clear();
    }
    return true;
}

ID3D12Resource* D3D12CbtFramePipeline::RenderVertices() const
{
    return _geometry.RenderVertices();
}

ID3D12Resource* D3D12CbtFramePipeline::ClassificationPositions() const
{
    return _geometry.ClassificationPositions();
}

std::size_t D3D12CbtFramePipeline::RenderVertexCapacityBytes() const
{
    return _geometry.RenderVertexCapacityBytes();
}

std::size_t D3D12CbtFramePipeline::ClassificationPositionCapacityBytes() const
{
    return _geometry.ClassificationPositionCapacityBytes();
}

std::uint64_t D3D12CbtFramePipeline::TopologyFrameGeneration() const
{
    return _topologyFrameGeneration;
}

std::uint32_t D3D12CbtFramePipeline::LastSplitCandidateCount() const
{
    return _diagnostics.Snapshot().SplitCandidateCount;
}

std::uint32_t D3D12CbtFramePipeline::LastSimplifyCandidateCount() const
{
    return _diagnostics.Snapshot().SimplifyCandidateCount;
}

std::uint32_t D3D12CbtFramePipeline::LastPlannedSplitNodeCount() const
{
    return _diagnostics.Snapshot().PlannedSplitNodeCount;
}

std::uint32_t D3D12CbtFramePipeline::LastAllocatedSplitSlotCount() const
{
    return _diagnostics.Snapshot().AllocatedSplitSlotCount;
}

std::uint32_t D3D12CbtFramePipeline::LastRemainingDynamicSlotCount() const
{
    return _diagnostics.Snapshot().RemainingDynamicSlotCount;
}

std::uint32_t D3D12CbtFramePipeline::LastDuplicateSplitClaimCount() const
{
    return _diagnostics.Snapshot().DuplicateSplitClaimCount;
}

std::uint32_t D3D12CbtFramePipeline::LastSharedCompatibilityCount() const
{
    return _diagnostics.Snapshot().SharedCompatibilityCount;
}

std::uint32_t D3D12CbtFramePipeline::LastCompatibilityStepCount() const
{
    return _diagnostics.Snapshot().CompatibilityStepCount;
}

std::uint32_t D3D12CbtFramePipeline::LastMaximumCompatibilityLength() const
{
    return _diagnostics.Snapshot().MaximumCompatibilityLength;
}

std::uint32_t D3D12CbtFramePipeline::LastCommittedDynamicSlotCount() const
{
    return _diagnostics.Snapshot().CommittedDynamicSlotCount;
}

std::uint32_t D3D12CbtFramePipeline::LastSplitPropagationCount() const
{
    return _diagnostics.Snapshot().SplitPropagationCount;
}

const std::array<std::uint32_t, 4>& D3D12CbtFramePipeline::LastBisectTemplateCounts() const
{
    return _diagnostics.Snapshot().BisectTemplateCounts;
}

std::uint32_t D3D12CbtFramePipeline::LastPreparedSimplificationCount() const
{
    return _diagnostics.Snapshot().PreparedSimplificationCount;
}

std::uint32_t D3D12CbtFramePipeline::LastReleasedDynamicSlotCount() const
{
    return _diagnostics.Snapshot().ReleasedDynamicSlotCount;
}

std::uint32_t D3D12CbtFramePipeline::LastSimplifyPropagationCount() const
{
    return _diagnostics.Snapshot().SimplifyPropagationCount;
}

std::uint32_t D3D12CbtFramePipeline::LastPairMergeCount() const
{
    return _diagnostics.Snapshot().PairMergeCount;
}

std::uint32_t D3D12CbtFramePipeline::LastQuadMergeCount() const
{
    return _diagnostics.Snapshot().QuadMergeCount;
}

std::uint32_t D3D12CbtFramePipeline::LastActiveDynamicSlotCount() const
{
    return _diagnostics.Snapshot().ActiveDynamicSlotCount;
}

std::uint32_t D3D12CbtFramePipeline::LastIndexedActiveCount() const
{
    return _diagnostics.Snapshot().IndexedActiveCount;
}

std::uint32_t D3D12CbtFramePipeline::LastMaximumActiveDepth() const
{
    return _diagnostics.Snapshot().MaximumActiveDepth;
}

std::uint64_t D3D12CbtFramePipeline::ClassificationSampleGeneration() const
{
    return _diagnostics.Snapshot().SampleGeneration;
}

std::uint64_t D3D12CbtFramePipeline::GpuTimingSampleGeneration() const
{
    return _diagnostics.Snapshot().GpuTimingSampleGeneration;
}

const std::array<float, TerrainLodCbtGpuStageCount>&
D3D12CbtFramePipeline::LastGpuStageMilliseconds() const
{
    return _diagnostics.Snapshot().GpuStageMilliseconds;
}

float D3D12CbtFramePipeline::LastGpuStageSumMilliseconds() const
{
    return _diagnostics.Snapshot().GpuStageSumMilliseconds;
}

float D3D12CbtFramePipeline::LastBlockingValidationWaitMilliseconds() const
{
    return _lastBlockingValidationWaitMilliseconds;
}

bool D3D12CbtFramePipeline::IsFaulted() const
{
    return _diagnostics.IsFaulted();
}

const std::string& D3D12CbtFramePipeline::FaultMessage() const
{
    return _diagnostics.FaultMessage();
}

bool D3D12CbtFramePipeline::IsInitialized() const
{
    return _initialized;
}
} // namespace ParallelRoam::Algorithms::Cbt2024::D3D12
