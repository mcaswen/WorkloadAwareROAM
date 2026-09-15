#pragma once

#include "render/TerrainRenderer.h"
#include "experiment/formal/FormalExperimentCamera.h"
#include <glm/ext/matrix_transform.hpp>
#include <glm/ext/matrix_clip_space.hpp>
#include <cmath>

namespace ParallelRoam::Benchmark
{
inline constexpr std::array<std::uint32_t,24> PlatformReplayViews{14,14,14,15,16,18,22,26,30,34,38,42,46,50,54,58,14,14,14,14,14,14,14,14};

/// <summary>
/// 重用 A 相机；压力输入保留已有 orbit64 的 float 公式和运算顺序
/// 后端只选择投影深度域，不改变相机或屏幕尺寸
/// </summary>
inline Render::RenderContext PlatformReplayCamera(bool peking,std::uint32_t index,bool zo)
{
    ParallelRoam::Experiment::Formal::CameraSample sample;
    if (peking)
    {
        const float t=static_cast<float>(index)/63.0F,angle=t*6.28318530718F;
        sample.Position={std::cos(angle)*58.0F,20.0F+std::sin(angle*2.0F)*3.0F,std::sin(angle)*58.0F};
        sample.Target={std::cos(angle+.55F)*10.0F,4.0F,std::sin(angle+.55F)*10.0F};
    }
    else
    {
        ParallelRoam::Experiment::Formal::FormalScenario scenario;scenario.TrajectoryId="A";
        scenario.Settings.TerrainSize=30;
        sample=ParallelRoam::Experiment::Formal::GenerateCameraSample(scenario,index);
    }
    Render::RenderContext c;c.CameraPosition=sample.Position;
    c.CameraForward=glm::normalize(sample.Target-sample.Position);
    c.View=glm::lookAtRH(sample.Position,sample.Target,glm::vec3{0,1,0});
    c.DrawableWidth=1280;c.DrawableHeight=720;c.UsesZeroToOneDepth=zo;
    c.Projection=zo ? glm::perspectiveRH_ZO(glm::radians(60.0F),1280.0F/720.0F,.1F,500.0F)
                    : glm::perspectiveRH_NO(glm::radians(60.0F),1280.0F/720.0F,.1F,500.0F);
    return c;
}
}
