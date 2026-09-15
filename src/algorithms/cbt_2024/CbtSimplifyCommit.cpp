#include "algorithms/cbt_2024/CbtSimplifyCommit.h"

#include <algorithm>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <string>

namespace ParallelRoam::Algorithms::Cbt2024
{
namespace
{
bool IsIndex(std::uint32_t index, std::size_t count)
{
    return index < count;
}

bool SetInvalid(CbtSimplifyCommitResult& result)
{
    result.Valid = false;
    return false;
}

bool HasReverseNeighbor(
    const CbtBisectorNeighbors& neighbors,
    std::uint32_t physicalSlot)
{
    return neighbors.Previous == physicalSlot ||
           neighbors.Next == physicalSlot ||
           neighbors.Twin == physicalSlot;
}

bool ReplaceNeighbor(
    CbtBisectorNeighbors& neighbors,
    std::uint32_t expected,
    std::uint32_t replacement)
{
    // 返回值区分真实邻接改写与无操作传播，避免把未变化叶片计入 merge 着色。
    // CPU 参考与 GPU 的逐分量 CompareExchange 保持相同的条件替换语义
    bool replaced = false;
    if (neighbors.Previous == expected)
    {
        neighbors.Previous = replacement;
        replaced = true;
    }
    if (neighbors.Next == expected)
    {
        neighbors.Next = replacement;
        replaced = true;
    }
    if (neighbors.Twin == expected)
    {
        neighbors.Twin = replacement;
        replaced = true;
    }
    return replaced;
}

bool PrepareSimplification(
    CbtSimplifyCommitResult& result,
    std::uint32_t currentId)
{
    if (!IsIndex(currentId, result.HeapIds.size()) || result.HeapIds[currentId] == 0U)
    {
        return SetInvalid(result);
    }

    const std::uint64_t currentHeapId = result.HeapIds[currentId];
    const CbtBisectorData& currentData = result.BisectorData[currentId];
    // Classification 只把偶 heapID 写入 simplify candidate 列表
    if ((currentHeapId & 1U) != 0U || currentData.BisectorState != CbtSimplifyElement)
    {
        return true;
    }

    const CbtBisectorNeighbors currentNeighbors = result.Neighbors[currentId];
    const std::uint32_t pairId = currentNeighbors.Previous;
    if (!IsIndex(pairId, result.HeapIds.size()) || result.HeapIds[pairId] == 0U)
    {
        return SetInvalid(result);
    }
    const CbtBisectorData& pairData = result.BisectorData[pairId];
    const std::uint32_t currentDepth = CbtHeapIdDepth(currentHeapId);
    if (CbtHeapIdDepth(result.HeapIds[pairId]) != currentDepth ||
        pairData.BisectorState != CbtSimplifyElement)
    {
        return true;
    }

    const CbtBisectorNeighbors pairNeighbors = result.Neighbors[pairId];
    const std::uint32_t twinLowId = pairNeighbors.Previous;
    const std::uint32_t twinHighId = currentNeighbors.Next;
    if (twinLowId != InvalidCbtBisectorIndex)
    {
        if (!IsIndex(twinLowId, result.HeapIds.size()) ||
            !IsIndex(twinHighId, result.HeapIds.size()) ||
            result.HeapIds[twinLowId] == 0U || result.HeapIds[twinHighId] == 0U)
        {
            return SetInvalid(result);
        }
        // facing pair 中逻辑 heapID 最小的一侧唯一登记四节点 merge
        if (currentHeapId > result.HeapIds[twinLowId])
        {
            return true;
        }
        if (CbtHeapIdDepth(result.HeapIds[twinLowId]) != currentDepth ||
            CbtHeapIdDepth(result.HeapIds[twinHighId]) != currentDepth ||
            result.BisectorData[twinLowId].BisectorState != CbtSimplifyElement ||
            result.BisectorData[twinHighId].BisectorState != CbtSimplifyElement)
        {
            return true;
        }
    }

    if (std::find(
            result.SimplificationNodes.begin(),
            result.SimplificationNodes.end(),
            currentId) == result.SimplificationNodes.end())
    {
        result.SimplificationNodes.push_back(currentId);
    }
    return true;
}

bool AppendReleasedSlot(
    CbtSimplifyCommitResult& result,
    std::uint32_t physicalSlot,
    std::uint32_t dynamicElementCount)
{
    if (physicalSlot >= dynamicElementCount ||
        std::find(
            result.ReleasedDynamicSlots.begin(),
            result.ReleasedDynamicSlots.end(),
            physicalSlot) != result.ReleasedDynamicSlots.end())
    {
        return SetInvalid(result);
    }
    result.ReleasedDynamicSlots.push_back(physicalSlot);
    return true;
}

bool CommitSimplification(
    CbtSimplifyCommitResult& result,
    std::uint32_t currentId,
    std::uint32_t dynamicElementCount)
{
    const CbtBisectorNeighbors currentNeighbors = result.Neighbors[currentId];
    const std::uint32_t pairId = currentNeighbors.Previous;
    const CbtBisectorNeighbors pairNeighbors = result.Neighbors[pairId];
    const std::uint32_t twinLowId = pairNeighbors.Previous;
    const std::uint32_t twinHighId = currentNeighbors.Next;
    if (!AppendReleasedSlot(result, pairId, dynamicElementCount))
    {
        return false;
    }

    result.HeapIds[currentId] /= 2U;
    result.HeapIds[pairId] = 0U;
    result.Neighbors[currentId] = {
        currentNeighbors.Twin,
        pairNeighbors.Twin,
        twinLowId,
    };

    CbtBisectorData currentData = result.BisectorData[currentId];
    currentData.PropagationId = pairId;
    currentData.ProblematicNeighbor = pairNeighbors.Twin;
    currentData.BisectorState = CbtMergedElement;
    currentData.Flags = CbtVisibleFlag | CbtModifiedFlag | CbtMergeEventFlag |
        EncodeCbtDebugEventLifetime(CbtDebugEventHoldFrames) |
        EncodeCbtActiveDepth(
            static_cast<std::uint32_t>(std::bit_width(result.HeapIds[currentId])));
    result.BisectorData[currentId] = currentData;
    if (currentData.ProblematicNeighbor != InvalidCbtBisectorIndex)
    {
        result.PropagationNodes.push_back(currentId);
    }

    CbtBisectorData pairData = result.BisectorData[pairId];
    pairData.BisectorState = CbtMergedElement;
    pairData.Flags = 0U;
    result.BisectorData[pairId] = pairData;

    if (twinLowId == InvalidCbtBisectorIndex)
    {
        ++result.PairMergeCount;
        return true;
    }
    if (!AppendReleasedSlot(result, twinHighId, dynamicElementCount))
    {
        return false;
    }

    const CbtBisectorNeighbors lowNeighbors = result.Neighbors[twinLowId];
    const CbtBisectorNeighbors highNeighbors = result.Neighbors[twinHighId];
    result.HeapIds[twinLowId] /= 2U;
    result.HeapIds[twinHighId] = 0U;
    result.Neighbors[twinLowId] = {
        lowNeighbors.Twin,
        highNeighbors.Twin,
        currentId,
    };

    CbtBisectorData lowData = result.BisectorData[twinLowId];
    lowData.PropagationId = twinHighId;
    lowData.ProblematicNeighbor = highNeighbors.Twin;
    lowData.BisectorState = CbtMergedElement;
    lowData.Flags = CbtVisibleFlag | CbtModifiedFlag | CbtMergeEventFlag |
        EncodeCbtDebugEventLifetime(CbtDebugEventHoldFrames) |
        EncodeCbtActiveDepth(
            static_cast<std::uint32_t>(std::bit_width(result.HeapIds[twinLowId])));
    result.BisectorData[twinLowId] = lowData;
    if (lowData.ProblematicNeighbor != InvalidCbtBisectorIndex)
    {
        result.PropagationNodes.push_back(twinLowId);
    }

    CbtBisectorData highData = result.BisectorData[twinHighId];
    highData.BisectorState = CbtMergedElement;
    highData.Flags = 0U;
    result.BisectorData[twinHighId] = highData;
    ++result.QuadMergeCount;
    return true;
}

bool PropagateSimplification(
    CbtSimplifyCommitResult& result,
    std::uint32_t currentId)
{
    CbtBisectorData& currentData = result.BisectorData[currentId];
    const std::uint32_t deletedPair = currentData.PropagationId;
    const std::uint32_t neighborId = currentData.ProblematicNeighbor;
    if (!IsIndex(deletedPair, result.HeapIds.size()) ||
        !IsIndex(neighborId, result.HeapIds.size()))
    {
        return SetInvalid(result);
    }

    const CbtBisectorData neighborData = result.BisectorData[neighborId];
    if (neighborData.BisectorState != CbtMergedElement || result.HeapIds[neighborId] != 0U)
    {
        if (ReplaceNeighbor(result.Neighbors[neighborId], deletedPair, currentId))
        {
            result.BisectorData[neighborId].Flags = WithCbtDebugEvent(
                result.BisectorData[neighborId].Flags,
                CbtMergeEventFlag);
        }
    }
    else
    {
        // 被相邻 merge 删除的节点通过其保留 pair 转发外侧引用
        const std::uint32_t neighborPair = result.Neighbors[neighborId].Next;
        if (!IsIndex(neighborPair, result.HeapIds.size()) || result.HeapIds[neighborPair] == 0U)
        {
            return SetInvalid(result);
        }
        if (ReplaceNeighbor(result.Neighbors[neighborPair], deletedPair, currentId))
        {
            result.BisectorData[neighborPair].Flags = WithCbtDebugEvent(
                result.BisectorData[neighborPair].Flags,
                CbtMergeEventFlag);
        }
    }
    currentData.ProblematicNeighbor = InvalidCbtBisectorIndex;
    return true;
}
} // namespace

CbtSimplifyCommitResult CommitCbtSimplifications(
    const std::vector<std::uint64_t>& heapIds,
    const std::vector<CbtBisectorNeighbors>& neighbors,
    const std::vector<CbtBisectorData>& bisectorData,
    const std::vector<std::uint32_t>& simplifyCandidates,
    std::uint32_t dynamicElementCount)
{
    CbtSimplifyCommitResult result{};
    result.HeapIds = heapIds;
    result.Neighbors = neighbors;
    result.BisectorData = bisectorData;
    if (heapIds.size() != neighbors.size() || heapIds.size() != bisectorData.size() ||
        dynamicElementCount > heapIds.size())
    {
        SetInvalid(result);
        return result;
    }

    for (const std::uint32_t currentId : simplifyCandidates)
    {
        if (!PrepareSimplification(result, currentId))
        {
            return result;
        }
    }
    for (const std::uint32_t currentId : result.SimplificationNodes)
    {
        if (!CommitSimplification(result, currentId, dynamicElementCount))
        {
            return result;
        }
    }
    for (const std::uint32_t currentId : result.PropagationNodes)
    {
        if (!PropagateSimplification(result, currentId))
        {
            return result;
        }
    }
    return result;
}

bool ValidateCbtSimplifiedTopology(
    const CbtSimplifyCommitResult& topology,
    std::uint32_t dynamicElementCount,
    std::string* errorMessage)
{
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
        return fail("CBT simplify snapshot layout mismatch");
    }

