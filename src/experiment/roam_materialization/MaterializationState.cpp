#include "experiment/roam_materialization/MaterializationState.h"

#include <algorithm>
#include <cmath>
#include <exception>
#include <limits>
#include <stdexcept>

namespace ParallelRoam::Experiment::RoamMaterialization
{
namespace
{
OrderedEntry Key(NodeId id, QueueValue value, bool split)
{
    // 资格和排序键分别保存，抑制不会丢失候选；同分最终由路径打破
    return {value.Suppressed ? std::numeric_limits<float>::max() :
        (split ? -value.Score : value.Score), id};
}
}

MaterializationState::MaterializationState(std::shared_ptr<const FrozenEnvironment> environment,
    std::size_t budget) : _environment(std::move(environment)), _budget(budget)
{
    if (!_environment || !_environment->Score || !_environment->Emit || budget < 2 ||
        _environment->MaxDepth < 0 || _environment->MaxDepth > MaximumDepth)
        throw std::invalid_argument("无效物化环境或预算");
    Ensure(RootA); Ensure(RootB);
    SetLeaf(RootA, true); SetLeaf(RootB, true);
    SetNeighbor(RootA, 0, RootB); SetNeighbor(RootB, 0, RootA);
    RefreshAllQueues();
    ConsumePending();
}

MaterializationState::MaterializationState(const MaterializationState& s)
    : _environment(s._environment), _version(s._version), _nodes(s._nodes), _events(s._events),
      _leaves(s._leaves), _splitQueue(s._splitQueue), _mergeQueue(s._mergeQueue),
      _splitOrder(s._splitOrder), _mergeOrder(s._mergeOrder), _history(s._history),
      _pending(s._pending), _consumed(s._consumed), _slots(s._slots), _freeSlots(s._freeSlots),
      _nextSlot(s._nextSlot), _budget(s._budget), _usable(s._usable)
{
    RebindNeighbors();
}

MaterializationState::WorkSession::WorkSession(MaterializationState& state, WorkCounters* work)
    : _state(state), _exceptions(std::uncaught_exceptions())
{
    // 计时遍没有计数器，重入检测仍必须有效，不能用空指针代替事务状态
    if (!state._usable || state._inTransaction) throw std::logic_error("不能重入或使用失败状态");
    state._inTransaction = true;
    state._work = work;
}

MaterializationState::WorkSession::~WorkSession()
{
    // 不承诺任意分配失败后的强回滚，异常离开时整个自有副本失去发布资格
    _state._work = nullptr;
    _state._inTransaction = false;
    if (std::uncaught_exceptions() > _exceptions) _state._usable = false;
}

const NodeRecord& MaterializationState::Node(NodeId id) const
{
    if (_work) { ++_work->RecordQueries; ++_work->NodeIndexProbes; }
    return _nodes.at(id);
}

bool MaterializationState::Event(NodeId id) const
{
    if (_work) ++_work->EventQueries;
    return _events.contains(id);
}

bool MaterializationState::Leaf(NodeId id) const
{
    if (_work) ++_work->RecordQueries;
    return _leaves.contains(id);
}

NodeId MaterializationState::Group(NodeId id) const
{
    // 已有记录复用静态伙伴；尚未物化的身份才支付层次解码费用
    if (_work) { ++_work->RecordQueries; ++_work->NodeIndexProbes; }
    const auto it = _nodes.find(id);
    const auto mate = it == _nodes.end() ? MaterializationHierarchy::Mate(id) : it->second.Mate;
    return mate == 0 ? id : std::min(id, mate);
}

std::vector<NodeId> MaterializationState::Members(NodeId id) const
{
    if (_work) { ++_work->RecordQueries; ++_work->NodeIndexProbes; }
    const auto it = _nodes.find(id);
    const auto mate = it == _nodes.end() ? MaterializationHierarchy::Mate(id) : it->second.Mate;
    return mate == 0 ? std::vector<NodeId>{id} : std::vector<NodeId>{std::min(id, mate), std::max(id, mate)};
}

float MaterializationState::Score(NodeId id) const
{
    // 两种算法都在需要时调用同一冻结函数，不预填直接路径将要使用的答案
    if (_work) ++_work->ScoreEvaluations;
    const auto& node = Node(id);
    return EvaluateScore(*_environment, id, node.Triangle);
}

float MaterializationState::EvaluateScore(const FrozenEnvironment& environment, NodeId id, const Domain& triangle)
{
    const float score = environment.Score(id, triangle);
    if (!std::isfinite(score) || score < 0) throw std::invalid_argument("评分必须是有限非负值");
    return score;
}

NodeRecord& MaterializationState::Ensure(NodeId id)
{
    if (_work) ++_work->NodeIndexProbes;
    auto [it, inserted] = _nodes.try_emplace(id);
    if (!inserted)
    {
        // 统计的是命中缓存的 Ensure 调用次数，不是独立记录的重新激活数量
        if (_work) ++_work->RecordsReused;
        return it->second;
    }
    if (_work) ++_work->RecordsCreated;
    it->second = BuildRecord(id);
    return it->second;
}

NodeRecord MaterializationState::BuildRecord(NodeId id)
{
    NodeRecord node;
    // 父子与伙伴属于逻辑身份属性，休眠记录重新激活时不必重新推导
    node.Id = id;
    node.Depth = MaterializationHierarchy::Depth(id);
    node.Parent = MaterializationHierarchy::Parent(id);
    node.Mate = MaterializationHierarchy::Mate(id);
    node.Triangle = MaterializationHierarchy::Decode(id);
    if (node.Depth < MaximumDepth) node.Children = MaterializationHierarchy::Children(id);
    return node;
}

void MaterializationState::SetNeighbor(NodeId id, std::size_t edge, NodeId target)
{
    // 身份供逻辑比较，指针供后续局部访问；两份引用必须同时指向本状态
    if (_work) _work->NodeIndexProbes += target == 0 ? 1 : 2;
    auto& node = _nodes.at(id);
    node.Neighbors.at(edge) = target;
    node.NeighborRecords.at(edge) = target == 0 ? nullptr : &_nodes.at(target);
    if (_work) ++_work->NeighborWrites;
}

void MaterializationState::SetEvent(NodeId id, bool present)
{
    SetEvent(Ensure(id), present);
}

void MaterializationState::SetEvent(NodeRecord& node, bool present)
{
    // 标记保留本轮已经发生的方向，不把合并再细分误当成没有历史
    const auto id = node.Id;
    node.Active = true;
    node.Internal = present;
    if (present) { _events.insert(id); _history.Split.insert(id); }
    else { _events.erase(id); _history.Merged.insert(id); }
}

void MaterializationState::SetLeaf(NodeId id, bool present)
{
    SetLeaf(Ensure(id), present);
}

void MaterializationState::SetLeaf(NodeRecord& node, bool present)
{
    const auto id = node.Id;
    if (present)
    {
        node.Active = true; node.Internal = false;
        node.Neighbors = {}; node.NeighborRecords = {};
        _leaves.insert(id);
        if (!_slots.contains(id))
        {
            // 尚在消费者基线中的身份继续拥有原槽位，只有真正新增身份才分配
            if (_freeSlots.empty())
            {
                _slots[id] = _nextSlot++;
                if (_work) ++_work->SlotAllocations;
            }
            else
            {
                _slots[id] = _freeSlots.back(); _freeSlots.pop_back();
                if (_work) ++_work->SlotReuses;
            }
        }
    }
    else
    {
        _leaves.erase(id);
        node.Active = _events.contains(id);
        node.Neighbors = {}; node.NeighborRecords = {};
    }
    // 消费者在事务间保持不变，因此它就是多次合成共同的最早基线
    const bool baseline = _consumed.contains(id);
    if (baseline == present) _pending.erase(id);
    else _pending[id] = {baseline, present};
    if (!present && !baseline)
    {
        // 未被消费过的短暂叶可以立即回收；已消费叶须等删除确认
        const auto slot = _slots.find(id);
        if (slot != _slots.end()) { _freeSlots.push_back(slot->second); _slots.erase(slot); }
    }
    if (_work) ++_work->PendingWrites;
}

bool MaterializationState::MergeReady(NodeId group) const
{
    // 在父/伙伴闭合的不变量下，组内直接孩子未细分就足以判定局部合并资格
    for (const auto id : Members(group))
    {
        if (!Event(id)) return false;
        for (const auto child : Node(id).Children)
            if (Event(child)) return false;
    }
    return true;
}

MaterializationState::CandidateInput MaterializationState::PrepareSplit(NodeId id) const
{
    if (_work) ++_work->SplitRechecks;
    CandidateInput input;
    input.Id = id;
    if (Leaf(id))
    {
        // 抑制时评分没有执行意义，规范化为零并保留独立抑制位
        const auto& node = Node(id);
        input.Present = true;
        input.Suppressed = node.Depth >= _environment->MaxDepth ||
            _history.Blocked.contains(id) || _history.Merged.contains(id);
        if (!input.Suppressed)
        {
            input.Samples = 1; input.Members[0] = id; input.Triangles[0] = node.Triangle;
        }
    }
    return input;
}

MaterializationState::CandidateInput MaterializationState::PrepareMerge(NodeId group) const
{
    // 调用者传入唯一组代表；局部刷新可以重复，但绝不扫描其他组
    if (_work) ++_work->MergeRechecks;
    CandidateInput input;
    input.Id = group;
    if (MergeReady(group))
    {
        // 任一侧本轮刚细分都会抑制整个菱形，不能只检查代表的一侧
        input.Present = true;
        const auto members = Members(group);
        for (const auto id : members) input.Suppressed |= _history.Split.contains(id);
        if (!input.Suppressed)
            for (const auto id : members)
            {
                input.Members[input.Samples] = id;
                input.Triangles[input.Samples++] = Node(id).Triangle;
            }
    }
    return input;
}

QueueValue MaterializationState::EvaluateCandidate(const FrozenEnvironment& environment, const CandidateInput& input)
{
    QueueValue value{0, input.Suppressed};
    for (std::size_t i = 0; i < input.Samples; ++i)
        value.Score = std::max(value.Score, EvaluateScore(environment, input.Members[i], input.Triangles[i]));
    return value;
}

void MaterializationState::InstallCandidate(const CandidateInput& input, QueueValue value, bool split)
{
    // 同时更新成员与排序索引；准备结果只在同一固定目标代中有效
    auto& queue = split ? _splitQueue : _mergeQueue;
    auto& order = split ? _splitOrder : _mergeOrder;
    const auto old = queue.find(input.Id);
    if (_work)
    {
        const bool absent = old == queue.end();
        _work->QueueAbsentRefreshes += absent && !input.Present;
        _work->QueueUnchangedRefreshes += absent ? !input.Present : input.Present && old->second == value;
    }
    if (old != queue.end())
    {
        const auto erased = order.erase(Key(input.Id, old->second, split));
        queue.erase(old);
        if (_work) { ++_work->QueueMemberErases; _work->QueueOrderErases += erased; }
    }
    if (input.Present)
    {
        const bool member = queue.emplace(input.Id, value).second;
        const bool ordered = order.insert(Key(input.Id, value, split)).second;
        if (_work) { _work->QueueMemberInserts += member; _work->QueueOrderInserts += ordered; }
    }
    if (_work) ++_work->QueueWrites;
}

void MaterializationState::RefreshSplit(NodeId id)
{
    const auto input = PrepareSplit(id);
    if (_work) _work->ScoreEvaluations += input.Samples;
    InstallCandidate(input, EvaluateCandidate(*_environment, input), true);
}

void MaterializationState::RefreshMerge(NodeId group)
{
    const auto input = PrepareMerge(group);
    if (_work) _work->ScoreEvaluations += input.Samples;
    InstallCandidate(input, EvaluateCandidate(*_environment, input), false);
}

void MaterializationState::RefreshAllQueues()
{
    // 仅初始化、导入和换轮使用；目标事务必须通过支持集逐项刷新
    _splitQueue.clear(); _mergeQueue.clear(); _splitOrder.clear(); _mergeOrder.clear();
    for (const auto id : _leaves) { RefreshSplit(id); if (_work) ++_work->FullScanItems; }
    for (const auto id : _events)
        if (Group(id) == id) { RefreshMerge(id); if (_work) ++_work->FullScanItems; }
}

std::optional<NodeId> MaterializationState::PeekSplit() const
{
    if (_splitOrder.empty()) return {};
    return _splitOrder.begin()->Id;
}

std::optional<NodeId> MaterializationState::PeekMerge() const
{
    if (_mergeOrder.empty()) return {};
    return _mergeOrder.begin()->Id;
}

bool MaterializationState::ShouldSplit(NodeId id) const
{
    // 历史只影响两阈值之间的迟滞区间，不覆盖深度和本轮抑制
    const auto it = _splitQueue.find(id);
    if (it == _splitQueue.end() || it->second.Suppressed) return false;
    const float score = it->second.Score;
    return score > _environment->SplitThreshold ||
        (score >= _environment->MergeThreshold && _history.Previous.contains(id));
}

void MaterializationState::ConsumePending(WorkCounters* work)
{
    // 消费遍历全部待确认条目，费用按其实际规模计，不受本次目标 k 限制
    WorkSession session(*this, work);
    for (const auto& [id, change] : _pending)
    {
        if (change.Target) _consumed[id] = _environment->Emit(id, Node(id).Triangle);
        else
        {
            // 删除先撤销消费者持有关系，再把已无读者的槽位交还空闲表
            _consumed.erase(id);
            const auto slot = _slots.find(id);
            if (slot == _slots.end()) throw std::logic_error("待消费删除丢失槽位");
            _freeSlots.push_back(slot->second); _slots.erase(slot);
        }
        if (_work) ++_work->FullScanItems;
    }
    _pending.clear(); FinishMutation();
}

void MaterializationState::AdvanceEpoch(WorkCounters* work)
{
    WorkSession session(*this, work);
    // 明确记录全量迟滞复制和过期标记清理，不把它们摊进当前 k 的局部界
    if (_work) _work->FullScanItems += _events.size() + _history.Previous.size() +
        _history.Blocked.size() + _history.Split.size() + _history.Merged.size();
    _history.Previous = _events;
    _history.Blocked.clear(); _history.Split.clear(); _history.Merged.clear();
    RefreshAllQueues(); FinishMutation();
}

void MaterializationState::SetHistory(History history)
{
    WorkSession session(*this, nullptr);
    _history = std::move(history); RefreshAllQueues(); FinishMutation();
}

void MaterializationState::FinishMutation()
{
    _version = std::make_shared<StateVersion>();
}

void MaterializationState::RebindNeighbors()
{
    // 深复制保留数值身份，但地址属于新副本；这次全量恢复在事务时钟之外
    for (auto& [id, node] : _nodes)
    {
        static_cast<void>(id);
        for (std::size_t edge = 0; edge < 3; ++edge)
            node.NeighborRecords[edge] = node.Neighbors[edge] == 0 ? nullptr : &_nodes.at(node.Neighbors[edge]);
    }
}
}
