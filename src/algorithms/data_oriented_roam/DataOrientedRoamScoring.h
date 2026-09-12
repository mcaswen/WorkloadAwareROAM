#pragma once

#include "algorithms/data_oriented_roam/DataOrientedRoamState.h"

namespace ParallelRoam::Algorithms::DataOrientedRoam
{
/// <summary>
/// 提供 DOD 细分判断、屏幕误差计算和叶节点调试分类
/// 这些函数读取当前状态但不修改拓扑，结果供优先队列和网格提交阶段使用
/// </summary>
[[nodiscard]] bool ShouldSplitWithScore(
    const DataOrientedRoamState& state,
    DataOrientedRoamNodeIndex node,
    float screenErrorScore);

// 根据固定的路径编号判断节点上一帧是否处于细分状态
[[nodiscard]] bool WasSplitLastFrame(
    const DataOrientedRoamState& state,
    DataOrientedRoamNodeIndex node);

// 调试分类只读取节点的创建、激活和合并标记，不改变活动叶集合
[[nodiscard]] DataOrientedRoamLeafDebugClass ClassifyLeafDebug(
    const DataOrientedRoamState& state,
    DataOrientedRoamNodeIndex node);
[[nodiscard]] glm::vec3 DebugColorForLeaf(
    const DataOrientedRoamState& state,
    DataOrientedRoamNodeIndex node);
[[nodiscard]] float DebugHighlightForLeaf(
    const DataOrientedRoamState& state,
    DataOrientedRoamNodeIndex node);

// 屏幕误差分数同时供细分队列和合并队列使用，单位为像素
[[nodiscard]] float ComputeScreenErrorScore(
    const DataOrientedRoamState& state,
    DataOrientedRoamNodeIndex node);

// 虚拟节点以静态描述求值，不必先创建生产节点；两种入口保持相同浮点运算顺序
[[nodiscard]] float ComputeScreenErrorScore(
    const DataOrientedRoamState& state, const TriangleDomain& domain, float geometricError);

// 迟滞只依赖当前深度、评分和上一帧事件成员，覆盖视图无需伪造生产下标
[[nodiscard]] bool ShouldSplitWithScore(
    const DataOrientedRoamState& state, int depth, std::uint64_t path, float score);
} // 命名空间 ParallelRoam::Algorithms::DataOrientedRoam
