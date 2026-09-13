#pragma once

#include "algorithms/data_oriented_roam/materialization/NativeMaterializationTypes.h"
#include "algorithms/data_oriented_roam/materialization/NativePlanningStorage.h"
#include "algorithms/data_oriented_roam/DataOrientedRoamState.h"

#include <array>
#include <initializer_list>
#include <set>
#include <stdexcept>
#include <vector>

namespace ParallelRoam::Algorithms::DataOrientedRoam::Materialization
{
/// <summary>
/// 借用原生输入并覆盖决策所需字段，既不持有完整目标状态，也不修改来源
/// 虚拟孩子只有逻辑身份和求值数据，活动数组、网格及生产槽位由物化阶段负责
/// </summary>
class NativePlanningView
{
public:
    /// <summary>
    /// 仅供无写入区间内读取同一节点的多个字段，任何 View 修改后都须重新取得
    /// 保存稳定记录下标而非元素指针；字段读取仍分别核算来源与私有访问
    /// </summary>
    class ReadCursor
    {
    public:
        template<NativePlanningField Selected>
        [[nodiscard]] std::uint64_t Read() const { return _view.ReadAt<Selected>(_node, _record); }
    private:
        friend class NativePlanningView;
        ReadCursor(const NativePlanningView& view, DataOrientedRoamNodeIndex node, std::size_t record)
            : _view(view), _node(node), _record(record) {}
        const NativePlanningView& _view;
        DataOrientedRoamNodeIndex _node;
        std::size_t _record;
    };
    explicit NativePlanningView(const DataOrientedRoamState& source, bool collectReadCoverage = false,
        NativePlanningCosts* costs = nullptr);
    NativePlanningView(const NativePlanningView&) = delete;
    NativePlanningView& operator=(const NativePlanningView&) = delete;

    // 来源须在整个同步规划期间保持有效且无人修改，视图不延长高度图或线程池寿命
    [[nodiscard]] const DataOrientedRoamState& Source() const noexcept { return _source; }
    [[nodiscard]] NativePlanningCosts* Costs() const noexcept { return _costs; }
    [[nodiscard]] bool CollectStorageDiagnostics() const noexcept { return _collectReadCoverage || (_costs && _costs->Detailed); }
    [[nodiscard]] std::size_t NodeCount() const noexcept { return _source.Nodes.size() + _virtualNodes.size(); }
    [[nodiscard]] bool IsValidNode(DataOrientedRoamNodeIndex node) const noexcept { return node < NodeCount(); }
    [[nodiscard]] std::uint64_t Read(DataOrientedRoamNodeIndex node, NativePlanningField field) const;
    // 固定字段形成独立实例，避免热路径仍调用含全部字段映射的通用分派
    template<NativePlanningField Selected>
    [[nodiscard]] std::uint64_t Read(DataOrientedRoamNodeIndex node) const
    {
        return ReadAt<Selected>(node, _overrides.Find(node));
    }
    [[nodiscard]] ReadCursor Inspect(DataOrientedRoamNodeIndex node) const
    { return ReadCursor{*this, node, _overrides.Find(node)}; }
    // 净出口比较基态默认值时也经由计费入口，不能用直接数组读隐藏提取成本
    [[nodiscard]] std::uint64_t Initial(DataOrientedRoamNodeIndex node, NativePlanningField field) const;
    void Write(DataOrientedRoamNodeIndex node, NativePlanningField field, std::uint64_t value);
    // 仅合并调用点本来相邻的赋值，逐字段过滤和校验不变，不承诺部分异常后的可续用
    void WriteFields(DataOrientedRoamNodeIndex node,
        std::initializer_list<std::pair<NativePlanningField, std::uint64_t>> values);
    [[nodiscard]] DataOrientedRoamNodeIndex Relation(DataOrientedRoamNodeIndex node, NativePlanningField field) const;
    template<NativePlanningField Selected>
    [[nodiscard]] DataOrientedRoamNodeIndex Relation(DataOrientedRoamNodeIndex node) const
    {
        static_assert(Selected <= NativePlanningField::RightNeighbor);
        return static_cast<DataOrientedRoamNodeIndex>(Read<Selected>(node));
    }
    [[nodiscard]] NativePlanningActivity Activity(DataOrientedRoamNodeIndex node) const;
    [[nodiscard]] std::uint64_t Path(DataOrientedRoamNodeIndex node) const;
    [[nodiscard]] DataOrientedRoamNodeIndex Parent(DataOrientedRoamNodeIndex node) const;
    [[nodiscard]] int Depth(DataOrientedRoamNodeIndex node) const;
    [[nodiscard]] TriangleDomain Domain(DataOrientedRoamNodeIndex node) const;
    [[nodiscard]] float GeometricError(DataOrientedRoamNodeIndex node) const;
    [[nodiscard]] std::uint8_t VarianceTree(DataOrientedRoamNodeIndex node) const;
    [[nodiscard]] std::size_t VarianceIndex(DataOrientedRoamNodeIndex node) const;
    [[nodiscard]] std::uint64_t CreatedBuild(DataOrientedRoamNodeIndex node) const;

