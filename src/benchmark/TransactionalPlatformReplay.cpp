#include "benchmark/TransactionalPlatformReplay.h"
#include "algorithms/TerrainLodAlgorithmRegistry.h"
#include "experiment/formal/FormalExperimentCamera.h"
#include "experiment/mesh_quality/PlatformMeshArtifact.h"
#include "platform/Window.h"
#include "render/GraphicsBackend.h"
#include "render/TerrainRenderer.h"
#include <glm/ext/matrix_transform.hpp>
#include <glm/ext/matrix_clip_space.hpp>
#include <chrono>
#include <cmath>
#include <iomanip>
#include <iostream>

namespace ParallelRoam::Benchmark
{
namespace
{
using Clock=std::chrono::steady_clock;
double Ms(Clock::time_point start) { return std::chrono::duration<double,std::milli>(Clock::now()-start).count(); }
void Require(bool ok,const std::string& message) { if (!ok) throw std::runtime_error(message); }
constexpr std::array<std::uint32_t,24> Views{14,14,14,15,16,18,22,26,30,34,38,42,46,50,54,58,14,14,14,14,14,14,14,14};

/// <summary>
/// 重用 A 相机；压力输入保留已有 orbit64 的 float 公式和运算顺序
/// 后端只选择投影深度域，不改变相机或屏幕尺寸
/// </summary>
Render::RenderContext Camera(bool peking,std::uint32_t index,bool zo)
{
    Experiment::Formal::CameraSample sample;
    if (peking)
    {
        const float t=static_cast<float>(index)/63.0F,angle=t*6.28318530718F;
        sample.Position={std::cos(angle)*58.0F,20.0F+std::sin(angle*2.0F)*3.0F,std::sin(angle)*58.0F};
        sample.Target={std::cos(angle+.55F)*10.0F,4.0F,std::sin(angle+.55F)*10.0F};
    }
    else
    {
        Experiment::Formal::FormalScenario scenario;scenario.TrajectoryId="A";
        scenario.Settings.TerrainSize=30;
        sample=Experiment::Formal::GenerateCameraSample(scenario,index);
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

int RunTransactionalPlatformReplay(int argc,char** argv)
{
    Platform::Window window;auto graphics=Render::CreateConfiguredGraphicsBackend();Render::TerrainRenderer renderer;
    const auto cleanup=[&] { if (graphics && graphics->IsValid()) graphics->WaitForGpuIdle();
        renderer.Shutdown();if (graphics) graphics->Shutdown();window.Destroy(); };
    try
    {
        Require(argc==7 && std::string_view(argv[1])=="--transactional-platform-replay",
            "Usage: --transactional-platform-replay test129|peking classic|dod|transactional 1|8 OUTPUT normal|export");
        const bool peking=std::string_view(argv[2])=="peking";
        Require(peking || std::string_view(argv[2])=="test129","Unknown frozen input");
        const auto id=Algorithms::ParseTerrainLodAlgorithm(argv[3]);Require(id.has_value(),"Algorithm unavailable");
        const std::string_view threads(argv[4]);Require(threads=="1" || threads=="8","Workers must be 1 or 8");
        const std::size_t workers=threads=="1" ? 1 : 8;
        const std::filesystem::path output(argv[5]);const bool exporting=std::string_view(argv[6])=="export";
        Require(exporting || std::string_view(argv[6])=="normal","Unknown replay mode");
        Require(!std::filesystem::exists(output),"Refusing to overwrite replay output");
        std::filesystem::create_directories(output);
        std::string error;const auto setup=Clock::now();
        Require(window.Initialize(),"SDL initialization failed");Require(graphics && graphics->ConfigureWindow(&error),error);
        Require(window.Create("Transactional platform replay",1280,720,graphics->RequiredSdlWindowFlags()),"Window failed");
        Require(graphics->Initialize(window.NativeWindow(),&error),error);
        static_cast<void>(graphics->SetVSyncEnabled(false));graphics->RefreshDrawableSize();
        Require(graphics->DrawableWidth()==1280 && graphics->DrawableHeight()==720,"Drawable differs from frozen protocol");
        Render::TerrainRenderSettings settings;settings.TerrainLodAlgorithm=*id;
        settings.TerrainSize=peking ? 80.0F : 30.0F;settings.HeightScale=peking ? 12.0F : 4.0F;
        settings.RoamMaxDepth=peking ? 20 : 14;settings.RoamTriangleBudget=peking ? 50000 : 4096;
        settings.RoamScreenSpaceSplitThresholdPixels=peking ? .25F : 4.0F;
        settings.RoamScreenSpaceMergeThresholdPixels=peking ? .10F : 2.0F;
        settings.Transactional.WorkerCount=workers;
        settings.Transactional.PrefixLimit=settings.Transactional.DonorLimit=peking ? 160 : 64;
        auto& policy=settings.RoamPassPolicy;
        policy.SplitScoreWorkerCount=policy.MergeScoreWorkerCount=policy.SplitTopologyWorkerCount=
            policy.MergeTopologyWorkerCount=policy.MeshEmitWorkerCount=workers;
        const auto asset=peking ? "assets/heightmaps/Hm_Terrain_Peking_513.png" : "assets/heightmaps/Hm_Terrain_Test_129.pgm";
        Require(renderer.Initialize(*graphics,asset,"assets/textures/Tex_Terrain_Debug_Diffuse.ppm",settings,&error),error);
        // 旧 renderer 的初始化视图不进入冻结轨迹；费用仍留在 setup 中
        renderer.ResetTerrainLodAlgorithm();
        std::ofstream meta(output/"environment.txt");meta<<std::setprecision(17)
            <<"backend="<<graphics->Name()<<"\nadapter="<<graphics->AdapterName()<<"\nversion="<<graphics->VersionString()
            <<"\nsetupMs="<<Ms(setup)<<"\nasset="<<asset<<"\nworkers="<<workers<<"\nmode="<<argv[6]<<'\n';
        std::ofstream csv(output/"frames.csv");csv.exceptions(std::ios::badbit|std::ios::failbit);csv<<std::setprecision(17);
        csv<<"frame,sample,ok,faces,sequence,hash,cpuMs,uploadMs,uploadBytes,beginMs,waitMs,renderMs,presentMs,frameMs,gpuDelayedMs,split,merge,status,updated,cold,seedFaces,samples,raw,examined,receivers,need,feasible,exchanges,free,pairs,conflicts,donorReuse,touches,evaluations,vertexWrites,indexWrites,seedMs,initializeMs,viewMs,receiverMs,donorMs,reservationMs,topologyPrepareMs,topologyPublishMs,sampleRepairMs,meshPrepareMs,continuationMs,adapterMs\n";
        const std::size_t count=graphics->UsesZeroToOneDepth() ? 8 : Views.size();
        for (std::size_t i=0;i<count;++i)
        {
            SDL_Event event;while (SDL_PollEvent(&event)) Require(event.type!=SDL_QUIT,"Replay window closed");
            const auto view=Camera(peking,Views[i],graphics->UsesZeroToOneDepth());
            renderer.RequestMeshRebuild();const auto frame=Clock::now();auto part=Clock::now();graphics->BeginFrame();
            const double beginMs=Ms(part),waitMs=graphics->LastGpuWaitMilliseconds();
            const bool ok=renderer.UpdateForView(view,&error);part=Clock::now();renderer.Render(view);
            const double renderMs=Ms(part);part=Clock::now();graphics->Present();const double presentMs=Ms(part),frameMs=Ms(frame);
            const auto stats=renderer.Stats();const auto* mesh=renderer.CurrentCpuMeshForDiagnostics();
            Require(mesh!=nullptr && !mesh->Indices.empty(),"No published CPU mesh");
            Require(stats.TriangleCount<=settings.RoamTriangleBudget,"Hard budget exceeded");
            // 计时之外按实际绘制索引检查有限几何；不把低精度输出换回内部曲面
            for (std::size_t k=0;k<mesh->Indices.size();k+=3)
            {
                std::array<glm::dvec3,3> p{};
                for (std::size_t j=0;j<3;++j)
                {
                    Require(mesh->Indices[k+j]<mesh->Vertices.size(),"Index out of bounds");
                    p[j]=glm::dvec3(mesh->Vertices[mesh->Indices[k+j]].Position);
                    Require(std::isfinite(p[j].x) && std::isfinite(p[j].y) && std::isfinite(p[j].z),"Non-finite mesh");
                }
                // 核心参数域 CCW 对应 x/z 下向绕序，法线单独朝上；后端不启用背面剔除
                const double orientation=glm::cross(p[1]-p[0],p[2]-p[0]).y;
                const bool transactional=*id==Algorithms::TerrainLodAlgorithmId::TransactionalCpuLod;
                Require(transactional ? orientation<0 : orientation>0,"Float face inverted or collapsed");
            }
            const auto hash=Experiment::MeshQuality::PlatformMeshHash(*mesh);
            const auto t=stats.RoamLodStats.Transactional.value_or(Algorithms::TransactionalLodStats{});
            csv<<i<<','<<Views[i]<<','<<ok<<','<<stats.TriangleCount<<','<<stats.RoamBuildSequence<<','<<hash
                <<','<<stats.RoamUpdateMilliseconds<<','<<stats.RoamCpuUploadMilliseconds<<','<<stats.RoamCpuGpuUploadBytes
                <<','<<beginMs<<','<<waitMs<<','<<renderMs<<','<<presentMs<<','<<frameMs<<','<<graphics->LastGpuFrameMilliseconds()
                <<','<<stats.RoamSplitCount<<','<<stats.RoamMergeCount<<','<<(stats.RoamLodStats.Transactional ? int(t.Status) : -1)
                <<','<<t.Updated<<','<<t.ColdStart<<','<<t.SeedTriangles<<','<<t.Samples<<','<<t.RawCandidates<<','<<t.Examined
                <<','<<t.Receivers<<','<<t.Need<<','<<t.Feasible<<','<<t.Exchanges<<','<<t.FreeExecuted<<','<<t.PairChecks
                <<','<<t.Conflicts<<','<<t.DonorReuse<<','<<t.SampleTouches<<','<<t.SampleEvaluations<<','<<t.VertexWrites<<','<<t.IndexWrites
                <<','<<t.SeedMilliseconds<<','<<t.InitializeMilliseconds<<','<<t.ViewMilliseconds<<','<<t.ReceiverMilliseconds
                <<','<<t.DonorMilliseconds<<','<<t.ReservationMilliseconds<<','<<t.TopologyPrepareMilliseconds<<','<<t.TopologyPublishMilliseconds
                <<','<<t.SampleRepairMilliseconds<<','<<t.MeshPrepareMilliseconds<<','<<t.ContinuationPublishMilliseconds<<','<<t.AdapterMilliseconds<<'\n';
            csv.flush();
            if (exporting && (i==0 || i==2 || i==15 || i==16 || i==23))
                Experiment::MeshQuality::WritePlatformMesh(output/("mesh-"+std::to_string(i)+".bin"),*mesh,
                    view.Projection*view.View,1280,720,view.UsesZeroToOneDepth);
            Require(ok,"Update failed: "+error);
        }
        cleanup();return 0;
    }
    catch (const std::exception& e) { std::cerr<<e.what()<<'\n';cleanup();return 1; }
}
}
