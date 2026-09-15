#pragma once

#include "render/FrameCapture.h"

namespace ParallelRoam::Render
{
/// <summary>
/// 只在当前OpenGL上下文的Present前执行同步诊断回读
/// 无请求时不调用；不保存或共享其他后端的资源
/// </summary>
[[nodiscard]] FrameCapture CaptureOpenGlBackBuffer(std::uint64_t id, int width, int height);
}
