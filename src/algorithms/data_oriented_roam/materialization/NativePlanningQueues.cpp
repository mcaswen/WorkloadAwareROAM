#include "algorithms/data_oriented_roam/materialization/NativePlanningQueues.h"

#include "algorithms/data_oriented_roam/DataOrientedRoamIndexedHeap.h"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace ParallelRoam::Algorithms::DataOrientedRoam::Materialization
{
using Kind = NativePlanningQueueKind;
using Entry = NativePlanningQueueEntry;
using Node = DataOrientedRoamNodeIndex;
constexpr auto Absent = static_cast<std::size_t>(InvalidDataOrientedRoamPosition);

NativePlanningQueues::NativePlanningQueues(NativePlanningView& view, bool collectReadCoverage)
    : _view(view), _collectReadCoverage(collectReadCoverage)
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
    const auto found = heap.Cells.find(index);
    if (found != heap.Cells.end()) return found->second;
    // 曾截断的来源后缀不再可见，即使物理输入仍保存着那些旧条目
    if (index >= heap.SourceVisible) throw std::logic_error("unwritten slot after source truncation");
    ++heap.Counters.SourceReads;
    if (_collectReadCoverage) heap.SourceSlotsRead.insert(index);
    if (kind == Kind::Split)
    {
        const auto& entry = _view.Source().SplitQueue[index];
        return {entry.Score, entry.Node};
    }
    const auto& entry = _view.Source().MergeQueue[index];
    return {entry.Score, entry.Node};
}

Entry NativePlanningQueues::Top(Kind kind) const
{
    if (Size(kind) > 0) return At(kind, 0);
    return {kind == Kind::Split ? -std::numeric_limits<float>::max() : std::numeric_limits<float>::max(),
        InvalidDataOrientedRoamNodeIndex};
}

std::size_t NativePlanningQueues::Position(Kind kind, Node node) const
{
    const auto& heap = _heaps.at(static_cast<std::size_t>(kind));
    ++heap.Counters.MembershipReads;
    const auto found = heap.Reverse.find(node);
    if (found != heap.Reverse.end()) return found->second;
    // 删除必须保留显式无效覆盖，不能因移除私有键而重新读到旧反向位置
    if (node >= _view.Source().Nodes.size()) return Absent;
    const auto& membership = _view.Source().NodeMembership[node];
    return kind == Kind::Split ? membership.SplitQueuePosition : membership.MergeQueuePosition;
}

bool NativePlanningQueues::Precedes(Kind kind, Entry left, Entry right) const
{
    ++_heaps.at(static_cast<std::size_t>(kind)).Counters.Comparisons;
    if (left.Score != right.Score) return kind == Kind::Split ? left.Score > right.Score : left.Score < right.Score;
    return _view.Path(left.Node) < _view.Path(right.Node);
}

void NativePlanningQueues::Write(Kind kind, std::size_t index, Entry entry)
{
    auto& heap = _heaps.at(static_cast<std::size_t>(kind));
    if (index >= heap.Length || !_view.IsValidNode(entry.Node)) throw std::out_of_range("planning heap write");
    heap.Cells[index] = entry;
    heap.Reverse[entry.Node] = index;
    ++heap.Counters.Writes;
    ++heap.Counters.MembershipWrites;
}

void NativePlanningQueues::Swap(Kind kind, std::size_t left, std::size_t right)
{
    if (left == right) return;
    const auto a = At(kind, left), b = At(kind, right);
    Write(kind, left, b); Write(kind, right, a);
}

void NativePlanningQueues::Restore(Kind kind, std::size_t index)
{
    IndexedHeap::Restore(Size(kind), index,
        [&](std::size_t a, std::size_t b) { return Precedes(kind, At(kind, a), At(kind, b)); },
        [&](std::size_t a, std::size_t b) { Swap(kind, a, b); });
}

void NativePlanningQueues::Upsert(Kind kind, Node node, float score)
{
    if (!_view.IsValidNode(node) || !std::isfinite(score)) throw std::invalid_argument("invalid planning candidate");
    auto& heap = _heaps.at(static_cast<std::size_t>(kind));
    auto position = Position(kind, node);
    if (position == Absent)
    {
        if (heap.Length >= Absent) throw std::length_error("planning heap capacity");
        position = heap.Length++;
    }
    else if (position >= heap.Length || At(kind, position).Node != node)
        throw std::logic_error("planning reverse position is stale");
    // 每次追加都完整覆盖条目，即使该位置曾属于已删除的来源尾部
    Write(kind, position, {score, node});
    Restore(kind, position);
}

