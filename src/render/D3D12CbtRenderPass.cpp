#include "render/D3D12CbtRenderPass.h"

#include <algorithm>
#include <array>
#include <filesystem>
#include <fstream>
#include <limits>
#include <vector>

namespace ParallelRoam::Render
{
namespace
{
// 内部 helper 统一遵守可空 errorMessage 约定
// 调用方只处理 bool，不需要理解 HRESULT 或文件流状态
void SetError(std::string* errorMessage, const std::string& message)
{
    if (errorMessage != nullptr)
    {
        *errorMessage = message;
    }
}

// shader 文件一次性载入内存，PSO 创建完成后 vector 即可释放
// 失败消息必须区分打开、空文件和短读三种资源问题
std::vector<std::uint8_t> ReadBinaryFile(const std::filesystem::path& path, std::string* errorMessage)
{
    // PSO 初始化依赖完整 DXIL blob，打开失败必须保留具体文件路径
    std::ifstream stream{path, std::ios::binary | std::ios::ate};
    if (!stream)
    {
        SetError(errorMessage, "Failed to open D3D12 shader: " + path.string());
        return {};
    }
    // 先定位末尾获取长度，避免逐块增长 vector 并隐藏空 shader 产物
    const std::streamsize size = stream.tellg();
    if (size <= 0)
    {
        SetError(errorMessage, "D3D12 shader is empty: " + path.string());
        return {};
    }
    // 单次分配和读取保证返回数据可直接作为 D3D12_SHADER_BYTECODE 使用
    std::vector<std::uint8_t> bytes(static_cast<std::size_t>(size));
    stream.seekg(0, std::ios::beg);
    if (!stream.read(reinterpret_cast<char*>(bytes.data()), size))
    {
        SetError(errorMessage, "Failed to read D3D12 shader: " + path.string());
        return {};
    }
    return bytes;
}

// 状态构造函数显式填写本管线依赖的字段
// 未使用字段保持 D3D12 零初始化语义
D3D12_BLEND_DESC OpaqueBlendDescription()
{
    // terrain pixel shader 输出直接覆盖颜色目标，不参与透明混合
    D3D12_BLEND_DESC description{};
    description.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
    return description;
}

// solid 与 wireframe 只通过 fillMode 分化，其他光栅约定必须一致
D3D12_RASTERIZER_DESC RasterizerDescription(D3D12_FILL_MODE fillMode)
{
    // 人工槽位沿用正式地形逆时针绕序，背面剔除可直接发现索引映射错误
    D3D12_RASTERIZER_DESC description{};
    description.FillMode = fillMode;
    description.CullMode = D3D12_CULL_MODE_BACK;
    description.FrontCounterClockwise = TRUE;
    description.DepthClipEnable = TRUE;
    return description;
}

// 独立函数避免两个 PSO 分支出现深度状态漂移
D3D12_DEPTH_STENCIL_DESC DepthStencilDescription()
{
    // 程序化路径与普通 terrain 共用深度缓冲，必须保持相同 LESS 写入约定
    D3D12_DEPTH_STENCIL_DESC description{};
    description.DepthEnable = TRUE;
    description.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;
    description.DepthFunc = D3D12_COMPARISON_FUNC_LESS;
    return description;
}

D3D12_HEAP_PROPERTIES ReadbackHeapProperties()
{
    D3D12_HEAP_PROPERTIES properties{};
    properties.Type = D3D12_HEAP_TYPE_READBACK;
    properties.CreationNodeMask = 1U;
    properties.VisibleNodeMask = 1U;
    return properties;
}

D3D12_RESOURCE_DESC TimestampReadbackDescription()
{
    D3D12_RESOURCE_DESC description{};
    description.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    description.Width = D3D12CbtRenderPass::GpuTimestampReadbackBytes;
    description.Height = 1U;
    description.DepthOrArraySize = 1U;
    description.MipLevels = 1U;
    description.SampleDesc.Count = 1U;
    description.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    return description;
}
} // namespace

D3D12CbtRenderPass::~D3D12CbtRenderPass()
{
    Shutdown();
}

// 初始化建立稳定管线对象和每帧描述符槽位，不绑定算法资源
// 算法资源可能每次 Build 改变，由 ConfigureResourceDescriptors 延迟写入
bool D3D12CbtRenderPass::Initialize(
    D3D12GraphicsBackend& backend,
    std::string* errorMessage)
{
    // Initialize 支持失败后重试，先释放旧对象避免描述符槽位重复占用
    Shutdown();
    _backend = &backend;
    // 创建顺序遵循依赖关系，任一步失败都由 Shutdown 回滚已经完成的部分
    if (!CreateRootSignature(errorMessage) ||
        !CreatePipelineStates(errorMessage) ||
        !CreateCommandSignature(errorMessage) ||
        !AllocateFrameDescriptors(errorMessage) ||
        !CreateTimingResources(errorMessage))
    {
        Shutdown();
        return false;
    }
    return true;
}

// Shutdown 可重复调用，既服务显式 renderer 清理也服务析构回退
// backend 指针清空后再次调用不会重复归还描述符
void D3D12CbtRenderPass::Shutdown()
{
    // SRV 槽位属于后端分配器，必须在 backend 仍存活时显式归还
    if (_backend != nullptr)
    {
        for (std::uint32_t frameIndex = 0; frameIndex < D3D12GraphicsBackend::FrameCount; ++frameIndex)
        {
            _backend->ReleaseSrvDescriptor(_activeElementSrvs[frameIndex]);
            _backend->ReleaseSrvDescriptor(_vertexSrvs[frameIndex]);
            _backend->ReleaseSrvDescriptor(_lodStateSrvs[frameIndex]);
        }
    }
    // 先归还外部分配的描述符，再释放引用这些槽位的管线对象
    _descriptorGenerations = {};
    _timestampPending = {};
    _timestampGenerations = {};
    _timestampReadbacks = {};
    _timestampQueryHeap.Reset();
    _timestampFrequency = 0U;
    _lastGpuDrawSampleGeneration = 0U;
    _lastGpuDrawMilliseconds = 0.0F;
    _drawCommandSignature.Reset();
    _wireframePipelineState.Reset();
    _fillPipelineState.Reset();
    _rootSignature.Reset();
    _backend = nullptr;
}

void D3D12CbtRenderPass::InvalidateResourceDescriptors()
{
    // 零版本表示每个交换链帧下次使用时都必须重新创建 SRV
    _descriptorGenerations = {};
}

// 该函数是算法借用资源进入图形管线的唯一入口
// 它不记录命令，只为当前 frameIndex 建立稳定 SRV 解释
bool D3D12CbtRenderPass::ConfigureResourceDescriptors(
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
    std::string* errorMessage)
{
    // backend 在进入 Render 前已等待当前 frame fence，因此只允许改写该帧槽位
    if (_backend == nullptr || frameIndex >= D3D12GraphicsBackend::FrameCount)
    {
        SetError(errorMessage, "D3D12 procedural descriptor frame index is invalid");
        return false;
    }
    // 同一资源版本轮转回该帧时复用现有描述符，避免每帧重复设备调用
    if (_descriptorGenerations[frameIndex] == resourceGeneration)
    {
        return true;
    }
    // StructuredBuffer 容量必须是 stride 的整数倍，否则最后一个元素会越界
    if (vertexBuffer == nullptr || activeElementBuffer == nullptr || lodStateBuffer == nullptr ||
        vertexStrideBytes == 0U || activeElementStrideBytes == 0U || lodStateStrideBytes == 0U ||
        vertexCapacityBytes % vertexStrideBytes != 0U ||
        activeElementCapacityBytes % activeElementStrideBytes != 0U ||
        lodStateCapacityBytes % lodStateStrideBytes != 0U)
    {
        SetError(errorMessage, "D3D12 procedural SRV metadata is incomplete");
        return false;
    }

    // D3D12 描述符字段使用 UINT，先在 size_t 中计算再检查收窄范围
    const std::size_t vertexCount = vertexCapacityBytes / vertexStrideBytes;
    const std::size_t activeElementCount = activeElementCapacityBytes / activeElementStrideBytes;
    const std::size_t lodStateCount = lodStateCapacityBytes / lodStateStrideBytes;
    if (vertexStrideBytes > std::numeric_limits<UINT>::max() ||
        activeElementStrideBytes > std::numeric_limits<UINT>::max() ||
        lodStateStrideBytes > std::numeric_limits<UINT>::max() ||
        vertexCount > std::numeric_limits<UINT>::max() ||
        activeElementCount > std::numeric_limits<UINT>::max() ||
        lodStateCount > std::numeric_limits<UINT>::max())
    {
        SetError(errorMessage, "D3D12 procedural SRV exceeds descriptor limits");
        return false;
    }

    // t1 保存活动序号到物理槽位的映射，元素格式由 StructureByteStride 定义
    D3D12_SHADER_RESOURCE_VIEW_DESC activeElementDescription{};
    activeElementDescription.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    activeElementDescription.Format = DXGI_FORMAT_UNKNOWN;
    activeElementDescription.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
    activeElementDescription.Buffer.NumElements = static_cast<UINT>(activeElementCount);
    activeElementDescription.Buffer.StructureByteStride = static_cast<UINT>(activeElementStrideBytes);
    _backend->Device()->CreateShaderResourceView(
        activeElementBuffer,
        &activeElementDescription,
        _activeElementSrvs[frameIndex].Cpu);

    // t2 保存按物理槽位连续排列的三顶点数据，不能复用 t1 的元素跨度
    D3D12_SHADER_RESOURCE_VIEW_DESC vertexDescription{};
    vertexDescription.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    vertexDescription.Format = DXGI_FORMAT_UNKNOWN;
    vertexDescription.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
    vertexDescription.Buffer.NumElements = static_cast<UINT>(vertexCount);
    vertexDescription.Buffer.StructureByteStride = static_cast<UINT>(vertexStrideBytes);
    _backend->Device()->CreateShaderResourceView(
        vertexBuffer,
        &vertexDescription,
        _vertexSrvs[frameIndex].Cpu);

    // t3 让顶点着色器直接观察本帧拓扑事件，避免把短生命周期状态固化进几何缓冲。
    D3D12_SHADER_RESOURCE_VIEW_DESC lodStateDescription{};
    lodStateDescription.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    lodStateDescription.Format = DXGI_FORMAT_UNKNOWN;
    lodStateDescription.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
    lodStateDescription.Buffer.NumElements = static_cast<UINT>(lodStateCount);
    lodStateDescription.Buffer.StructureByteStride = static_cast<UINT>(lodStateStrideBytes);
    _backend->Device()->CreateShaderResourceView(
        lodStateBuffer,
        &lodStateDescription,
        _lodStateSrvs[frameIndex].Cpu);
    // 三个 SRV 都成功写入后才发布版本，失败重试不会误判为已配置
    _descriptorGenerations[frameIndex] = resourceGeneration;
    return true;
}

// RecordDraw 假设当前帧描述符已经配置且 indirectBuffer 满足 DRAW 布局
// 函数不修改资源状态，算法和 renderer 必须提前保证读取状态可用
void D3D12CbtRenderPass::RecordDraw(
    ID3D12GraphicsCommandList* commandList,
    std::uint32_t frameIndex,
    D3D12_GPU_VIRTUAL_ADDRESS constantBufferAddress,
    D3D12_GPU_DESCRIPTOR_HANDLE textureSrv,
    ID3D12Resource* indirectBuffer,
    std::size_t indirectArgumentOffsetBytes,
    bool wireframe,
    std::uint64_t topologyGeneration)
{
    ConsumeCompletedTiming(frameIndex);
    const UINT queryStart = frameIndex * 2U;
    if (_timestampQueryHeap != nullptr)
    {
        commandList->EndQuery(_timestampQueryHeap.Get(), D3D12_QUERY_TYPE_TIMESTAMP, queryStart);
    }
    // 根参数顺序固定为 b0、t0、t1、t2、t3，与独立 root signature 完全一致
    commandList->SetPipelineState(wireframe ? _wireframePipelineState.Get() : _fillPipelineState.Get());
    commandList->SetGraphicsRootSignature(_rootSignature.Get());
    commandList->SetGraphicsRootConstantBufferView(0, constantBufferAddress);
    commandList->SetGraphicsRootDescriptorTable(1, textureSrv);
    commandList->SetGraphicsRootDescriptorTable(2, _activeElementSrvs[frameIndex].Gpu);
    commandList->SetGraphicsRootDescriptorTable(3, _vertexSrvs[frameIndex].Gpu);
    commandList->SetGraphicsRootDescriptorTable(4, _lodStateSrvs[frameIndex].Gpu);
    commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    // 当前命令缓冲只含一条 DRAW，不使用额外 count buffer
    commandList->ExecuteIndirect(
        _drawCommandSignature.Get(),
        1,
        indirectBuffer,
        static_cast<UINT64>(indirectArgumentOffsetBytes),
        nullptr,
        0);
    if (_timestampQueryHeap != nullptr && _timestampReadbacks[frameIndex] != nullptr)
    {
        commandList->EndQuery(_timestampQueryHeap.Get(), D3D12_QUERY_TYPE_TIMESTAMP, queryStart + 1U);
        commandList->ResolveQueryData(
            _timestampQueryHeap.Get(),
            D3D12_QUERY_TYPE_TIMESTAMP,
            queryStart,
            2U,
            _timestampReadbacks[frameIndex].Get(),
            0U);
        _timestampPending[frameIndex] = true;
        _timestampGenerations[frameIndex] = topologyGeneration;
    }
}

// Ready 只表示持久管线对象完整，不代表某一帧算法资源已经绑定
// solid 和 wireframe 都必须存在，运行时切换模式不能触发空 PSO
bool D3D12CbtRenderPass::IsReady() const
{
    return _rootSignature != nullptr &&
           _fillPipelineState != nullptr &&
           _wireframePipelineState != nullptr &&
           _drawCommandSignature != nullptr;
}

float D3D12CbtRenderPass::LastGpuDrawMilliseconds() const
{
    return _lastGpuDrawMilliseconds;
}

std::uint64_t D3D12CbtRenderPass::LastGpuDrawSampleGeneration() const
{
    return _lastGpuDrawSampleGeneration;
}

void D3D12CbtRenderPass::ConsumeCompletedTiming(std::uint32_t frameIndex)
{
    if (frameIndex >= D3D12GraphicsBackend::FrameCount || !_timestampPending[frameIndex] ||
        _timestampReadbacks[frameIndex] == nullptr || _timestampFrequency == 0U)
    {
        return;
    }
    const D3D12_RANGE readRange{0U, GpuTimestampReadbackBytes};
    void* mapped = nullptr;
    if (FAILED(_timestampReadbacks[frameIndex]->Map(0U, &readRange, &mapped)))
    {
        return;
    }
    const auto* timestamps = static_cast<const std::uint64_t*>(mapped);
    if (timestamps[1] >= timestamps[0])
    {
        _lastGpuDrawMilliseconds = static_cast<float>(
            static_cast<double>(timestamps[1] - timestamps[0]) * 1000.0 /
            static_cast<double>(_timestampFrequency));
        _lastGpuDrawSampleGeneration = _timestampGenerations[frameIndex];
    }
    const D3D12_RANGE noWrite{0U, 0U};
    _timestampReadbacks[frameIndex]->Unmap(0U, &noWrite);
    _timestampPending[frameIndex] = false;
}

// 根签名专门描述无 IA 顶点输入的 terrain 绘制协议
// 与普通 terrain root signature 分离，避免程序化 CBT 绘制污染 CPU mesh 管线。
bool D3D12CbtRenderPass::CreateRootSignature(std::string* errorMessage)
{
    // t0 仅供像素着色器采样地形纹理，t1..t3 仅供程序化顶点着色器读取
    // 每个 table 独占一个 range，后续新增 UAV 时不会改变现有根参数编号
    std::array<D3D12_DESCRIPTOR_RANGE, 4> srvRanges{};
    for (std::size_t index = 0; index < srvRanges.size(); ++index)
    {
        srvRanges[index].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
        srvRanges[index].NumDescriptors = 1;
        srvRanges[index].BaseShaderRegister = static_cast<UINT>(index);
        srvRanges[index].OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;
    }

    // range 指针只在当前函数序列化期间使用，局部数组生命周期覆盖调用
    std::array<D3D12_ROOT_PARAMETER, 5> parameters{};
    parameters[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    parameters[0].Descriptor.ShaderRegister = 0;
    parameters[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    for (std::size_t index = 0; index < srvRanges.size(); ++index)
    {
        D3D12_ROOT_PARAMETER& parameter = parameters[index + 1U];
        parameter.ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        parameter.DescriptorTable.NumDescriptorRanges = 1;
        parameter.DescriptorTable.pDescriptorRanges = &srvRanges[index];
        // 限制 shader visibility 可缩小驱动验证范围并暴露错误阶段绑定
        parameter.ShaderVisibility = index == 0U
            ? D3D12_SHADER_VISIBILITY_PIXEL
            : D3D12_SHADER_VISIBILITY_VERTEX;
    }

    // 地形纹理沿用线性 wrap sampler，确保像素着色器与普通 terrain 表现一致
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
    // 当前路径不使用曲面细分和几何阶段，显式拒绝无关根访问
    description.Flags = D3D12_ROOT_SIGNATURE_FLAG_DENY_HULL_SHADER_ROOT_ACCESS |
                        D3D12_ROOT_SIGNATURE_FLAG_DENY_DOMAIN_SHADER_ROOT_ACCESS |
                        D3D12_ROOT_SIGNATURE_FLAG_DENY_GEOMETRY_SHADER_ROOT_ACCESS;

    Microsoft::WRL::ComPtr<ID3DBlob> serialized;
    Microsoft::WRL::ComPtr<ID3DBlob> errors;
    // 序列化错误 blob 通常包含寄存器冲突，是最有价值的初始化诊断信息
    HRESULT result = D3D12SerializeRootSignature(
        &description,
        D3D_ROOT_SIGNATURE_VERSION_1,
        &serialized,
        &errors);
    if (FAILED(result))
    {
        const char* detail = errors != nullptr ? static_cast<const char*>(errors->GetBufferPointer()) : "unknown error";
        SetError(errorMessage, std::string{"D3D12 procedural root signature serialization failed: "} + detail);
        return false;
    }
    result = _backend->Device()->CreateRootSignature(
        0,
        serialized->GetBufferPointer(),
        serialized->GetBufferSize(),
        IID_PPV_ARGS(&_rootSignature));
    if (FAILED(result))
    {
        SetError(errorMessage, "D3D12 procedural root signature creation failed");
        return false;
    }
    return true;
}

// 两个 PSO 共用 shader 和 root signature，仅区分填充模式
// 创建过程不依赖算法 buffer，因此可在 renderer 初始化时一次完成
bool D3D12CbtRenderPass::CreatePipelineStates(std::string* errorMessage)
{
#if defined(PARALLEL_ROAM_DX12_SHADER_DIR)
    // 构建目录优先使用刚编译的 shader，回退路径只服务手动资源布局
    const std::filesystem::path shaderDirectory{PARALLEL_ROAM_DX12_SHADER_DIR};
#else
    const std::filesystem::path shaderDirectory{"assets/shaders/dx12"};
#endif
    // 程序化 VS 与普通 terrain PS 共享输出语义，避免维护第二套材质实现
    const std::vector<std::uint8_t> vertexShader =
        ReadBinaryFile(shaderDirectory / "CbtProceduralTerrainVS.cso", errorMessage);
    if (vertexShader.empty())
    {
        return false;
    }
    const std::vector<std::uint8_t> pixelShader = ReadBinaryFile(shaderDirectory / "TerrainPS.cso", errorMessage);
    if (pixelShader.empty())
    {
        return false;
    }

    // 顶点数据完全由 SRV 和 SV_VertexID 提供，因此 PSO 不声明 IA 输入布局
    D3D12_GRAPHICS_PIPELINE_STATE_DESC description{};
    description.pRootSignature = _rootSignature.Get();
    description.VS = {vertexShader.data(), vertexShader.size()};
    description.PS = {pixelShader.data(), pixelShader.size()};
    description.BlendState = OpaqueBlendDescription();
    description.SampleMask = std::numeric_limits<UINT>::max();
    description.RasterizerState = RasterizerDescription(D3D12_FILL_MODE_SOLID);
    description.DepthStencilState = DepthStencilDescription();
    description.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    description.NumRenderTargets = 1;
    description.RTVFormats[0] = _backend->RenderTargetFormat();
    description.DSVFormat = _backend->DepthStencilFormat();
    description.SampleDesc.Count = 1;
    HRESULT result = _backend->Device()->CreateGraphicsPipelineState(
        &description,
        IID_PPV_ARGS(&_fillPipelineState));
    if (FAILED(result))
    {
        SetError(errorMessage, "D3D12 procedural solid pipeline creation failed: HRESULT=" +
            std::to_string(static_cast<std::uint32_t>(result)));
        return false;
    }

    // 线框 PSO 只改变 rasterizer，根签名和 shader 字节码保持完全相同
    description.RasterizerState = RasterizerDescription(D3D12_FILL_MODE_WIREFRAME);
    result = _backend->Device()->CreateGraphicsPipelineState(
        &description,
        IID_PPV_ARGS(&_wireframePipelineState));
    if (FAILED(result))
    {
        SetError(errorMessage, "D3D12 procedural wireframe pipeline creation failed");
        return false;
    }
    return true;
}

// 命令签名不含 root arguments，所有根绑定由 RecordDraw 直接设置
bool D3D12CbtRenderPass::CreateCommandSignature(std::string* errorMessage)
{
    // 参数缓冲由算法写入 D3D12_DRAW_ARGUMENTS，ByteStride 必须精确匹配
    D3D12_INDIRECT_ARGUMENT_DESC argument{};
    argument.Type = D3D12_INDIRECT_ARGUMENT_TYPE_DRAW;
    D3D12_COMMAND_SIGNATURE_DESC signature{};
    signature.ByteStride = sizeof(D3D12_DRAW_ARGUMENTS);
    signature.NumArgumentDescs = 1;
    signature.pArgumentDescs = &argument;
    if (FAILED(_backend->Device()->CreateCommandSignature(
            &signature,
            nullptr,
            IID_PPV_ARGS(&_drawCommandSignature))))
    {
        SetError(errorMessage, "D3D12 procedural command signature creation failed");
        return false;
    }
    return true;
}

// 分配数量与交换链 FrameCount 固定对应，resize 不需要重新分配
bool D3D12CbtRenderPass::AllocateFrameDescriptors(std::string* errorMessage)
{
    // 描述符内容可变且会被 GPU 异步读取，所以每个交换链帧持有独立槽位
    // 部分分配失败由 Initialize 的 Shutdown 统一归还，不在循环内重复清理
    for (std::uint32_t frameIndex = 0; frameIndex < D3D12GraphicsBackend::FrameCount; ++frameIndex)
    {
        _activeElementSrvs[frameIndex] = _backend->AllocateSrvDescriptor();
        _vertexSrvs[frameIndex] = _backend->AllocateSrvDescriptor();
        _lodStateSrvs[frameIndex] = _backend->AllocateSrvDescriptor();
        if (!_activeElementSrvs[frameIndex].IsValid() ||
            !_vertexSrvs[frameIndex].IsValid() ||
            !_lodStateSrvs[frameIndex].IsValid())
        {
            SetError(errorMessage, "D3D12 SRV heap has no descriptors for procedural terrain rendering");
            return false;
        }
    }
    return true;
}

bool D3D12CbtRenderPass::CreateTimingResources(std::string* errorMessage)
{
    if (_backend == nullptr || _backend->Device() == nullptr || _backend->CommandQueue() == nullptr)
    {
        SetError(errorMessage, "D3D12 procedural timing requires an initialized backend");
        return false;
    }
    D3D12_QUERY_HEAP_DESC queryDescription{};
    queryDescription.Type = D3D12_QUERY_HEAP_TYPE_TIMESTAMP;
    queryDescription.Count = D3D12GraphicsBackend::FrameCount * 2U;
    if (FAILED(_backend->Device()->CreateQueryHeap(
            &queryDescription,
            IID_PPV_ARGS(_timestampQueryHeap.ReleaseAndGetAddressOf()))))
    {
        SetError(errorMessage, "D3D12 procedural draw timestamp query creation failed");
        return false;
    }

    const D3D12_HEAP_PROPERTIES heap = ReadbackHeapProperties();
    const D3D12_RESOURCE_DESC description = TimestampReadbackDescription();
    for (Microsoft::WRL::ComPtr<ID3D12Resource>& readback : _timestampReadbacks)
    {
        if (FAILED(_backend->Device()->CreateCommittedResource(
                &heap,
                D3D12_HEAP_FLAG_NONE,
                &description,
                D3D12_RESOURCE_STATE_COPY_DEST,
                nullptr,
                IID_PPV_ARGS(readback.ReleaseAndGetAddressOf()))))
        {
            SetError(errorMessage, "D3D12 procedural draw timestamp readback creation failed");
            return false;
        }
    }
    if (FAILED(_backend->CommandQueue()->GetTimestampFrequency(&_timestampFrequency)) ||
        _timestampFrequency == 0U)
    {
        SetError(errorMessage, "D3D12 procedural draw timestamp frequency query failed");
        return false;
    }
    return true;
}
} // namespace ParallelRoam::Render