    /// <summary>
    /// 沿两个根的已缓存父子链查找身份，不扫描全池；缺失身份返回无效节点
    /// </summary>
    [[nodiscard]] DataOrientedRoamNodeIndex FindPath(std::uint64_t path) const;

    /// <summary>
    /// 仅在两个孩子都未缓存时创建一对虚拟记录，更新私有父记录而非生产数组
    /// 调用方随后决定活动资格和邻接，不能把创建等同于成功细分
    /// </summary>
    [[nodiscard]] std::array<DataOrientedRoamNodeIndex, 2> CreateChildren(DataOrientedRoamNodeIndex parent);
    [[nodiscard]] bool TryReserveBudget();
    void ReleaseBudget();
    [[nodiscard]] std::size_t RemainingBudget() const noexcept { return _remainingBudget; }
    [[nodiscard]] std::vector<DataOrientedRoamNodeIndex> TouchedNodes() const;
    [[nodiscard]] std::size_t TouchedCount() const noexcept { return _overrides.size(); }
    [[nodiscard]] DataOrientedRoamNodeIndex TouchedNode(std::size_t record) const { return _overrides.RecordAt(record).first; }
    // 净出口已定位记录后不再按键查找，期间不得增加记录或保存向量引用
    [[nodiscard]] std::uint64_t ReadTouched(std::size_t record, NativePlanningField field) const;
    [[nodiscard]] NativePlanningViewMetrics Metrics() const;
    [[nodiscard]] NativePlanningStorageMetrics StorageMetrics() const { return _overrides.Metrics(); }

private:
    /// <summary>
    /// 新身份的静态求值信息，没有活动索引、队列或最终关系
    /// </summary>
    struct VirtualNode
    {
        TriangleDomain Domain;
        DataOrientedRoamNodeIndex Parent;
        std::uint64_t Path;
        int Depth;
        std::uint8_t VarianceTree;
        std::size_t VarianceIndex;
        float GeometricError;
    };
    /// <summary>
    /// 未填写字段继续借用基态，存在位与无效关系值分开编码
    /// 写回原值仍保留记录，最终净差异在汇总时核算
    /// </summary>
    struct Fields
    {
        std::array<std::uint64_t, 4> Builds{};
        std::array<std::uint32_t, 5> Relations{};
        std::uint8_t Flags{0};
        std::uint16_t Present{0};
        [[nodiscard]] std::uint64_t Get(NativePlanningField field) const
        {
            const auto index = static_cast<std::size_t>(field);
            if (field <= NativePlanningField::RightNeighbor) return Relations[index];
            if (field <= NativePlanningField::MergeBuild) return Builds[index - 5U];
            if (field == NativePlanningField::SplitBlockedBuild) return Builds[3];
            const auto shift = field == NativePlanningField::CurrentSplitPath ? 4U : index - 8U;
            return (Flags >> shift) & (field == NativePlanningField::Activity ? 3U : 1U);
        }
        void Set(NativePlanningField field, std::uint64_t value)
        {
            const auto index = static_cast<std::size_t>(field);
            // 关系和标记在 View::Write 中先校验，轮次保留全部64位
            if (field <= NativePlanningField::RightNeighbor) Relations[index] = static_cast<std::uint32_t>(value);
            else if (field <= NativePlanningField::MergeBuild) Builds[index - 5U] = value;
            else if (field == NativePlanningField::SplitBlockedBuild) Builds[3] = value;
            else
            {
                const auto shift = field == NativePlanningField::CurrentSplitPath ? 4U : index - 8U;
                const auto mask = (field == NativePlanningField::Activity ? 3U : 1U) << shift;
                Flags = static_cast<std::uint8_t>((Flags & ~mask) | (value << shift));
            }
        }
    };
    static_assert(sizeof(Fields) == 56);
    template<NativePlanningField Selected>
    [[nodiscard]] std::uint64_t ReadAt(DataOrientedRoamNodeIndex node, std::size_t record) const
    {
        static_assert(Selected < NativePlanningField::Count);
        constexpr auto index = static_cast<std::size_t>(Selected);
        if (record != decltype(_overrides)::Missing && (_overrides.At(record).Present & (1U << index)))
        {
            ObserveRead(node, false);
            return _overrides.At(record).Get(Selected);
        }
        return Baseline<Selected>(node);
    }
    void ObserveRead(DataOrientedRoamNodeIndex node, bool sourceRead) const;
    [[nodiscard]] std::uint64_t Baseline(DataOrientedRoamNodeIndex node, NativePlanningField field) const;
    template<NativePlanningField Selected>
    [[nodiscard]] std::uint64_t Baseline(DataOrientedRoamNodeIndex node) const;

