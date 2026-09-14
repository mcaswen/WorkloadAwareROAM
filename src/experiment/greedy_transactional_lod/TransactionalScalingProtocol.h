#pragma once

#include "algorithms/greedy_transactional_lod/TransactionalTypes.h"
#include "experiment/formal/FormalExperimentTypes.h"

#include <optional>
#include <string_view>

namespace ParallelRoam::Experiment::GreedyTransactionalLod
{
using Algorithms::GreedyTransactionalLod::Configuration;
/// <summary>
/// 固定压力输入、额度映射和相机规则，供实验入口共同使用。
/// 不持有算法状态，也不参与每轮候选发现。
/// </summary>
struct TransactionalScalingProtocol
{
    static constexpr std::array<std::size_t,4> Budgets{20000,50000,100000,200000};
    static constexpr std::array<std::uint32_t,8> ViewOrder{14,14,14,15,16,17,18,14};
    /// <summary>
    /// 仅识别四个完整场景身份；旧场景和近似名称均不命中。
    /// </summary>
    static std::optional<std::size_t> Budget(std::string_view scenario);
    /// <summary>
    /// 构造公共 DOD 设置，保留默认阶段动作，仅冻结输入及线程请求。
    /// </summary>
    static Formal::FormalScenario Scenario(const std::filesystem::path& root,std::size_t budget,std::size_t workers);
    /// <summary>
    /// 沿用已有 float 压力轨迹，返回指定索引的姿态。
    /// </summary>
    static Formal::CameraSample Camera(std::uint32_t index);
    /// <summary>
    /// 复制输入配置并更新视图，保持初始化时选定的额度。
    /// </summary>
    static Configuration View(const Configuration& initial,std::uint32_t index);
    /// <summary>
    /// 核对来源参数与实际矩阵，拒绝混用旧轨迹或预算标签。
    /// </summary>
    static void Validate(const Configuration& config);
    /// <summary>
    /// 在已有有效配置上选择固定或同比增长额度，不改变质量规则。
    /// </summary>
    static void ConfigureLimits(Configuration& config,std::string_view policy);
};
}
