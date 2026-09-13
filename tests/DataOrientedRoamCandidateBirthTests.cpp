#include "DataOrientedRoamExperimentTestSupport.h"
#include "DataOrientedRoamNativeMaterializationTestSupport.h"
#include "algorithms/data_oriented_roam/DataOrientedRoamQueues.h"
#include "algorithms/data_oriented_roam/DataOrientedRoamScoring.h"
#include "algorithms/data_oriented_roam/DataOrientedRoamStateOps.h"
#include "algorithms/data_oriented_roam/DataOrientedRoamVariance.h"

#include <chrono>
#include <iostream>
#include <limits>
#include <map>
#include <optional>
#include <set>
#include <tuple>

using namespace ParallelRoam;
using namespace Tests;
namespace Dod = Algorithms::DataOrientedRoam;

namespace
{
using State = Dod::DataOrientedRoamState;
using Node = Dod::DataOrientedRoamNodeIndex;
using Path = std::uint64_t;
using Identity = std::tuple<bool, Path, Path>;
constexpr auto Invalid = Dod::InvalidDataOrientedRoamNodeIndex;
constexpr auto NoPosition = Dod::InvalidDataOrientedRoamPosition;
constexpr float Maximum = std::numeric_limits<float>::max();

/// <summary>
/// 逻辑组身份与排序用的代表路径分开，成员改组会生成不同身份
/// </summary>
struct Candidate
{
    bool Merge{false};
    Path Representative{0}, Partner{0};
    float Score{0};
    Identity Id() const { return {Merge, Partner ? std::min(Representative, Partner) : Representative,
        Partner ? std::max(Representative, Partner) : 0}; }
    bool operator==(const Candidate&) const = default;
};
using Registry = std::map<Identity, Candidate>;

Path PathAt(const State& state, Node node)
{
    return state.IsValidNode(node) ? state.Nodes.PathIdAt(node) : 0;
}

Node FindEventNode(const State& state, Path path)
{
    // 现有事件只暴露 Path；线性定位属于诊断适配，不是所提局部规则的节点访问
    const auto found = std::find(state.Nodes.PathIds.begin(), state.Nodes.PathIds.end(), path);
    return found == state.Nodes.PathIds.end() ? Invalid : static_cast<Node>(found - state.Nodes.PathIds.begin());
}

bool Active(const State& state, Node node, bool internal)
{
    return state.IsValidNode(node) && (internal ? state.NodeMembership[node].ActiveInternalPosition :
        state.NodeMembership[node].ActiveLeafPosition) != NoPosition;
}

Registry Actual(const State& state)
{
    // 参考答案从真实队列完整读取；局部枚举器不能读取本函数的结果
    Registry result;
    for (const auto& entry : state.SplitQueue)
    {
        Candidate value{false, PathAt(state, entry.Node), 0, entry.Score};
        Require(result.emplace(value.Id(), value).second, "duplicate source split identity");
    }
    for (const auto& entry : state.MergeQueue)
    {
        Candidate value{true, PathAt(state, entry.Node),
            PathAt(state, state.NodeMembership[entry.Node].MergeQueuePartner), entry.Score};
        Require(result.emplace(value.Id(), value).second, "duplicate source merge identity");
    }
    return result;
}

bool Precedes(const Candidate& a, const Candidate& b)
{
    if (a.Score != b.Score) return a.Merge ? a.Score < b.Score : a.Score > b.Score;
    return a.Representative < b.Representative;
}

/// <summary>
/// 无序版本账本只核查覆盖和严格极值，不实现堆、反向槽位或新的控制器
/// 局部登记变化暂存到区间结束；未变的端点条目无需发布新版本
/// </summary>
class PublicationLedger
{
public:
    explicit PublicationLedger(const Registry& source) : _current(source)
    {
        for (const auto& [id, value] : source)
        {
            Require(std::isfinite(value.Score), "non-finite source key outside proof domain");
            Associate(value); _revision[id] = 1; _entries.push_back({value, 1});
        }
        Peak = _entries.size();
    }

    bool ContainsMember(bool merge, Path path) const { return _members.contains({merge, path}); }

