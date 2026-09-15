#include "CbtGpuAbi.hlsli"
#include "CbtOccupancyTree.hlsli"

// 分类返回值的数值和上游 ClassifyBisector ABI 完全一致。
// 负值同时承载不可见原因，正值只表示本帧请求 split。
static const int CbtBackFaceCulled = CBT_GPU_CLASSIFICATION_BACK_FACE_CULLED;
static const int CbtFrustumCulled = CBT_GPU_CLASSIFICATION_FRUSTUM_CULLED;
static const int CbtTooSmall = CBT_GPU_CLASSIFICATION_TOO_SMALL;
static const int CbtUnchanged = CBT_GPU_CLASSIFICATION_UNCHANGED;
static const int CbtBisect = CBT_GPU_CLASSIFICATION_BISECT;

cbuffer CbtGlobalConstants : register(b0)
{
    // 使用列主矩阵，保持 GLM 上传内存与 HLSL mul(matrix, vector) 一致。
    column_major float4x4 CbtViewProjection;
};

cbuffer CbtGeometryConstants : register(b1)
{
    // total 包含动态池和位于尾部的六个基础槽位。
    uint CbtTotalElementCount;
    uint CbtBaseElementOffset;
    uint CbtBaseElementCount;
    uint CbtBaseDepth;
    uint CbtDynamicElementCount;
    // 最大深度逐帧上传，但不会改变已分配资源容量。
    uint CbtMaxSubdivisionDepth;
    // 0/1 选择本帧只读邻接；另一份是复制和模板提交的下一代。
    uint CbtNeighborReadIndex;
};

cbuffer CbtUpdateConstants : register(b2)
{
    // 面积阈值的单位是实际 drawable 像素，不复用 CPU ROAM 厚度误差。
    float CbtTriangleAreaPixels;
    float2 CbtScreenSize;
    float CbtUpdatePadding0;
    float3 CbtCameraPosition;
    float CbtUpdatePadding1;
    float3 CbtCameraForward;
    float CbtUpdatePadding2;
    float4 CbtFrustumPlanes[6];
};

StructuredBuffer<float3> CbtClassificationPositions : register(t0); // 三当前顶点后接每槽父级辅助点。
StructuredBuffer<uint> CbtPreviousActiveIndices : register(t1); // 顺序和上一帧 Indexation 输出一致。

// u0 和 u1 由 OCBT include 声明 其余寄存器严格延续上游 topology ABI
RWStructuredBuffer<uint64_t> CbtHeapIds : register(u2);
RWStructuredBuffer<uint3> CbtCurrentNeighbors : register(u3);
RWStructuredBuffer<uint3> CbtNextNeighbors : register(u4);
RWStructuredBuffer<CbtBisectorData> CbtBisectorDataBuffer : register(u5);
RWStructuredBuffer<uint> CbtClassification : register(u6);
RWStructuredBuffer<uint> CbtSimplification : register(u7);
RWStructuredBuffer<int> CbtAllocation : register(u8);
RWStructuredBuffer<int> CbtPropagation : register(u9);
// u10 到 u12 保存跨 pass 控制状态而不是按 element 展开的任务数组
RWStructuredBuffer<int> CbtMemory : register(u10);
RWStructuredBuffer<uint> CbtTopologyIndirect : register(u11);
RWStructuredBuffer<uint> CbtDrawState : register(u12);
RWStructuredBuffer<uint> CbtActiveIndices : register(u13);
RWStructuredBuffer<uint> CbtVisibleIndices : register(u14);
RWStructuredBuffer<uint> CbtModifiedIndices : register(u15);
// validation 只记录错误状态 不参与候选批准或拓扑提交
RWStructuredBuffer<uint> CbtValidation : register(u16);

// u3/u4 的描述符位置在管线生命周期内不变，只有常量选择当前发布代次。
// 所有规划 pass 只读 current；复制、Bisect、传播和验证只写或读取 next。
// CPU 仅在完整帧事务记录成功后翻转 CbtNeighborReadIndex。
uint3 CbtLoadCurrentNeighbors(uint physicalSlot)
{
    return CbtNeighborReadIndex == 0u
        ? CbtCurrentNeighbors[physicalSlot]
        : CbtNextNeighbors[physicalSlot];
}

uint3 CbtLoadNextNeighbors(uint physicalSlot)
{
    return CbtNeighborReadIndex == 0u
        ? CbtNextNeighbors[physicalSlot]
        : CbtCurrentNeighbors[physicalSlot];
}

void CbtStoreNextNeighbors(uint physicalSlot, uint3 neighbors)
{
    if (CbtNeighborReadIndex == 0u)
    {
        CbtNextNeighbors[physicalSlot] = neighbors;
    }
    else
    {
        CbtCurrentNeighbors[physicalSlot] = neighbors;
    }
}

bool CbtReplaceNextNeighbor(
    uint physicalSlot,
    uint component,
    uint expectedNeighbor,
    uint replacementNeighbor)
{
    // 同一个三角形的不同边可能被不同 propagation 线程同时修补；按分量原子替换，
    // 避免 uint3 的 read-modify-write 覆盖另一个线程刚刚发布的边。
    // CompareExchange 还保证只替换仍指向旧 parent 的关系。
    // 若相邻模板已经发布了更新值，本传播任务不会把它改回过期引用。
    uint originalNeighbor;
    if (CbtNeighborReadIndex == 0u)
    {
        InterlockedCompareExchange(
            CbtNextNeighbors[physicalSlot][component],
            expectedNeighbor,
            replacementNeighbor,
            originalNeighbor);
    }
    else
    {
        InterlockedCompareExchange(
            CbtCurrentNeighbors[physicalSlot][component],
            expectedNeighbor,
            replacementNeighbor,
            originalNeighbor);
    }
    return originalNeighbor == expectedNeighbor;
}

void CbtMarkTopologyEvent(uint physicalSlot, uint eventFlag)
{
    // Propagation-touched leaves remain visible even though their neighbor references changed.
    // Keep visibility and active depth intact while replacing the event type and lifetime.
    // Atomic flag updates allow independent propagation chains to meet at the same leaf safely.
    uint previousFlags;
    InterlockedAnd(
        CbtBisectorDataBuffer[physicalSlot].Flags,
        ~(CbtDebugEventMask | CbtDebugEventLifetimeMask),
        previousFlags);
    InterlockedOr(
        CbtBisectorDataBuffer[physicalSlot].Flags,
        CbtModifiedFlag | (eventFlag & CbtDebugEventMask) |
            CbtEncodeDebugEventLifetime(CbtDebugEventHoldFrames),
        previousFlags);
}

void CbtStoreNextTwin(uint physicalSlot, uint twin)
{
    // right/left-double 的上游规则已经唯一确定 facing 边，不需要 expected 比较。
    // 这里仍按 ping/pong 代次选择目标资源，禁止写回 current 快照。
    if (CbtNeighborReadIndex == 0u)
    {
        CbtNextNeighbors[physicalSlot].z = twin;
    }
    else
    {
        CbtCurrentNeighbors[physicalSlot].z = twin;
    }
}

void CbtSetValidationError(uint code, uint physicalSlot)
{
    // 第一种错误类型获胜，同类型错误再用最小物理槽位提供稳定诊断。
    // validation 不参与提交决策，因此不会改变生产路径的线程调度。
    uint previousCode;
    InterlockedCompareExchange(CbtValidation[0], 0u, code, previousCode);
    if (previousCode == 0u || previousCode == code)
    {
        InterlockedMin(CbtValidation[1], physicalSlot);
    }
}

