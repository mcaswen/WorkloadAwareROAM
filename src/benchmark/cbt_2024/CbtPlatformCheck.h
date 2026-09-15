#pragma once

namespace ParallelRoam::Benchmark
{
/// <summary>
/// 在真实 D3D12 帧边界检查 CBT 更新、资源切换和借用输出，并保存有限截图
/// </summary>
int RunCbtPlatformCheck(int argc, char** argv);
}
