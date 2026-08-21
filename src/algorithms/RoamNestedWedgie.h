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
/// 计算覆盖给定采样段数所需的最小二次幂层级，用于确定误差树深度
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
/// 根据高度图分辨率、运行深度和实现上限，确定嵌套楔形误差树需要构建到的深度
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
/// 计算从根节点到指定最细深度的完整二叉树需要多少个节点
/// </summary>
[[nodiscard]] inline std::size_t CompleteBinaryTreeNodeCount(int finestDepth)
{
    assert(finestDepth >= 0);
    assert(static_cast<unsigned>(finestDepth + 1) < std::numeric_limits<std::size_t>::digits);
    return (std::size_t{1} << static_cast<unsigned>(finestDepth + 1)) - 1U;
}

/// <summary>
/// 按 ROAM 论文公式 (1) 自底向上累积局部位移，得到每个节点覆盖子树的保守几何误差
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
        // 最细层直接对应输入采样，没有更细层需要包络，因此误差厚度为零
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
/// 分配完整误差树、填充全部节点，并返回根节点覆盖整棵树的误差厚度
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
} // 命名空间 ParallelRoam::Algorithms::Roam
