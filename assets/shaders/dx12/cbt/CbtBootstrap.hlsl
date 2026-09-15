#include "CbtGpuAbi.hlsli"

// 八个 root constants 固定拓扑容量、高度图尺寸与世界缩放协议。
cbuffer CbtBootstrapConstants : register(b0)
{
    uint TotalElementCount;
    uint BaseElementOffset;
    uint BaseElementCount;
    float TerrainSize;
    uint BaseDepth;
    uint HeightMapWidth;
    uint HeightMapHeight;
    float HeightScale;
};

struct TerrainVertex
{
    // 52 字节布局由 structured buffer stride 和程序化 VS 共同解释
    float3 Position;
    float3 Normal;
    float2 TexCoord;
    float Height;
    float3 DebugColor;
    float DebugHighlight;
};

StructuredBuffer<uint64_t> HeapIds : register(t0);
StructuredBuffer<CbtBisectorData> BisectorData : register(t1);
// 基础控制点仅用于首次或 terrain size 变化时重建平面几何
StructuredBuffer<float3> BaseControlPoints : register(t2);
// 使用 Load 手动双线性采样，与 CPU HeightMap 的 clamp 和插值顺序一致。
Texture2D<float> HeightMapTexture : register(t3);
RWStructuredBuffer<uint> ActiveIndices : register(u0);
RWStructuredBuffer<uint> VisibleIndices : register(u1);
RWStructuredBuffer<uint> ModifiedIndices : register(u2);
RWStructuredBuffer<uint> DrawState : register(u3);
RWStructuredBuffer<uint> GeometryDispatch : register(u4);
RWStructuredBuffer<float3> ClassificationPositions : register(u5);
RWStructuredBuffer<TerrainVertex> RenderVertices : register(u6);
RWStructuredBuffer<uint> Validation : register(u7);

uint CbtBootstrapHeapDepth(uint64_t heapId)
{
    const uint high = uint(heapId >> 32u);
    return high != 0u ? uint(firstbithigh(high)) + 33u : uint(firstbithigh(uint(heapId))) + 1u;
}

[numthreads(64, 1, 1)]
void CSIndexation(uint physicalSlot : SV_DispatchThreadID)
{
    // heap id 为零表示未分配物理槽位 因此无需扫描 OCBT rank
    if (physicalSlot >= TotalElementCount || HeapIds[physicalSlot] == 0)
    {
        return;
    }

    uint vertexOffset;
    // draw 顶点数同时充当 active list 的三倍 append 游标
    InterlockedAdd(DrawState[CbtDrawActiveVertexCountWord], 3u, vertexOffset);
    ActiveIndices[vertexOffset / 3u] = physicalSlot;
    // HeapID 位长就是分类使用的 subdivision depth；与活动索引同一次全槽扫描归约。
    InterlockedMax(
        Validation[CbtValidationMaxActiveDepthWord],
        CbtBootstrapHeapDepth(HeapIds[physicalSlot]));

    // visible 和 modified 是 active 的逐级子集 只有对应标志存在才继续追加
    const uint flags = BisectorData[physicalSlot].Flags;
    if ((flags & CbtVisibleFlag) == 0)
    {
        return;
    }

    InterlockedAdd(DrawState[CbtDrawVisibleVertexCountWord], 3u, vertexOffset);
    VisibleIndices[vertexOffset / 3u] = physicalSlot;
    if ((flags & CbtModifiedFlag) == 0)
    {
        return;
    }

    uint positionOffset;
    InterlockedAdd(DrawState[CbtDrawModifiedPositionCountWord], 4u, positionOffset);
    ModifiedIndices[positionOffset / 4u] = physicalSlot;
}

[numthreads(1, 1, 1)]
void CSPrepareIndirect(uint dispatchThreadId : SV_DispatchThreadID)
{
    // draw state 已由所有 Indexation 线程完成写入 UAV barrier 保证这里读取稳定
    const uint activeCount = DrawState[CbtDrawActiveVertexCountWord] / 3u;
    // 三组 dispatch 分别覆盖活动槽位 活动四位置和本帧修改位置
    GeometryDispatch[CbtActiveDispatchOffsetWord + 0u] = (activeCount + 63u) / 64u;
    GeometryDispatch[CbtActiveDispatchOffsetWord + 1u] = 1u;
    GeometryDispatch[CbtActiveDispatchOffsetWord + 2u] = 1u;
    GeometryDispatch[CbtActivePositionDispatchOffsetWord + 0u] = (activeCount * 4u + 63u) / 64u;
    GeometryDispatch[CbtActivePositionDispatchOffsetWord + 1u] = 1u;
    GeometryDispatch[CbtActivePositionDispatchOffsetWord + 2u] = 1u;
    const uint modifiedCount = DrawState[CbtDrawModifiedPositionCountWord] / 4u;
    GeometryDispatch[CbtModifiedDispatchOffsetWord + 0u] = (modifiedCount + 63u) / 64u;
    GeometryDispatch[CbtModifiedDispatchOffsetWord + 1u] = 1u;
    GeometryDispatch[CbtModifiedDispatchOffsetWord + 2u] = 1u;
    // 显式活动数供下一帧分类协议使用 不参与当前 ExecuteIndirect
    DrawState[CbtDrawActiveBisectorCountWord] = activeCount;
}