[numthreads(1, 1, 1)]
void CSResetE0(uint dispatchThreadId : SV_DispatchThreadID)
{
    // Reset 只清瞬态任务头和诊断；heapID、OCBT 位和已发布邻接跨帧常驻。
    // ActiveBisectorCount 保留上一帧值，供本帧 classification indirect 使用。
    // memory[1] 只描述可分配动态池 基础六槽不进入 OCBT 容量
    CbtMemory[0] = 0;
    CbtMemory[1] = int(CbtElementCount - CbtReadTreeCount(1u));
    CbtClassification[0] = 0;
    CbtClassification[1] = 0;
    CbtSimplification[0] = 0;
    CbtAllocation[0] = 0;
    CbtPropagation[0] = 0;
    CbtPropagation[1] = 0;
    CbtValidation[0] = 0;
    CbtValidation[1] = 0xffffffffu;
    // E2/E3 统计后接 F merge 计数和帧前动态活动数。
    [unroll]
    for (uint diagnostic = 2u; diagnostic < CbtValidationWordCount; ++diagnostic)
    {
        CbtValidation[diagnostic] = 0u;
    }
    CbtValidation[17] = CbtReadTreeCount(1u);

    // 两个 draw command 保留 InstanceCount=1 其余计数由本帧 Indexation 重建
    CbtDrawState[CbtDrawActiveVertexCountWord] = 0;
    CbtDrawState[CbtDrawActiveInstanceCountWord] = 1;
    CbtDrawState[CbtDrawActiveStartVertexWord] = 0;
    CbtDrawState[CbtDrawActiveStartInstanceWord] = 0;
    CbtDrawState[CbtDrawVisibleVertexCountWord] = 0;
    CbtDrawState[CbtDrawVisibleInstanceCountWord] = 1;
    CbtDrawState[CbtDrawVisibleStartVertexWord] = 0;
    CbtDrawState[CbtDrawVisibleStartInstanceWord] = 0;
    // 尾部两个字分别是修改位置数和显式活动二分器数
    CbtDrawState[CbtDrawModifiedPositionCountWord] = 0;
    // 第九字保留上一帧活动数供本帧 Classify 间接调度

    [unroll]
    for (uint word = 0u; word < 9u; ++word)
    {
        CbtTopologyIndirect[word] = 0u;
    }
}

uint CbtHeapDepth(uint64_t heapId)
{
    // 上游深度采用 heapID 位长，因此基础 heapID 8..13 的深度都是 4。
    const uint high = uint(heapId >> 32u);
    return high != 0u ? uint(firstbithigh(high)) + 33u : uint(firstbithigh(uint(heapId))) + 1u;
}

bool CbtFrustumAabbIntersects(float3 aabbMin, float3 aabbMax)
{
    const float3 center = (aabbMax + aabbMin) * 0.5;
    const float3 extents = (aabbMax - aabbMin) * 0.5;
    // 上游分类只使用左右上下四个平面 近远裁剪继续由投影和背面条件处理
    [unroll]
    for (uint planeIndex = 0u; planeIndex < 4u; ++planeIndex)
    {
        const float4 plane = CbtFrustumPlanes[planeIndex];
        const float3 positiveVertex = center + extents * sign(plane.xyz);
        if (dot(positiveVertex, plane.xyz) + plane.w < 0.0)
        {
            return false;
        }
    }
    return true;
}

float2 CbtProjectToUnitScreen(float3 position)
{
    float4 projected = mul(CbtViewProjection, float4(position, 1.0));
    projected.xy /= projected.w;
    return projected.xy * 0.5 + 0.5;
}

float CbtProjectedArea(float2 p0, float2 p1, float2 p2)
{
    // 单位屏幕中的鞋带公式结果乘 drawable 面积后得到像素面积。
    const float normalizedArea = 0.5 * abs(
        p0.x * (p2.y - p1.y) +
        p1.x * (p0.y - p2.y) +
        p2.x * (p1.y - p0.y));
    return normalizedArea * CbtScreenSize.x * CbtScreenSize.y;
}

int CbtClassifyTriangle(float3 p0, float3 p1, float3 p2, float3 parentPosition, uint depth)
{
    // 裁剪、面积和迟滞判断的顺序也是协议的一部分，不能为优化而交换。
    const float3 triangleNormal = normalize(cross(p2 - p1, p0 - p1));
    const float3 triangleCenter = (p0 + p1 + p2) / 3.0;
    // 上游顶点是相机相对坐标 这里对世界坐标作等价视线变换
    const float3 viewDirection = normalize(CbtCameraPosition - triangleCenter);
    const float forwardDotView = dot(viewDirection, CbtCameraForward);
    const float viewDotNormal = dot(viewDirection, triangleNormal);
    if (forwardDotView < 0.0 && viewDotNormal < -1.0e-3)
    {
        return CbtBackFaceCulled;
    }

    const float3 aabbMin = min(min(p0, p1), p2);
    const float3 aabbMax = max(max(p0, p1), p2);
    if (!CbtFrustumAabbIntersects(aabbMin, aabbMax))
    {
        return CbtFrustumCulled;
    }

    const float2 projected0 = CbtProjectToUnitScreen(p0);
    const float2 projected1 = CbtProjectToUnitScreen(p1);
    const float2 projected2 = CbtProjectToUnitScreen(p2);
    // 掠射角放大保持上游公式 前序背面条件保证有效地形法线进入非负定义域
    const float areaOverestimation = lerp(2.0, 1.0, pow(viewDotNormal, 0.2));
    const float area = CbtProjectedArea(projected0, projected1, projected2) * areaOverestimation;
    if (CbtTriangleAreaPixels < area && depth < CbtMaxSubdivisionDepth)
    {
        return CbtBisect;
    }
    if ((CbtTriangleAreaPixels * 0.5 > area) || (depth > CbtMaxSubdivisionDepth))
    {
        // 当前叶过小时还要检查父三角形，防止在阈值附近产生 split/simplify 抖动。
        const float2 projectedParent = CbtProjectToUnitScreen(parentPosition);
        const float parentArea =
            CbtProjectedArea(projected0, projectedParent, projected2) * areaOverestimation;
        return ((CbtTriangleAreaPixels >= parentArea) || (depth > CbtMaxSubdivisionDepth))
            ? CbtTooSmall
            : CbtUnchanged;
    }
    return CbtUnchanged;
}

