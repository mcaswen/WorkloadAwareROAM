#include "experiment/roam_materialization/MaterializationReference.h"

#include <algorithm>
#include <chrono>
#include <stdexcept>

namespace ParallelRoam::Experiment::RoamMaterialization
{
namespace
{
// 单菱形最多四个旧叶，外部接口表大小与总网格规模无关
struct LocalPort { EdgeKey Edge; NodeId Outside; std::size_t OutsideEdge; };
}

void MaterializationReference::EditGroup(MaterializationState& s, NodeId group, bool split)
{
    using Clock = std::chrono::steady_clock;
    auto boundary = s._work ? Clock::now() : Clock::time_point{};
    const auto record = [&](double WorkCounters::*field) {
        if (!s._work) return;
        const auto now = Clock::now();
        s._work->*field += std::chrono::duration<double, std::milli>(now - boundary).count();
        boundary = now;
    };
    const auto members = s.Members(group);
    // 不查询直接物化器的支持集，原子步仅依赖当前局部结构
    std::vector<NodeId> before, after;
    for (const auto id : members)
    {
        if (split)
        {
            if (!s.Leaf(id) || s.Node(id).Depth >= s.Environment().MaxDepth)
                throw std::logic_error("参考细分没有满足伙伴或深度前置");
            before.push_back(id);
            for (const auto child : s.Node(id).Children) after.push_back(child);
        }
        else
        {
            if (!s.MergeReady(group)) throw std::logic_error("参考合并前置不成立");
            after.push_back(id);
            for (const auto child : s.Node(id).Children) before.push_back(child);
        }
    }
    if (s._leaves.size() - before.size() + after.size() > s._budget)
        throw std::logic_error("参考中间状态超预算");

    std::vector<LocalPort> ports;
    // 一个合法菱形的旧外部边可以逐项继承，无需排序全网格边表
    for (const auto id : before)
    {
        const auto& node = s.Node(id);
        const auto edges = MaterializationHierarchy::Edges(node.Triangle);
        for (std::size_t edge = 0; edge < 3; ++edge)
        {
            const auto outside = node.Neighbors[edge];
            if (std::find(before.begin(), before.end(), outside) != before.end()) continue;
            std::size_t outsideEdge = 0;
            if (outside != 0)
            {
                const auto otherEdges = MaterializationHierarchy::Edges(s.Node(outside).Triangle);
                const auto found = std::find(otherEdges.begin(), otherEdges.end(), edges[edge]);
                if (found == otherEdges.end()) throw std::logic_error("参考旧接口不互反");
                outsideEdge = static_cast<std::size_t>(found - otherEdges.begin());
            }
            ports.push_back({edges[edge], outside, outsideEdge});
        }
    }

    // 每次只应用一个合法组；中间叶和历史是参考真实付出的工作
    record(&WorkCounters::SupportMs);
    for (const auto id : members) s.SetEvent(id, split);
    for (const auto id : before) s.SetLeaf(id, false);
    for (const auto id : after) s.SetLeaf(id, true);
    record(&WorkCounters::RecordsMs);
    for (const auto id : after)
    {
        // 新叶最多四片，常量枚举独立恢复内边，不借用批量连接程序
        const auto edges = MaterializationHierarchy::Edges(s.Node(id).Triangle);
        for (std::size_t edge = 0; edge < 3; ++edge)
        {
            bool found = false;
            for (const auto other : after)
            {
                if (other == id) continue;
                const auto otherEdges = MaterializationHierarchy::Edges(s.Node(other).Triangle);
                if (std::find(otherEdges.begin(), otherEdges.end(), edges[edge]) == otherEdges.end()) continue;
                s.SetNeighbor(id, edge, other); found = true; break;
            }
            if (found) continue;
            const auto port = std::find_if(ports.begin(), ports.end(),
                [&](const auto& p) { return p.Edge == edges[edge]; });
            // 边界细分会把域边界拆开，外部叶接口则必须保持整边
            if (port == ports.end())
            {
                if (!MaterializationHierarchy::IsBoundary(edges[edge]))
                    throw std::logic_error("参考新叶找不到完整外部接口");
                s.SetNeighbor(id, edge, 0);
            }
            else
            {
                s.SetNeighbor(id, edge, port->Outside);
                if (port->Outside != 0) s.SetNeighbor(port->Outside, port->OutsideEdge, id);
            }
        }
    }
    record(&WorkCounters::ConnectMs);
    for (const auto id : before) s.RefreshSplit(id);
    for (const auto id : after) s.RefreshSplit(id);
    // 父组最多两个，常量局部更新不重新排序整条队列
    std::vector<NodeId> groups{group};
    for (const auto id : members)
    {
        const auto parent = s.Node(id).Parent;
        if (parent != 0)
        {
            const auto parentGroup = s.Group(parent);
            if (std::find(groups.begin(), groups.end(), parentGroup) == groups.end()) groups.push_back(parentGroup);
        }
    }
    for (const auto id : groups) s.RefreshMerge(id);
    if (s._work) ++s._work->PrimitiveGroups;
    record(&WorkCounters::MaintenanceMs);
}

void MaterializationReference::Apply(MaterializationState& s, const CertifiedTarget& target, WorkCounters* work)
{
    if (s._version != target._version) throw std::invalid_argument("参考目标认证已过期");
    MaterializationState::WorkSession session(s, work);
    const auto apply = [&](const std::vector<NodeId>& changes, bool split)
    {
        // 组内成员共同执行，排序只覆盖差分中的组，不维护额外的全局目标索引
        const auto start = work ? std::chrono::steady_clock::now() : std::chrono::steady_clock::time_point{};
        std::vector<std::pair<int, NodeId>> groups;
        for (const auto id : changes)
            if (s.Group(id) == id) groups.emplace_back(MaterializationHierarchy::Depth(id), id);
        std::sort(groups.begin(), groups.end(), [split](const auto& a, const auto& b) {
            if (a.first != b.first) return split ? a.first < b.first : a.first > b.first;
            return a.second < b.second;
        });
        if (work) work->SupportMs += std::chrono::duration<double, std::milli>(
            std::chrono::steady_clock::now() - start).count();
        for (const auto& [depth, id] : groups) { static_cast<void>(depth); EditGroup(s, id, split); }
    };
    // 先下降至两端交集再上升；合法闭合集保证每步前置成立且预算没有中间峰值
    apply(target._removed, false); apply(target._added, true);
    if (s._leaves.size() != target._leafCount) throw std::logic_error("参考结果数量错误");
    if (!target._added.empty() || !target._removed.empty()) s.FinishMutation();
}

bool MaterializationReference::TrySplit(MaterializationState& s, NodeId root, WorkCounters* work)
{
    // 续接接口不自动补闭包：前置未就绪时明确拒绝，由调用者读取状态并推进前置
    if (!s.ShouldSplit(root)) return false;
    const auto members = s.Members(root);
    for (const auto id : members) if (!s.Leaf(id)) return false;
    if (s._leaves.size() + members.size() > s._budget) return false;
    MaterializationState::WorkSession session(s, work);
    EditGroup(s, s.Group(root), true); s.FinishMutation(); return true;
}

bool MaterializationReference::TryMerge(MaterializationState& s, NodeId group, WorkCounters* work)
{
    // 读取真实持久队列的资格与抑制，不根据测试给出的目标集合跳过这些条件
    group = s.Group(group);
    const auto candidate = s._mergeQueue.find(group);
    if (candidate == s._mergeQueue.end() || candidate->second.Suppressed ||
        candidate->second.Score > s.Environment().MergeThreshold) return false;
    MaterializationState::WorkSession session(s, work);
    EditGroup(s, group, false); s.FinishMutation(); return true;
}
}
