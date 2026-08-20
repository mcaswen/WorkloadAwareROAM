#pragma once

#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstddef>
#include <limits>
#include <vector>

namespace ParallelRoam::Algorithms::Roam
{
/// <summary>
/// 返回覆盖给定采样段数所需的二次幂层级。
/// </summary>
[[nodiscard]] inline int CeilLog2Extent(int segmentCount)
{
    const std::size_t target = static_cast<std::size_t>(std::max(segmentCount, 1));
    std::size_t extent = 1U;
    int level = 0;
    while (extent < target)
    {
        extent *= 2U;
        ++level;
    }
    return level;
}

/// <summary>
/// 二叉三角树每两个深度层级会把两个地形轴的采样间隔各减半。
/// </summary>
[[nodiscard]] inline int ResolveNestedWedgieTreeDepth(
    int width,
    int height,
    int runtimeMaxDepth,
    int maximumSupportedDepth)
{
    const int supportedDepth = std::max(maximumSupportedDepth, 0);
    const int normalizedRuntimeDepth = std::clamp(runtimeMaxDepth, 0, supportedDepth);
    const int sourceAxisLevel = std::max(
        CeilLog2Extent(width - 1),
        CeilLog2Extent(height - 1));
    const int sourceDepth = std::min(sourceAxisLevel * 2, supportedDepth);
    return std::max(normalizedRuntimeDepth, sourceDepth);
}

/// <summary>
/// 计算包含 root 和指定最细深度的完整二叉树节点数。
/// </summary>
[[nodiscard]] inline std::size_t CompleteBinaryTreeNodeCount(int finestDepth)
{
    assert(finestDepth >= 0);
    assert(static_cast<unsigned>(finestDepth + 1) < std::numeric_limits<std::size_t>::digits);
    return (std::size_t{1} << static_cast<unsigned>(finestDepth + 1)) - 1U;
}

/// <summary>
/// 按 ROAM 论文公式 (1) 自底向上构建 nested wedgie thickness。
/// </summary>
template <typename Domain, typename SplitFunction, typename DisplacementFunction>
[[nodiscard]] float BuildNestedWedgieSubtree(
    const Domain& domain,
    int depth,
    int finestDepth,
    std::size_t treeIndex,
    std::vector<float>& tree,
    const SplitFunction& splitDomain,
    const DisplacementFunction& signedBaseMidpointDisplacement)
{
    assert(treeIndex < tree.size());
    if (depth >= finestDepth)
    {
        // 论文把输入地形的最细 bintree level 定义为零 thickness。
        tree[treeIndex] = 0.0F;
        return 0.0F;
    }

    const auto children = splitDomain(domain);
    const float leftThickness = BuildNestedWedgieSubtree(
        children.Left,
        depth + 1,
        finestDepth,
        treeIndex * 2U + 1U,
        tree,
        splitDomain,
        signedBaseMidpointDisplacement);
    const float rightThickness = BuildNestedWedgieSubtree(
        children.Right,
        depth + 1,
        finestDepth,
        treeIndex * 2U + 2U,
        tree,
        splitDomain,
        signedBaseMidpointDisplacement);
    const float localDisplacement = std::abs(signedBaseMidpointDisplacement(domain));
    const float thickness = std::max(leftThickness, rightThickness) + localDisplacement;
    tree[treeIndex] = thickness;
    return thickness;
}

/// <summary>
/// 分配完整二叉树并返回 root 的 nested wedgie thickness。
/// </summary>
template <typename Domain, typename SplitFunction, typename DisplacementFunction>
[[nodiscard]] float BuildNestedWedgieTree(
    const Domain& root,
    int finestDepth,
    std::vector<float>& tree,
    const SplitFunction& splitDomain,
    const DisplacementFunction& signedBaseMidpointDisplacement)
{
    tree.assign(CompleteBinaryTreeNodeCount(finestDepth), 0.0F);
    return BuildNestedWedgieSubtree(
        root,
        0,
        finestDepth,
        0U,
        tree,
        splitDomain,
        signedBaseMidpointDisplacement);
}
} // namespace ParallelRoam::Algorithms::Roam
