#pragma once
#include "experiment/infrastructure/CameraRecipe.h"
#include <optional>

namespace ParallelRoam::App
{
/// <summary>
/// 管理录制与顺序播放生命周期，不拥有相机或LOD状态
/// 重放起点产生显式Reset请求；路线中的返回和停留不产生Reset
/// </summary>
class ExperimentCameraSession
{
public:
    void Capture(glm::vec3 position, glm::vec3 forward, int hold);
    void RemoveLast();
    std::filesystem::path Export(const std::filesystem::path& heightMap, float size, float heightScale);
    void Load(const std::filesystem::path& path);
    void Play();
    void Pause() { _playing = false; }
    void Step();
    void Restart();
    void Stop() { _active = _playing = _step = false; }
    [[nodiscard]] bool TakeReset();
    [[nodiscard]] std::optional<Experiment::Infrastructure::CameraFrame> Current();
    [[nodiscard]] bool ShouldUpdate() const { return !_active || _playing || _step; }
    void CompleteOpportunity();
    [[nodiscard]] std::size_t KeyCount() const { return _keys.size(); }
    [[nodiscard]] std::size_t Cursor() const { return _cursor; }
    [[nodiscard]] std::size_t FrameCount() const { return _frames.size(); }
    [[nodiscard]] bool Active() const { return _active; }
private:
    std::vector<Experiment::Infrastructure::CameraKeyframe> _keys;
    Experiment::Infrastructure::CameraSequence _frames;
    std::size_t _cursor{};
    bool _active{}, _playing{}, _step{}, _reset{}, _advance{};
};
}
