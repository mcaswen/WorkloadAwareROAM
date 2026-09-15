#pragma once

#include <cstdint>
#include <filesystem>
#include <string>

namespace ParallelRoam::Experiment::Infrastructure
{
/// <summary>
/// 保存已解析的单次实验输入，不创建算法或图形资源
/// 资产内容由离线清单核对；本类型继续校验运行字段与实际文件
/// </summary>
struct ExperimentCase
{
    std::string Id;
    std::string TerrainId;
    std::filesystem::path HeightMapPath;
    std::string FileSha256;
    std::string SampleSha256;
    std::string Algorithm;
    std::string HeightPolicy;
    bool FlipRecovery{};
    std::string Camera;
    std::string Material;
    std::string Mode;
    std::string Backend{"opengl"};
    std::string Prefix;
    std::uint32_t Width{};
    std::uint32_t Height{};
    std::uint32_t Budget{};
    std::uint32_t Workers{};
    std::uint32_t MaxDepth{};
    std::uint32_t ViewWidth{};
    std::uint32_t ViewHeight{};
    float TerrainSize{};
    float HeightScale{};
    float SplitPixels{};
    float MergePixels{};
    std::filesystem::path CameraFile, MaterialFile;
    std::uint64_t SampleFnv64{}, CameraFnv64{};
    float MaterialTiling{12}, MaterialTint{.35F};
    std::uint32_t CaptureStride{4}, Warmup{3}, MaxFrames{10000};

    static ExperimentCase LoadResolved(const std::filesystem::path& path);
};
}
