#include "algorithms/data_oriented_roam/materialization/NativeRefinementSimulation.h"
#include "algorithms/data_oriented_roam/DataOrientedRoamScoring.h"

#include <algorithm>
#include <limits>

namespace ParallelRoam::Algorithms::DataOrientedRoam::Materialization
{
namespace
{
constexpr auto Invalid = InvalidDataOrientedRoamNodeIndex;
constexpr float Maximum = std::numeric_limits<float>::max();
using Activity = NativePlanningActivity;
}

template<bool Trace>
bool NativeRefinementSimulation<Trace>::Leaf(Node node) const
{
    // 原 primitive 判断缓存 IsSplit；队列资格另行检查活动成员，二者不能混用
    return _view.IsValidNode(node) && _view.Read(node, Field::IsSplit) == 0;
}

template<bool Trace>
bool NativeRefinementSimulation<Trace>::Active(Node node, Activity activity) const
{
    return _view.IsValidNode(node) && _view.Activity(node) == activity;
}

template<bool Trace>
float NativeRefinementSimulation<Trace>::Score(Node node)
{
    ++Work.ScoreEvaluations;
    const float score = ComputeScreenErrorScore(_view.Source(), _view.Domain(node), _view.GeometricError(node));
    // 首版不跳过原本需要的求值，缓存只复用给后续物化，不改变控制器工作量口径
    _scores[node] = score;
    return score;
}

template<bool Trace>
float NativeRefinementSimulation<Trace>::SplitScore(Node node)
{
    const auto& source = _view.Source();
    // 抑制值仍是有限数，成员留在 Q_s 中；队首停止规则决定本轮是否继续
    if (!Active(node, Activity::Leaf) || _view.Depth(node) >= source.Settings.MaxDepth ||
        _view.Read(node, Field::SplitBlockedBuild) == source.BuildSequence ||
        _view.Read(node, Field::MergeBuild) == source.BuildSequence) return -Maximum;
    return Score(node);
}

template<bool Trace>
bool NativeRefinementSimulation<Trace>::WantsSplit(Node node, float score) const
{
    return _view.IsValidNode(node) && ShouldSplitWithScore(_view.Source(), _view.Depth(node), _view.Path(node), score);
}

template<bool Trace>
auto NativeRefinementSimulation<Trace>::MergeRepresentative(Node node) -> Node
{
    ++Work.CandidateChecks;
    // 缓存存在不等于活动，父和孩子都必须仍由当前 cut 接纳
    if (!Active(node, Activity::Internal) || !Active(Relation(node, Field::LeftChild), Activity::Leaf) ||
        !Active(Relation(node, Field::RightChild), Activity::Leaf)) return Invalid;
    const auto base = Relation(node, Field::BaseNeighbor);
    if (!_view.IsValidNode(base) || Active(base, Activity::Leaf)) return node;
    // 跨菱形资格要求双向底边和四个活动叶，代表只由稳定身份决定
    if (!Active(base, Activity::Internal) || Relation(base, Field::BaseNeighbor) != node ||
        !Active(Relation(base, Field::LeftChild), Activity::Leaf) ||
        !Active(Relation(base, Field::RightChild), Activity::Leaf)) return Invalid;
    return _view.Path(node) < _view.Path(base) ? node : base;
}

template<bool Trace>
bool NativeRefinementSimulation<Trace>::CanMerge(Node node)
{
    // 实际执行前的复核与候选入堆条件分开，保留原评分时机和防御性失败分支
    if (!_view.IsValidNode(node) || Leaf(node) || !Leaf(Relation(node, Field::LeftChild)) ||
        !Leaf(Relation(node, Field::RightChild))) return false;
    if (Score(node) > Maximum) return false;
    // 评分上限按原比较处理，不用队列中的陈旧缓存代替 primitive 复核
    const auto base = Relation(node, Field::BaseNeighbor);
    if (!_view.IsValidNode(base) || Leaf(base)) return true;
    if (Relation(base, Field::BaseNeighbor) != node || !Leaf(Relation(base, Field::LeftChild)) ||
        !Leaf(Relation(base, Field::RightChild))) return false;
    return !(Score(base) > Maximum);
}

template<bool Trace>
void NativeRefinementSimulation<Trace>::AppendNeighborhood(Node node, DataOrientedRoamNeighborhood& nodes)
{
    if (!_view.IsValidNode(node)) return;
    const Node seeds[]{node, _view.Parent(node), Relation(node, Field::LeftChild), Relation(node, Field::RightChild),
        Relation(node, Field::BaseNeighbor), Relation(node, Field::LeftNeighbor), Relation(node, Field::RightNeighbor)};
    const auto append = [&](Node candidate) {
        ++Work.NeighborhoodVisits;
        if (_view.IsValidNode(candidate)) nodes.append_unique(candidate);
    };
    // 先保存修改前邻域，再追加修改后邻域；去重顺序保持与原局部维护一致
    for (const auto seed : seeds) append(seed);
    for (const auto seed : seeds)
        if (_view.IsValidNode(seed)) { append(_view.Parent(seed)); append(Relation(seed, Field::BaseNeighbor)); }
}

template<bool Trace>
void NativeRefinementSimulation<Trace>::Invalidate(const DataOrientedRoamNeighborhood& nodes)
{
    for (const auto node : nodes) (void)_queues.RemoveMerge(node);
}

template<bool Trace>
void NativeRefinementSimulation<Trace>::Refresh(const DataOrientedRoamNeighborhood& nodes)
{
    const auto build = _view.Source().BuildSequence;
    for (const auto node : nodes)
    {
        // 已存在的代表不重新评分，避免额外刷新改变原控制器的候选语义
        const auto representative = MergeRepresentative(node);
        if (representative == Invalid || _queues.MergeRepresentative(representative) != Invalid) continue;
        auto partner = Relation(representative, Field::BaseNeighbor);
        if (!Active(partner, Activity::Internal) || Relation(partner, Field::BaseNeighbor) != representative) partner = Invalid;
        float score = Maximum;
        // 本轮刚细分的任一侧都会抑制整组立即合并，不能仅检查代表一侧
        if (_view.Read(representative, Field::SplitBuild) != build &&
            (partner == Invalid || _view.Read(partner, Field::SplitBuild) != build))
        {
            score = Score(representative);
            if (partner != Invalid) score = std::max(score, Score(partner));
        }
        _queues.UpsertMerge(representative, partner, score);
        // 候选后来重新入堆，就不再承担先前失败移除的持续义务
        _failedMergeRemovals.erase(_view.Path(representative));
        if (partner != Invalid) _failedMergeRemovals.erase(_view.Path(partner));
    }
}

template<bool Trace>
void NativeRefinementSimulation<Trace>::Replace(Node neighbor, Node oldNode, Node newNode)
{
    if (!_view.IsValidNode(neighbor)) return;
    // 三个方向分别比较，不能用 else-if 漏掉暂态中对同一旧节点的重复引用
    for (const auto field : {Field::BaseNeighbor, Field::LeftNeighbor, Field::RightNeighbor})
        if (Relation(neighbor, field) == oldNode) _view.Write(neighbor, field, newNode);
}

template<bool Trace>
void NativeRefinementSimulation<Trace>::CommitSplit(Node node, Node base, bool forced)
{
    if (Relation(node, Field::LeftChild) == Invalid || Relation(node, Field::RightChild) == Invalid)
        (void)_view.CreateChildren(node);
    // 创建缓存先于邻域冻结，复用路径保留同一身份，均不写生产池
    DataOrientedRoamNeighborhood neighborhood;
    AppendNeighborhood(node, neighborhood); AppendNeighborhood(base, neighborhood);
    Invalidate(neighborhood);
    const auto left = Relation(node, Field::LeftChild), right = Relation(node, Field::RightChild);
    const auto build = _view.Source().BuildSequence;
    // 父激活轮次保持原值，只有孩子此次被重新激活；净出口必须区分这两种历史
    _view.Write(node, Field::IsSplit, 1); _view.Write(node, Field::SplitBuild, build);
    for (const auto child : {left, right})
    {
        for (const auto field : {Field::BaseNeighbor, Field::LeftNeighbor, Field::RightNeighbor})
            _view.Write(child, field, Invalid);
        _view.Write(child, Field::ActivatedBuild, build);
        _view.Write(child, Field::ForcedActivation, forced ? 1 : 0);
    }
    // 新关系仅为继续规划服务，最终邻接不随正常结果导出
    _view.Write(left, Field::LeftNeighbor, right); _view.Write(right, Field::RightNeighbor, left);
    const auto outerLeft = Relation(node, Field::LeftNeighbor), outerRight = Relation(node, Field::RightNeighbor);
    // 前置可能已改写外侧关系，此处读取当前私有值而不是入口快照
    _view.Write(left, Field::BaseNeighbor, outerLeft); _view.Write(right, Field::BaseNeighbor, outerRight);
    Replace(outerLeft, node, left); Replace(outerRight, node, right);
    if (_view.IsValidNode(base) && !Leaf(base))
    {
        const auto baseLeft = Relation(base, Field::LeftChild), baseRight = Relation(base, Field::RightChild);
        _view.Write(left, Field::RightNeighbor, baseRight); _view.Write(right, Field::LeftNeighbor, baseLeft);
        if (_view.IsValidNode(baseRight)) _view.Write(baseRight, Field::LeftNeighbor, left);
        if (_view.IsValidNode(baseLeft)) _view.Write(baseLeft, Field::RightNeighbor, right);
    }
    (void)_queues.RemoveSplit(node);
    _view.Write(node, Field::Activity, static_cast<std::uint64_t>(Activity::Internal));
    // Q_s 成员立即变化，两个孩子完成激活后才重新发现 Q_m 候选
    for (const auto child : {left, right})
    {
        _view.Write(child, Field::Activity, static_cast<std::uint64_t>(Activity::Leaf));
        _queues.UpsertSplit(child, SplitScore(child));
    }
    AppendNeighborhood(node, neighborhood); AppendNeighborhood(base, neighborhood);
    Refresh(neighborhood);
    // 路径追加用于私有历史对照，不能以本轮曾发生事件代替最终净事件
    _view.Write(node, Field::CurrentSplitPath, 1);
    ++Work.PrimitiveSplits;
    if (forced) ++Work.ForcedSplits;
    if constexpr (Trace) _trace->Emit(DecisionEventKind::SplitComplete, forced ? DecisionReason::Forced : DecisionReason::Requested);
}

template<bool Trace>
void NativeRefinementSimulation<Trace>::ReleaseBudget()
{
    const auto before = _view.RemainingBudget();
    _view.ReleaseBudget();
    // 递归作用域已恢复到父尝试，取消预留不会错记到已完成的子前置
    if constexpr (Trace) _trace->Emit(DecisionEventKind::Release, DecisionReason::None, before, _view.RemainingBudget());
}

template<bool Trace>
bool NativeRefinementSimulation<Trace>::Split(Node node, Node forcedFrom)
{
    const bool forced = forcedFrom != Invalid;
    ++Work.SplitAttempts;
    if (forced) ++Work.ForcedAttempts;
    std::uint64_t path = 0, from = 0;
    if constexpr (Trace)
    {
        path = _view.IsValidNode(node) ? _view.Path(node) : 0;
        from = _view.IsValidNode(forcedFrom) ? _view.Path(forcedFrom) : 0;
    }
    DecisionAttemptScope<Trace> attempt{_trace, path, from, forced};
    if (!Leaf(node)) return false;
    if (_view.Depth(node) >= _view.Source().Settings.MaxDepth)
    {
        attempt.Result = DecisionReason::DepthLimit;
        return false;
    }
    const auto before = _view.RemainingBudget();
    // 先预留自身净增名额，再探索前置，整个调用栈共同服从同一硬预算
    const bool reserved = _view.TryReserveBudget();
    if constexpr (Trace) _trace->Emit(DecisionEventKind::Reserve,
        reserved ? DecisionReason::Success : DecisionReason::BudgetRejected, before, _view.RemainingBudget());
    if (!reserved)
    {
        ++Work.BudgetRejections;
        attempt.Result = DecisionReason::BudgetRejected;
        return false;
    }
    // 失败只释放当前尚未完成的预留，已经成功的递归前置仍留在私有状态
    const auto prerequisite = [&](Node required) {
        if (Split(required, node)) return true;
        ReleaseBudget();
        attempt.Result = DecisionReason::PrerequisiteFailed;
        return false;
    };
    auto base = Relation(node, Field::BaseNeighbor);
    if (_view.Source().Settings.EnableLocalConstraints)
    {
        int guard = 0;
        while (_view.IsValidNode(base) && base != forcedFrom && Relation(base, Field::BaseNeighbor) != node &&
            guard < _view.Source().Settings.MaxDepth + 2)
        {
            if (!prerequisite(base)) return false;
            // 成功前置可替换本根底边，必须重新查关系后再判断下一项依赖
            base = Relation(node, Field::BaseNeighbor);
            ++guard;
        }
        // guard 达界本身不新增失败语义，继续执行原来的对侧叶检查
        if (_view.IsValidNode(base) && Leaf(base) && base != forcedFrom)
        {
            if (!prerequisite(base)) return false;
            base = Relation(node, Field::BaseNeighbor);
        }
    }
    CommitSplit(node, base, forced);
    attempt.Result = DecisionReason::Success;
    return true;
}

template<bool Trace>
void NativeRefinementSimulation<Trace>::CommitMerge(Node node)
{
    const auto left = Relation(node, Field::LeftChild), right = Relation(node, Field::RightChild);
    const auto outerLeft = Relation(left, Field::BaseNeighbor), outerRight = Relation(right, Field::BaseNeighbor);
    // 父恢复为叶时以孩子的当前外边为准，不复活父记录里的历史左右邻接
    Replace(outerLeft, left, node); Replace(outerRight, right, node);
    _view.Write(node, Field::LeftNeighbor, outerLeft); _view.Write(node, Field::RightNeighbor, outerRight);
    _view.Write(node, Field::IsSplit, 0);
    _view.Write(node, Field::ActivatedBuild, _view.Source().BuildSequence);
    _view.Write(node, Field::MergeBuild, _view.Source().BuildSequence);
    _view.Write(node, Field::ForcedActivation, 0);
    for (const auto child : {left, right})
    {
        (void)_queues.RemoveSplit(child);
        // 只撤销活动资格，缓存及其历史字段仍保留，后续可再次激活
        _view.Write(child, Field::Activity, static_cast<std::uint64_t>(Activity::Dormant));
    }
    _view.Write(node, Field::Activity, static_cast<std::uint64_t>(Activity::Leaf));
    _queues.UpsertSplit(node, SplitScore(node));
    if constexpr (Trace) { _trace->Node = _view.Path(node); _trace->From = 0; }
    ReleaseBudget();
    ++Work.PrimitiveMerges;
    if constexpr (Trace) _trace->Emit(DecisionEventKind::MergeComplete);
}

template<bool Trace>
bool NativeRefinementSimulation<Trace>::Merge(Node node)
{
    if (!CanMerge(node)) return false;
    const auto base = Relation(node, Field::BaseNeighbor);
    DataOrientedRoamNeighborhood neighborhood;
    AppendNeighborhood(node, neighborhood); AppendNeighborhood(base, neighborhood);
    Invalidate(neighborhood);
    const bool diamond = _view.IsValidNode(base) && !Leaf(base);
    if (diamond)
    {
        if (Relation(base, Field::BaseNeighbor) != node) return false;
        _view.Write(node, Field::BaseNeighbor, base); _view.Write(base, Field::BaseNeighbor, node);
    }
    CommitMerge(node);
    if (diamond)
    {
        CommitMerge(base);
        // 两侧的外邻接回写可能触及彼此，最后显式恢复父级底边互指
        _view.Write(node, Field::BaseNeighbor, base); _view.Write(base, Field::BaseNeighbor, node);
    }
    AppendNeighborhood(node, neighborhood);
    if (diamond) AppendNeighborhood(base, neighborhood);
    Refresh(neighborhood);
    return true;
}

template<bool Trace>
void NativeRefinementSimulation<Trace>::Block(Node node)
{
    if (!Active(node, Activity::Leaf)) return;
    // 失败处置属于本轮状态，既不删除活动叶，也不被净 refinement 差分自动表达
    _view.Write(node, Field::SplitBlockedBuild, _view.Source().BuildSequence);
    _queues.UpsertSplit(node, -Maximum);
}

template<bool Trace>
void NativeRefinementSimulation<Trace>::RemoveFailedMerge(Node node)
{
    (void)_queues.RemoveMerge(node);
    _failedMergeRemovals.insert(_view.Path(node));
}

template<bool Trace>
std::vector<NativeScoreEvaluation> NativeRefinementSimulation<Trace>::Evaluations() const
{
    std::vector<NativeScoreEvaluation> result;
    result.reserve(_scores.size());
    // 转换只扫描实际求值缓存，排序和结果分配都属于规划费用
    for (const auto& [node, score] : _scores) result.push_back({_view.Path(node), score});
    std::sort(result.begin(), result.end(), [](const auto& a, const auto& b) { return a.Path < b.Path; });
    return result;
}

template<bool Trace>
bool NativeRefinementSimulation<Trace>::MissingFailedMerge(std::uint64_t path)
{
    const auto node = _view.FindPath(path);
    // 最终已不具备候选资格的旧失败没有持续效果，不应阻止物化后的正常资格恢复
    return node != Invalid && MergeRepresentative(node) == node && _queues.MergeRepresentative(node) == Invalid;
}

template class NativeRefinementSimulation<false>;
template class NativeRefinementSimulation<true>;
}
