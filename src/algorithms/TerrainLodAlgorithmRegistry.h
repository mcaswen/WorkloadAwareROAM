#pragma once

#include "algorithms/ITerrainLodAlgorithm.h"
#include <array>
#include <memory>
#include <optional>

namespace ParallelRoam::Render
{
class IGraphicsBackend;
}

namespace ParallelRoam::Algorithms
{
/// <summary>
/// 公共算法的稳定名称表；解析和界面查询不创建算法，也不持有运行状态
/// 工厂单独编译，避免命令行测试链接全部拓扑实现
/// </summary>
inline constexpr std::array<TerrainLodAlgorithmInfo, 4> TerrainLodAlgorithms{{
    {TerrainLodAlgorithmId::ClassicCpuRoam, "classic", "Classic CPU ROAM", "Classic incremental ROAM"},
    {TerrainLodAlgorithmId::DataOrientedCpuRoam, "dod", "Data-Oriented CPU ROAM", "Data-oriented incremental ROAM"},
    {TerrainLodAlgorithmId::TransactionalCpuLod, "transactional", "Transactional CPU LOD", "Experimental staged transactions"},
    {TerrainLodAlgorithmId::Cbt2024, "cbt", "CBT 2024", "Imported GPU reference"},
}};

[[nodiscard]] inline bool IsTerrainLodAlgorithmAvailable(TerrainLodAlgorithmId id)
{
    if (id == TerrainLodAlgorithmId::ClassicCpuRoam || id == TerrainLodAlgorithmId::DataOrientedCpuRoam)
    {
        return true;
    }
#if defined(PARALLEL_ROAM_TRANSACTIONAL_LOD_RUNTIME)
    if (id == TerrainLodAlgorithmId::TransactionalCpuLod)
    {
        return true;
    }
#endif
#if defined(PARALLEL_ROAM_CBT_2024_RUNTIME)
    if (id == TerrainLodAlgorithmId::Cbt2024)
    {
        return true;
    }
#endif
    return false;
}

[[nodiscard]] inline std::optional<TerrainLodAlgorithmId> ParseTerrainLodAlgorithm(std::string_view name)
{
    for (const auto& entry : TerrainLodAlgorithms)
    {
        if (entry.Name == name && IsTerrainLodAlgorithmAvailable(entry.Id))
        {
            return entry.Id;
        }
    }
    return std::nullopt;
}

[[nodiscard]] inline std::string_view TerrainLodAlgorithmDisplayName(TerrainLodAlgorithmId id)
{
    for (const auto& entry : TerrainLodAlgorithms)
    {
        if (entry.Id == id)
        {
            return entry.DisplayName;
        }
    }
    return "Unavailable";
}

/// <summary>
/// 工厂可选的设备上下文，CPU 算法创建不要求图形设备
/// </summary>
struct TerrainLodCreationContext
{
    // 只借用设备边界；算法实例必须先于后端销毁
    Render::IGraphicsBackend* GraphicsBackend{nullptr};
};

[[nodiscard]] std::unique_ptr<ITerrainLodAlgorithm> CreateTerrainLodAlgorithm(
    TerrainLodAlgorithmId id,
    const TerrainLodCreationContext& context = {});
}
