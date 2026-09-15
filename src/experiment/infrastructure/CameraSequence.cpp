#include "experiment/infrastructure/CameraSequence.h"
#include <glm/ext/matrix_transform.hpp>
#include <glm/ext/matrix_clip_space.hpp>
#include <bit>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <limits>
#include <sstream>
#include <stdexcept>

namespace ParallelRoam::Experiment::Infrastructure
{
namespace
{
struct Hash
{
    // 每个float按小端位编码累计，与CSV空格和小数排版无关
    std::uint64_t Value{14695981039346656037ULL};
    void Word(std::uint32_t word)
    {
        for (int shift = 0; shift < 32; shift += 8)
        { Value ^= (word >> shift) & 255U; Value *= 1099511628211ULL; }
    }
    void Float(float value) { Word(std::bit_cast<std::uint32_t>(value)); }
    void Vector(glm::vec3 value) { for (int i = 0; i < 3; ++i) Float(value[i]); }
    void Matrix(const glm::mat4& value)
    { for (int column = 0; column < 4; ++column) for (int row = 0; row < 4; ++row) Float(value[column][row]); }
};
std::string Header()
{
    std::ostringstream out;
    out << "frame,source,nominalSeconds,event,width,height,fov,near,far,px,py,pz,fx,fy,fz";
    for (const auto* name : {"view", "no", "zo"})
        for (int column = 0; column < 4; ++column)
            for (int row = 0; row < 4; ++row) out << ',' << name << column << row;
    out << ",poseHash,noHash,zoHash";
    return out.str();
}
std::vector<std::string> Fields(const std::string& line)
{
    std::istringstream stream(line);
    std::vector<std::string> result;
    std::string field;
    while (std::getline(stream, field, ',')) result.push_back(field);
    return result;
}
}
void SealCameraFrame(CameraFrame& frame)
{
    Hash pose;
    pose.Vector(frame.Position);
    pose.Vector(frame.Forward);
    pose.Matrix(frame.View);
    frame.PoseHash = pose.Value;
    const auto projectionHash = [&](const glm::mat4& projection) {
        auto hash = pose;
        hash.Word(static_cast<std::uint32_t>(frame.Width));
        hash.Word(static_cast<std::uint32_t>(frame.Height));
        hash.Float(frame.Fov); hash.Float(frame.Near); hash.Float(frame.Far);
        hash.Matrix(projection);
        return hash.Value;
    };
    frame.NoHash = projectionHash(frame.ProjectionNo);
    frame.ZoHash = projectionHash(frame.ProjectionZo);
}
CameraFrame MakeCameraFrame(const glm::vec3& position, const glm::vec3& target,
    std::uint32_t index, int width, int height, float farPlane)
{
    const auto direction = target - position;
    if (glm::length(direction) < 1e-5F ||
        glm::length(glm::cross(direction, glm::vec3(0,1,0))) < 1e-5F)
        throw std::runtime_error("相机朝向退化");
    CameraFrame frame;
    frame.Index = frame.SourceIndex = index;
    frame.NominalSeconds = static_cast<double>(index) / 30.0;
    frame.Width = width; frame.Height = height; frame.Far = farPlane;
    frame.Position = position; frame.Forward = glm::normalize(direction);
    frame.View = glm::lookAtRH(position, target, glm::vec3(0,1,0));
    const auto aspect = static_cast<float>(width) / static_cast<float>(height);
    frame.ProjectionNo = glm::perspectiveRH_NO(glm::radians(frame.Fov), aspect, frame.Near, frame.Far);
    frame.ProjectionZo = glm::perspectiveRH_ZO(glm::radians(frame.Fov), aspect, frame.Near, frame.Far);
    SealCameraFrame(frame);
    return frame;
}
void ValidateCameraSequence(const CameraSequence& frames)
{
    if (frames.empty() || frames.size() > 10000) throw std::runtime_error("路线长度无效");
    double previous = -1;
    for (std::size_t i = 0; i < frames.size(); ++i)
    {
        const auto& frame = frames[i];
        if (frame.Index != i || !std::isfinite(frame.NominalSeconds) || frame.NominalSeconds < previous ||
            frame.Width != frames.front().Width || frame.Height != frames.front().Height ||
            frame.Width <= 0 || frame.Height <= 0 || frame.Width > 8192 || frame.Height > 8192 ||
            !(frame.Fov > 0 && frame.Fov < 179 && frame.Near > 0 && frame.Far > frame.Near) ||
            frame.Event.empty() || frame.Event.find_first_not_of("abcdefghijklmnopqrstuvwxyz0123456789_-") != std::string::npos)
            throw std::runtime_error("相机序号、视口、事件或时间无效");
        previous = frame.NominalSeconds;
        for (int axis = 0; axis < 3; ++axis)
            if (!std::isfinite(frame.Position[axis]) || !std::isfinite(frame.Forward[axis]))
                throw std::runtime_error("相机非有限");
        if (std::abs(glm::length(frame.Forward)-1) > .001F ||
            glm::length(glm::cross(frame.Forward, glm::vec3(0,1,0))) < 1e-5F)
            throw std::runtime_error("相机方向无效");
        for (const auto* matrix : {&frame.View, &frame.ProjectionNo, &frame.ProjectionZo})
            for (int c = 0; c < 4; ++c) for (int r = 0; r < 4; ++r)
                if (!std::isfinite((*matrix)[c][r])) throw std::runtime_error("相机矩阵非有限");
        auto verified = frame;
        SealCameraFrame(verified);
        if (verified.PoseHash != frame.PoseHash || verified.NoHash != frame.NoHash || verified.ZoHash != frame.ZoHash)
            throw std::runtime_error("相机输入身份不一致");
    }
}
void SaveCameraSequence(const std::filesystem::path& path, const CameraSequence& frames)
{
    ValidateCameraSequence(frames);
    if (std::filesystem::exists(path)) throw std::runtime_error("拒绝覆盖冻结路线");
    if (!path.parent_path().empty()) std::filesystem::create_directories(path.parent_path());
    std::ofstream out(path);
    out.exceptions(std::ios::badbit | std::ios::failbit);
    out << Header() << '\n' << std::setprecision(std::numeric_limits<double>::max_digits10);
    for (const auto& f : frames)
    {
        out << f.Index << ',' << f.SourceIndex << ',' << f.NominalSeconds << ',' << f.Event << ','
            << f.Width << ',' << f.Height << ',' << f.Fov << ',' << f.Near << ',' << f.Far;
        for (auto vector : {f.Position, f.Forward}) for (int i = 0; i < 3; ++i) out << ',' << vector[i];
        for (const auto* matrix : {&f.View, &f.ProjectionNo, &f.ProjectionZo})
            for (int c = 0; c < 4; ++c) for (int r = 0; r < 4; ++r) out << ',' << (*matrix)[c][r];
        out << ',' << f.PoseHash << ',' << f.NoHash << ',' << f.ZoHash << '\n';
    }
}
CameraSequence LoadCameraSequence(const std::filesystem::path& path)
{
    std::ifstream in(path);
    if (!in) throw std::runtime_error("无法打开冻结路线");
    std::string line;
    std::getline(in, line);
    if (!line.empty() && line.back() == '\r') line.pop_back();
    if (line != Header())
        throw std::runtime_error("冻结路线列协议不符");
    CameraSequence result;
    while (std::getline(in, line))
    {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        const auto fields = Fields(line);
        if (fields.size() != 66) throw std::runtime_error("冻结路线缺列");
        std::size_t cursor = 0;
        const auto number = [&]() {
            const auto& text = fields.at(cursor++);
            std::size_t consumed{};
            const auto value = std::stod(text, &consumed);
            if (consumed != text.size() || !std::isfinite(value)) throw std::runtime_error("相机数值无效");
            return value;
        };
        const auto integer = [&]() {
            const double value = number();
            if (value < 0 || value > 1000000 || std::floor(value) != value) throw std::runtime_error("相机整数无效");
            return static_cast<std::uint32_t>(value);
        };
        CameraFrame f;
        f.Index = integer(); f.SourceIndex = integer(); f.NominalSeconds = number();
        f.Event = fields.at(cursor++); f.Width = static_cast<int>(integer()); f.Height = static_cast<int>(integer());
        f.Fov = static_cast<float>(number()); f.Near = static_cast<float>(number()); f.Far = static_cast<float>(number());
        for (auto* vector : {&f.Position, &f.Forward}) for (int axis = 0; axis < 3; ++axis) (*vector)[axis] = static_cast<float>(number());
        for (auto* matrix : {&f.View, &f.ProjectionNo, &f.ProjectionZo})
            for (int c = 0; c < 4; ++c) for (int r = 0; r < 4; ++r) (*matrix)[c][r] = static_cast<float>(number());
        const auto hash = [&]() {
            const auto& text = fields.at(cursor++);
            std::size_t consumed{};
            const auto value = std::stoull(text, &consumed);
            if (consumed != text.size()) throw std::runtime_error("相机hash格式错误");
            return value;
        };
        f.PoseHash = hash(); f.NoHash = hash(); f.ZoHash = hash();
        result.push_back(f);
        if (result.size() > 10000) throw std::runtime_error("路线超过限制");
    }
    ValidateCameraSequence(result);
    return result;
}
}
