#include "algorithms/data_oriented_roam/materialization/NativePlanningQueues.h"

#include "algorithms/data_oriented_roam/DataOrientedRoamIndexedHeap.h"

#include <algorithm>
#include <bit>
#include <cmath>
#include <stdexcept>

namespace ParallelRoam::Algorithms::DataOrientedRoam::Materialization
{
using Kind = NativePlanningQueueKind;
using Entry = NativePlanningQueueEntry;
using Node = DataOrientedRoamNodeIndex;
using Member = NativePlanningMembership::Field;
constexpr auto Absent = static_cast<std::size_t>(InvalidDataOrientedRoamPosition);

NativePlanningQueues::NativePlanningQueues(NativePlanningView& view, bool collectReadCoverage)
    : _view(view), _collectReadCoverage(collectReadCoverage),
      _members(view.Source(), view.CollectStorageDiagnostics())
{
    _heaps[0].Length = _heaps[0].SourceVisible = view.Source().SplitQueue.size();
    _heaps[1].Length = _heaps[1].SourceVisible = view.Source().MergeQueue.size();
}

std::size_t NativePlanningQueues::Size(Kind kind) const
{
    return _heaps.at(static_cast<std::size_t>(kind)).Length;
}

Entry NativePlanningQueues::At(Kind kind, std::size_t index) const
{
    const auto& heap = _heaps.at(static_cast<std::size_t>(kind));
    if (index >= heap.Length) throw std::out_of_range("planning heap slot");
    ++heap.Counters.Reads;
    if (heap.Cells.IsPrivate()) return heap.Cells.At(index);
    // 曾截断的来源后缀不再可见，即使物理输入仍保存着那些旧条目
    if (index >= heap.SourceVisible) throw std::logic_error("unwritten slot after source truncation");
    ++heap.Counters.SourceReads;
    if (_collectReadCoverage) heap.SourceSlotsRead.insert(index);
    if (kind == Kind::Split)
    {
        const auto& entry = _view.Source().SplitQueue[index];
        return {entry.Score, entry.Node, _view.Path(entry.Node)};
    }
    const auto& entry = _view.Source().MergeQueue[index];
    return {entry.Score, entry.Node, _view.Path(entry.Node)};
}

Entry NativePlanningQueues::Top(Kind kind) const
{
    NativePlanningCostScope cost{_view.Costs(), NativePlanningCost::Queue, true};
    if (kind == Kind::Merge && _mergeMaintenance) throw std::logic_error("merge head inside incomplete maintenance");
    if (Size(kind) > 0) return At(kind, 0);
    return {kind == Kind::Split ? -std::numeric_limits<float>::max() : std::numeric_limits<float>::max(),
        InvalidDataOrientedRoamNodeIndex};
}

std::size_t NativePlanningQueues::Position(Kind kind, Node node) const
{
    const auto& heap = _heaps.at(static_cast<std::size_t>(kind));
    ++heap.Counters.MembershipReads;
    // 删除继续保存显式无效位置，私有投影不得重新读回已删除的来源成员
    return _members.Get(node, kind == Kind::Split ? Member::Split : Member::Merge);
}

bool NativePlanningQueues::Precedes(Kind kind, Entry left, Entry right) const
{
    ++_heaps.at(static_cast<std::size_t>(kind)).Counters.Comparisons;
    if (left.Score != right.Score) return kind == Kind::Split ? left.Score > right.Score : left.Score < right.Score;
    return left.Path < right.Path;
}

void NativePlanningQueues::Restore(Kind kind, std::size_t index, Entry entry)
{
    auto& heap = _heaps.at(static_cast<std::size_t>(kind));
    if (index >= heap.Length || !_view.IsValidNode(entry.Node)) throw std::out_of_range("planning heap write");
    if (!heap.Cells.IsPrivate())
    {
        // 复制仅限这一决策堆，完整来源读取计入本次队列成本
        const auto pathOf = [&](Node node) { return _view.Path(node); };
        if (kind == Kind::Split) heap.Cells.CopySource(_view.Source().SplitQueue, pathOf);
        else heap.Cells.CopySource(_view.Source().MergeQueue, pathOf);
        heap.Counters.SourceReads += heap.Cells.Metrics().SourceCopies;
    }
    heap.Cells.Prepare(heap.Length);
    const auto originalSize = kind == Kind::Split ? _view.Source().SplitQueue.size() : _view.Source().MergeQueue.size();
    const auto memberField = kind == Kind::Split ? Member::Split : Member::Merge;
    // 封闭洞修复中，下标由堆原语界定，所有条目来自已验证候选或原有合法成员
    IndexedHeap::FillHole(heap.Length, index, entry,
        [&](std::size_t slot) { ++heap.Counters.Reads; return heap.Cells.At(slot); },
        [&](std::size_t slot, Entry value) {
            if (heap.Cells.SetPrepared(slot, value))
            {
                if (slot < originalSize) ++heap.Counters.OldSlotsWritten;
                else ++heap.Counters.AppendedSlotsWritten;
            }
            _members.Set(value.Node, memberField, static_cast<std::uint32_t>(slot));
            ++heap.Counters.Writes; ++heap.Counters.MembershipWrites;
        },
        [&](Entry a, Entry b) { return Precedes(kind, a, b); });
}

void NativePlanningQueues::Upsert(Kind kind, Node node, float score)
{
    NativePlanningCostScope cost{_view.Costs(), NativePlanningCost::Queue, true};
    if (!_view.IsValidNode(node) || !std::isfinite(score)) throw std::invalid_argument("invalid planning candidate");
    auto& heap = _heaps.at(static_cast<std::size_t>(kind));
    auto position = Position(kind, node);
    if (position == Absent)
    {
        if (heap.Length >= Absent) throw std::length_error("planning heap capacity");
        position = heap.Length++;
    }
    else
    {
        if (position >= heap.Length) throw std::logic_error("planning reverse position is stale");
        const auto previous = At(kind, position);
        if (previous.Node != node) throw std::logic_error("planning reverse position is stale");
        // 数值相等仍可能有不同的零位型，只有原位值完全相同才省去覆盖和修复
        if (std::bit_cast<std::uint32_t>(previous.Score) == std::bit_cast<std::uint32_t>(score))
        {
            ++heap.Counters.UnchangedUpserts;
            return;
        }
    }
    // 每次追加都完整覆盖条目，即使该位置曾属于已删除的来源尾部
    Restore(kind, position, {score, node, _view.Path(node)});
}

bool NativePlanningQueues::Remove(Kind kind, Node node)
{
    NativePlanningCostScope cost{_view.Costs(), NativePlanningCost::Queue, true};
    auto& heap = _heaps.at(static_cast<std::size_t>(kind));
    const auto position = Position(kind, node);
    if (position == Absent) return false;
    if (position >= heap.Length || At(kind, position).Node != node)
        throw std::logic_error("planning removal has stale reverse position");
    const auto last = At(kind, heap.Length - 1U);
    --heap.Length;
    // 已写尾部留在私有记录中用于计费；有效长度阻止读取它，下一次追加必须重写
    heap.SourceVisible = std::min(heap.SourceVisible, heap.Length);
    _members.Set(node, kind == Kind::Split ? Member::Split : Member::Merge, static_cast<std::uint32_t>(Absent));
    ++heap.Counters.MembershipWrites;
    if (position < heap.Length)
    {
        Restore(kind, position, last);
    }
    return true;
}

void NativePlanningQueues::UpsertSplit(Node node, float score) { Upsert(Kind::Split, node, score); }
bool NativePlanningQueues::RemoveSplit(Node node) { return Remove(Kind::Split, node); }

Node NativePlanningQueues::MergeRepresentative(Node node) const
{
    ++_heaps[1].Counters.MembershipReads;
    return _members.Get(node, Member::Representative);
}

Node NativePlanningQueues::MergePartner(Node representative) const
{
    ++_heaps[1].Counters.MembershipReads;
    return _members.Get(representative, Member::Partner);
}

bool NativePlanningQueues::RemoveMerge(Node eitherSide)
{
    NativePlanningCostScope cost{_view.Costs(), NativePlanningCost::Queue, true};
    const auto representative = MergeRepresentative(eitherSide);
    if (representative == InvalidDataOrientedRoamNodeIndex) return false;
    const auto partner = MergePartner(representative);
    if (!Remove(Kind::Merge, representative)) throw std::logic_error("merge representative lost its entry");
    _members.Set(representative, Member::Representative, InvalidDataOrientedRoamNodeIndex);
    _members.Set(representative, Member::Partner, InvalidDataOrientedRoamNodeIndex);
    _heaps[1].Counters.MembershipWrites += 2;
    if (partner != InvalidDataOrientedRoamNodeIndex)
    {
        _members.Set(partner, Member::Representative, InvalidDataOrientedRoamNodeIndex);
        ++_heaps[1].Counters.MembershipWrites;
    }
    return true;
}

void NativePlanningQueues::BeginMergeMaintenance()
{
    // 拒绝遗留区间，不能让上一根的待删除候选混入下一根的严格选择
    if (_mergeMaintenance || !_deferredMerge.empty()) throw std::logic_error("nested merge maintenance");
    _mergeMaintenance = true;
}

void NativePlanningQueues::InvalidateMerge(Node eitherSide)
{
    NativePlanningCostScope cost{_view.Costs(), NativePlanningCost::Queue, true};
    if (!_mergeMaintenance) throw std::logic_error("merge invalidation outside maintenance");
    const auto representative = MergeRepresentative(eitherSide);
    if (representative == InvalidDataOrientedRoamNodeIndex) return;
    const auto partner = MergePartner(representative);
    // 映射立即失效，后续同组邻域成员不会重复加入；物理条目暂不可被控制器观察
    _deferredMerge.push_back(representative);
    _members.Set(representative, Member::Representative, InvalidDataOrientedRoamNodeIndex);
    _members.Set(representative, Member::Partner, InvalidDataOrientedRoamNodeIndex);
    auto& counters = _heaps[1].Counters;
    counters.MembershipWrites += 2;
    ++counters.DeferredRemovals;
    counters.DeferredPeak = std::max(counters.DeferredPeak, _deferredMerge.size());
    if (partner != InvalidDataOrientedRoamNodeIndex)
    {
        _members.Set(partner, Member::Representative, InvalidDataOrientedRoamNodeIndex);
        ++counters.MembershipWrites;
    }
}

void NativePlanningQueues::FinishMergeMaintenance()
{
    NativePlanningCostScope cost{_view.Costs(), NativePlanningCost::Queue, true};
    if (!_mergeMaintenance) throw std::logic_error("merge maintenance is not active");
    for (const auto node : _deferredMerge)
    {
        // 恢复者已按原刷新位置更新分数；未恢复者只移除旧条目，不覆盖其可能的新伙伴身份
        if (MergeRepresentative(node) == node) ++_heaps[1].Counters.RestoredEntries;
        else if (Remove(Kind::Merge, node)) ++_heaps[1].Counters.DeferredErases;
    }
    // 容量只在本次同步规划内复用，调用结束仍随 Queues 一并释放
    _deferredMerge.clear();
    _mergeMaintenance = false;
}

void NativePlanningQueues::UpsertMerge(Node representative, Node partner, float score)
{
    NativePlanningCostScope cost{_view.Costs(), NativePlanningCost::Queue, true};
    if (!_view.IsValidNode(representative) || !std::isfinite(score) ||
        (partner != InvalidDataOrientedRoamNodeIndex && (!_view.IsValidNode(partner) ||
            _view.Path(representative) >= _view.Path(partner))))
        throw std::invalid_argument("merge pair is not canonically ordered");
    if (MergeRepresentative(representative) == representative && MergePartner(representative) == partner)
    {
        Upsert(Kind::Merge, representative, score);
        return;
    }
    // 伙伴改组必须先撤销双方旧菱形，避免一个节点同时属于两个合并候选
    (void)RemoveMerge(representative);
    if (partner != InvalidDataOrientedRoamNodeIndex) (void)RemoveMerge(partner);
    _members.Set(representative, Member::Representative, representative);
    _members.Set(representative, Member::Partner, partner);
    _heaps[1].Counters.MembershipWrites += 2;
    if (partner != InvalidDataOrientedRoamNodeIndex)
    {
        _members.Set(partner, Member::Representative, representative);
        ++_heaps[1].Counters.MembershipWrites;
    }
    Upsert(Kind::Merge, representative, score);
}

bool NativePlanningQueues::Validate(Kind kind) const
{
    if (_mergeMaintenance) throw std::logic_error("queue audit inside incomplete maintenance");
    // 这里只证明存储与代表关联一致，拓扑资格须由独立规划审计检查
    std::set<Node> members;
    for (std::size_t index = 0; index < Size(kind); ++index)
    {
        const auto entry = At(kind, index);
        if (!_view.IsValidNode(entry.Node) || entry.Path != _view.Path(entry.Node) ||
            !members.insert(entry.Node).second || Position(kind, entry.Node) != index ||
            (index > 0 && Precedes(kind, entry, At(kind, (index - 1U) / 2U)))) return false;
        if (kind == Kind::Merge)
        {
            const auto partner = MergePartner(entry.Node);
            if (MergeRepresentative(entry.Node) != entry.Node ||
                (partner != InvalidDataOrientedRoamNodeIndex && MergeRepresentative(partner) != entry.Node)) return false;
        }
    }
    // 反向覆盖中仍指向有效位置的条目也必须被正向堆接纳
    const auto field = kind == Kind::Split ? Member::Split : Member::Merge;
    for (const auto node : _members.Touched())
    {
        if (!_members.WasWritten(node, field)) continue;
        const auto position = _members.Get(node, field);
        if (position != Absent && (position >= Size(kind) || At(kind, position).Node != node)) return false;
    }
    return true;
}

NativePlanningQueueMetrics NativePlanningQueues::Metrics(Kind kind) const
{
    if (_mergeMaintenance) throw std::logic_error("queue metrics inside incomplete maintenance");
    const auto& heap = _heaps.at(static_cast<std::size_t>(kind));
    auto result = heap.Counters;
    // 以入口长度区分旧槽位与追加槽位，截断后再次使用旧下标仍计入旧槽位覆盖
    const auto originalSize = kind == Kind::Split ? _view.Source().SplitQueue.size() : _view.Source().MergeQueue.size();
    result.ReverseRecords = _members.Records(kind == Kind::Split ? Member::Split : Member::Merge);
    result.ReadCoverageCollected = _collectReadCoverage;
    result.DistinctSourceSlotsRead = _collectReadCoverage && heap.Cells.IsPrivate() ? originalSize : heap.SourceSlotsRead.size();
    if (kind == Kind::Merge) result.DeferredCapacity = _deferredMerge.capacity();
    return result;
}

std::array<NativePlanningStorageMetrics, 6> NativePlanningQueues::StorageMetrics() const
{
    // 四字段共享投影的容量只记一次，其余历史表位留空以免重复相加
    return {_heaps[0].Cells.Metrics(), _members.Metrics(), _heaps[1].Cells.Metrics(), {}, {}, {}};
}
}
