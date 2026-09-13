#pragma once

#include "algorithms/data_oriented_roam/materialization/NativeMaterializationTypes.h"
#include "algorithms/data_oriented_roam/materialization/NativePlanningPageIndex.h"

#include <algorithm>
#include <limits>
#include <stdexcept>
#include <type_traits>

namespace ParallelRoam::Algorithms::DataOrientedRoam::Materialization
{
/// <summary>
/// 调用内追加式整数覆盖表，记录独立于哈希或分页索引，值在连续存储中
/// 删除由业务的显式无效值表达；记录下标稳定，扩容后的引用和指针无效
/// </summary>
template<class Key, class Value, bool Paged = false>
class NativePlanningStorage
{
    static_assert(std::is_unsigned_v<Key> && std::is_integral_v<Key>);
public:
    /// <summary>
    /// 键与覆盖值同寿命，枚举仅访问实际创建的记录
    /// </summary>
    struct Record { Key first; Value second; };
    static constexpr std::size_t Missing = std::numeric_limits<std::size_t>::max();
    explicit NativePlanningStorage(bool diagnostics = false) : _diagnostics(diagnostics), _pages(diagnostics) {}
    NativePlanningStorage(const NativePlanningStorage&) = delete;
    NativePlanningStorage& operator=(const NativePlanningStorage&) = delete;

    [[nodiscard]] std::size_t size() const noexcept { return _records.size(); }
    [[nodiscard]] auto begin() const noexcept { return _records.begin(); }
    [[nodiscard]] auto end() const noexcept { return _records.end(); }
    [[nodiscard]] Value& At(std::size_t index) { return _records[index].second; }
    [[nodiscard]] const Value& At(std::size_t index) const { return _records[index].second; }
    [[nodiscard]] const Record& RecordAt(std::size_t index) const { return _records[index]; }

    [[nodiscard]] std::size_t Find(Key key) const
    {
        if constexpr (Paged)
        {
            const auto encoded = _pages.Find(key);
            return encoded ? static_cast<std::size_t>(encoded - 1U) : Missing;
        }
        if (_slots.empty()) { Observe(0); return Missing; }
        auto slot = Hash(key) & (_slots.size() - 1U);
        std::size_t probes = 1;
        while (_slots[slot].EncodedRecord != 0)
        {
            if (_slots[slot].KeyValue == key)
            {
                Observe(probes);
                return _slots[slot].EncodedRecord - 1U;
            }
            slot = (slot + 1U) & (_slots.size() - 1U);
            ++probes;
        }
        Observe(probes);
        return Missing;
    }

    std::size_t Ensure(Key key)
    {
        const auto found = Find(key);
        return found == Missing ? InsertMissing(key) : found;
    }

    // 仅在刚确认缺失且期间没有插入时调用，避免一次业务写重复搜索同一个键
    std::size_t InsertMissing(Key key)
    {
        if (_records.size() >= std::numeric_limits<std::uint32_t>::max())
            throw std::length_error("planning record encoding exhausted");
        std::uint32_t* pageSlot = nullptr;
        // 索引方式在实例化时决定，没有按案例或运行时负载选择另一条算法
        if constexpr (Paged)
            pageSlot = &_pages.Prepare(key, _records.capacity() * sizeof(Record));
        else if (_slots.empty() || _records.size() >= _slots.size() - _slots.size() / 4U)
            Rehash(Grow(_slots.size(), _slots.max_size()));
        if (_records.size() == _records.capacity())
        {
            // 页索引已经可能扩容，记录增长的峰值以当前索引容量重新计算
            const auto capacity = Grow(_records.capacity(), _records.max_size());
            Peak(Bytes() + capacity * sizeof(Record));
            _metrics.MovedRecords += _records.size();
            _records.reserve(capacity);
            ++_metrics.Allocations;
        }
        if constexpr (Paged)
        {
            // 页槽与记录向量分开持有，记录构造成功后才发布编码
            _records.push_back({key, {}});
            *pageSlot = static_cast<std::uint32_t>(_records.size());
            Peak(Bytes());
            return _records.size() - 1U;
        }
        auto slot = Hash(key) & (_slots.size() - 1U);
        std::size_t probes = 1;
        while (_slots[slot].EncodedRecord) { slot = (slot + 1U) & (_slots.size() - 1U); ++probes; }
        Observe(probes);
        // 先构造值再发布索引；分配异常交给调用级 RAII，不发布半个条目
        _records.push_back({key, {}});
        _slots[slot] = {key, static_cast<std::uint32_t>(_records.size())};
        Peak(Bytes());
        return _records.size() - 1U;
    }

