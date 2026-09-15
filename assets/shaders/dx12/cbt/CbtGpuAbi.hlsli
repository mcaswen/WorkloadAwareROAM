#ifndef PARALLEL_ROAM_CBT_GPU_ABI_HLSLI
#define PARALLEL_ROAM_CBT_GPU_ABI_HLSLI

#include "CbtGpuAbi.shared.h"

// DXC 侧把共享宏适配为 HLSL 常量，生产 shader 不直接散布数值字面量。
static const uint CbtInvalidIndex = CBT_GPU_INVALID_INDEX;
static const uint CbtVisibleFlag = CBT_GPU_VISIBLE_FLAG;
static const uint CbtModifiedFlag = CBT_GPU_MODIFIED_FLAG;
static const uint CbtSplitEventFlag = CBT_GPU_SPLIT_EVENT_FLAG;
static const uint CbtMergeEventFlag = CBT_GPU_MERGE_EVENT_FLAG;
static const uint CbtDebugEventMask = CBT_GPU_DEBUG_EVENT_MASK;
static const uint CbtDebugEventLifetimeMask = CBT_GPU_DEBUG_EVENT_LIFETIME_MASK;
static const uint CbtDebugEventHoldFrames = CBT_GPU_DEBUG_EVENT_HOLD_FRAMES;
static const uint CbtActiveDepthMask = CBT_GPU_ACTIVE_DEPTH_MASK;
static const uint CbtInitialDepth = CBT_GPU_BASE_DEPTH;
static const uint CbtUnchangedElement = CBT_GPU_UNCHANGED_ELEMENT;
static const uint CbtBisectElement = CBT_GPU_BISECT_ELEMENT;
static const uint CbtSimplifyElement = CBT_GPU_SIMPLIFY_ELEMENT;
static const uint CbtMergedElement = CBT_GPU_MERGED_ELEMENT;

// Event type and remaining lifetime occupy separate flag ranges.
// Classify decays the lifetime once per CBT topology update.
// A zero lifetime makes stale event bits invisible to the renderer.
// Active depth is refreshed from the heap ID during every classification.
// Saturating encoders keep malformed debug metadata inside the shared ABI.
uint CbtEncodeDebugEventLifetime(uint lifetime)
{
    const uint maximum = CbtDebugEventLifetimeMask >> CBT_GPU_DEBUG_EVENT_LIFETIME_SHIFT;
    return (min(lifetime, maximum) << CBT_GPU_DEBUG_EVENT_LIFETIME_SHIFT) &
        CbtDebugEventLifetimeMask;
}

uint CbtDecodeDebugEventLifetime(uint flags)
{
    return (flags & CbtDebugEventLifetimeMask) >> CBT_GPU_DEBUG_EVENT_LIFETIME_SHIFT;
}

uint CbtEncodeActiveDepth(uint depth)
{
    const uint maximum = CbtActiveDepthMask >> CBT_GPU_ACTIVE_DEPTH_SHIFT;
    return (min(depth, maximum) << CBT_GPU_ACTIVE_DEPTH_SHIFT) & CbtActiveDepthMask;
}

uint CbtDecodeActiveDepth(uint flags)
{
    return (flags & CbtActiveDepthMask) >> CBT_GPU_ACTIVE_DEPTH_SHIFT;
}

uint CbtDecayDebugEvent(uint flags)
{
    const uint lifetime = CbtDecodeDebugEventLifetime(flags);
    if (lifetime <= 1u)
    {
        return 0u;
    }
    return (flags & CbtDebugEventMask) | CbtEncodeDebugEventLifetime(lifetime - 1u);
}

static const uint CbtNoSplitPattern = CBT_GPU_NO_SPLIT_PATTERN;
// 派生模板保持位组合语义，与 CPU split planner 使用同一基础位。
static const uint CbtCenterSplitPattern = CBT_GPU_CENTER_SPLIT_PATTERN;
static const uint CbtRightSplitPattern = CBT_GPU_RIGHT_SPLIT_PATTERN;
static const uint CbtLeftSplitPattern = CBT_GPU_LEFT_SPLIT_PATTERN;
static const uint CbtRightDoubleSplitPattern =
    CBT_GPU_CENTER_SPLIT_PATTERN | CBT_GPU_RIGHT_SPLIT_PATTERN;
static const uint CbtLeftDoubleSplitPattern =
    CBT_GPU_CENTER_SPLIT_PATTERN | CBT_GPU_LEFT_SPLIT_PATTERN;
static const uint CbtTripleSplitPattern =
    CBT_GPU_CENTER_SPLIT_PATTERN | CBT_GPU_RIGHT_SPLIT_PATTERN | CBT_GPU_LEFT_SPLIT_PATTERN;

static const uint CbtDrawActiveVertexCountWord = CBT_GPU_DRAW_ACTIVE_VERTEX_COUNT_WORD;
// draw state word index 同时约束 ExecuteIndirect 参数和 compute 统计尾部。
static const uint CbtDrawActiveInstanceCountWord = CBT_GPU_DRAW_ACTIVE_INSTANCE_COUNT_WORD;
static const uint CbtDrawActiveStartVertexWord = CBT_GPU_DRAW_ACTIVE_START_VERTEX_WORD;
static const uint CbtDrawActiveStartInstanceWord = CBT_GPU_DRAW_ACTIVE_START_INSTANCE_WORD;
static const uint CbtDrawVisibleVertexCountWord = CBT_GPU_DRAW_VISIBLE_VERTEX_COUNT_WORD;
static const uint CbtDrawVisibleInstanceCountWord = CBT_GPU_DRAW_VISIBLE_INSTANCE_COUNT_WORD;
static const uint CbtDrawVisibleStartVertexWord = CBT_GPU_DRAW_VISIBLE_START_VERTEX_WORD;
static const uint CbtDrawVisibleStartInstanceWord = CBT_GPU_DRAW_VISIBLE_START_INSTANCE_WORD;
static const uint CbtDrawModifiedPositionCountWord = CBT_GPU_DRAW_MODIFIED_POSITION_COUNT_WORD;
static const uint CbtDrawActiveBisectorCountWord = CBT_GPU_DRAW_ACTIVE_BISECTOR_COUNT_WORD;
static const uint CbtActiveDispatchOffsetWord = CBT_GPU_ACTIVE_DISPATCH_OFFSET_WORD;
// 三条 dispatch 命令各占三个连续 uint，偏移可直接用于 ExecuteIndirect。
static const uint CbtActivePositionDispatchOffsetWord = CBT_GPU_ACTIVE_POSITION_DISPATCH_OFFSET_WORD;
static const uint CbtModifiedDispatchOffsetWord = CBT_GPU_MODIFIED_DISPATCH_OFFSET_WORD;
static const uint CbtValidationMaxActiveDepthWord = CBT_GPU_VALIDATION_MAX_ACTIVE_DEPTH_WORD;
static const uint CbtValidationWordCount = CBT_GPU_VALIDATION_WORD_COUNT;

struct CbtBisectorData
{
    // Field order is locked by C++ offsetof assertions against the shared word indices.
    uint SubdivisionPattern;
    uint3 Indices;
    uint ProblematicNeighbor;
    uint BisectorState;
    uint Flags;
    uint PropagationId;
};

#endif
