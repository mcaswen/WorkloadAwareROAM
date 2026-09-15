#include "algorithms/cbt_2024/CbtBisectCommit.h"

#include "algorithms/cbt_2024/CbtSplitPlanner.h"

#include <algorithm>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <string>

namespace ParallelRoam::Algorithms::Cbt2024
{
namespace
{
// CPU 参考刻意保留上游四模板的数据流，而不抽象成通用三角剖分器。
// 这样 GPU 读回失败时，可以逐字段对照 heapID、邻接和传播元数据。

// 任一局部模板不满足前置条件时，整批事务都标记为无效。
// 调用者不会消费半提交的 CPU 快照。
bool SetInvalid(CbtBisectCommitResult& result)
{
    result.Valid = false;
    return false;
}

bool IsIndex(std::uint32_t index, std::size_t count)
{
    return index < count;
}

bool IsNeighbor(std::uint32_t index, std::size_t count)
{
    return index == InvalidCbtBisectorIndex || IsIndex(index, count);
}

std::array<std::uint32_t, 2> EvaluateNeighbors(
    std::uint32_t currentId,
    std::uint32_t neighborId,
    const std::vector<CbtBisectorNeighbors>& neighbors,
    const std::vector<CbtBisectorData>& data,
    bool& valid)
{
    // 返回值对应上游 evaluate_neighbors 的 resX/resY。
    // currentId 决定双模板中当前半边接触的是 neighbor 的哪一侧。
    // 这里读取提交前快照，避免 allocationNodes 的遍历顺序改变结果。
    if (!IsIndex(neighborId, neighbors.size()))
    {
        valid = false;
        return {InvalidCbtBisectorIndex, InvalidCbtBisectorIndex};
    }

    const CbtBisectorData& neighborData = data[neighborId];
    const CbtBisectorNeighbors& neighborNeighbors = neighbors[neighborId];
    if (neighborData.SubdivisionPattern == CbtCenterSplitPattern)
    {
        return {neighborData.Indices[0], neighborId};
    }
    if (neighborData.SubdivisionPattern == CbtRightDoubleSplitPattern)
    {
        return neighborNeighbors.Previous == currentId
            ? std::array<std::uint32_t, 2>{neighborData.Indices[1], neighborId}
            : std::array<std::uint32_t, 2>{neighborData.Indices[0], neighborData.Indices[1]};
    }
    if (neighborData.SubdivisionPattern == CbtLeftDoubleSplitPattern)
    {
        return neighborNeighbors.Next == currentId
            ? std::array<std::uint32_t, 2>{neighborData.Indices[1], neighborData.Indices[0]}
            : std::array<std::uint32_t, 2>{neighborData.Indices[0], neighborId};
    }
    if (neighborData.SubdivisionPattern ==
        (CbtCenterSplitPattern | CbtRightSplitPattern | CbtLeftSplitPattern))
    {
        if (neighborNeighbors.Previous == currentId)
        {
            return {neighborData.Indices[1], neighborId};
        }
        if (neighborNeighbors.Next == currentId)
        {
            return {neighborData.Indices[2], neighborData.Indices[0]};
        }
        return {neighborData.Indices[0], neighborData.Indices[1]};
    }

    valid = false;
    return {InvalidCbtBisectorIndex, InvalidCbtBisectorIndex};
}

void WriteCommittedData(
    CbtBisectCommitResult& result,
    std::uint32_t target,
    const CbtBisectorData& source,
    std::uint32_t parent,
    std::uint32_t problematicNeighbor)
{
    // retained 节点与新 sibling 都继承本帧 subdivision pattern 和 allocation indices。
    // PropagationId 保存模板提交前的父物理槽位，不是逻辑 heap 父节点。
    // ProblematicNeighbor 只在 center/right-double 的外侧 sibling 上有效。
    CbtBisectorData committed = source;
    committed.ProblematicNeighbor = problematicNeighbor;
    committed.BisectorState = 0U;
    committed.Flags = CbtVisibleFlag | CbtModifiedFlag | CbtSplitEventFlag |
        EncodeCbtDebugEventLifetime(CbtDebugEventHoldFrames) |
        EncodeCbtActiveDepth(static_cast<std::uint32_t>(std::bit_width(result.HeapIds[target])));
    committed.PropagationId = parent;
    result.BisectorData[target] = committed;
}

CbtBaseControlPoint Midpoint(const CbtBaseControlPoint& first, const CbtBaseControlPoint& second)
{
    // 平面参考仍保留 Height 分量，使后续高度图阶段可复用同一个返回类型。
    return {
        (first.U + second.U) * 0.5F,
        (first.Height + second.Height) * 0.5F,
        (first.V + second.V) * 0.5F,
    };
}

void SplitLebTriangle(std::array<CbtBaseControlPoint, 3>& triangle, bool rightChild)
{
    // LEB 子位从最高有效路径位向最低位消费。
    // 每步都沿当前三角形的主边 p0-p2 取中点。
    const auto previous = triangle;
    const CbtBaseControlPoint midpoint = Midpoint(previous[0], previous[2]);
    // 这两种排列与上游 leb__SplittingMatrix(bit) 的三行完全等价。
    triangle = rightChild
        ? std::array<CbtBaseControlPoint, 3>{previous[1], midpoint, previous[0]}
        : std::array<CbtBaseControlPoint, 3>{previous[2], midpoint, previous[1]};
}
} // namespace

CbtBisectCommitResult CommitCbtBisects(
    const std::vector<std::uint64_t>& heapIds,
    const std::vector<CbtBisectorNeighbors>& currentNeighbors,
    const std::vector<CbtBisectorData>& bisectorData,
    const std::vector<std::uint32_t>& allocationNodes,
    std::uint32_t dynamicElementCount)
{
    // 邻接采用 copy-on-write：输入代表已发布代次，result 代表待发布代次。
    // heapID 和 BisectorData 也复制到结果，便于单测像 GPU readback 一样检查整帧。
    CbtBisectCommitResult result{};
    result.HeapIds = heapIds;
    result.Neighbors = currentNeighbors;
    result.BisectorData = bisectorData;
    if (heapIds.size() != currentNeighbors.size() || heapIds.size() != bisectorData.size() ||
        dynamicElementCount > heapIds.size())
    {
        SetInvalid(result);
        return result;
    }

    for (const std::uint32_t currentId : allocationNodes)
    {
        // E2 保证 allocationNodes 唯一；E3 仍防御无效或非活动父槽位。
        // 每个 pattern 的置位数就是其需要消费的新动态槽位数。
        if (!IsIndex(currentId, heapIds.size()) || heapIds[currentId] == 0U)
        {
            SetInvalid(result);
            return result;
        }

        const CbtBisectorData source = bisectorData[currentId];
        const CbtBisectorNeighbors parentNeighbors = currentNeighbors[currentId];
        const std::uint32_t pattern = source.SubdivisionPattern;
        const std::uint32_t slotCount = static_cast<std::uint32_t>(std::popcount(pattern));
        if (pattern != CbtCenterSplitPattern && pattern != CbtRightDoubleSplitPattern &&
            pattern != CbtLeftDoubleSplitPattern &&
            pattern != (CbtCenterSplitPattern | CbtRightSplitPattern | CbtLeftSplitPattern))
        {
            SetInvalid(result);
            return result;
        }
        for (std::uint32_t slot = 0U; slot < slotCount; ++slot)
        {
            // 新槽必须来自旧 OCBT complement，且一批事务内不可重复认领。
            // 记录顺序保留 allocation rank 顺序，便于定位容量边界错误。
            const std::uint32_t physicalSlot = source.Indices[slot];
            if (physicalSlot >= dynamicElementCount || result.HeapIds[physicalSlot] != 0U ||
                std::find(result.CommittedDynamicSlots.begin(), result.CommittedDynamicSlots.end(), physicalSlot) !=
                    result.CommittedDynamicSlots.end())
            {
                SetInvalid(result);
                return result;
            }
            result.CommittedDynamicSlots.push_back(physicalSlot);
        }

        const std::uint64_t baseHeapId = heapIds[currentId];
        const std::uint32_t sibling0 = source.Indices[0];
        const std::uint32_t sibling1 = source.Indices[1];
        const std::uint32_t sibling2 = source.Indices[2];
        bool valid = true;
        std::array<std::uint32_t, 2> result0{InvalidCbtBisectorIndex, InvalidCbtBisectorIndex};
        std::array<std::uint32_t, 2> result1{InvalidCbtBisectorIndex, InvalidCbtBisectorIndex};
        std::array<std::uint32_t, 2> result2{InvalidCbtBisectorIndex, InvalidCbtBisectorIndex};

        if (pattern == CbtCenterSplitPattern)
        {
            // center 模板保留偶子在原槽，并创建奇子 sibling0。
            // facing twin 若存在，必须已被 E2 规划为兼容 split pattern。
            // sibling0 的外侧 next 引用在统一 propagation 阶段修补。
            if (parentNeighbors.Twin != InvalidCbtBisectorIndex)
            {
                result2 = EvaluateNeighbors(currentId, parentNeighbors.Twin, currentNeighbors, bisectorData, valid);
            }
            result.HeapIds[currentId] = baseHeapId * 2U;
            result.HeapIds[sibling0] = baseHeapId * 2U + 1U;
            result.Neighbors[currentId] = {sibling0, result2[0], parentNeighbors.Previous};
            result.Neighbors[sibling0] = {result2[1], currentId, parentNeighbors.Next};
            WriteCommittedData(result, currentId, source, currentId, InvalidCbtBisectorIndex);
            WriteCommittedData(result, sibling0, source, currentId, parentNeighbors.Next);
            result.PropagationNodes.push_back(sibling0);
            ++result.TemplateCounts[0];
        }
        else if (pattern == CbtRightDoubleSplitPattern)
        {
            // right-double 生成深两级的 retained/right 子节点和一级 sibling0。
            // previous 与 facing twin 的提交结果由 evaluate_neighbors 交叉拼接。
            // 只有一级 sibling0 需要把旧 parent 引用传播到外侧邻居。
            result0 = EvaluateNeighbors(currentId, parentNeighbors.Previous, currentNeighbors, bisectorData, valid);
            if (parentNeighbors.Twin != InvalidCbtBisectorIndex)
            {
                result1 = EvaluateNeighbors(currentId, parentNeighbors.Twin, currentNeighbors, bisectorData, valid);
            }
            result.HeapIds[currentId] = baseHeapId * 4U;
            result.HeapIds[sibling0] = baseHeapId * 2U + 1U;
            result.HeapIds[sibling1] = baseHeapId * 4U + 1U;
            result.Neighbors[currentId] = {sibling1, result0[0], sibling0};
            result.Neighbors[sibling0] = {result1[1], currentId, parentNeighbors.Next};
            result.Neighbors[sibling1] = {result0[1], currentId, result1[0]};
            WriteCommittedData(result, currentId, source, currentId, InvalidCbtBisectorIndex);
            WriteCommittedData(result, sibling0, source, currentId, parentNeighbors.Next);
            WriteCommittedData(result, sibling1, source, currentId, InvalidCbtBisectorIndex);
            result.PropagationNodes.push_back(sibling0);
            ++result.TemplateCounts[1];
        }
        else if (pattern == CbtLeftDoubleSplitPattern)
        {
            // left-double 是 right-double 的定向镜像，但 heapID 排列并非简单数组翻转。
            // 三个输出节点都在模板内闭合，因此不登记额外传播任务。
            // next 邻居必须参与相同提交，否则 EvaluateNeighbors 会拒绝事务。
            result0 = EvaluateNeighbors(currentId, parentNeighbors.Next, currentNeighbors, bisectorData, valid);
            if (parentNeighbors.Twin != InvalidCbtBisectorIndex)
            {
                result1 = EvaluateNeighbors(currentId, parentNeighbors.Twin, currentNeighbors, bisectorData, valid);
            }
            result.HeapIds[currentId] = baseHeapId * 2U;
            result.HeapIds[sibling0] = baseHeapId * 4U + 2U;
            result.HeapIds[sibling1] = baseHeapId * 4U + 3U;
            result.Neighbors[currentId] = {sibling1, result1[0], parentNeighbors.Previous};
            result.Neighbors[sibling0] = {sibling1, result0[0], result1[1]};
            result.Neighbors[sibling1] = {result0[1], sibling0, currentId};
            WriteCommittedData(result, currentId, source, currentId, InvalidCbtBisectorIndex);
            WriteCommittedData(result, sibling0, source, currentId, InvalidCbtBisectorIndex);
            WriteCommittedData(result, sibling1, source, currentId, InvalidCbtBisectorIndex);
            ++result.TemplateCounts[2];
        }
        else
        {
            // triple 同时消费 previous、next 和可选 facing twin 的提交结果。
            // 四个输出槽形成两层完整 LEB 子树，所有外侧引用都在模板内确定。
            // 该分支不需要延迟 propagation。
            result0 = EvaluateNeighbors(currentId, parentNeighbors.Previous, currentNeighbors, bisectorData, valid);
            result1 = EvaluateNeighbors(currentId, parentNeighbors.Next, currentNeighbors, bisectorData, valid);
            if (parentNeighbors.Twin != InvalidCbtBisectorIndex)
            {
                result2 = EvaluateNeighbors(currentId, parentNeighbors.Twin, currentNeighbors, bisectorData, valid);
            }
            result.HeapIds[currentId] = baseHeapId * 4U;
            result.HeapIds[sibling0] = baseHeapId * 4U + 2U;
            result.HeapIds[sibling1] = baseHeapId * 4U + 1U;
            result.HeapIds[sibling2] = baseHeapId * 4U + 3U;
            result.Neighbors[currentId] = {sibling1, result0[0], sibling2};
            result.Neighbors[sibling0] = {sibling2, result1[0], result2[1]};
            result.Neighbors[sibling1] = {result0[1], currentId, result2[0]};
            result.Neighbors[sibling2] = {result1[1], sibling0, currentId};
            WriteCommittedData(result, currentId, source, currentId, InvalidCbtBisectorIndex);
            WriteCommittedData(result, sibling0, source, currentId, InvalidCbtBisectorIndex);
            WriteCommittedData(result, sibling1, source, currentId, InvalidCbtBisectorIndex);
            WriteCommittedData(result, sibling2, source, currentId, InvalidCbtBisectorIndex);
            ++result.TemplateCounts[3];
        }
        if (!valid)
        {
            SetInvalid(result);
            return result;
        }
    }

    // 传播始终读取已经复制并写完模板的下一代邻接。
    // 先完成全部 Bisect 再传播，等价于 GPU dispatch 间的全局 UAV barrier。
    // 传播仅替换仍指向旧 parentId 的边，避免覆盖相邻模板的新引用。
    for (const std::uint32_t currentId : result.PropagationNodes)
    {
        CbtBisectorData& currentData = result.BisectorData[currentId];
        const std::uint32_t parentId = currentData.PropagationId;
        const std::uint32_t problematic = currentData.ProblematicNeighbor;
        if (problematic == InvalidCbtBisectorIndex)
        {
            // 边界半边没有外侧邻居；与 GPU 路径一致，将传播视为已完成。
            currentData.BisectorState = 0U;
            continue;
        }
        if (!IsIndex(problematic, result.Neighbors.size()) || result.HeapIds[problematic] == 0U)
        {
            SetInvalid(result);
            return result;
        }
        const CbtBisectorData targetData = result.BisectorData[problematic];
        if (targetData.SubdivisionPattern == CbtNoSplitPattern)
        {
            // 未 split 的目标可能从任意一个有向边引用旧 parent。
            auto& target = result.Neighbors[problematic];
            // CPU 参考与 GPU 一样只标记确实替换了旧 parent 引用的存活目标。
            bool replaced = false;
            for (std::uint32_t* neighbor : {&target.Previous, &target.Next, &target.Twin})
            {
                if (*neighbor == parentId)
                {
                    *neighbor = currentId;
                    replaced = true;
                }
            }
            if (replaced)
            {
                result.BisectorData[problematic].Flags = WithCbtDebugEvent(
                    result.BisectorData[problematic].Flags,
                    CbtSplitEventFlag);
            }
        }
        else if (targetData.SubdivisionPattern == CbtCenterSplitPattern)
        {
            // center 目标可能把 facing 边保留在 retained 或其奇子上，两处都要条件替换。
            if (result.Neighbors[problematic].Twin == parentId)
            {
                result.Neighbors[problematic].Twin = currentId;
                result.BisectorData[problematic].Flags = WithCbtDebugEvent(
                    result.BisectorData[problematic].Flags,
                    CbtSplitEventFlag);
            }
            const std::uint32_t propagated = targetData.PropagationId;
            if (!IsIndex(propagated, result.Neighbors.size()))
            {
                SetInvalid(result);
                return result;
            }
            if (result.Neighbors[propagated].Twin == parentId)
            {
                result.Neighbors[propagated].Twin = currentId;
                result.BisectorData[propagated].Flags = WithCbtDebugEvent(
                    result.BisectorData[propagated].Flags,
                    CbtSplitEventFlag);
            }
        }
        else if (targetData.SubdivisionPattern == CbtRightDoubleSplitPattern)
        {
            // 上游模板规定 right-double 的第二个 allocation sibling 承接传播 facing 边。
            const std::uint32_t sibling = targetData.Indices[1];
            if (!IsIndex(sibling, result.Neighbors.size()))
            {
                SetInvalid(result);
                return result;
            }
            result.Neighbors[sibling].Twin = currentId;
            result.BisectorData[sibling].Flags = WithCbtDebugEvent(
                result.BisectorData[sibling].Flags,
                CbtSplitEventFlag);
        }
        else if (targetData.SubdivisionPattern == CbtLeftDoubleSplitPattern)
        {
            // left-double 的 retained 物理槽直接承接新的 facing 引用。
            result.Neighbors[problematic].Twin = currentId;
            result.BisectorData[problematic].Flags = WithCbtDebugEvent(
                result.BisectorData[problematic].Flags,
                CbtSplitEventFlag);
        }
        else
        {
            SetInvalid(result);
            return result;
        }
        currentData.ProblematicNeighbor = InvalidCbtBisectorIndex;
        currentData.BisectorState = 0U;
    }
    return result;
}

bool ValidateCbtCommittedTopology(
    const CbtBisectCommitResult& topology,
    std::uint32_t dynamicElementCount,
    std::string* errorMessage)
{
    // validator 不依赖模板执行顺序，只检查发布后必须成立的全局关系。
    // INVALID 邻接表示开放边界，其他邻接必须活动、在范围内且可反向找到当前槽。
    const auto fail = [&](const std::string& message) {
        if (errorMessage != nullptr)
        {
            *errorMessage = message;
        }
        return false;
    };
    if (!topology.Valid || topology.HeapIds.size() != topology.Neighbors.size() ||
        topology.HeapIds.size() != topology.BisectorData.size() ||
        dynamicElementCount > topology.HeapIds.size())
    {
        return fail("invalid CBT committed topology layout");
    }

    std::vector<bool> committed(dynamicElementCount, false);
    // 新置位列表同时验证 OCBT 动态域边界和一帧内的唯一所有权。
    for (const std::uint32_t slot : topology.CommittedDynamicSlots)
    {
        if (slot >= dynamicElementCount || committed[slot] || topology.HeapIds[slot] == 0U)
        {
            return fail("committed CBT slot is out of range, duplicated, or inactive");
        }
        committed[slot] = true;
    }

    for (std::size_t current = 0U; current < topology.HeapIds.size(); ++current)
    {
        // heapID 为零的空槽不要求邻接内容清零，也不会进入 active indexation。
        if (topology.HeapIds[current] == 0U)
        {
            continue;
        }
        const CbtBisectorNeighbors& neighbors = topology.Neighbors[current];
        for (const std::uint32_t neighbor : {neighbors.Previous, neighbors.Next, neighbors.Twin})
        {
            if (!IsNeighbor(neighbor, topology.HeapIds.size()))
            {
                return fail("active CBT neighbor is out of range");
            }
            if (neighbor == InvalidCbtBisectorIndex)
            {
                continue;
            }
            if (topology.HeapIds[neighbor] == 0U)
            {
                return fail("active CBT node references an inactive neighbor");
            }
            const CbtBisectorNeighbors& reverse = topology.Neighbors[neighbor];
            if (reverse.Previous != current && reverse.Next != current && reverse.Twin != current)
            {
                return fail("CBT neighbor reference is not reciprocal");
            }
        }
    }
    if (errorMessage != nullptr)
    {
        errorMessage->clear();
    }
    return true;
}

CbtLebTriangleResult EvaluateCbtLebTriangle(
    std::uint64_t heapId,
    std::uint32_t baseDepth,
    const std::array<CbtBaseControlPoint, CbtBaseControlPointCount>& baseControlPoints)
{
    // baseDepth 切分 heapID 的基础面前缀与该面内部的二进制 LEB 路径。
    // 返回 Parent 是为了给分类阶段提供与上游第四个辅助位置相同的语义。
    CbtLebTriangleResult result{};
    const std::uint32_t depth = heapId == 0U
        ? 0U
        : static_cast<std::uint32_t>(std::bit_width(heapId));
    if (heapId == 0U || baseDepth == 0U || depth < baseDepth || baseDepth >= 64U)
    {
        result.Valid = false;
        return result;
    }

    const std::uint32_t subtreeDepth = depth - baseDepth;
    const std::uint64_t firstBaseHeapId = std::uint64_t{1U} << (baseDepth - 1U);
    const std::uint64_t baseHeapId = heapId >> subtreeDepth;
    if (baseHeapId < firstBaseHeapId ||
        baseHeapId - firstBaseHeapId >= CbtBaseBisectorCount)
    {
        result.Valid = false;
        return result;
    }
    result.BaseBisector = static_cast<std::uint32_t>(baseHeapId - firstBaseHeapId);
    // 每个基础二分器连续保存三个控制点，因此前缀可直接定位基础三角形。
    std::copy_n(
        baseControlPoints.begin() + static_cast<std::ptrdiff_t>(result.BaseBisector * 3U),
        3U,
        result.Child.begin());
    result.Parent = result.Child;
    for (std::uint32_t remaining = subtreeDepth; remaining > 0U; --remaining)
    {
        // 先保存本步父三角形，再根据当前路径位生成 child。
        result.Parent = result.Child;
        const std::uint32_t bit = remaining - 1U;
        SplitLebTriangle(result.Child, ((heapId >> bit) & 1U) != 0U);
    }
    return result;
}
} // namespace ParallelRoam::Algorithms::Cbt2024
