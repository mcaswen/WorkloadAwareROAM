#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace ParallelRoam::Render
{
/// <summary>
/// 从实际颜色附件回读的自有CPU图像，行顺序统一为从上到下
/// 字节保留帧缓冲颜色解释，不在捕获阶段改变gamma或色调
/// </summary>
struct FrameCapture
{
    std::uint64_t Id{};
    int Width{};
    int Height{};
    std::vector<std::uint8_t> Rgba;
    std::string ColorEncoding{"framebuffer-rgba8"};
};
}