    void Remove(bool merge, Path path)
    {
        const auto found = _members.find({merge, path});
        if (found == _members.end()) return;
        const auto id = found->second;
        Touch(id);
        const auto old = _current.at(id);
        _members.erase({merge, old.Representative});
        if (old.Partner) _members.erase({merge, old.Partner});
        _current.erase(id);
        ++LogicalRemoves;
    }

    void Insert(const Candidate& value)
    {
        Require(std::isfinite(value.Score), "non-finite generated key outside proof domain");
        Touch(value.Id());
        _current[value.Id()] = value;
        Associate(value); _allowed.insert(value.Id());
        ++LogicalInserts;
        if (value.Merge)
        {
            Readmissions += Failed.erase(value.Representative);
            if (value.Partner) Readmissions += Failed.erase(value.Partner);
        }
    }

    void Publish()
    {
        for (const auto& [id, before] : _before)
        {
            const auto found = _current.find(id);
            const auto after = found == _current.end() ? std::optional<Candidate>{} : found->second;
            if (before == after) { ++Unchanged; continue; }
            ++_revision[id]; ++RevisionChanges;
            if (after)
            {
                Require(_allowed.contains(id), "changed live entry was not locally published");
                _entries.push_back({*after, _revision[id]}); ++Pushes;
                if (!before) ++Births;
            }
        }
        Peak = std::max(Peak, _entries.size());
        _before.clear(); _allowed.clear();
    }

    void Check(const Registry& expected)
    {
        Require(_before.empty(), "strict observation inside unfinished publication");
        Require(_current == expected, "local candidate registry disagrees with Legacy");
        for (const auto& [id, value] : expected)
            Require(std::any_of(_entries.begin(), _entries.end(), [&](const auto& entry) {
                return entry.Value == value && Valid(entry);
            }), "current-entry coverage missing");
        for (const bool merge : {false, true})
        {
            std::optional<Candidate> best;
            for (const auto& [id, value] : expected)
                if (value.Merge == merge && (!best || Precedes(value, *best))) best = value;
            Require(Peek(merge) == best, "strict extremum mismatch");
        }
        ++Checks;
    }

    void CheckObservedHead(bool merge, const std::optional<Candidate>& expected)
    {
        Require(Peek(merge) == expected, "version ledger differs from actual Legacy heap head");
    }

    std::set<Path> Failed;
    std::size_t LogicalRemoves{0}, LogicalInserts{0}, Pushes{0}, Births{0}, Readmissions{0};
    std::size_t RevisionChanges{0}, Unchanged{0}, StalePops{0}, Peak{0}, Checks{0};

private:
    /// <summary>
    /// 一次发布绑定一个版本，旧条目允许继续保留到头部观察时再排除
    /// </summary>
    struct Versioned
    {
        Candidate Value;
        std::size_t Revision;
    };
    Registry _current;
    std::map<std::pair<bool, Path>, Identity> _members;
    std::map<Identity, std::size_t> _revision;
    std::map<Identity, std::optional<Candidate>> _before;
    std::set<Identity> _allowed;
    std::vector<Versioned> _entries;

