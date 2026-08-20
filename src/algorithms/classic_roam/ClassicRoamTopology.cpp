#include "algorithms/classic_roam/ClassicRoamMeshBuilder.h"

#include <algorithm>
#include <cstdint>
#include <vector>

namespace ParallelRoam::Algorithms::ClassicRoam
{
namespace
{
std::uint64_t LeftChildPathId(std::uint64_t parentPathId)
{
    // child path 保持二叉堆编码，便于跨帧 hysteresis 复用
    return parentPathId * 2ULL;
}

std::uint64_t RightChildPathId(std::uint64_t parentPathId)
{
    // right child 通过末位 1 与 left child 区分
    return parentPathId * 2ULL + 1ULL;
}
} // 匿名命名空间

bool ClassicRoamMeshBuilder::SplitNode(
    ClassicRoamNode* node,
    SplitReason reason,
    ClassicRoamNode* forcedFrom,
    std::size_t reservedSplitSlots)
{
    if (!IsLeaf(node))
    {
        // 已 split 的节点由 child 承担更细层级决策
        return false;
    }

    if (node->Depth >= _settings.MaxDepth)
    {
        ++_stats.RejectedSplitCount;
        return false;
    }

    if (_remainingSplitBudget <= reservedSplitSlots)
    {
        // forced 调用必须为所有尚未 split 的调用者各保留一个 token
        ++_stats.BudgetRejectedSplitCount;
        return false;
    }

    ClassicRoamNode* baseNeighbor = node->BaseNeighbor;
    if (_settings.EnableLocalConstraints)
    {
        int guard = 0;
        // baseNeighbor 不是互指关系时，先沿邻接链追到合法 diamond
        // guard 防止损坏拓扑导致无限递归
        while (baseNeighbor != nullptr &&
               baseNeighbor != forcedFrom &&
               baseNeighbor->BaseNeighbor != node &&
               guard < _settings.MaxDepth + 2)
        {
            ++_stats.ConstraintPassCount;
            // 经典 ROAM 要求 baseNeighbor 先回到互为 base 的 diamond 关系
            if (!SplitNode(baseNeighbor, SplitReason::ForcedByBaseNeighbor, node, reservedSplitSlots + 1U))
            {
                return false;
            }

            // split 可能通过 ReplaceNeighborReference 改写 node 的 baseNeighbor
            baseNeighbor = node->BaseNeighbor;
            ++guard;
        }
    }

    if (_settings.EnableLocalConstraints && baseNeighbor != nullptr && IsLeaf(baseNeighbor) && baseNeighbor != forcedFrom)
    {
        // Classic ROAM 先补齐 base neighbor，保证旧 base edge 两侧一起 split 成 diamond
        // forcedFrom 防止互为 base neighbor 的两个 leaf 递归回跳
        ++_stats.ConstraintPassCount;
        if (!SplitNode(baseNeighbor, SplitReason::ForcedByBaseNeighbor, node, reservedSplitSlots + 1U))
        {
            return false;
        }
        // forced split 完成后刷新指针，后续 LinkSplitNeighbors 使用最新 diamond
        baseNeighbor = node->BaseNeighbor;
    }

    // forced closure 已确定后，先摘除会受本次局部拓扑变更影响的 Q_m membership
    std::vector<ClassicRoamNode*> queueNeighborhood;
    AppendQueueNeighborhood(node, queueNeighborhood);
    AppendQueueNeighborhood(baseNeighbor, queueNeighborhood);
    InvalidateMergeQueueNeighborhood(queueNeighborhood);
    RemoveSplitQueueNode(node);

    const std::uint64_t parentPathId = node->PathId;
    if (node->LeftChild == nullptr || node->RightChild == nullptr)
    {
        // 首次 split 创建 child，后续 merge 后再次 split 会复用旧 child
        const int childDepth = node->Depth + 1;
        const TriangleDomainChildren childDomains = SplitTriangleDomain(node->Domain);
        const std::size_t leftVarianceIndex = node->VarianceIndex * 2U + 1U;
        const std::size_t rightVarianceIndex = node->VarianceIndex * 2U + 2U;
        node->LeftChild = AddNode(
            childDomains.Left,
            node,
            childDepth,
            LeftChildPathId(parentPathId),
            node->VarianceTreeIndex,
            leftVarianceIndex);
        node->RightChild = AddNode(
            childDomains.Right,
            node,
            childDepth,
            RightChildPathId(parentPathId),
            node->VarianceTreeIndex,
            rightVarianceIndex);
    }

    node->IsSplit = true;
    node->Active = true;
    node->SplitBuildId = _buildSequence;
    // 重新激活 child 前清空旧邻接，避免历史 merge 状态污染本次 split
    // child 指针复用是性能优化
    // 但 neighbor 指针必须按当前 active topology 重建
    node->LeftChild->BaseNeighbor = nullptr;
    node->LeftChild->LeftNeighbor = nullptr;
    node->LeftChild->RightNeighbor = nullptr;
    node->RightChild->BaseNeighbor = nullptr;
    node->RightChild->LeftNeighbor = nullptr;
    node->RightChild->RightNeighbor = nullptr;
    node->LeftChild->Active = true;
    node->RightChild->Active = true;
    node->LeftChild->ActivatedBuildId = _buildSequence;
    node->RightChild->ActivatedBuildId = _buildSequence;
    node->LeftChild->ActivatedByForcedSplit = reason != SplitReason::Requested;
    // forced 标记只用于 debug color，不改变拓扑语义
    node->RightChild->ActivatedByForcedSplit = reason != SplitReason::Requested;

    LinkSplitNeighbors(node, baseNeighbor);
    RecordMeshSplit(node);
    InsertSplitQueueNode(node->LeftChild);
    InsertSplitQueueNode(node->RightChild);
    AppendQueueNeighborhood(node, queueNeighborhood);
    AppendQueueNeighborhood(node->LeftChild, queueNeighborhood);
    AppendQueueNeighborhood(node->RightChild, queueNeighborhood);
    AppendQueueNeighborhood(baseNeighbor, queueNeighborhood);
    RefreshMergeQueueNeighborhood(queueNeighborhood);
    --_remainingSplitBudget;
    _currentSplitPaths.insert(parentPathId);
    ++_stats.SplitCount;
    if (reason != SplitReason::Requested)
    {
        ++_stats.ForcedSplitCount;
    }
    return true;
}

void ClassicRoamMeshBuilder::LinkSplitNeighbors(ClassicRoamNode* node, ClassicRoamNode* baseNeighbor)
{
    ClassicRoamNode* leftChild = node->LeftChild;
    ClassicRoamNode* rightChild = node->RightChild;
    if (leftChild == nullptr || rightChild == nullptr)
    {
        return;
    }

    // left child 的 left edge 与 right child 的 right edge 共享 split 中线
    leftChild->LeftNeighbor = rightChild;
    rightChild->RightNeighbor = leftChild;

    // child 的 base edge 分别来自父节点的 left edge 和 right edge
    leftChild->BaseNeighbor = node->LeftNeighbor;
    rightChild->BaseNeighbor = node->RightNeighbor;
    ReplaceNeighborReference(node->LeftNeighbor, node, leftChild);
    ReplaceNeighborReference(node->RightNeighbor, node, rightChild);

    if (baseNeighbor == nullptr || IsLeaf(baseNeighbor))
    {
        // 对侧仍是粗 leaf 时没有可连接的 child pair
        // 后续 forced split 会补齐 diamond
        return;
    }

    // baseNeighbor 已经 split 时，四个 child 共同组成无裂缝 diamond
    leftChild->RightNeighbor = baseNeighbor->RightChild;
    rightChild->LeftNeighbor = baseNeighbor->LeftChild;
    if (baseNeighbor->RightChild != nullptr)
    {
        baseNeighbor->RightChild->LeftNeighbor = leftChild;
    }

    if (baseNeighbor->LeftChild != nullptr)
    {
        baseNeighbor->LeftChild->RightNeighbor = rightChild;
    }
}

void ClassicRoamMeshBuilder::ReplaceNeighborReference(
    ClassicRoamNode* neighbor,
    ClassicRoamNode* oldNode,
    ClassicRoamNode* newNode) const
{
    if (neighbor == nullptr)
    {
        // terrain 边界没有邻居需要修复
        return;
    }

    // 相邻 leaf 仍指向旧节点时，把它改到 split 后共享完整边的 child
    if (neighbor->BaseNeighbor == oldNode)
    {
        neighbor->BaseNeighbor = newNode;
    }

    if (neighbor->LeftNeighbor == oldNode)
    {
        neighbor->LeftNeighbor = newNode;
    }

    if (neighbor->RightNeighbor == oldNode)
    {
        neighbor->RightNeighbor = newNode;
    }
}

bool ClassicRoamMeshBuilder::CanMergeNode(const ClassicRoamNode* node, float maximumScore) const
{
    // Q_m membership 先保证纯 topology 条件，这里只叠加当前帧 priority 上限
    if (!IsMergeableTopology(node))
    {
        return false;
    }

    if (ComputeScreenErrorScore(*node) > maximumScore)
    {
        // parent 自身误差还高时，回收会造成明显 LOD 退化
        return false;
    }

    const ClassicRoamNode* baseNeighbor = node->BaseNeighbor;
    if (baseNeighbor == nullptr || IsLeaf(baseNeighbor))
    {
        return true;
    }

    return ComputeScreenErrorScore(*baseNeighbor) <= maximumScore;
}

void ClassicRoamMeshBuilder::MergeSingleNode(ClassicRoamNode* node)
{
    if (node == nullptr || node->LeftChild == nullptr || node->RightChild == nullptr)
    {
        return;
    }

    ClassicRoamNode* leftChild = node->LeftChild;
    ClassicRoamNode* rightChild = node->RightChild;
    ClassicRoamNode* newLeftNeighbor = leftChild->BaseNeighbor;
    ClassicRoamNode* newRightNeighbor = rightChild->BaseNeighbor;

    // 两个 child 离开 active cut，parent 回到 Q_s；对象仍留在池中等待复用
    RemoveSplitQueueNode(leftChild);
    RemoveSplitQueueNode(rightChild);

    // parent 的 left/right 边分别来自两个 child 的 base 边
    // 外部 neighbor 必须改指向 parent，不能继续指向 inactive child
    // 否则 validator 会发现 neighbor 指向非 active leaf
    ReplaceNeighborReference(newLeftNeighbor, leftChild, node);
    ReplaceNeighborReference(newRightNeighbor, rightChild, node);
    node->LeftNeighbor = newLeftNeighbor;
    node->RightNeighbor = newRightNeighbor;
    // child 指针保留但不再 active，后续重新 split 可复用 child 对象
    node->IsSplit = false;
    node->Active = true;
    leftChild->Active = false;
    rightChild->Active = false;
    node->ActivatedBuildId = _buildSequence;
    node->MergeBuildId = _buildSequence;
    node->ActivatedByForcedSplit = false;
    RecordMeshMerge(node);
    InsertSplitQueueNode(node);
    _remainingSplitBudget = std::min(
        _settings.TriangleBudget,
        _remainingSplitBudget + 1U);
    ++_stats.MergeCount;
}

bool ClassicRoamMeshBuilder::MergeNodeOrDiamond(ClassicRoamNode* node, float maximumScore)
{
    if (!CanMergeNode(node, maximumScore))
    {
        // 外层队列可能持有已过期的 merge candidate
        return false;
    }

    ClassicRoamNode* baseNeighbor = node->BaseNeighbor;
    if (baseNeighbor != nullptr && !IsLeaf(baseNeighbor))
    {
        // 完整 diamond merge 要同时回收当前 parent 和 base parent
        // 单侧回收会让对侧 child 贴到粗边上
        if (!CanMergeNode(baseNeighbor, maximumScore) || baseNeighbor->BaseNeighbor != node)
        {
            return false;
        }
    }

    // 两侧都确认可合并后再摘除局部 Q_m membership，失败路径不改变持久队列。
    std::vector<ClassicRoamNode*> queueNeighborhood;
    AppendQueueNeighborhood(node, queueNeighborhood);
    AppendQueueNeighborhood(baseNeighbor, queueNeighborhood);
    InvalidateMergeQueueNeighborhood(queueNeighborhood);
    if (baseNeighbor != nullptr && !IsLeaf(baseNeighbor))
    {
        // 先固定 parent 之间的 base 互指，再回收两侧 child
        // MergeSingleNode 不会改 baseNeighbor，因此互指关系会保留下来
        node->BaseNeighbor = baseNeighbor;
        baseNeighbor->BaseNeighbor = node;
        MergeSingleNode(node);
        MergeSingleNode(baseNeighbor);
        node->BaseNeighbor = baseNeighbor;
        baseNeighbor->BaseNeighbor = node;
        AppendQueueNeighborhood(node, queueNeighborhood);
        AppendQueueNeighborhood(baseNeighbor, queueNeighborhood);
        RefreshMergeQueueNeighborhood(queueNeighborhood);
        return true;
    }

    MergeSingleNode(node);
    AppendQueueNeighborhood(node, queueNeighborhood);
    RefreshMergeQueueNeighborhood(queueNeighborhood);
    return true;
}
} // 命名空间 ParallelRoam::Algorithms::ClassicRoam