[numthreads(64, 1, 1)]
void CSClassify(uint activeOrdinal : SV_DispatchThreadID)
{
    // dispatch 向上取整，显式活动数负责裁掉最后一个 thread group 的空线程。
    if (activeOrdinal >= CbtDrawState[CbtDrawActiveBisectorCountWord])
    {
        return;
    }

    const uint physicalSlot = CbtPreviousActiveIndices[activeOrdinal];
    if (physicalSlot >= CbtTotalElementCount || CbtHeapIds[physicalSlot] == 0u)
    {
        CbtValidation[0] = 1u;
        InterlockedMin(CbtValidation[1], physicalSlot);
        return;
    }

    const float3 p0 = CbtClassificationPositions[3u * physicalSlot + 0u];
    const float3 p1 = CbtClassificationPositions[3u * physicalSlot + 1u];
    const float3 p2 = CbtClassificationPositions[3u * physicalSlot + 2u];
    const float3 parentPosition =
        CbtClassificationPositions[3u * CbtTotalElementCount + physicalSlot];
    const uint64_t heapId = CbtHeapIds[physicalSlot];
    const uint depth = CbtHeapDepth(heapId);
    const int validity = CbtClassifyTriangle(p0, p1, p2, parentPosition, depth);

    CbtBisectorData data = CbtBisectorDataBuffer[physicalSlot];
    data.SubdivisionPattern = 0u;
    data.BisectorState = CbtUnchangedElement;
    data.ProblematicNeighbor = CbtInvalidIndex;
    // CBT 每帧都会分类；短暂保留事件元数据，让人眼能够看到刚完成的拓扑变化。
    const uint debugFlags = CbtDecayDebugEvent(data.Flags);
    const uint persistentFlags = debugFlags | CbtEncodeActiveDepth(depth);
    data.Flags = CbtVisibleFlag | persistentFlags;

    if (validity > CbtUnchanged)
    {
        // split 候选从 classification[2] 开始紧密排列，头两个字保留原子计数。
        data.BisectorState = CbtBisectElement;
        uint targetSlot;
        InterlockedAdd(CbtClassification[0], 1u, targetSlot);
        if (targetSlot < CbtTotalElementCount)
        {
            CbtClassification[2u + targetSlot] = physicalSlot;
        }
        else
        {
            CbtValidation[0] = 2u;
            InterlockedMin(CbtValidation[1], physicalSlot);
        }
    }
    else
    {
        data.Flags = (validity >= CbtTooSmall ? CbtVisibleFlag : 0u) | persistentFlags;
    }

    if (depth != CbtBaseDepth && validity < CbtUnchanged)
    {
        // simplify 表位于 2+T，且偶 heapID 只让一侧 sibling 发出一次 pair 请求。
        data.BisectorState = CbtSimplifyElement;
        if ((heapId & 1u) == 0u)
        {
            uint targetSlot;
            InterlockedAdd(CbtClassification[1], 1u, targetSlot);
            if (targetSlot < CbtTotalElementCount)
            {
                CbtClassification[2u + CbtTotalElementCount + targetSlot] = physicalSlot;
            }
            else
            {
                CbtValidation[0] = 3u;
                InterlockedMin(CbtValidation[1], physicalSlot);
            }
        }
    }
    CbtBisectorDataBuffer[physicalSlot] = data;
}

[numthreads(1, 1, 1)]
void CSPrepareClassificationIndirect(uint dispatchThreadId : SV_DispatchThreadID)
{
    // topology indirect 连续保存三组 D3D12_DISPATCH_ARGUMENTS。
    // 前两组分别消费 split 和 simplify 计数，第三组留给 E2 allocation。
    CbtTopologyIndirect[0] = (CbtClassification[0] + 63u) / 64u;
    CbtTopologyIndirect[1] = 1u;
    CbtTopologyIndirect[2] = 1u;
    CbtTopologyIndirect[3] = (CbtClassification[1] + 63u) / 64u;
    CbtTopologyIndirect[4] = 1u;
    CbtTopologyIndirect[5] = 1u;
    CbtTopologyIndirect[6] = 0u;
    CbtTopologyIndirect[7] = 1u;
    CbtTopologyIndirect[8] = 1u;
}

bool CbtValidatePlanningNeighbor(uint physicalSlot)
{
    // 无效哨兵表示合法边界；其他值必须同时落在总槽位内且拥有非零 heapID。
    if (physicalSlot == CbtInvalidIndex)
    {
        return true;
    }
    if (physicalSlot < CbtTotalElementCount && CbtHeapIds[physicalSlot] != 0u)
    {
        return true;
    }
    CbtValidation[0] = 4u;
    InterlockedMin(CbtValidation[1], physicalSlot);
    return false;
}

void CbtAppendAllocationNode(uint physicalSlot)
{
    // allocation[0] 是节点计数，正文从 1 开始保存首次认领 pattern 的物理槽位。
    int targetLocation;
    InterlockedAdd(CbtAllocation[0], 1, targetLocation);
    if (targetLocation >= 0 && uint(targetLocation) < CbtTotalElementCount)
    {
        CbtAllocation[1u + uint(targetLocation)] = int(physicalSlot);
        return;
    }
    CbtValidation[0] = 5u;
    InterlockedMin(CbtValidation[1], physicalSlot);
}

void CbtPlanSplit(uint currentId)
{
    // SplitElement 的所有写入都限制在 pattern、allocation 和 memory 三类暂存资源。
    uint3 currentNeighbors = CbtLoadCurrentNeighbors(currentId);
    if (!CbtValidatePlanningNeighbor(currentNeighbors.x) ||
        !CbtValidatePlanningNeighbor(currentNeighbors.y) ||
        !CbtValidatePlanningNeighbor(currentNeighbors.z))
    {
        return;
    }

    // prev/next 邻居若以当前节点为兼容路径 twin，就由该邻居的候选线程拥有本段路径。
    if (currentNeighbors.x != CbtInvalidIndex)
    {
        const uint3 previousNeighbors = CbtLoadCurrentNeighbors(currentNeighbors.x);
        if (previousNeighbors.z == currentId &&
            CbtBisectorDataBuffer[currentNeighbors.x].BisectorState != CbtUnchangedElement)
        {
            InterlockedAdd(CbtValidation[2], 1u);
            return;
        }
    }
    if (currentNeighbors.y != CbtInvalidIndex)
    {
        const uint3 nextNeighbors = CbtLoadCurrentNeighbors(currentNeighbors.y);
        if (nextNeighbors.z == currentId &&
            CbtBisectorDataBuffer[currentNeighbors.y].BisectorState != CbtUnchangedElement)
        {
            InterlockedAdd(CbtValidation[2], 1u);
            return;
        }
    }

    uint currentDepth = CbtHeapDepth(CbtHeapIds[currentId]);
    int maximumRequiredMemory = 2 * int(currentDepth - CbtBaseDepth) - 1;
    uint twinId = currentNeighbors.z;
    // 边界和直接 facing twin 的真实上界远小于通用兼容链公式。
    if (twinId == CbtInvalidIndex)
    {
        maximumRequiredMemory = 1;
    }
    else if (CbtLoadCurrentNeighbors(twinId).z == currentId)
    {
        maximumRequiredMemory = 2;
    }
    if (maximumRequiredMemory <= 0)
    {
        CbtValidation[0] = 6u;
        InterlockedMin(CbtValidation[1], currentId);
        return;
    }

    int previousRemainingMemory;
    // InterlockedAdd 返回扣减前的值，因此小于请求量时必须把整份请求加回。
    InterlockedAdd(CbtMemory[1], -maximumRequiredMemory, previousRemainingMemory);
    if (previousRemainingMemory < maximumRequiredMemory)
    {
        // 预留失败不允许留下 pattern 或 allocation 节点。
        InterlockedAdd(CbtMemory[1], maximumRequiredMemory, previousRemainingMemory);
        return;
    }

    uint usedMemory = 1u;
    uint previousPattern;
    InterlockedOr(
        CbtBisectorDataBuffer[currentId].SubdivisionPattern,
        CbtCenterSplitPattern,
        previousPattern);
    if (previousPattern != 0u)
    {
        // 另一候选已认领共享路径，当前候选归还完整保守预留。
        InterlockedAdd(CbtValidation[2], 1u);
        InterlockedAdd(CbtMemory[1], maximumRequiredMemory, previousRemainingMemory);
        return;
    }
    CbtAppendAllocationNode(currentId);

    // 正常拓扑每次沿 twin 向兼容节点靠近；步数守卫只在损坏邻接形成环时触发。
    [loop]
    for (uint traversalStep = 0u;
         twinId != CbtInvalidIndex && traversalStep < CbtTotalElementCount;
         ++traversalStep)
    {
        InterlockedAdd(CbtValidation[4], 1u);
        // 最大链长用原子 max 聚合，候选执行顺序不会影响统计值。
        InterlockedMax(CbtValidation[5], traversalStep + 1u);
        if (!CbtValidatePlanningNeighbor(twinId))
        {
            break;
        }
        const uint64_t neighborHeapId = CbtHeapIds[twinId];
        const uint neighborDepth = CbtHeapDepth(neighborHeapId);
        const uint3 neighborNeighbors = CbtLoadCurrentNeighbors(twinId);
        if (!CbtValidatePlanningNeighbor(neighborNeighbors.x) ||
            !CbtValidatePlanningNeighbor(neighborNeighbors.y) ||
            !CbtValidatePlanningNeighbor(neighborNeighbors.z))
        {
            break;
        }

        if (neighborDepth == currentDepth)
        {
            // 同深节点只增加一个中心子槽，并在首次认领时登记 allocation。
            InterlockedOr(
                CbtBisectorDataBuffer[twinId].SubdivisionPattern,
                CbtCenterSplitPattern,
                previousPattern);
            if (previousPattern == 0u)
            {
                CbtAppendAllocationNode(twinId);
                ++usedMemory;
            }
            else
            {
                InterlockedAdd(CbtValidation[3], 1u);
            }
            twinId = CbtInvalidIndex;
        }
        else
        {
            // prev 指向来路时使用 right double，否则沿用上游 left double 分支。
            const uint doublePattern = neighborNeighbors.x == currentId
                ? CbtRightDoubleSplitPattern
                : CbtLeftDoubleSplitPattern;
            InterlockedOr(
                CbtBisectorDataBuffer[twinId].SubdivisionPattern,
                doublePattern,
                previousPattern);
            if (previousPattern != 0u)
            {
                InterlockedAdd(CbtValidation[3], 1u);
                ++usedMemory;
                twinId = CbtInvalidIndex;
            }
            else
            {
                CbtAppendAllocationNode(twinId);
                usedMemory += 2u;
                currentId = twinId;
                currentDepth = neighborDepth;
                twinId = neighborNeighbors.z;
            }
        }
    }

    const int unusedReservation = max(maximumRequiredMemory - int(usedMemory), 0);
    InterlockedAdd(CbtMemory[1], unusedReservation, previousRemainingMemory);
}

