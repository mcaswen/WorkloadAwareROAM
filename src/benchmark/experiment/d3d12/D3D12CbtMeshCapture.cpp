#include "benchmark/experiment/d3d12/D3D12CbtMeshCapture.h"

#include "algorithms/cbt_2024/CbtBisectorTopology.h"
#include "render/D3D12GraphicsBackend.h"

#include <array>
#include <chrono>
#include <cstring>
#include <stdexcept>
#include <vector>

namespace ParallelRoam::Benchmark::Experiment
{
namespace
{
void Require(bool condition, const char* message)
{
    if (!condition)
    {
        throw std::runtime_error(message);
    }
}

/// <summary>
/// 只描述本次复制的资源与原发布状态，捕获后原控制器的状态跟踪保持有效
/// </summary>
struct CopySource
{
    ID3D12Resource* Resource{};
    std::uint64_t Bytes{};
    std::uint64_t Offset{};
    D3D12_RESOURCE_STATES PublishedState{};
};
}

CbtMeshCapture CaptureCbtMesh(Render::D3D12GraphicsBackend& backend,
    const Algorithms::TerrainLodGpuOutput& output, float terrainSize, float heightScale)
{
    using namespace Algorithms::Cbt2024;
    const auto started = std::chrono::steady_clock::now();
    Require(!backend.FrameOpen(), "CBT capture requires a closed submitted frame");
    Require(output.HasConsistentResourceContract(), "Invalid CBT capture resource contract");
    Require(output.GpuVertexStrideBytes == sizeof(Terrain::TerrainMeshVertex) &&
        output.GpuActiveLeafStrideBytes == sizeof(std::uint32_t) &&
        output.GpuIndirectDrawArgumentOffsetBytes == offsetof(CbtDrawState, Active),
        "CBT capture ABI mismatch");

    std::array<CopySource, 3> sources{{
        {reinterpret_cast<ID3D12Resource*>(output.NativeIndirectDrawBuffer),
            sizeof(CbtDrawState), 0U, D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT},
        {reinterpret_cast<ID3D12Resource*>(output.NativeActiveLeafBuffer),
            output.GpuActiveLeafBufferCapacityBytes, 0U, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE},
        {reinterpret_cast<ID3D12Resource*>(output.NativeVertexBuffer),
            output.GpuVertexBufferCapacityBytes, 0U, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE},
    }};
    std::uint64_t bytes = 0U;
    for (auto& source : sources)
    {
        const auto description = source.Resource->GetDesc();
        Require(description.Dimension == D3D12_RESOURCE_DIMENSION_BUFFER &&
            description.Width >= source.Bytes, "CBT capture buffer capacity mismatch");
        source.Offset = bytes;
        bytes += source.Bytes;
    }
    Require(bytes <= 200ULL * 1024ULL * 1024ULL, "CBT capture exceeds diagnostic quota");

    // 一份暂存承接三类同代数据，等待与复制都只计入证据成本
    backend.WaitForGpuIdle();
    D3D12_HEAP_PROPERTIES heap{};
    heap.Type = D3D12_HEAP_TYPE_READBACK;
    heap.CreationNodeMask = 1U;
    heap.VisibleNodeMask = 1U;
    D3D12_RESOURCE_DESC description{};
    description.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    description.Width = bytes;
    description.Height = 1U;
    description.DepthOrArraySize = 1U;
    description.MipLevels = 1U;
    description.SampleDesc.Count = 1U;
    description.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    Microsoft::WRL::ComPtr<ID3D12Resource> readback;
    Require(SUCCEEDED(backend.Device()->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE,
        &description, D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&readback))),
        "Cannot allocate CBT capture readback");
    std::string error;
    const bool copied = backend.ExecuteImmediate([&](ID3D12GraphicsCommandList* commands, std::string*) {
        for (const auto& source : sources)
        {
            D3D12_RESOURCE_BARRIER barrier{};
            barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
            barrier.Transition.pResource = source.Resource;
            barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
            barrier.Transition.StateBefore = source.PublishedState;
            barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_SOURCE;
            commands->ResourceBarrier(1U, &barrier);
            commands->CopyBufferRegion(readback.Get(), source.Offset, source.Resource, 0U, source.Bytes);
            std::swap(barrier.Transition.StateBefore, barrier.Transition.StateAfter);
            commands->ResourceBarrier(1U, &barrier);
        }
        return true;
    }, &error);
    if (!copied)
    {
        throw std::runtime_error(error);
    }

    std::vector<std::byte> storage(static_cast<std::size_t>(bytes));
    void* mapped = nullptr;
    const D3D12_RANGE range{0U, static_cast<SIZE_T>(bytes)};
    Require(SUCCEEDED(readback->Map(0U, &range, &mapped)), "Cannot map CBT capture");
    std::memcpy(storage.data(), mapped, storage.size());
    const D3D12_RANGE noWrites{0U, 0U};
    readback->Unmap(0U, &noWrites);

    CbtDrawState draw{};
    std::memcpy(&draw, storage.data(), sizeof(draw));
    Require(draw.Active.InstanceCount == 1U && draw.Active.StartInstanceLocation == 0U &&
        draw.Active.StartVertexLocation == 0U && draw.Active.VertexCountPerInstance % 3U == 0U,
        "Unsupported CBT indirect draw layout");
    const std::size_t count = draw.Active.VertexCountPerInstance / 3U;
    const std::size_t slots = sources[2].Bytes / (3U * sizeof(Terrain::TerrainMeshVertex));
    Require(count > 0U && count <= sources[1].Bytes / sizeof(std::uint32_t) && count <= slots &&
        count == draw.ActiveBisectorCount, "CBT actual active count mismatch");

    CbtMeshCapture result;
    result.ResourceGeneration = output.GpuResourceGeneration;
    result.TopologyGeneration = output.TopologyGeneration;
    result.ReadbackBytes = bytes;
    auto& mesh = result.Mesh;
    mesh.TerrainSize = terrainSize;
    mesh.HeightScale = heightScale;
    mesh.Vertices.resize(count * 3U);
    mesh.Indices.resize(count * 3U);
    std::vector<bool> seen(slots, false);
    for (std::size_t active = 0U; active < count; ++active)
    {
        std::uint32_t slot = 0U;
        std::memcpy(&slot, storage.data() + sources[1].Offset + active * sizeof(slot), sizeof(slot));
        Require(slot < slots && !seen[slot], "CBT active list contains an invalid or repeated slot");
        seen[slot] = true;
        // 与程序化 VS 相同的物理寻址，只压紧诊断文件，不改实际顶点值
        std::memcpy(mesh.Vertices.data() + active * 3U,
            storage.data() + sources[2].Offset + slot * 3ULL * sizeof(Terrain::TerrainMeshVertex),
            3U * sizeof(Terrain::TerrainMeshVertex));
        for (std::size_t vertex = 0U; vertex < 3U; ++vertex)
        {
            mesh.Indices[active * 3U + vertex] = static_cast<std::uint32_t>(active * 3U + vertex);
        }
    }
    result.CaptureMilliseconds = std::chrono::duration<double, std::milli>(
        std::chrono::steady_clock::now() - started).count();
    return result;
}
}
