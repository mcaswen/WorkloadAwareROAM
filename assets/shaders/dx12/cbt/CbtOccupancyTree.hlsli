#ifndef PARALLEL_ROAM_CBT_OCCUPANCY_TREE_HLSLI
#define PARALLEL_ROAM_CBT_OCCUPANCY_TREE_HLSLI

#ifndef CBT_CAPACITY
#error CBT_CAPACITY must select an OCBT capacity specialization
#endif

// 容量决定压缩树深度和槽位数量 因此四档必须分别编译 PSO
#if CBT_CAPACITY == 131072
static const uint CbtLeafDepth = 17;
static const uint CbtLastTreeDepth = 10;
static const uint CbtTreeSlotCount = 831;
#elif CBT_CAPACITY == 262144
static const uint CbtLeafDepth = 18;
static const uint CbtLastTreeDepth = 11;
static const uint CbtTreeSlotCount = 1599;
#elif CBT_CAPACITY == 524288
static const uint CbtLeafDepth = 19;
static const uint CbtLastTreeDepth = 12;
static const uint CbtTreeSlotCount = 3135;
#elif CBT_CAPACITY == 1048576
static const uint CbtLeafDepth = 20;
static const uint CbtLastTreeDepth = 13;
static const uint CbtTreeSlotCount = 6207;
#else
#error Unsupported CBT_CAPACITY
#endif

static const uint CbtElementCount = CBT_CAPACITY;
static const uint CbtBitfieldSlotCount = CbtElementCount / 64;
static const uint CbtLastTreeNodeCount = CbtElementCount / 128;
static const uint CbtSubtreeRootDepth = CbtLeafDepth - 14;
static const uint CbtSubtreeCount = CbtElementCount / 16384;

// 测试和生产拓扑管线共享同一份三级归约实现
// 两块 groupshared 数组分别容纳 128 叶 subtree 和最多 64 个 subtree 根
groupshared uint CbtReduceSubtree[255];
groupshared uint CbtReduceTopTree[127];

RWStructuredBuffer<uint> CbtTree : register(u0);
RWStructuredBuffer<uint64_t> CbtBitfield : register(u1);

uint CbtTreeElementWidth(uint depth)
{
    // 靠近根部的计数范围更大 深层节点用窄字段减少常驻显存
    if (depth < 7)
    {
        return 32;
    }
    return depth < CbtLastTreeDepth ? 16 : 8;
}

uint CbtTreeDepthOffsetBits(uint depth)
{
    // 各深度连续打包到同一 uint 流 偏移必须以位而不是槽位计算
    if (depth <= 7)
    {
        return 32 * ((1u << depth) - 1u);
    }

    // 前七层保留 32 位计数，后续非叶层使用 16 位计数
    return 32 * 127 + 16 * ((1u << depth) - 128u);
}

uint CbtReadTreeCount(uint heapId)
{
    // heap id 先解出深度和层内位置，再映射到压缩位流
    const uint depth = uint(firstbithigh(heapId));
    const uint width = CbtTreeElementWidth(depth);
    const uint element = heapId - (1u << depth);
    const uint firstBit = CbtTreeDepthOffsetBits(depth) + width * element;
    const uint slot = firstBit / 32;
    const uint localBit = firstBit % 32;
    const uint mask = width == 32 ? 0xffffffffu : ((1u << width) - 1u);
    return (CbtTree[slot] >> localBit) & mask;
}

void CbtWriteTreeCountAtomic(uint heapId, uint value)
{
    // 16 位和 8 位计数共享 uint 槽位，写入必须保护相邻字段
    const uint depth = uint(firstbithigh(heapId));
    const uint width = CbtTreeElementWidth(depth);
    const uint element = heapId - (1u << depth);
    const uint firstBit = CbtTreeDepthOffsetBits(depth) + width * element;
    const uint slot = firstBit / 32;
    const uint localBit = firstBit % 32;
    if (width == 32)
    {
        // 32 位层的节点独占完整槽位，归约调度保证单写者
        CbtTree[slot] = value;
        return;
    }

    // 同一 32 位槽位中的字段互不重叠，原子清位和置位可并发提交
    const uint valueMask = (1u << width) - 1u;
    const uint fieldMask = valueMask << localBit;
    InterlockedAnd(CbtTree[slot], ~fieldMask);
    InterlockedOr(CbtTree[slot], (value & valueMask) << localBit);
}

void CbtSetBitAtomic(uint bitIndex, bool occupied)
{
    // 并行更新只触碰原始位域，计数树由后续归约统一发布
    const uint slot = bitIndex / 64;
    const uint localBit = bitIndex % 64;
    const uint64_t mask = uint64_t(1) << localBit;
    if (occupied)
    {
        InterlockedOr(CbtBitfield[slot], mask);
    }
    else
    {
        InterlockedAnd(CbtBitfield[slot], ~mask);
    }
}

uint CbtSelectOne(uint64_t word, uint rank)
{
    [loop]
    for (uint bit = 0; bit < 64; ++bit)
    {
        if ((word & (uint64_t(1) << bit)) != 0)
        {
            if (rank == 0)
            {
                return bit;
            }
            --rank;
        }
    }
    return 0xffffffffu;
}

