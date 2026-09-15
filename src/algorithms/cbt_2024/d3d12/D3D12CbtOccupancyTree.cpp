#include "algorithms/cbt_2024/d3d12/D3D12CbtOccupancyTree.h"

#include "algorithms/cbt_2024/Cbt2024Support.h"
#include "algorithms/cbt_2024/CbtOccupancyTree.h"
#include "render/D3D12GraphicsBackend.h"

#include <d3d12.h>
#include <wrl/client.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <numeric>
#include <random>
#include <string>
#include <utility>
#include <vector>

namespace ParallelRoam::Algorithms::Cbt2024::D3D12
{
namespace
{
constexpr std::uint32_t WorkGroupSize = 64U;
constexpr std::uint32_t ResultHeaderCount = 1U;

/// <summary>
/// GPU 更新缓冲中的单个位操作
/// </summary>
struct CbtBitUpdate
{
    std::uint32_t BitIndex{0U};
    std::uint32_t Occupied{0U};
};

/// <summary>
/// 根常量控制更新批次和 rank-select 输出区间
/// </summary>
struct CbtTestConstants
{
    std::uint32_t UpdateOffset{0U};
    std::uint32_t UpdateCount{0U};
    std::uint32_t DecodeCount{0U};
    std::uint32_t ResultOffset{0U};
};

static_assert(sizeof(CbtBitUpdate) == 8U);
static_assert(sizeof(CbtTestConstants) == 16U);

/// <summary>
/// 单个容量特化持有的 OCBT 测试管线和资源
/// </summary>
struct CbtGpuValidationState
{
    CbtOccupancyLayout Layout{};
    Microsoft::WRL::ComPtr<ID3D12RootSignature> RootSignature;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> ClearPipeline;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> ApplyPipeline;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> ReducePrePipeline;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> ReduceFirstPipeline;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> ReduceSecondPipeline;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> DecodeOccupiedPipeline;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> DecodeFreePipeline;
    Microsoft::WRL::ComPtr<ID3D12Resource> Tree;
    Microsoft::WRL::ComPtr<ID3D12Resource> Bitfield;
    Microsoft::WRL::ComPtr<ID3D12Resource> Updates;
    Microsoft::WRL::ComPtr<ID3D12Resource> Results;
    Microsoft::WRL::ComPtr<ID3D12Resource> Readback;
    // 更新缓冲持久映射，析构时必须在资源释放前 Unmap
    std::uint8_t* MappedUpdates{nullptr};
    std::size_t ResultBytes{0U};

