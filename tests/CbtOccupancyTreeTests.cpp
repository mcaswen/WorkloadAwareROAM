#include "algorithms/cbt_2024/CbtOccupancyTree.h"

#include <array>
#include <cstdint>
#include <iostream>
#include <random>
#include <string>
#include <vector>

namespace
{
using ParallelRoam::Algorithms::Cbt2024::BuildCbtOccupancyLayout;
using ParallelRoam::Algorithms::Cbt2024::CbtOccupancyCapacity;
using ParallelRoam::Algorithms::Cbt2024::CbtOccupancyCapacityName;
using ParallelRoam::Algorithms::Cbt2024::CbtOccupancyTree;
using ParallelRoam::Algorithms::Cbt2024::InvalidCbtBitIndex;

bool Expect(bool condition, const std::string& message)
{
    if (!condition)
    {
        std::cerr << message << '\n';
    }
    return condition;
}

bool ValidateLayout()
{
    struct ExpectedLayout
    {
        CbtOccupancyCapacity Capacity;
        std::uint32_t LeafDepth;
        std::uint32_t LastTreeDepth;
        std::uint32_t TreeSlotCount;
        std::uint32_t BitfieldSlotCount;
        std::uint32_t SubtreeCount;
    };

    const std::array<ExpectedLayout, 4> expected{{
        {CbtOccupancyCapacity::Capacity128K, 17U, 10U, 831U, 2048U, 8U},
        {CbtOccupancyCapacity::Capacity256K, 18U, 11U, 1599U, 4096U, 16U},
        {CbtOccupancyCapacity::Capacity512K, 19U, 12U, 3135U, 8192U, 32U},
        {CbtOccupancyCapacity::Capacity1M, 20U, 13U, 6207U, 16384U, 64U},
    }};

    bool valid = true;
    for (const ExpectedLayout& item : expected)
    {
        const auto layout = BuildCbtOccupancyLayout(item.Capacity);
        valid &= Expect(layout.LeafDepth == item.LeafDepth, "OCBT leaf depth mismatch");
        valid &= Expect(layout.LastTreeDepth == item.LastTreeDepth, "OCBT tree depth mismatch");
        valid &= Expect(layout.TreeSlotCount == item.TreeSlotCount, "OCBT tree slot count mismatch");
        valid &= Expect(layout.BitfieldSlotCount == item.BitfieldSlotCount, "OCBT bitfield slot count mismatch");
        valid &= Expect(layout.SubtreeCount == item.SubtreeCount, "OCBT reduction subtree count mismatch");
    }
    return valid;
}

bool ValidateCapacity(CbtOccupancyCapacity capacity)
{
    CbtOccupancyTree tree{capacity};
    const std::uint32_t elementCount = tree.Layout().ElementCount;
    bool valid = true;

    tree.Reduce();
    valid &= Expect(tree.BitCount() == 0U, "empty OCBT count mismatch");
    valid &= Expect(tree.DecodeBit(0U) == InvalidCbtBitIndex, "empty OCBT occupied decode should fail");
    valid &= Expect(tree.DecodeBitComplement(0U) == 0U, "empty OCBT first free bit mismatch");
    valid &= Expect(
        tree.DecodeBitComplement(elementCount - 1U) == elementCount - 1U,
        "empty OCBT last free bit mismatch");

    const std::array<std::uint32_t, 8> boundaryBits{
        0U,
        1U,
        63U,
        64U,
        65U,
        127U,
        elementCount - 2U,
        elementCount - 1U,
    };
    for (const std::uint32_t bit : boundaryBits)
    {
        valid &= Expect(tree.SetBit(bit, true), "boundary set failed");
    }
    tree.Reduce();
    valid &= Expect(tree.BitCount() == boundaryBits.size(), "boundary OCBT count mismatch");
    for (std::uint32_t rank = 0U; rank < boundaryBits.size(); ++rank)
    {
        valid &= Expect(tree.DecodeBit(rank) == boundaryBits[rank], "boundary occupied rank mismatch");
    }

    tree.Clear();
    for (std::uint32_t bit = 0U; bit < elementCount; bit += 2U)
    {
        valid &= Expect(tree.SetBit(bit, true), "alternating set failed");
    }
    tree.Reduce();
    valid &= Expect(tree.BitCount() == elementCount / 2U, "alternating OCBT count mismatch");
    const std::array<std::uint32_t, 5> sampleRanks{0U, 1U, 31U, elementCount / 4U, elementCount / 2U - 1U};
    for (const std::uint32_t rank : sampleRanks)
    {
        valid &= Expect(tree.DecodeBit(rank) == rank * 2U, "alternating occupied rank mismatch");
        valid &= Expect(tree.DecodeBitComplement(rank) == rank * 2U + 1U, "alternating free rank mismatch");
    }

    tree.Clear();
    std::mt19937 random{0xCB72024U + elementCount};
    std::uniform_int_distribution<std::uint32_t> distribution{0U, elementCount - 1U};
    std::vector<std::uint8_t> expected(elementCount, 0U);
    for (std::uint32_t iteration = 0U; iteration < 12000U; ++iteration)
    {
        const std::uint32_t bit = distribution(random);
        const bool occupied = (iteration % 5U) != 0U;
        expected[bit] = occupied ? 1U : 0U;
        valid &= Expect(tree.SetBit(bit, occupied), "random update failed");
    }
    tree.Reduce();

    std::vector<std::uint32_t> expectedActive;
    std::vector<std::uint32_t> expectedFree;
    for (std::uint32_t bit = 0U; bit < elementCount; ++bit)
    {
        (expected[bit] != 0U ? expectedActive : expectedFree).push_back(bit);
    }
    valid &= Expect(tree.ActiveIndices() == expectedActive, "random active bitset mismatch");
    valid &= Expect(tree.FreeIndices() == expectedFree, "random free bitset mismatch");
    for (std::uint32_t rank = 0U; rank < expectedActive.size(); ++rank)
    {
        valid &= Expect(tree.DecodeBit(rank) == expectedActive[rank], "random occupied rank mismatch");
    }
    const std::array<std::uint32_t, 4> freeSampleRanks{
        0U,
        63U,
        static_cast<std::uint32_t>(expectedFree.size() / 2U),
        static_cast<std::uint32_t>(expectedFree.size() - 1U),
    };
    for (const std::uint32_t rank : freeSampleRanks)
    {
        valid &= Expect(tree.DecodeBitComplement(rank) == expectedFree[rank], "random free rank mismatch");
    }

    if (!valid)
    {
        std::cerr << "OCBT CPU validation failed for " << CbtOccupancyCapacityName(capacity) << '\n';
    }
    return valid;
}
} // namespace

int main()
{
    bool valid = ValidateLayout();
    const std::array<CbtOccupancyCapacity, 4> capacities{
        CbtOccupancyCapacity::Capacity128K,
        CbtOccupancyCapacity::Capacity256K,
        CbtOccupancyCapacity::Capacity512K,
        CbtOccupancyCapacity::Capacity1M,
    };
    for (const CbtOccupancyCapacity capacity : capacities)
    {
        valid &= ValidateCapacity(capacity);
    }
    return valid ? 0 : 1;
}
