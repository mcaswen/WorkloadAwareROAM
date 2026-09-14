#include "benchmark/TransactionalPlatformProtocol.h"
#include "experiment/greedy_transactional_lod/TransactionalQualityProvenance.h"
#include "experiment/mesh_quality/PlatformMeshArtifact.h"
#include "algorithms/greedy_transactional_lod/TransactionalSeedBuilder.h"
#include "algorithms/greedy_transactional_lod/TransactionalReservation.h"
#include "algorithms/greedy_transactional_lod/TransactionalStateInvariant.h"
#include "algorithms/TerrainLodView.h"
#include "tools/CpuTaskExecutor.h"
#include <iomanip>
#include <iostream>

namespace
{
using namespace ParallelRoam;
using namespace Algorithms;
using namespace Algorithms::GreedyTransactionalLod;
TerrainLodViewInput View(std::size_t frame)
{
    const auto c=Benchmark::PlatformReplayCamera(true,Benchmark::PlatformReplayViews.at(frame),false);
    return BuildTerrainLodViewInput(c.View,c.Projection,c.CameraPosition,c.CameraForward,1280,720,false);
}
}

int main(int argc,char** argv)
{
    try
    {
        if (argc!=2 && argc!=3) throw std::runtime_error("Usage: quality-provenance-probe OUTPUT [immutable]");
        if (argc==3 && std::string_view(argv[2])!="immutable") throw std::runtime_error("Unknown height policy");
        const std::filesystem::path output(argv[1]);
        if (std::filesystem::exists(output)) throw std::runtime_error("Refusing provenance overwrite");
        std::filesystem::create_directories(output);
        Terrain::HeightMap source;std::string error;
        if (!source.LoadFromFile("assets/heightmaps/Hm_Terrain_Peking_513.png",&error)) throw std::runtime_error(error);
        TerrainLodBuildInput input;input.HeightMap=&source;input.View=View(0);
        auto& s=input.Settings;s.TerrainSize=80;s.HeightScale=12;s.MaxDepth=20;s.TriangleBudget=50000;
        s.ScreenSpaceSplitThresholdPixels=.25F;s.ScreenSpaceMergeThresholdPixels=.10F;
        s.Transactional.WorkerCount=8;s.Transactional.PrefixLimit=s.Transactional.DonorLimit=160;
        s.Transactional.PreserveSurvivingHeights=argc==3;
        // 与平台共用公共种子，不通过旧轨迹重放获得后续目标
        auto seed=TransactionalSeedBuilder::Build(input);
        Tools::CpuTaskExecutor executor(8);
        TransactionalExecution execution{8,false,[&](auto n,const auto& task) { executor.Dispatch(n,task); }};
        TransactionalPipeline pipeline(std::move(seed),execution);
        TransactionalStateInvariant::Validate(pipeline.State());
        WorkLedger initialize;pipeline.Initialize(initialize);
        input.View=View(15);const auto future=TransactionalSeedBuilder::ConfigurationFor(input);
        input.View=View(16);const auto returned=TransactionalSeedBuilder::ConfigurationFor(input);
        Experiment::GreedyTransactionalLod::TransactionalQualityProvenance audit(output,future,returned);
        std::ofstream frames(output/"frames.csv");frames.exceptions(std::ios::badbit|std::ios::failbit);
        frames<<std::setprecision(17)<<"frame,sample,hash,faces,raw,examined,receivers,need,feasible,exchanges,free,pairs,conflicts,donorReuse\n";
        for (std::size_t frame=0;frame<Benchmark::PlatformReplayViews.size();++frame)
        {
            WorkLedger work;work.Deadline=std::chrono::steady_clock::now()+std::chrono::seconds(180);
            input.View=View(frame);pipeline.SetView(TransactionalSeedBuilder::ConfigurationFor(input),work);
            const auto batch=TransactionalReservation::Plan(pipeline.State(),pipeline.Samples(),work,execution);
            // 批次已冻结后再观测，额外诊断不能影响本批成员
            audit.Before(frame,pipeline.State(),pipeline.Samples(),batch);
            pipeline.Apply(batch,work);static_cast<void>(pipeline.ConsumeMesh());
            audit.After(frame,pipeline.State(),pipeline.Samples());
            const auto hash=Experiment::MeshQuality::PlatformMeshHash(pipeline.Mesh());
            frames<<frame<<','<<Benchmark::PlatformReplayViews[frame]<<','<<hash<<','<<pipeline.State().FaceCount()
                <<','<<batch.Raw<<','<<batch.Examined<<','<<batch.Receivers<<','<<batch.Need<<','<<batch.Feasible
                <<','<<batch.Executed<<','<<batch.FreeExecuted<<','<<work.PairChecks<<','<<work.Conflicts<<','<<work.DonorReuse<<'\n';
            frames.flush();
            std::cout<<"frame="<<frame<<" exchanges="<<batch.Executed<<" hash="<<hash<<'\n';
            if (frame==0 || frame==2 || frame==15 || frame==16 || frame==23)
                Experiment::MeshQuality::WritePlatformMesh(output/("mesh-"+std::to_string(frame)+".bin"),pipeline.Mesh(),
                    input.View.ViewProjection,1280,720,false);
        }
        // 持续运行后再核查全结构，诊断程序不把它计入正常阶段成本
        TransactionalStateInvariant::Validate(pipeline.State());
        return 0;
    }
    catch (const std::exception& e) { std::cerr<<e.what()<<'\n';return 1; }
}
