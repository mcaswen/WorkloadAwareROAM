#pragma once

#include "experiment/infrastructure/TerrainAssetCatalog.h"
#include "experiment/infrastructure/MaterialCatalog.h"
#include "app/ExperimentCameraSession.h"
#include <optional>
#include <string>

namespace ParallelRoam::App
{
/// <summary>
/// 管理实验资产选择请求，不拥有渲染器或算法
/// 目录只在初始化时读取；应用在受控帧边界消费请求
/// </summary>
class ExperimentSessionController
{
public:
    void Initialize(const std::string& requestedId);
    ExperimentCameraSession& Camera() { return _camera; }
    void Request(int index);
    void RequestMaterial(int index);
    [[nodiscard]] std::optional<Experiment::Infrastructure::MaterialPreset> TakeMaterial();
    [[nodiscard]] const auto& Materials() const { return _materials; }
    [[nodiscard]] std::optional<Experiment::Infrastructure::TerrainAsset> TakeSelection();
    [[nodiscard]] const auto& Assets() const { return _assets; }
    [[nodiscard]] const std::string& Error() const { return _error; }
    void SetError(std::string error) { _error = std::move(error); }

private:
    std::vector<Experiment::Infrastructure::TerrainAsset> _assets;
    std::vector<Experiment::Infrastructure::MaterialPreset> _materials;
    std::optional<std::size_t> _pending, _pendingMaterial;
    std::string _error;
    ExperimentCameraSession _camera;
};
}
