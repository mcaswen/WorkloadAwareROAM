#include "algorithms/data_oriented_roam/materialization/NativePlanningView.h"

#include "algorithms/data_oriented_roam/DataOrientedRoamStateOps.h"
#include "algorithms/data_oriented_roam/DataOrientedRoamVariance.h"

#include <bit>
#include <stdexcept>

namespace ParallelRoam::Algorithms::DataOrientedRoam::Materialization
{
using Node = DataOrientedRoamNodeIndex;
using Field = NativePlanningField;

NativePlanningView::NativePlanningView(const DataOrientedRoamState& source, bool collectReadCoverage)
    : _source(source), _collectReadCoverage(collectReadCoverage),
      // 与严格阶段入口相同，余量从当前活动数量计算，不借用可能属于上一阶段的计数器
      _remainingBudget(source.Settings.TriangleBudget > source.ActiveLeafNodes.size()
          ? source.Settings.TriangleBudget - source.ActiveLeafNodes.size() : 0)
{
    // 构造只检查常数规模描述，不复制节点或建立全池身份字典
    if (source.Settings.MaxDepth < 0 || source.Settings.MaxDepth > 20 ||
        source.Nodes.size() >= InvalidDataOrientedRoamNodeIndex ||
        source.NodeMembership.size() < source.Nodes.size())
        throw std::invalid_argument("unsupported native planning source");
}

void NativePlanningView::ObserveRead(Node node, bool sourceRead) const
{
    if (!IsValidNode(node)) throw std::out_of_range("planning node");
    ++_metrics.Queries;
    if (sourceRead)
    {
        ++_metrics.SourceQueries;
        if (_collectReadCoverage) _sourceNodesRead.insert(node);
    }
}

std::uint64_t NativePlanningView::Baseline(Node node, Field field) const
{
    const bool old = node < _source.Nodes.size();
    ObserveRead(node, old);
    if (!old)
    {
        // 新缓存先处于休眠状态；创建轮次与默认激活轮次一致，关系等待逻辑提交填写
        if (field <= Field::RightNeighbor) return InvalidDataOrientedRoamNodeIndex;
        if (field == Field::ActivatedBuild) return _source.BuildSequence;
        return 0;
    }
    const auto& pool = _source.Nodes;
    switch (field)
    {
    case Field::LeftChild: return pool.LeftChildAt(node);
    case Field::RightChild: return pool.RightChildAt(node);
    case Field::BaseNeighbor: return pool.BaseNeighborAt(node);
    case Field::LeftNeighbor: return pool.LeftNeighborAt(node);
    case Field::RightNeighbor: return pool.RightNeighborAt(node);
    case Field::ActivatedBuild: return pool.ActivatedBuildIdAt(node);
    case Field::SplitBuild: return pool.SplitBuildIdAt(node);
    case Field::MergeBuild: return pool.MergeBuildIdAt(node);
    case Field::ForcedActivation: return pool.ActivatedByForcedSplitAt(node);
    case Field::IsSplit: return pool.IsSplitAt(node);
    case Field::Activity:
        if (_source.NodeMembership[node].ActiveLeafPosition != InvalidDataOrientedRoamPosition)
            return static_cast<std::uint64_t>(NativePlanningActivity::Leaf);
        return static_cast<std::uint64_t>(_source.NodeMembership[node].ActiveInternalPosition != InvalidDataOrientedRoamPosition
            ? NativePlanningActivity::Internal : NativePlanningActivity::Dormant);
    case Field::SplitBlockedBuild: return _source.SplitQueueBlockedBuildIds.at(node);
    case Field::CurrentSplitPath: return _source.CurrentSplitPaths.contains(pool.PathIdAt(node));
    default: throw std::out_of_range("planning field");
    }
}

std::uint64_t NativePlanningView::Read(Node node, Field field) const
{
    if (field >= Field::Count) throw std::out_of_range("planning field");
    const auto found = _overrides.find(node);
    if (found != _overrides.end() && found->second[static_cast<std::size_t>(field)])
    {
        // 命中覆盖不是一次来源读取，诊断不能把私有查询计成旧状态覆盖
        ObserveRead(node, false);
        return *found->second[static_cast<std::size_t>(field)];
    }
    return Baseline(node, field);
}

std::uint64_t NativePlanningView::Initial(Node node, Field field) const
{
    if (field >= Field::Count) throw std::out_of_range("planning initial field");
    return Baseline(node, field);
}

void NativePlanningView::Write(Node node, Field field, std::uint64_t value)
{
    if (Read(node, field) == value) return;
    if (field <= Field::RightNeighbor && value != InvalidDataOrientedRoamNodeIndex && value >= NodeCount())
        throw std::out_of_range("planning relation target");
    if ((field == Field::Activity && value > static_cast<std::uint64_t>(NativePlanningActivity::Internal)) ||
        ((field == Field::ForcedActivation || field == Field::IsSplit || field == Field::CurrentSplitPath) && value > 1))
        throw std::invalid_argument("planning flag value");
    const auto baseline = Baseline(node, field);
    auto& slot = _overrides[node][static_cast<std::size_t>(field)];
    slot = value == baseline ? std::nullopt : std::optional{value};
    // 恢复原值也保留曾触及记录，累计覆盖不能因最终净差分小而消失
    ++_metrics.FieldWrites;
}

Node NativePlanningView::Relation(Node node, Field field) const
{
    if (field > Field::RightNeighbor) throw std::invalid_argument("field is not a relation");
    return static_cast<Node>(Read(node, field));
}

NativePlanningActivity NativePlanningView::Activity(Node node) const
{
    return static_cast<NativePlanningActivity>(Read(node, Field::Activity));
}

std::uint64_t NativePlanningView::Path(Node node) const
{
    const bool old = node < _source.Nodes.size();
    ObserveRead(node, old);
    return old ? _source.Nodes.PathIdAt(node) : _virtualNodes[node - _source.Nodes.size()].Path;
}

Node NativePlanningView::Parent(Node node) const
{
    const bool old = node < _source.Nodes.size();
    ObserveRead(node, old);
    return old ? _source.Nodes.ParentAt(node) : _virtualNodes[node - _source.Nodes.size()].Parent;
}

int NativePlanningView::Depth(Node node) const
{
    const bool old = node < _source.Nodes.size();
    ObserveRead(node, old);
    return old ? _source.Nodes.DepthAt(node) : _virtualNodes[node - _source.Nodes.size()].Depth;
}

TriangleDomain NativePlanningView::Domain(Node node) const
{
    const bool old = node < _source.Nodes.size();
    ObserveRead(node, old);
    return old ? _source.Nodes.DomainAt(node) : _virtualNodes[node - _source.Nodes.size()].Domain;
}

float NativePlanningView::GeometricError(Node node) const
{
    const bool old = node < _source.Nodes.size();
    ObserveRead(node, old);
    return old ? _source.Nodes.GeometricErrorAt(node) : _virtualNodes[node - _source.Nodes.size()].GeometricError;
}

std::uint8_t NativePlanningView::VarianceTree(Node node) const
{
    const bool old = node < _source.Nodes.size();
    ObserveRead(node, old);
    return old ? _source.Nodes.VarianceTreeIndexAt(node) : _virtualNodes[node - _source.Nodes.size()].VarianceTree;
}

std::size_t NativePlanningView::VarianceIndex(Node node) const
{
    const bool old = node < _source.Nodes.size();
    ObserveRead(node, old);
    return old ? _source.Nodes.VarianceIndexAt(node) : _virtualNodes[node - _source.Nodes.size()].VarianceIndex;
}

std::uint64_t NativePlanningView::CreatedBuild(Node node) const
{
    const bool old = node < _source.Nodes.size();
    ObserveRead(node, old);
    return old ? _source.Nodes.CreatedBuildIds[node] : _source.BuildSequence;
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
    auto result = _metrics;
    result.VirtualNodes = _virtualNodes.size();
    result.ReadCoverageCollected = _collectReadCoverage;
    result.DistinctSourceNodesRead = _sourceNodesRead.size();
    for (const auto& [node, fields] : _overrides)
    {
        if (node < _source.Nodes.size()) ++result.OldNodeRecords;
        else ++result.VirtualNodeRecords;
        for (const auto& field : fields) result.ChangedFields += static_cast<std::size_t>(field.has_value());
    }
    return result;
}
}
