#pragma once

#include "experiment/infrastructure/TerrainAssetCatalog.h"
#include "experiment/infrastructure/MaterialCatalog.h"
#include <span>
#include <string>

namespace ParallelRoam::Gui
{
/// <summary>
/// 面板只发出选择请求，不加载文件或持有GPU资源
/// 两种选择分别作用于场景重置与渲染观察状态
/// </summary>
struct ExperimentPanelCommand
{
    int Terrain{-1};
    int Material{-1};
    int Hold{4};
    bool Record{}, Remove{}, Export{}, Load{}, Play{}, Pause{}, Step{}, Restart{}, Stop{};
    std::string CameraPath;
};
class ExperimentPanel
{
public:
    [[nodiscard]] ExperimentPanelCommand Draw(
        std::span<const Experiment::Infrastructure::TerrainAsset> assets,
        std::span<const Experiment::Infrastructure::MaterialPreset> materials,
        const std::filesystem::path& current, const std::string& error,
        std::size_t keys, std::size_t cursor, std::size_t frameCount);
private:
    char _cameraPath[512]{"configs/experiments/cameras/frozen/test129-pq-return24.csv"};
    int _hold{4};
};
}
