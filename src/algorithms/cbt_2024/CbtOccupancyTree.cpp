#include "algorithms/cbt_2024/CbtOccupancyTree.h"

#include <algorithm>
#include <bit>
#include <stdexcept>

namespace ParallelRoam::Algorithms::Cbt2024
{
namespace
{
constexpr std::uint32_t FullWidthDepthCount = 7U;
constexpr std::uint32_t BitfieldBlockSize = 128U;
constexpr std::uint32_t ReductionSubtreeSize = 16384U;

std::uint32_t CapacityValue(CbtOccupancyCapacity capacity)
{
    return static_cast<std::uint32_t>(capacity);
}

std::uint32_t ComputeTreeDepthOffsetBits(std::uint32_t depth)
{
    // 层偏移按实际字段宽度累加，而不是按完整二叉树节点结构寻址
    // CPU 和 HLSL 必须使用同一公式才能共享压缩缓冲
    if (depth <= FullWidthDepthCount)
    {
        return 32U * ((1U << depth) - 1U);
    }

    // 深度 0 到 6 使用 32 位，后续非叶计数使用 16 位
    return 32U * ((1U << FullWidthDepthCount) - 1U) +
           16U * ((1U << depth) - (1U << FullWidthDepthCount));
}

void AppendSetBits(
    std::uint64_t word,
    std::uint32_t baseIndex,
    std::vector<std::uint32_t>& output)
{
    // 每次清除最低有效位，枚举成本只与结果数量相关
    while (word != 0U)
    {
        const std::uint32_t localIndex = static_cast<std::uint32_t>(std::countr_zero(word));
        output.push_back(baseIndex + localIndex);
        word &= word - 1U;
    }
}
} // namespace

CbtOccupancyLayout BuildCbtOccupancyLayout(CbtOccupancyCapacity capacity)
{
    const std::uint32_t elementCount = CapacityValue(capacity);
    if (elementCount < 2U || !std::has_single_bit(elementCount))
    {
        throw std::invalid_argument{"CBT occupancy capacity must be a supported power of two"};
    }

    // 官方布局把树顶七层保留为 32 位计数，中段压缩为 16 位
    // 最深计数层用 8 位统计 128 个占用位，并作为 GPU 分段归约的输入
    CbtOccupancyLayout layout{};
    layout.Capacity = capacity;
    layout.ElementCount = elementCount;
    layout.LeafDepth = static_cast<std::uint32_t>(std::countr_zero(elementCount));
    layout.LastTreeDepth = layout.LeafDepth - 7U;
    layout.LastTreeNodeCount = elementCount / BitfieldBlockSize;
    layout.BitfieldSlotCount = elementCount / 64U;
    layout.SubtreeRootDepth = layout.LeafDepth - 14U;
    layout.SubtreeCount = elementCount / ReductionSubtreeSize;

    const std::uint32_t treeBits =
        ComputeTreeDepthOffsetBits(layout.LastTreeDepth) + 8U * layout.LastTreeNodeCount;
    layout.TreeSlotCount = treeBits / 32U;
    return layout;
}

const char* CbtOccupancyCapacityName(CbtOccupancyCapacity capacity)
{
    switch (capacity)
    {
    case CbtOccupancyCapacity::Capacity128K:
        return "128K";
    case CbtOccupancyCapacity::Capacity256K:
        return "256K";
    case CbtOccupancyCapacity::Capacity512K:
        return "512K";
    case CbtOccupancyCapacity::Capacity1M:
        return "1M";
    }
    return "unknown";
}

CbtOccupancyTree::CbtOccupancyTree(CbtOccupancyCapacity capacity)
    : _layout(BuildCbtOccupancyLayout(capacity)),
      _packedTree(_layout.TreeSlotCount, 0U),
      _bitfield(_layout.BitfieldSlotCount, 0U)
{
}

const CbtOccupancyLayout& CbtOccupancyTree::Layout() const
{
    return _layout;
}

bool CbtOccupancyTree::SetBit(std::uint32_t bitIndex, bool occupied)
{
    if (bitIndex >= _layout.ElementCount)
    {
        return false;
    }

    // 位操作只修改原始占用域，计数树由显式 Reduce 统一重建
    const std::uint32_t slot = bitIndex / 64U;
    const std::uint32_t localIndex = bitIndex % 64U;
    const std::uint64_t mask = std::uint64_t{1U} << localIndex;
    if (occupied)
    {
        _bitfield[slot] |= mask;
    }
    else
    {
        _bitfield[slot] &= ~mask;
    }
    return true;
}

bool CbtOccupancyTree::GetBit(std::uint32_t bitIndex) const
{
    if (bitIndex >= _layout.ElementCount)
    {
        return false;
    }

    const std::uint32_t slot = bitIndex / 64U;
    const std::uint32_t localIndex = bitIndex % 64U;
    return (_bitfield[slot] & (std::uint64_t{1U} << localIndex)) != 0U;
}

void CbtOccupancyTree::Clear()
{
    std::fill(_packedTree.begin(), _packedTree.end(), 0U);
    std::fill(_bitfield.begin(), _bitfield.end(), 0U);
}

void CbtOccupancyTree::Reduce()
{
    // 每次从位域完整重建计数树，使参考实现不依赖增量更新顺序
    std::fill(_packedTree.begin(), _packedTree.end(), 0U);

    // 最深压缩层的每个 8 位计数覆盖两个 64 位位域槽位
    const std::uint32_t lastLevelHeapStart = 1U << _layout.LastTreeDepth;
    for (std::uint32_t blockIndex = 0U; blockIndex < _layout.LastTreeNodeCount; ++blockIndex)
    {
        const std::uint32_t bitfieldIndex = blockIndex * 2U;
        const std::uint32_t count =
            static_cast<std::uint32_t>(std::popcount(_bitfield[bitfieldIndex])) +
            static_cast<std::uint32_t>(std::popcount(_bitfield[bitfieldIndex + 1U]));
        SetHeapElement(lastLevelHeapStart + blockIndex, count);
    }

    // 上层计数自底向上归约，根计数最终等于全部活动位数量
    for (std::uint32_t depth = _layout.LastTreeDepth; depth-- > 0U;)
    {
        const std::uint32_t levelStart = 1U << depth;
        const std::uint32_t levelCount = 1U << depth;
        for (std::uint32_t element = 0U; element < levelCount; ++element)
        {
            const std::uint32_t heapId = levelStart + element;
            SetHeapElement(heapId, GetHeapElement(heapId * 2U) + GetHeapElement(heapId * 2U + 1U));
        }
    }
}

std::uint32_t CbtOccupancyTree::BitCount() const
{
    // Reduce 完成后根计数位于压缩流首槽
    return _packedTree.empty() ? 0U : _packedTree[0];
}

std::uint32_t CbtOccupancyTree::DecodeBit(std::uint32_t rank) const
{
    if (rank >= BitCount())
    {
        return InvalidCbtBitIndex;
    }

    // 每层用左子树计数判断 rank 落点，进入右子树时扣除整个左子树
    std::uint32_t heapId = 1U;
    for (std::uint32_t depth = 0U; depth < _layout.LastTreeDepth; ++depth)
    {
        const std::uint32_t leftCount = GetHeapElement(heapId * 2U);
        const bool selectRight = rank >= leftCount;
        heapId = heapId * 2U + static_cast<std::uint32_t>(selectRight);
        if (selectRight)
        {
            rank -= leftCount;
        }
    }

    // 压缩树只定位到 128 位块，最后在两个 64 位 word 中完成选择
    const std::uint32_t blockIndex = heapId - (1U << _layout.LastTreeDepth);
    const std::uint32_t bitfieldIndex = blockIndex * 2U;
    const std::uint32_t firstWordCount = static_cast<std::uint32_t>(std::popcount(_bitfield[bitfieldIndex]));
    if (rank < firstWordCount)
    {
        return blockIndex * BitfieldBlockSize + SelectOne(_bitfield[bitfieldIndex], rank);
    }

    return blockIndex * BitfieldBlockSize + 64U +
           SelectOne(_bitfield[bitfieldIndex + 1U], rank - firstWordCount);
}

std::uint32_t CbtOccupancyTree::DecodeBitComplement(std::uint32_t rank) const
{
    // 根节点只保存活动数，空闲总数由固定容量取补集得到
    const std::uint32_t freeCount = _layout.ElementCount - BitCount();
    if (rank >= freeCount)
    {
        return InvalidCbtBitIndex;
    }

    // 空闲 rank-select 不维护第二棵树，而是用节点容量减去活动计数
    std::uint32_t heapId = 1U;
    for (std::uint32_t depth = 0U; depth < _layout.LastTreeDepth; ++depth)
    {
        const std::uint32_t childCapacity = _layout.ElementCount >> (depth + 1U);
        const std::uint32_t leftFreeCount = childCapacity - GetHeapElement(heapId * 2U);
        const bool selectRight = rank >= leftFreeCount;
        heapId = heapId * 2U + static_cast<std::uint32_t>(selectRight);
        if (selectRight)
        {
            rank -= leftFreeCount;
        }
    }

    const std::uint32_t blockIndex = heapId - (1U << _layout.LastTreeDepth);
    const std::uint32_t bitfieldIndex = blockIndex * 2U;
    // 支持容量没有尾部无效 bit，word 取反即可直接选择空闲槽位
    const std::uint32_t firstWordCount = static_cast<std::uint32_t>(std::popcount(~_bitfield[bitfieldIndex]));
    if (rank < firstWordCount)
    {
        return blockIndex * BitfieldBlockSize + SelectOne(~_bitfield[bitfieldIndex], rank);
    }

    return blockIndex * BitfieldBlockSize + 64U +
           SelectOne(~_bitfield[bitfieldIndex + 1U], rank - firstWordCount);
}

std::vector<std::uint32_t> CbtOccupancyTree::ActiveIndices() const
{
    // 直接枚举位域作为验证真值，不通过待验证的 rank-select 反推结果
    std::vector<std::uint32_t> result;
    result.reserve(BitCount());
    for (std::uint32_t slot = 0U; slot < _layout.BitfieldSlotCount; ++slot)
    {
        AppendSetBits(_bitfield[slot], slot * 64U, result);
    }
    return result;
}

std::vector<std::uint32_t> CbtOccupancyTree::FreeIndices() const
{
    // 支持容量均为 64 的倍数，因此末尾 word 取反后不需要额外屏蔽
    std::vector<std::uint32_t> result;
    result.reserve(_layout.ElementCount - BitCount());
    for (std::uint32_t slot = 0U; slot < _layout.BitfieldSlotCount; ++slot)
    {
        AppendSetBits(~_bitfield[slot], slot * 64U, result);
    }
    return result;
}

const std::vector<std::uint32_t>& CbtOccupancyTree::PackedTree() const
{
    return _packedTree;
}

const std::vector<std::uint64_t>& CbtOccupancyTree::Bitfield() const
{
    return _bitfield;
}

std::uint32_t CbtOccupancyTree::TreeElementWidth(std::uint32_t depth) const
{
    // 根附近需要容纳大计数，越靠近位域越可以使用窄字段
    if (depth < FullWidthDepthCount)
    {
        return 32U;
    }
    return depth < _layout.LastTreeDepth ? 16U : 8U;
}

std::uint32_t CbtOccupancyTree::TreeDepthOffsetBits(std::uint32_t depth) const
{
    return ComputeTreeDepthOffsetBits(depth);
}

std::uint32_t CbtOccupancyTree::GetHeapElement(std::uint32_t heapId) const
{
    if (heapId == 0U)
    {
        return 0U;
    }

    const std::uint32_t depth = static_cast<std::uint32_t>(std::bit_width(heapId) - 1U);
    if (depth > _layout.LastTreeDepth)
    {
        return 0U;
    }

    // 一基 heap id 先映射到层内索引，再映射到连续压缩位流
    const std::uint32_t width = TreeElementWidth(depth);
    const std::uint32_t element = heapId - (1U << depth);
    const std::uint32_t firstBit = TreeDepthOffsetBits(depth) + width * element;
    const std::uint32_t slot = firstBit / 32U;
    const std::uint32_t localIndex = firstBit % 32U;
    const std::uint32_t mask = width == 32U ? 0xffffffffU : ((1U << width) - 1U);
    return (_packedTree[slot] >> localIndex) & mask;
}

void CbtOccupancyTree::SetHeapElement(std::uint32_t heapId, std::uint32_t value)
{
    // 各层起点和字段宽度都按 32 位槽对齐，单个计数不会跨槽写入
    // 先清除旧字段再写入新值，避免相邻压缩计数受到影响
    const std::uint32_t depth = static_cast<std::uint32_t>(std::bit_width(heapId) - 1U);
    const std::uint32_t width = TreeElementWidth(depth);
    const std::uint32_t element = heapId - (1U << depth);
    const std::uint32_t firstBit = TreeDepthOffsetBits(depth) + width * element;
    const std::uint32_t slot = firstBit / 32U;
    const std::uint32_t localIndex = firstBit % 32U;
    const std::uint32_t mask = width == 32U ? 0xffffffffU : ((1U << width) - 1U);
    _packedTree[slot] &= ~(mask << localIndex);
    _packedTree[slot] |= (value & mask) << localIndex;
}

std::uint32_t CbtOccupancyTree::SelectOne(std::uint64_t word, std::uint32_t rank) const
{
    // 调用方已经用树计数选中 word，此处只解析 word 内的局部 rank
    // 清除最低位可避免逐 bit 扫描空洞区域
    while (word != 0U)
    {
        const std::uint32_t localIndex = static_cast<std::uint32_t>(std::countr_zero(word));
        if (rank == 0U)
        {
            return localIndex;
        }
        --rank;
        word &= word - 1U;
    }
    return InvalidCbtBitIndex;
}
} // namespace ParallelRoam::Algorithms::Cbt2024
