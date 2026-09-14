#include "benchmark/TransactionalPlatformProbe.h"
#include "algorithms/TerrainLodAlgorithmRegistry.h"
#include "platform/Window.h"
#include "render/GraphicsBackend.h"
#include "render/TerrainRenderer.h"
#include <glm/ext/matrix_transform.hpp>
#include <glm/ext/matrix_clip_space.hpp>
#if defined(PARALLEL_ROAM_GRAPHICS_API_OPENGL)
#include <glad/gl.h>
#endif
#include <iostream>
#include <stdexcept>

namespace ParallelRoam::Benchmark
{
namespace
{
void Require(bool condition, const std::string& error)
{
    if (!condition) throw std::runtime_error(error);
}
}

int RunTransactionalPlatformCheck()
{
    if (!Algorithms::IsTerrainLodAlgorithmAvailable(Algorithms::TerrainLodAlgorithmId::TransactionalCpuLod))
    {
        std::cerr << "Transactional runtime unavailable\n";
        return 2;
    }
    Platform::Window window;
    auto graphics = Render::CreateConfiguredGraphicsBackend();
    Render::TerrainRenderer renderer;
    std::string error;
    const auto cleanup = [&] {
        if (graphics && graphics->IsValid()) graphics->WaitForGpuIdle();
        renderer.Shutdown();
        if (graphics) graphics->Shutdown();
        window.Destroy();
    };
    try
    {
        Require(window.Initialize(), "SDL initialization failed");
        Require(graphics && graphics->ConfigureWindow(&error), error);
        Require(window.Create("Transactional platform check", 1280, 720, graphics->RequiredSdlWindowFlags()), "Window failed");
        Require(graphics->Initialize(window.NativeWindow(), &error), error);
        static_cast<void>(graphics->SetVSyncEnabled(false));
        Render::TerrainRenderSettings settings;
        settings.UseTerrainLod = false;
        settings.RoamTriangleBudget = 4096;
        Require(renderer.Initialize(*graphics, "assets/heightmaps/Hm_Terrain_Test_129.pgm",
            "assets/textures/Tex_Terrain_Debug_Diffuse.ppm", settings, &error), error);
        Render::RenderContext view;
        view.CameraPosition = {15, 19.5F, 27};
        view.CameraForward = glm::vec3{0, 1.5F, 0} - view.CameraPosition;
        view.View = glm::lookAtRH(view.CameraPosition, view.CameraPosition + view.CameraForward, glm::vec3{0, 1, 0});
        view.UsesZeroToOneDepth = graphics->UsesZeroToOneDepth();
        const auto resize = [&] {
            graphics->RefreshDrawableSize();
            view.DrawableWidth = graphics->DrawableWidth(); view.DrawableHeight = graphics->DrawableHeight();
            const float aspect = static_cast<float>(view.DrawableWidth) / static_cast<float>(view.DrawableHeight);
            view.Projection = view.UsesZeroToOneDepth
                ? glm::perspectiveRH_ZO(glm::radians(60.0F), aspect, .1F, 500.0F)
                : glm::perspectiveRH_NO(glm::radians(60.0F), aspect, .1F, 500.0F);
        };
        resize();
        std::cout << "case,sequence,faces,status,updated,cpuMs,uploadMs,bytes\n";
        const auto tick = [&](const char* label, bool expected = true) {
            graphics->BeginFrame();
            const bool ok = renderer.UpdateForView(view, &error);
            renderer.Render(view); graphics->Present();
            Require(ok == expected, std::string{label} + ": " + error);
            const auto stats = renderer.Stats();
            Require(stats.TriangleCount <= settings.RoamTriangleBudget, "Rendered budget exceeded");
            const auto& t = stats.RoamLodStats.Transactional;
            std::cout << label << ',' << stats.RoamBuildSequence << ',' << stats.TriangleCount << ','
                << (t ? static_cast<int>(t->Status) : -1) << ',' << (t && t->Updated) << ','
                << stats.RoamUpdateMilliseconds << ',' << stats.RoamCpuUploadMilliseconds << ','
                << stats.RoamCpuGpuUploadBytes << '\n';
            return stats;
        };
        // 工厂切换经过同一 renderer，确保旧借用不会随算法销毁而悬空
        for (const auto id : {Algorithms::TerrainLodAlgorithmId::ClassicCpuRoam,
            Algorithms::TerrainLodAlgorithmId::DataOrientedCpuRoam, Algorithms::TerrainLodAlgorithmId::TransactionalCpuLod})
        {
            settings.UseTerrainLod = true; settings.TerrainLodAlgorithm = id;
            Require(renderer.ApplySettings(settings, &error), error);
            renderer.RequestMeshRebuild(); tick("switch");
        }
        const auto first = tick("stationary");
        const auto second = tick("stationary-next");
        Require(second.RoamBuildSequence == first.RoamBuildSequence + 1, "Stationary batch skipped");
        settings.TransactionalPaused = true;
        Require(renderer.ApplySettings(settings, &error), error);
        Require(tick("paused").RoamBuildSequence == second.RoamBuildSequence, "Paused algorithm executed");
        renderer.RequestLodStep();
        Require(tick("step").RoamBuildSequence == second.RoamBuildSequence + 1, "Step did not execute once");
        renderer.RequestCpuMeshFullUpload();
        const auto pausedRecovery = tick("paused-resource-resync");
        Require(pausedRecovery.RoamBuildSequence == second.RoamBuildSequence + 1 &&
            pausedRecovery.RoamCpuGpuUploadBytes > 0, "Paused resource recovery ran an extra batch");
        const auto sequence = renderer.Stats().RoamBuildSequence;
        const int width = view.DrawableWidth; view.DrawableWidth = 0;
        Require(renderer.UpdateForView(view, &error) && renderer.Stats().RoamBuildSequence == sequence, "Zero drawable executed");
        view.DrawableWidth = width;
        settings.TransactionalPaused = false;
        Require(renderer.ApplySettings(settings, &error), error);
        renderer.RequestCpuMeshFullUpload();
        Require(tick("resource-resync").RoamCpuGpuUploadBytes > 0, "Full sync lost");
#if defined(PARALLEL_ROAM_GRAPHICS_API_OPENGL)
        // 注入 GL 错误，覆盖 Consume 之后上传失败；下一帧必须补齐最新完整输出
        glBindBuffer(0xFFFFFFFFU, 0);
        tick("upload-failure", false);
        Require(tick("upload-recovery").RoamCpuGpuUploadBytes > 0, "Upload recovery lost ranges");
#endif
#if defined(PARALLEL_ROAM_GRAPHICS_API_D3D12)
        renderer.FailNextCpuUploadAllocationForDiagnostics();
        tick("allocation-failure", false);
        Require(tick("allocation-recovery").RoamCpuGpuUploadBytes > 0, "Allocation recovery reused invalid capacity");
        // 两个帧槽分别追赶；全部追上后空批不再复制 CPU mesh
        tick("frame-slot-catchup");
        Require(tick("frame-slots-current").RoamCpuGpuUploadBytes == 0, "Current frame slot recopied unchanged mesh");
#endif
        SDL_SetWindowSize(window.NativeWindow(), 960, 540); resize(); tick("resize");
        settings.RoamTriangleBudget = 512;
        Require(renderer.ApplySettings(settings, &error), error);
        Require(tick("budget-lower").RoamLodStats.Transactional->ColdStart, "Budget did not reinitialize");
        Require(renderer.LoadHeightMap("assets/heightmaps/Hm_Terrain_Test_129.pgm", &error), error);
        Require(tick("source-reload").RoamLodStats.Transactional->ColdStart, "Source did not reinitialize");
        settings.TerrainLodAlgorithm = Algorithms::TerrainLodAlgorithmId::DataOrientedCpuRoam;
        Require(renderer.ApplySettings(settings, &error), error); tick("return-dod");
        cleanup();
        return 0;
    }
    catch (const std::exception& exception)
    {
        std::cerr << exception.what() << '\n'; cleanup(); return 1;
    }
}
}