[numthreads(64, 1, 1)]
void CSSplitE2(uint dispatchId : SV_DispatchThreadID)
{
    // 第一组间接参数来自 E1 split candidate counter。
    if (dispatchId >= CbtClassification[0])
    {
        return;
    }
    const uint currentId = CbtClassification[2u + dispatchId];
    if (currentId >= CbtTotalElementCount || CbtHeapIds[currentId] == 0u)
    {
        CbtValidation[0] = 7u;
        InterlockedMin(CbtValidation[1], currentId);
        return;
    }
    CbtPlanSplit(currentId);
}

[numthreads(1, 1, 1)]
void CSPrepareAllocationIndirect(uint dispatchThreadId : SV_DispatchThreadID)
{
    // 第三组 D3D12_DISPATCH_ARGUMENTS 由 allocation list 头派生。
    // 即使计数为零，Y/Z 仍保持合法的 1，X=0 会令 ExecuteIndirect 不启动线程组。
    CbtTopologyIndirect[6] = (uint(CbtAllocation[0]) + 63u) / 64u;
    CbtTopologyIndirect[7] = 1u;
    CbtTopologyIndirect[8] = 1u;
}

[numthreads(64, 1, 1)]
void CSAllocateE2(uint dispatchId : SV_DispatchThreadID)
{
    // 第三组间接参数只覆盖非零 pattern 节点，而不是所有 split 候选。
    if (dispatchId >= uint(CbtAllocation[0]))
    {
        return;
    }
    const int allocationNode = CbtAllocation[1u + dispatchId];
    if (allocationNode < 0 || uint(allocationNode) >= CbtTotalElementCount)
    {
        CbtValidation[0] = 8u;
        InterlockedMin(CbtValidation[1], uint(allocationNode));
        return;
    }

    const uint currentId = uint(allocationNode);
    CbtBisectorData data = CbtBisectorDataBuffer[currentId];
    const int slotCount = int(countbits(data.SubdivisionPattern));
    int firstFreeRank;
    // 每个节点通过原子 add 取得互不重叠的旧 OCBT free-rank 区间。
    InterlockedAdd(CbtMemory[0], slotCount, firstFreeRank);
    [loop]
    for (int slot = 0; slot < slotCount; ++slot)
    {
        const uint freeRank = uint(firstFreeRank + slot);
        const uint physicalSlot = CbtDecodeBitComplement(freeRank);
        if (physicalSlot >= CbtDynamicElementCount)
        {
            CbtValidation[0] = 9u;
            InterlockedMin(CbtValidation[1], currentId);
            return;
        }
        data.Indices[slot] = physicalSlot;
    }
    // E2 只写预分配索引；OCBT 位要到 E3 Bisect 真正提交后才能置一。
    CbtBisectorDataBuffer[currentId] = data;
}

bool CbtEvaluateCommittedNeighbors(
    uint currentId,
    uint neighborId,
    out uint2 result)
{
    // 对端 pattern 由 E2 兼容链规划提前确定；NO_SPLIT 在这里是协议错误。
    // 返回的两个物理槽对应上游 evaluate_neighbors 的 resX/resY。
    // 函数只读 current 邻接和 allocation indices，不依赖 Bisect 线程执行顺序。
    result = uint2(CbtInvalidIndex, CbtInvalidIndex);
    if (neighborId >= CbtTotalElementCount || CbtHeapIds[neighborId] == 0u)
    {
        CbtSetValidationError(10u, currentId);
        return false;
    }

    const CbtBisectorData neighborData = CbtBisectorDataBuffer[neighborId];
    const uint3 neighborNeighbors = CbtLoadCurrentNeighbors(neighborId);
    if (neighborData.SubdivisionPattern == CbtCenterSplitPattern)
    {
        result = uint2(neighborData.Indices[0], neighborId);
    }
    else if (neighborData.SubdivisionPattern == CbtRightDoubleSplitPattern)
    {
        result = neighborNeighbors.x == currentId
            ? uint2(neighborData.Indices[1], neighborId)
            : uint2(neighborData.Indices[0], neighborData.Indices[1]);
    }
    else if (neighborData.SubdivisionPattern == CbtLeftDoubleSplitPattern)
    {
        result = neighborNeighbors.y == currentId
            ? uint2(neighborData.Indices[1], neighborData.Indices[0])
            : uint2(neighborData.Indices[0], neighborId);
    }
    else if (neighborData.SubdivisionPattern == CbtTripleSplitPattern)
    {
        if (neighborNeighbors.x == currentId)
        {
            result = uint2(neighborData.Indices[1], neighborId);
        }
        else if (neighborNeighbors.y == currentId)
        {
            result = uint2(neighborData.Indices[2], neighborData.Indices[0]);
        }
        else
        {
            result = uint2(neighborData.Indices[0], neighborData.Indices[1]);
        }
    }
    else
    {
        CbtSetValidationError(11u, neighborId);
        return false;
    }
    if (result.x >= CbtTotalElementCount || result.y >= CbtTotalElementCount)
    {
        CbtSetValidationError(12u, neighborId);
        return false;
    }
    return true;
}

void CbtWriteCommittedData(
    uint targetId,
    CbtBisectorData source,
    uint parentId,
    uint problematicNeighbor)
{
    // retained 与 sibling 继承同一 subdivision pattern，供本帧 propagation 查询。
    // PropagationId 是提交前父物理槽，而不是 heapID 的逻辑父节点。
    // visible/modified 同时置位，使 Indexation 派生绘制和增量几何任务。
    source.ProblematicNeighbor = problematicNeighbor;
    source.BisectorState = CbtUnchangedElement;
    source.Flags = CbtVisibleFlag | CbtModifiedFlag | CbtSplitEventFlag |
        CbtEncodeDebugEventLifetime(CbtDebugEventHoldFrames) |
        CbtEncodeActiveDepth(CbtHeapDepth(CbtHeapIds[targetId]));
    source.PropagationId = parentId;
    CbtBisectorDataBuffer[targetId] = source;
}

