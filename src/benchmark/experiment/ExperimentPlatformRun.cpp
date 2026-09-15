#include "benchmark/experiment/ExperimentReplay.h"
#include "benchmark/experiment/ExperimentVisualArtifacts.h"
#include "experiment/mesh_quality/PlatformMeshArtifact.h"
#include "render/TerrainRenderer.h"
#include "render/GraphicsBackend.h"
#include "platform/Window.h"
#include <chrono>
#include <iomanip>
#include <iostream>

namespace ParallelRoam::Benchmark::Experiment
{
int RunExperimentPlatform(int argc,char** argv)
{
    Platform::Window window;
    auto graphics=Render::CreateConfiguredGraphicsBackend();
    Render::TerrainRenderer renderer;
    const auto cleanup=[&] {
        if (graphics && graphics->IsValid()) graphics->WaitForGpuIdle();
        renderer.Shutdown();if(graphics) graphics->Shutdown();window.Destroy();
    };
    try
    {
        if (argc!=4) throw std::runtime_error("用法: --experiment-run RESOLVED OUTPUT");
        const auto input=LoadReplayInput(argv[2]);
        const auto& c=input.Case;
        if (c.Mode=="profile") throw std::runtime_error("函数采集请使用Linux CPU入口和既有FPR采集器");
        const std::filesystem::path output(argv[3]);
        if (std::filesystem::exists(output)) throw std::runtime_error("拒绝覆盖回放目录");
        std::filesystem::create_directories(output);
        using Clock=std::chrono::steady_clock;
        const auto ms=[](Clock::time_point t) { return std::chrono::duration<double,std::milli>(Clock::now()-t).count(); };
        const auto setup=Clock::now();
        std::string error;
        if (!window.Initialize() || !graphics || !graphics->ConfigureWindow(&error) ||
            !window.Create("Experiment replay",static_cast<int>(c.ViewWidth),static_cast<int>(c.ViewHeight),graphics->RequiredSdlWindowFlags()) ||
            !graphics->Initialize(window.NativeWindow(),&error)) throw std::runtime_error(error);
        static_cast<void>(graphics->SetVSyncEnabled(false));
        graphics->RefreshDrawableSize();
        if (graphics->DrawableWidth()!=static_cast<int>(c.ViewWidth) ||
            graphics->DrawableHeight()!=static_cast<int>(c.ViewHeight)) throw std::runtime_error("实际视口不符");
        const bool zero=graphics->UsesZeroToOneDepth();
        if ((c.Backend=="d3d12")!=zero) throw std::runtime_error("构建后端与case不符");
        Render::TerrainRenderSettings settings;
        settings.TerrainLodAlgorithm=input.Algorithm;
        settings.TerrainSize=c.TerrainSize;settings.HeightScale=c.HeightScale;
        settings.RoamMaxDepth=static_cast<int>(c.MaxDepth);settings.RoamTriangleBudget=c.Budget;
        settings.RoamScreenSpaceSplitThresholdPixels=c.SplitPixels;
        settings.RoamScreenSpaceMergeThresholdPixels=c.MergePixels;
        settings.RoamPassPolicy=input.Settings.PassPolicy;settings.Transactional=input.Settings.Transactional;
        settings.RoamEnablePassEvidence=input.Settings.EnablePassEvidence;
        if (!renderer.Initialize(*graphics,c.HeightMapPath,c.MaterialFile,settings,&error) ||
            !renderer.ApplyMaterial(c.MaterialFile,c.MaterialTiling,c.MaterialTint,&error)) throw std::runtime_error(error);
        renderer.ResetTerrainLodAlgorithm();
        std::ofstream meta(output/"environment.txt");
        meta << std::setprecision(17) << "backend=" << graphics->Name() << "\nadapter=" << graphics->AdapterName()
             << "\nversion=" << graphics->VersionString() << "\nsetupMs=" << ms(setup)
             << "\ndecisionEvidence=public-counters\nmeshEvidence=actual-renderer-cpu-mesh\n";
        ReplayFrameWriter writer(output/"frames.csv");
        for(std::size_t i=0;i<input.Cameras.size();++i)
        {
            SDL_Event event;
            while(SDL_PollEvent(&event)) if(event.type==SDL_QUIT) throw std::runtime_error("回放被关闭");
            const auto& f=input.Cameras[i];
            Render::RenderContext context;
            context.View=f.View;context.Projection=zero ? f.ProjectionZo:f.ProjectionNo;
            context.CameraPosition=f.Position;context.CameraForward=f.Forward;
            context.DrawableWidth=f.Width;context.DrawableHeight=f.Height;context.UsesZeroToOneDepth=zero;
            renderer.RequestMeshRebuild();
            ReplayTiming timing;timing.Platform=true;
            const auto started=Clock::now();auto part=Clock::now();
            graphics->BeginFrame();timing.Begin=ms(part);timing.Wait=graphics->LastGpuWaitMilliseconds();
            if(!renderer.UpdateForView(context,&error)) throw std::runtime_error(error);
            part=Clock::now();renderer.Render(context);timing.Render=ms(part);
            const bool evidence=IsEvidenceFrame(input,i);
            const bool capture=c.Mode=="visual" && evidence;
            if(capture && !graphics->RequestFrameCapture(i)) throw std::runtime_error("截图请求失败");
            part=Clock::now();graphics->Present();timing.Present=ms(part);timing.Frame=ms(started);
            // 真实GPU读回在visual的Present中；该模式墙钟不得冒充正常计时
            part=Clock::now();
            const auto* mesh=renderer.CurrentCpuMeshForDiagnostics();
            if(!mesh) throw std::runtime_error("未发布CPU网格");
            const auto hash=ValidateReplayMesh(*mesh,input);
            std::string image,artifact;
            if(capture)
            {
                const auto pixels=graphics->TakeFrameCapture();
                if(!pixels || pixels->Id!=i) throw std::runtime_error("截图与机会身份不符");
                image="frame-"+std::to_string(i)+".ppm";WriteCapturedFrame(output/image,*pixels);
            }
            if(evidence && (c.Mode=="visual" || c.Mode=="quality"))
            {
                artifact="mesh-"+std::to_string(i)+".bin";
                ParallelRoam::Experiment::MeshQuality::WritePlatformMesh(output/artifact,*mesh,
                    context.Projection*context.View,f.Width,f.Height,zero);
            }
            timing.Evidence=ms(part);
            auto stats=renderer.Stats().RoamLodStats;
            const auto rendered=renderer.Stats();
            stats.CpuUpdateMilliseconds=rendered.RoamUpdateMilliseconds;
            stats.CpuUploadMilliseconds=rendered.RoamCpuUploadMilliseconds;
            stats.CpuGpuUploadBytes=rendered.RoamCpuGpuUploadBytes;
            writer.Append(input,i,zero,stats,*mesh,hash,timing,image,artifact);
        }
        cleanup();return 0;
    }
    catch(const std::exception& e) { std::cerr << e.what() << '\n';cleanup();return 1; }
}
}
