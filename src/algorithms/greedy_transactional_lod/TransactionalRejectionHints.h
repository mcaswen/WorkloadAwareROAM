#pragma once

#include "algorithms/greedy_transactional_lod/TransactionalTypes.h"

#include <optional>

namespace ParallelRoam::Algorithms::GreedyTransactionalLod
{
/// <summary>
/// 目录项的失败样本身份；只有重新检查当前几何才能用于拒绝
/// </summary>
struct RejectionHint
{
    Identity Root{};
    char Kind{};
    std::size_t Ordinal{};
    Slot Sample{InvalidSlot};
};

/// <summary>
/// 持续实例独占的有界提示表，读阶段不改替换次序
/// 归并按根和目录顺序发生，不持有几何、投影或质量证书
/// </summary>
class TransactionalRejectionHints
{
public:
    static constexpr std::size_t Capacity = 2048;
    std::optional<Slot> Find(Identity root, char kind, std::size_t ordinal) const;
    void Remember(const std::vector<RejectionHint>& hints, WorkLedger& work);
    std::size_t Size() const { return _positions.size(); }

private:
    using Key = std::tuple<Identity, char, std::size_t>;
    std::map<Key, std::size_t> _positions;
    std::array<RejectionHint, Capacity> _entries{};
    std::size_t _next{};
};
}