float3 DebugColor(uint localBase)
{
    // 六个基础半边使用稳定颜色 便于观察 winding 和共享边
    static const float3 Colors[6] = {
        float3(0.08, 0.72, 0.62),
        float3(0.10, 0.52, 0.88),
        float3(0.45, 0.76, 0.24),
        float3(1.00, 0.58, 0.12),
        float3(0.90, 0.28, 0.24),
        float3(0.70, 0.34, 0.82)
    };
    return Colors[min(localBase, 5u)];
}

float CbtSampleHeight(float2 uv)
{
    // CPU reference clamps normalized coordinates before converting to texel space.
    // Integer Load keeps filtering independent from sampler and driver state.
    // The low/high pair deliberately collapses at the last row or column.
    // Interpolation remains horizontal-first, then vertical, matching HeightMap.
    const uint2 dimensions = max(uint2(HeightMapWidth, HeightMapHeight), 1u);
    const float2 pixel = saturate(uv) * float2(dimensions - 1u);
    const uint2 low = uint2(pixel);
    const uint2 high = min(low + 1u, dimensions - 1u);
    const float2 interpolation = pixel - float2(low);
    const float h00 = HeightMapTexture.Load(int3(low, 0));
    const float h10 = HeightMapTexture.Load(int3(uint2(high.x, low.y), 0));
    const float h01 = HeightMapTexture.Load(int3(uint2(low.x, high.y), 0));
    const float h11 = HeightMapTexture.Load(int3(high, 0));
    return lerp(lerp(h00, h10, interpolation.x), lerp(h01, h11, interpolation.x), interpolation.y);
}

float3 CbtTerrainPosition(float2 uv)
{
    const float height = CbtSampleHeight(uv);
    return float3(
        (uv.x - 0.5) * TerrainSize,
        height * HeightScale,
        (uv.y - 0.5) * TerrainSize);
}

float3 CbtTerrainNormal(float2 uv)
{
    // One texel in each axis is the common finite-difference footprint.
    // Boundary samples rely on CbtSampleHeight clamping instead of one-sided code.
    // Tangents include both terrain extent and height scale in world units.
    // cross(Z, X) preserves the renderer's positive-Y winding convention.
    // Degenerate one-pixel or zero-scale inputs fall back to a stable up vector.
    const float2 denominator = max(float2(HeightMapWidth - 1u, HeightMapHeight - 1u), 1.0);
    const float2 step = 1.0 / denominator;
    const float left = CbtSampleHeight(uv - float2(step.x, 0.0));
    const float right = CbtSampleHeight(uv + float2(step.x, 0.0));
    const float down = CbtSampleHeight(uv - float2(0.0, step.y));
    const float up = CbtSampleHeight(uv + float2(0.0, step.y));
    const float3 tangentX = float3(step.x * 2.0 * TerrainSize, (right - left) * HeightScale, 0.0);
    const float3 tangentZ = float3(0.0, (up - down) * HeightScale, step.y * 2.0 * TerrainSize);
    const float3 normal = cross(tangentZ, tangentX);
    return dot(normal, normal) <= 1.192092896e-7 ? float3(0.0, 1.0, 0.0) : normalize(normal);
}

TerrainVertex CbtBuildTerrainVertex(float3 control, uint localBase)
{
    const float2 uv = control.xz;
    const float height = CbtSampleHeight(uv);
    TerrainVertex vertex;
    vertex.Position = CbtTerrainPosition(uv);
    vertex.Normal = CbtTerrainNormal(uv);
    vertex.TexCoord = uv;
    vertex.Height = height;
    vertex.DebugColor = DebugColor(localBase);
    vertex.DebugHighlight = 1.0;
    return vertex;
}

