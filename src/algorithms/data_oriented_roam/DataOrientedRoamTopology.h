#pragma once

#include "algorithms/data_oriented_roam/DataOrientedRoamState.h"

namespace ParallelRoam::Algorithms::DataOrientedRoam
{
/// <summary>
/// DOD 每帧合并和细分拓扑的入口
/// 函数会修改活动索引、邻接关系、预算和跨帧保留的队列，调用期间不得从其他入口访问同一状态
/// 多个线程先处理能够独立修改的候选，随后由主线程继续处理剩余候选和需要连锁细分相邻三角形的情况
/// </summary>
// 先合并低误差菱形回收预算，再按分数从高到低细分节点
void RefineWithSplitQueue(DataOrientedRoamState& state);
void MergeWithDiamondQueue(DataOrientedRoamState& state);

/// <summary>
/// 消费已评分的长期队列完成拓扑修改且不重复执行整批评分
/// </summary>
void CommitScoredSplitTopology(DataOrientedRoamState& state);
void CommitScoredMergeTopology(DataOrientedRoamState& state);

/// <summary>
/// 在调用方提供的状态副本上执行一种细分拓扑策略并返回规范化结果
/// 串行方式直接处理长期队列，并行辅助方式使用调用方提供的冻结候选
/// </summary>
[[nodiscard]] TerrainLodTopologyReplayEvidence ReplayFrozenSplitTopologyAction(
    DataOrientedRoamState& state,
    const std::vector<DataOrientedRoamSplitCandidate>& candidates,
    bool parallel,
    float cloneMilliseconds);

/// <summary>
/// 在调用方提供的状态副本上执行一种合并拓扑策略并返回规范化结果
/// 串行方式直接处理长期队列，并行辅助方式使用调用方提供的冻结候选
/// </summary>
[[nodiscard]] TerrainLodTopologyReplayEvidence ReplayFrozenMergeTopologyAction(
    DataOrientedRoamState& state,
    const std::vector<DataOrientedRoamMergeCandidate>& candidates,
    bool parallel,
    float cloneMilliseconds);

/// <summary>
/// 为冻结拓扑输入生成与候选顺序无关的稳定编号
/// </summary>
[[nodiscard]] std::uint64_t HashFrozenSplitTopologyInput(
    const DataOrientedRoamState& state,
    const std::vector<DataOrientedRoamSplitCandidate>& candidates);
[[nodiscard]] std::uint64_t HashFrozenMergeTopologyInput(
    const DataOrientedRoamState& state,
    const std::vector<DataOrientedRoamMergeCandidate>& candidates);

/// <summary>
/// 让实验构造状态沿正式串行路径推进，同时保留同一帧已经记录的网格修改
/// </summary>
void AdvanceMergeTopologySerialForExperiment(DataOrientedRoamState& state);
void AdvanceSplitTopologySerialForExperiment(DataOrientedRoamState& state);
} // 命名空间 ParallelRoam::Algorithms::DataOrientedRoam
