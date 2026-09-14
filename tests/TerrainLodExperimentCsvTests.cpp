#include "experiment/TerrainLodExperimentCsv.h"

#include <iostream>
#include <sstream>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace
{
std::vector<std::string> SplitCsv(std::string_view text)
{
    std::vector<std::string> fields;
    std::size_t begin = 0U;
    while (begin <= text.size())
    {
        const std::size_t end = text.find(',', begin);
        fields.emplace_back(text.substr(begin, end == std::string_view::npos ? end : end - begin));
        if (end == std::string_view::npos)
        {
            break;
        }
        begin = end + 1U;
    }
    return fields;
}

bool CheckSerializedFields(
    const std::vector<std::string>& header,
    const std::vector<std::string>& values,
    const std::unordered_map<std::string, std::string>& expected)
{
    if (header.size() != values.size())
    {
        std::cerr << "CSV header/value field count mismatch: "
                  << header.size() << " != " << values.size() << '\n';
        return false;
    }

    std::unordered_map<std::string, std::string> row;
    for (std::size_t index = 0U; index < header.size(); ++index)
    {
        if (!row.emplace(header[index], values[index]).second)
        {
            std::cerr << "Duplicate CSV field: " << header[index] << '\n';
            return false;
        }
    }

    for (const auto& [name, value] : expected)
    {
        const auto found = row.find(name);
        if (found == row.end() || found->second != value)
        {
            std::cerr << "Unexpected CSV value for " << name << '\n';
            return false;
        }
    }
    return true;
}
} // 匿名命名空间

int main()
{
    ParallelRoam::Algorithms::TerrainLodSettings settings{};
    settings.PassPolicy.MergeScoreMinParallelEntryCount = 0U;
    settings.PassPolicy.SplitScoreMinParallelEntryCount = 255U;
    settings.PassPolicy.MeshEmitMinParallelTriangleCount = 257U;
    settings.PassPolicy.SplitTopologyMinParallelCandidateCount = 7U;
    settings.PassPolicy.MergeTopologyMinParallelCandidateCount = 13U;
    settings.PassPolicy.ParallelTopologyTargetBuild = 5U;
    settings.PassPolicy.ParallelTopologyPhase =
        ParallelRoam::Algorithms::TerrainLodParallelTopologyPhase::MergeOnly;
    settings.EnableTopologyPairEvidence = true;

    std::ostringstream settingsHeader;
    std::ostringstream settingsValues;
    ParallelRoam::Experiment::WriteTerrainLodSettingsCsvHeader(settingsHeader);
    ParallelRoam::Experiment::WriteTerrainLodSettingsCsvValues(settingsValues, settings);
    if (!CheckSerializedFields(
            SplitCsv(settingsHeader.str()),
            SplitCsv(settingsValues.str()),
            {
                {"experimentSchemaVersion", "5"},
                {"mergeScoreMinParallelEntryCount", "0"},
                {"splitScoreMinParallelEntryCount", "255"},
                {"meshEmitMinParallelTriangleCount", "257"},
                {"topologyPairEvidenceEnabled", "true"},
                {"splitTopologyMinParallelCandidateCount", "7"},
                {"mergeTopologyMinParallelCandidateCount", "13"},
                {"parallelTopologyTargetBuild", "5"},
                {"parallelTopologyPhase", "merge"},
            }))
    {
        return 1;
    }

    ParallelRoam::Algorithms::TerrainLodStats stats{};
    stats.InteriorSplitCandidateCount = 17U;
    stats.BoundarySplitCandidateCount = 19U;
    stats.InteriorMergeCandidateCount = 23U;
    stats.BoundaryMergeCandidateCount = 29U;
    stats.PassTraces[0].CandidateCount = 31U;
    stats.ResultValidationEvaluated = true;
    stats.ResultValidationPassed = true;
    stats.ReferenceComparisonEvaluated = true;
    stats.ReferenceComparisonPassed = true;
    stats.SplitTopologyPair.Evaluated = true;
    stats.SplitTopologyPair.Equivalent = true;
    stats.SplitTopologyPair.FrozenCandidateHash = 37U;
    stats.SplitTopologyPair.FrozenCandidateCount = 41U;
    stats.SplitTopologyPair.Serial.EffectiveWorkerCount = 1U;
    stats.SplitTopologyPair.Parallel.EffectiveWorkerCount = 4U;

    std::ostringstream statsHeader;
    std::ostringstream statsValues;
    ParallelRoam::Experiment::WriteTerrainLodStatsCsvHeader(statsHeader);
    ParallelRoam::Experiment::WriteTerrainLodStatsCsvValues(statsValues, stats);
    if (!CheckSerializedFields(
            SplitCsv(statsHeader.str()),
            SplitCsv(statsValues.str()),
            {
                {"interiorSplitCandidateCount", "17"},
                {"boundarySplitCandidateCount", "19"},
                {"interiorMergeCandidateCount", "23"},
                {"boundaryMergeCandidateCount", "29"},
                {"pass_mergeScoreCandidateCount", "31"},
                {"resultValidationEvaluated", "1"},
                {"resultValidationPassed", "1"},
                {"referenceComparisonEvaluated", "1"},
                {"referenceComparisonPassed", "1"},
                {"splitTopologyPairEvaluated", "true"},
                {"splitTopologyPairEquivalent", "true"},
                {"splitTopologyPairFrozenCandidateHash", "37"},
                {"splitTopologyPairFrozenCandidateCount", "41"},
                {"splitTopologyPairSerialEffectiveWorkerCount", "1"},
                {"splitTopologyPairParallelEffectiveWorkerCount", "4"},
            }))
    {
        return 1;
    }

    stats.Transactional.emplace();
    stats.Transactional->Exchanges = 7;
    stats.Transactional->ViewMilliseconds = 2.5;
    std::ostringstream transactionalValues;
    ParallelRoam::Experiment::WriteTerrainLodStatsCsvValues(transactionalValues, stats);
    if (!CheckSerializedFields(SplitCsv(statsHeader.str()), SplitCsv(transactionalValues.str()),
        {{"stageModel", "transactional"}, {"transactionalExchanges", "7"},
         {"transactionalViewMilliseconds", "2.5"}, {"cpuWorkerCount", "n/a"},
         {"pass_mergeScoreCandidateCount", "n/a"}})) return 1;

    return 0;
}
