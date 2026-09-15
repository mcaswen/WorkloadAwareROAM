#include "app/ExperimentSessionController.h"

#include <algorithm>
#include <stdexcept>

namespace ParallelRoam::App
{
void ExperimentSessionController::Initialize(const std::string& requestedId)
{
    try
    {
        _assets = Experiment::Infrastructure::LoadTerrainAssetCatalog("assets/experiments/terrain_catalog.json");
        _materials = Experiment::Infrastructure::LoadMaterialCatalog("assets/experiments/material_catalog.json");
        if (!requestedId.empty())
        {
            const auto found = std::find_if(_assets.begin(), _assets.end(),
                [&](const auto& asset) { return asset.Id == requestedId; });
            if (found == _assets.end()) throw std::runtime_error("未知实验资产: " + requestedId);
            _pending = static_cast<std::size_t>(found - _assets.begin());
        }
    }
    catch (const std::exception& error) { _error = error.what(); }
}
void ExperimentSessionController::Request(int index)
{
    if (index >= 0 && static_cast<std::size_t>(index) < _assets.size())
        _pending = static_cast<std::size_t>(index);
}
std::optional<Experiment::Infrastructure::TerrainAsset> ExperimentSessionController::TakeSelection()
{
    if (!_pending) return std::nullopt;
    const auto index = *_pending;
    _pending.reset();
    return _assets.at(index);
}
void ExperimentSessionController::RequestMaterial(int index)
{
    if (index >= 0 && static_cast<std::size_t>(index) < _materials.size())
        _pendingMaterial = static_cast<std::size_t>(index);
}
std::optional<Experiment::Infrastructure::MaterialPreset> ExperimentSessionController::TakeMaterial()
{
    if (!_pendingMaterial) return std::nullopt;
    const auto index = *_pendingMaterial;
    _pendingMaterial.reset();
    return _materials.at(index);
}
}
