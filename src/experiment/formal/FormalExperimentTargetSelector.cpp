#include "experiment/formal/FormalExperimentTargetSelector.h"

#include <cmath>
#include <iomanip>
#include <limits>
#include <locale>
#include <set>
#include <sstream>
#include <stdexcept>
#include <tuple>

namespace ParallelRoam::Experiment::Formal
{
namespace
{
constexpr std::array<std::string_view, 4> Strata{"low", "middle", "high", "coverage"};

/// <summary>
/// 非整数百分位在相邻样本之间线性插值且输入必须已排序
/// </summary>
double Percentile(const std::vector<double>& sorted, double probability)
{
    const double position = probability * static_cast<double>(sorted.size() - 1U);
    const auto left = static_cast<std::size_t>(position);
    const auto right = std::min(left + 1U, sorted.size() - 1U);
    return sorted[left] + (sorted[right] - sorted[left]) * (position - static_cast<double>(left));
}

/// <summary>
/// 选择期间保留最初分层散列以稳定裁决后续覆盖补点
/// </summary>
struct RankedCandidate
{
    TargetSelectionCandidate Value;
    std::size_t Stratum{0U};
    std::uint64_t TieHash{0U};
    std::vector<double> Normalized;
    bool Selected{false};
};

bool TieBefore(const RankedCandidate& left, const RankedCandidate& right)
{
    return std::tie(left.TieHash, left.Value.SampleIndex) < std::tie(right.TieHash, right.Value.SampleIndex);
}
}

std::uint64_t HashTargetSelectionFeatures(const TargetSelectionCandidate& candidate)
{
    std::uint64_t hash = Algorithms::TerrainLodHashOffset;
    Algorithms::AppendTerrainLodHash(hash, CpuTargetSelectorVersion);
    Algorithms::AppendTerrainLodHash(hash, static_cast<std::uint32_t>(candidate.PassId));
    Algorithms::AppendTerrainLodHash(hash, std::bit_cast<std::uint32_t>(candidate.PrimaryWorkValue));
    for (const float feature : candidate.AuxiliaryFeatures)
        Algorithms::AppendTerrainLodHash(hash, std::bit_cast<std::uint32_t>(feature));
    return hash;
}

std::string FormatTargetSelectionFeatures(const std::vector<float>& features)
{
    std::ostringstream output;
    output.imbue(std::locale::classic());
    output << std::setprecision(std::numeric_limits<float>::max_digits10);
    for (std::size_t index = 0U; index < features.size(); ++index)
    {
        if (!std::isfinite(features[index]))
            throw std::invalid_argument{"Non-finite selection feature"};
        if (index != 0U)
            output << ';';
        output << features[index];
    }
    return output.str();
}

TargetSelectionResult SelectTargetStates(
    const std::vector<TargetSelectionCandidate>& candidates, const TargetSelectionConfig& config)
{
    if (config.TargetsPerPass != 4U && config.TargetsPerPass != 8U)
        throw std::invalid_argument{"Targets per pass must be 4 or 8"};
    TargetSelectionResult result;
    result.RequestedCount = config.TargetsPerPass;
    result.EligibleCount = candidates.size();
    if (candidates.empty())
    {
        result.InsufficiencyReason = "no_eligible_candidates";
        return result;
    }
    std::set<std::uint32_t> samples;
    std::vector<RankedCandidate> ranked;
    const auto& first = candidates.front();
    const std::size_t dimensions = first.PassId == Algorithms::TerrainLodPassId::MergeScore ||
        first.PassId == Algorithms::TerrainLodPassId::MeshEmit ? 3U : 4U;
    for (const auto& candidate : candidates)
    {
        if (candidate.ScenarioId.empty() || candidate.ScenarioId != first.ScenarioId ||
            candidate.PassId != first.PassId || candidate.PassId == Algorithms::TerrainLodPassId::CpuUpload ||
            static_cast<unsigned>(candidate.PassId) >= static_cast<unsigned>(Algorithms::TerrainLodPassId::Count) || candidate.SampleIndex == 0U ||
            candidate.SampleIndex >= 64U || !samples.insert(candidate.SampleIndex).second ||
            !std::isfinite(candidate.PrimaryWorkValue) || candidate.PrimaryWorkValue <= 0.0F ||
            candidate.AuxiliaryFeatures.size() != dimensions ||
            !std::all_of(candidate.AuxiliaryFeatures.begin(), candidate.AuxiliaryFeatures.end(),
                [](float value) { return std::isfinite(value) && value >= 0.0F; }))
            throw std::invalid_argument{"Invalid or duplicate target selection candidate"};
        ranked.push_back({candidate, 0U, 0U, std::vector<double>(dimensions), false});
    }
    // 排序身份不依赖输入 CSV 行序或任何耗时列
    std::sort(ranked.begin(), ranked.end(), [](const auto& left, const auto& right) {
        return std::tie(left.Value.PrimaryWorkValue, left.Value.SampleIndex) <
            std::tie(right.Value.PrimaryWorkValue, right.Value.SampleIndex);
    });
    for (std::size_t index = 0U; index < ranked.size(); ++index)
    {
        auto& item = ranked[index];
        item.Stratum = std::min(std::size_t{2U}, 3U * index / ranked.size());
        const std::string text = std::to_string(CpuTargetSelectionSeed) + "|" + item.Value.ScenarioId + "|" +
            std::string{Algorithms::ToString(item.Value.PassId)} + "|" + std::to_string(item.Value.SampleIndex) +
            "|" + std::string{Strata[item.Stratum]};
        item.TieHash = Algorithms::TerrainLodHashOffset;
        Algorithms::AppendTerrainLodHash(item.TieHash, std::string_view{text});
    }
    // 百分位使用全部有效候选且仅供距离计算
    for (std::size_t dimension = 0U; dimension < dimensions; ++dimension)
    {
        std::vector<double> values;
        for (const auto& item : ranked)
            values.push_back(item.Value.AuxiliaryFeatures[dimension]);
        std::sort(values.begin(), values.end());
        const double low = Percentile(values, 0.05);
        const double high = Percentile(values, 0.95);
        for (auto& item : ranked)
            item.Normalized[dimension] = high == low ? 0.0 :
                std::clamp((static_cast<double>(item.Value.AuxiliaryFeatures[dimension]) - low) / (high - low), 0.0, 1.0);
    }
    const auto select = [&result](RankedCandidate& item, std::size_t stratum) {
        item.Selected = true;
        result.Selected.push_back({item.Value, std::string{Strata[stratum]},
            static_cast<std::uint32_t>(result.Selected.size()), HashTargetSelectionFeatures(item.Value)});
        ++result.StratumCounts[stratum];
    };
    const auto quota = config.TargetsPerPass / 4U;
    for (std::size_t stratum = 0U; stratum < 3U; ++stratum)
    {
        std::vector<std::size_t> indices;
        for (std::size_t index = 0U; index < ranked.size(); ++index)
            if (ranked[index].Stratum == stratum)
                indices.push_back(index);
        std::sort(indices.begin(), indices.end(), [&](auto left, auto right) {
            return TieBefore(ranked[left], ranked[right]);
        });
        for (std::size_t index = 0U; index < std::min<std::size_t>(quota, indices.size()); ++index)
            select(ranked[indices[index]], stratum);
    }
    // 覆盖补点保留最初层的散列裁决以保证同距时仍可重复
    while (result.Selected.size() < config.TargetsPerPass)
    {
        RankedCandidate* best = nullptr;
        double bestDistance = -1.0;
        for (auto& item : ranked)
        {
            if (item.Selected)
                continue;
            double distance = std::numeric_limits<double>::infinity();
            for (const auto& selected : ranked)
            {
                if (!selected.Selected)
                    continue;
                double squared = 0.0;
                for (std::size_t dimension = 0U; dimension < dimensions; ++dimension)
                {
                    const double delta = item.Normalized[dimension] - selected.Normalized[dimension];
                    squared += delta * delta;
                }
                distance = std::min(distance, squared);
            }
            if (best == nullptr || distance > bestDistance || (distance == bestDistance && TieBefore(item, *best)))
            {
                best = &item;
                bestDistance = distance;
            }
        }
        if (best == nullptr)
            break;
        select(*best, 3U);
    }
    if (result.Selected.size() < config.TargetsPerPass)
        result.InsufficiencyReason = "insufficient_eligible_candidates";
    return result;
}
}
