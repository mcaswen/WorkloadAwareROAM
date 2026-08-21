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
} // 命名空间 ParallelRoam::Algorithms::DataOrientedRoam
