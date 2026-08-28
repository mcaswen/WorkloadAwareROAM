#pragma once

#include "algorithms/ITerrainLodAlgorithm.h"

#include <iosfwd>
#include <string_view>

namespace ParallelRoam::Experiment
{
inline constexpr std::string_view TerrainLodExperimentCsvSchemaVersion{"2"};

// 两种实验入口共用同一组设置字段，字段顺序由实现文件唯一维护
void WriteTerrainLodSettingsCsvHeader(std::ostream& output);
void WriteTerrainLodSettingsCsvValues(
    std::ostream& output,
    const Algorithms::TerrainLodSettings& settings);

// 所有算法统计和阶段记录都从这里写入，避免运行时与无窗口报告各自遗漏字段
void WriteTerrainLodStatsCsvHeader(std::ostream& output);
void WriteTerrainLodStatsCsvValues(
    std::ostream& output,
    const Algorithms::TerrainLodStats& stats);
} // 命名空间 ParallelRoam::Experiment
