#include "experiment/roam_materialization/MaterializationPatch.h"

#include <algorithm>
#include <chrono>
#include <exception>
#include <stdexcept>

namespace ParallelRoam::Experiment::RoamMaterialization
{
namespace
{
// 内部新叶边与旧外部接口共用精确键，端点类型决定最终写入所有者
struct Endpoint { EdgeKey Edge; NodeId Id; std::size_t Side; bool NewLeaf; NodeRecord* Record; };

// 成员判定来自旧状态与认证覆盖；缺失的休眠孩子保持空指针，不提前物化
struct SupportEntry { NodeId Id; NodeRecord* Record; bool TargetEvent, TargetLeaf; };

// 按记录归并所有槽位，任务只写本记录，不在提交期间查询邻居状态
struct RecordPatch
{
    NodeRecord* Record;
    std::array<NodeId, 3> Neighbors;
    std::array<const NodeRecord*, 3> Pointers;
};

// 边分组先产生槽位更新，随后按身份排序，以检查重复写并确定唯一所有者
struct NeighborWrite
{
    NodeId Id, Target;
    std::size_t Side;
    NodeRecord* Record;
    const NodeRecord* TargetRecord;
};

template<class Function>
void RunRange(std::size_t items, const MaterializationExecution& execution,
    PhaseEvidence* evidence, Function&& function)
{
    if (items == 0) return;
    const auto chunks = std::min(items, execution.Workers);
    // 异常槽与任务同寿命；包括内联路径在内，所有分块结束后才传播失败
    std::vector<std::exception_ptr> errors(chunks);
    const auto start = evidence ? std::chrono::steady_clock::now() : std::chrono::steady_clock::time_point{};
    if (evidence)
    {
        evidence->Items = items;
        evidence->ChunkItems.resize(chunks); evidence->Threads.resize(chunks);
        evidence->Dispatches = chunks > 1 ? 1 : 0;
    }
    const auto task = [&](std::size_t chunk) noexcept {
        try
        {
            const auto first = items / chunks * chunk + std::min(chunk, items % chunks);
            const auto last = first + items / chunks + (chunk < items % chunks ? 1 : 0);
            if (evidence)
            {
                evidence->ChunkItems[chunk] = last - first;
                evidence->Threads[chunk] = std::this_thread::get_id();
            }
            for (auto i = first; i < last; ++i) function(i);
        }
        catch (...) { errors[chunk] = std::current_exception(); }
    };
    if (chunks == 1) task(0);
    else execution.Dispatch(chunks, task);
    if (evidence) evidence->WallMs = std::chrono::duration<double, std::milli>(
        std::chrono::steady_clock::now() - start).count();
    for (const auto& error : errors) if (error) std::rethrow_exception(error);
}
}

void MaterializationPatch::Apply(MaterializationState& s, const CertifiedTarget& target, WorkCounters* work,
    const MaterializationExecution& execution)
{
    if (!s._usable) throw std::invalid_argument("物化不能使用失败状态");
    if (s._version != target._version) throw std::invalid_argument("物化目标认证已过期");
    if (target._leafCount > s._budget) throw std::invalid_argument("目标超过预算");
    if (execution.Workers == 0 || (execution.Workers > 1 && !execution.Dispatch))
        throw std::invalid_argument("缺少同步并行执行能力");
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
    const auto& added = target._added;
    const auto& removed = target._removed;
    const auto contains = [&](const std::vector<NodeId>& ids, NodeId id) {
        if (work) ++work->LocalSearches;
        return std::binary_search(ids.begin(), ids.end(), id);
    };
    const auto event = [&](NodeId id) { return contains(added, id) || (!contains(removed, id) && s.Event(id)); };
    std::vector<NodeId> affected, groups;
    const auto unique = [&](std::vector<NodeId>& ids) {
        if (work) work->ScratchSortItems += ids.size();
        std::sort(ids.begin(), ids.end());
        ids.erase(std::unique(ids.begin(), ids.end()), ids.end());
    };
    // 叶支持集来自事件及直接孩子，合并支持集来自事件组及父组
    const auto collect = [&](const std::vector<NodeId>& changes) {
        for (const auto id : changes)
        {
            affected.push_back(id);
            for (const auto child : MaterializationHierarchy::Children(id)) affected.push_back(child);
            groups.push_back(s.Group(id));
            const auto parent = MaterializationHierarchy::Parent(id);
            if (parent != 0) groups.push_back(s.Group(parent));
        }
    };
    collect(target._added); collect(target._removed);
    unique(affected); unique(groups);
    // 支持集已按身份有序，单次筛选得到的旧叶与新叶也可直接二分查询
    std::vector<NodeId> before, after;
    std::vector<SupportEntry> support;
    support.reserve(affected.size());
    // 在旧状态上同时求旧叶和目标叶，避免应用一个事件后改变其他判定输入
    for (const auto id : affected)
    {
        const auto parent = MaterializationHierarchy::Parent(id);
        const bool targetEvent = event(id);
        const bool newLeaf = (parent == 0 || event(parent)) && !targetEvent;
        const bool oldLeaf = s.Leaf(id);
        if (work) ++work->NodeIndexProbes;
        const auto found = s._nodes.find(id);
        support.push_back({id, found == s._nodes.end() ? nullptr : &found->second, targetEvent, newLeaf});
        if (oldLeaf && !newLeaf) before.push_back(id);
        if (!oldLeaf && newLeaf) after.push_back(id);
    }
    const auto locate = [&](NodeId id) -> SupportEntry& {
        if (work) ++work->LocalSearches;
        const auto found = std::lower_bound(support.begin(), support.end(), id,
            [](const auto& item, NodeId key) { return item.Id < key; });
        if (found == support.end() || found->Id != id) throw std::logic_error("身份不在局部支持集");
        return *found;
    };
    if (work)
    {
        work->LeafSupport = affected.size(); work->MergeSupport = groups.size();
    }

    std::vector<Endpoint> endpoints;
    // 保留叶与变化区之间是完整共边；只读取命中的外部槽位，不沿邻居继续扩散
    for (const auto id : before)
    {
        const auto& node = *locate(id).Record;
        const auto edges = MaterializationHierarchy::Edges(node.Triangle);
        for (std::size_t edge = 0; edge < 3; ++edge)
        {
            const auto outside = node.Neighbors[edge];
            if (outside == 0 || contains(before, outside)) continue;
            // 变化区内部的旧边会整体失效，只有外部接口需要继承反向位置
            if (work) ++work->NodeIndexProbes;
            auto* outsideRecord = &s._nodes.at(outside);
            const auto otherEdges = MaterializationHierarchy::Edges(outsideRecord->Triangle);
            const auto found = std::find(otherEdges.begin(), otherEdges.end(), edges[edge]);
            if (found == otherEdges.end()) throw std::logic_error("旧外部接口缺少反向槽位");
            endpoints.push_back({edges[edge], outside, static_cast<std::size_t>(found - otherEdges.begin()),
                false, outsideRecord});
        }
    }
    // 只创建目标实际持有的内部节点和新叶，不创建 R 的中间叶序列
    record(&WorkCounters::SupportMs);
    std::vector<std::pair<NodeId, NodeRecord*>> newRecords;
    const auto reserve = [&](NodeId id) {
        auto& entry = locate(id);
        if (entry.Record) return;
        if (work) ++work->NodeIndexProbes;
        const auto [it, inserted] = s._nodes.try_emplace(id);
        entry.Record = &it->second;
        if (inserted)
        {
            newRecords.emplace_back(id, &it->second);
            if (work) ++work->RecordsCreated;
        }
    };
    for (const auto id : target._added) reserve(id);
    for (const auto id : after) reserve(id);
    record(&WorkCounters::AllocationMs);
    RunRange(newRecords.size(), execution, work ? &work->Phases[0] : nullptr, [&](std::size_t i) {
        *newRecords[i].second = MaterializationState::BuildRecord(newRecords[i].first);
    });
    record(&WorkCounters::RecordsMs);
    // 新静态记录已经就绪，再串行修改集合、历史和消费者义务
    for (const auto id : target._removed) s.SetEvent(*locate(id).Record, false);
    for (const auto id : target._added) s.SetEvent(*locate(id).Record, true);
    for (const auto id : before) s.SetLeaf(*locate(id).Record, false);
    for (const auto id : after) s.SetLeaf(*locate(id).Record, true);
    for (const auto& entry : support)
    {
        // 缓存允许保存休眠孩子，但它们的旧活动位和邻接不能继续影响后续查询
        if (!entry.Record) continue;
        auto& node = *entry.Record;
        node.Internal = entry.TargetEvent;
        node.Active = entry.TargetEvent || entry.TargetLeaf;
        if (!node.Active) { node.Neighbors = {}; node.NeighborRecords = {}; }
    }
    record(&WorkCounters::StateMaintenanceMs);
    const auto externalCount = endpoints.size();
    endpoints.resize(externalCount + after.size() * 3);
    // 此处复制的是变化叶和候选的局部描述，不把状态查询及其共享计数带入任务
    std::vector<NodeRecord*> leafInputs;
    leafInputs.reserve(after.size());
    for (const auto id : after) leafInputs.push_back(locate(id).Record);
    std::vector<MaterializationState::CandidateInput> candidates;
    candidates.reserve(affected.size() + groups.size());
    for (const auto id : affected) candidates.push_back(s.PrepareSplit(id));
    for (const auto id : groups) candidates.push_back(s.PrepareMerge(id));
    std::vector<QueueValue> values(candidates.size());
    record(&WorkCounters::DescriptorMs);
    const auto& environment = s.Environment();
    RunRange(leafInputs.size() + candidates.size(), execution, work ? &work->Phases[1] : nullptr,
        [&](std::size_t i) {
            if (i < leafInputs.size())
            {
                const auto* node = leafInputs[i];
                const auto edges = MaterializationHierarchy::Edges(node->Triangle);
                for (std::size_t side = 0; side < 3; ++side)
                    endpoints[externalCount + i * 3 + side] = {edges[side], node->Id, side, true, leafInputs[i]};
            }
            else
            {
                const auto candidate = i - leafInputs.size();
                values[candidate] = MaterializationState::EvaluateCandidate(environment, candidates[candidate]);
            }
        });
    record(&WorkCounters::RecordsMs);
    if (work)
    {
        work->PreparedEdges = endpoints.size();
        for (const auto& input : candidates) work->ScoreEvaluations += input.Samples;
    }
    // 以整边键分组连接最终叶，排序规模只取决于新叶和外部接口
    std::vector<NeighborWrite> writes;
    writes.reserve(endpoints.size());
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
            writes.push_back({a.Id, 0, a.Side, a.Record, nullptr});
        }
        else if (last - first == 2)
        {
            const auto& b = endpoints[first + 1];
            if ((!a.NewLeaf && !b.NewLeaf) || a.Id == b.Id) throw std::logic_error("连接端点重复");
            writes.push_back({a.Id, b.Id, a.Side, a.Record, b.Record});
            writes.push_back({b.Id, a.Id, b.Side, b.Record, a.Record});
        }
        else throw std::logic_error("直接连接产生多重边");
        first = last;
    }
    std::sort(writes.begin(), writes.end(), [](const auto& a, const auto& b) {
        return a.Id == b.Id ? a.Side < b.Side : a.Id < b.Id;
    });
    if (work) work->ScratchSortItems += endpoints.size() + writes.size();
    std::vector<RecordPatch> patches;
    patches.reserve(after.size() + externalCount);
    for (std::size_t i = 0; i < writes.size(); ++i)
    {
        const auto& write = writes[i];
        if (i == 0 || write.Id != writes[i - 1].Id)
        {
            auto& node = *write.Record;
            patches.push_back({&node, node.Neighbors, node.NeighborRecords});
        }
        else if (write.Side == writes[i - 1].Side) throw std::logic_error("同一邻接槽位出现重复写者");
        auto& patch = patches.back();
        patch.Neighbors[write.Side] = write.Target;
        patch.Pointers[write.Side] = write.TargetRecord;
    }
    record(&WorkCounters::ConnectMs);
    RunRange(patches.size(), execution, work ? &work->Phases[2] : nullptr, [&](std::size_t i) {
        auto& patch = patches[i];
        patch.Record->Neighbors = patch.Neighbors;
        patch.Record->NeighborRecords = patch.Pointers;
    });
    record(&WorkCounters::RecordsMs);
    // 几何和评分均已完成，按原支持集顺序安装队列，再发布新逻辑版本
    for (std::size_t i = 0; i < candidates.size(); ++i)
        s.InstallCandidate(candidates[i], values[i], i < affected.size());
    if (work)
    {
        work->NeighborWrites += writes.size(); work->RecordPatches = patches.size();
        work->DescriptorItems = newRecords.size() + leafInputs.size() + candidates.size() + patches.size();
        work->ScratchPayloadBytes = newRecords.capacity() * sizeof(newRecords[0]) +
            leafInputs.capacity() * sizeof(leafInputs[0]) + candidates.capacity() * sizeof(candidates[0]) +
            values.capacity() * sizeof(values[0]) + endpoints.capacity() * sizeof(endpoints[0]) +
            writes.capacity() * sizeof(writes[0]) + patches.capacity() * sizeof(patches[0]) +
            (affected.capacity() + groups.capacity() + before.capacity() + after.capacity()) * sizeof(NodeId) +
            support.capacity() * sizeof(support[0]);
    }
    if (s._leaves.size() != target._leafCount) throw std::logic_error("物化数量与认证不一致");
    s.FinishMutation();
    record(&WorkCounters::MaintenanceMs);
}
}
