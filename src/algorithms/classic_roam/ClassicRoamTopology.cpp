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
    // 子路径沿用二叉堆编码，使同一拓扑位置能跨帧稳定识别
    return parentPathId * 2ULL;
}

std::uint64_t RightChildPathId(std::uint64_t parentPathId)
{
    // 右子节点用末位 1 与左子节点区分
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
        // 节点已经细分时由其子节点继续承担更细层级的决策
        return false;
    }

    if (node->Depth >= _settings.MaxDepth)
    {
        ++_stats.RejectedSplitCount;
        return false;
    }

    if (_remainingSplitBudget <= reservedSplitSlots)
    {
        // 强制细分链中的每个待细分节点都需要预留一个三角形预算名额
        ++_stats.BudgetRejectedSplitCount;
        return false;
    }

    ClassicRoamNode* baseNeighbor = node->BaseNeighbor;
    if (_settings.EnableLocalConstraints)
    {
        int guard = 0;
        // 底边邻居尚未互指时，先沿邻接链细分到能够组成合法菱形
        // 递归保护计数防止损坏的邻接关系造成无限递归
        while (baseNeighbor != nullptr &&
               baseNeighbor != forcedFrom &&
               baseNeighbor->BaseNeighbor != node &&
               guard < _settings.MaxDepth + 2)
        {
            ++_stats.ConstraintPassCount;
            // Classic ROAM 要求两侧先形成互为底边邻居的菱形关系
            if (!SplitNode(baseNeighbor, SplitReason::ForcedByBaseNeighbor, node, reservedSplitSlots + 1U))
            {
                return false;
            }

            // 递归细分可能重写当前节点的底边邻居，因此每轮都重新读取
            baseNeighbor = node->BaseNeighbor;
            ++guard;
        }
    }

    if (_settings.EnableLocalConstraints && baseNeighbor != nullptr && IsLeaf(baseNeighbor) && baseNeighbor != forcedFrom)
    {
        // 先细分底边邻居，使旧底边两侧一起细分为无裂缝菱形
        // forcedFrom 防止互为底边邻居的两个叶节点递归回跳
        ++_stats.ConstraintPassCount;
        if (!SplitNode(baseNeighbor, SplitReason::ForcedByBaseNeighbor, node, reservedSplitSlots + 1U))
        {
            return false;
        }
        // 强制细分可能改变邻接指针，连接子节点前必须取得最新菱形
        baseNeighbor = node->BaseNeighbor;
    }

    // 确定补齐相邻三角形需要执行的全部细分后，先从 Q_m 移除将受影响的菱形
    std::vector<ClassicRoamNode*> queueNeighborhood;
    AppendQueueNeighborhood(node, queueNeighborhood);
    AppendQueueNeighborhood(baseNeighbor, queueNeighborhood);
    InvalidateMergeQueueNeighborhood(queueNeighborhood);
    RemoveSplitQueueNode(node);

    const std::uint64_t parentPathId = node->PathId;
    if (node->LeftChild == nullptr || node->RightChild == nullptr)
    {
        // 首次细分创建子节点，合并后再次细分则复用原有对象
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
    // 重新激活子节点前清空旧邻接，避免历史合并状态污染本次细分
    // 子节点对象可以复用，但邻居必须按当前活动拓扑重新连接
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
    // 强制细分标记只控制调试颜色，不改变拓扑处理规则
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

    // 左子节点的左边与右子节点的右边共享本次细分产生的中线
    leftChild->LeftNeighbor = rightChild;
    rightChild->RightNeighbor = leftChild;

    // 两个子节点的底边分别继承父节点的左边和右边
    leftChild->BaseNeighbor = node->LeftNeighbor;
    rightChild->BaseNeighbor = node->RightNeighbor;
    ReplaceNeighborReference(node->LeftNeighbor, node, leftChild);
    ReplaceNeighborReference(node->RightNeighbor, node, rightChild);

    if (baseNeighbor == nullptr || IsLeaf(baseNeighbor))
    {
        // 对侧仍是未细分叶节点时没有可连接的子节点
        // 随后的强制细分会补齐完整菱形
        return;
    }

    // 底边邻居已经细分时，两侧四个子节点可以连接成无裂缝菱形
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
        // 地形外边界没有邻居需要更新
        return;
    }

    // 相邻叶节点仍指向旧节点时，将引用改到细分后与其共享整条边的子节点
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
    // Q_m 已保证基础拓扑条件，此处只检查本帧屏幕误差是否允许合并
    if (!IsMergeableTopology(node))
    {
        return false;
    }

    if (ComputeScreenErrorScore(*node) > maximumScore)
    {
        // 父节点误差仍高时不能合并，否则会造成明显画质下降
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

    // 两个子节点退出当前网格，父节点重新进入 Q_s，子节点对象留在池中等待复用
    RemoveSplitQueueNode(leftChild);
    RemoveSplitQueueNode(rightChild);

    // 父节点的左右边分别由两个子节点的底边恢复
    // 外部邻居必须改为指向父节点，不能继续引用已经停用的子节点
    ReplaceNeighborReference(newLeftNeighbor, leftChild, node);
    ReplaceNeighborReference(newRightNeighbor, rightChild, node);
    node->LeftNeighbor = newLeftNeighbor;
    node->RightNeighbor = newRightNeighbor;
    // 保留子节点指针但将其停用，后续再次细分时可以复用对象
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
        // 跨帧保留的队列可能暂时含有已不满足条件的合并候选，实际合并前需要再次确认
        return false;
    }

    ClassicRoamNode* baseNeighbor = node->BaseNeighbor;
    if (baseNeighbor != nullptr && !IsLeaf(baseNeighbor))
    {
        // 完整菱形必须同时合并当前父节点和对侧父节点
        // 只合并一侧会让细子边直接连接粗边并产生裂缝
        if (!CanMergeNode(baseNeighbor, maximumScore) || baseNeighbor->BaseNeighbor != node)
        {
            return false;
        }
    }

    // 两侧都确认可合并后再移除局部 Q_m 成员，检查失败时不会改变跨帧保留的队列
    std::vector<ClassicRoamNode*> queueNeighborhood;
    AppendQueueNeighborhood(node, queueNeighborhood);
    AppendQueueNeighborhood(baseNeighbor, queueNeighborhood);
    InvalidateMergeQueueNeighborhood(queueNeighborhood);
    if (baseNeighbor != nullptr && !IsLeaf(baseNeighbor))
    {
        // 先恢复两个父节点的底边互指，再停用两侧子节点
        // MergeSingleNode 不修改 BaseNeighbor，因此该关系会一直保留
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
