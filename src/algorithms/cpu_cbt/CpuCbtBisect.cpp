#include "algorithms/cpu_cbt/CpuCbtBisect.h"

#include <bit>
#include <chrono>

namespace ParallelRoam::Algorithms::CpuCbt
{
namespace
{
using namespace Cbt2024;
using Clock = std::chrono::steady_clock;
using NeighborPair = std::array<std::uint32_t, 2>;
constexpr auto Invalid = InvalidCbtBisectorIndex;

// 模板公式与传播分支移植自 RoamTesting d462089 的 CbtBisectCommit
// 来源和本地研究边界见该仓库 THIRD_PARTY_NOTICES.md，参考文件本身保持不变
double Elapsed(Clock::time_point start)
{
    return std::chrono::duration<double, std::milli>(Clock::now() - start).count();
}

/// <summary>
/// 每个模板独占一项记录，任务只填自身状态；全局列表和计数在屏障后归并
/// </summary>
struct TemplateRecord
{
    std::array<std::uint32_t, 3> Slots{};
    std::uint32_t SlotCount{0}, Kind{0}, Propagation{Invalid};
    bool Executed{false}, Valid{false};
};

bool PrepareOwnership(const std::vector<std::uint64_t>& ids,
    const std::vector<CbtBisectorData>& data, const std::vector<std::uint32_t>& nodes,
    std::uint32_t capacity, std::vector<TemplateRecord>& records)
{
    // 活动父槽与原先空闲的新槽在同一集合认领，避免只检查兄弟之间的重复
    std::vector<unsigned char> owned(ids.size(), 0U);
    for (std::size_t i = 0; i < nodes.size(); ++i)
    {
        const auto parent = nodes[i];
        if (parent >= ids.size() || ids[parent] == 0U || owned[parent]) return false;
        owned[parent] = 1U;
        const auto& source = data[parent];
        const auto pattern = source.SubdivisionPattern;
        auto& record = records[i];
        if (pattern == CbtCenterSplitPattern) record.Kind = 0;
        else if (pattern == CbtRightDoubleSplitPattern) record.Kind = 1;
        else if (pattern == CbtLeftDoubleSplitPattern) record.Kind = 2;
        else if (pattern == CbtTripleSplitPattern) record.Kind = 3;
        else return false;
        const unsigned growth = record.Kind == 0 ? 1U : 2U;
        if (static_cast<unsigned>(std::bit_width(ids[parent])) > 64U - growth) return false;
        record.SlotCount = static_cast<std::uint32_t>(std::popcount(pattern));
        for (std::uint32_t j = 0; j < record.SlotCount; ++j)
        {
            const auto slot = source.Indices[j];
            if (slot >= capacity || ids[slot] != 0U || owned[slot]) return false;
            owned[slot] = 1U;
            record.Slots[j] = slot;
        }
    }
    return true;
}

NeighborPair EvaluateNeighbors(std::uint32_t current, std::uint32_t neighbor,
    const std::vector<CbtBisectorNeighbors>& neighbors, const std::vector<CbtBisectorData>& data,
    bool& valid) noexcept
{
    if (neighbor >= neighbors.size()) { valid = false; return {Invalid, Invalid}; }
    // 所有跨模板读取只查冻结输入，即使目标模板已经执行也不查看其输出
    const auto& source = data[neighbor];
    const auto& n = neighbors[neighbor];
    if (source.SubdivisionPattern == CbtCenterSplitPattern) return {source.Indices[0], neighbor};
    if (source.SubdivisionPattern == CbtRightDoubleSplitPattern)
        return n.Previous == current ? NeighborPair{source.Indices[1], neighbor} :
            NeighborPair{source.Indices[0], source.Indices[1]};
    if (source.SubdivisionPattern == CbtLeftDoubleSplitPattern)
        return n.Next == current ? NeighborPair{source.Indices[1], source.Indices[0]} :
            NeighborPair{source.Indices[0], neighbor};
    if (source.SubdivisionPattern == CbtTripleSplitPattern)
    {
        if (n.Previous == current) return {source.Indices[1], neighbor};
        if (n.Next == current) return {source.Indices[2], source.Indices[0]};
        return {source.Indices[0], source.Indices[1]};
    }
    valid = false;
    return {Invalid, Invalid};
}

void WriteData(CbtBisectCommitResult& output, std::uint32_t slot, const CbtBisectorData& source,
    std::uint32_t parent, std::uint32_t problematic) noexcept
{
    auto committed = source;
    committed.ProblematicNeighbor = problematic;
    committed.BisectorState = 0U;
    // 这里只读取本模板刚写入的编号；父传播编号仍是提交前的物理槽
    committed.Flags = CbtVisibleFlag | CbtModifiedFlag | CbtSplitEventFlag |
        EncodeCbtDebugEventLifetime(CbtDebugEventHoldFrames) |
        EncodeCbtActiveDepth(static_cast<std::uint32_t>(std::bit_width(output.HeapIds[slot])));
    committed.PropagationId = parent;
    output.BisectorData[slot] = committed;
}

void FillTemplate(std::uint32_t parent, const std::vector<std::uint64_t>& ids,
    const std::vector<CbtBisectorNeighbors>& neighbors, const std::vector<CbtBisectorData>& data,
    CbtBisectCommitResult& output, TemplateRecord& record) noexcept
{
    const auto& source = data[parent];
    const auto n = neighbors[parent];
    const auto heap = ids[parent];
    const auto s0 = record.Slots[0], s1 = record.Slots[1], s2 = record.Slots[2];
    bool valid = true;
    NeighborPair a{Invalid, Invalid}, b{Invalid, Invalid}, c{Invalid, Invalid};
    if (record.Kind == 0)
    {
        // 原槽保留偶子，奇子的外侧引用延迟到统一传播阶段修补
        if (n.Twin != Invalid) c = EvaluateNeighbors(parent, n.Twin, neighbors, data, valid);
        output.HeapIds[parent] = heap * 2U;
        output.HeapIds[s0] = heap * 2U + 1U;
        output.Neighbors[parent] = {s0, c[0], n.Previous};
        output.Neighbors[s0] = {c[1], parent, n.Next};
        record.Propagation = s0;
    }
    else if (record.Kind == 1)
    {
        // 右双模板跨接旧前邻居和面对面邻居，仅一级奇子需要外侧传播
        a = EvaluateNeighbors(parent, n.Previous, neighbors, data, valid);
        if (n.Twin != Invalid) b = EvaluateNeighbors(parent, n.Twin, neighbors, data, valid);
        output.HeapIds[parent] = heap * 4U;
        output.HeapIds[s0] = heap * 2U + 1U;
        output.HeapIds[s1] = heap * 4U + 1U;
        output.Neighbors[parent] = {s1, a[0], s0};
        output.Neighbors[s0] = {b[1], parent, n.Next};
        output.Neighbors[s1] = {a[1], parent, b[0]};
        record.Propagation = s0;
    }
    else if (record.Kind == 2)
    {
        // 左双模板的编号排列不等于简单翻转右双模板，三槽直接闭合
        a = EvaluateNeighbors(parent, n.Next, neighbors, data, valid);
        if (n.Twin != Invalid) b = EvaluateNeighbors(parent, n.Twin, neighbors, data, valid);
        output.HeapIds[parent] = heap * 2U;
        output.HeapIds[s0] = heap * 4U + 2U;
        output.HeapIds[s1] = heap * 4U + 3U;
        output.Neighbors[parent] = {s1, b[0], n.Previous};
        output.Neighbors[s0] = {s1, a[0], b[1]};
        output.Neighbors[s1] = {a[1], s0, parent};
    }
    else
    {
        // 三重模板使用全部相邻计划，输出完整两层子树，无延迟传播项
        a = EvaluateNeighbors(parent, n.Previous, neighbors, data, valid);
        b = EvaluateNeighbors(parent, n.Next, neighbors, data, valid);
        if (n.Twin != Invalid) c = EvaluateNeighbors(parent, n.Twin, neighbors, data, valid);
        output.HeapIds[parent] = heap * 4U;
        output.HeapIds[s0] = heap * 4U + 2U;
        output.HeapIds[s1] = heap * 4U + 1U;
        output.HeapIds[s2] = heap * 4U + 3U;
        output.Neighbors[parent] = {s1, a[0], s2};
        output.Neighbors[s0] = {s2, b[0], c[1]};
        output.Neighbors[s1] = {a[1], parent, c[0]};
        output.Neighbors[s2] = {b[1], s0, parent};
    }
    WriteData(output, parent, source, parent, Invalid);
    for (std::uint32_t j = 0; j < record.SlotCount; ++j)
        WriteData(output, record.Slots[j], source, parent,
            record.Slots[j] == record.Propagation ? n.Next : Invalid);
    record.Valid = valid;
    record.Executed = true;
}

bool Propagate(CbtBisectCommitResult& output)
{
    // 此处读取完整新代并可能修改其他模板输出，必须在全部填写完成后顺序执行
    for (const auto current : output.PropagationNodes)
    {
        auto& data = output.BisectorData[current];
        const auto parent = data.PropagationId, problematic = data.ProblematicNeighbor;
        if (problematic == Invalid) { data.BisectorState = 0U; continue; }
        if (problematic >= output.Neighbors.size() || output.HeapIds[problematic] == 0U) return false;
        const auto target = output.BisectorData[problematic];
        const auto mark = [&](std::uint32_t slot) {
            output.BisectorData[slot].Flags = WithCbtDebugEvent(output.BisectorData[slot].Flags, CbtSplitEventFlag);
        };
        if (target.SubdivisionPattern == CbtNoSplitPattern)
        {
            auto& n = output.Neighbors[problematic];
            bool replaced = false;
            for (auto* neighbor : {&n.Previous, &n.Next, &n.Twin})
                if (*neighbor == parent) { *neighbor = current; replaced = true; }
            if (replaced) mark(problematic);
        }
        else if (target.SubdivisionPattern == CbtCenterSplitPattern)
        {
            if (output.Neighbors[problematic].Twin == parent)
            {
                output.Neighbors[problematic].Twin = current;
                mark(problematic);
            }
            const auto propagated = target.PropagationId;
            if (propagated >= output.Neighbors.size()) return false;
            if (output.Neighbors[propagated].Twin == parent)
            {
                output.Neighbors[propagated].Twin = current;
                mark(propagated);
            }
        }
        else if (target.SubdivisionPattern == CbtRightDoubleSplitPattern)
        {
            const auto sibling = target.Indices[1];
            if (sibling >= output.Neighbors.size()) return false;
            output.Neighbors[sibling].Twin = current;
            mark(sibling);
        }
        else if (target.SubdivisionPattern == CbtLeftDoubleSplitPattern)
        {
            output.Neighbors[problematic].Twin = current;
            mark(problematic);
        }
        else return false;
        data.ProblematicNeighbor = Invalid;
        data.BisectorState = 0U;
    }
    return true;
}
}

CpuCbtBisectResult CommitCpuCbtBisects(const std::vector<std::uint64_t>& heapIds,
    const std::vector<Cbt2024::CbtBisectorNeighbors>& neighbors,
    const std::vector<Cbt2024::CbtBisectorData>& data,
    const std::vector<std::uint32_t>& allocationNodes, std::uint32_t dynamicElementCount,
    const CpuCbtRangeExecutor& executor)
{
    CpuCbtBisectResult result;
    auto& output = result.Topology;
    output.Valid = false;
    auto stage = Clock::now();
    if (heapIds.size() != neighbors.size() || heapIds.size() != data.size() ||
        dynamicElementCount > heapIds.size()) return result;
    std::vector<TemplateRecord> records(allocationNodes.size());
    if (!PrepareOwnership(heapIds, data, allocationNodes, dynamicElementCount, records)) return result;
    output.HeapIds = heapIds;
    output.Neighbors = neighbors;
    output.BisectorData = data;
    output.CommittedDynamicSlots.reserve(allocationNodes.size() * 3U);
    output.PropagationNodes.reserve(allocationNodes.size());
    result.Timings.PrepareMs = Elapsed(stage);

    if (!allocationNodes.empty())
    {
        stage = Clock::now();
        const CpuCbtRangeTask fill = [&](std::size_t begin, std::size_t end) noexcept {
            if (begin > end || end > allocationNodes.size()) return;
            for (auto i = begin; i < end; ++i)
                FillTemplate(allocationNodes[i], heapIds, neighbors, data, output, records[i]);
        };
        if (executor) executor(allocationNodes.size(), fill);
        else fill(0U, allocationNodes.size());
        result.Timings.TemplateFillWallMs = Elapsed(stage);
    }

    // 执行器的同步返回是唯一填写屏障，归并次序与实际线程完成次序无关
    stage = Clock::now();
    for (const auto& record : records)
    {
        if (!record.Executed || !record.Valid) return result;
        for (std::uint32_t j = 0; j < record.SlotCount; ++j)
            output.CommittedDynamicSlots.push_back(record.Slots[j]);
        if (record.Propagation != Invalid) output.PropagationNodes.push_back(record.Propagation);
        ++output.TemplateCounts[record.Kind];
    }
    result.Timings.CollectMs = Elapsed(stage);
    stage = Clock::now();
    output.Valid = Propagate(output);
    result.Timings.PropagationMs = Elapsed(stage);
    return result;
}
} // 命名空间 ParallelRoam::Algorithms::CpuCbt
