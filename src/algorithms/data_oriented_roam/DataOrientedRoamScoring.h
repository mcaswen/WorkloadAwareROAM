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
} // 命名空间 ParallelRoam::Algorithms::DataOrientedRoam