void CbtAppendSplitPropagation(uint physicalSlot)
{
    // 传播列表使用一个原子任务头；第二个头保留给后续 merge 阶段。
    // 只有 center 和 right-double 的一级 sibling 会登记此任务。
    // 溢出视为拓扑错误，绝不截断后继续发布邻接代次。
    int targetLocation;
    InterlockedAdd(CbtPropagation[0], 1, targetLocation);
    if (targetLocation < int(CbtTotalElementCount))
    {
        CbtPropagation[2u + uint(targetLocation)] = int(physicalSlot);
        InterlockedAdd(CbtValidation[7], 1u);
    }
    else
    {
        CbtSetValidationError(13u, physicalSlot);
    }
}

[numthreads(64, 1, 1)]
void CSBisectE3(uint dispatchId : SV_DispatchThreadID)
{
    // 每个 allocation node 由 E2 唯一认领，因此不同线程写入的输出槽互不重叠。
    // 所有模板都从 current 邻接求值，并把完整结果写到 next 邻接。
    // 新 heapID 和 OCBT 位只在模板输入全部通过验证后发布。
    if (dispatchId >= uint(CbtAllocation[0]))
    {
        return;
    }
    const int allocationNode = CbtAllocation[1u + dispatchId];
    if (allocationNode < 0 || uint(allocationNode) >= CbtTotalElementCount)
    {
        CbtSetValidationError(14u, uint(allocationNode));
        return;
    }

    const uint currentId = uint(allocationNode);
    const uint64_t baseHeapId = CbtHeapIds[currentId];
    const CbtBisectorData source = CbtBisectorDataBuffer[currentId];
    const uint pattern = source.SubdivisionPattern;
    if (baseHeapId == 0u ||
        (pattern != CbtCenterSplitPattern &&
         pattern != CbtRightDoubleSplitPattern &&
         pattern != CbtLeftDoubleSplitPattern &&
         pattern != CbtTripleSplitPattern))
    {
        CbtSetValidationError(15u, currentId);
        return;
    }

    const uint sibling0 = source.Indices[0];
    const uint sibling1 = source.Indices[1];
    const uint sibling2 = source.Indices[2];
    const uint slotCount = countbits(pattern);
    [unroll]
    for (uint slot = 0u; slot < 3u; ++slot)
    {
        if (slot < slotCount && source.Indices[slot] >= CbtDynamicElementCount)
        {
            CbtSetValidationError(16u, currentId);
            return;
        }
    }

    const uint3 parentNeighbors = CbtLoadCurrentNeighbors(currentId);
    uint2 result0 = uint2(CbtInvalidIndex, CbtInvalidIndex);
    uint2 result1 = uint2(CbtInvalidIndex, CbtInvalidIndex);
    uint2 result2 = uint2(CbtInvalidIndex, CbtInvalidIndex);
    if (pattern == CbtCenterSplitPattern)
    {
        // center：原槽承载偶子，sibling0 承载奇子。
        // 开放边界允许 facing twin 为 INVALID，此时 res2 保持 INVALID。
        // sibling0 的旧 next 外侧引用交给 propagation 条件修补。
        if (parentNeighbors.z != CbtInvalidIndex &&
            !CbtEvaluateCommittedNeighbors(currentId, parentNeighbors.z, result2))
        {
            return;
        }
        CbtHeapIds[currentId] = 2u * baseHeapId;
        CbtHeapIds[sibling0] = 2u * baseHeapId + 1u;
        CbtStoreNextNeighbors(currentId, uint3(sibling0, result2.x, parentNeighbors.x));
        CbtStoreNextNeighbors(sibling0, uint3(result2.y, currentId, parentNeighbors.y));
        CbtWriteCommittedData(currentId, source, currentId, CbtInvalidIndex);
        CbtWriteCommittedData(sibling0, source, currentId, parentNeighbors.y);
        CbtAppendSplitPropagation(sibling0);
        InterlockedAdd(CbtValidation[8], 1u);
    }
    else if (pattern == CbtRightDoubleSplitPattern)
    {
        // right-double：同时生成一级奇子和 retained 分支下的两个二级子。
        // previous 与可选 facing twin 的模板结果共同确定三组邻接。
        // sibling0 延迟修补 parentNeighbors.y 的反向引用。
        if (!CbtEvaluateCommittedNeighbors(currentId, parentNeighbors.x, result0) ||
            (parentNeighbors.z != CbtInvalidIndex &&
             !CbtEvaluateCommittedNeighbors(currentId, parentNeighbors.z, result1)))
        {
            return;
        }
        CbtHeapIds[currentId] = 4u * baseHeapId;
        CbtHeapIds[sibling0] = 2u * baseHeapId + 1u;
        CbtHeapIds[sibling1] = 4u * baseHeapId + 1u;
        CbtStoreNextNeighbors(currentId, uint3(sibling1, result0.x, sibling0));
        CbtStoreNextNeighbors(sibling0, uint3(result1.y, currentId, parentNeighbors.y));
        CbtStoreNextNeighbors(sibling1, uint3(result0.y, currentId, result1.x));
        CbtWriteCommittedData(currentId, source, currentId, CbtInvalidIndex);
        CbtWriteCommittedData(sibling0, source, currentId, parentNeighbors.y);
        CbtWriteCommittedData(sibling1, source, currentId, CbtInvalidIndex);
        CbtAppendSplitPropagation(sibling0);
        InterlockedAdd(CbtValidation[9], 1u);
    }
    else if (pattern == CbtLeftDoubleSplitPattern)
    {
        // left-double：next 侧的兼容模板提供镜像连接。
        // 三个输出的 facing 引用均在本模板中闭合，无需额外 propagation。
        // heapID 排列严格保持上游的 2h、4h+2、4h+3 顺序。
        if (!CbtEvaluateCommittedNeighbors(currentId, parentNeighbors.y, result0) ||
            (parentNeighbors.z != CbtInvalidIndex &&
             !CbtEvaluateCommittedNeighbors(currentId, parentNeighbors.z, result1)))
        {
            return;
        }
        CbtHeapIds[currentId] = 2u * baseHeapId;
        CbtHeapIds[sibling0] = 4u * baseHeapId + 2u;
        CbtHeapIds[sibling1] = 4u * baseHeapId + 3u;
        CbtStoreNextNeighbors(currentId, uint3(sibling1, result1.x, parentNeighbors.x));
        CbtStoreNextNeighbors(sibling0, uint3(sibling1, result0.x, result1.y));
        CbtStoreNextNeighbors(sibling1, uint3(result0.y, sibling0, currentId));
        CbtWriteCommittedData(currentId, source, currentId, CbtInvalidIndex);
        CbtWriteCommittedData(sibling0, source, currentId, CbtInvalidIndex);
        CbtWriteCommittedData(sibling1, source, currentId, CbtInvalidIndex);
        InterlockedAdd(CbtValidation[10], 1u);
    }
    else
    {
        // triple：previous、next 和可选 twin 三侧都参与相同批次提交。
        // 四个输出槽形成两层完整 LEB 子树，并在模板内闭合邻接。
        // 此分支不产生传播任务。
        if (!CbtEvaluateCommittedNeighbors(currentId, parentNeighbors.x, result0) ||
            !CbtEvaluateCommittedNeighbors(currentId, parentNeighbors.y, result1) ||
            (parentNeighbors.z != CbtInvalidIndex &&
             !CbtEvaluateCommittedNeighbors(currentId, parentNeighbors.z, result2)))
        {
            return;
        }
        CbtHeapIds[currentId] = 4u * baseHeapId;
        CbtHeapIds[sibling0] = 4u * baseHeapId + 2u;
        CbtHeapIds[sibling1] = 4u * baseHeapId + 1u;
        CbtHeapIds[sibling2] = 4u * baseHeapId + 3u;
        CbtStoreNextNeighbors(currentId, uint3(sibling1, result0.x, sibling2));
        CbtStoreNextNeighbors(sibling0, uint3(sibling2, result1.x, result2.y));
        CbtStoreNextNeighbors(sibling1, uint3(result0.y, currentId, result2.x));
        CbtStoreNextNeighbors(sibling2, uint3(result1.y, sibling0, currentId));
        CbtWriteCommittedData(currentId, source, currentId, CbtInvalidIndex);
        CbtWriteCommittedData(sibling0, source, currentId, CbtInvalidIndex);
        CbtWriteCommittedData(sibling1, source, currentId, CbtInvalidIndex);
        CbtWriteCommittedData(sibling2, source, currentId, CbtInvalidIndex);
        InterlockedAdd(CbtValidation[11], 1u);
    }

    // 新槽位全部写完 heapID、邻接和元数据后才发布原始占用位。
    // Reduce 在 Bisect/propagation 的 UAV barrier 之后重建 packed OCBT 计数。
    // validation[6] 与 memory[0] 的相等关系检查提交没有漏位或重复置位。
    [unroll]
    for (uint sibling = 0u; sibling < 3u; ++sibling)
    {
        if (sibling < slotCount)
        {
            CbtSetBitAtomic(source.Indices[sibling], true);
            InterlockedAdd(CbtValidation[6], 1u);
        }
    }
}

