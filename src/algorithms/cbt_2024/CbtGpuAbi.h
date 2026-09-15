#pragma once

#include "algorithms/cbt_2024/CbtGpuAbi.shared.h"

#include <cstdint>

namespace ParallelRoam::Algorithms::Cbt2024
{
// C++ 侧只提供强类型别名；数值唯一来源是 DXC 同时包含的 shared header。
// GPU buffer 布局变化必须先修改共享 word index，再由 offsetof 断言验证结构体。
inline constexpr std::uint32_t InvalidCbtBisectorIndex = CBT_GPU_INVALID_INDEX;
inline constexpr std::uint32_t CbtBaseBisectorCount = CBT_GPU_BASE_BISECTOR_COUNT;
inline constexpr std::uint32_t CbtBaseDepth = CBT_GPU_BASE_DEPTH;
inline constexpr std::uint32_t CbtBaseControlPointCount = CbtBaseBisectorCount * 3U;

inline constexpr std::uint32_t CbtVisibleFlag = CBT_GPU_VISIBLE_FLAG;
// Flags 和 element state 分属不同字段，保持独立常量避免位值/枚举值误用。
inline constexpr std::uint32_t CbtModifiedFlag = CBT_GPU_MODIFIED_FLAG;
inline constexpr std::uint32_t CbtSplitEventFlag = CBT_GPU_SPLIT_EVENT_FLAG;
inline constexpr std::uint32_t CbtMergeEventFlag = CBT_GPU_MERGE_EVENT_FLAG;
inline constexpr std::uint32_t CbtDebugEventMask = CBT_GPU_DEBUG_EVENT_MASK;
inline constexpr std::uint32_t CbtDebugEventLifetimeMask =
    CBT_GPU_DEBUG_EVENT_LIFETIME_MASK;
inline constexpr std::uint32_t CbtDebugEventHoldFrames =
    CBT_GPU_DEBUG_EVENT_HOLD_FRAMES;
inline constexpr std::uint32_t CbtActiveDepthMask = CBT_GPU_ACTIVE_DEPTH_MASK;

// 调试元数据只占用 Flags 的高位，Visible/Modified 的执行语义保持不变。
// 事件类型和剩余帧数分开编码，分类阶段可以衰减寿命而不改写类型。
// 活动深度由 heap ID 刷新，程序化绘制无需依赖几何缓冲中的旧调色板。
// 编码函数采用饱和截断，防止诊断值越过共享 ABI 预留范围。
[[nodiscard]] inline constexpr std::uint32_t EncodeCbtDebugEventLifetime(
    std::uint32_t lifetime)
{
    const std::uint32_t maximum =
        CbtDebugEventLifetimeMask >> CBT_GPU_DEBUG_EVENT_LIFETIME_SHIFT;
    return ((lifetime < maximum ? lifetime : maximum) <<
            CBT_GPU_DEBUG_EVENT_LIFETIME_SHIFT) &
        CbtDebugEventLifetimeMask;
}

[[nodiscard]] inline constexpr std::uint32_t DecodeCbtDebugEventLifetime(
    std::uint32_t flags)
{
    return (flags & CbtDebugEventLifetimeMask) >>
        CBT_GPU_DEBUG_EVENT_LIFETIME_SHIFT;
}

[[nodiscard]] inline constexpr std::uint32_t EncodeCbtActiveDepth(
    std::uint32_t depth)
{
    const std::uint32_t maximum = CbtActiveDepthMask >> CBT_GPU_ACTIVE_DEPTH_SHIFT;
    return ((depth < maximum ? depth : maximum) << CBT_GPU_ACTIVE_DEPTH_SHIFT) &
        CbtActiveDepthMask;
}

[[nodiscard]] inline constexpr std::uint32_t DecodeCbtActiveDepth(
    std::uint32_t flags)
{
    return (flags & CbtActiveDepthMask) >> CBT_GPU_ACTIVE_DEPTH_SHIFT;
}

// 邻接传播也属于本帧拓扑变化；更新事件时保留可见性和活动深度等其余语义位。
[[nodiscard]] inline constexpr std::uint32_t WithCbtDebugEvent(
    std::uint32_t flags,
    std::uint32_t eventFlag)
{
    return (flags & ~(CbtDebugEventMask | CbtDebugEventLifetimeMask)) |
        CbtModifiedFlag | (eventFlag & CbtDebugEventMask) |
        EncodeCbtDebugEventLifetime(CbtDebugEventHoldFrames);
}
inline constexpr std::uint32_t CbtUnchangedElement = CBT_GPU_UNCHANGED_ELEMENT;
inline constexpr std::uint32_t CbtBisectElement = CBT_GPU_BISECT_ELEMENT;
inline constexpr std::uint32_t CbtSimplifyElement = CBT_GPU_SIMPLIFY_ELEMENT;
inline constexpr std::uint32_t CbtMergedElement = CBT_GPU_MERGED_ELEMENT;

inline constexpr std::uint32_t CbtNoSplitPattern = CBT_GPU_NO_SPLIT_PATTERN;
// pattern 是可组合位图；double/triple 值只由三个基础模板位派生。
inline constexpr std::uint32_t CbtCenterSplitPattern = CBT_GPU_CENTER_SPLIT_PATTERN;
inline constexpr std::uint32_t CbtRightSplitPattern = CBT_GPU_RIGHT_SPLIT_PATTERN;
inline constexpr std::uint32_t CbtLeftSplitPattern = CBT_GPU_LEFT_SPLIT_PATTERN;
inline constexpr std::uint32_t CbtRightDoubleSplitPattern =
    CbtCenterSplitPattern | CbtRightSplitPattern;
inline constexpr std::uint32_t CbtLeftDoubleSplitPattern =
    CbtCenterSplitPattern | CbtLeftSplitPattern;
inline constexpr std::uint32_t CbtTripleSplitPattern =
    CbtCenterSplitPattern | CbtRightSplitPattern | CbtLeftSplitPattern;

inline constexpr std::uint32_t CbtBisectorDataWordCount = CBT_GPU_BISECTOR_DATA_WORD_COUNT;
// word count 用于资源容量、readback 范围和跨语言静态布局检查。
inline constexpr std::uint32_t CbtDrawStateWordCount = CBT_GPU_DRAW_STATE_WORD_COUNT;
inline constexpr std::uint32_t CbtIndirectDispatchWordCount = CBT_GPU_GEOMETRY_DISPATCH_WORD_COUNT;
inline constexpr std::uint32_t CbtValidationMaxActiveDepthWord =
    CBT_GPU_VALIDATION_MAX_ACTIVE_DEPTH_WORD;
inline constexpr std::uint32_t CbtValidationWordCount = CBT_GPU_VALIDATION_WORD_COUNT;
} // namespace ParallelRoam::Algorithms::Cbt2024
