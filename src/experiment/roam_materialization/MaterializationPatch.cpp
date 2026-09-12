#include "experiment/roam_materialization/MaterializationPatch.h"

#include <algorithm>
#include <chrono>
#include <stdexcept>

namespace ParallelRoam::Experiment::RoamMaterialization
{
namespace
{
// 内部新叶边与旧外部接口共用精确键，端点类型决定最终写入所有者
struct Endpoint { EdgeKey Edge; NodeId Id; std::size_t Side; bool NewLeaf; };
}

void MaterializationPatch::Apply(MaterializationState& s, const CertifiedTarget& target, WorkCounters* work)
{
    if (!s._usable) throw std::invalid_argument("物化不能使用失败状态");
    if (s._version != target._version) throw std::invalid_argument("物化目标认证已过期");
    if (target._leafCount > s._budget) throw std::invalid_argument("目标超过预算");
    if (target._added.empty() && target._removed.empty()) return;
    // 认证已检查完整 J；热路径只持有有向覆盖，不能借机再遍历目标全集
    MaterializationState::WorkSession session(s, work);
    using Clock = std::chrono::steady_clock;
    auto boundary = work ? Clock::now() : Clock::time_point{};
    const auto record = [&](double WorkCounters::*field) {
        if (!work) return;
        const auto now = Clock::now();
        work->*field += std::chrono::duration<double, std::milli>(now - boundary).count();
        boundary = now;
    };
    const EventSet added(target._added.begin(), target._added.end());
    const EventSet removed(target._removed.begin(), target._removed.end());
    const auto event = [&](NodeId id) { return added.contains(id) || (!removed.contains(id) && s.Event(id)); };
    EventSet affected, groups;
    // 叶支持集来自事件及直接孩子，合并支持集来自事件组及父组
    const auto collect = [&](const std::vector<NodeId>& changes) {
        for (const auto id : changes)
        {
            affected.insert(id);
            for (const auto child : MaterializationHierarchy::Children(id)) affected.insert(child);
            groups.insert(s.Group(id));
            const auto parent = MaterializationHierarchy::Parent(id);
            if (parent != 0) groups.insert(s.Group(parent));
        }
    };
    collect(target._added); collect(target._removed);
    EventSet before, after;
    // 在旧状态上同时求旧叶和目标叶，避免应用一个事件后改变其他判定输入
    for (const auto id : affected)
    {
        const auto parent = MaterializationHierarchy::Parent(id);
        const bool newLeaf = (parent == 0 || event(parent)) && !event(id);
        const bool oldLeaf = s.Leaf(id);
        if (oldLeaf && !newLeaf) before.insert(id);
        if (!oldLeaf && newLeaf) after.insert(id);
    }
    if (work) { work->LeafSupport = affected.size(); work->MergeSupport = groups.size(); }

    std::vector<Endpoint> endpoints;
    // 保留叶与变化区之间是完整共边；只读取命中的外部槽位，不沿邻居继续扩散
    for (const auto id : before)
    {
        const auto& node = s.Node(id);
        const auto edges = MaterializationHierarchy::Edges(node.Triangle);
        for (std::size_t edge = 0; edge < 3; ++edge)
        {
            const auto outside = node.Neighbors[edge];
            if (outside == 0 || before.contains(outside)) continue;
            // 变化区内部的旧边会整体失效，只有外部接口需要继承反向位置
            const auto otherEdges = MaterializationHierarchy::Edges(s.Node(outside).Triangle);
            const auto found = std::find(otherEdges.begin(), otherEdges.end(), edges[edge]);
            if (found == otherEdges.end()) throw std::logic_error("旧外部接口缺少反向槽位");
            endpoints.push_back({edges[edge], outside, static_cast<std::size_t>(found - otherEdges.begin()), false});
        }
    }
    // 只创建目标实际持有的内部节点和新叶，不创建 R 的中间叶序列
    record(&WorkCounters::SupportMs);
    for (const auto id : target._removed) s.SetEvent(id, false);
    for (const auto id : target._added) s.SetEvent(id, true);
    for (const auto id : before) s.SetLeaf(id, false);
    for (const auto id : after)
    {
        s.SetLeaf(id, true);
        const auto edges = MaterializationHierarchy::Edges(s.Node(id).Triangle);
        for (std::size_t side = 0; side < 3; ++side) endpoints.push_back({edges[side], id, side, true});
    }
    for (const auto id : affected)
    {
        // 缓存允许保存休眠孩子，但它们的旧活动位和邻接不能继续影响后续查询
        const auto found = s._nodes.find(id);
        if (found == s._nodes.end()) continue;
        found->second.Internal = s.Event(id);
        found->second.Active = found->second.Internal || s.Leaf(id);
        if (!found->second.Active) { found->second.Neighbors = {}; found->second.NeighborRecords = {}; }
    }
    if (work) work->PreparedEdges = endpoints.size();
    record(&WorkCounters::RecordsMs);
    // 以整边键分组连接最终叶，排序规模只取决于新叶和外部接口
    std::sort(endpoints.begin(), endpoints.end(), [](const auto& a, const auto& b) { return a.Edge < b.Edge; });
    for (std::size_t first = 0; first < endpoints.size();)
    {
        std::size_t last = first + 1;
        while (last < endpoints.size() && endpoints[last].Edge == endpoints[first].Edge) ++last;
        const auto& a = endpoints[first];
        if (last - first == 1)
        {
            // 外部接口失去另一侧说明补丁不完整，只有真实域边界允许单端点
            if (!a.NewLeaf || !MaterializationHierarchy::IsBoundary(a.Edge))
                throw std::logic_error("直接连接产生非边界单侧边");
            s.SetNeighbor(a.Id, a.Side, 0);
        }
        else if (last - first == 2)
        {
            const auto& b = endpoints[first + 1];
            if ((!a.NewLeaf && !b.NewLeaf) || a.Id == b.Id) throw std::logic_error("连接端点重复");
            s.SetNeighbor(a.Id, a.Side, b.Id); s.SetNeighbor(b.Id, b.Side, a.Id);
        }
        else throw std::logic_error("直接连接产生多重边");
        first = last;
    }
    record(&WorkCounters::ConnectMs);
    // 几何连接结束后维护候选，复用未变化键，最后一次性发布新逻辑版本
    for (const auto id : affected) s.RefreshSplit(id);
    for (const auto id : groups) s.RefreshMerge(id);
    if (s._leaves.size() != target._leafCount) throw std::logic_error("物化数量与认证不一致");
    s.FinishMutation();
    record(&WorkCounters::MaintenanceMs);
}
}