[numthreads(1, 1, 1)]
void CSPreparePropagationIndirectE3(uint dispatchThreadId : SV_DispatchThreadID)
{
    // propagation 复用 topology indirect 的第一组 Dispatch 参数。
    // X 为零时 ExecuteIndirect 合法地不启动线程组，Y/Z 始终保持一。
    // allocation 已消费第三组命令，传播复用第一组命令而不扩展九字协议。
    CbtTopologyIndirect[0] = (uint(CbtPropagation[0]) + 63u) / 64u;
    CbtTopologyIndirect[1] = 1u;
    CbtTopologyIndirect[2] = 1u;
}

[numthreads(64, 1, 1)]
void CSPropagateBisectE3(uint dispatchId : SV_DispatchThreadID)
{
    // 此 pass 在所有四模板写完 next 邻接后执行，只修补外部反向引用。
    // 不同任务可能触碰同一目标的不同边，因此条件替换必须按 uint 分量原子化。
    // 完成后清除 ProblematicNeighbor，并将任务节点恢复为 unchanged。
    if (dispatchId >= uint(CbtPropagation[0]))
    {
        return;
    }
    const int propagationNode = CbtPropagation[2u + dispatchId];
    if (propagationNode < 0 || uint(propagationNode) >= CbtTotalElementCount)
    {
        CbtSetValidationError(17u, uint(propagationNode));
        return;
    }

    const uint currentId = uint(propagationNode);
    CbtBisectorData currentData = CbtBisectorDataBuffer[currentId];
    const uint parentId = currentData.PropagationId;
    const uint problematic = currentData.ProblematicNeighbor;
    if (problematic == CbtInvalidIndex)
    {
        // 边界半边没有外侧邻居；模板仍登记 propagation，以统一提交路径，但无需修补。
        // INVALID 只在开放边界合法；其他越界或非活动槽仍报告 code 18。
        currentData.BisectorState = CbtUnchangedElement;
        CbtBisectorDataBuffer[currentId] = currentData;
        return;
    }
    if (problematic >= CbtTotalElementCount || CbtHeapIds[problematic] == 0u)
    {
        CbtSetValidationError(18u, problematic);
        return;
    }

    const CbtBisectorData targetData = CbtBisectorDataBuffer[problematic];
    if (targetData.SubdivisionPattern == CbtNoSplitPattern)
    {
        // Evaluate every component because separate propagation chains can own different edges.
        // A successful replacement promotes the stable target into this frame's split event set.
        // 未 split 目标可能从 previous、next 或 twin 任一分量指向旧 parent。
        bool replaced = CbtReplaceNextNeighbor(problematic, 0u, parentId, currentId);
        replaced = CbtReplaceNextNeighbor(problematic, 1u, parentId, currentId) || replaced;
        replaced = CbtReplaceNextNeighbor(problematic, 2u, parentId, currentId) || replaced;
        if (replaced)
        {
            CbtMarkTopologyEvent(problematic, CbtSplitEventFlag);
        }
    }
    else if (targetData.SubdivisionPattern == CbtCenterSplitPattern)
    {
        // center 目标的 facing 引用可能位于 retained 或其奇子，两处均条件替换。
        if (CbtReplaceNextNeighbor(problematic, 2u, parentId, currentId))
        {
            CbtMarkTopologyEvent(problematic, CbtSplitEventFlag);
        }
        if (targetData.PropagationId >= CbtTotalElementCount)
        {
            CbtSetValidationError(19u, currentId);
            return;
        }
        if (CbtReplaceNextNeighbor(targetData.PropagationId, 2u, parentId, currentId))
        {
            CbtMarkTopologyEvent(targetData.PropagationId, CbtSplitEventFlag);
        }
    }
    else if (targetData.SubdivisionPattern == CbtRightDoubleSplitPattern)
    {
        // right-double 的第二个 allocation sibling 承接新的 facing twin。
        if (targetData.Indices[1] >= CbtTotalElementCount)
        {
            CbtSetValidationError(20u, currentId);
            return;
        }
        CbtStoreNextTwin(targetData.Indices[1], currentId);
        CbtMarkTopologyEvent(targetData.Indices[1], CbtSplitEventFlag);
    }
    else if (targetData.SubdivisionPattern == CbtLeftDoubleSplitPattern)
    {
        // left-double 直接在 retained 物理槽上发布 facing twin。
        CbtStoreNextTwin(problematic, currentId);
        CbtMarkTopologyEvent(problematic, CbtSplitEventFlag);
    }
    else
    {
        CbtSetValidationError(21u, currentId);
        return;
    }
    currentData.ProblematicNeighbor = CbtInvalidIndex;
    currentData.BisectorState = CbtUnchangedElement;
    CbtBisectorDataBuffer[currentId] = currentData;
}

void CbtAppendSimplification(uint physicalSlot)
{
    // The append index is the unique merge-group index, never a per-node work item.
    // PrepareSimplify has already elected the minimum logical heapID for facing pairs.
    uint targetLocation;
    InterlockedAdd(CbtSimplification[0], 1u, targetLocation);
    if (targetLocation < CbtTotalElementCount)
    {
        CbtSimplification[1u + targetLocation] = physicalSlot;
        InterlockedAdd(CbtValidation[12], 1u);
    }
    else
    {
        CbtSetValidationError(28u, physicalSlot);
    }
}

