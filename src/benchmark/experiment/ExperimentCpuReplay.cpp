#include "benchmark/experiment/ExperimentReplay.h"
#include "algorithms/TerrainLodAlgorithmRegistry.h"
#include "algorithms/TerrainLodView.h"
#include "experiment/mesh_quality/PlatformMeshArtifact.h"
#include "tools/profiling/ProfileSession.h"
#include <chrono>
#include <iostream>

namespace ParallelRoam::Benchmark::Experiment
{
int RunExperimentCpu(int argc,char** argv)
{
    try
    {
        if(argc!=4) throw std::runtime_error("用法: --experiment-cpu RESOLVED OUTPUT");
        const auto input=LoadReplayInput(argv[2]);
        const auto& c=input.Case;
        if(c.Mode=="visual") throw std::runtime_error("CPU入口没有实际渲染，visual需平台入口");
        const std::filesystem::path output(argv[3]);
        if(std::filesystem::exists(output)) throw std::runtime_error("拒绝覆盖CPU回放");
        std::filesystem::create_directories(output);
        auto algorithm=Algorithms::CreateTerrainLodAlgorithm(input.Algorithm);
        if(!algorithm) throw std::runtime_error("算法未构建");
        std::unique_ptr<Tools::Profiling::ProfileSession> profile;
        if(c.Mode=="profile") profile=std::make_unique<Tools::Profiling::ProfileSession>(output/"windows.csv");
        ReplayFrameWriter writer(output/"frames.csv");
        const bool zero=c.Backend=="d3d12";
        for(std::size_t i=0;i<input.Cameras.size();++i)
        {
            const auto& f=input.Cameras[i];
            Algorithms::TerrainLodBuildInput task;
            task.HeightMap=&input.Source;task.Settings=input.Settings;
            task.View=Algorithms::BuildTerrainLodViewInput(f.View,zero ? f.ProjectionZo:f.ProjectionNo,
                f.Position,f.Forward,f.Width,f.Height,zero);
            Algorithms::TerrainLodRenderPacket packet;
            std::string error;
            using Clock=std::chrono::steady_clock;
            if(profile) profile->Begin(0,static_cast<int>(i));
            const auto started=Clock::now();
            const bool ok=algorithm->BuildRenderData(task,packet,&error);
            ReplayTiming timing;
            timing.Frame=std::chrono::duration<double,std::milli>(Clock::now()-started).count();
            if(profile) profile->End();
            if(!ok || !packet.HasConsistentResourceContract()) throw std::runtime_error("CPU更新失败: "+error);
            const auto* mesh=packet.ResolveCpuMesh();
            if(!mesh) throw std::runtime_error("没有实际CPU输出");
            const auto evidence=Clock::now();
            const auto hash=ValidateReplayMesh(*mesh,input);
            std::string artifact;
            if(c.Mode=="quality" && IsEvidenceFrame(input,i))
            {
                artifact="mesh-"+std::to_string(i)+".bin";
                ParallelRoam::Experiment::MeshQuality::WritePlatformMesh(output/artifact,*mesh,
                    (zero ? f.ProjectionZo:f.ProjectionNo)*f.View,f.Width,f.Height,zero);
            }
            timing.Evidence=std::chrono::duration<double,std::milli>(Clock::now()-evidence).count();
            writer.Append(input,i,zero,algorithm->Stats(),*mesh,hash,timing,"",artifact);
        }
        if(profile) profile->Finish();
        return 0;
    }
    catch(const std::exception& e) { std::cerr << e.what() << '\n';return 1; }
}
}
