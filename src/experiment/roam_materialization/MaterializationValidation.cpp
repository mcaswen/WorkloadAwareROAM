#include "experiment/roam_materialization/MaterializationValidation.h"

#include <algorithm>
#include <cmath>
#include <iterator>
#include <limits>
#include <stdexcept>

namespace ParallelRoam::Experiment::RoamMaterialization
{
namespace
{
void Require(bool value, const char* message)
{
    if (!value) throw std::logic_error(message);
}

bool SameDomain(const Domain& a, const Domain& b)
{
    return a.A == b.A && a.B == b.B && a.C == b.C;
}
}

void MaterializationValidation::ValidateClosed(const EventSet& events, int depth, std::size_t budget)
{
    // 每个二分事件净增一个叶；伙伴闭合确保这个计数属于合法共形切割
    Require(depth >= 0 && depth <= MaximumDepth, "认证深度无效");
    Require(budget >= 2 && events.size() <= budget - 2, "认证目标超预算");
    for (const auto id : events)
    {
        Require(MaterializationHierarchy::Depth(id) < depth, "事件超过细分深度");
        const auto parent = MaterializationHierarchy::Parent(id);
        const auto mate = MaterializationHierarchy::Mate(id);
        Require(parent == 0 || events.contains(parent), "事件集缺少父前置");
        Require(mate == 0 || events.contains(mate), "事件集缺少伙伴前置");
    }
}

EventSet MaterializationValidation::EnumerateLeaves(const EventSet& events)
{
    // 从两根独立遍历目标，不使用被测程序保存的活动列表作为预期答案
    EventSet leaves;
    std::vector<NodeId> stack{RootA, RootB};
    while (!stack.empty())
    {
        const auto id = stack.back(); stack.pop_back();
        if (!events.contains(id)) leaves.insert(id);
        else for (const auto child : MaterializationHierarchy::Children(id)) stack.push_back(child);
    }
    return leaves;
}

TargetRequest MaterializationValidation::Difference(const MaterializationState& s, EventSet target)
{
    TargetRequest request;
    request.Target = std::move(target);
    std::set_difference(request.Target.begin(), request.Target.end(), s.Events().begin(), s.Events().end(),
        std::back_inserter(request.Added));
    std::set_difference(s.Events().begin(), s.Events().end(), request.Target.begin(), request.Target.end(),
        std::back_inserter(request.Removed));
    return request;
}

CertifiedTarget MaterializationValidation::Certify(const MaterializationState& s, const TargetRequest& request)
{
    Validate(s);
    ValidateClosed(request.Target, s.Environment().MaxDepth, s.Budget());
    const auto expected = Difference(s, request.Target);
    // 排序后仍保留重复，既检查集合内容，也拒绝重复方向和遗漏条目
    auto added = request.Added, removed = request.Removed;
    std::sort(added.begin(), added.end()); std::sort(removed.begin(), removed.end());
    Require(added == expected.Added && removed == expected.Removed, "差分不完整或方向重复");
    CertifiedTarget certified;
    certified._version = s._version;
    // 认证绑定对象版本而不是概率哈希；同源副本可复用，任何发布后立即失效
    certified._added = std::move(added); certified._removed = std::move(removed);
    certified._leafCount = request.Target.size() + 2;
    return certified;
}

void MaterializationValidation::Validate(const MaterializationState& s)
{
    Require(s.Usable(), "验证遇到失败状态");
    ValidateClosed(s._events, s.Environment().MaxDepth, s._budget);
    const auto leaves = EnumerateLeaves(s._events);
    Require(leaves == s._leaves, "活动叶与目标独立遍历不一致");
    EventSet active = leaves;
    active.insert(s._events.begin(), s._events.end());
    std::size_t actualActive = 0;
    for (const auto& [id, node] : s._nodes)
    {
        // 历史缓存不参与逻辑投影，但所有活动记录都必须出现在独立目标遍历中
        if (!node.Active) continue;
        ++actualActive;
        Require(active.contains(id) && node.Id == id, "活动索引包含额外记录");
        Require(node.Internal == s._events.contains(id), "内部资格错误");
        Require(node.Depth == MaterializationHierarchy::Depth(id) &&
            node.Parent == MaterializationHierarchy::Parent(id) &&
            node.Mate == MaterializationHierarchy::Mate(id), "静态层次字段错误");
        Require(SameDomain(node.Triangle, MaterializationHierarchy::Decode(id)), "节点参数域错误");
        Require(node.Children == (node.Depth < MaximumDepth ? MaterializationHierarchy::Children(id) :
            std::array<NodeId, 2>{}), "孩子身份字段错误");
        if (node.Internal)
            for (const auto child : MaterializationHierarchy::Children(id))
                Require(active.contains(child) && s._nodes.contains(child), "活动内部节点缺少孩子");
    }
    Require(actualActive == active.size(), "缺失活动节点记录");

    std::map<EdgeKey, std::vector<std::pair<NodeId, std::size_t>>> edges;
    // 完整边重数、方向和总面积共同核查覆盖，不能只凭相邻指针互指宣布无裂缝
    std::int64_t twiceArea = 0;
    for (const auto id : leaves)
    {
        const auto& node = s.Node(id);
        const auto a = MaterializationHierarchy::Point(node.Triangle.A);
        const auto b = MaterializationHierarchy::Point(node.Triangle.B);
        const auto c = MaterializationHierarchy::Point(node.Triangle.C);
        const auto area = (b.X - a.X) * (c.Y - a.Y) - (b.Y - a.Y) * (c.X - a.X);
        Require(area < 0, "叶绕序或面积无效"); twiceArea += area;
        const auto keys = MaterializationHierarchy::Edges(node.Triangle);
        for (std::size_t side = 0; side < 3; ++side) edges[keys[side]].emplace_back(id, side);
    }
    Require(twiceArea == -(2LL << (MaximumDepth * 2)), "网格没有完整覆盖参数域");
    for (const auto& [edge, endpoints] : edges)
    {
        // 原型身份一致仍可能引用旧副本，因此同时验证数值邻接和真实记录地址
        Require(endpoints.size() == 2 ||
            (endpoints.size() == 1 && MaterializationHierarchy::IsBoundary(edge)), "非共形整边或重叠");
        for (std::size_t i = 0; i < endpoints.size(); ++i)
        {
            const auto [id, side] = endpoints[i];
            const auto target = endpoints.size() == 1 ? 0 : endpoints[1 - i].first;
            Require(s.Node(id).Neighbors[side] == target, "显式邻接与几何不一致");
            Require(s.Node(id).NeighborRecords[side] == (target == 0 ? nullptr : &s.Node(target)), "邻接句柄悬空或指向旧副本");
        }
    }

    // 独立重算完整逻辑队列，不调用状态的局部刷新过程
    std::map<NodeId, QueueValue> splitQueue, mergeQueue;
    std::set<OrderedEntry> splitOrder, mergeOrder;
    const auto key = [](NodeId id, QueueValue q, bool split) {
        return OrderedEntry{q.Suppressed ? std::numeric_limits<float>::max() : (split ? -q.Score : q.Score), id};
    };
    for (const auto id : leaves)
    {
        const bool suppressed = s.Node(id).Depth >= s.Environment().MaxDepth ||
            s._history.Blocked.contains(id) || s._history.Merged.contains(id);
        const QueueValue q{suppressed ? 0.0F : s.Score(id), suppressed};
        splitQueue[id] = q; splitOrder.insert(key(id, q, true));
    }
    for (const auto id : s._events)
    {
        if (MaterializationHierarchy::Group(id) != id) continue;
        const auto members = MaterializationHierarchy::Members(id);
        bool ready = true, suppressed = false;
        for (const auto member : members)
        {
            ready &= s._events.contains(member);
            for (const auto child : MaterializationHierarchy::Children(member)) ready &= leaves.contains(child);
            suppressed |= s._history.Split.contains(member);
        }
        if (!ready) continue;
        float score = 0;
        if (!suppressed) for (const auto member : members) score = std::max(score, s.Score(member));
        const QueueValue q{score, suppressed};
        mergeQueue[id] = q; mergeOrder.insert(key(id, q, false));
    }
    Require(splitQueue == s._splitQueue && splitOrder == s._splitOrder, "细分队列逻辑条目或排序错误");
    Require(mergeQueue == s._mergeQueue && mergeOrder == s._mergeOrder, "合并队列逻辑条目或排序错误");

    EventSet owners = leaves;
    // 删除尚未确认时，消费者旧叶仍是合法所有者，不能按最终叶过滤掉它
    for (const auto& [id, triangle] : s._consumed)
    {
        owners.insert(id);
        Require(triangle == s.Environment().Emit(id, MaterializationHierarchy::Decode(id)), "消费者几何属性错误");
        for (const auto value : triangle) Require(std::isfinite(value), "消费几何有非有限值");
    }
    std::map<NodeId, PendingEntry> pending;
    // 从最早基线与当前目标重新求期望差分，审计多事务合成是否丢失义务
    std::set<std::uint64_t> usedSlots;
    for (const auto id : owners)
    {
        const bool baseline = s._consumed.contains(id), target = leaves.contains(id);
        if (baseline != target) pending[id] = {baseline, target};
        Require(s._slots.contains(id), "仍被活动叶或消费者引用的槽位丢失");
        Require(usedSlots.insert(s._slots.at(id)).second, "槽位被两个身份同时拥有");
    }
    Require(s._slots.size() == owners.size(), "输出槽位含有无所有者的记录");
    for (const auto slot : s._freeSlots) Require(usedSlots.insert(slot).second, "空闲槽位重复或仍被引用");
    Require(pending == s._pending, "待消费差分丢失旧基线或最新目标");
}

void MaterializationValidation::Compare(const MaterializationState& a, const MaterializationState& b)
{
    // 两侧各自通过完整验证后再逐项比较，物理槽位排列和休眠缓存容量允许不同
    Validate(a); Validate(b);
    Require(a._events == b._events && a._leaves == b._leaves, "目标事件或叶集合不同");
    Require(a._splitQueue == b._splitQueue && a._mergeQueue == b._mergeQueue, "逻辑队列不同");
    Require(a._history == b._history && a._budget == b._budget, "历史或预算不同");
    Require(a._pending == b._pending && a._consumed == b._consumed, "网格消费投影不同");
    for (const auto id : a._leaves) Require(a.Node(id).Neighbors == b.Node(id).Neighbors, "最终邻接不同");
    Require(a.PeekSplit() == b.PeekSplit() && a.PeekMerge() == b.PeekMerge(), "后续队首不同");
}
}
