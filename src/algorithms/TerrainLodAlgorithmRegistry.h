#pragma once

#include "algorithms/ITerrainLodAlgorithm.h"
#include <array>
#include <memory>
#include <optional>

namespace ParallelRoam::Algorithms
{
/// <summary>
/// 公共算法的稳定名称表；解析和界面查询不创建算法，也不持有运行状态
/// 工厂单独编译，避免命令行测试链接全部拓扑实现
/// </summary>
inline constexpr std::array<TerrainLodAlgorithmInfo, 3> TerrainLodAlgorithms{{
    {TerrainLodAlgorithmId::ClassicCpuRoam, "classic", "Classic CPU ROAM", "Classic incremental ROAM"},
    {TerrainLodAlgorithmId::DataOrientedCpuRoam, "dod", "Data-Oriented CPU ROAM", "Data-oriented incremental ROAM"},
    {TerrainLodAlgorithmId::TransactionalCpuLod, "transactional", "Transactional CPU LOD", "Experimental staged transactions"},
}};

[[nodiscard]] inline bool IsTerrainLodAlgorithmAvailable(TerrainLodAlgorithmId id)
{
    if (id == TerrainLodAlgorithmId::ClassicCpuRoam || id == TerrainLodAlgorithmId::DataOrientedCpuRoam)
        return true;
#if defined(PARALLEL_ROAM_TRANSACTIONAL_LOD_RUNTIME)
    return id == TerrainLodAlgorithmId::TransactionalCpuLod;
#else
    return false;
#endif
}

[[nodiscard]] inline std::optional<TerrainLodAlgorithmId> ParseTerrainLodAlgorithm(std::string_view name)
{
    for (const auto& entry : TerrainLodAlgorithms)
        if (entry.Name == name && IsTerrainLodAlgorithmAvailable(entry.Id)) return entry.Id;
    return std::nullopt;
}

[[nodiscard]] inline std::string_view TerrainLodAlgorithmDisplayName(TerrainLodAlgorithmId id)
{
    for (const auto& entry : TerrainLodAlgorithms) if (entry.Id == id) return entry.DisplayName;
    return "Unavailable";
}

/// <summary>
/// 创建独立算法实例；未编译的实现明确返回空，不回退到其他算法
/// </summary>
[[nodiscard]] std::unique_ptr<ITerrainLodAlgorithm> CreateTerrainLodAlgorithm(TerrainLodAlgorithmId id);
}
