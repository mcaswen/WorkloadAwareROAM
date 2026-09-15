#include "algorithms/cbt_2024/CbtBisectorTopology.h"

#include <bit>
#include <cmath>
#include <cstddef>

namespace ParallelRoam::Algorithms::Cbt2024
{
namespace
{
constexpr std::uint32_t BaseDepth = CbtBaseDepth;
constexpr std::uint64_t FirstBaseHeapId = std::uint64_t{1U} << (BaseDepth - 1U);
constexpr std::uint32_t GeometryThreadGroupSize = 64U;

// 六个基础半边需要三位层内索引，官方 minimalDepth 因此为四
// 逻辑 heap ID 从 8 开始，与物理槽位位于动态容量尾部是两套独立寻址

bool SetValidationError(std::string* errorMessage, const char* message)
{
    if (errorMessage != nullptr)
    {
        *errorMessage = message;
    }
    return false;
}

bool NearlyEqual(float lhs, float rhs)
{
    return std::abs(lhs - rhs) <= 1.0e-6F;
}

bool SamePoint(const CbtBaseControlPoint& lhs, const CbtBaseControlPoint& rhs)
{
    return NearlyEqual(lhs.U, rhs.U) &&
           NearlyEqual(lhs.Height, rhs.Height) &&
           NearlyEqual(lhs.V, rhs.V);
}

std::uint32_t DispatchGroupCount(std::uint32_t elementCount)
{
    return (elementCount + GeometryThreadGroupSize - 1U) / GeometryThreadGroupSize;
}
} // namespace

static_assert(sizeof(CbtBisectorNeighbors) == 12U);
static_assert(sizeof(CbtBaseControlPoint) == 12U);
static_assert(sizeof(CbtBisectorData) == CbtBisectorDataWordCount * sizeof(std::uint32_t));
static_assert(offsetof(CbtBisectorData, SubdivisionPattern) ==
    CBT_GPU_BISECTOR_SUBDIVISION_PATTERN_WORD * sizeof(std::uint32_t));
static_assert(offsetof(CbtBisectorData, Indices) ==
    CBT_GPU_BISECTOR_INDICES_WORD * sizeof(std::uint32_t));
static_assert(offsetof(CbtBisectorData, ProblematicNeighbor) ==
    CBT_GPU_BISECTOR_PROBLEMATIC_NEIGHBOR_WORD * sizeof(std::uint32_t));
static_assert(offsetof(CbtBisectorData, BisectorState) ==
    CBT_GPU_BISECTOR_STATE_WORD * sizeof(std::uint32_t));
static_assert(offsetof(CbtBisectorData, Flags) ==
    CBT_GPU_BISECTOR_FLAGS_WORD * sizeof(std::uint32_t));
static_assert(offsetof(CbtBisectorData, PropagationId) ==
    CBT_GPU_BISECTOR_PROPAGATION_ID_WORD * sizeof(std::uint32_t));
// draw state 的前四个 uint 会被 ExecuteIndirect 直接解释为 D3D12_DRAW_ARGUMENTS。
// 后六个 uint 继续保持上游 visible draw、modified count 和 explicit active count 偏移。
static_assert(sizeof(CbtDrawArguments) == 16U);
static_assert(offsetof(CbtDrawArguments, VertexCountPerInstance) ==
    CBT_GPU_DRAW_ACTIVE_VERTEX_COUNT_WORD * sizeof(std::uint32_t));
static_assert(offsetof(CbtDrawArguments, InstanceCount) ==
    CBT_GPU_DRAW_ACTIVE_INSTANCE_COUNT_WORD * sizeof(std::uint32_t));
static_assert(offsetof(CbtDrawArguments, StartVertexLocation) ==
    CBT_GPU_DRAW_ACTIVE_START_VERTEX_WORD * sizeof(std::uint32_t));
static_assert(offsetof(CbtDrawArguments, StartInstanceLocation) ==
    CBT_GPU_DRAW_ACTIVE_START_INSTANCE_WORD * sizeof(std::uint32_t));
static_assert(sizeof(CbtDrawState) == CbtDrawStateWordCount * sizeof(std::uint32_t));
static_assert(offsetof(CbtDrawState, Active) == 0U);
static_assert(offsetof(CbtDrawState, Visible) ==
    CBT_GPU_DRAW_VISIBLE_VERTEX_COUNT_WORD * sizeof(std::uint32_t));
static_assert(offsetof(CbtDrawState, ModifiedPositionCount) ==
    CBT_GPU_DRAW_MODIFIED_POSITION_COUNT_WORD * sizeof(std::uint32_t));
static_assert(offsetof(CbtDrawState, ActiveBisectorCount) ==
    CBT_GPU_DRAW_ACTIVE_BISECTOR_COUNT_WORD * sizeof(std::uint32_t));
static_assert(sizeof(CbtDispatchArguments) ==
    CBT_GPU_DISPATCH_ARGUMENT_WORD_COUNT * sizeof(std::uint32_t));

CbtTopologyBufferLayout BuildCbtTopologyBufferLayout(CbtOccupancyCapacity capacity)
{
    CbtTopologyBufferLayout layout{};
    layout.Occupancy = BuildCbtOccupancyLayout(capacity);
    layout.DynamicElementCount = layout.Occupancy.ElementCount;
    layout.BaseElementOffset = layout.DynamicElementCount;
    layout.TotalElementCount = layout.DynamicElementCount + CbtBaseBisectorCount;

    // 任务缓冲的头部计数沿用官方布局，后续 pass 可以直接迁移索引写入协议
    // 分类同时保存 split 和 merge 列表，所以主体容量是总元素数的两倍
    // 其余任务每个物理槽位至多产生一个直接记录
    layout.ClassificationElementCount = 2U + 2U * layout.TotalElementCount;
    layout.SimplificationElementCount = 1U + layout.TotalElementCount;
    layout.AllocationElementCount = 1U + layout.TotalElementCount;
    layout.PropagationElementCount = 2U + layout.TotalElementCount;
    layout.IndexElementCount = layout.TotalElementCount;
    layout.MemoryElementCount = 2U;
    // 错误码/槽位后接 split、merge、传播和帧前活动数诊断。
    layout.ValidationElementCount = CbtValidationWordCount;
    layout.DrawStateElementCount = CbtDrawStateWordCount;
    layout.TopologyDispatchElementCount = CbtIndirectDispatchWordCount;
    layout.GeometryDispatchElementCount = CbtIndirectDispatchWordCount;
    return layout;
}

CbtBaseTopology BuildSquareCbtBaseTopology(CbtOccupancyCapacity capacity)
{
    CbtBaseTopology topology{};
    topology.Layout = BuildCbtTopologyBufferLayout(capacity);
    topology.BaseDepth = BaseDepth;

    const std::uint32_t baseOffset = topology.Layout.BaseElementOffset;
    for (std::uint32_t index = 0U; index < CbtBaseBisectorCount; ++index)
    {
        topology.HeapIds[index] = FirstBaseHeapId + index;
        topology.ActiveIndices[index] = baseOffset + index;
        topology.VisibleIndices[index] = baseOffset + index;
        topology.BisectorData[index].Indices.fill(InvalidCbtBisectorIndex);
        topology.BisectorData[index].ProblematicNeighbor = InvalidCbtBisectorIndex;
        topology.BisectorData[index].Flags =
            CbtVisibleFlag | CbtModifiedFlag | EncodeCbtActiveDepth(CbtBaseDepth);
        topology.BisectorData[index].PropagationId = InvalidCbtBisectorIndex;
    }

    // 每个根三角形的三条有向边各对应一个基础二分器
    // 两个逆时针根三角形共享 v1-v2 对角线，边界半边保留无效 twin
    topology.Neighbors = {{
        {baseOffset + 2U, baseOffset + 1U, InvalidCbtBisectorIndex},
        {baseOffset + 0U, baseOffset + 2U, baseOffset + 3U},
        {baseOffset + 1U, baseOffset + 0U, InvalidCbtBisectorIndex},
        {baseOffset + 5U, baseOffset + 4U, baseOffset + 1U},
        {baseOffset + 3U, baseOffset + 5U, InvalidCbtBisectorIndex},
        {baseOffset + 4U, baseOffset + 3U, InvalidCbtBisectorIndex},
    }};

    constexpr CbtBaseControlPoint v0{0.0F, 0.0F, 0.0F};
    constexpr CbtBaseControlPoint v1{1.0F, 0.0F, 0.0F};
    constexpr CbtBaseControlPoint v2{0.0F, 0.0F, 1.0F};
    constexpr CbtBaseControlPoint v3{1.0F, 0.0F, 1.0F};
    constexpr CbtBaseControlPoint centroid0{1.0F / 3.0F, 0.0F, 1.0F / 3.0F};
    constexpr CbtBaseControlPoint centroid1{2.0F / 3.0F, 0.0F, 2.0F / 3.0F};

    // 每个半边使用 next、面中心、current，顺序与官方 LEB 控制点解码一致
    // 同一面的三个二分器共享面中心，但交换首尾点以对应各自有向半边
    topology.ControlPoints = {{
        v2, centroid0, v0,
        v1, centroid0, v2,
        v0, centroid0, v1,
        v2, centroid1, v1,
        v3, centroid1, v2,
        v1, centroid1, v3,
    }};

    const std::uint32_t baseVertexCount = CbtBaseBisectorCount * 3U;
    // active 和 visible 初始指向同一六项列表，modified 尚无待重算几何
    // draw state 精确保持两个 DRAW、修改位置数和显式活动数的上游布局
    topology.IndirectDrawState.Active = {baseVertexCount, 1U, 0U, 0U};
    topology.IndirectDrawState.Visible = {baseVertexCount, 1U, 0U, 0U};
    topology.IndirectDrawState.ModifiedPositionCount = CbtBaseBisectorCount * 4U;
    topology.IndirectDrawState.ActiveBisectorCount = CbtBaseBisectorCount;
    topology.GeometryDispatchCommands[0] = {DispatchGroupCount(CbtBaseBisectorCount), 1U, 1U};
    topology.GeometryDispatchCommands[1] = {DispatchGroupCount(CbtBaseBisectorCount * 4U), 1U, 1U};
    topology.GeometryDispatchCommands[2] = {DispatchGroupCount(CbtBaseBisectorCount * 4U), 1U, 1U};
    return topology;
}

bool ValidateCbtBaseTopology(const CbtBaseTopology& topology, std::string* errorMessage)
{
    const CbtTopologyBufferLayout& layout = topology.Layout;

    // 物理基础槽位必须紧跟动态池，不能被 OCBT rank-select 返回或覆盖
    if (layout.BaseElementOffset != layout.DynamicElementCount ||
        layout.TotalElementCount != layout.DynamicElementCount + CbtBaseBisectorCount)
    {
        return SetValidationError(errorMessage, "base bisectors overlap the dynamic CBT budget");
    }
    if (topology.BaseDepth != BaseDepth)
    {
        return SetValidationError(errorMessage, "base depth does not match six halfedges");
    }

    for (std::uint32_t index = 0U; index < CbtBaseBisectorCount; ++index)
    {
        // heap ID 描述 LEB 路径，physicalIndex 才是缓冲和邻接使用的实际地址
        const std::uint32_t physicalIndex = layout.BaseElementOffset + index;
        if (topology.HeapIds[index] != FirstBaseHeapId + index)
        {
            return SetValidationError(errorMessage, "base heap IDs are not contiguous");
        }
        if (topology.ActiveIndices[index] != physicalIndex || topology.VisibleIndices[index] != physicalIndex)
        {
            return SetValidationError(errorMessage, "base active indices do not reference physical base slots");
        }

        const CbtBisectorNeighbors& neighbors = topology.Neighbors[index];

        // prev/next 在各自三角形内闭环，twin 只跨越共享对角线
        if (neighbors.Previous < layout.BaseElementOffset || neighbors.Previous >= layout.TotalElementCount ||
            neighbors.Next < layout.BaseElementOffset || neighbors.Next >= layout.TotalElementCount)
        {
            return SetValidationError(errorMessage, "prev or next leaves the base halfedge range");
        }
        if (topology.Neighbors[neighbors.Previous - layout.BaseElementOffset].Next != physicalIndex ||
            topology.Neighbors[neighbors.Next - layout.BaseElementOffset].Previous != physicalIndex)
        {
            return SetValidationError(errorMessage, "prev and next are not reciprocal");
        }
        if (neighbors.Twin != InvalidCbtBisectorIndex)
        {
            if (neighbors.Twin < layout.BaseElementOffset || neighbors.Twin >= layout.TotalElementCount ||
                topology.Neighbors[neighbors.Twin - layout.BaseElementOffset].Twin != physicalIndex)
            {
                return SetValidationError(errorMessage, "twin is not reciprocal");
            }
        }
    }

    // 每组三个控制点的中点必须等于所属根三角形的面中心
    // 这里不只检查范围，防止两个面的控制点批次被错误交叉
    for (std::uint32_t halfedge = 0U; halfedge < CbtBaseBisectorCount; ++halfedge)
    {
        const CbtBaseControlPoint& centroid = topology.ControlPoints[halfedge * 3U + 1U];
        const CbtBaseControlPoint expected = halfedge < 3U
            ? CbtBaseControlPoint{1.0F / 3.0F, 0.0F, 1.0F / 3.0F}
            : CbtBaseControlPoint{2.0F / 3.0F, 0.0F, 2.0F / 3.0F};
        if (!SamePoint(centroid, expected))
        {
            return SetValidationError(errorMessage, "base control point centroid mismatch");
        }
    }

    if (topology.IndirectDrawState.Active.VertexCountPerInstance != CbtBaseControlPointCount ||
        topology.IndirectDrawState.Visible.VertexCountPerInstance != CbtBaseControlPointCount ||
        topology.IndirectDrawState.ModifiedPositionCount != CbtBaseBisectorCount * 4U ||
        topology.IndirectDrawState.ActiveBisectorCount != CbtBaseBisectorCount)
    {
        return SetValidationError(errorMessage, "initial indirect draw counts are invalid");
    }
    if (errorMessage != nullptr)
    {
        errorMessage->clear();
    }
    return true;
}

std::uint32_t CbtHeapIdDepth(std::uint64_t heapId)
{
    return heapId == 0U ? 0U : static_cast<std::uint32_t>(std::bit_width(heapId) - 1U);
}

std::uint64_t CbtHeapIdParent(std::uint64_t heapId)
{
    return heapId >> 1U;
}

std::uint64_t CbtHeapIdChild(std::uint64_t heapId, bool rightChild)
{
    // 低位记录本次二分方向，父路径整体左移后仍保持标准二叉 heap 编码
    return (heapId << 1U) | static_cast<std::uint64_t>(rightChild);
}
} // namespace ParallelRoam::Algorithms::Cbt2024
