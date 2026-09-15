#include "experiment/infrastructure/ExperimentRecords.h"

#include <stdexcept>

namespace ParallelRoam::Experiment::Infrastructure
{
ExperimentRecords::ExperimentRecords(const std::filesystem::path& directory) : _directory(directory)
{
    std::filesystem::create_directories(directory.parent_path());
    if (!std::filesystem::create_directory(directory))
        throw std::runtime_error("实验目录已经存在，拒绝覆盖");
    _records.open(directory / "evidence.csv", std::ios::binary);
    _records.exceptions(std::ios::badbit | std::ios::failbit);
    WriteExperimentCsvRow(_records, {"kind", "identity", "value"});
}
void ExperimentRecords::Write(const std::string& kind, const std::string& identity, const std::string& value)
{
    WriteExperimentCsvRow(_records, {kind, identity, value});
    // 诊断写出不进入帧计时；失败立即可见，避免生成表面完整的报告
    _records.flush();
}
}
