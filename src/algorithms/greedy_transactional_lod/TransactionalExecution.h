#pragma once

#include "algorithms/greedy_transactional_lod/TransactionalTypes.h"
#include <functional>

namespace ParallelRoam::Algorithms::GreedyTransactionalLod
{
/// <summary>
/// 认证工作次序的内部消融模式，不改变提案或质量政策
/// </summary>
enum class HeightRejectionMode { Reference, HeightFirst, SampleHint };

/// <summary>
/// 同步分块执行边界；回调返回或抛出前必须排空所有已派发任务
/// 调用方预先划定独占输出，执行器不负责推断业务数据依赖
/// </summary>
struct TransactionalExecution
{
    std::size_t Workers{1};
    bool Diagnostics{};
    std::function<void(std::size_t,const std::function<void(std::size_t)>&)> Dispatch;
    HeightRejectionMode HeightRejection{HeightRejectionMode::SampleHint};
    bool AuditHeightRejections{};
    // 诊断回调只在屏障后按根序调用，正常平台不安装观察器
    std::function<void(Identity, char, std::size_t, Slot, bool, const std::string&)> ObserveHeightFailure;
    /// <summary>
    /// 局部账本在 barrier 后归并，任何失败必须发生在 live 发布之前
    /// 阶段墙钟包括派发和归并；局部计时另记，不冒充 CPU time
    /// </summary>
    void Run(const std::string& phase,std::size_t count,WorkLedger& work,
        const std::function<void(std::size_t,std::size_t,WorkLedger&)>& task) const;

    /// <summary>
    /// 独立项由空闲任务动态领取，调用方必须保证每项只写自己的输出
    /// 仍借用原同步派发与局部账本；串行路径允许一次处理完整区间
    /// </summary>
    void RunIndependent(const std::string& phase, std::size_t count, WorkLedger& work,
        const std::function<void(std::size_t, std::size_t, WorkLedger&)>& task) const;
};
}