    const DataOrientedRoamState& _source;
    bool _collectReadCoverage;
    NativePlanningCosts* _costs;
    std::size_t _remainingBudget;
    std::vector<VirtualNode> _virtualNodes;
    NativePlanningStorage<DataOrientedRoamNodeIndex, Fields, true> _overrides;
    mutable NativePlanningViewMetrics _metrics;
    mutable std::set<DataOrientedRoamNodeIndex> _sourceNodesRead;
};

// 字段参数通常在调用点固定，让编译器消去无关映射分支，仍保留查询计数和边界检查
inline void NativePlanningView::ObserveRead(DataOrientedRoamNodeIndex node, bool sourceRead) const
{
    if (!IsValidNode(node)) throw std::out_of_range("planning node");
    ++_metrics.Queries;
    if (sourceRead)
    {
        ++_metrics.SourceQueries;
        if (_collectReadCoverage) _sourceNodesRead.insert(node);
    }
}

template<NativePlanningField Selected>
inline std::uint64_t NativePlanningView::Baseline(DataOrientedRoamNodeIndex node) const
{
    using Field = NativePlanningField;
    static_assert(Selected < Field::Count);
    const bool old = node < _source.Nodes.size();
    ObserveRead(node, old);
    if (!old)
    {
        // 新缓存先处于休眠状态；默认轮次属于本轮，关系等待逻辑提交填写
        if constexpr (Selected <= Field::RightNeighbor) return InvalidDataOrientedRoamNodeIndex;
        if constexpr (Selected == Field::ActivatedBuild) return _source.BuildSequence;
        return 0;
    }
    const auto& pool = _source.Nodes;
    if constexpr (Selected == Field::LeftChild) return pool.LeftChildAt(node);
    else if constexpr (Selected == Field::RightChild) return pool.RightChildAt(node);
    else if constexpr (Selected == Field::BaseNeighbor) return pool.BaseNeighborAt(node);
    else if constexpr (Selected == Field::LeftNeighbor) return pool.LeftNeighborAt(node);
    else if constexpr (Selected == Field::RightNeighbor) return pool.RightNeighborAt(node);
    else if constexpr (Selected == Field::ActivatedBuild) return pool.ActivatedBuildIdAt(node);
    else if constexpr (Selected == Field::SplitBuild) return pool.SplitBuildIdAt(node);
    else if constexpr (Selected == Field::MergeBuild) return pool.MergeBuildIdAt(node);
    else if constexpr (Selected == Field::ForcedActivation) return pool.ActivatedByForcedSplitAt(node);
    else if constexpr (Selected == Field::IsSplit) return pool.IsSplitAt(node);
    else if constexpr (Selected == Field::Activity)
    {
        if (_source.NodeMembership[node].ActiveLeafPosition != InvalidDataOrientedRoamPosition)
            return static_cast<std::uint64_t>(NativePlanningActivity::Leaf);
        return static_cast<std::uint64_t>(_source.NodeMembership[node].ActiveInternalPosition != InvalidDataOrientedRoamPosition
            ? NativePlanningActivity::Internal : NativePlanningActivity::Dormant);
    }
    else if constexpr (Selected == Field::SplitBlockedBuild) return _source.SplitQueueBlockedBuildIds.at(node);
    else return _source.CurrentSplitPaths.contains(pool.PathIdAt(node));
}

inline std::uint64_t NativePlanningView::Baseline(DataOrientedRoamNodeIndex node, NativePlanningField field) const
{
    using Field = NativePlanningField;
    // 动态入口复用相同映射，不另维护一份可能与固定字段分歧的默认值规则
    switch (field)
    {
    case Field::LeftChild: return Baseline<Field::LeftChild>(node);
    case Field::RightChild: return Baseline<Field::RightChild>(node);
    case Field::BaseNeighbor: return Baseline<Field::BaseNeighbor>(node);
    case Field::LeftNeighbor: return Baseline<Field::LeftNeighbor>(node);
    case Field::RightNeighbor: return Baseline<Field::RightNeighbor>(node);
    case Field::ActivatedBuild: return Baseline<Field::ActivatedBuild>(node);
    case Field::SplitBuild: return Baseline<Field::SplitBuild>(node);
    case Field::MergeBuild: return Baseline<Field::MergeBuild>(node);
    case Field::ForcedActivation: return Baseline<Field::ForcedActivation>(node);
    case Field::IsSplit: return Baseline<Field::IsSplit>(node);
    case Field::Activity: return Baseline<Field::Activity>(node);
    case Field::SplitBlockedBuild: return Baseline<Field::SplitBlockedBuild>(node);
    case Field::CurrentSplitPath: return Baseline<Field::CurrentSplitPath>(node);
    default: throw std::out_of_range("planning field");
    }
}

inline std::uint64_t NativePlanningView::Read(DataOrientedRoamNodeIndex node, NativePlanningField field) const
{
    if (field >= NativePlanningField::Count) throw std::out_of_range("planning field");
    const auto found = _overrides.Find(node);
    const auto index = static_cast<std::size_t>(field);
    if (found != decltype(_overrides)::Missing && (_overrides.At(found).Present & (1U << index)))
    {
        // 覆盖命中不计为来源读取，避免把私有访问误报为旧状态覆盖
        ObserveRead(node, false);
        return _overrides.At(found).Get(field);
    }
    return Baseline(node, field);
}

inline std::uint64_t NativePlanningView::Initial(DataOrientedRoamNodeIndex node, NativePlanningField field) const
{
    if (field >= NativePlanningField::Count) throw std::out_of_range("planning initial field");
    return Baseline(node, field);
}

inline std::uint64_t NativePlanningView::ReadTouched(std::size_t record, NativePlanningField field) const
{
    if (record >= _overrides.size() || field >= NativePlanningField::Count) throw std::out_of_range("planning touched record");
    const auto& entry = _overrides.RecordAt(record);
    const auto index = static_cast<std::size_t>(field);
    if (!(entry.second.Present & (1U << index))) return Baseline(entry.first, field);
    ObserveRead(entry.first, false);
    return entry.second.Get(field);
}

inline DataOrientedRoamNodeIndex NativePlanningView::Relation(DataOrientedRoamNodeIndex node, NativePlanningField field) const
{
    if (field > NativePlanningField::RightNeighbor) throw std::invalid_argument("field is not a relation");
    return static_cast<DataOrientedRoamNodeIndex>(Read(node, field));
}

inline NativePlanningActivity NativePlanningView::Activity(DataOrientedRoamNodeIndex node) const
{
    return static_cast<NativePlanningActivity>(Read<NativePlanningField::Activity>(node));
}

// 静态信息在单次规划内不变，直接按身份借用来源或访问虚拟记录
inline std::uint64_t NativePlanningView::Path(DataOrientedRoamNodeIndex node) const
{
    const bool old = node < _source.Nodes.size();
    ObserveRead(node, old);
    return old ? _source.Nodes.PathIdAt(node) : _virtualNodes[node - _source.Nodes.size()].Path;
}

inline DataOrientedRoamNodeIndex NativePlanningView::Parent(DataOrientedRoamNodeIndex node) const
{
    const bool old = node < _source.Nodes.size();
    ObserveRead(node, old);
    return old ? _source.Nodes.ParentAt(node) : _virtualNodes[node - _source.Nodes.size()].Parent;
}

inline int NativePlanningView::Depth(DataOrientedRoamNodeIndex node) const
{
    const bool old = node < _source.Nodes.size();
    ObserveRead(node, old);
    return old ? _source.Nodes.DepthAt(node) : _virtualNodes[node - _source.Nodes.size()].Depth;
}

inline TriangleDomain NativePlanningView::Domain(DataOrientedRoamNodeIndex node) const
{
    const bool old = node < _source.Nodes.size();
    ObserveRead(node, old);
    return old ? _source.Nodes.DomainAt(node) : _virtualNodes[node - _source.Nodes.size()].Domain;
}

inline float NativePlanningView::GeometricError(DataOrientedRoamNodeIndex node) const
{
    const bool old = node < _source.Nodes.size();
    ObserveRead(node, old);
    return old ? _source.Nodes.GeometricErrorAt(node) : _virtualNodes[node - _source.Nodes.size()].GeometricError;
}

inline std::uint8_t NativePlanningView::VarianceTree(DataOrientedRoamNodeIndex node) const
{
    const bool old = node < _source.Nodes.size();
    ObserveRead(node, old);
    return old ? _source.Nodes.VarianceTreeIndexAt(node) : _virtualNodes[node - _source.Nodes.size()].VarianceTree;
}

inline std::size_t NativePlanningView::VarianceIndex(DataOrientedRoamNodeIndex node) const
{
    const bool old = node < _source.Nodes.size();
    ObserveRead(node, old);
    return old ? _source.Nodes.VarianceIndexAt(node) : _virtualNodes[node - _source.Nodes.size()].VarianceIndex;
}

inline std::uint64_t NativePlanningView::CreatedBuild(DataOrientedRoamNodeIndex node) const
{
    const bool old = node < _source.Nodes.size();
    ObserveRead(node, old);
    return old ? _source.Nodes.CreatedBuildIds[node] : _source.BuildSequence;
}
}
