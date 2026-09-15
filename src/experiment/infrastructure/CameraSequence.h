#pragma once
#include <glm/glm.hpp>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace ParallelRoam::Experiment::Infrastructure
{
/// <summary>
/// 一个更新机会的冻结相机输入，矩阵直接来自C++构造
/// NO与ZO共享姿态，各自拥有投影身份；名义时间不代表耗时
/// </summary>
struct CameraFrame
{
    std::uint32_t Index{}, SourceIndex{};
    double NominalSeconds{};
    std::string Event{"move"};
    int Width{1280}, Height{720};
    float Fov{60}, Near{.1F}, Far{500};
    glm::vec3 Position{}, Forward{};
    glm::mat4 View{1}, ProjectionNo{1}, ProjectionZo{1};
    std::uint64_t PoseHash{}, NoHash{}, ZoHash{};
};
using CameraSequence = std::vector<CameraFrame>;

[[nodiscard]] CameraFrame MakeCameraFrame(const glm::vec3& position, const glm::vec3& target,
    std::uint32_t index, int width = 1280, int height = 720, float farPlane = 500);
void SealCameraFrame(CameraFrame& frame);
void ValidateCameraSequence(const CameraSequence& frames);
void SaveCameraSequence(const std::filesystem::path& path, const CameraSequence& frames);
[[nodiscard]] CameraSequence LoadCameraSequence(const std::filesystem::path& path);
}
