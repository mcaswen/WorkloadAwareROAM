#include "algorithms/greedy_transactional_lod/TransactionalRejectionHints.h"

#include "profiling/CpuProfiling.h"

namespace ParallelRoam::Algorithms::GreedyTransactionalLod
{
std::optional<Slot> TransactionalRejectionHints::Find(Identity root, char kind, std::size_t ordinal) const
{
    const auto found = _positions.find(Key{root, kind, ordinal});
    if (found == _positions.end())
    {
        return std::nullopt;
    }
    return _entries[found->second].Sample;
}

void TransactionalRejectionHints::Remember(const std::vector<RejectionHint>& hints, WorkLedger& work)
{
    ROAM_CPU_ZONE("gtp.height_hint_merge");
    const auto started = std::chrono::steady_clock::now();
    for (const auto& hint : hints)
    {
        if (hint.Sample == InvalidSlot)
        {
            continue;
        }
        work.CheckLimit();
        const Key key{hint.Root, hint.Kind, hint.Ordinal};
        auto found = _positions.find(key);
        ++work.Reasons["height_hint_remember"];
        if (found != _positions.end())
        {
            // 更新值不刷新年龄，避免线程完成时序改变FIFO淘汰
            _entries[found->second] = hint;
            continue;
        }
        const bool full = _positions.size() == Capacity;
        const auto index = full ? _next : _positions.size();
        if (full)
        {
            ++work.Reasons["height_hint_evictions"];
        }
        // 唯一可能分配的插入先完成，之后只有无分配的索引和小记录替换
        _positions.emplace(key, index);
        if (full)
        {
            const auto& old = _entries[index];
            _positions.erase(Key{old.Root, old.Kind, old.Ordinal});
            _next = (_next + 1) % Capacity;
        }
        _entries[index] = hint;
    }
    work.Reasons["height_hint_entries"] = _positions.size();
    work.Seconds["height_hint_merge"] +=
        std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count();
}
}
