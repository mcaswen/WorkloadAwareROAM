#pragma once

#include "experiment/formal/FormalExperimentTypes.h"

#include <iosfwd>

namespace ParallelRoam::Experiment::Formal
{
/// <summary>
/// 校验场景协议与实际尺寸，失败抛异常；资产相对路径基于 assetRoot，文件 SHA-256 由准备脚本核对
/// </summary>
[[nodiscard]] std::vector<FormalScenario> LoadScenarioManifest(
    const std::filesystem::path& path, const std::filesystem::path& assetRoot,
    const std::vector<std::string>& selectedIds = {});
/// <summary>
/// 要求每个给定场景完整的 0..63 冻结相机行，任一读取或校验错误均抛异常且不返回部分集合
/// </summary>
[[nodiscard]] std::vector<CameraSample> LoadCameraManifest(
    const std::filesystem::path& path, const std::vector<FormalScenario>& scenarios);
/// <summary>
/// 要求各场景完整相机集合并校验目标引用，失败抛异常；不验证真实 DOD 重放或选择特征身份
/// </summary>
[[nodiscard]] std::vector<TargetStateRef> LoadTargetManifest(
    const std::filesystem::path& path, const std::vector<FormalScenario>& scenarios,
    const std::vector<CameraSample>& cameras);
void WriteScenarioManifest(std::ostream& output, const std::vector<FormalScenario>& scenarios);
void WriteCameraManifest(std::ostream& output, const std::vector<CameraSample>& cameras);
void WriteTargetManifest(std::ostream& output, const std::vector<TargetStateRef>& targets);
} // 命名空间 ParallelRoam::Experiment::Formal
