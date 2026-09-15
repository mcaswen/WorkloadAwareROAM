#pragma once

#include "experiment/ExperimentCsvCodec.h"

#include <filesystem>
#include <fstream>
#include <string>

namespace ParallelRoam::Experiment::Infrastructure
{
/// <summary>
/// 单个实验独占输出目录，并按调用顺序保存具名证据行
/// 不覆盖历史运行；析构关闭文件，不执行统计或归档删除
/// </summary>
class ExperimentRecords
{
public:
    explicit ExperimentRecords(const std::filesystem::path& directory);
    void Write(const std::string& kind, const std::string& identity, const std::string& value);
    [[nodiscard]] const std::filesystem::path& Directory() const { return _directory; }

private:
    std::filesystem::path _directory;
    std::ofstream _records;
};
}
