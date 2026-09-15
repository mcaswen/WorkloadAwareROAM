#include "algorithms/cbt_2024/d3d12/D3D12CbtGpuState.h"

#include "algorithms/cbt_2024/Cbt2024Support.h"
#include "render/D3D12GraphicsBackend.h"

#include <d3d12.h>
#include <wrl/client.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <string>
#include <utility>
#include <vector>

namespace ParallelRoam::Algorithms::Cbt2024::D3D12
{
namespace
{
using Microsoft::WRL::ComPtr;

/// <summary>
/// 一次容量配置拥有的全部 GPU 缓冲
/// </summary>
struct CbtTopologyResources
{
    ComPtr<ID3D12Resource> OccupancyTree;
    ComPtr<ID3D12Resource> OccupancyBitfield;
    ComPtr<ID3D12Resource> HeapIds;
    std::array<ComPtr<ID3D12Resource>, 2> Neighbors;
    ComPtr<ID3D12Resource> BisectorData;
    ComPtr<ID3D12Resource> Classification;
    ComPtr<ID3D12Resource> Simplification;
    ComPtr<ID3D12Resource> Allocation;
    ComPtr<ID3D12Resource> Propagation;
    ComPtr<ID3D12Resource> Memory;
    ComPtr<ID3D12Resource> Validation;
    ComPtr<ID3D12Resource> ActiveIndices;
    ComPtr<ID3D12Resource> VisibleIndices;
    ComPtr<ID3D12Resource> ModifiedIndices;
    ComPtr<ID3D12Resource> TopologyDispatchCommands;
    ComPtr<ID3D12Resource> IndirectDrawState;
    ComPtr<ID3D12Resource> GeometryDispatchCommands;
    ComPtr<ID3D12Resource> BaseControlPoints;
    // 帧内首次初始化时上传资源必须活到该资源代完成；帧外初始化完成后可立即释放
    ComPtr<ID3D12Resource> InitializationUpload;
};

/// <summary>
/// 资源名称、对象和预期字节数的统一验证记录
/// </summary>
struct ResourceRecord
{
    const char* Name{nullptr};
    ID3D12Resource* Resource{nullptr};
    std::size_t Bytes{0U};
};

/// <summary>
/// 从一个 GPU 缓冲选取的紧凑读回片段
/// </summary>
struct ReadbackSlice
{
    const char* Name{nullptr};
    ID3D12Resource* Source{nullptr};
    std::size_t SourceOffset{0U};
    std::size_t ReadbackOffset{0U};
    std::vector<std::uint8_t> Expected;
};

void SetError(std::string* errorMessage, const std::string& message)
{
    if (errorMessage != nullptr)
    {
        *errorMessage = message;
    }
}

std::size_t AlignUp(std::size_t value, std::size_t alignment)
{
    return (value + alignment - 1U) & ~(alignment - 1U);
}

std::size_t BufferBytes(std::uint32_t elementCount, std::size_t elementSize)
{
    return static_cast<std::size_t>(elementCount) * elementSize;
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

bool CreateDefaultBuffer(
    ID3D12Device* device,
    const char* name,
    std::size_t bytes,
    ComPtr<ID3D12Resource>& resource,
    std::string* errorMessage)
{
    const D3D12_HEAP_PROPERTIES heap = HeapProperties(D3D12_HEAP_TYPE_DEFAULT);
    const D3D12_RESOURCE_DESC description =
        BufferDescription(bytes, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
    if (FAILED(device->CreateCommittedResource(
            &heap,
            D3D12_HEAP_FLAG_NONE,
            &description,
            D3D12_RESOURCE_STATE_COPY_DEST,
            nullptr,
            IID_PPV_ARGS(&resource))))
    {
        SetError(errorMessage, std::string{"Failed to allocate CBT base topology buffer: "} + name);
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
        SetError(errorMessage, "Failed to allocate CBT base topology upload buffer");
        return false;
    }
    return true;
}

bool CreateReadbackBuffer(
    ID3D12Device* device,
    std::size_t bytes,
    ComPtr<ID3D12Resource>& resource,
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
        SetError(errorMessage, "Failed to allocate CBT base topology readback buffer");
        return false;
    }
    return true;
}

D3D12CbtGpuResourceView BuildResourceView(const CbtTopologyResources& resources)
{
    return {
        resources.OccupancyTree.Get(),
        resources.OccupancyBitfield.Get(),
        resources.HeapIds.Get(),
        {resources.Neighbors[0].Get(), resources.Neighbors[1].Get()},
        resources.BisectorData.Get(),
        resources.Classification.Get(),
        resources.Simplification.Get(),
        resources.Allocation.Get(),
        resources.Propagation.Get(),
        resources.Memory.Get(),
        resources.Validation.Get(),
        resources.ActiveIndices.Get(),
        resources.VisibleIndices.Get(),
        resources.ModifiedIndices.Get(),
        resources.TopologyDispatchCommands.Get(),
        resources.IndirectDrawState.Get(),
        resources.GeometryDispatchCommands.Get(),
        resources.BaseControlPoints.Get(),
    };
}

std::vector<ResourceRecord> BuildResourceRecords(
    const CbtBaseTopology& topology,
    const D3D12CbtGpuResourceView& resources)
{
    // 这里集中定义物理缓冲尺寸，创建、清零和读回验证共同消费同一记录
    // 后续增加 pass 缓冲时必须只扩展这一份清单，避免某条生命周期路径漏处理
    const CbtTopologyBufferLayout& layout = topology.Layout;
    return {
        {"occupancy tree", resources.OccupancyTree, BufferBytes(layout.Occupancy.TreeSlotCount, sizeof(std::uint32_t))},
        {"occupancy bitfield", resources.OccupancyBitfield, BufferBytes(layout.Occupancy.BitfieldSlotCount, sizeof(std::uint64_t))},
        {"heap IDs", resources.HeapIds, BufferBytes(layout.TotalElementCount, sizeof(std::uint64_t))},
        {"neighbors 0", resources.Neighbors[0], BufferBytes(layout.TotalElementCount, sizeof(CbtBisectorNeighbors))},
        {"neighbors 1", resources.Neighbors[1], BufferBytes(layout.TotalElementCount, sizeof(CbtBisectorNeighbors))},
        {"bisector data", resources.BisectorData, BufferBytes(layout.TotalElementCount, sizeof(CbtBisectorData))},
        {"classification", resources.Classification, BufferBytes(layout.ClassificationElementCount, sizeof(std::uint32_t))},
        {"simplification", resources.Simplification, BufferBytes(layout.SimplificationElementCount, sizeof(std::uint32_t))},
        {"allocation", resources.Allocation, BufferBytes(layout.AllocationElementCount, sizeof(std::uint32_t))},
        {"propagation", resources.Propagation, BufferBytes(layout.PropagationElementCount, sizeof(std::uint32_t))},
        {"memory", resources.Memory, BufferBytes(layout.MemoryElementCount, sizeof(std::int32_t))},
        {"validation", resources.Validation, BufferBytes(layout.ValidationElementCount, sizeof(std::uint32_t))},
        {"active indices", resources.ActiveIndices, BufferBytes(layout.IndexElementCount, sizeof(std::uint32_t))},
        {"visible indices", resources.VisibleIndices, BufferBytes(layout.IndexElementCount, sizeof(std::uint32_t))},
        {"modified indices", resources.ModifiedIndices, BufferBytes(layout.IndexElementCount, sizeof(std::uint32_t))},
        {"topology dispatch commands", resources.TopologyDispatchCommands, BufferBytes(layout.TopologyDispatchElementCount, sizeof(std::uint32_t))},
        {"draw state", resources.IndirectDrawState, BufferBytes(layout.DrawStateElementCount, sizeof(std::uint32_t))},
        {"geometry dispatch commands", resources.GeometryDispatchCommands, BufferBytes(layout.GeometryDispatchElementCount, sizeof(std::uint32_t))},
        {"base control points", resources.BaseControlPoints, BufferBytes(CbtBaseControlPointCount, sizeof(CbtBaseControlPoint))},
    };
}

bool CreateTopologyResources(
    ID3D12Device* device,
    const CbtBaseTopology& topology,
    CbtTopologyResources& resources,
    std::string* errorMessage)
{
    // 一次 Rebuild 创建完整资源代，不在容量切换时保留旧任务或索引缓冲
    // 全部资源先进入 COPY_DEST，初值上传结束后再统一转为 UAV
    const CbtTopologyBufferLayout& layout = topology.Layout;
    return CreateDefaultBuffer(device, "occupancy tree", BufferBytes(layout.Occupancy.TreeSlotCount, sizeof(std::uint32_t)), resources.OccupancyTree, errorMessage) &&
           CreateDefaultBuffer(device, "occupancy bitfield", BufferBytes(layout.Occupancy.BitfieldSlotCount, sizeof(std::uint64_t)), resources.OccupancyBitfield, errorMessage) &&
           CreateDefaultBuffer(device, "heap IDs", BufferBytes(layout.TotalElementCount, sizeof(std::uint64_t)), resources.HeapIds, errorMessage) &&
           CreateDefaultBuffer(device, "neighbors 0", BufferBytes(layout.TotalElementCount, sizeof(CbtBisectorNeighbors)), resources.Neighbors[0], errorMessage) &&
           CreateDefaultBuffer(device, "neighbors 1", BufferBytes(layout.TotalElementCount, sizeof(CbtBisectorNeighbors)), resources.Neighbors[1], errorMessage) &&
           CreateDefaultBuffer(device, "bisector data", BufferBytes(layout.TotalElementCount, sizeof(CbtBisectorData)), resources.BisectorData, errorMessage) &&
           CreateDefaultBuffer(device, "classification", BufferBytes(layout.ClassificationElementCount, sizeof(std::uint32_t)), resources.Classification, errorMessage) &&
           CreateDefaultBuffer(device, "simplification", BufferBytes(layout.SimplificationElementCount, sizeof(std::uint32_t)), resources.Simplification, errorMessage) &&
           CreateDefaultBuffer(device, "allocation", BufferBytes(layout.AllocationElementCount, sizeof(std::uint32_t)), resources.Allocation, errorMessage) &&
           CreateDefaultBuffer(device, "propagation", BufferBytes(layout.PropagationElementCount, sizeof(std::uint32_t)), resources.Propagation, errorMessage) &&
           CreateDefaultBuffer(device, "memory", BufferBytes(layout.MemoryElementCount, sizeof(std::int32_t)), resources.Memory, errorMessage) &&
           CreateDefaultBuffer(device, "validation", BufferBytes(layout.ValidationElementCount, sizeof(std::uint32_t)), resources.Validation, errorMessage) &&
           CreateDefaultBuffer(device, "active indices", BufferBytes(layout.IndexElementCount, sizeof(std::uint32_t)), resources.ActiveIndices, errorMessage) &&
           CreateDefaultBuffer(device, "visible indices", BufferBytes(layout.IndexElementCount, sizeof(std::uint32_t)), resources.VisibleIndices, errorMessage) &&
           CreateDefaultBuffer(device, "modified indices", BufferBytes(layout.IndexElementCount, sizeof(std::uint32_t)), resources.ModifiedIndices, errorMessage) &&
           CreateDefaultBuffer(device, "topology dispatch commands", BufferBytes(layout.TopologyDispatchElementCount, sizeof(std::uint32_t)), resources.TopologyDispatchCommands, errorMessage) &&
           CreateDefaultBuffer(device, "draw state", BufferBytes(layout.DrawStateElementCount, sizeof(std::uint32_t)), resources.IndirectDrawState, errorMessage) &&
           CreateDefaultBuffer(device, "geometry dispatch commands", BufferBytes(layout.GeometryDispatchElementCount, sizeof(std::uint32_t)), resources.GeometryDispatchCommands, errorMessage) &&
           CreateDefaultBuffer(device, "base control points", BufferBytes(CbtBaseControlPointCount, sizeof(CbtBaseControlPoint)), resources.BaseControlPoints, errorMessage);
}

template<typename T, std::size_t Count>
std::size_t AppendPayload(std::vector<std::uint8_t>& payload, const std::array<T, Count>& values)
{
    // 每段按自身对齐要求放置，CopyBufferRegion 可以直接使用计算后的字节偏移
    const std::size_t offset = AlignUp(payload.size(), alignof(T));
    payload.resize(offset + sizeof(values));
    std::memcpy(payload.data() + offset, values.data(), sizeof(values));
    return offset;
}

template<typename T>
std::size_t AppendPayloadValue(std::vector<std::uint8_t>& payload, const T& value)
{
    const std::size_t offset = AlignUp(payload.size(), alignof(T));
    payload.resize(offset + sizeof(T));
    std::memcpy(payload.data() + offset, &value, sizeof(T));
    return offset;
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

bool UploadInitialState(
    Render::D3D12GraphicsBackend& backend,
    const CbtBaseTopology& topology,
    CbtTopologyResources& resources,
    std::string* errorMessage)
{
    // 最大资源决定可复用零区长度，其余资源只复制各自有效前缀
    // 这样 1M 配置也不需要为十余个空缓冲分别分配上传资源
    const std::vector<ResourceRecord> records = BuildResourceRecords(topology, BuildResourceView(resources));
    const auto largest = std::max_element(
        records.begin(),
        records.end(),
        [](const ResourceRecord& lhs, const ResourceRecord& rhs) { return lhs.Bytes < rhs.Bytes; });
    const std::size_t zeroRegionBytes = AlignUp(largest->Bytes, 16U);

    // 非零数据只包含六个基础二分器及初始命令，大小不随动态容量增长
    // payload 偏移相对尾部计算，前方零区可以被所有目标缓冲重复引用
    std::vector<std::uint8_t> payload;
    const std::size_t heapOffset = AppendPayload(payload, topology.HeapIds);
    const std::size_t neighborOffset = AppendPayload(payload, topology.Neighbors);
    const std::size_t bisectorDataOffset = AppendPayload(payload, topology.BisectorData);
    const std::size_t activeOffset = AppendPayload(payload, topology.ActiveIndices);
    const std::size_t visibleOffset = AppendPayload(payload, topology.VisibleIndices);
    const std::size_t drawOffset = AppendPayloadValue(payload, topology.IndirectDrawState);
    const std::size_t dispatchOffset = AppendPayload(payload, topology.GeometryDispatchCommands);
    const std::size_t controlPointOffset = AppendPayload(payload, topology.ControlPoints);
    const std::array<std::int32_t, 2> memoryState{
        0,
        static_cast<std::int32_t>(topology.Layout.DynamicElementCount),
    };
    const std::array<std::uint32_t, 2> validationState{0U, InvalidCbtBisectorIndex};
    const std::size_t memoryOffset = AppendPayload(payload, memoryState);
    const std::size_t validationOffset = AppendPayload(payload, validationState);

    ComPtr<ID3D12Resource> upload;
    if (!CreateUploadBuffer(backend.Device(), zeroRegionBytes + payload.size(), upload, errorMessage))
    {
        return false;
    }

    const D3D12_RANGE noRead{0U, 0U};
    void* mapped = nullptr;
    if (FAILED(upload->Map(0U, &noRead, &mapped)))
    {
        SetError(errorMessage, "Failed to map CBT base topology upload buffer");
        return false;
    }
    std::memset(mapped, 0, zeroRegionBytes);
    std::memcpy(static_cast<std::uint8_t*>(mapped) + zeroRegionBytes, payload.data(), payload.size());
    upload->Unmap(0U, nullptr);

    // 动态槽位范围是 [0, baseOffset)，基础半边固定写入容量尾部
    // OCBT 位域只覆盖动态范围，因此基础网格始终存在且不占分裂预算
    const std::size_t baseOffset = topology.Layout.BaseElementOffset;
    const auto recordInitialization = [&](ID3D12GraphicsCommandList* commandList, std::string*) {
                // 先完整清零，保证容量切换不会继承上一代任务计数或动态槽位
                // 邻接缓冲的动态前缀按官方实现保持零值，只有已分配槽位才解释其内容
                for (const ResourceRecord& record : records)
                {
                    commandList->CopyBufferRegion(record.Resource, 0U, upload.Get(), 0U, record.Bytes);
                }

                commandList->CopyBufferRegion(
                    resources.HeapIds.Get(),
                    baseOffset * sizeof(std::uint64_t),
                    upload.Get(),
                    zeroRegionBytes + heapOffset,
                    sizeof(topology.HeapIds));
                for (ComPtr<ID3D12Resource>& neighbors : resources.Neighbors)
                {
                    // 两份邻接以相同根状态开始，split/merge 提交时再交替读写
                    commandList->CopyBufferRegion(
                        neighbors.Get(),
                        baseOffset * sizeof(CbtBisectorNeighbors),
                        upload.Get(),
                        zeroRegionBytes + neighborOffset,
                        sizeof(topology.Neighbors));
                }
                commandList->CopyBufferRegion(
                    resources.BisectorData.Get(),
                    baseOffset * sizeof(CbtBisectorData),
                    upload.Get(),
                    zeroRegionBytes + bisectorDataOffset,
                    sizeof(topology.BisectorData));
                commandList->CopyBufferRegion(resources.ActiveIndices.Get(), 0U, upload.Get(), zeroRegionBytes + activeOffset, sizeof(topology.ActiveIndices));
                commandList->CopyBufferRegion(resources.VisibleIndices.Get(), 0U, upload.Get(), zeroRegionBytes + visibleOffset, sizeof(topology.VisibleIndices));
                commandList->CopyBufferRegion(resources.ModifiedIndices.Get(), 0U, upload.Get(), zeroRegionBytes + activeOffset, sizeof(topology.ActiveIndices));
                commandList->CopyBufferRegion(resources.IndirectDrawState.Get(), 0U, upload.Get(), zeroRegionBytes + drawOffset, sizeof(topology.IndirectDrawState));
                commandList->CopyBufferRegion(resources.GeometryDispatchCommands.Get(), 0U, upload.Get(), zeroRegionBytes + dispatchOffset, sizeof(topology.GeometryDispatchCommands));
                commandList->CopyBufferRegion(resources.BaseControlPoints.Get(), 0U, upload.Get(), zeroRegionBytes + controlPointOffset, sizeof(topology.ControlPoints));
                commandList->CopyBufferRegion(resources.Memory.Get(), 0U, upload.Get(), zeroRegionBytes + memoryOffset, sizeof(memoryState));
                commandList->CopyBufferRegion(resources.Validation.Get(), 0U, upload.Get(), zeroRegionBytes + validationOffset, sizeof(validationState));

                // 初始上传完成后所有缓冲保持 UAV 状态
                // 间接命令真正消费前由对应 pass 转为 INDIRECT_ARGUMENT
                for (const ResourceRecord& record : records)
                {
                    Transition(
                        commandList,
                        record.Resource,
                        D3D12_RESOURCE_STATE_COPY_DEST,
                        D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
                }
                return true;
            };

    if (backend.FrameOpen())
    {
        // 首次选择 CBT 时资源会在已打开帧中创建；上传 COM 引用随资源代保留到销毁
        if (backend.CommandList() == nullptr || !recordInitialization(backend.CommandList(), errorMessage))
        {
            SetError(errorMessage, "CBT base topology frame command list is unavailable");
            return false;
        }
        resources.InitializationUpload = std::move(upload);
        return true;
    }

    if (!backend.ExecuteImmediate(recordInitialization, errorMessage))
    {
        return false;
    }
    return true;
}

template<typename T>
void AddExpectedSlice(
    std::vector<ReadbackSlice>& slices,
    const char* name,
    ID3D12Resource* source,
    std::size_t sourceOffset,
    const T* values,
    std::size_t count)
{
    ReadbackSlice slice{};
    slice.Name = name;
    slice.Source = source;
    slice.SourceOffset = sourceOffset;
    slice.Expected.resize(sizeof(T) * count);
    std::memcpy(slice.Expected.data(), values, slice.Expected.size());
    slices.push_back(std::move(slice));
}

void AddZeroSlice(
    std::vector<ReadbackSlice>& slices,
    const char* name,
    ID3D12Resource* source,
    std::size_t sourceOffset,
    std::size_t bytes)
{
    // 空缓冲只抽查协议关键位置，避免验证器把整套 1M 状态复制回 CPU
    ReadbackSlice slice{};
    slice.Name = name;
    slice.Source = source;
    slice.SourceOffset = sourceOffset;
    slice.Expected.resize(bytes, 0U);
    slices.push_back(std::move(slice));
}

bool ValidateResourceSizes(
    const CbtBaseTopology& topology,
    const D3D12CbtGpuResourceView& resources,
    std::string* errorMessage)
{
    // 资源宽度本身也是协议的一部分，错误 stride 即使初始片段正确也不能通过
    for (const ResourceRecord& record : BuildResourceRecords(topology, resources))
    {
        if (record.Resource == nullptr || record.Resource->GetDesc().Width != record.Bytes)
        {
            SetError(errorMessage, std::string{"CBT base topology resource size mismatch: "} + record.Name);
            return false;
        }
    }
    return true;
}

bool ValidateGpuInitialState(
    Render::D3D12GraphicsBackend& backend,
    const CbtBaseTopology& topology,
    const D3D12CbtGpuResourceView& resources,
    std::string* errorMessage)
{
    if (!ValidateResourceSizes(topology, resources, errorMessage))
    {
        return false;
    }

    const CbtTopologyBufferLayout& layout = topology.Layout;
    std::vector<ReadbackSlice> slices;

    // OCBT 初始为空，首尾位域同时检查可覆盖错误的容量字节数和清零范围
    AddZeroSlice(slices, "occupancy tree", resources.OccupancyTree, 0U, sizeof(std::uint32_t));
    AddZeroSlice(slices, "occupancy bitfield head", resources.OccupancyBitfield, 0U, sizeof(std::uint64_t));
    AddZeroSlice(slices, "occupancy bitfield tail", resources.OccupancyBitfield, BufferBytes(layout.Occupancy.BitfieldSlotCount - 1U, sizeof(std::uint64_t)), sizeof(std::uint64_t));
    AddZeroSlice(slices, "dynamic heap IDs", resources.HeapIds, 0U, sizeof(std::uint64_t) * 2U);
    AddExpectedSlice(slices, "base heap IDs", resources.HeapIds, BufferBytes(layout.BaseElementOffset, sizeof(std::uint64_t)), topology.HeapIds.data(), topology.HeapIds.size());

    // 邻接双缓冲既要保持动态前缀为空，也要拥有完全相同的基础半边尾部
    // 共享对角线和四条边界的精确值由 CPU 拓扑真值提供
    AddZeroSlice(slices, "dynamic neighbors 0", resources.Neighbors[0], 0U, sizeof(CbtBisectorNeighbors) * 2U);
    AddExpectedSlice(slices, "base neighbors 0", resources.Neighbors[0], BufferBytes(layout.BaseElementOffset, sizeof(CbtBisectorNeighbors)), topology.Neighbors.data(), topology.Neighbors.size());
    AddZeroSlice(slices, "dynamic neighbors 1", resources.Neighbors[1], 0U, sizeof(CbtBisectorNeighbors) * 2U);
    AddExpectedSlice(slices, "base neighbors 1", resources.Neighbors[1], BufferBytes(layout.BaseElementOffset, sizeof(CbtBisectorNeighbors)), topology.Neighbors.data(), topology.Neighbors.size());
    AddZeroSlice(slices, "dynamic bisector data", resources.BisectorData, 0U, sizeof(CbtBisectorData));
    AddExpectedSlice(slices, "base bisector data", resources.BisectorData, BufferBytes(layout.BaseElementOffset, sizeof(CbtBisectorData)), topology.BisectorData.data(), topology.BisectorData.size());

    // 任务缓冲头部是后续 append 游标和分类计数，重建后必须全部归零
    // 验证头部比抽查列表主体更能发现旧资源代残留
    AddZeroSlice(slices, "classification headers", resources.Classification, 0U, sizeof(std::uint32_t) * 2U);
    AddZeroSlice(slices, "simplification header", resources.Simplification, 0U, sizeof(std::uint32_t));
    AddZeroSlice(slices, "allocation header", resources.Allocation, 0U, sizeof(std::uint32_t));
    AddZeroSlice(slices, "propagation headers", resources.Propagation, 0U, sizeof(std::uint32_t) * 2U);
    const std::array<std::int32_t, 2> memoryState{0, static_cast<std::int32_t>(layout.DynamicElementCount)};
    const std::array<std::uint32_t, 2> validationState{0U, InvalidCbtBisectorIndex};
    AddExpectedSlice(slices, "memory", resources.Memory, 0U, memoryState.data(), memoryState.size());
    AddExpectedSlice(slices, "validation", resources.Validation, 0U, validationState.data(), validationState.size());

    // 活动和可见列表初始都包含六个基础槽位，修改列表为空
    // 绘制参数因此输出十八个顶点，几何调度只需要一个工作组
    AddExpectedSlice(slices, "active indices", resources.ActiveIndices, 0U, topology.ActiveIndices.data(), topology.ActiveIndices.size());
    AddExpectedSlice(slices, "visible indices", resources.VisibleIndices, 0U, topology.VisibleIndices.data(), topology.VisibleIndices.size());
    AddExpectedSlice(slices, "modified indices", resources.ModifiedIndices, 0U, topology.ActiveIndices.data(), topology.ActiveIndices.size());
    AddZeroSlice(slices, "topology dispatch commands", resources.TopologyDispatchCommands, 0U, sizeof(std::uint32_t) * CbtIndirectDispatchWordCount);
    AddExpectedSlice(slices, "draw state", resources.IndirectDrawState, 0U, &topology.IndirectDrawState, 1U);
    AddExpectedSlice(slices, "geometry dispatch commands", resources.GeometryDispatchCommands, 0U, topology.GeometryDispatchCommands.data(), topology.GeometryDispatchCommands.size());
    AddExpectedSlice(slices, "base control points", resources.BaseControlPoints, 0U, topology.ControlPoints.data(), topology.ControlPoints.size());

    std::size_t readbackBytes = 0U;
    std::vector<ID3D12Resource*> transitioned;
    for (ReadbackSlice& slice : slices)
    {
        // 片段按八字节对齐打包到单个 readback，资源去重后只做一次状态往返
        slice.ReadbackOffset = AlignUp(readbackBytes, 8U);
        readbackBytes = slice.ReadbackOffset + slice.Expected.size();
        if (std::find(transitioned.begin(), transitioned.end(), slice.Source) == transitioned.end())
        {
            transitioned.push_back(slice.Source);
        }
    }

    ComPtr<ID3D12Resource> readback;
    if (!CreateReadbackBuffer(backend.Device(), readbackBytes, readback, errorMessage))
    {
        return false;
    }
    if (!backend.ExecuteImmediate(
            [&](ID3D12GraphicsCommandList* commandList, std::string*) {
                // 每个资源只转换一次，随后可复制多个不连续片段
                // ExecuteImmediate 返回前等待 fence，映射时不再需要额外 CPU 同步
                for (ID3D12Resource* resource : transitioned)
                {
                    Transition(commandList, resource, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COPY_SOURCE);
                }
                for (const ReadbackSlice& slice : slices)
                {
                    commandList->CopyBufferRegion(
                        readback.Get(),
                        slice.ReadbackOffset,
                        slice.Source,
                        slice.SourceOffset,
                        slice.Expected.size());
                }
                for (ID3D12Resource* resource : transitioned)
                {
                    Transition(commandList, resource, D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
                }
                return true;
            },
            errorMessage))
    {
        return false;
    }

    const D3D12_RANGE readRange{0U, readbackBytes};
    void* mapped = nullptr;
    if (FAILED(readback->Map(0U, &readRange, &mapped)))
    {
        SetError(errorMessage, "Failed to map CBT base topology readback buffer");
        return false;
    }
    const auto* bytes = static_cast<const std::uint8_t*>(mapped);
    for (const ReadbackSlice& slice : slices)
    {
        // 按片段比较可以把失败直接归因到具体协议缓冲
        // 不比较对齐填充，因为 readback 的未写区域没有初始化保证
        if (std::memcmp(bytes + slice.ReadbackOffset, slice.Expected.data(), slice.Expected.size()) != 0)
        {
            const D3D12_RANGE noWrite{0U, 0U};
            readback->Unmap(0U, &noWrite);
            SetError(errorMessage, std::string{"CBT base topology GPU initial state mismatch: "} + slice.Name);
            return false;
        }
    }
    const D3D12_RANGE noWrite{0U, 0U};
    readback->Unmap(0U, &noWrite);
    return true;
}
} // namespace

struct D3D12CbtGpuState::Impl
{
    CbtBaseTopology Topology{};
    CbtTopologyResources Resources{};
    std::uint64_t Generation{0U};
    bool Initialized{false};
};

D3D12CbtGpuState::D3D12CbtGpuState()
    : _impl(std::make_unique<Impl>())
{
}

D3D12CbtGpuState::~D3D12CbtGpuState() = default;

bool D3D12CbtGpuState::Rebuild(
    Render::D3D12GraphicsBackend& backend,
    CbtOccupancyCapacity capacity,
    std::string* errorMessage)
{
    // 已发布资源代不能在帧内替换；首次资源代允许直接记录到当前帧命令列表
    if (backend.FrameOpen() && _impl->Initialized)
    {
        SetError(errorMessage, "Initialized CBT topology resources can only be rebuilt outside a D3D12 frame");
        return false;
    }

    CbtBaseTopology topology = BuildSquareCbtBaseTopology(capacity);
    if (!ValidateCbtBaseTopology(topology, errorMessage))
    {
        return false;
    }

    // 旧资源可能仍被上一帧引用，完成围栏后再构建并替换资源代
    // 新资源全部成功前不修改当前状态，使分配失败仍可保留上一代配置
    if (!backend.FrameOpen())
    {
        backend.WaitForGpuIdle();
    }
    CbtTopologyResources resources{};
    if (!CreateTopologyResources(backend.Device(), topology, resources, errorMessage) ||
        !UploadInitialState(backend, topology, resources, errorMessage))
    {
        return false;
    }

    _impl->Topology = std::move(topology);
    _impl->Resources = std::move(resources);

    // Generation 只统计成功替换，测试用它确认容量往返确实发生完整重建
    ++_impl->Generation;
    _impl->Initialized = true;
    if (errorMessage != nullptr)
    {
        errorMessage->clear();
    }
    return true;
}

bool D3D12CbtGpuState::Reset(
    Render::D3D12GraphicsBackend& backend,
    std::string* errorMessage)
{
    // Reset 与 Rebuild 使用相同的帧外约束，防止释放仍被命令列表引用的资源
    if (backend.FrameOpen())
    {
        SetError(errorMessage, "CBT base topology resources can only be reset outside a D3D12 frame");
        return false;
    }

    backend.WaitForGpuIdle();
    _impl->Resources = {};
    _impl->Topology = {};
    _impl->Initialized = false;
    if (errorMessage != nullptr)
    {
        errorMessage->clear();
    }
    return true;
}

bool D3D12CbtGpuState::IsInitialized() const
{
    return _impl->Initialized;
}

std::uint64_t D3D12CbtGpuState::Generation() const
{
    return _impl->Generation;
}

const CbtBaseTopology& D3D12CbtGpuState::Topology() const
{
    return _impl->Topology;
}

D3D12CbtGpuResourceView D3D12CbtGpuState::Resources() const
{
    return BuildResourceView(_impl->Resources);
}

bool RunD3D12CbtBaseTopologySmokeTest(
    Render::D3D12GraphicsBackend& backend,
    std::string* errorMessage)
{
    // 资源状态依赖 SM 6.6 和 64 位原子能力，保持与正式 CBT 路径相同门槛
    const Cbt2024Availability availability = QueryCbt2024Availability(backend);
    if (!availability.Available)
    {
        SetError(errorMessage, availability.UnavailableReason);
        return false;
    }

    D3D12CbtGpuState state;
    const std::array<CbtOccupancyCapacity, 5> capacities{
        CbtOccupancyCapacity::Capacity128K,
        CbtOccupancyCapacity::Capacity256K,
        CbtOccupancyCapacity::Capacity512K,
        CbtOccupancyCapacity::Capacity1M,
        CbtOccupancyCapacity::Capacity128K,
    };
    // 最后回到 128K，专门覆盖大容量资源释放后的小容量重建
    for (const CbtOccupancyCapacity capacity : capacities)
    {
        if (!state.Rebuild(backend, capacity, errorMessage) ||
            !ValidateGpuInitialState(backend, state.Topology(), state.Resources(), errorMessage))
        {
            return false;
        }
        std::cout << "CBT base topology " << CbtOccupancyCapacityName(capacity)
                  << ": total-elements=" << state.Topology().Layout.TotalElementCount
                  << " generation=" << state.Generation() << '\n';
    }

    if (!state.Reset(backend, errorMessage) || state.IsInitialized())
    {
        SetError(errorMessage, "CBT base topology reset did not release the active resource generation");
        return false;
    }
    std::cout << "CBT base topology GPU validation passed for rebuild and capacity switching\n";
    return true;
}
} // namespace ParallelRoam::Algorithms::Cbt2024::D3D12
