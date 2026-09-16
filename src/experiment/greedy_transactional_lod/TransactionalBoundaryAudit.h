#pragma once

#include "algorithms/greedy_transactional_lod/TransactionalSamples.h"
#include <filesystem>
#include <span>

namespace ParallelRoam::Experiment::GreedyTransactionalLod
{
/// <summary>
/// 在冻结状态上调查单侧外边界中点，不取得生产提交权限
/// 净增加一个面的预算与续接义务留给明确的生产事务契约
/// </summary>
class TransactionalBoundaryAudit
{
public:
    /// <summary>
    /// 只分割单位参数域的单侧边，初始新高保持原边上线性插值
    /// 构造失败写入原因；形状和动态质量由调用者分别核查
    /// </summary>
    static Algorithms::GreedyTransactionalLod::Proposal Construct(
        const Algorithms::GreedyTransactionalLod::TransactionalState& state,
        Algorithms::GreedyTransactionalLod::Slot root,
        Algorithms::GreedyTransactionalLod::Edge edge);

    /// <summary>
    /// 比较线性对照和双线性源高，输出原认证、有限预算配对与工作量
    /// 见证只定位调查域，不改变实际前缀、批次或下一轮状态
    /// </summary>
    static void Run(const Algorithms::GreedyTransactionalLod::TransactionalState& state,
        const Algorithms::GreedyTransactionalLod::TransactionalSamples& samples,
        std::size_t frame, std::span<const Algorithms::GreedyTransactionalLod::Point> witnesses,
        const std::filesystem::path& output);
};
}
