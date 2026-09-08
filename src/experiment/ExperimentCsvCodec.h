#pragma once

#include <cstdint>
#include <iosfwd>
#include <string>
#include <string_view>
#include <vector>

namespace ParallelRoam::Experiment
{
using ExperimentCsvRow = std::vector<std::string>;

/// <summary>
/// 保存完成词法和行宽检查的 CSV 内容，字段语义由各清单解释
/// </summary>
struct ExperimentCsvTable
{
    ExperimentCsvRow Header;
    std::vector<ExperimentCsvRow> Rows;
};

[[nodiscard]] ExperimentCsvTable ReadExperimentCsv(std::istream& input, std::string_view source);
void WriteExperimentCsvRow(std::ostream& output, const ExperimentCsvRow& row);
void RequireExperimentCsvHeader(const ExperimentCsvTable& table, const ExperimentCsvRow& header);
[[nodiscard]] std::uint64_t ParseExperimentCsvUnsigned(std::string_view value);
[[nodiscard]] float ParseExperimentCsvFloat(std::string_view value);
[[nodiscard]] std::string FormatExperimentCsvFloat(float value);
[[nodiscard]] std::string FormatExperimentCsvDouble(double value);
} // 命名空间 ParallelRoam::Experiment
