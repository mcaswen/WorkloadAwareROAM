#include "experiment/greedy_transactional_lod/TransactionalScalingProtocol.h"
#include "experiment/formal/FormalExperimentCamera.h"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace ParallelRoam::Experiment::GreedyTransactionalLod
{
using namespace Algorithms::GreedyTransactionalLod;
std::optional<std::size_t> TransactionalScalingProtocol::Budget(std::string_view scenario)
{
    for (auto budget : Budgets)
        if (scenario=="peking547-sve-orbit64-b"+std::to_string(budget)) return budget;
    return std::nullopt;
}

Formal::FormalScenario TransactionalScalingProtocol::Scenario(
    const std::filesystem::path& root,std::size_t budget,std::size_t workers)
{
    if (std::find(Budgets.begin(),Budgets.end(),budget)==Budgets.end() || (workers!=4 && workers!=8))
        throw std::invalid_argument("压力协议预算或线程数非法");
    Formal::FormalScenario result;
    result.ScenarioId="peking547-sve-orbit64-b"+std::to_string(budget);
    result.TerrainId="peking547";result.TrajectoryId="budget-orbit";
    result.HeightMapPath=root/"assets/heightmaps/Hm_Terrain_Peking_513.png";
    result.HeightMapWidth=result.HeightMapHeight=547;
    auto& settings=result.Settings;
    settings.TerrainSize=80;settings.HeightScale=12;settings.MaxDepth=20;
    settings.TriangleBudget=budget;
    settings.ScreenSpaceSplitThresholdPixels=.25F;settings.ScreenSpaceMergeThresholdPixels=.10F;
    // 只改变请求线程数，保留默认阶段动作和小工作量回退
    auto& policy=settings.PassPolicy;
    policy.SplitScoreWorkerCount=policy.MergeScoreWorkerCount=workers;
    policy.SplitTopologyWorkerCount=policy.MergeTopologyWorkerCount=workers;
    policy.MeshEmitWorkerCount=workers;
    return result;
}

Formal::CameraSample TransactionalScalingProtocol::Camera(std::uint32_t index)
{
    if (index>=64) throw std::invalid_argument("压力相机索引越界");
    Formal::CameraSample result;
    result.TrajectoryId="budget-orbit";result.SampleIndex=index;
    // 沿用已有压力路径的 float 常量和运算顺序，禁止以不同精度重新生成
    const float t=static_cast<float>(index)/63.0F;
    const float angle=t*6.28318530718F;
    result.Position={std::cos(angle)*58.0F,20.0F+std::sin(angle*2.0F)*3.0F,std::sin(angle)*58.0F};
    result.Target={std::cos(angle+.55F)*10.0F,4.0F,std::sin(angle+.55F)*10.0F};
    return result;
}

Configuration TransactionalScalingProtocol::View(const Configuration& initial,std::uint32_t index)
{
    auto result=initial;const auto camera=Camera(index);const auto view=Formal::BuildCameraView(camera);
    result.SampleIndex=index;result.Width=camera.DrawableWidth;result.Height=camera.DrawableHeight;
    for (glm::length_t row=0;row<4;++row)
        for (glm::length_t column=0;column<4;++column)
            result.Matrix[static_cast<std::size_t>(row*4+column)]=view.ViewProjection[column][row];
    return result;
}

void TransactionalScalingProtocol::Validate(const Configuration& config)
{
    const auto budget=Budget(config.Scenario);
    if (!budget || *budget!=config.Budget || config.TerrainSize!=80 || config.HeightScale!=12 ||
        config.SplitPixels!=.25 || config.Width!=1280 || config.Height!=720 ||
        config.Matrix!=View(config,config.SampleIndex).Matrix)
        throw std::invalid_argument("压力输入与冻结参数或矩阵不一致");
}

void TransactionalScalingProtocol::ConfigureLimits(Configuration& config,std::string_view policy)
{
    Validate(config);
    if (policy!="fixed64" && policy!="scaled") throw std::invalid_argument("额度策略非法");
    config.PrefixLimit=config.DonorLimit=policy=="fixed64" ? 64 : 64*config.Budget/20000;
}
}
