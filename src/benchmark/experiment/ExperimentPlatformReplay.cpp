#include "benchmark/experiment/ExperimentPlatformReplay.h"
#include "benchmark/experiment/ExperimentVisualArtifacts.h"
#include "experiment/infrastructure/TerrainAssetCatalog.h"
#include "experiment/infrastructure/MaterialCatalog.h"
#include "platform/Window.h"
#include "render/GraphicsBackend.h"
#include "render/TerrainRenderer.h"

#include <glm/ext/matrix_transform.hpp>
#include <glm/ext/matrix_clip_space.hpp>
#include <algorithm>
#include <chrono>
#include <fstream>
#include <iomanip>
#include <iostream>

namespace ParallelRoam::Benchmark::Experiment
{
int RunExperimentAssetPreview(int argc, char** argv)
{
    Platform::Window window;
    auto graphics = Render::CreateConfiguredGraphicsBackend();
    Render::TerrainRenderer renderer;
    const auto cleanup = [&] {
        if (graphics && graphics->IsValid()) graphics->WaitForGpuIdle();
        renderer.Shutdown();
        if (graphics) graphics->Shutdown();
        window.Destroy();
    };
    try
    {
        if (argc != 4 && argc != 5) throw std::runtime_error("用法: --experiment-preview ASSET OUTPUT [materials]");
        const bool materialReview = argc == 5 && std::string(argv[4]) == "materials";
        if (argc == 5 && !materialReview) throw std::runtime_error("未知预览模式");
        namespace Data = ParallelRoam::Experiment::Infrastructure;
        const auto assets = Data::LoadTerrainAssetCatalog("assets/experiments/terrain_catalog.json");
        const auto found = std::find_if(assets.begin(), assets.end(), [&](const auto& a) { return a.Id == argv[2]; });
        if (found == assets.end()) throw std::runtime_error("未知资产");
        const auto& asset = *found;
        const std::filesystem::path output(argv[3]);
        if (std::filesystem::exists(output)) throw std::runtime_error("拒绝覆盖视觉运行");
        std::filesystem::create_directories(output);
        auto materials = Data::LoadMaterialCatalog("assets/experiments/material_catalog.json");
        if (!materialReview) materials.resize(1);
        std::string error;
        if (!window.Initialize() || !graphics->ConfigureWindow(&error) ||
            !window.Create("Experiment visual review", 960, 540, graphics->RequiredSdlWindowFlags()) ||
            !graphics->Initialize(window.NativeWindow(), &error))
            throw std::runtime_error(error);
        static_cast<void>(graphics->SetVSyncEnabled(false));
        graphics->RefreshDrawableSize();
        Render::TerrainRenderSettings settings;
        settings.TerrainLodAlgorithm = Algorithms::TerrainLodAlgorithmId::DataOrientedCpuRoam;
        settings.TerrainSize = asset.TerrainSize;
        settings.HeightScale = asset.HeightScale;
        settings.RoamMaxDepth = asset.Resolution <= 129 ? 14 : 20;
        settings.RoamTriangleBudget = 20000;
        settings.RoamEnablePassEvidence = true;
        if (!renderer.Initialize(*graphics, asset.Path, "assets/textures/Tex_Terrain_Debug_Diffuse.ppm", settings, &error))
            throw std::runtime_error(error);
        std::ofstream record(output / "frames.csv");
        record << std::setprecision(17) << "view,asset,resolution,faces,backend,image,material,sequence,topologyHash,materialMs\n";
        for (int angle = 0; angle < 2; ++angle)
        {
            Render::RenderContext view;
            const float size = asset.TerrainSize;
            view.CameraPosition = angle == 0 ? glm::vec3(0, size*.5F+asset.HeightScale, size*.9F)
                : glm::vec3(size*.9F, size*.5F+asset.HeightScale, 0);
            const glm::vec3 target(0, asset.HeightScale*.35F, 0);
            view.CameraForward = glm::normalize(target-view.CameraPosition);
            view.View = glm::lookAtRH(view.CameraPosition, target, glm::vec3(0,1,0));
            view.DrawableWidth = graphics->DrawableWidth();
            view.DrawableHeight = graphics->DrawableHeight();
            view.UsesZeroToOneDepth = graphics->UsesZeroToOneDepth();
            const float aspect = static_cast<float>(view.DrawableWidth)/static_cast<float>(view.DrawableHeight);
            view.Projection = view.UsesZeroToOneDepth
                ? glm::perspectiveRH_ZO(glm::radians(60.0F), aspect, .1F, size*10)
                : glm::perspectiveRH_NO(glm::radians(60.0F), aspect, .1F, size*10);
            for (int update = 0; update < 4; ++update)
            {
                SDL_Event event;
                while (SDL_PollEvent(&event)) if (event.type == SDL_QUIT) throw std::runtime_error("视觉窗口关闭");
                graphics->BeginFrame();
                renderer.RequestMeshRebuild();
                if (!renderer.UpdateForView(view, &error)) throw std::runtime_error(error);
                renderer.Render(view);
                graphics->Present();
            }
            const auto stable = renderer.Stats();
            for (std::size_t index = 0; index < materials.size(); ++index)
            {
                const auto& material = materials[index];
                const auto start = std::chrono::steady_clock::now();
                if (!renderer.ApplyMaterial(material.Path, material.Tiling, material.HeightTint, &error))
                    throw std::runtime_error(error);
                const double milliseconds = std::chrono::duration<double, std::milli>(
                    std::chrono::steady_clock::now()-start).count();
                const auto pathBeforeFailure = renderer.TexturePath();
                if (renderer.ApplyMaterial("missing-material-for-validation.png", 1, 0, &error) ||
                    renderer.TexturePath() != pathBeforeFailure)
                    throw std::runtime_error("失败材质未保留原资源");
                if (renderer.Stats().RoamBuildSequence != stable.RoamBuildSequence ||
                    renderer.Stats().RoamTopologyHash != stable.RoamTopologyHash)
                    throw std::runtime_error("材质切换改变了LOD状态");
                graphics->BeginFrame();
                renderer.Render(view);
                const auto id = static_cast<std::uint64_t>(angle) * 100 + index;
                if (!graphics->RequestFrameCapture(id)) throw std::runtime_error("截图请求被拒绝");
                graphics->Present();
                const auto capture = graphics->TakeFrameCapture();
                if (!capture || capture->Id != id) throw std::runtime_error("截图身份不匹配");
                const auto name = "view-" + std::to_string(angle) +
                    (materialReview ? "-" + material.Id : "") + ".ppm";
                WriteCapturedFrame(output/name, *capture);
                record << angle << ',' << asset.Id << ',' << asset.Resolution << ','
                    << stable.TriangleCount << ',' << graphics->Name() << ',' << name << ','
                    << material.Id << ',' << stable.RoamBuildSequence << ',' << stable.RoamTopologyHash
                    << ',' << milliseconds << '\n';
            }
        }
        if (!record) throw std::runtime_error("视觉记录写入失败");
        cleanup();
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        cleanup();
        return 1;
    }
}
}