    void Touch(const Identity& id)
    {
        if (_before.contains(id)) return;
        const auto found = _current.find(id);
        _before[id] = found == _current.end() ? std::optional<Candidate>{} : found->second;
    }
    void Associate(const Candidate& value)
    {
        _members[{value.Merge, value.Representative}] = value.Id();
        if (value.Partner) _members[{value.Merge, value.Partner}] = value.Id();
    }
    bool Valid(const Versioned& entry) const
    {
        const auto found = _current.find(entry.Value.Id());
        return found != _current.end() && found->second == entry.Value &&
            _revision.at(entry.Value.Id()) == entry.Revision;
    }
    std::optional<Candidate> Peek(bool merge)
    {
        // 线性找头模拟有序观察，实际复杂度和弹出次数都只属于本诊断账本
        for (;;)
        {
            std::size_t best = _entries.size();
            for (std::size_t i = 0; i < _entries.size(); ++i)
                if (_entries[i].Value.Merge == merge &&
                    (best == _entries.size() || Precedes(_entries[i].Value, _entries[best].Value))) best = i;
            if (best == _entries.size()) return std::nullopt;
            if (Valid(_entries[best])) return _entries[best].Value;
            _entries.erase(_entries.begin() + static_cast<std::ptrdiff_t>(best)); ++StalePops;
        }
    }
};

void AppendLocal(const State& state, Node node, std::vector<Node>& result)
{
    if (!state.IsValidNode(node)) return;
    const std::array seeds{node, state.Nodes.ParentAt(node), state.Nodes.LeftChildAt(node),
        state.Nodes.RightChildAt(node), state.Nodes.BaseNeighborAt(node),
        state.Nodes.LeftNeighborAt(node), state.Nodes.RightNeighborAt(node)};
    const auto append = [&](Node value) {
        if (state.IsValidNode(value) && std::find(result.begin(), result.end(), value) == result.end()) result.push_back(value);
    };
    // 显式展开论文中的七个种子与 parent/base，避免调用旧 Refresh 作为答案
    for (const auto seed : seeds) append(seed);
    for (const auto seed : seeds)
        if (state.IsValidNode(seed)) { append(state.Nodes.ParentAt(seed)); append(state.Nodes.BaseNeighborAt(seed)); }
}

std::optional<Candidate> GroupAt(const State& state, Node node)
{
    if (!Active(state, node, true) || !Active(state, state.Nodes.LeftChildAt(node), false) ||
        !Active(state, state.Nodes.RightChildAt(node), false)) return std::nullopt;
    const auto base = state.Nodes.BaseNeighborAt(node);
    Node partner = Invalid;
    if (state.IsValidNode(base) && !Active(state, base, false))
    {
        if (!Active(state, base, true) || state.Nodes.BaseNeighborAt(base) != node ||
            !Active(state, state.Nodes.LeftChildAt(base), false) || !Active(state, state.Nodes.RightChildAt(base), false))
            return std::nullopt;
        partner = base;
        if (PathAt(state, base) < PathAt(state, node)) { partner = node; node = base; }
    }
    return Candidate{true, PathAt(state, node), PathAt(state, partner), 0};
}

float SplitKey(const State& state, Node node)
{
    if (!Active(state, node, false) || state.Nodes.DepthAt(node) >= state.Settings.MaxDepth ||
        state.SplitQueueBlockedBuildIds[node] == state.BuildSequence || state.Nodes.MergeBuildIdAt(node) == state.BuildSequence)
        return -Maximum;
    return Dod::ComputeScreenErrorScore(state, node);
}

/// <summary>
/// 观察适配器持有小状态前快照；出生规则仅使用当前操作和声明的局部节点
/// 完整队列投影只在规则执行后核对，不参与候选发现
/// </summary>
class BirthAudit
{
public:
    explicit BirthAudit(State& state) : Ledger(Actual(state)), _state(state), _before(std::make_unique<State>(state)) {}
    Dod::DecisionTraceSink Sink() { return {this, &Receive}; }
    void Check() { Require(Error.empty(), Error); Verify(); }

    void Reconcile(const std::vector<Node>& oldSupport, const std::vector<Node>& support)
    {
        Require(support.size() <= 84, "local support exceeded paper bound");
        MaximumSupport = std::max(MaximumSupport, support.size()); Visits += support.size();
        for (const auto node : oldSupport) Ledger.Remove(true, PathAt(*_before, node));
        for (const auto node : support)
        {
            ++Eligibility;
            auto candidate = GroupAt(_state, node);
            if (!candidate || Ledger.ContainsMember(true, candidate->Representative)) continue;
            const auto representative = PathAt(_state, node) == candidate->Representative ? node : _state.Nodes.BaseNeighborAt(node);
            const auto partner = candidate->Partner ? _state.Nodes.BaseNeighborAt(representative) : Invalid;
            if (candidate->Partner) Ledger.Remove(true, candidate->Partner);
            // 键只在原生同样会重新入队时计算；已有未触及 storedScore 保留
            candidate->Score = Maximum;
            if (_state.Nodes.SplitBuildIdAt(representative) != _state.BuildSequence &&
                (!_state.IsValidNode(partner) || _state.Nodes.SplitBuildIdAt(partner) != _state.BuildSequence))
            {
                candidate->Score = Dod::ComputeScreenErrorScore(_state, representative);
                if (_state.IsValidNode(partner)) candidate->Score = std::max(candidate->Score, Dod::ComputeScreenErrorScore(_state, partner));
            }
            if (!candidate->Partner && _state.IsValidNode(_state.Nodes.BaseNeighborAt(representative))) ++InteriorSingles;
            Ledger.Insert(*candidate);
        }
    }

