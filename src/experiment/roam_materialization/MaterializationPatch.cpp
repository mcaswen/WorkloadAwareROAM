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
struct Endpoint { EdgeKey Edge; NodeId Id; std::size_t Side; bool NewLeaf; };

// 按记录归并所有槽位，任务只写本记录，不在提交期间查询邻居状态
struct RecordPatch
{
    NodeRecord* Record;
    std::array<NodeId, 3> Neighbors;
    std::array<const NodeRecord*, 3> Pointers;
};

// 边分组先产生槽位更新，随后按身份排序，以检查重复写并确定唯一所有者
struct NeighborWrite { NodeId Id, Target; std::size_t Side; };

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
    std::vector<std::pair<NodeId, NodeRecord*>> newRecords;
    const auto reserve = [&](NodeId id) {
        const auto [it, inserted] = s._nodes.try_emplace(id);
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
    for (const auto id : target._removed) s.SetEvent(id, false);
    for (const auto id : target._added) s.SetEvent(id, true);
    for (const auto id : before) s.SetLeaf(id, false);
    for (const auto id : after) s.SetLeaf(id, true);
    for (const auto id : affected)
    {
        // 缓存允许保存休眠孩子，但它们的旧活动位和邻接不能继续影响后续查询
        const auto found = s._nodes.find(id);
        if (found == s._nodes.end()) continue;
        found->second.Internal = s.Event(id);
        found->second.Active = found->second.Internal || s.Leaf(id);
        if (!found->second.Active) { found->second.Neighbors = {}; found->second.NeighborRecords = {}; }
    }
    record(&WorkCounters::StateMaintenanceMs);
    const auto externalCount = endpoints.size();
    endpoints.resize(externalCount + after.size() * 3);
    // 此处复制的是变化叶和候选的局部描述，不把状态查询及其共享计数带入任务
    std::vector<std::pair<NodeId, Domain>> leafInputs;
    leafInputs.reserve(after.size());
    for (const auto id : after) leafInputs.emplace_back(id, s.Node(id).Triangle);
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
                const auto& [id, triangle] = leafInputs[i];
                const auto edges = MaterializationHierarchy::Edges(triangle);
                for (std::size_t side = 0; side < 3; ++side)
                    endpoints[externalCount + i * 3 + side] = {edges[side], id, side, true};
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
            writes.push_back({a.Id, 0, a.Side});
        }
        else if (last - first == 2)
        {
            const auto& b = endpoints[first + 1];
            if ((!a.NewLeaf && !b.NewLeaf) || a.Id == b.Id) throw std::logic_error("连接端点重复");
            writes.push_back({a.Id, b.Id, a.Side}); writes.push_back({b.Id, a.Id, b.Side});
        }
        else throw std::logic_error("直接连接产生多重边");
        first = last;
    }
    std::sort(writes.begin(), writes.end(), [](const auto& a, const auto& b) {
        return a.Id == b.Id ? a.Side < b.Side : a.Id < b.Id;
    });
    std::vector<RecordPatch> patches;
    patches.reserve(after.size() + externalCount);
    for (std::size_t i = 0; i < writes.size(); ++i)
    {
        const auto& write = writes[i];
        if (i == 0 || write.Id != writes[i - 1].Id)
        {
            auto& node = s._nodes.at(write.Id);
            patches.push_back({&node, node.Neighbors, node.NeighborRecords});
        }
        else if (write.Side == writes[i - 1].Side) throw std::logic_error("同一邻接槽位出现重复写者");
        auto& patch = patches.back();
        patch.Neighbors[write.Side] = write.Target;
        patch.Pointers[write.Side] = write.Target == 0 ? nullptr : &s._nodes.at(write.Target);
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
            writes.capacity() * sizeof(writes[0]) + patches.capacity() * sizeof(patches[0]);
    }
    if (s._leaves.size() != target._leafCount) throw std::logic_error("物化数量与认证不一致");
    s.FinishMutation();
    record(&WorkCounters::MaintenanceMs);
}
}
