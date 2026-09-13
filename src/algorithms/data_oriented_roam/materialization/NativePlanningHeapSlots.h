#pragma once

#include "algorithms/data_oriented_roam/materialization/NativeMaterializationTypes.h"

#include <algorithm>
#include <limits>
#include <stdexcept>

namespace ParallelRoam::Algorithms::DataOrientedRoam::Materialization
{
/// <summary>
/// 首次写入才复制决策堆，后续槽位直接寻址；不保存资格或维护堆序
/// 曾写标记与副本条目分开计量，完整来源复制不是稀疏覆盖
/// </summary>
class NativePlanningHeapSlots
{
public:
    [[nodiscard]] bool IsPrivate() const noexcept { return _private; }
    [[nodiscard]] NativePlanningQueueEntry At(std::size_t index) const { return _values[index]; }

    template<class Entries, class PathOf>
    void CopySource(const Entries& source, PathOf&& pathOf)
    {
        if (_private) throw std::logic_error("planning heap already copied");
        // 按字段转换两个原生 entry 类型，不能依赖同布局作别名访问
        Grow(source.size());
        for (const auto& entry : source) _values.push_back({entry.Score, entry.Node, pathOf(entry.Node)});
        _written.resize(source.size(), 0);
        _metrics.InitializedSlots += source.size();
        _metrics.SourceCopies = source.size();
        _private = true;
    }

    /// <summary>
    /// 修复前一次准备所需长度，过程中只搬移已有槽，不再重复执行增长分支
    /// 私有数组保留高水位，已截断槽再次使用仍须完整覆盖值
    /// </summary>
    void Prepare(std::size_t required)
    {
        if (!_private) throw std::logic_error("planning heap write before copy");
        if (required > _values.size())
        {
            if (required > static_cast<std::size_t>(InvalidDataOrientedRoamPosition))
                throw std::length_error("planning heap slot capacity");
            Grow(required);
            _metrics.InitializedSlots += required - _written.size();
            _values.resize(required);
            _written.resize(required, 0);
        }
    }

    /// <summary>
    /// 仅供已经 Prepare 的私有槽位，调用者保证下标合法且没有外部观察
    /// 热搬移保留首次写标记，减少边界检查不等于关闭写入计数
    /// </summary>
    bool SetPrepared(std::size_t index, NativePlanningQueueEntry value)
    {
        const bool first = _written[index] == 0;
        _values[index] = value;
        _written[index] = 1;
        return first;
    }

    [[nodiscard]] NativePlanningStorageMetrics Metrics() const
    {
        // 逻辑长度由 Queues 持有，这里报告实际高水位存储，不能按存活成员少报容量
        auto result = _metrics;
        result.DenseIndex = true;
        result.Records = _values.size(); result.RecordCapacity = _values.capacity();
        result.RecordBytes = sizeof(NativePlanningQueueEntry);
        result.IndexCapacity = _written.capacity(); result.IndexBytes = sizeof(std::uint8_t);
        result.ReservedBytes = Bytes();
        return result;
    }

private:
    [[nodiscard]] std::size_t Bytes() const noexcept
    { return _values.capacity() * sizeof(NativePlanningQueueEntry) + _written.capacity(); }
    void Peak(std::size_t bytes) { _metrics.PeakReservedBytes = std::max(_metrics.PeakReservedBytes, bytes); }
    void Grow(std::size_t required)
    {
        if (required == 0) return;
        if (required > _values.max_size() || required > _written.max_size())
            throw std::length_error("planning heap storage capacity");
        // 首次只按来源长度付费，后来采用正常几何增长，不按最终答案预留
        if (required > _values.capacity())
        {
            const auto capacity = _values.capacity() == 0 ? required
                : std::max(required, _values.capacity() + _values.capacity() / 2U + 1U);
            Peak(Bytes() + capacity * sizeof(NativePlanningQueueEntry));
            _metrics.MovedRecords += _values.size();
            _values.reserve(capacity);
            ++_metrics.Allocations;
        }
        if (required > _written.capacity())
        {
            // 标记随条目容量一起预留，两块内存在增长时可能分别形成峰值
            const auto capacity = _values.capacity();
            Peak(Bytes() + capacity);
            _metrics.MovedIndexEntries += _written.size();
            _written.reserve(capacity);
            ++_metrics.Allocations;
        }
        Peak(Bytes());
    }
    bool _private{false};
    std::vector<NativePlanningQueueEntry> _values;
    std::vector<std::uint8_t> _written;
    NativePlanningStorageMetrics _metrics;
};
}
