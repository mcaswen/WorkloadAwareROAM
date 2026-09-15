#include "CbtOccupancyTree.hlsli"

struct CbtBitUpdate
{
    uint BitIndex;
    uint Occupied;
};

cbuffer CbtTestConstants : register(b0)
{
    uint UpdateOffset;
    uint UpdateCount;
    uint DecodeCount;
    uint ResultOffset;
};

StructuredBuffer<CbtBitUpdate> CbtUpdates : register(t0);
RWStructuredBuffer<uint> CbtResults : register(u2);

[numthreads(64, 1, 1)]
void CSClear(uint dispatchThreadId : SV_DispatchThreadID)
{
    // 同一入口按各自长度清空压缩树和位域
    if (dispatchThreadId < CbtTreeSlotCount)
    {
        CbtTree[dispatchThreadId] = 0;
    }
    if (dispatchThreadId < CbtBitfieldSlotCount)
    {
        CbtBitfield[dispatchThreadId] = 0;
    }
}

[numthreads(64, 1, 1)]
void CSApplyUpdates(uint dispatchThreadId : SV_DispatchThreadID)
{
    // 批内 bit index 唯一，跨批顺序由命令列表 UAV barrier 保证
    if (dispatchThreadId >= UpdateCount)
    {
        return;
    }

    const CbtBitUpdate update = CbtUpdates[UpdateOffset + dispatchThreadId];
    if (update.BitIndex < CbtElementCount)
    {
        CbtSetBitAtomic(update.BitIndex, update.Occupied != 0);
    }
}

[numthreads(64, 1, 1)]
void CSReducePre(uint dispatchThreadId : SV_DispatchThreadID)
{
    CbtReducePre(dispatchThreadId);
}

[numthreads(64, 1, 1)]
void CSReduceFirst(uint3 groupId : SV_GroupID, uint groupIndex : SV_GroupIndex)
{
    CbtReduceFirst(groupId, groupIndex);
}

[numthreads(64, 1, 1)]
void CSReduceSecond(uint groupIndex : SV_GroupIndex)
{
    CbtReduceSecond(groupIndex);

    if (groupIndex == 0)
    {
        // 根计数同时写入验证结果头部，避免额外读回压缩树
        CbtResults[0] = CbtReadTreeCount(1u);
    }
}

[numthreads(64, 1, 1)]
void CSDecodeOccupied(uint dispatchThreadId : SV_DispatchThreadID)
{
    if (dispatchThreadId < DecodeCount)
    {
        CbtResults[ResultOffset + dispatchThreadId] = CbtDecodeBit(dispatchThreadId);
    }
}

[numthreads(64, 1, 1)]
void CSDecodeFree(uint dispatchThreadId : SV_DispatchThreadID)
{
    if (dispatchThreadId < DecodeCount)
    {
        CbtResults[ResultOffset + dispatchThreadId] = CbtDecodeBitComplement(dispatchThreadId);
    }
}
