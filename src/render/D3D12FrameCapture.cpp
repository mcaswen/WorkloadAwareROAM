#include "render/D3D12FrameCapture.h"
#include <cstring>
#include <utility>
#include <stdexcept>

namespace ParallelRoam::Render
{
void D3D12FrameCapture::Record(ID3D12Device* device, ID3D12GraphicsCommandList* list, ID3D12Resource* target)
{
    const auto texture = target->GetDesc();
    if (texture.Format != DXGI_FORMAT_R8G8B8A8_UNORM || texture.SampleDesc.Count != 1)
        throw std::runtime_error("帧捕获只支持非多采样RGBA8交换链");
    _width = static_cast<int>(texture.Width);
    _height = static_cast<int>(texture.Height);
    UINT64 bytes{};
    device->GetCopyableFootprints(&texture, 0, 1, 0, &_footprint, nullptr, nullptr, &bytes);
    D3D12_HEAP_PROPERTIES heap{};
    heap.Type = D3D12_HEAP_TYPE_READBACK;
    heap.CreationNodeMask = heap.VisibleNodeMask = 1;
    D3D12_RESOURCE_DESC description{};
    description.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    description.Width = bytes;
    description.Height = description.DepthOrArraySize = description.MipLevels = 1;
    description.SampleDesc.Count = 1;
    description.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    if (FAILED(device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &description,
        D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&_buffer))))
        throw std::runtime_error("无法分配截图回读缓冲");
    D3D12_RESOURCE_BARRIER barrier{};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition.pResource = target;
    barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
    barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_SOURCE;
    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    // 只在诊断帧转换颜色附件；返回后仍由后端执行正常Present转换
    list->ResourceBarrier(1, &barrier);
    D3D12_TEXTURE_COPY_LOCATION source{}, destination{};
    source.pResource = target;
    source.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    destination.pResource = _buffer.Get();
    destination.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
    destination.PlacedFootprint = _footprint;
    list->CopyTextureRegion(&destination, 0, 0, 0, &source, nullptr);
    std::swap(barrier.Transition.StateBefore, barrier.Transition.StateAfter);
    list->ResourceBarrier(1, &barrier);
}
FrameCapture D3D12FrameCapture::Read(std::uint64_t id)
{
    FrameCapture result;
    result.Id = id;
    result.Width = _width;
    result.Height = _height;
    const auto stride = static_cast<std::size_t>(_width) * 4;
    result.Rgba.resize(stride * static_cast<std::size_t>(_height));
    void* mapped{};
    const D3D12_RANGE read{static_cast<SIZE_T>(_footprint.Offset),
        static_cast<SIZE_T>(_footprint.Offset) + static_cast<SIZE_T>(_footprint.Footprint.RowPitch) * static_cast<SIZE_T>(_height)};
    if (!_buffer || FAILED(_buffer->Map(0, &read, &mapped))) throw std::runtime_error("截图映射失败");
    // 忽略GPU行尾填充，CPU契约保持紧密排列和自上而下的行方向
    for (int y = 0; y < _height; ++y)
        std::memcpy(result.Rgba.data() + static_cast<std::size_t>(y) * stride,
            static_cast<const std::uint8_t*>(mapped) + _footprint.Offset +
            static_cast<std::size_t>(y) * _footprint.Footprint.RowPitch, stride);
    const D3D12_RANGE written{0, 0};
    _buffer->Unmap(0, &written);
    _buffer.Reset();
    return result;
}
}
