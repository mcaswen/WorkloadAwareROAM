#include "app/ExperimentCameraSession.h"
#include <chrono>
#include <stdexcept>

namespace ParallelRoam::App
{
void ExperimentCameraSession::Capture(glm::vec3 position, glm::vec3 forward, int hold)
{
    if (_keys.size() >= 128 || hold < 1 || hold > 64) throw std::runtime_error("关键帧或停留数量超限");
    _keys.push_back({position,forward,static_cast<std::uint32_t>(hold)});
}
void ExperimentCameraSession::RemoveLast() { if (!_keys.empty()) _keys.pop_back(); }
std::filesystem::path ExperimentCameraSession::Export(const std::filesystem::path& heightMap, float size, float heightScale)
{
    namespace Data = Experiment::Infrastructure;
    const auto frames = Data::ExpandCameraKeys(_keys);
    Terrain::HeightMap reference;
    std::string error;
    if (!reference.LoadFromFile(heightMap,&error)) throw std::runtime_error(error);
    Data::CheckCameraClearance(frames,reference,size,heightScale);
    const auto stamp = std::chrono::system_clock::now().time_since_epoch().count();
    const auto output = std::filesystem::path("benchmark-output/experiment-infrastructure/recordings") / std::to_string(stamp);
    if (!std::filesystem::create_directories(output)) throw std::runtime_error("录制目录已存在");
    Data::SaveCameraKeys(output/"recipe.json",_keys);
    Data::SaveCameraSequence(output/"frames.csv",frames);
    return output/"frames.csv";
}
void ExperimentCameraSession::Load(const std::filesystem::path& path)
{
    auto frames = Experiment::Infrastructure::LoadCameraSequence(path);
    _frames = std::move(frames);
    _cursor = 0; _advance = false;
    _active = _playing = _step = false;
    _reset = true;
}
void ExperimentCameraSession::Play()
{
    if (_frames.empty()) throw std::runtime_error("请先载入冻结路线");
    _active = _playing = true;
}
void ExperimentCameraSession::Step() { Play(); _playing = false; _step = true; }
void ExperimentCameraSession::Restart()
{
    if (_frames.empty()) throw std::runtime_error("请先载入冻结路线");
    _cursor = 0; _advance = false; _reset = true; _active = true; _playing = false; _step = true;
}
bool ExperimentCameraSession::TakeReset()
{
    if (!_active) return false;
    const bool value = _reset;
    _reset = false;
    return value;
}
std::optional<Experiment::Infrastructure::CameraFrame> ExperimentCameraSession::Current()
{
    if (!_active || _frames.empty()) return std::nullopt;
    if (_advance && (_playing || _step))
    {
        ++_cursor;
        _advance = false;
    }
    return _frames.at(_cursor);
}
void ExperimentCameraSession::CompleteOpportunity()
{
    if (!_active || (!_playing && !_step)) return;
    _step = false;
    if (_cursor+1 < _frames.size()) _advance = true;
    else _playing = false;
}
}