    std::vector<std::uint32_t> released = topology.ReleasedDynamicSlots;
    std::sort(released.begin(), released.end());
    if (std::adjacent_find(released.begin(), released.end()) != released.end())
    {
        return fail("CBT simplify released a dynamic slot more than once");
    }
    for (const std::uint32_t physicalSlot : released)
    {
        if (physicalSlot >= dynamicElementCount || topology.HeapIds[physicalSlot] != 0U)
        {
            return fail("CBT simplify did not clear a released dynamic slot");
        }
    }

    for (std::uint32_t physicalSlot = 0U; physicalSlot < topology.HeapIds.size(); ++physicalSlot)
    {
        if (topology.HeapIds[physicalSlot] == 0U)
        {
            continue;
        }
        const CbtBisectorNeighbors& neighbors = topology.Neighbors[physicalSlot];
        for (const std::uint32_t neighbor : {neighbors.Previous, neighbors.Next, neighbors.Twin})
        {
            if (neighbor == InvalidCbtBisectorIndex)
            {
                continue;
            }
            if (!IsIndex(neighbor, topology.HeapIds.size()) || topology.HeapIds[neighbor] == 0U)
            {
                return fail("CBT simplify left an active node pointing at a released slot");
            }
            if (!HasReverseNeighbor(topology.Neighbors[neighbor], physicalSlot))
            {
                return fail("CBT simplify neighbor relation is not reciprocal");
            }
        }
    }
    if (errorMessage != nullptr)
    {
        errorMessage->clear();
    }
    return true;
}
} // namespace ParallelRoam::Algorithms::Cbt2024
