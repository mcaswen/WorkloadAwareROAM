#include "experiment/formal/FormalExperimentTargetSelector.h"

#include <algorithm>
#include <iostream>
#include <limits>
#include <set>
#include <stdexcept>

using namespace ParallelRoam::Experiment::Formal;
using ParallelRoam::Algorithms::TerrainLodPassId;

namespace
{
void Require(bool condition, const char* message)
{
    if (!condition) throw std::runtime_error{message};
}

std::vector<TargetSelectionCandidate> Candidates(std::size_t count, bool equal = true)
{
    std::vector<TargetSelectionCandidate> result;
    for (std::size_t index = 1U; index <= count; ++index)
        result.push_back({"fixture", TerrainLodPassId::MergeScore, static_cast<std::uint32_t>(index),
            equal ? 7.0F : static_cast<float>(index), {7.0F, 0.25F, 0.5F}});
    return result;
}

std::vector<std::uint32_t> Ids(const TargetSelectionResult& result)
{
    std::vector<std::uint32_t> values;
    for (const auto& selected : result.Selected)
        values.push_back(selected.Candidate.SampleIndex);
    return values;
}

template<class Action>
void Reject(Action action)
{
    bool threw = false;
    try { action(); } catch (const std::invalid_argument&) { threw = true; }
    Require(threw, "invalid selection input accepted");
}
}

int main()
{
    try
    {
        // 预期散列来自独立 little-endian 字节编码而非调用实现生成
        Require(HashTargetSelectionFeatures(Candidates(1U)[0]) == 12385574045780273463ULL, "binary32 hash fixture");
        Require(FormatTargetSelectionFeatures({7.0F, 0.25F, 0.5F}) == "7;0.25;0.5", "raw feature formatting");
        for (const auto requested : {4U, 8U})
        {
            for (const auto count : {0U, 1U, 2U, 3U, 4U, 7U, 8U, 12U, 63U})
            {
                auto candidates = Candidates(count);
                const auto result = SelectTargetStates(candidates, {requested});
                Require(result.Selected.size() == std::min(count, requested), "coverage count");
                Require(result.InsufficiencyReason.empty() == (count >= requested), "insufficiency reported");
                std::set<std::uint32_t> ids;
                for (std::size_t index = 0U; index < result.Selected.size(); ++index)
                {
                    const auto& item = result.Selected[index];
                    Require(ids.insert(item.Candidate.SampleIndex).second && item.Rank == index, "unique contiguous ranks");
                    Require(item.Candidate.AuxiliaryFeatures == std::vector<float>{7.0F, 0.25F, 0.5F}, "raw features retained");
                }
                std::reverse(candidates.begin(), candidates.end());
                Require(Ids(result) == Ids(SelectTargetStates(candidates, {requested})), "input order invariance");
            }
        }
        Require(Ids(SelectTargetStates(Candidates(12U), {4U})) == std::vector<std::uint32_t>{1U, 7U, 10U, 6U},
            "equal-distance four-point fixture");
        Require(Ids(SelectTargetStates(Candidates(12U), {8U})) == std::vector<std::uint32_t>{1U, 3U, 7U, 6U, 10U, 12U, 5U, 4U},
            "equal-distance eight-point fixture");
        auto coverage = Candidates(12U, false);
        // 首轮各层选择保持相同但极端辅助值使覆盖补点唯一指向第九点
        for (auto& item : coverage) item.AuxiliaryFeatures = {0.0F, 0.0F, 0.0F};
        coverage[8].AuxiliaryFeatures = {100.0F, 100.0F, 100.0F};
        Require(Ids(SelectTargetStates(coverage)) == std::vector<std::uint32_t>{1U, 7U, 10U, 9U}, "P5/P95 coverage distance");
        auto invalid = Candidates(2U);
        invalid[1].SampleIndex = invalid[0].SampleIndex;
        Reject([&] { (void)SelectTargetStates(invalid); });
        invalid = Candidates(1U);
        invalid[0].PrimaryWorkValue = std::numeric_limits<float>::quiet_NaN();
        Reject([&] { (void)SelectTargetStates(invalid); });
        invalid[0].PrimaryWorkValue = 1.0F;
        invalid[0].AuxiliaryFeatures[0] = std::numeric_limits<float>::infinity();
        Reject([&] { (void)SelectTargetStates(invalid); });
        Reject([] { (void)SelectTargetStates({}, {5U}); });
        std::cout << "Deterministic target selection and raw feature identity verified\n";
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
