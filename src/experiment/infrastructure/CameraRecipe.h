#pragma once
#include "experiment/infrastructure/CameraSequence.h"
#include "terrain/HeightMap.h"

namespace ParallelRoam::Experiment::Infrastructure
{
/// <summary>
/// 自由录制的一处姿态及停留机会数，坐标保留世界单位
/// 配方与展开的冻结序列分别保存，便于追查插值来源
/// </summary>
struct CameraKeyframe
{
    glm::vec3 Position{}, Forward{};
    std::uint32_t Hold{4};
};
[[nodiscard]] CameraSequence GenerateCameraRecipe(const std::filesystem::path& recipe,
    const Terrain::HeightMap& reference, float size, float heightScale);
[[nodiscard]] CameraSequence ExpandCameraKeys(const std::vector<CameraKeyframe>& keys);
void SaveCameraKeys(const std::filesystem::path& path, const std::vector<CameraKeyframe>& keys);
void CheckCameraClearance(const CameraSequence& frames, const Terrain::HeightMap& reference,
    float size, float heightScale);
}
