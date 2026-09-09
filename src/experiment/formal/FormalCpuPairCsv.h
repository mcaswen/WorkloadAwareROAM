#pragma once

#include "experiment/formal/FormalCpuPairRecords.h"

#include <iosfwd>
#include <string_view>

namespace ParallelRoam::Experiment::Formal
{
/// <summary>
/// 配对 CSV 使用独立 v2，未知版本或缺少执行证据的有效行必须拒绝
/// </summary>
void WriteCpuPairCsvHeader(std::ostream& output);
void WriteCpuPairCsvRow(std::ostream& output, const CpuPairRecord& record);
[[nodiscard]] std::vector<CpuPairRecord> ReadCpuPairCsv(std::istream& input, std::string_view source);

/// <summary>
/// 核对整个目标的全部预热和计时块，不从失败目标中保留局部有效策略对
/// 调用方另行核对目标集合和来源帧，缺行、重复及身份不一致均抛出异常
/// </summary>
void ValidateCpuPairTarget(const std::vector<CpuPairRecord>& records, const TargetStateRef& target,
    const std::string& runId, const CpuPairConfiguration& configuration);
void ValidateCpuPairConfiguration(const CpuPairConfiguration& configuration);
[[nodiscard]] std::vector<Algorithms::TerrainLodPassAction> CpuPairActions(Algorithms::TerrainLodPassId pass);
void WriteCpuTargetPairSummaries(std::ostream& output, const std::vector<CpuTargetPairSummary>& summaries);
void WriteCpuPairSummary(std::ostream& output, const CpuPairSummary& summary);
void WriteCpuTimingCalibration(std::ostream& output, const std::vector<CpuTimingCalibrationRecord>& records);
}

