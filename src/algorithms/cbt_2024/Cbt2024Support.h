#pragma once

#include <string>

namespace ParallelRoam::Render
{
class IGraphicsBackend;
}

namespace ParallelRoam::Algorithms::Cbt2024
{
/// <summary>
/// CBT 运行要求与当前图形设备能力的匹配结果
/// </summary>
struct Cbt2024Availability
{
    bool Available{false};
    std::string UnavailableReason;
};

/// <summary>
/// 根据后端类型和通用设备能力返回 CBT 的可用状态及完整失败原因
/// </summary>
[[nodiscard]] Cbt2024Availability QueryCbt2024Availability(const Render::IGraphicsBackend& backend);
} // namespace ParallelRoam::Algorithms::Cbt2024
