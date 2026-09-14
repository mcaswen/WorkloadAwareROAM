#pragma once

#include "algorithms/greedy_transactional_lod/TransactionalTypes.h"
#include <functional>

namespace ParallelRoam::Algorithms::GreedyTransactionalLod
{
/// <summary>
/// 同步分块执行边界；回调返回或抛出前必须排空所有已派发任务
/// 调用方预先划定独占输出，执行器不负责推断业务数据依赖
/// </summary>
struct TransactionalExecution
{
    std::size_t Workers{1};
    bool Diagnostics{};
    std::function<void(std::size_t,const std::function<void(std::size_t)>&)> Dispatch;
    /// <summary>
    /// 局部账本在 barrier 后归并，任何失败必须发生在 live 发布之前
    /// 阶段墙钟包括派发和归并；局部计时另记，不冒充 CPU time
    /// </summary>
    void Run(const std::string& phase,std::size_t count,WorkLedger& work,
        const std::function<void(std::size_t,std::size_t,WorkLedger&)>& task) const;
};
}