    void ExplicitFailureRemoval(Node node)
    {
        const auto path = PathAt(_state, node);
        Ledger.Remove(true, path); Ledger.Failed.insert(path);
        Ledger.Publish(); Finish();
    }
    void ExplicitReadmission(Node node)
    {
        Reconcile({}, {node}); Ledger.Publish(); Finish();
    }

    PublicationLedger Ledger;
    std::string Error;
    std::size_t Splits{0}, Forced{0}, Merges{0}, Blocks{0}, Failures{0}, PartialFailures{0};
    std::size_t Visits{0}, Eligibility{0}, MaximumSupport{0}, InteriorSingles{0}, Created{0};

private:
    State& _state;
    std::unique_ptr<State> _before;
    std::vector<Node> _merged;
    std::size_t _rootCompleted{0};

    void Verify()
    {
        Ledger.Check(Actual(_state));
        // 除数学极值外，还直接比对原堆首项，避免错误的来源堆形状被全量排序掩盖
        std::optional<Candidate> split, merge;
        if (!_state.SplitQueue.empty())
        {
            const auto& entry = _state.SplitQueue.front();
            split = Candidate{false, PathAt(_state, entry.Node), 0, entry.Score};
        }
        if (!_state.MergeQueue.empty())
        {
            const auto& entry = _state.MergeQueue.front();
            merge = Candidate{true, PathAt(_state, entry.Node),
                PathAt(_state, _state.NodeMembership[entry.Node].MergeQueuePartner), entry.Score};
        }
        Ledger.CheckObservedHead(false, split); Ledger.CheckObservedHead(true, merge);
    }
    void Finish()
    {
        Verify();
        Created += _state.Nodes.size() - _before->Nodes.size();
        _before = std::make_unique<State>(_state);
    }
    void AddSplitEntry(Node node)
    {
        Ledger.Insert({false, PathAt(_state, node), 0, SplitKey(_state, node)});
    }
    void Operation(Node root, bool split)
    {
        const auto base = _before->Nodes.BaseNeighborAt(root);
        std::vector<Node> oldSupport;
        AppendLocal(*_before, root, oldSupport); AppendLocal(*_before, base, oldSupport);
        auto support = oldSupport;
        AppendLocal(_state, root, support);
        if (split || _merged.size() == 2) AppendLocal(_state, base, support);
        if (split)
        {
            Ledger.Remove(false, PathAt(_state, root));
            AddSplitEntry(_state.Nodes.LeftChildAt(root)); AddSplitEntry(_state.Nodes.RightChildAt(root));
        }
        else for (const auto node : _merged)
        {
            Ledger.Remove(false, PathAt(*_before, _before->Nodes.LeftChildAt(node)));
            Ledger.Remove(false, PathAt(*_before, _before->Nodes.RightChildAt(node)));
            AddSplitEntry(node);
        }
        Reconcile(oldSupport, support); Ledger.Publish(); Finish();
    }
    void Observe(const Dod::DecisionEvent& event)
    {
        using Kind = Dod::DecisionEventKind;
        const auto node = FindEventNode(_state, event.Node);
        if (event.Kind == Kind::SelectSplit) _rootCompleted = 0;
        if (event.Kind == Kind::SelectMerge) _merged.clear();
        if (event.Kind == Kind::SplitComplete)
        {
            Operation(node, true); ++Splits; ++_rootCompleted;
            if (event.Reason == Dod::DecisionReason::Forced) ++Forced;
        }
        else if (event.Kind == Kind::MergeComplete) { _merged.push_back(node); ++Merges; }
        else if (event.Kind == Kind::MergeRootEnd && event.Reason == Dod::DecisionReason::Success)
            Operation(node, false);
        else if (event.Kind == Kind::BlockSplit)
        {
            AddSplitEntry(node); Ledger.Publish(); Finish(); ++Blocks;
        }
        else if (event.Kind == Kind::RemoveMerge) { ExplicitFailureRemoval(node); ++Failures; }
        // 半个菱形的完成事件尚未刷新队列；只在真正的请求/根出口核对头部
        if (event.Kind == Kind::SelectSplit || event.Kind == Kind::SelectMerge || event.Kind == Kind::Stop ||
            event.Kind == Kind::SplitRootEnd || event.Kind == Kind::MergeRootEnd) Verify();
        if (event.Kind == Kind::SplitRootEnd && event.Reason == Dod::DecisionReason::Failure && _rootCompleted) ++PartialFailures;
    }
    static void Receive(void* context, const Dod::DecisionEvent& event)
    {
        auto& self = *static_cast<BirthAudit*>(context);
        if (!self.Error.empty()) return;
        // 接收器契约禁止向生产调用栈抛异常；保存错误，在外层严格迭代结束后报告
        try { self.Observe(event); }
        catch (const std::exception& error) { self.Error = error.what(); }
    }
};

State Seed(const Terrain::HeightMap& height)
{
    State state;
    state.HeightMap = &height; state.TerrainSize = 30; state.HeightScale = 4;
    state.Settings.MaxDepth = 6; state.Settings.TriangleBudget = 128;
    state.Settings.SplitThreshold = -1; state.Settings.MergeThreshold = -2;
    state.Settings.EnableLocalConstraints = true; state.Settings.MirrorSplitScoresToNodePool = false;
    state.Settings.PassPolicy = Algorithms::MakeTerrainLodSerialIncrementalPolicy(); state.BuildSequence = 1;
    const auto view = ExperimentView(0);
    state.ViewProjection = view.ViewProjection; state.FrustumPlanes = view.FrustumPlanes;
    state.DrawableWidth = 1280; state.DrawableHeight = 720;
    Dod::RebuildVarianceTrees(state, 6); Dod::ResetTopology(state);
    state.Settings.MaxDepth = 3; Dod::AdvanceSplitTopologySerialForExperiment(state);
    state.Settings.MaxDepth = 6;
    Require(state.ActiveLeafNodes.size() == 16, "unexpected seed");
    return state;
}

void Refresh(State& state)
{
    ++state.BuildSequence; state.CurrentSplitPaths.clear();
    Dod::RefreshPersistentMergeQueuePriorities(state); Dod::RefreshPersistentSplitQueuePriorities(state);
}

/// <summary>
/// 用实际覆盖断言约束夹具，避免输出了诊断行却没有触发声明的边界
/// </summary>
struct RunEvidence
{
    std::size_t Splits, Forced, Merges, Failures, Blocks, Partial, Created, InteriorSingles, Readmissions;
    Dod::TopologySplitStop Stop;
};

RunEvidence Run(std::string_view name, State state, std::size_t limit = 2048)
{
    const auto begin = std::chrono::steady_clock::now();
    auto iteration = Dod::BeginStrictSplitIteration(state);
    BirthAudit audit{state};
    for (std::size_t i = 0; i < limit && iteration.Stop == Dod::TopologySplitStop::Running; ++i)
    {
        (void)Dod::AdvanceStrictSplitIterationWithDecisions(state, iteration, audit.Sink()); audit.Check();
    }
    const auto& ledger = audit.Ledger;
    const auto elapsed = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - begin).count();
    std::cout << name << " steps=" << iteration.Iteration << " stop=" << static_cast<int>(iteration.Stop)
        << " split=" << audit.Splits << " forced=" << audit.Forced << " merge=" << audit.Merges
        << " failures=" << audit.Failures << " block=" << audit.Blocks << " partial=" << audit.PartialFailures
        << " supportMax=" << audit.MaximumSupport << " qualification=" << audit.Eligibility
        << " logicalRemove=" << ledger.LogicalRemoves << " insert=" << ledger.LogicalInserts
        << " birth=" << ledger.Births << " readmission=" << ledger.Readmissions
        << " pendingFailureRecords=" << ledger.Failed.size() << " push=" << ledger.Pushes << " revision=" << ledger.RevisionChanges
        << " unchanged=" << ledger.Unchanged << " stalePop=" << ledger.StalePops << " peak=" << ledger.Peak
        << " checks=" << ledger.Checks << " internalSingleton=" << audit.InteriorSingles
        << " created=" << audit.Created << " diagnosticMs=" << elapsed << '\n';
    return {audit.Splits, audit.Forced, audit.Merges, audit.Failures, audit.Blocks, audit.PartialFailures,
        audit.Created, audit.InteriorSingles, ledger.Readmissions, iteration.Stop};
}

