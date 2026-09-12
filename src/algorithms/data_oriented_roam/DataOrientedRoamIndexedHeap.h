#pragma once

#include <cstddef>

namespace ParallelRoam::Algorithms::DataOrientedRoam::IndexedHeap
{
// 访问和交换由调用方静态绑定；这里不持有堆，也不决定候选资格或代表关系
template<class Precedes, class Swap>
void SiftUp(std::size_t index, Precedes&& precedes, Swap&& swap)
{
    while (index > 0)
    {
        const auto parent = (index - 1U) / 2U;
        if (!precedes(index, parent)) return;
        swap(index, parent);
        index = parent;
    }
}

template<class Precedes, class Swap>
void SiftDown(std::size_t size, std::size_t index, Precedes&& precedes, Swap&& swap)
{
    for (;;)
    {
        const auto left = index * 2U + 1U;
        if (left >= size) return;
        const auto right = left + 1U;
        const auto best = right < size && precedes(right, left) ? right : left;
        if (!precedes(best, index)) return;
        swap(index, best);
        index = best;
    }
}

// 任意删除或分数更新后，先比较父节点以选择唯一修复方向
template<class Precedes, class Swap>
void Restore(std::size_t size, std::size_t index, Precedes&& precedes, Swap&& swap)
{
    if (index >= size) return;
    if (index > 0 && precedes(index, (index - 1U) / 2U)) SiftUp(index, precedes, swap);
    else SiftDown(size, index, precedes, swap);
}
}
