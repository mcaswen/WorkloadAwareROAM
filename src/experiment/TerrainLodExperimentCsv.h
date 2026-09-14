#pragma once

#include "algorithms/ITerrainLodAlgorithm.h"

#include <iosfwd>
#include <string_view>

namespace ParallelRoam::Experiment
{
inline constexpr std::string_view TerrainLodExperimentCsvSchemaVersion{"5"};

// 运行时和无窗口报告共用设置列，调用方应传入已经应用预设与覆盖项的最终配置
// 写入器只输出本组字段，组间逗号和整行换行由调用方负责
void WriteTerrainLodSettingsCsvHeader(std::ostream& output);
void WriteTerrainLodSettingsCsvValues(
    std::ostream& output,
    const Algorithms::TerrainLodSettings& settings);

// 两类报告共用统计与阶段记录的列顺序，避免同一字段在不同入口中含义不同
void WriteTerrainLodStatsCsvHeader(std::ostream& output);
void WriteTerrainLodStatsCsvValues(
    std::ostream& output,
    const Algorithms::TerrainLodStats& stats);
} // 命名空间 ParallelRoam::Experiment