bool NativePlanningQueues::Remove(Kind kind, Node node)
{
    auto& heap = _heaps.at(static_cast<std::size_t>(kind));
    const auto position = Position(kind, node);
    if (position == Absent) return false;
    if (position >= heap.Length || At(kind, position).Node != node)
        throw std::logic_error("planning removal has stale reverse position");
    const auto last = At(kind, heap.Length - 1U);
    --heap.Length;
    // 已写尾部留在私有记录中用于计费；有效长度阻止读取它，下一次追加必须重写
    heap.SourceVisible = std::min(heap.SourceVisible, heap.Length);
    heap.Reverse[node] = Absent;
    ++heap.Counters.MembershipWrites;
    if (position < heap.Length)
    {
        Write(kind, position, last);
        Restore(kind, position);
    }
    return true;
}

void NativePlanningQueues::UpsertSplit(Node node, float score) { Upsert(Kind::Split, node, score); }
bool NativePlanningQueues::RemoveSplit(Node node) { return Remove(Kind::Split, node); }

Node NativePlanningQueues::MergeRepresentative(Node node) const
{
    ++_heaps[1].Counters.MembershipReads;
    const auto found = _representatives.find(node);
    if (found != _representatives.end()) return found->second;
    return node < _view.Source().Nodes.size() ? _view.Source().NodeMembership[node].MergeQueueRepresentative
        : InvalidDataOrientedRoamNodeIndex;
}

Node NativePlanningQueues::MergePartner(Node representative) const
{
    ++_heaps[1].Counters.MembershipReads;
    const auto found = _partners.find(representative);
    if (found != _partners.end()) return found->second;
    return representative < _view.Source().Nodes.size() ? _view.Source().NodeMembership[representative].MergeQueuePartner
        : InvalidDataOrientedRoamNodeIndex;
}

bool NativePlanningQueues::RemoveMerge(Node eitherSide)
{
    const auto representative = MergeRepresentative(eitherSide);
    if (representative == InvalidDataOrientedRoamNodeIndex) return false;
    const auto partner = MergePartner(representative);
    if (!Remove(Kind::Merge, representative)) throw std::logic_error("merge representative lost its entry");
    _representatives[representative] = InvalidDataOrientedRoamNodeIndex;
    _partners[representative] = InvalidDataOrientedRoamNodeIndex;
    _heaps[1].Counters.MembershipWrites += 2;
    if (partner != InvalidDataOrientedRoamNodeIndex)
    {
        _representatives[partner] = InvalidDataOrientedRoamNodeIndex;
        ++_heaps[1].Counters.MembershipWrites;
    }
    return true;
}

void NativePlanningQueues::UpsertMerge(Node representative, Node partner, float score)
{
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
    _representatives[representative] = representative;
    _partners[representative] = partner;
    _heaps[1].Counters.MembershipWrites += 2;
    if (partner != InvalidDataOrientedRoamNodeIndex)
    {
        _representatives[partner] = representative;
        ++_heaps[1].Counters.MembershipWrites;
    }
    Upsert(Kind::Merge, representative, score);
}

bool NativePlanningQueues::Validate(Kind kind) const
{
    // 这里只证明存储与代表关联一致，拓扑资格须由独立规划审计检查
    std::set<Node> members;
    for (std::size_t index = 0; index < Size(kind); ++index)
    {
        const auto entry = At(kind, index);
        if (!_view.IsValidNode(entry.Node) || !members.insert(entry.Node).second || Position(kind, entry.Node) != index ||
            (index > 0 && Precedes(kind, entry, At(kind, (index - 1U) / 2U)))) return false;
        if (kind == Kind::Merge)
        {
            const auto partner = MergePartner(entry.Node);
            if (MergeRepresentative(entry.Node) != entry.Node ||
                (partner != InvalidDataOrientedRoamNodeIndex && MergeRepresentative(partner) != entry.Node)) return false;
        }
    }
    // 反向覆盖中仍指向有效位置的条目也必须被正向堆接纳
    const auto& heap = _heaps.at(static_cast<std::size_t>(kind));
    for (const auto& [node, position] : heap.Reverse)
        if (position != Absent && (position >= Size(kind) || At(kind, position).Node != node)) return false;
    return true;
}

NativePlanningQueueMetrics NativePlanningQueues::Metrics(Kind kind) const
{
    const auto& heap = _heaps.at(static_cast<std::size_t>(kind));
    auto result = heap.Counters;
    // 以入口长度区分旧槽位与追加槽位，截断后再次使用旧下标仍计入旧槽位覆盖
    const auto originalSize = kind == Kind::Split ? _view.Source().SplitQueue.size() : _view.Source().MergeQueue.size();
    for (const auto& [index, entry] : heap.Cells)
    {
        (void)entry;
        if (index < originalSize) ++result.OldSlotsWritten;
        else ++result.AppendedSlotsWritten;
    }
    result.ReverseRecords = heap.Reverse.size();
    result.ReadCoverageCollected = _collectReadCoverage;
    result.DistinctSourceSlotsRead = heap.SourceSlotsRead.size();
    return result;
}
}
