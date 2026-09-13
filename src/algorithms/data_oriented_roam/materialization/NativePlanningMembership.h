#pragma once

#include "algorithms/data_oriented_roam/materialization/NativeMaterializationTypes.h"
#include "algorithms/data_oriented_roam/DataOrientedRoamState.h"

#include <algorithm>
#include <array>
#include <span>
#include <stdexcept>
#include <type_traits>

namespace ParallelRoam::Algorithms::DataOrientedRoam::Materialization
{
/// <summary>
/// 集中保存决策队列的四个成员字段，不持有活动资格、拓扑关系或网格状态
/// 旧节点首次写时复制成员投影，虚拟尾部独立增长；所有存储在调用内释放
/// </summary>
class NativePlanningMembership
{
public:
    /// <summary>
    /// 两堆的反向位置与合并菱形映射独立计数，不包含活动叶或内部节点位置
    /// </summary>
    enum class Field : std::size_t { Split, Merge, Representative, Partner };
    explicit NativePlanningMembership(const DataOrientedRoamState& source, bool diagnostics)
        : _source(std::span{source.NodeMembership}.first(source.Nodes.size())), _diagnostics(diagnostics) {}

    [[nodiscard]] std::uint32_t Get(DataOrientedRoamNodeIndex node, Field field) const
    {
        if (_diagnostics) { ++_metrics.Lookups; ++_metrics.Probes; _metrics.MaximumProbe = 1; }
        const auto index = static_cast<std::size_t>(field);
        if (node < _source.size())
        {
            if (_private) return _old[node].Values[index];
            const auto& source = _source[node];
            switch (field)
            {
            case Field::Split: return source.SplitQueuePosition;
            case Field::Merge: return source.MergeQueuePosition;
            case Field::Representative: return source.MergeQueueRepresentative;
            case Field::Partner: return source.MergeQueuePartner;
            }
        }
        const auto tail = static_cast<std::size_t>(node) - _source.size();
        return tail < _new.size() ? _new[tail].Values[index] : InvalidDataOrientedRoamNodeIndex;
    }

    void Set(DataOrientedRoamNodeIndex node, Field field, std::uint32_t value)
    {
        if (node == InvalidDataOrientedRoamNodeIndex) throw std::out_of_range("invalid planning membership identity");
        const auto index = static_cast<std::size_t>(field);
        if (node < _source.size()) CopySource();
        else
        {
            const auto required = static_cast<std::size_t>(node) - _source.size() + 1U;
            if (required > _new.size())
            {
                Reserve(_new, required);
                _metrics.InitializedSlots += required - _new.size();
                _new.resize(required);
            }
        }
        auto& row = node < _source.size() ? _old[node] : _new[node - _source.size()];
        // 触及列表与字段计数记录真实写覆盖，不把全域复制算成全部字段被逻辑修改
        if (row.Written == 0) { Reserve(_touched, _touched.size() + 1U); _touched.push_back(node); }
        if (!(row.Written & (1U << index))) ++_fieldRecords[index];
        row.Values[index] = value;
        row.Written |= static_cast<std::uint8_t>(1U << index);
    }

    [[nodiscard]] std::size_t Records(Field field) const { return _fieldRecords[static_cast<std::size_t>(field)]; }
    [[nodiscard]] std::span<const DataOrientedRoamNodeIndex> Touched() const { return _touched; }
    [[nodiscard]] bool WasWritten(DataOrientedRoamNodeIndex node, Field field) const
    {
        const auto& row = node < _source.size() ? _old[node] : _new[node - _source.size()];
        return (row.Written & (1U << static_cast<std::size_t>(field))) != 0;
    }
    [[nodiscard]] NativePlanningStorageMetrics Metrics() const
    {
        // 四字段共享行只记录一次字节量，字段曾写规模由 Records 单独给出
        auto result = _metrics;
        result.DenseIndex = true;
        result.Records = _old.size() + _new.size(); result.RecordCapacity = _old.capacity() + _new.capacity();
        result.RecordBytes = sizeof(Row);
        result.IndexCapacity = _touched.capacity(); result.IndexBytes = sizeof(DataOrientedRoamNodeIndex);
        result.ReservedBytes = Bytes();
        return result;
    }

private:
    /// <summary>
    /// 四字段共享一条缓存记录，Written 只标记本调用实际写过的字段
    /// 默认无效用于尚未入堆的虚拟节点，不能回退到另一节点的来源位置
    /// </summary>
    struct Row
    {
        std::array<std::uint32_t, 4> Values{InvalidDataOrientedRoamNodeIndex, InvalidDataOrientedRoamNodeIndex,
            InvalidDataOrientedRoamNodeIndex, InvalidDataOrientedRoamNodeIndex};
        std::uint8_t Written{0};
    };
    [[nodiscard]] std::size_t Bytes() const noexcept
    { return (_old.capacity() + _new.capacity()) * sizeof(Row) + _touched.capacity() * sizeof(DataOrientedRoamNodeIndex); }
    void Peak(std::size_t bytes) { _metrics.PeakReservedBytes = std::max(_metrics.PeakReservedBytes, bytes); }
    template<class Value>
    void Reserve(std::vector<Value>& values, std::size_t required)
    {
        // 新虚拟尾部和触及列表各自增长，不为追加一个节点搬移整个旧节点投影
        if (required <= values.capacity()) return;
        const auto capacity = std::max(required, values.capacity() + values.capacity() / 2U + 16U);
        Peak(Bytes() + capacity * sizeof(Value));
        if constexpr (std::is_same_v<Value, Row>) _metrics.MovedRecords += values.size();
        else _metrics.MovedIndexEntries += values.size();
        values.reserve(capacity);
        ++_metrics.Allocations;
    }
    void CopySource()
    {
        if (_private) return;
        // 只复制四个决策字段；活动成员位置继续由 View 独立读取
        Reserve(_old, _source.size());
        for (const auto& entry : _source)
            _old.push_back({{entry.SplitQueuePosition, entry.MergeQueuePosition,
                entry.MergeQueueRepresentative, entry.MergeQueuePartner}, 0});
        _metrics.SourceCopies = _source.size();
        _metrics.InitializedSlots += _source.size();
        _private = true;
    }
    std::span<const DataOrientedRoamNodeMembership> _source;
    bool _diagnostics, _private{false};
    std::vector<Row> _old, _new;
    std::vector<DataOrientedRoamNodeIndex> _touched;
    std::array<std::size_t, 4> _fieldRecords{};
    mutable NativePlanningStorageMetrics _metrics;
};
}
