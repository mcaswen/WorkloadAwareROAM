#pragma once

#include "algorithms/TerrainLodPassTrace.h"

#include <string>

namespace ParallelRoam::Experiment::Formal
{
inline constexpr std::uint32_t CpuTargetSelectorVersion = 1U;
inline constexpr std::uint32_t CpuTargetSelectionSeed = 20260830U;

/// <summary>
/// 选择输入只含获准的原始工作量且不接受计时和执行结果
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
/// 明确区分四点探索配置与八点覆盖配置且两者均使用固定版本规则
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
/// 对同一场景阶段的有效候选确定性分层补点且重复身份或非法数值直接失败
/// </summary>
[[nodiscard]] TargetSelectionResult SelectTargetStates(
    const std::vector<TargetSelectionCandidate>& candidates, const TargetSelectionConfig& config = {});

/// <summary>
/// 编码选择器版本与原始 binary32 特征以支持独立重算
/// </summary>
[[nodiscard]] std::uint64_t HashTargetSelectionFeatures(const TargetSelectionCandidate& candidate);

/// <summary>
/// 九位有效数字足以使原始 binary32 向量往返且不输出百分位归一化值
/// </summary>
[[nodiscard]] std::string FormatTargetSelectionFeatures(const std::vector<float>& features);
}
