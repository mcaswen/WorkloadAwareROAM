#pragma once

#include "algorithms/greedy_transactional_lod/TransactionalTypes.h"
#include <algorithm>
#include <optional>
#include <type_traits>

namespace ParallelRoam::Algorithms::GreedyTransactionalLod
{
/// <summary>
/// 连续槽保存完整候选尾部，有序块只在自身变脏时重建。
/// 查询合并块首而不维护全局树，键内稳定身份决定同分次序。
/// </summary>
template<class Key> class TransactionalPriorityIndex
{
    static constexpr std::size_t BlockSize=256;
    using Record=std::optional<Key>;
    static_assert(std::is_nothrow_move_assignable_v<Record>);
public:
    using Writes=std::map<Slot,Record>;
    /// <summary>
    /// 私有修复保存目标脏块；丢弃它不会改变当前候选或计数。
    /// 容量预留可以提前发生，逻辑发布不再分配。
    /// </summary>
    struct Repair
    {
        Writes Values;
        std::map<std::size_t,std::vector<Key>> Blocks;
        std::size_t Slots{},Count{};
    };
    /// <summary>
    /// 从完整槽记录建立有序块；调用方先构造私有目标，再发布所有权。
    /// </summary>
    void Rebuild(std::vector<Record> records,WorkLedger& work)
    {
        _records=std::move(records);_blocks.clear();_blocks.resize(BlockCount(_records.size()));_count=0;
        for (std::size_t block=0;block<_blocks.size();++block)
        {
            auto& keys=_blocks[block];
            for (auto i=block*BlockSize;i<std::min(_records.size(),(block+1)*BlockSize);++i)
            { ++work.IndexSlots;if (_records[i]) keys.push_back(*_records[i]); }
            Sort(keys,work);_count+=keys.size();
        }
    }
    /// <summary>
    /// 按去重槽写集构造目标脏块，容量变化也在准备阶段计费。
    /// </summary>
    Repair PrepareRepair(Writes writes,std::size_t slots,WorkLedger& work)
    {
        if (slots<_records.size()) throw std::runtime_error("前缀索引不能隐式截断存活槽");
        Repair result;result.Values=std::move(writes);result.Slots=slots;result.Count=_count;
        for (auto it=result.Values.begin();it!=result.Values.end();)
        {
            const auto block=it->first/BlockSize;
            std::array<Record,BlockSize> records{};
            for (std::size_t i=0;i<BlockSize;++i)
            {
                const auto slot=block*BlockSize+i;
                if (slot<_records.size()) { records[i]=_records[slot];++work.IndexSlots; }
            }
            while (it!=result.Values.end() && it->first/BlockSize==block)
            {
                if (it->first>=slots) throw std::runtime_error("前缀索引写入超出目标槽数");
                auto& old=records[it->first%BlockSize];
                result.Count-=static_cast<std::size_t>(old.has_value());
                result.Count+=static_cast<std::size_t>(it->second.has_value());old=it->second;++it;
            }
            auto& keys=result.Blocks[block];
            for (const auto& record : records) if (record) keys.push_back(*record);
            Sort(keys,work);
        }
        work.Reserve(_records,slots);work.Reserve(_blocks,BlockCount(slots));return result;
    }
    /// <summary>
    /// 只发布本索引最新状态上准备的修复；调用方保证中间没有另一次发布。
    /// </summary>
    void Publish(Repair&& repair) noexcept
    {
        _records.resize(repair.Slots);_blocks.resize(BlockCount(repair.Slots));
        for (auto& [slot,key] : repair.Values) _records[slot]=std::move(key);
        for (auto& [block,keys] : repair.Blocks) _blocks[block]=std::move(keys);
        _count=repair.Count;
    }
    std::size_t Count() const { return _count; }
    /// <summary>
    /// 合并所有非空块的开头，读取精确前缀；超长请求可用于完整诊断。
    /// </summary>
    std::vector<Key> Prefix(std::size_t limit,WorkLedger* work=nullptr) const
    {
        std::vector<Key> result;limit=std::min(limit,_count);result.reserve(limit);
        if (!limit) return result;
        struct Cursor { std::size_t Block,Offset; };
        std::vector<Cursor> heap;heap.reserve(_blocks.size());
        for (std::size_t block=0;block<_blocks.size();++block)
        {
            if (work) ++work->IndexQueryBlocks;
            if (!_blocks[block].empty()) heap.push_back({block,0});
        }
        const auto later=[&](const Cursor& a,const Cursor& b) {
            if (work) ++work->IndexComparisons;
            return _blocks[a.Block][a.Offset]>_blocks[b.Block][b.Offset];
        };
        std::make_heap(heap.begin(),heap.end(),later);
        while (result.size()<limit)
        {
            std::pop_heap(heap.begin(),heap.end(),later);auto item=heap.back();heap.pop_back();
            result.push_back(_blocks[item.Block][item.Offset]);
            if (++item.Offset<_blocks[item.Block].size())
            { heap.push_back(item);std::push_heap(heap.begin(),heap.end(),later); }
        }
        return result;
    }
private:
    static std::size_t BlockCount(std::size_t slots) { return (slots+BlockSize-1)/BlockSize; }
    static void Sort(std::vector<Key>& keys,WorkLedger& work)
    {
        ++work.IndexBlocks;
        std::sort(keys.begin(),keys.end(),[&](const Key& a,const Key& b) { ++work.IndexComparisons;return a<b; });
    }
    std::vector<Record> _records;
    std::vector<std::vector<Key>> _blocks;
    std::size_t _count{};
};
}
