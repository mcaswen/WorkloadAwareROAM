#pragma once

#include "experiment/formal/FormalExperimentTypes.h"

#include <iosfwd>

namespace ParallelRoam::Experiment::Formal
{
/// <summary>
/// 加载并校验场景协议与高度图实际尺寸，资产相对路径以 assetRoot 为基准
/// 读取或校验失败时抛出异常；实际文件的 SHA-256 由准备脚本另行核对
/// </summary>
[[nodiscard]] std::vector<FormalScenario> LoadScenarioManifest(
    const std::filesystem::path& path, const std::filesystem::path& assetRoot,
    const std::vector<std::string>& selectedIds = {});
/// <summary>
/// 每个给定场景必须提供完整的 0..63 冻结相机行；读取或校验失败时抛出异常，不返回部分集合
/// </summary>
[[nodiscard]] std::vector<CameraSample> LoadCameraManifest(
    const std::filesystem::path& path, const std::vector<FormalScenario>& scenarios);
/// <summary>
/// 使用各场景完整的冻结相机集合校验目标引用，读取或校验失败时抛出异常
/// 这里只核对清单声明与引用关系，真实 DOD 重放输入及选择特征身份仍需重建验证
/// </summary>
[[nodiscard]] std::vector<TargetStateRef> LoadTargetManifest(
    const std::filesystem::path& path, const std::vector<FormalScenario>& scenarios,
    const std::vector<CameraSample>& cameras);
void WriteScenarioManifest(std::ostream& output, const std::vector<FormalScenario>& scenarios);
void WriteCameraManifest(std::ostream& output, const std::vector<CameraSample>& cameras);
void WriteTargetManifest(std::ostream& output, const std::vector<TargetStateRef>& targets);
} // 命名空间 ParallelRoam::Experiment::Formal