uint CbtDecodeBit(uint rank)
{
    // 每层比较左子树计数，选择右侧时扣除整棵左子树
    uint heapId = 1;
    for (uint depth = 0; depth < CbtLastTreeDepth; ++depth)
    {
        const uint leftCount = CbtReadTreeCount(heapId * 2);
        const uint selectRight = rank >= leftCount ? 1 : 0;
        heapId = heapId * 2 + selectRight;
        rank -= leftCount * selectRight;
    }

    const uint blockIndex = heapId - (1u << CbtLastTreeDepth);
    const uint bitfieldIndex = blockIndex * 2;
    // 树只定位到 128 位块，最终 rank 在两个 64 位 word 中解析
    const uint firstWordCount = countbits(CbtBitfield[bitfieldIndex]);
    if (rank < firstWordCount)
    {
        return blockIndex * 128 + CbtSelectOne(CbtBitfield[bitfieldIndex], rank);
    }
    return blockIndex * 128 + 64 + CbtSelectOne(CbtBitfield[bitfieldIndex + 1], rank - firstWordCount);
}

uint CbtDecodeBitComplement(uint rank)
{
    // 空闲选择复用活动计数树，通过节点容量减活动数得到补集计数
    uint heapId = 1;
    for (uint depth = 0; depth < CbtLastTreeDepth; ++depth)
    {
        const uint childCapacity = CbtElementCount >> (depth + 1);
        const uint leftFreeCount = childCapacity - CbtReadTreeCount(heapId * 2);
        const uint selectRight = rank >= leftFreeCount ? 1 : 0;
        heapId = heapId * 2 + selectRight;
        rank -= leftFreeCount * selectRight;
    }

    const uint blockIndex = heapId - (1u << CbtLastTreeDepth);
    const uint bitfieldIndex = blockIndex * 2;
    const uint64_t firstWord = ~CbtBitfield[bitfieldIndex];
    const uint firstWordCount = countbits(firstWord);
    if (rank < firstWordCount)
    {
        return blockIndex * 128 + CbtSelectOne(firstWord, rank);
    }
    return blockIndex * 128 + 64 + CbtSelectOne(~CbtBitfield[bitfieldIndex + 1], rank - firstWordCount);
}

void CbtReducePre(uint dispatchThreadId)
{
    // 每个线程把相邻两个 64 位 word 汇总为一个 128 位叶块计数
    if (dispatchThreadId >= CbtLastTreeNodeCount)
    {
        return;
    }

    const uint bitfieldIndex = dispatchThreadId * 2;
    const uint count = countbits(CbtBitfield[bitfieldIndex]) + countbits(CbtBitfield[bitfieldIndex + 1]);
    CbtWriteTreeCountAtomic((1u << CbtLastTreeDepth) + dispatchThreadId, count);
}

void CbtReduceFirst(uint3 groupId, uint groupIndex)
{
    // 一个 group 在共享内存中构造 255 节点完整二叉树
    const uint subtreeIndex = groupId.x;
    if (subtreeIndex >= CbtSubtreeCount)
    {
        return;
    }

    const uint blockBase = subtreeIndex * 128;
    for (uint element = 0; element < 2; ++element)
    {
        const uint localLeaf = groupIndex + element * 64;
        CbtReduceSubtree[127 + localLeaf] =
            CbtReadTreeCount((1u << CbtLastTreeDepth) + blockBase + localLeaf);
    }
    GroupMemoryBarrierWithGroupSync();

    for (uint width = 64; width > 0; width >>= 1)
    {
        if (groupIndex < width)
        {
            const uint parentStart = width - 1;
            const uint childStart = width * 2 - 1;
            CbtReduceSubtree[parentStart + groupIndex] =
                CbtReduceSubtree[childStart + groupIndex * 2] +
                CbtReduceSubtree[childStart + groupIndex * 2 + 1];
        }
        GroupMemoryBarrierWithGroupSync();
    }

    // 只回写非叶节点 叶计数已经由 ReducePre 发布
    for (uint localIndex = groupIndex; localIndex < 127; localIndex += 64)
    {
        const uint localHeapId = localIndex + 1;
        const uint localDepth = uint(firstbithigh(localHeapId));
        const uint localElement = localHeapId - (1u << localDepth);
        const uint globalDepth = CbtSubtreeRootDepth + localDepth;
        const uint globalElement = subtreeIndex * (1u << localDepth) + localElement;
        CbtWriteTreeCountAtomic((1u << globalDepth) + globalElement, CbtReduceSubtree[localIndex]);
    }
}

void CbtReduceSecond(uint groupIndex)
{
    // 顶层树最大为 127 节点 单个 group 即可完成最终归约
    const uint leafStart = CbtSubtreeCount - 1;
    if (groupIndex < CbtSubtreeCount)
    {
        CbtReduceTopTree[leafStart + groupIndex] =
            CbtReadTreeCount((1u << CbtSubtreeRootDepth) + groupIndex);
    }
    GroupMemoryBarrierWithGroupSync();

    for (uint width = CbtSubtreeCount / 2; width > 0; width >>= 1)
    {
        if (groupIndex < width)
        {
            const uint parentStart = width - 1;
            const uint childStart = width * 2 - 1;
            CbtReduceTopTree[parentStart + groupIndex] =
                CbtReduceTopTree[childStart + groupIndex * 2] +
                CbtReduceTopTree[childStart + groupIndex * 2 + 1];
        }
        GroupMemoryBarrierWithGroupSync();
    }

    for (uint localIndex = groupIndex; localIndex < leafStart; localIndex += 64)
    {
        CbtWriteTreeCountAtomic(localIndex + 1, CbtReduceTopTree[localIndex]);
    }
    // 测试入口会立即读取根计数 因此在函数返回前发布 device 可见性
    DeviceMemoryBarrierWithGroupSync();
}

#endif
