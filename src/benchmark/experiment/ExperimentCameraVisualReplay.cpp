#include "benchmark/experiment/ExperimentCameraTools.h"
#include "benchmark/experiment/ExperimentVisualArtifacts.h"
#include "experiment/infrastructure/TerrainAssetCatalog.h"
#include "render/TerrainRenderer.h"
#include "render/GraphicsBackend.h"
#include "platform/Window.h"
#include <algorithm>
#include <fstream>
#include <iostream>

namespace ParallelRoam::Benchmark::Experiment
{
int RunExperimentCameraPreview(int argc, char** argv)
{
    Platform::Window window;
    auto graphics = Render::CreateConfiguredGraphicsBackend();
    Render::TerrainRenderer renderer;
    const auto cleanup = [&] {
        if (graphics && graphics->IsValid()) graphics->WaitForGpuIdle();
        renderer.Shutdown(); graphics->Shutdown(); window.Destroy();
    };
    try
    {
        if (argc != 6) throw std::runtime_error("用法: --experiment-camera preview ASSET CSV OUTPUT");
        namespace Data = ParallelRoam::Experiment::Infrastructure;
        const auto frames = Data::LoadCameraSequence(argv[4]);
        const auto assets = Data::LoadTerrainAssetCatalog("assets/experiments/terrain_catalog.json");
        const auto asset = std::find_if(assets.begin(),assets.end(),[&](const auto& a){return a.Id==argv[3];});
        if (asset==assets.end()) throw std::runtime_error("未知路线资产");
        const std::filesystem::path output(argv[5]);
        if (std::filesystem::exists(output)) throw std::runtime_error("拒绝覆盖路线预览");
        std::filesystem::create_directories(output);
        std::string error;
        if (!window.Initialize() || !graphics->ConfigureWindow(&error) ||
            !window.Create("Frozen camera preview",frames[0].Width,frames[0].Height,graphics->RequiredSdlWindowFlags()) ||
            !graphics->Initialize(window.NativeWindow(),&error)) throw std::runtime_error(error);
        static_cast<void>(graphics->SetVSyncEnabled(false));
        graphics->RefreshDrawableSize();
        if (graphics->DrawableWidth()!=frames[0].Width || graphics->DrawableHeight()!=frames[0].Height)
            throw std::runtime_error("实际视口与冻结路线不同");
        Render::TerrainRenderSettings settings;
        settings.TerrainLodAlgorithm=Algorithms::TerrainLodAlgorithmId::DataOrientedCpuRoam;
        settings.TerrainSize=asset->TerrainSize; settings.HeightScale=asset->HeightScale;
        settings.RoamMaxDepth=asset->Resolution<=129 ? 14 : 20;
        settings.RoamTriangleBudget=20000;
        settings.RoamEnablePassEvidence=true;
        if (!renderer.Initialize(*graphics,asset->Path,"assets/textures/experiment/generated/Tex_Neutral.png",settings,&error) ||
            !renderer.ApplyMaterial("assets/textures/experiment/generated/Tex_Neutral.png",1,0,&error))
            throw std::runtime_error(error);
        renderer.ResetTerrainLodAlgorithm();
        std::ofstream record(output/"frames.csv");
        record << "frame,event,source,poseHash,projectionHash,faces,sequence,topologyHash,image\n";
        for (const auto& f:frames)
        {
            SDL_Event event;
            while (SDL_PollEvent(&event)) if (event.type==SDL_QUIT) throw std::runtime_error("预览被关闭");
            Render::RenderContext context;
            context.View=f.View; context.UsesZeroToOneDepth=graphics->UsesZeroToOneDepth();
            context.Projection=context.UsesZeroToOneDepth ? f.ProjectionZo : f.ProjectionNo;
            context.CameraPosition=f.Position; context.CameraForward=f.Forward;
            context.DrawableWidth=f.Width; context.DrawableHeight=f.Height;
            graphics->BeginFrame();
            renderer.RequestMeshRebuild();
            if (!renderer.UpdateForView(context,&error)) throw std::runtime_error(error);
            renderer.Render(context);
            // 每个机会都更新，画面仅每四机会采集；播放时不得伪称原始30FPS实测
            const bool capture = f.Index%4==0 || f.Index+1==frames.size();
            if (capture && !graphics->RequestFrameCapture(f.Index)) throw std::runtime_error("截图请求失败");
            graphics->Present();
            std::string image;
            if (capture)
            {
                const auto pixels=graphics->TakeFrameCapture();
                if (!pixels || pixels->Id!=f.Index) throw std::runtime_error("截图错帧");
                image="frame-"+std::to_string(f.Index)+".ppm";
                WriteCapturedFrame(output/image,*pixels);
            }
            const auto stats=renderer.Stats();
            record << f.Index << ',' << f.Event << ',' << f.SourceIndex << ',' << f.PoseHash << ','
                << (context.UsesZeroToOneDepth ? f.ZoHash : f.NoHash) << ',' << stats.TriangleCount << ','
                << stats.RoamBuildSequence << ',' << stats.RoamTopologyHash << ',' << image << '\n';
        }
        if (!record) throw std::runtime_error("路线记录失败");
        cleanup(); return 0;
    }
    catch(const std::exception& error) { std::cerr << error.what() << '\n'; cleanup(); return 1; }
}
}