[numthreads(64, 1, 1)]
void CSPrepareSimplifyF(uint dispatchId : SV_DispatchThreadID)
{
    // 第二组 classification indirect 参数遍历偶 heapID 的 simplify 候选。
    // 同帧 split 可能已改变候选状态，因此不再满足 simplify 时直接跳过。
    if (dispatchId >= CbtClassification[1])
    {
        return;
    }
    const uint currentId = CbtClassification[2u + CbtTotalElementCount + dispatchId];
    if (currentId >= CbtTotalElementCount || CbtHeapIds[currentId] == 0u)
    {
        CbtSetValidationError(26u, currentId);
        return;
    }

    const uint64_t currentHeapId = CbtHeapIds[currentId];
    const CbtBisectorData currentData = CbtBisectorDataBuffer[currentId];
    if ((currentHeapId & 1u) != 0u || currentData.BisectorState != CbtSimplifyElement)
    {
        return;
    }
    const uint currentDepth = CbtHeapDepth(currentHeapId);
    const uint3 currentNeighbors = CbtLoadNextNeighbors(currentId);
    const uint pairId = currentNeighbors.x;
    if (pairId >= CbtTotalElementCount || CbtHeapIds[pairId] == 0u)
    {
        CbtSetValidationError(26u, currentId);
        return;
    }

    const CbtBisectorData pairData = CbtBisectorDataBuffer[pairId];
    const uint3 pairNeighbors = CbtLoadNextNeighbors(pairId);
    if (CbtHeapDepth(CbtHeapIds[pairId]) != currentDepth ||
        pairData.BisectorState != CbtSimplifyElement)
    {
        return;
    }

    const uint twinLowId = pairNeighbors.x;
    const uint twinHighId = currentNeighbors.y;
    if (twinLowId != CbtInvalidIndex)
    {
        if (twinLowId >= CbtTotalElementCount || twinHighId >= CbtTotalElementCount ||
            CbtHeapIds[twinLowId] == 0u || CbtHeapIds[twinHighId] == 0u)
        {
            CbtSetValidationError(27u, currentId);
            return;
        }
        // facing neighborhood 只允许逻辑 heapID 最小的一侧登记四节点 merge。
        if (currentHeapId > CbtHeapIds[twinLowId])
        {
            return;
        }
        if (CbtHeapDepth(CbtHeapIds[twinLowId]) != currentDepth ||
            CbtHeapDepth(CbtHeapIds[twinHighId]) != currentDepth ||
            CbtBisectorDataBuffer[twinLowId].BisectorState != CbtSimplifyElement ||
            CbtBisectorDataBuffer[twinHighId].BisectorState != CbtSimplifyElement)
        {
            return;
        }
    }
    CbtAppendSimplification(currentId);
}

[numthreads(1, 1, 1)]
void CSPrepareSimplifyIndirectF(uint dispatchThreadId : SV_DispatchThreadID)
{
    // PrepareSimplify 消费完 classification 计数后，第二组命令改为合法 merge 数。
    CbtTopologyIndirect[3] = (CbtSimplification[0] + 63u) / 64u;
    CbtTopologyIndirect[4] = 1u;
    CbtTopologyIndirect[5] = 1u;
}

void CbtAppendSimplifyPropagation(uint physicalSlot)
{
    int targetLocation;
    InterlockedAdd(CbtPropagation[1], 1, targetLocation);
    if (targetLocation >= 0 && uint(targetLocation) < CbtTotalElementCount)
    {
        // split propagation 已经消费完成，merge 阶段可以从正文起点复用任务区。
        CbtPropagation[2u + uint(targetLocation)] = int(physicalSlot);
        InterlockedAdd(CbtValidation[14], 1u);
    }
    else
    {
        CbtSetValidationError(32u, physicalSlot);
    }
}

void CbtReleaseMergedSlot(uint physicalSlot)
{
    if (physicalSlot >= CbtDynamicElementCount)
    {
        CbtSetValidationError(31u, physicalSlot);
        return;
    }
    CbtHeapIds[physicalSlot] = 0u;
    CbtSetBitAtomic(physicalSlot, false);
    int previousFreeCount;
    InterlockedAdd(CbtMemory[1], 1, previousFreeCount);
    InterlockedAdd(CbtValidation[13], 1u);
}

[numthreads(64, 1, 1)]
void CSSimplifyF(uint dispatchId : SV_DispatchThreadID)
{
    if (dispatchId >= CbtSimplification[0])
    {
        return;
    }
    const uint currentId = CbtSimplification[1u + dispatchId];
    if (currentId >= CbtTotalElementCount || CbtHeapIds[currentId] == 0u)
    {
        CbtSetValidationError(29u, currentId);
        return;
    }

    const uint3 currentNeighbors = CbtLoadNextNeighbors(currentId);
    const uint pairId = currentNeighbors.x;
    if (pairId >= CbtTotalElementCount || CbtHeapIds[pairId] == 0u)
    {
        CbtSetValidationError(29u, currentId);
        return;
    }
    const uint3 pairNeighbors = CbtLoadNextNeighbors(pairId);
    const uint twinLowId = pairNeighbors.x;
    const uint twinHighId = currentNeighbors.y;
    if (pairId >= CbtDynamicElementCount ||
        (twinLowId != CbtInvalidIndex &&
         (twinLowId >= CbtTotalElementCount || twinHighId >= CbtDynamicElementCount ||
          CbtHeapIds[twinLowId] == 0u || CbtHeapIds[twinHighId] == 0u)))
    {
        CbtSetValidationError(30u, currentId);
        return;
    }

    CbtHeapIds[currentId] /= 2u;
    CbtStoreNextNeighbors(currentId, uint3(currentNeighbors.z, pairNeighbors.z, twinLowId));
    CbtBisectorData currentData = CbtBisectorDataBuffer[currentId];
    currentData.PropagationId = pairId;
    currentData.ProblematicNeighbor = pairNeighbors.z;
    currentData.BisectorState = CbtMergedElement;
    currentData.Flags = CbtVisibleFlag | CbtModifiedFlag | CbtMergeEventFlag |
        CbtEncodeDebugEventLifetime(CbtDebugEventHoldFrames) |
        CbtEncodeActiveDepth(CbtHeapDepth(CbtHeapIds[currentId]));
    CbtBisectorDataBuffer[currentId] = currentData;
    if (currentData.ProblematicNeighbor != CbtInvalidIndex)
    {
        CbtAppendSimplifyPropagation(currentId);
    }

    CbtBisectorData pairData = CbtBisectorDataBuffer[pairId];
    pairData.BisectorState = CbtMergedElement;
    pairData.Flags = 0u;
    CbtBisectorDataBuffer[pairId] = pairData;
    CbtReleaseMergedSlot(pairId);

    if (twinLowId == CbtInvalidIndex)
    {
        InterlockedAdd(CbtValidation[15], 1u);
        return;
    }

    const uint3 lowNeighbors = CbtLoadNextNeighbors(twinLowId);
    const uint3 highNeighbors = CbtLoadNextNeighbors(twinHighId);
    CbtHeapIds[twinLowId] /= 2u;
    CbtStoreNextNeighbors(twinLowId, uint3(lowNeighbors.z, highNeighbors.z, currentId));
    CbtBisectorData lowData = CbtBisectorDataBuffer[twinLowId];
    lowData.PropagationId = twinHighId;
    lowData.ProblematicNeighbor = highNeighbors.z;
    lowData.BisectorState = CbtMergedElement;
    lowData.Flags = CbtVisibleFlag | CbtModifiedFlag | CbtMergeEventFlag |
        CbtEncodeDebugEventLifetime(CbtDebugEventHoldFrames) |
        CbtEncodeActiveDepth(CbtHeapDepth(CbtHeapIds[twinLowId]));
    CbtBisectorDataBuffer[twinLowId] = lowData;
    if (lowData.ProblematicNeighbor != CbtInvalidIndex)
    {
        CbtAppendSimplifyPropagation(twinLowId);
    }

    CbtBisectorData highData = CbtBisectorDataBuffer[twinHighId];
    highData.BisectorState = CbtMergedElement;
    highData.Flags = 0u;
    CbtBisectorDataBuffer[twinHighId] = highData;
    CbtReleaseMergedSlot(twinHighId);
    InterlockedAdd(CbtValidation[16], 1u);
}

[numthreads(1, 1, 1)]
void CSPrepareSimplifyPropagationIndirectF(uint dispatchThreadId : SV_DispatchThreadID)
{
    CbtTopologyIndirect[3] = (uint(CbtPropagation[1]) + 63u) / 64u;
    CbtTopologyIndirect[4] = 1u;
    CbtTopologyIndirect[5] = 1u;
}