    ~CbtGpuValidationState()
    {
        if (Updates != nullptr && MappedUpdates != nullptr)
        {
            Updates->Unmap(0, nullptr);
        }
    }
};

using UpdateBatch = std::vector<CbtBitUpdate>;

void SetError(std::string* errorMessage, const std::string& message)
{
    if (errorMessage != nullptr)
    {
        *errorMessage = message;
    }
}

D3D12_HEAP_PROPERTIES HeapProperties(D3D12_HEAP_TYPE type)
{
    D3D12_HEAP_PROPERTIES properties{};
    properties.Type = type;
    properties.CreationNodeMask = 1U;
    properties.VisibleNodeMask = 1U;
    return properties;
}

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

std::vector<std::uint8_t> ReadShader(const std::string& fileName, std::string* errorMessage)
{
#if defined(PARALLEL_ROAM_DX12_SHADER_DIR)
    const std::filesystem::path shaderDirectory{PARALLEL_ROAM_DX12_SHADER_DIR};
#else
    const std::filesystem::path shaderDirectory{"assets/shaders/dx12"};
#endif
    const std::filesystem::path path = shaderDirectory / fileName;
    std::ifstream stream{path, std::ios::binary | std::ios::ate};
    if (!stream)
    {
        SetError(errorMessage, "Failed to open CBT OCBT shader: " + path.string());
        return {};
    }

    const std::streamsize size = stream.tellg();
    if (size <= 0)
    {
        SetError(errorMessage, "CBT OCBT shader is empty: " + path.string());
        return {};
    }

    std::vector<std::uint8_t> bytes(static_cast<std::size_t>(size));
    stream.seekg(0, std::ios::beg);
    if (!stream.read(reinterpret_cast<char*>(bytes.data()), size))
    {
        SetError(errorMessage, "Failed to read CBT OCBT shader: " + path.string());
        return {};
    }
    return bytes;
}

bool CreateDefaultBuffer(
    ID3D12Device* device,
    std::size_t bytes,
    Microsoft::WRL::ComPtr<ID3D12Resource>& resource,
    std::string* errorMessage)
{
    const D3D12_HEAP_PROPERTIES heap = HeapProperties(D3D12_HEAP_TYPE_DEFAULT);
    const D3D12_RESOURCE_DESC description =
        BufferDescription(bytes, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
    if (FAILED(device->CreateCommittedResource(
            &heap,
            D3D12_HEAP_FLAG_NONE,
            &description,
            D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
            nullptr,
            IID_PPV_ARGS(&resource))))
    {
        SetError(errorMessage, "Failed to allocate CBT OCBT default buffer");
        return false;
    }
    return true;
}

bool CreateMappedUploadBuffer(
    ID3D12Device* device,
    std::size_t bytes,
    Microsoft::WRL::ComPtr<ID3D12Resource>& resource,
    std::uint8_t*& mapped,
    std::string* errorMessage)
{
    // 更新序列在多个测试 case 间复用同一映射，只有析构时解除映射
    const D3D12_HEAP_PROPERTIES heap = HeapProperties(D3D12_HEAP_TYPE_UPLOAD);
    const D3D12_RESOURCE_DESC description = BufferDescription(bytes, D3D12_RESOURCE_FLAG_NONE);
    if (FAILED(device->CreateCommittedResource(
            &heap,
            D3D12_HEAP_FLAG_NONE,
            &description,
            D3D12_RESOURCE_STATE_GENERIC_READ,
            nullptr,
            IID_PPV_ARGS(&resource))))
    {
        SetError(errorMessage, "Failed to allocate CBT OCBT update buffer");
        return false;
    }

    const D3D12_RANGE noRead{0U, 0U};
    void* memory = nullptr;
    if (FAILED(resource->Map(0U, &noRead, &memory)))
    {
        resource.Reset();
        SetError(errorMessage, "Failed to map CBT OCBT update buffer");
        return false;
    }
    mapped = static_cast<std::uint8_t*>(memory);
    return true;
}

bool CreateReadbackBuffer(
    ID3D12Device* device,
    std::size_t bytes,
    Microsoft::WRL::ComPtr<ID3D12Resource>& resource,
    std::string* errorMessage)
{
    const D3D12_HEAP_PROPERTIES heap = HeapProperties(D3D12_HEAP_TYPE_READBACK);
    const D3D12_RESOURCE_DESC description = BufferDescription(bytes, D3D12_RESOURCE_FLAG_NONE);
    if (FAILED(device->CreateCommittedResource(
            &heap,
            D3D12_HEAP_FLAG_NONE,
            &description,
            D3D12_RESOURCE_STATE_COPY_DEST,
            nullptr,
            IID_PPV_ARGS(&resource))))
    {
        SetError(errorMessage, "Failed to allocate CBT OCBT readback buffer");
        return false;
    }
    return true;
}

bool CreateRootSignature(CbtGpuValidationState& state, ID3D12Device* device, std::string* errorMessage)
{
    // 根描述符让验证器可以在 renderer 初始化前独立运行
    // 绑定顺序固定为 b0 常量、u0 树、u1 位域、t0 更新和 u2 结果
    std::array<D3D12_ROOT_PARAMETER, 5> parameters{};
    parameters[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
    parameters[0].Constants.Num32BitValues = 4U;
    parameters[0].Constants.ShaderRegister = 0U;
    parameters[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    parameters[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_UAV;
    parameters[1].Descriptor.ShaderRegister = 0U;
    parameters[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_UAV;
    parameters[2].Descriptor.ShaderRegister = 1U;
    parameters[3].ParameterType = D3D12_ROOT_PARAMETER_TYPE_SRV;
    parameters[3].Descriptor.ShaderRegister = 0U;
    parameters[4].ParameterType = D3D12_ROOT_PARAMETER_TYPE_UAV;
    parameters[4].Descriptor.ShaderRegister = 2U;

    D3D12_ROOT_SIGNATURE_DESC description{};
    description.NumParameters = static_cast<UINT>(parameters.size());
    description.pParameters = parameters.data();

    Microsoft::WRL::ComPtr<ID3DBlob> serialized;
    Microsoft::WRL::ComPtr<ID3DBlob> errors;
    const HRESULT serializedResult = D3D12SerializeRootSignature(
        &description,
        D3D_ROOT_SIGNATURE_VERSION_1,
        &serialized,
        &errors);
    if (FAILED(serializedResult))
    {
        const char* detail = errors != nullptr
            ? static_cast<const char*>(errors->GetBufferPointer())
            : "unknown error";
        SetError(errorMessage, std::string{"Failed to serialize CBT OCBT root signature: "} + detail);
        return false;
    }

    if (FAILED(device->CreateRootSignature(
            0U,
            serialized->GetBufferPointer(),
            serialized->GetBufferSize(),
            IID_PPV_ARGS(&state.RootSignature))))
    {
        SetError(errorMessage, "Failed to create CBT OCBT root signature");
        return false;
    }
    return true;
}

bool CreatePipeline(
    CbtGpuValidationState& state,
    ID3D12Device* device,
    const std::string& shaderName,
    Microsoft::WRL::ComPtr<ID3D12PipelineState>& pipeline,
    std::string* errorMessage)
{
    const std::vector<std::uint8_t> shader = ReadShader(shaderName, errorMessage);
    if (shader.empty())
    {
        return false;
    }

    D3D12_COMPUTE_PIPELINE_STATE_DESC description{};
    description.pRootSignature = state.RootSignature.Get();
    description.CS = {shader.data(), shader.size()};
    if (FAILED(device->CreateComputePipelineState(&description, IID_PPV_ARGS(&pipeline))))
    {
        SetError(errorMessage, "Failed to create CBT OCBT pipeline: " + shaderName);
        return false;
    }
    return true;
}

std::string ShaderPrefix(CbtOccupancyCapacity capacity)
{
    return std::string{"CbtOcbt"} + CbtOccupancyCapacityName(capacity);
}

bool InitializeState(
    CbtGpuValidationState& state,
    ID3D12Device* device,
    CbtOccupancyCapacity capacity,
    std::string* errorMessage)
{
    // 容量布局同时决定资源大小和三段归约的 dispatch 形状
    // 每个容量单独创建 PSO，避免运行时分支改变官方静态布局
    state.Layout = BuildCbtOccupancyLayout(capacity);
    if (!CreateRootSignature(state, device, errorMessage))
    {
        return false;
    }

    const std::string prefix = ShaderPrefix(capacity);
    if (!CreatePipeline(state, device, prefix + "Clear.cso", state.ClearPipeline, errorMessage) ||
        !CreatePipeline(state, device, prefix + "ApplyUpdates.cso", state.ApplyPipeline, errorMessage) ||
        !CreatePipeline(state, device, prefix + "ReducePre.cso", state.ReducePrePipeline, errorMessage) ||
        !CreatePipeline(state, device, prefix + "ReduceFirst.cso", state.ReduceFirstPipeline, errorMessage) ||
        !CreatePipeline(state, device, prefix + "ReduceSecond.cso", state.ReduceSecondPipeline, errorMessage) ||
        !CreatePipeline(
            state,
            device,
            prefix + "DecodeOccupied.cso",
            state.DecodeOccupiedPipeline,
            errorMessage) ||
        !CreatePipeline(state, device, prefix + "DecodeFree.cso", state.DecodeFreePipeline, errorMessage))
    {
        return false;
    }

    const std::size_t treeBytes = state.Layout.TreeSlotCount * sizeof(std::uint32_t);
    const std::size_t bitfieldBytes = state.Layout.BitfieldSlotCount * sizeof(std::uint64_t);
    // 更新缓冲按满容量预留，full case 不需要拆分为多次上传
    const std::size_t updateBytes = state.Layout.ElementCount * sizeof(CbtBitUpdate);
    // 结果缓冲恰好容纳根计数以及全部占用和空闲排列
    state.ResultBytes =
        (static_cast<std::size_t>(state.Layout.ElementCount) + ResultHeaderCount) * sizeof(std::uint32_t);
    return CreateDefaultBuffer(device, treeBytes, state.Tree, errorMessage) &&
           CreateDefaultBuffer(device, bitfieldBytes, state.Bitfield, errorMessage) &&
           CreateMappedUploadBuffer(device, updateBytes, state.Updates, state.MappedUpdates, errorMessage) &&
           CreateDefaultBuffer(device, state.ResultBytes, state.Results, errorMessage) &&
           CreateReadbackBuffer(device, state.ResultBytes, state.Readback, errorMessage);
}

std::uint32_t DispatchCount(std::uint32_t itemCount)
{
    return (itemCount + WorkGroupSize - 1U) / WorkGroupSize;
}

void UavBarrier(ID3D12GraphicsCommandList* commandList)
{
    D3D12_RESOURCE_BARRIER barrier{};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
    // 空资源指针建立所有 OCBT UAV 的跨 pass 写后读顺序
    barrier.UAV.pResource = nullptr;
    commandList->ResourceBarrier(1U, &barrier);
}

void Transition(
    ID3D12GraphicsCommandList* commandList,
    ID3D12Resource* resource,
    D3D12_RESOURCE_STATES before,
    D3D12_RESOURCE_STATES after)
{
    D3D12_RESOURCE_BARRIER barrier{};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition.pResource = resource;
    barrier.Transition.StateBefore = before;
    barrier.Transition.StateAfter = after;
    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    commandList->ResourceBarrier(1U, &barrier);
}

void SetPass(
    ID3D12GraphicsCommandList* commandList,
    ID3D12PipelineState* pipeline,
    const CbtTestConstants& constants)
{
    commandList->SetPipelineState(pipeline);
    commandList->SetComputeRoot32BitConstants(0U, 4U, &constants, 0U);
}

bool ReadResults(
    CbtGpuValidationState& state,
    std::vector<std::uint32_t>& results,
    std::string* errorMessage)
{
    D3D12_RANGE readRange{0U, state.ResultBytes};
    void* mapped = nullptr;
    if (FAILED(state.Readback->Map(0U, &readRange, &mapped)))
    {
        SetError(errorMessage, "Failed to map CBT OCBT readback results");
        return false;
    }

    results.resize(state.Layout.ElementCount + ResultHeaderCount);
    std::memcpy(results.data(), mapped, state.ResultBytes);
    const D3D12_RANGE noWrite{0U, 0U};
    state.Readback->Unmap(0U, &noWrite);
    return true;
}

bool CompareResults(
    const CbtGpuValidationState& state,
    const CbtOccupancyTree& reference,
    const std::string& caseName,
    const std::vector<std::uint32_t>& results,
    std::string* errorMessage)
{
    // GPU 输出按根计数、占用 rank 序列、空闲 rank 序列连续存放
    // 两类序列都逐 rank 比较，不能只用总数掩盖树内寻址错误
    const std::vector<std::uint32_t> active = reference.ActiveIndices();
    const std::vector<std::uint32_t> free = reference.FreeIndices();
    if (results[0] != active.size())
    {
        SetError(
            errorMessage,
            "CBT OCBT " + std::string{CbtOccupancyCapacityName(state.Layout.Capacity)} + " " + caseName +
                " active count mismatch");
        return false;
    }

    for (std::size_t rank = 0U; rank < active.size(); ++rank)
    {
        if (results[ResultHeaderCount + rank] != active[rank])
        {
            SetError(
                errorMessage,
                "CBT OCBT " + std::string{CbtOccupancyCapacityName(state.Layout.Capacity)} + " " + caseName +
                    " occupied rank mismatch at " + std::to_string(rank));
            return false;
        }
    }

    const std::size_t freeOffset = ResultHeaderCount + active.size();
    for (std::size_t rank = 0U; rank < free.size(); ++rank)
    {
        if (results[freeOffset + rank] != free[rank])
        {
            SetError(
                errorMessage,
                "CBT OCBT " + std::string{CbtOccupancyCapacityName(state.Layout.Capacity)} + " " + caseName +
                    " free rank mismatch at " + std::to_string(rank));
            return false;
        }
    }
    return true;
}

bool RunValidationCase(
    CbtGpuValidationState& state,
    Render::D3D12GraphicsBackend& backend,
    const std::string& caseName,
    const std::vector<UpdateBatch>& batches,
    std::string* errorMessage)
{
    // CPU 参考树按批次顺序执行相同更新，避免并行 batch 掩盖跨批依赖
    // GPU 侧在一条命令列表内执行完整 pass 链，最终只读回一次结果
    CbtOccupancyTree reference{state.Layout.Capacity};
    std::vector<CbtBitUpdate> flattenedUpdates;
    for (const UpdateBatch& batch : batches)
    {
        flattenedUpdates.insert(flattenedUpdates.end(), batch.begin(), batch.end());
        for (const CbtBitUpdate& update : batch)
        {
            if (!reference.SetBit(update.BitIndex, update.Occupied != 0U))
            {
                SetError(errorMessage, "CBT OCBT validation update is out of range");
                return false;
            }
        }
    }
    reference.Reduce();

    if (flattenedUpdates.size() > state.Layout.ElementCount)
    {
        SetError(errorMessage, "CBT OCBT validation update buffer capacity exceeded");
        return false;
    }
    if (!flattenedUpdates.empty())
    {
        std::memcpy(
            state.MappedUpdates,
            flattenedUpdates.data(),
            flattenedUpdates.size() * sizeof(CbtBitUpdate));
    }

    const std::uint32_t activeCount = reference.BitCount();
    const std::uint32_t freeCount = state.Layout.ElementCount - activeCount;
    if (!backend.ExecuteImmediate(
            [&](ID3D12GraphicsCommandList* commandList, std::string*) {
                commandList->SetComputeRootSignature(state.RootSignature.Get());
                commandList->SetComputeRootUnorderedAccessView(1U, state.Tree->GetGPUVirtualAddress());
                commandList->SetComputeRootUnorderedAccessView(2U, state.Bitfield->GetGPUVirtualAddress());
                commandList->SetComputeRootShaderResourceView(3U, state.Updates->GetGPUVirtualAddress());
                commandList->SetComputeRootUnorderedAccessView(4U, state.Results->GetGPUVirtualAddress());

                // 每个 case 从空树开始，批次之间用 UAV barrier 保留更新顺序
                // 同一批次保证 bit index 唯一，批内原子操作无需定义先后顺序
                CbtTestConstants constants{};
                SetPass(commandList, state.ClearPipeline.Get(), constants);
                commandList->Dispatch(
                    DispatchCount(std::max(state.Layout.TreeSlotCount, state.Layout.BitfieldSlotCount)),
                    1U,
                    1U);
                UavBarrier(commandList);

                std::uint32_t updateOffset = 0U;
                for (const UpdateBatch& batch : batches)
                {
                    if (!batch.empty())
                    {
                        constants.UpdateOffset = updateOffset;
                        constants.UpdateCount = static_cast<std::uint32_t>(batch.size());
                        SetPass(commandList, state.ApplyPipeline.Get(), constants);
                        commandList->Dispatch(DispatchCount(constants.UpdateCount), 1U, 1U);
                        UavBarrier(commandList);
                    }
                    updateOffset += static_cast<std::uint32_t>(batch.size());
                }

                // 归约先统计 128 位块，再合并 16K 子树，最后由单组写出树顶
                // 每一级读取前一级输出，三级之间都需要显式 UAV 可见性
                constants = {};
                SetPass(commandList, state.ReducePrePipeline.Get(), constants);
                commandList->Dispatch(DispatchCount(state.Layout.LastTreeNodeCount), 1U, 1U);
                UavBarrier(commandList);

                SetPass(commandList, state.ReduceFirstPipeline.Get(), constants);
                commandList->Dispatch(state.Layout.SubtreeCount, 1U, 1U);
                UavBarrier(commandList);

                SetPass(commandList, state.ReduceSecondPipeline.Get(), constants);
                commandList->Dispatch(1U, 1U, 1U);
                UavBarrier(commandList);

                if (activeCount > 0U)
                {
                    // 每个线程独立解析一个 rank，输出顺序天然与 rank 一致
                    constants.DecodeCount = activeCount;
                    constants.ResultOffset = ResultHeaderCount;
                    SetPass(commandList, state.DecodeOccupiedPipeline.Get(), constants);
                    commandList->Dispatch(DispatchCount(activeCount), 1U, 1U);
                }
                if (freeCount > 0U)
                {
                    // 空闲序列紧随占用序列，避免为验证再分配第二个结果缓冲
                    constants.DecodeCount = freeCount;
                    constants.ResultOffset = ResultHeaderCount + activeCount;
                    SetPass(commandList, state.DecodeFreePipeline.Get(), constants);
                    commandList->Dispatch(DispatchCount(freeCount), 1U, 1U);
                }
                UavBarrier(commandList);

                // ExecuteImmediate 在返回前等待 fence，因此回读只需一次复制
                Transition(
                    commandList,
                    state.Results.Get(),
                    D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                    D3D12_RESOURCE_STATE_COPY_SOURCE);
                commandList->CopyBufferRegion(
                    state.Readback.Get(),
                    0U,
                    state.Results.Get(),
                    0U,
                    state.ResultBytes);
                Transition(
                    commandList,
                    state.Results.Get(),
                    D3D12_RESOURCE_STATE_COPY_SOURCE,
                    D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
                return true;
            },
            errorMessage))
    {
        return false;
    }

    std::vector<std::uint32_t> results;
    if (!ReadResults(state, results, errorMessage) ||
        !CompareResults(state, reference, caseName, results, errorMessage))
    {
        return false;
    }

    std::cout << "CBT OCBT " << CbtOccupancyCapacityName(state.Layout.Capacity) << ' ' << caseName
              << ": active=" << activeCount << " free=" << freeCount << '\n';
    return true;
}

UpdateBatch BuildFullBatch(std::uint32_t elementCount)
{
    UpdateBatch batch(elementCount);
    for (std::uint32_t bit = 0U; bit < elementCount; ++bit)
    {
        batch[bit] = CbtBitUpdate{bit, 1U};
    }
    return batch;
}

UpdateBatch BuildAlternatingBatch(std::uint32_t elementCount)
{
    UpdateBatch batch;
    batch.reserve(elementCount / 2U);
    for (std::uint32_t bit = 0U; bit < elementCount; bit += 2U)
    {
        batch.push_back(CbtBitUpdate{bit, 1U});
    }
    return batch;
}

std::vector<UpdateBatch> BuildRandomBatches(std::uint32_t elementCount)
{
    // 三个批次依次置位、清除子集和置位新集合，覆盖跨 dispatch 的状态依赖
    std::vector<std::uint32_t> indices(elementCount);
    std::iota(indices.begin(), indices.end(), 0U);
    std::mt19937 random{0xCB72024U + elementCount};
    std::shuffle(indices.begin(), indices.end(), random);

    const std::uint32_t firstSetCount = std::min<std::uint32_t>(elementCount / 8U, 32768U);
    const std::uint32_t secondSetCount = std::min<std::uint32_t>(elementCount / 16U, 16384U);
    UpdateBatch firstSet;
    UpdateBatch clearSubset;
    UpdateBatch secondSet;
    firstSet.reserve(firstSetCount);
    clearSubset.reserve(firstSetCount / 3U + 1U);
    secondSet.reserve(secondSetCount);
    for (std::uint32_t index = 0U; index < firstSetCount; ++index)
    {
        firstSet.push_back(CbtBitUpdate{indices[index], 1U});
        if (index % 3U == 0U)
        {
            clearSubset.push_back(CbtBitUpdate{indices[index], 0U});
        }
    }
    for (std::uint32_t index = 0U; index < secondSetCount; ++index)
    {
        secondSet.push_back(CbtBitUpdate{indices[firstSetCount + index], 1U});
    }
    return {std::move(firstSet), std::move(clearSubset), std::move(secondSet)};
}

bool ValidateCapacity(
    Render::D3D12GraphicsBackend& backend,
    CbtOccupancyCapacity capacity,
    std::string* errorMessage)
{
    // 边界 case 穿过 64 位 word 和 128 位最深计数节点的分界
    // 交替和随机 case 继续覆盖稀疏分布及跨批清除后重新归约
    CbtGpuValidationState state{};
    if (!InitializeState(state, backend.Device(), capacity, errorMessage))
    {
        return false;
    }

    const std::uint32_t elementCount = state.Layout.ElementCount;
    const UpdateBatch boundary{
        {0U, 1U},
        {1U, 1U},
        {63U, 1U},
        {64U, 1U},
        {65U, 1U},
        {127U, 1U},
        {elementCount - 2U, 1U},
        {elementCount - 1U, 1U},
    };
    return RunValidationCase(state, backend, "empty", {}, errorMessage) &&
           RunValidationCase(state, backend, "full", {BuildFullBatch(elementCount)}, errorMessage) &&
           RunValidationCase(state, backend, "boundary", {boundary}, errorMessage) &&
           RunValidationCase(
               state,
               backend,
               "alternating",
               {BuildAlternatingBatch(elementCount)},
               errorMessage) &&
           RunValidationCase(state, backend, "random-mutation", BuildRandomBatches(elementCount), errorMessage);
}
} // namespace

bool RunD3D12CbtOccupancyTreeSmokeTest(
    Render::D3D12GraphicsBackend& backend,
    std::string* errorMessage)
{
    // 能力检查先于任何资源创建，失败时不污染后端设备状态
    const Cbt2024Availability availability = QueryCbt2024Availability(backend);
    if (!availability.Available)
    {
        SetError(errorMessage, availability.UnavailableReason);
        return false;
    }

    const std::array<CbtOccupancyCapacity, 4> capacities{
        CbtOccupancyCapacity::Capacity128K,
        CbtOccupancyCapacity::Capacity256K,
        CbtOccupancyCapacity::Capacity512K,
        CbtOccupancyCapacity::Capacity1M,
    };
    // 四种容量逐个创建静态特化管线，防止某一组常量偶然通过
    for (const CbtOccupancyCapacity capacity : capacities)
    {
        if (!ValidateCapacity(backend, capacity, errorMessage))
        {
            return false;
        }
    }

    std::cout << "CBT OCBT CPU/GPU validation passed for 128K, 256K, 512K and 1M\n";
    return true;
}
} // namespace ParallelRoam::Algorithms::Cbt2024::D3D12