[numthreads(64, 1, 1)]
void CSBuildBaseGeometry(uint localBase : SV_DispatchThreadID)
{
    // 基础半边数量很小 仍保留标准 64 线程入口以复用 dispatch helper
    if (localBase >= BaseElementCount)
    {
        return;
    }

    const uint physicalSlot = BaseElementOffset + localBase;
    float3 positions[3];
    TerrainVertex vertices[3];
    [unroll]
    for (uint localVertex = 0; localVertex < 3; ++localVertex)
    {
        const float3 control = BaseControlPoints[localBase * 3u + localVertex];
        const TerrainVertex vertex = CbtBuildTerrainVertex(control, localBase);
        positions[localVertex] = vertex.Position;
        // 子位置位于前三个容量平面 父位置单独位于第四个平面
        ClassificationPositions[physicalSlot * 3u + localVertex] = positions[localVertex];

        vertices[localVertex] = vertex;
    }

    // 基础深度不消费父位置 仍写入有限值以保证分类缓冲完全初始化
    ClassificationPositions[TotalElementCount * 3u + physicalSlot] =
        (HeapIds[physicalSlot] & 1u) == 0u ? positions[0] : positions[2];
    [unroll]
    for (uint outputVertex = 0; outputVertex < 3; ++outputVertex)
    {
        RenderVertices[physicalSlot * 3u + outputVertex] = vertices[outputVertex];
    }
}

void CbtSplitPlaneTriangle(inout float3 p0, inout float3 p1, inout float3 p2, bool rightChild)
{
    const float3 previous0 = p0;
    const float3 previous1 = p1;
    const float3 previous2 = p2;
    const float3 midpoint = (previous0 + previous2) * 0.5;
    // 与官方 LEB splitting matrix 的 bit=0/1 两种排列逐行等价。
    p0 = rightChild ? previous1 : previous2;
    p1 = midpoint;
    p2 = rightChild ? previous0 : previous1;
}

bool CbtEvaluatePlaneTriangle(
    uint64_t heapId,
    out uint localBase,
    out float3 child0,
    out float3 child1,
    out float3 child2,
    out float3 parent0,
    out float3 parent1,
    out float3 parent2)
{
    localBase = 0u;
    child0 = child1 = child2 = 0.0;
    parent0 = parent1 = parent2 = 0.0;
    const uint depth = CbtBootstrapHeapDepth(heapId);
    if (heapId == 0u || depth < BaseDepth)
    {
        return false;
    }

    const uint subtreeDepth = depth - BaseDepth;
    const uint64_t firstBaseHeapId = uint64_t(1) << (BaseDepth - 1u);
    const uint64_t baseHeapId = heapId >> subtreeDepth;
    if (baseHeapId < firstBaseHeapId || baseHeapId - firstBaseHeapId >= BaseElementCount)
    {
        return false;
    }
    localBase = uint(baseHeapId - firstBaseHeapId);
    child0 = BaseControlPoints[localBase * 3u + 0u];
    child1 = BaseControlPoints[localBase * 3u + 1u];
    child2 = BaseControlPoints[localBase * 3u + 2u];
    parent0 = child0;
    parent1 = child1;
    parent2 = child2;
    [loop]
    for (uint remaining = subtreeDepth; remaining > 0u; --remaining)
    {
        parent0 = child0;
        parent1 = child1;
        parent2 = child2;
        const uint bit = remaining - 1u;
        CbtSplitPlaneTriangle(child0, child1, child2, ((heapId >> bit) & 1u) != 0u);
    }
    return true;
}

float3 CbtControlToWorld(float3 control)
{
    return CbtTerrainPosition(control.xz);
}

void CbtBuildGeometryForSlot(uint physicalSlot)
{
    // A physical slot owns three render vertices and three child classification points.
    // Its fourth classification value lives in a separate parent plane after all children.
    // Reconstructing from heapID avoids any CPU vertex upload on ordinary frames.
    if (physicalSlot >= TotalElementCount || HeapIds[physicalSlot] == 0u)
    {
        return;
    }

    uint localBase;
    float3 child0;
    float3 child1;
    float3 child2;
    float3 parent0;
    float3 parent1;
    float3 parent2;
    const uint64_t heapId = HeapIds[physicalSlot];
    if (!CbtEvaluatePlaneTriangle(
            heapId,
            localBase,
            child0,
            child1,
            child2,
            parent0,
            parent1,
            parent2))
    {
        return;
    }

    const float3 childControl[3] = {child0, child1, child2};
    [unroll]
    for (uint vertexIndex = 0u; vertexIndex < 3u; ++vertexIndex)
    {
        const float3 control = childControl[vertexIndex];
        const TerrainVertex vertex = CbtBuildTerrainVertex(control, localBase);
        const float3 position = vertex.Position;
        ClassificationPositions[physicalSlot * 3u + vertexIndex] = position;
        RenderVertices[physicalSlot * 3u + vertexIndex] = vertex;
    }

    // 分类父面积只需要与当前最长边相对的旧父顶点。
    const float3 parentControl = (heapId & 1u) == 0u ? parent0 : parent2;
    ClassificationPositions[TotalElementCount * 3u + physicalSlot] =
        CbtControlToWorld(parentControl);
}

