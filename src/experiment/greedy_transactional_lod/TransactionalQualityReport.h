#pragma once

#include "algorithms/greedy_transactional_lod/TransactionalSamples.h"
#include <filesystem>

namespace ParallelRoam::Experiment::GreedyTransactionalLod
{
using Algorithms::GreedyTransactionalLod::Slot;
using Algorithms::GreedyTransactionalLod::TransactionalSamples;
/// <summary>
/// 离线质量摘要保留绝对误差与见证，不将局部接受视为全局质量保证。
/// </summary>
struct TransactionalQualitySummary
{
    double Screen{},Height{},Rms{};
    Slot ScreenSample{},HeightSample{};
    std::size_t Visible{};
};

/// <summary>
/// 探针专用的公共 Q 评价输出，不链接进正常算法核心。
/// 逐点误差用于跨算法比较，文件写入必须在测量窗口之外。
/// </summary>
struct TransactionalQualityReport
{
    /// <summary>
    /// 归约绝对最大误差与可见参数域样本 RMS，同时保留极值样本身份。
    /// </summary>
    static TransactionalQualitySummary Summarize(const TransactionalSamples& samples);
    /// <summary>
    /// 按公共 Q 顺序输出摘要和逐点像素误差；参考不可见值以负数标记。
    /// </summary>
    static void Write(const TransactionalSamples& samples,const std::filesystem::path& directory);
};
}