void MissingPublicationChecks()
{
    const Candidate old{false, 2, 0, 10}, born{false, 3, 0, 20};
    Registry source{{old.Id(), old}};
    for (const bool changeKey : {false, true})
    {
        PublicationLedger ledger{source};
        auto expected = source;
        if (changeKey) expected[old.Id()].Score = 30;
        else expected[born.Id()] = born;
        bool rejected = false;
        try { ledger.Check(expected); } catch (const std::exception&) { rejected = true; }
        Require(rejected, "oracle failed to reject a missing birth/rekey with valid old top");
    }
    std::cout << "missing-publication counterexamples=2 rejected=2\n";
}

void LifecycleCase(const State& seed)
{
    auto state = seed; Refresh(state);
    BirthAudit audit{state};
    const auto node = state.MergeQueue.front().Node;
    const auto before = GroupAt(state, node);
    Require(before.has_value(), "missing lifecycle candidate");
    // 显式构造失败后的缺席状态，验证恢复不需要 Leaf/MergeReady 发生变化
    Dod::RemovePersistentMergeQueueCandidate(state, node); audit.ExplicitFailureRemoval(node);
    Require(audit.Ledger.Failed.contains(PathAt(state, node)), "failure disposition was lost");
    Dod::RefreshPersistentMergeQueueNeighborhood(state, std::vector<Node>{node}); audit.ExplicitReadmission(node);
    Require(GroupAt(state, node) == before && audit.Ledger.Failed.empty() && audit.Ledger.Readmissions == 1,
        "unchanged-topology readmission or failure lifecycle mismatch");
    std::cout << "explicit-readmission unchangedTopology=1 readmissions=1 remainingFailure=0\n";
}

