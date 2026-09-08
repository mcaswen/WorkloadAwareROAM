#pragma once

#include "experiment/formal/FormalExperimentRecords.h"

#include <iosfwd>

namespace ParallelRoam::Experiment::Formal
{
void WriteInputPreparationSummary(std::ostream& output, const InputPreparationSummary& summary);
void WriteCpuDiscoveryCsvHeader(std::ostream& output);
void WriteCpuDiscoveryCsvRow(std::ostream& output, const CpuDiscoveryRecord& record);
void WriteCpuPairCsvHeader(std::ostream& output);
void WriteCpuPairCsvRow(std::ostream& output, const CpuPairRecord& record);
} // 命名空间 ParallelRoam::Experiment::Formal
