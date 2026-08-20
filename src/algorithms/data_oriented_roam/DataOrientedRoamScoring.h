#pragma once

#include "algorithms/data_oriented_roam/DataOrientedRoamState.h"

namespace ParallelRoam::Algorithms::DataOrientedRoam
{
/// <summary>
/// DOD 的误差、hysteresis 和叶节点调试分类接口
/// 读取当前 Build 的 state；除 score cache 外不拥有也不改变拓扑，结果被 queue 和 emit pass 消费
/// </summary>
[[nodiscard]] bool ShouldSplitWithScore(
    const DataOrientedRoamState& state,
    DataOrientedRoamNodeIndex node,
    float screenErrorScore);

// WasSplitLastFrame 只读取 state 保存的跨帧 path 集合
[[nodiscard]] bool WasSplitLastFrame(
    const DataOrientedRoamState& state,
    DataOrientedRoamNodeIndex node);

// 调试分类只依赖当前节点生命周期标记，不改变活动叶集合
[[nodiscard]] DataOrientedRoamLeafDebugClass ClassifyLeafDebug(
    const DataOrientedRoamState& state,
    DataOrientedRoamNodeIndex node);
[[nodiscard]] glm::vec3 DebugColorForLeaf(
    const DataOrientedRoamState& state,
    DataOrientedRoamNodeIndex node);
[[nodiscard]] float DebugHighlightForLeaf(
    const DataOrientedRoamState& state,
    DataOrientedRoamNodeIndex node);

// score 同时服务 split queue 和 merge queue，单位由当前 screen-error 公式定义
[[nodiscard]] float ComputeScreenErrorScore(
    const DataOrientedRoamState& state,
    DataOrientedRoamNodeIndex node);
} // namespace ParallelRoam::Algorithms::DataOrientedRoam
