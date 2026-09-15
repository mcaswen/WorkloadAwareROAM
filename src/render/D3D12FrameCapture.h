#pragma once
#include "render/FrameCapture.h"
#include <d3d12.h>
#include <wrl/client.h>

namespace ParallelRoam::Render
{
/// <summary>
/// 持有一次诊断回读的资源与行距
/// 后端负责提交和等待，完成后才能读取CPU像素
/// </summary>
class D3D12FrameCapture
{
public:
    void Record(ID3D12Device* device, ID3D12GraphicsCommandList* list, ID3D12Resource* target);
    [[nodiscard]] FrameCapture Read(std::uint64_t id);
private:
    Microsoft::WRL::ComPtr<ID3D12Resource> _buffer;
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT _footprint{};
    int _width{}, _height{};
};
}
