#pragma once

#include "algorithms/TerrainLodGpuOutput.h"
#include "terrain/TerrainMeshBuilder.h"

namespace ParallelRoam::Render
{
class D3D12GraphicsBackend;
}

namespace ParallelRoam::Benchmark::Experiment
{
/// <summary>
/// 同一已提交代的实际 GPU 几何；只由实验捕获持有，不回写算法
/// 字节数包括按容量复制的暂存，不能解释为正常输出传输
/// </summary>
struct CbtMeshCapture
{
    Terrain::TerrainMeshData Mesh;
    std::uint64_t ResourceGeneration{};
    std::uint64_t TopologyGeneration{};
    std::uint64_t ReadbackBytes{};
    double CaptureMilliseconds{};
};

/// <summary>
/// 仅在 Present 后、下一次 Build 前调用；等待同代资源并恢复其发布状态
/// 非法布局抛出异常，不用 CPU 重建结果替代实际几何
/// </summary>
[[nodiscard]] CbtMeshCapture CaptureCbtMesh(Render::D3D12GraphicsBackend& backend,
    const Algorithms::TerrainLodGpuOutput& output, float terrainSize, float heightScale);
}
