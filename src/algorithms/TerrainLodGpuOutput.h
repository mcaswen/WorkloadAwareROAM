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

    [[nodiscard]] bool IsEmpty() const
    {
        return *this == TerrainLodGpuOutput{};
    }

    /// <summary>
    /// 只检查借用描述的布局，资源存活与实际 GPU 数量由所有者和同步保证
    /// </summary>
    [[nodiscard]] bool HasConsistentResourceContract() const
    {
        if (NativeResourceApi != TerrainLodNativeResourceApi::Direct3D12 ||
            GpuResourceLifetime != TerrainLodGpuResourceLifetime::UntilNextBuildOrReset ||
            GpuResourceGeneration == 0U || TopologyGeneration == 0U ||
            NativeVertexBuffer == 0U || NativeActiveLeafBuffer == 0U ||
            NativeLodStateBuffer == 0U || NativeIndirectDrawBuffer == 0U)
        {
            return false;
        }

        // 结构化视图不能包含半个元素；间接 DRAW 固定为四个 uint32
        const auto validBuffer = [](std::size_t bytes, std::size_t stride) {
            return stride > 0U && bytes >= stride && bytes % stride == 0U;
        };
        constexpr std::size_t drawBytes = 4U * sizeof(std::uint32_t);
        return validBuffer(GpuVertexBufferCapacityBytes, GpuVertexStrideBytes) &&
            validBuffer(GpuActiveLeafBufferCapacityBytes, GpuActiveLeafStrideBytes) &&
            validBuffer(GpuLodStateBufferCapacityBytes, GpuLodStateStrideBytes) &&
            GpuIndirectDrawArgumentOffsetBytes % sizeof(std::uint32_t) == 0U &&
            GpuIndirectDrawArgumentOffsetBytes <= GpuIndirectDrawBufferCapacityBytes &&
            drawBytes <= GpuIndirectDrawBufferCapacityBytes - GpuIndirectDrawArgumentOffsetBytes;
    }

    bool operator==(const TerrainLodGpuOutput&) const = default;
};
} // 命名空间 ParallelRoam::Algorithms
