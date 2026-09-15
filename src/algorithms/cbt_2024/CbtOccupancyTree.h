#pragma once

#include <cstddef>
#include <cstdint>
#include <limits>
#include <vector>

namespace ParallelRoam::Algorithms::Cbt2024
{
inline constexpr std::uint32_t InvalidCbtBitIndex = std::numeric_limits<std::uint32_t>::max();

/// <summary>
/// 官方 OCBT 内存池支持的静态容量
/// </summary>
enum class CbtOccupancyCapacity : std::uint32_t
{
    Capacity128K = 131072U,
    Capacity256K = 262144U,
    Capacity512K = 524288U,
    Capacity1M = 1048576U,
};

/// <summary>
/// CPU 参考实现和 GPU dispatch 共享的 OCBT 容量特化压缩布局
/// </summary>
struct CbtOccupancyLayout
{
    CbtOccupancyCapacity Capacity{CbtOccupancyCapacity::Capacity128K};
    std::uint32_t ElementCount{0U};
    std::uint32_t LeafDepth{0U};
    std::uint32_t LastTreeDepth{0U};
    std::uint32_t TreeSlotCount{0U};
    std::uint32_t BitfieldSlotCount{0U};
    std::uint32_t LastTreeNodeCount{0U};
    std::uint32_t SubtreeRootDepth{0U};
    std::uint32_t SubtreeCount{0U};
};

/// <summary>
/// CPU 参考 OCBT，用于验证压缩布局、计数归约和 rank-select
/// </summary>
class CbtOccupancyTree
{
public:
    explicit CbtOccupancyTree(CbtOccupancyCapacity capacity);

    [[nodiscard]] const CbtOccupancyLayout& Layout() const;
    [[nodiscard]] bool SetBit(std::uint32_t bitIndex, bool occupied);
    [[nodiscard]] bool GetBit(std::uint32_t bitIndex) const;
    void Clear();
    void Reduce();

    [[nodiscard]] std::uint32_t BitCount() const;

    // rank 超出有效范围时返回 InvalidCbtBitIndex
    [[nodiscard]] std::uint32_t DecodeBit(std::uint32_t rank) const;
    [[nodiscard]] std::uint32_t DecodeBitComplement(std::uint32_t rank) const;

    [[nodiscard]] std::vector<std::uint32_t> ActiveIndices() const;
    [[nodiscard]] std::vector<std::uint32_t> FreeIndices() const;
    [[nodiscard]] const std::vector<std::uint32_t>& PackedTree() const;
    [[nodiscard]] const std::vector<std::uint64_t>& Bitfield() const;

private:
    [[nodiscard]] std::uint32_t TreeElementWidth(std::uint32_t depth) const;
    [[nodiscard]] std::uint32_t TreeDepthOffsetBits(std::uint32_t depth) const;
    [[nodiscard]] std::uint32_t GetHeapElement(std::uint32_t heapId) const;
    void SetHeapElement(std::uint32_t heapId, std::uint32_t value);
    [[nodiscard]] std::uint32_t SelectOne(std::uint64_t word, std::uint32_t rank) const;

    CbtOccupancyLayout _layout{};
    std::vector<std::uint32_t> _packedTree;
    std::vector<std::uint64_t> _bitfield;
};

[[nodiscard]] CbtOccupancyLayout BuildCbtOccupancyLayout(CbtOccupancyCapacity capacity);
[[nodiscard]] const char* CbtOccupancyCapacityName(CbtOccupancyCapacity capacity);
} // namespace ParallelRoam::Algorithms::Cbt2024
