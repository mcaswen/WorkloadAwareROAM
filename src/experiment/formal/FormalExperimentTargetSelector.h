#pragma once

#include "algorithms/TerrainLodPassTrace.h"

#include <string>

namespace ParallelRoam::Experiment::Formal
{
inline constexpr std::uint32_t CpuTargetSelectorVersion = 1U;
inline constexpr std::uint32_t CpuTargetSelectionSeed = 20260830U;

/// <summary>
/// 将场景、阶段和采样身份与获准的原始工作量绑定
/// 不接收计时或策略执行结果，避免目标选择受到性能标签影响
/// </summary>
struct TargetSelectionCandidate
{
    std::string ScenarioId;
    Algorithms::TerrainLodPassId PassId{Algorithms::TerrainLodPassId::MergeScore};
    std::uint32_t SampleIndex{0U};
    float PrimaryWorkValue{0.0F};
    std::vector<float> AuxiliaryFeatures;
};

/// <summary>
/// 指定每组采用四点探索或八点覆盖配置
/// 两种配置共用固定版本的选择规则
/// </summary>
struct TargetSelectionConfig
{
    std::uint32_t TargetsPerPass{4U};
};

/// <summary>
/// 结果保存原始候选以便调用方绑定同一采样的相机和阶段身份
/// </summary>
struct SelectedTargetState
{
    TargetSelectionCandidate Candidate;
    std::string Stratum;
    std::uint32_t Rank{0U};
    std::uint64_t SelectionFeatureHash{0U};
};

/// <summary>
/// 整组选择返回唯一目标及不足原因以便独立写入覆盖记录
/// </summary>
struct TargetSelectionResult
{
    std::uint32_t RequestedCount{0U};
    std::size_t EligibleCount{0U};
    std::vector<SelectedTargetState> Selected;
    std::array<std::size_t, 4> StratumCounts{};
    std::string InsufficiencyReason;
};

/// <summary>
/// 对同一场景、同一阶段的有效候选执行确定性分层与覆盖补点
/// 重复身份或非法数值导致失败，不静默丢弃候选
/// </summary>
[[nodiscard]] TargetSelectionResult SelectTargetStates(
    const std::vector<TargetSelectionCandidate>& candidates, const TargetSelectionConfig& config = {});

/// <summary>
/// 编码选择器版本与原始 binary32 特征以支持独立重算
/// </summary>
[[nodiscard]] std::uint64_t HashTargetSelectionFeatures(const TargetSelectionCandidate& candidate);

/// <summary>
/// 用九位有效数字输出原始 binary32 特征，保证读取后数值不变
/// 不输出选择器内部使用的百分位归一化值
/// </summary>
[[nodiscard]] std::string FormatTargetSelectionFeatures(const std::vector<float>& features);
}
