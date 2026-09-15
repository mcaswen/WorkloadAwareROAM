#include "benchmark/experiment/ExperimentCameraTools.h"
#include "benchmark/TransactionalPlatformProtocol.h"
#include "experiment/greedy_transactional_lod/TransactionalScalingProtocol.h"
#include <stdexcept>

namespace ParallelRoam::Benchmark::Experiment
{
ParallelRoam::Experiment::Infrastructure::CameraSequence ExportLegacyCamera(const std::string& id, bool peking)
{
    namespace Data = ParallelRoam::Experiment::Infrastructure;
    std::vector<std::uint32_t> order;
    if (id == "pq-return24") order.assign(PlatformReplayViews.begin(), PlatformReplayViews.end());
    else if (id == "sve8")
    {
        const auto& source = ParallelRoam::Experiment::GreedyTransactionalLod::TransactionalScalingProtocol::ViewOrder;
        order.assign(source.begin(),source.end());
    }
    else if (id == "historical-a64" || id == "historical-orbit")
        for (std::uint32_t i = 0; i < 64; ++i) order.push_back(i);
    else throw std::runtime_error("未知历史相机协议");
    if ((id == "historical-a64" && peking) || (id == "historical-orbit" && !peking))
        throw std::runtime_error("历史相机与资产不匹配");
    Data::CameraSequence result;
    for (const auto index : order)
    {
        const auto no = PlatformReplayCamera(peking,index,false);
        const auto zo = PlatformReplayCamera(peking,index,true);
        Data::CameraFrame frame;
        frame.Index = static_cast<std::uint32_t>(result.size());
        frame.SourceIndex = index;
        frame.NominalSeconds = static_cast<double>(frame.Index)/30.0;
        frame.Position = no.CameraPosition; frame.Forward = no.CameraForward;
        frame.View = no.View; frame.ProjectionNo = no.Projection; frame.ProjectionZo = zo.Projection;
        frame.Event = frame.Index >= 16 && id == "pq-return24" ? "recovery" :
            frame.Index < 3 && id == "pq-return24" ? "warmup" : "move";
        Data::SealCameraFrame(frame);
        result.push_back(frame);
    }
    Data::ValidateCameraSequence(result);
    return result;
}
}