void BoundaryCases(const State& seed)
{
    auto fault = seed; Refresh(fault);
    fault.Nodes.IsSplits[fault.MergeQueue.front().Node] = 0;
    fault.Settings.MergeThreshold = fault.Settings.SplitThreshold = 1e30F;
    const auto failure = Run("fault-merge-removal", fault);
    Require(failure.Failures > 0 && failure.Readmissions > 0, "failed merge removal/readmission not exercised");
    auto stop = seed; Refresh(stop);
    stop.Settings.SplitThreshold = 100; stop.Settings.MergeThreshold = 10;
    for (auto& entry : stop.SplitQueue) entry.Score = 50;
    std::sort(stop.SplitQueue.begin(), stop.SplitQueue.end(), [&](const auto& a, const auto& b) {
        return PathAt(stop, a.Node) < PathAt(stop, b.Node);
    });
    for (std::size_t i = 0; i < stop.SplitQueue.size(); ++i)
        stop.NodeMembership[stop.SplitQueue[i].Node].SplitQueuePosition = static_cast<Dod::DataOrientedRoamPosition>(i);
    for (auto& entry : stop.MergeQueue) entry.Score = 10;
    std::sort(stop.MergeQueue.begin(), stop.MergeQueue.end(), [&](const auto& a, const auto& b) {
        return PathAt(stop, a.Node) < PathAt(stop, b.Node);
    });
    for (std::size_t i = 0; i < stop.MergeQueue.size(); ++i)
        stop.NodeMembership[stop.MergeQueue[i].Node].MergeQueuePosition = static_cast<Dod::DataOrientedRoamPosition>(i);
    // 同分第二项可以通过迟滞，但原控制器仍必须在第一项停止
    stop.PreviousSplitPaths.clear(); stop.PreviousSplitPaths.insert(PathAt(stop, stop.SplitQueue[1].Node));
    const auto stopped = Run("tie-head-stop", stop);
    Require(stopped.Splits == 0 && stopped.Merges == 0 && stopped.Stop == Dod::TopologySplitStop::NoEligibleSplit,
        "strict head was skipped at hysteresis boundary");
    auto blocked = seed; Refresh(blocked);
    const auto root = blocked.SplitQueue.front().Node;
    blocked.Nodes.BaseNeighbors[root] = blocked.RootA;
    for (auto& entry : blocked.SplitQueue) entry.Score = entry.Node == root ? Maximum : -Maximum;
    std::sort(blocked.SplitQueue.begin(), blocked.SplitQueue.end(), [&](const auto& a, const auto& b) {
        return a.Score != b.Score ? a.Score > b.Score : PathAt(blocked, a.Node) < PathAt(blocked, b.Node);
    });
    for (std::size_t i = 0; i < blocked.SplitQueue.size(); ++i)
        blocked.NodeMembership[blocked.SplitQueue[i].Node].SplitQueuePosition = static_cast<Dod::DataOrientedRoamPosition>(i);
    Require(Run("fault-block-sentinel", blocked).Blocks > 0, "split block not exercised");
    auto limited = seed;
    auto iteration = Dod::BeginStrictSplitIteration(limited); iteration.Iteration = iteration.MaximumIterations;
    BirthAudit audit{limited};
    (void)Dod::AdvanceStrictSplitIterationWithDecisions(limited, iteration, audit.Sink()); audit.Check();
    Require(iteration.Stop == Dod::TopologySplitStop::IterationLimit, "iteration guard changed");
}
}

