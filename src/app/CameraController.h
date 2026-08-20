#pragma once

#include "app/InputState.h"

#include <glm/glm.hpp>

namespace ParallelRoam::App
{
/// <summary>
/// 渲染闭环使用的自由飞行相机控制器
/// </summary>
class CameraController
{
public:
    CameraController();

    void Update(const InputState& input, float deltaSeconds);
    void SetPose(const glm::vec3& position, float yawDegrees, float pitchDegrees);

    [[nodiscard]] glm::mat4 GetViewMatrix() const;
    [[nodiscard]] glm::vec3 Position() const;
    [[nodiscard]] glm::vec3 Forward() const;
    [[nodiscard]] float YawDegrees() const;
    [[nodiscard]] float PitchDegrees() const;

private:
    // yaw 和 pitch 改变后统一刷新前向、右向和上向基向量
    void UpdateBasis();

    // 当前位置和朝向以世界空间存储，便于后续传给算法层做视点相关 LOD
    glm::vec3 _position{0.0F, 1.8F, 6.0F};
    glm::vec3 _front{0.0F, 0.0F, -1.0F};
    glm::vec3 _right{1.0F, 0.0F, 0.0F};
    glm::vec3 _up{0.0F, 1.0F, 0.0F};
    glm::vec3 _worldUp{0.0F, 1.0F, 0.0F};
    float _yawDegrees{-90.0F};
    float _pitchDegrees{-18.0F};
    float _moveSpeed{6.0F};
    float _fastMoveMultiplier{3.0F};
    float _mouseSensitivity{0.12F};
};
} // 命名空间 ParallelRoam::App