    void Set(Key key, Value value) { const auto index = Ensure(key); At(index) = value; }
    [[nodiscard]] NativePlanningStorageMetrics Metrics() const
    {
        auto result = _metrics;
        result.Records = size(); result.RecordCapacity = _records.capacity(); result.IndexCapacity = _slots.capacity();
        result.RecordBytes = sizeof(Record); result.IndexBytes = sizeof(Slot); result.ReservedBytes = Bytes();
        if constexpr (Paged)
        {
            const auto index = _pages.Metrics();
            // 页初始化与目录搬移不是重散列，保留各自口径而不套用哈希次数
            result.PagedIndex = true;
            result.IndexCapacity = index.IndexCapacity; result.IndexBytes = index.IndexBytes;
            result.Allocations += index.Allocations; result.InitializedSlots += index.InitializedSlots;
            result.PeakReservedBytes = std::max(result.PeakReservedBytes, index.PeakReservedBytes);
            result.Lookups = index.Lookups; result.Probes = index.Probes; result.MaximumProbe = index.MaximumProbe;
            result.IndexPages = index.IndexPages; result.DirectorySize = index.DirectorySize;
            result.DirectoryCapacity = index.DirectoryCapacity; result.DirectoryInitialized = index.DirectoryInitialized;
            result.DirectoryMoved = index.DirectoryMoved;
        }
        return result;
    }

private:
    /// <summary>
    /// 编码零表示空槽，完整键域仍可使用；碰撞时不再跳转到大记录取键
    /// </summary>
    struct Slot { Key KeyValue{0}; std::uint32_t EncodedRecord{0}; };
    static std::size_t Grow(std::size_t capacity, std::size_t maximum)
    {
        if (capacity > maximum / 2U || maximum < 16U) throw std::length_error("planning storage capacity");
        return capacity == 0 ? 16U : capacity * 2U;
    }
    static std::size_t Hash(Key key)
    {
        // 混合高低位，不能假设物理节点下标或路径的低位分布均匀
        std::uint64_t value = static_cast<std::uint64_t>(key);
        value = (value ^ (value >> 30U)) * 0xbf58476d1ce4e5b9ULL;
        value = (value ^ (value >> 27U)) * 0x94d049bb133111ebULL;
        return static_cast<std::size_t>(value ^ (value >> 31U));
    }
    [[nodiscard]] std::size_t Bytes() const noexcept
    {
        if constexpr (Paged) return _records.capacity() * sizeof(Record) + _pages.Bytes();
        return _records.capacity() * sizeof(Record) + _slots.capacity() * sizeof(Slot);
    }
    void Peak(std::size_t bytes) { _metrics.PeakReservedBytes = std::max(_metrics.PeakReservedBytes, bytes); }
    void Observe(std::size_t probes) const
    {
        if (!_diagnostics) return;
        ++_metrics.Lookups; _metrics.Probes += probes;
        _metrics.MaximumProbe = std::max(_metrics.MaximumProbe, probes);
    }
    void Rehash(std::size_t capacity)
    {
        Peak(Bytes() + capacity * sizeof(Slot));
        std::vector<Slot> slots(capacity);
        // 空槽清零与全部已有索引搬移都按实际容量计费，没有扫描来源域
        _metrics.InitializedSlots += capacity;
        for (std::size_t index = 0; index < _records.size(); ++index)
        {
            auto slot = Hash(_records[index].first) & (capacity - 1U);
            std::size_t probes = 1;
            while (slots[slot].EncodedRecord) { slot = (slot + 1U) & (capacity - 1U); ++probes; }
            Observe(probes);
            slots[slot] = {_records[index].first, static_cast<std::uint32_t>(index + 1U)};
        }
        _metrics.RehashedRecords += _records.size();
        _slots.swap(slots);
        ++_metrics.Rehashes; ++_metrics.Allocations;
    }
    bool _diagnostics;
    std::vector<Record> _records;
    std::vector<Slot> _slots;
    // 未选中的索引保持空，不建立第二套键到记录映射
    NativePlanningPageIndex _pages;
    mutable NativePlanningStorageMetrics _metrics;
};
}