int main()
{
    try
    {
        Terrain::HeightMap height; std::string error;
        Require(height.LoadFromFile("assets/heightmaps/Hm_Terrain_Test_129.pgm", &error), error);
        MissingPublicationChecks();
        const auto seed = Seed(height);
        auto refine = seed; Refresh(refine);
        const auto refined = Run("refine", refine);
        Require(refined.Forced > 0 && refined.InteriorSingles > 0 && refined.Created > 0, "forced/singleton/cache birth missing");
        auto coarse = seed; coarse.Settings.MergeThreshold = coarse.Settings.SplitThreshold = 1e30F; Refresh(coarse);
        Require(Run("coarsen", coarse).Merges > 0, "merge path missing"); Dod::AdvanceSplitTopologySerialForExperiment(coarse);
        coarse.Settings.MaxDepth = 3;
        coarse.Settings.SplitThreshold = -1; coarse.Settings.MergeThreshold = -2; Refresh(coarse);
        const auto reused = Run("reuse", coarse);
        Require(reused.Splits > 0 && reused.Created == 0, "cached child reuse missing");
        std::size_t partialFailures = 0;
        for (const auto extra : {0U, 1U, 2U})
        {
            auto state = seed; state.Settings.TriangleBudget = seed.ActiveLeafNodes.size() + extra; Refresh(state);
            partialFailures += Run("budget-" + std::to_string(extra), state).Partial;
        }
        Require(partialFailures > 0, "partial forced success with failed root missing");
        LifecycleCase(seed); BoundaryCases(seed);
        for (int depth = 2; depth <= 4; ++depth) for (std::size_t extra = 0; extra < 4; ++extra)
        {
            auto state = seed; state.Settings.MaxDepth = depth;
            state.Settings.TriangleBudget = seed.ActiveLeafNodes.size() + extra; Refresh(state);
            Run("bounded-d" + std::to_string(depth) + "-b" + std::to_string(extra), state, 64);
        }
        const auto root = std::filesystem::current_path();
        auto natural = NativeMaterializationTests::PrepareSource(root,
            root / "benchmark-output/roam-materialization/mpr-01/input-freeze/inputs/camera-samples.csv", 0);
        NativeMaterializationTests::DisableDiagnostics(*natural->Initial);
        std::cout << "natural-source hash=" << natural->InputHash << " nodes=" << natural->Initial->Nodes.size()
            << " sourceMs=" << natural->SourceMs << '\n';
        Run("test129-a-b4096-sample14", *natural->Initial, 8);
        return 0;
    }
    catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
