#include "experiment/infrastructure/CameraRecipe.h"
#include <boost/property_tree/json_parser.hpp>
#include <algorithm>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <stdexcept>

namespace ParallelRoam::Experiment::Infrastructure
{
void CheckCameraClearance(const CameraSequence& frames, const Terrain::HeightMap& reference,
    float size, float heightScale)
{
    // 连段抽查只用于路线准入，不宣称连续碰撞的数学证明
    for (std::size_t i = 0; i < frames.size(); ++i)
        for (int step = 0; step <= 4; ++step)
        {
            const auto point = glm::mix(frames[i].Position, frames[std::min(i+1, frames.size()-1)].Position,
                static_cast<float>(step)/4.0F);
            const float u = point.x/size + .5F, v = point.z/size + .5F;
            if (u >= 0 && u <= 1 && v >= 0 && v <= 1 &&
                point.y < reference.SampleBilinear(u,v)*heightScale + size*.01F)
                throw std::runtime_error("路线穿地或离地余量不足；需修改配方并生成新版本");
        }
}
CameraSequence GenerateCameraRecipe(const std::filesystem::path& path,
    const Terrain::HeightMap& reference, float size, float heightScale)
{
    boost::property_tree::ptree recipe;
    boost::property_tree::read_json(path.string(), recipe);
    const auto kind = recipe.get<std::string>("kind");
    const auto count = recipe.get<std::uint32_t>("opportunities");
    if (recipe.get<int>("version") != 1 || count < 2 || count > 10000 ||
        !reference.IsValid() || !std::isfinite(size) || size <= 0 ||
        !std::isfinite(heightScale) || heightScale <= 0)
        throw std::runtime_error("相机配方或参考地形无效");
    CameraSequence result;
    const auto surface = [&](float x, float z) {
        return reference.SampleBilinear(x/size+.5F, z/size+.5F)*heightScale;
    };
    for (std::uint32_t index = 0; index < count; ++index)
    {
        const float t = static_cast<float>(index)/static_cast<float>(count-1);
        glm::vec3 eye(0,size*.5F+heightScale,size*.9F), target(0,heightScale*.35F,0);
        std::string event = "move";
        if (kind == "overview-orbit")
        {
            const float angle = index == count-1 ? 0 : t*6.28318530718F;
            eye.x = std::sin(angle)*size*.9F; eye.z = std::cos(angle)*size*.9F;
            if (index == count-1) event = "return";
        }
        else if (kind == "approach-return")
        {
            const float phase = std::min(t*3.0F, 2.0F);
            const float near = phase <= 1 ? phase : 2-phase;
            eye = {0, heightScale+size*(.65F-.4F*near), size*(1.1F-.85F*near)};
            if (t >= 2.0F/3) event = "recovery";
            else if (t > 1.0F/3) event = "return";
        }
        else if (kind == "terrain-traverse")
        {
            const float x = (-.4F+.8F*t)*size, z = .12F*size*std::sin(t*6.28318530718F);
            eye = {x, surface(x,z)+heightScale+size*.18F, z+size*.16F};
            target = {x+size*.12F, surface(x+size*.12F,z), z};
        }
        else if (kind == "reveal-return")
        {
            const float phase = std::min(t*3.0F, 2.0F);
            const float angle = (phase <= 1 ? phase : 2-phase)*1.25F;
            const glm::vec3 forward(std::sin(angle), -.38F, -std::cos(angle));
            target = eye + forward*size;
            if (t >= 2.0F/3) event = "recovery";
            else if (t > 1.0F/3) event = "return";
            else event = "reveal";
        }
        else if (kind == "stationary-recovery") event = "recovery";
        else throw std::runtime_error("未知相机模板");
        auto frame = MakeCameraFrame(eye,target,index,1280,720,std::max(500.0F,size*10));
        frame.Event = event;
        result.push_back(frame);
    }
    ValidateCameraSequence(result);
    CheckCameraClearance(result, reference, size, heightScale);
    return result;
}
CameraSequence ExpandCameraKeys(const std::vector<CameraKeyframe>& keys)
{
    if (keys.empty() || keys.size() > 128) throw std::runtime_error("关键帧数量无效");
    CameraSequence frames;
    const auto append = [&](glm::vec3 position, glm::vec3 forward, const char* event) {
        auto frame = MakeCameraFrame(position, position+forward, static_cast<std::uint32_t>(frames.size()));
        frame.Event = event;
        frames.push_back(frame);
    };
    for (std::size_t i = 0; i < keys.size(); ++i)
    {
        if (keys[i].Hold < 1 || keys[i].Hold > 64) throw std::runtime_error("停留机会须为1～64");
        if (i > 0)
            for (int sample = 1; sample < 16; ++sample)
            {
                const float t = static_cast<float>(sample)/16.0F;
                const auto direction = glm::mix(keys[i-1].Forward,keys[i].Forward,t);
                if (glm::length(direction) < .01F) throw std::runtime_error("反向姿态需要补一个中间关键帧");
                append(glm::mix(keys[i-1].Position,keys[i].Position,t),glm::normalize(direction),"move");
            }
        for (std::uint32_t hold = 0; hold < keys[i].Hold; ++hold)
            append(keys[i].Position,keys[i].Forward,"hold");
    }
    ValidateCameraSequence(frames);
    return frames;
}
void SaveCameraKeys(const std::filesystem::path& path, const std::vector<CameraKeyframe>& keys)
{
    if (std::filesystem::exists(path)) throw std::runtime_error("拒绝覆盖录制配方");
    std::ofstream out(path);
    out.exceptions(std::ios::badbit | std::ios::failbit);
    out << std::setprecision(17) << "{\n\"version\":1,\"interpolation\":\"linear-position-normalized-direction-16\",\"keys\":[\n";
    for (std::size_t i = 0; i < keys.size(); ++i)
    {
        if (i) out << ",\n";
        out << "{\"position\":[" << keys[i].Position.x << ',' << keys[i].Position.y << ',' << keys[i].Position.z
            << "],\"forward\":[" << keys[i].Forward.x << ',' << keys[i].Forward.y << ',' << keys[i].Forward.z
            << "],\"hold\":" << keys[i].Hold << '}';
    }
    out << "\n]}\n";
}
}
