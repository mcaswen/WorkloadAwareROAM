#pragma once

#include "experiment/formal/FormalExperimentRecords.h"

#include <iosfwd>

namespace ParallelRoam::Experiment::Formal
{
void WriteInputPreparationSummary(std::ostream& output, const InputPreparationSummary& summary);
/// <summary>
/// 发现记录独立使用 v2 格式；有效行必须带有已执行的诊断结果和真实阶段输入身份
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
