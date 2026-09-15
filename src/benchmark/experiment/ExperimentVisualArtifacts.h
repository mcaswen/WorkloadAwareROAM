#pragma once

#include "render/FrameCapture.h"
#include <filesystem>

namespace ParallelRoam::Benchmark::Experiment
{
/// <summary>
/// 输出实际帧像素；图像编码不进入渲染器或计时窗口
/// 轻量PPM作为无依赖原件，报告层可另转PNG
/// </summary>
void WriteCapturedFrame(const std::filesystem::path& path, const Render::FrameCapture& capture);
}
