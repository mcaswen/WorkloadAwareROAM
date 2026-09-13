#include "algorithms/data_oriented_roam/materialization/NativePlanningView.h"

#include "algorithms/data_oriented_roam/DataOrientedRoamStateOps.h"
#include "algorithms/data_oriented_roam/DataOrientedRoamVariance.h"

#include <bit>
#include <stdexcept>

namespace ParallelRoam::Algorithms::DataOrientedRoam::Materialization
{
using Node = DataOrientedRoamNodeIndex;
using Field = NativePlanningField;

NativePlanningView::NativePlanningView(const DataOrientedRoamState& source, bool collectReadCoverage, NativePlanningCosts* costs)
    : _source(source), _collectReadCoverage(collectReadCoverage), _costs(costs),
      // 与严格阶段入口相同，余量从当前活动数量计算，不借用可能属于上一阶段的计数器
      _remainingBudget(source.Settings.TriangleBudget > source.ActiveLeafNodes.size()
          ? source.Settings.TriangleBudget - source.ActiveLeafNodes.size() : 0),
      _overrides(collectReadCoverage || (costs && costs->Detailed))
{
    // 构造只检查常数规模描述，不复制节点或建立全池身份字典
    if (source.Settings.MaxDepth < 0 || source.Settings.MaxDepth > 20 ||
        source.Nodes.size() >= InvalidDataOrientedRoamNodeIndex ||
        source.NodeMembership.size() < source.Nodes.size())
        throw std::invalid_argument("unsupported native planning source");
}

void NativePlanningView::Write(Node node, Field field, std::uint64_t value)
{
    WriteFields(node, {{field, value}});
}

void NativePlanningView::WriteFields(Node node, std::initializer_list<std::pair<Field, std::uint64_t>> values)
{
    if (!IsValidNode(node)) throw std::out_of_range("planning node");
    auto found = _overrides.Find(node);
    for (const auto [field, value] : values)
    {
        if (field >= Field::Count) throw std::out_of_range("planning field");
        const auto index = static_cast<std::size_t>(field);
        const bool present = found != decltype(_overrides)::Missing && (_overrides.At(found).Present & (1U << index));
        if (present) ObserveRead(node, false);
        const auto previous = present ? _overrides.At(found).Get(field) : Baseline(node, field);
        if (previous == value) continue;
        if (field <= Field::RightNeighbor && value != InvalidDataOrientedRoamNodeIndex && value >= NodeCount())
            throw std::out_of_range("planning relation target");
        if ((field == Field::Activity && value > static_cast<std::uint64_t>(NativePlanningActivity::Internal)) ||
            ((field == Field::ForcedActivation || field == Field::IsSplit || field == Field::CurrentSplitPath) && value > 1))
            throw std::invalid_argument("planning flag value");
        if (found == decltype(_overrides)::Missing) found = _overrides.InsertMissing(node);
        auto& fields = _overrides.At(found);
        fields.Set(field, value);
        fields.Present |= static_cast<std::uint16_t>(1U << index);
        // 恢复原值也保留曾触及记录，累计覆盖不能因最终净差分小而消失
        ++_metrics.FieldWrites;
    }
}

Node NativePlanningView::FindPath(std::uint64_t path) const
{
    if (path == 0) return InvalidDataOrientedRoamNodeIndex;
    const int high = static_cast<int>(std::bit_width(path)) - 1;
    // 第二根从第 32 位编码；先校验前缀再沿缓存链下降，避免把非法路径误认成已有身份
    const int depth = high >= 32 ? high - 32 : high;
    const std::uint64_t rootPath = high >= 32 ? 1ULL << 32U : 1ULL;
    if (depth > 20 || (path >> depth) != rootPath) return InvalidDataOrientedRoamNodeIndex;
    Node node = high >= 32 ? _source.RootB : _source.RootA;
    for (int bit = depth - 1; bit >= 0 && IsValidNode(node); --bit)
        node = Relation(node, ((path >> bit) & 1U) ? Field::RightChild : Field::LeftChild);
    // 缺失孩子只表示缓存尚不存在，查询本身不会创建潜在层次
    return IsValidNode(node) && Path(node) == path ? node : InvalidDataOrientedRoamNodeIndex;
}

std::array<Node, 2> NativePlanningView::CreateChildren(Node parent)
{
    NativePlanningCostScope cost{_costs, NativePlanningCost::Create, true};
    if (Relation(parent, Field::LeftChild) != InvalidDataOrientedRoamNodeIndex ||
        Relation(parent, Field::RightChild) != InvalidDataOrientedRoamNodeIndex)
        throw std::logic_error("planning children already cached");
    const int depth = Depth(parent) + 1;
    if (depth > _source.Settings.MaxDepth || NodeCount() > InvalidDataOrientedRoamNodeIndex - 2ULL)
        throw std::out_of_range("planning children exceed capacity or depth");
    const auto domains = SplitTriangleDomain(Domain(parent));
    const auto path = Path(parent), variance = VarianceIndex(parent);
    const auto tree = VarianceTree(parent);
    const Node left = static_cast<Node>(NodeCount()), right = left + 1U;
    // 先取得父节点所需的值，追加虚拟记录时不保留可能失效的 vector 元素引用
    _virtualNodes.push_back({domains.Left, parent, LeftChildPathId(path), depth, tree,
        variance * 2U + 1U, VarianceError(_source, tree, variance * 2U + 1U)});
    _virtualNodes.push_back({domains.Right, parent, RightChildPathId(path), depth, tree,
        variance * 2U + 2U, VarianceError(_source, tree, variance * 2U + 2U)});
    Write(parent, Field::LeftChild, left);
    Write(parent, Field::RightChild, right);
    return {left, right};
}

bool NativePlanningView::TryReserveBudget()
{
    if (_remainingBudget == 0) return false;
    --_remainingBudget;
    return true;
}

void NativePlanningView::ReleaseBudget()
{
    if (_remainingBudget >= _source.Settings.TriangleBudget) throw std::logic_error("planning budget over-release");
    ++_remainingBudget;
}

std::vector<Node> NativePlanningView::TouchedNodes() const
{
    // 仅枚举实际建立过覆盖的节点，后续净差分提取不必扫描来源缓存池
    std::vector<Node> result;
    result.reserve(_overrides.size());
    for (const auto& [node, fields] : _overrides) { (void)fields; result.push_back(node); }
    return result;
}

NativePlanningViewMetrics NativePlanningView::Metrics() const
{
    // 汇总只遍历覆盖记录；ChangedFields 与累计节点记录数量具有不同含义
    std::size_t oldRecords = 0, virtualRecords = 0, changed = 0;
    for (const auto& [node, fields] : _overrides)
    {
        if (node < _source.Nodes.size()) ++oldRecords;
        else ++virtualRecords;
        for (std::size_t index = 0; index < static_cast<std::size_t>(Field::Count); ++index)
            if ((fields.Present & (1U << index)) && fields.Get(static_cast<Field>(index)) != Baseline(node, static_cast<Field>(index))) ++changed;
    }
    // 基态比较也是真实查询，先完成遍历再冻结计数，避免把收尾读漏在返回指标之外
    auto result = _metrics;
    result.VirtualNodes = _virtualNodes.size();
    result.ReadCoverageCollected = _collectReadCoverage;
    result.DistinctSourceNodesRead = _sourceNodesRead.size();
    result.OldNodeRecords = oldRecords; result.VirtualNodeRecords = virtualRecords; result.ChangedFields = changed;
    return result;
}
}