[numthreads(64, 1, 1)]
void CSPropagateSimplifyF(uint dispatchId : SV_DispatchThreadID)
{
    if (dispatchId >= uint(CbtPropagation[1]))
    {
        return;
    }
    const int propagationNode = CbtPropagation[2u + dispatchId];
    if (propagationNode < 0 || uint(propagationNode) >= CbtTotalElementCount ||
        CbtHeapIds[uint(propagationNode)] == 0u)
    {
        CbtSetValidationError(33u, uint(propagationNode));
        return;
    }

    const uint currentId = uint(propagationNode);
    CbtBisectorData currentData = CbtBisectorDataBuffer[currentId];
    const uint deletedPair = currentData.PropagationId;
    const uint neighborId = currentData.ProblematicNeighbor;
    if (deletedPair >= CbtTotalElementCount || neighborId >= CbtTotalElementCount)
    {
        CbtSetValidationError(34u, currentId);
        return;
    }

    const CbtBisectorData neighborData = CbtBisectorDataBuffer[neighborId];
    if (neighborData.BisectorState != CbtMergedElement || CbtHeapIds[neighborId] != 0u)
    {
        // Only an actual deleted-sibling replacement counts as a visible merge-side change.
        bool replaced = CbtReplaceNextNeighbor(neighborId, 0u, deletedPair, currentId);
        replaced = CbtReplaceNextNeighbor(neighborId, 1u, deletedPair, currentId) || replaced;
        replaced = CbtReplaceNextNeighbor(neighborId, 2u, deletedPair, currentId) || replaced;
        if (replaced)
        {
            CbtMarkTopologyEvent(neighborId, CbtMergeEventFlag);
        }
    }
    else
    {
        // 相邻 merge 同帧删除目标时，通过目标的保留 pair 更新最终活动节点。
        const uint neighborPair = CbtLoadNextNeighbors(neighborId).y;
        if (neighborPair >= CbtTotalElementCount || CbtHeapIds[neighborPair] == 0u)
        {
            CbtSetValidationError(35u, currentId);
            return;
        }
        bool replaced = CbtReplaceNextNeighbor(neighborPair, 0u, deletedPair, currentId);
        replaced = CbtReplaceNextNeighbor(neighborPair, 1u, deletedPair, currentId) || replaced;
        replaced = CbtReplaceNextNeighbor(neighborPair, 2u, deletedPair, currentId) || replaced;
        if (replaced)
        {
            CbtMarkTopologyEvent(neighborPair, CbtMergeEventFlag);
        }
    }
    currentData.ProblematicNeighbor = CbtInvalidIndex;
    CbtBisectorDataBuffer[currentId] = currentData;
}

// F validation UAV words are intentionally append-only across integration stages:
// [0] keeps the first error code and [1] its physical slot;
// [2] counts rejected duplicate split claims;
// [3] counts compatibility nodes shared by split chains;
// [4] accumulates compatibility traversal steps;
// [5] records the longest compatibility chain;
// [6] counts dynamic slots committed by Bisect;
// [7] counts split-side external neighbor repairs;
// [8..11] partition commits into the four Bisect templates;
// [12] counts legal pair or facing-pair merge groups;
// [13] counts dynamic sibling slots returned to the OCBT;
// [14] counts merge-side external neighbor repairs;
// [15] counts two-node merge groups;
// [16] counts four-node merge groups;
// [17] captures the frame-start dynamic OCBT root;
// validation compares group partitions and occupancy deltas without a readback stall.
// The CPU mirror consumes the same word order through CbtGpuAbi.shared.h.
[numthreads(64, 1, 1)]
void CSValidateF(uint physicalSlot : SV_DispatchThreadID)
{
    // 全槽扫描同时验证动态 OCBT 位与 heapID 活性，以及 next 邻接互反关系。
    // INVALID 邻接表示开放边界；其他邻接必须在范围内并指向活动槽。
    // 槽零额外核对 Reduce root、Indexation、draw args 和 E3 计数守恒。
    if (physicalSlot >= CbtTotalElementCount)
    {
        return;
    }

    const bool active = CbtHeapIds[physicalSlot] != 0u;
    if (physicalSlot < CbtDynamicElementCount)
    {
        const uint64_t word = CbtBitfield[physicalSlot / 64u];
        const bool occupied = (word & (uint64_t(1) << (physicalSlot % 64u))) != 0u;
        if (active != occupied)
        {
            CbtSetValidationError(22u, physicalSlot);
            return;
        }
    }
    if (active)
    {
        const uint3 neighbors = CbtLoadNextNeighbors(physicalSlot);
        [unroll]
        for (uint edge = 0u; edge < 3u; ++edge)
        {
            const uint neighbor = neighbors[edge];
            if (neighbor == CbtInvalidIndex)
            {
                continue;
            }
            if (neighbor >= CbtTotalElementCount || CbtHeapIds[neighbor] == 0u)
            {
                CbtSetValidationError(23u, physicalSlot);
                return;
            }
            const uint3 reverse = CbtLoadNextNeighbors(neighbor);
            if (reverse.x != physicalSlot && reverse.y != physicalSlot && reverse.z != physicalSlot)
            {
                CbtSetValidationError(24u, physicalSlot);
                return;
            }
        }
    }

    if (physicalSlot == 0u)
    {
        const uint dynamicActive = CbtReadTreeCount(1u);
        const uint indexedActive = CbtDrawState[CbtDrawActiveVertexCountWord] / 3u;
        const uint expectedDynamic =
            CbtValidation[17] + CbtValidation[6] - CbtValidation[13];
        if (indexedActive != dynamicActive + CbtBaseElementCount ||
            CbtDrawState[CbtDrawActiveBisectorCountWord] != indexedActive)
        {
            CbtSetValidationError(40u, physicalSlot);
        }
        else if (uint(CbtMemory[0]) != CbtValidation[6])
        {
            CbtSetValidationError(41u, physicalSlot);
        }
        else if (uint(CbtPropagation[0]) != CbtValidation[7])
        {
            CbtSetValidationError(42u, physicalSlot);
        }
        else if (CbtSimplification[0] != CbtValidation[12])
        {
            CbtSetValidationError(43u, physicalSlot);
        }
        else if (uint(CbtPropagation[1]) != CbtValidation[14])
        {
            CbtSetValidationError(44u, physicalSlot);
        }
        else if (CbtValidation[12] != CbtValidation[15] + CbtValidation[16])
        {
            CbtSetValidationError(45u, physicalSlot);
        }
        else if (CbtValidation[13] != CbtValidation[15] + 2u * CbtValidation[16])
        {
            CbtSetValidationError(46u, physicalSlot);
        }
        else if (dynamicActive != expectedDynamic)
        {
            CbtSetValidationError(47u, physicalSlot);
        }
        else if (uint(CbtAllocation[0]) !=
                 CbtValidation[8] + CbtValidation[9] + CbtValidation[10] + CbtValidation[11])
        {
            CbtSetValidationError(49u, physicalSlot);
        }
    }
}

[numthreads(64, 1, 1)]
void CSReducePre(uint dispatchThreadId : SV_DispatchThreadID)
{
    // 生产入口与 GPU 对照测试调用同一个 include 实现 防止算法副本漂移
    CbtReducePre(dispatchThreadId);
}

[numthreads(64, 1, 1)]
void CSReduceFirst(uint3 groupId : SV_GroupID, uint groupIndex : SV_GroupIndex)
{
    // 每个 group 独立归约一个固定大小 subtree
    CbtReduceFirst(groupId, groupIndex);
}

[numthreads(64, 1, 1)]
void CSReduceSecond(uint groupIndex : SV_GroupIndex)
{
    // 最后一组线程汇总所有 subtree 根并发布全局根计数
    CbtReduceSecond(groupIndex);
}
