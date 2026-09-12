#pragma once

#include "algorithms/data_oriented_roam/materialization/NativeMaterializationTypes.h"
#include "algorithms/data_oriented_roam/DataOrientedRoamState.h"

#include <array>
#include <map>
#include <optional>
#include <set>
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
    explicit NativePlanningView(const DataOrientedRoamState& source, bool collectReadCoverage = false);
    NativePlanningView(const NativePlanningView&) = delete;
    NativePlanningView& operator=(const NativePlanningView&) = delete;

    // 来源须在整个同步规划期间保持有效且无人修改，视图不延长高度图或线程池寿命
    [[nodiscard]] const DataOrientedRoamState& Source() const noexcept { return _source; }
    [[nodiscard]] std::size_t NodeCount() const noexcept { return _source.Nodes.size() + _virtualNodes.size(); }
    [[nodiscard]] bool IsValidNode(DataOrientedRoamNodeIndex node) const noexcept { return node < NodeCount(); }
    [[nodiscard]] std::uint64_t Read(DataOrientedRoamNodeIndex node, NativePlanningField field) const;
    // 净出口比较基态默认值时也经由计费入口，不能用直接数组读隐藏提取成本
    [[nodiscard]] std::uint64_t Initial(DataOrientedRoamNodeIndex node, NativePlanningField field) const;
    void Write(DataOrientedRoamNodeIndex node, NativePlanningField field, std::uint64_t value);
    [[nodiscard]] DataOrientedRoamNodeIndex Relation(DataOrientedRoamNodeIndex node, NativePlanningField field) const;
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
    [[nodiscard]] NativePlanningViewMetrics Metrics() const;

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
    // 未填写字段继续借用基态；显式写入的无效节点值与“没有覆盖”严格区分
    using Fields = std::array<std::optional<std::uint64_t>, static_cast<std::size_t>(NativePlanningField::Count)>;
    void ObserveRead(DataOrientedRoamNodeIndex node, bool sourceRead) const;
    [[nodiscard]] std::uint64_t Baseline(DataOrientedRoamNodeIndex node, NativePlanningField field) const;

    const DataOrientedRoamState& _source;
    bool _collectReadCoverage;
    std::size_t _remainingBudget;
    std::vector<VirtualNode> _virtualNodes;
    std::map<DataOrientedRoamNodeIndex, Fields> _overrides;
    mutable NativePlanningViewMetrics _metrics;
    mutable std::set<DataOrientedRoamNodeIndex> _sourceNodesRead;
};
}
