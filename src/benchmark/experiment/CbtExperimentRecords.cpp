#include "benchmark/experiment/CbtExperimentRecords.h"

#include <iomanip>
#include <stdexcept>

namespace ParallelRoam::Benchmark::Experiment
{
CbtExperimentRecords::CbtExperimentRecords(const std::filesystem::path& path)
    : _stream(path)
{
    _stream.exceptions(std::ios::badbit | std::ios::failbit);
    _stream << std::setprecision(17)
        << "frame,resourceGeneration,topologyGeneration,classificationGeneration,timingGeneration,drawGeneration,age,dropped,delayedFaces,capacity,area,validation,geometry,activeSlots,remainingSlots,faults,computeMs,drawMs,captureResource,captureGeneration,actualFaces,readbackBytes,captureMs,committedSlots,splitPropagation,preparedSimplification,releasedSlots,simplifyPropagation,pairMerge,quadMerge,template0,template1,template2,template3";
    for (std::size_t stage = 0U; stage < Algorithms::TerrainLodCbtGpuStageCount; ++stage)
    {
        _stream << ",stage" << stage << "Ms";
    }
    _stream << '\n';
}

void CbtExperimentRecords::Append(std::size_t frame, const Algorithms::TerrainLodCbtStats& s,
    std::size_t delayedFaces, const std::optional<CbtCaptureRecord>& capture)
{
    auto& out = _stream;
    out << frame << ',' << s.ResourceGeneration << ',' << s.TopologyGeneration << ','
        << s.ClassificationSampleGeneration << ',' << s.GpuTimingSampleGeneration << ','
        << s.TerrainRenderSampleGeneration << ',' << s.DiagnosticSampleAge << ',' << s.DiagnosticSampleDropped << ',';
    if (s.ClassificationSampleGeneration != 0U)
    {
        out << delayedFaces;
    }
    out << ',' << s.CapacitySetting << ',' << s.TriangleAreaPixelsSetting << ','
        << static_cast<int>(s.ValidationModeSetting) << ',' << static_cast<int>(s.GeometryModeSetting) << ','
        << s.ActiveDynamicSlotCount << ',' << s.RemainingDynamicSlotCount << ',' << s.FaultRecoveryCount << ',';
    if (s.GpuTimingSampleGeneration != 0U)
    {
        out << s.GpuStageSumMilliseconds;
    }
    out << ',';
    const auto drawStage = static_cast<std::size_t>(Algorithms::TerrainLodCbtGpuStage::TerrainRender);
    if (s.TerrainRenderSampleGeneration != 0U)
    {
        out << s.GpuStageMilliseconds[drawStage];
    }
    if (capture)
    {
        if (capture->ResourceGeneration != s.ResourceGeneration ||
            capture->TopologyGeneration != s.TopologyGeneration)
        {
            throw std::runtime_error("CBT capture and submitted generation differ");
        }
        out << ',' << capture->ResourceGeneration << ',' << capture->TopologyGeneration << ','
            << capture->Faces << ',' << capture->ReadbackBytes << ',' << capture->Milliseconds;
    }
    else
    {
        out << ",,,,,";
    }
    if (s.ClassificationSampleGeneration != 0U)
    {
        out << ',' << s.CommittedDynamicSlotCount << ',' << s.SplitPropagationCount << ','
            << s.PreparedSimplificationCount << ',' << s.ReleasedDynamicSlotCount << ','
            << s.SimplifyPropagationCount << ',' << s.PairMergeCount << ',' << s.QuadMergeCount;
        for (const auto count : s.BisectTemplateCounts)
        {
            out << ',' << count;
        }
    }
    else
    {
        out << ",,,,,,,,,,,";
    }
    for (std::size_t stage = 0U; stage < Algorithms::TerrainLodCbtGpuStageCount; ++stage)
    {
        out << ',';
        const bool available = stage == drawStage ? s.TerrainRenderSampleGeneration != 0U :
            s.GpuTimingSampleGeneration != 0U;
        if (available)
        {
            out << s.GpuStageMilliseconds[stage];
        }
    }
    out << '\n';
    out.flush();
}
}
