#pragma once

#include "experiment/formal/FormalExperimentRecords.h"

#include <iosfwd>

namespace ParallelRoam::Experiment::Formal
{
void WriteInputPreparationSummary(std::ostream& output, const InputPreparationSummary& summary);
/// <summary>
/// 发现独立使用 v2 且有效行必须带已执行的诊断和阶段身份
/// </summary>
void WriteCpuDiscoveryCsvHeader(std::ostream& output);
void WriteCpuDiscoveryCsvRow(std::ostream& output, const CpuDiscoveryRecord& record);
/// <summary>
/// 覆盖与发现摘要记录真实不足数量并保持各自 v1 契约
/// </summary>
void WriteCpuTargetCoverageCsv(std::ostream& output, const std::vector<CpuTargetCoverageRecord>& records);
void WriteCpuDiscoverySummary(std::ostream& output, const CpuDiscoverySummary& summary);
void WriteCpuPairCsvHeader(std::ostream& output);
void WriteCpuPairCsvRow(std::ostream& output, const CpuPairRecord& record);
} // 命名空间 ParallelRoam::Experiment::Formal