[numthreads(64, 1, 1)]
void CSBuildActiveGeometry(uint activeOrdinal : SV_DispatchThreadID)
{
    // 参数域变化时必须在 Classify 前重建上一代所有活动槽，不能只更新六个基础槽。
    const uint activeCount = DrawState[9];
    if (activeOrdinal >= activeCount)
    {
        return;
    }
    CbtBuildGeometryForSlot(ActiveIndices[activeOrdinal]);
}

[numthreads(64, 1, 1)]
void CSBuildModifiedGeometry(uint modifiedOrdinal : SV_DispatchThreadID)
{
    const uint modifiedCount = DrawState[8] / 4u;
    if (modifiedOrdinal >= modifiedCount)
    {
        return;
    }
    CbtBuildGeometryForSlot(ModifiedIndices[modifiedOrdinal]);
}

void CbtSetGeometryValidationError(uint code, uint physicalSlot)
{
    uint previousCode;
    InterlockedCompareExchange(Validation[0], 0u, code, previousCode);
    if (previousCode == 0u || previousCode == code)
    {
        InterlockedMin(Validation[1], physicalSlot);
    }
}

bool CbtFinite(float value)
{
    return value == value && abs(value) <= 3.402823466e+38;
}

bool CbtFinite3(float3 value)
{
    return CbtFinite(value.x) && CbtFinite(value.y) && CbtFinite(value.z);
}

[numthreads(64, 1, 1)]
void CSValidateGeometryG(uint activeOrdinal : SV_DispatchThreadID)
{
    // Validation is dispatched from the same compact count consumed by rendering.
    // Rebuilding expected values in shader detects stale modified-list publication.
    // Error codes separate identity, finite-value, position, attribute, winding,
    // and parent-classification failures without copying full-capacity buffers.
    const uint activeCount = DrawState[CbtDrawActiveBisectorCountWord];
    if (activeOrdinal >= activeCount)
    {
        return;
    }
    const uint physicalSlot = ActiveIndices[activeOrdinal];
    if (physicalSlot >= TotalElementCount || HeapIds[physicalSlot] == 0u)
    {
        CbtSetGeometryValidationError(50u, physicalSlot);
        return;
    }

    uint localBase;
    float3 child0;
    float3 child1;
    float3 child2;
    float3 parent0;
    float3 parent1;
    float3 parent2;
    const uint64_t heapId = HeapIds[physicalSlot];
    if (!CbtEvaluatePlaneTriangle(
            heapId,
            localBase,
            child0,
            child1,
            child2,
            parent0,
            parent1,
            parent2))
    {
        CbtSetGeometryValidationError(51u, physicalSlot);
        return;
    }

    const float3 controls[3] = {child0, child1, child2};
    float3 positions[3];
    [unroll]
    for (uint vertexIndex = 0u; vertexIndex < 3u; ++vertexIndex)
    {
        const TerrainVertex actual = RenderVertices[physicalSlot * 3u + vertexIndex];
        const TerrainVertex expected = CbtBuildTerrainVertex(controls[vertexIndex], localBase);
        const float3 classification = ClassificationPositions[physicalSlot * 3u + vertexIndex];
        positions[vertexIndex] = actual.Position;
        if (!CbtFinite3(actual.Position) || !CbtFinite3(actual.Normal) ||
            !CbtFinite(actual.Height) || !CbtFinite3(classification))
        {
            CbtSetGeometryValidationError(52u, physicalSlot);
            return;
        }
        if (distance(actual.Position, expected.Position) > 1.0e-4 ||
            distance(classification, expected.Position) > 1.0e-4 ||
            abs(actual.Height - expected.Height) > 1.0e-5)
        {
            CbtSetGeometryValidationError(53u, physicalSlot);
            return;
        }
        if (distance(actual.Normal, expected.Normal) > 1.0e-4 ||
            abs(length(actual.Normal) - 1.0) > 1.0e-3 ||
            any(abs(actual.TexCoord - controls[vertexIndex].xz) > 1.0e-6))
        {
            CbtSetGeometryValidationError(54u, physicalSlot);
            return;
        }
    }

    const float3 faceNormal = cross(positions[1] - positions[0], positions[2] - positions[0]);
    if (!CbtFinite3(faceNormal) || faceNormal.y <= 0.0)
    {
        CbtSetGeometryValidationError(55u, physicalSlot);
        return;
    }
    const float3 parentControl = (heapId & 1u) == 0u ? parent0 : parent2;
    const float3 expectedParent = CbtControlToWorld(parentControl);
    const float3 actualParent = ClassificationPositions[TotalElementCount * 3u + physicalSlot];
    if (!CbtFinite3(actualParent) || distance(actualParent, expectedParent) > 1.0e-4)
    {
        CbtSetGeometryValidationError(56u, physicalSlot);
    }
}
