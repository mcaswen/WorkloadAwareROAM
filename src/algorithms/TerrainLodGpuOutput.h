#pragma once

#include <cstddef>
#include <cstdint>

namespace ParallelRoam::Algorithms
{
/// <summary>
/// 原生句柄的解释方式，公共声明不依赖图形 API 头文件
/// </summary>
enum class TerrainLodNativeResourceApi
{
    None,
    Direct3D12,
};

/// <summary>
/// GPU 输出只借用算法资源，下一次构建或重置后需重新取得描述
/// </summary>
enum class TerrainLodGpuResourceLifetime
{
    None,
    UntilNextBuildOrReset,
};

/// <summary>
/// 间接绘制所需的只读资源视图，算法保持所有权
/// 活动索引映射物理槽位，每个槽位保存三个结构化顶点
/// </summary>
struct TerrainLodGpuOutput
{
    TerrainLodNativeResourceApi NativeResourceApi{TerrainLodNativeResourceApi::None};
    std::uintptr_t NativeVertexBuffer{0U};
    std::uintptr_t NativeActiveLeafBuffer{0U};
    std::uintptr_t NativeLodStateBuffer{0U};
    std::uintptr_t NativeIndirectDrawBuffer{0U};
    std::size_t GpuVertexBufferCapacityBytes{0U};
    std::size_t GpuVertexStrideBytes{0U};
    std::size_t GpuActiveLeafBufferCapacityBytes{0U};
    std::size_t GpuActiveLeafStrideBytes{0U};
    std::size_t GpuLodStateBufferCapacityBytes{0U};
    std::size_t GpuLodStateStrideBytes{0U};
    std::size_t GpuIndirectDrawBufferCapacityBytes{0U};
    std::size_t GpuIndirectDrawArgumentOffsetBytes{0U};
    TerrainLodGpuResourceLifetime GpuResourceLifetime{TerrainLodGpuResourceLifetime::None};
    // 资源替换和算法更新是不同生命周期，不能共用一个版本号
    std::uint64_t GpuResourceGeneration{0U};
    std::uint64_t TopologyGeneration{0U};
};
} // 命名空间 ParallelRoam::Algorithms
