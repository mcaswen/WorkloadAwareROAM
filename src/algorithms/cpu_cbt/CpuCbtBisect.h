#pragma once

#include "algorithms/cbt_2024/CbtBisectCommit.h"

#include <cstddef>
#include <functional>

namespace ParallelRoam::Algorithms::CpuCbt
{
// 区间互不重叠且恰好覆盖全部模板；执行器返回前必须等待所有借用结束
using CpuCbtRangeTask = std::function<void(std::size_t begin, std::size_t end)>;
using CpuCbtRangeExecutor = std::function<void(std::size_t count, const CpuCbtRangeTask& task)>;

/// <summary>
/// 新细分内核的墙钟分段，单位为毫秒；填写包含派发和等待，空批次填写为零
/// </summary>
struct CpuCbtBisectTimings
{
    double PrepareMs{0.0}, TemplateFillWallMs{0.0}, CollectMs{0.0}, PropagationMs{0.0};
};

/// <summary>
/// 返回临时下一代及其生成成本，只有有效结果才能进入后续合并和发布
/// </summary>
struct CpuCbtBisectResult
{
    Cbt2024::CbtBisectCommitResult Topology;
    CpuCbtBisectTimings Timings;
};

/// <summary>
/// 先验证模板独占槽位，再从冻结输入填写新代，最后顺序传播外部邻接
/// 空执行器使用调用线程；执行器抛异常前也必须结束所有任务对临时数组的借用
/// </summary>
[[nodiscard]] CpuCbtBisectResult CommitCpuCbtBisects(
    const std::vector<std::uint64_t>& heapIds,
    const std::vector<Cbt2024::CbtBisectorNeighbors>& neighbors,
    const std::vector<Cbt2024::CbtBisectorData>& data,
    const std::vector<std::uint32_t>& allocationNodes,
    std::uint32_t dynamicElementCount, const CpuCbtRangeExecutor& executor = {});
} // 命名空间 ParallelRoam::Algorithms::CpuCbt
