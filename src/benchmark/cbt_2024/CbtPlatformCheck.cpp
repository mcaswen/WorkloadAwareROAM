#include "benchmark/cbt_2024/CbtPlatformCheck.h"

#include "algorithms/TerrainLodAlgorithmRegistry.h"
#include "algorithms/cbt_2024/Cbt2024Support.h"
#include "platform/Window.h"
#include "render/D3D12GraphicsBackend.h"
#include "render/TerrainRenderer.h"

#include <glm/ext/matrix_clip_space.hpp>
#include <glm/ext/matrix_transform.hpp>

#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <stdexcept>
#include <d3d12sdklayers.h>

namespace ParallelRoam::Benchmark
{
namespace
{
void Require(bool condition, const std::string& message)
{
    if (!condition)
    {
        throw std::runtime_error(message);
    }
}

void WriteImage(const std::filesystem::path& path, const Render::FrameCapture& image)
{
    std::ofstream stream(path, std::ios::binary);
    stream << "P6\n" << image.Width << ' ' << image.Height << "\n255\n";
    for (std::size_t offset = 0; offset < image.Rgba.size(); offset += 4U)
    {
        stream.write(reinterpret_cast<const char*>(image.Rgba.data() + offset), 3);
    }
    Require(static_cast<bool>(stream), "Cannot write captured frame");
}
}

int RunCbtPlatformCheck(int argc, char** argv)
{
    using Algorithms::TerrainLodAlgorithmId;
    using Algorithms::TerrainLodCbtCapacity;
    Platform::Window window;
    auto graphics = Render::CreateConfiguredGraphicsBackend();
    Render::TerrainRenderer renderer;
    const auto cleanup = [&] {
        if (graphics && graphics->IsValid())
        {
            graphics->WaitForGpuIdle();
        }
        renderer.Shutdown();
        if (graphics)
        {
            graphics->Shutdown();
        }
        window.Destroy();
    };

    try
    {
        Require(argc == 3, "Usage: --cbt-platform-check OUTPUT");
        const std::filesystem::path output(argv[2]);
        Require(!std::filesystem::exists(output), "Refusing to overwrite platform evidence");
        std::filesystem::create_directories(output);
        Microsoft::WRL::ComPtr<ID3D12Debug> debug;
        if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debug))))
        {
            debug->EnableDebugLayer();
        }
        std::string error;
        Require(window.Initialize(), "SDL initialization failed");
        Require(graphics && graphics->ConfigureWindow(&error), error);
        Require(window.Create("CBT platform check", 1280, 720,
            graphics->RequiredSdlWindowFlags()), "Window creation failed");
        Require(graphics->Initialize(window.NativeWindow(), &error), error);
        static_cast<void>(graphics->SetVSyncEnabled(false));
        const auto availability = Algorithms::Cbt2024::QueryCbt2024Availability(*graphics);
        Require(availability.Available, availability.UnavailableReason);
        auto* backend = dynamic_cast<Render::D3D12GraphicsBackend*>(graphics.get());
        Require(backend != nullptr, "Missing D3D12 backend");

        // 连续分配、耗尽和释放只测试后端区间，不创建第二套算法资源
        auto whole = backend->AllocateSrvDescriptorRange(Render::D3D12GraphicsBackend::ShaderVisibleDescriptorCount);
        Require(whole.IsValid() && whole.Count == Render::D3D12GraphicsBackend::ShaderVisibleDescriptorCount,
            "Fresh descriptor heap did not provide a full range");
        Require(!backend->AllocateSrvDescriptor().IsValid(), "Descriptor exhaustion was not detected");
        backend->ReleaseSrvDescriptor(whole);
        Require(!whole.IsValid(), "Released descriptor remained valid");

        Render::TerrainRenderSettings settings;
        settings.TerrainLodAlgorithm = TerrainLodAlgorithmId::Cbt2024;
        settings.RoamMaxDepth = 20;
        settings.Cbt.TriangleAreaPixels = 20.0F;
        settings.Cbt.ValidationMode = Algorithms::TerrainLodCbtValidationMode::BlockingSmoke;
        Require(renderer.Initialize(*graphics, "assets/heightmaps/Hm_Terrain_Test_129.pgm",
            "assets/textures/Tex_Terrain_Debug_Diffuse.ppm", settings, &error), error);

        Render::RenderContext view;
        view.CameraPosition = {15.0F, 19.5F, 27.0F};
        view.CameraForward = glm::normalize(glm::vec3{0.0F, 1.5F, 0.0F} - view.CameraPosition);
        view.View = glm::lookAtRH(view.CameraPosition, view.CameraPosition + view.CameraForward,
            glm::vec3{0.0F, 1.0F, 0.0F});
        view.DrawableWidth = graphics->DrawableWidth();
        view.DrawableHeight = graphics->DrawableHeight();
        view.UsesZeroToOneDepth = true;
        view.Projection = glm::perspectiveRH_ZO(glm::radians(60.0F),
            static_cast<float>(view.DrawableWidth) / static_cast<float>(view.DrawableHeight), 0.1F, 500.0F);

        std::ofstream environment(output / "environment.txt");
        environment << graphics->AdapterName() << '\n' << graphics->VersionString() << '\n';
        std::ofstream report(output / "frames.csv");
        report << "op,label,topologyGeneration,resourceGeneration,sampleGeneration,faces,cpuMs,gpuMs,faults\n";
        std::uint64_t opportunity = 0;
        const auto tick = [&](const char* label, bool capture = false) {
            SDL_Event event;
            while (SDL_PollEvent(&event))
            {
                Require(event.type != SDL_QUIT, "Check window closed");
            }
            graphics->BeginFrame();
            Require(renderer.UpdateForView(view, &error), error);
            renderer.Render(view);
            if (capture)
            {
                Require(graphics->RequestFrameCapture(opportunity), "Capture request failed");
            }
            graphics->Present();
            Require(SUCCEEDED(backend->Device()->GetDeviceRemovedReason()), "D3D12 device removed");
            const auto stats = renderer.Stats();
            const auto& cbt = stats.RoamLodStats.Cbt;
            if (settings.TerrainLodAlgorithm == TerrainLodAlgorithmId::Cbt2024)
            {
                Require(renderer.CurrentCpuMeshForDiagnostics() == nullptr, "GPU path exposed stale CPU mesh");
                const auto* gpu = renderer.CurrentGpuOutputForDiagnostics();
                Require(gpu && gpu->HasConsistentResourceContract(), "Invalid GPU output");
                Require(cbt.FaultRecoveryCount == 0U, "CBT fault recovery invalidated the check");
            }
            if (capture)
            {
                const auto image = graphics->TakeFrameCapture();
                Require(image && image->Id == opportunity, "Capture generation mismatch");
                WriteImage(output / (std::string(label) + ".ppm"), *image);
            }
            report << std::setprecision(10) << opportunity++ << ',' << label << ','
                << cbt.TopologyGeneration << ',' << cbt.ResourceGeneration << ','
                << cbt.ClassificationSampleGeneration << ',' << stats.TriangleCount << ','
                << stats.RoamUpdateMilliseconds << ',' << cbt.GpuStageSumMilliseconds << ','
                << cbt.FaultRecoveryCount << '\n';
            report.flush();
            return stats;
        };

        for (int index = 0; index < 20; ++index)
        {
            tick("adapt");
        }
        const auto adapted = tick("material", true);
        Require(adapted.TriangleCount > 6U, "CBT never refined beyond its base topology");
        settings.CbtPaused = true;
        Require(renderer.ApplySettings(settings, &error), error);
        const auto generation = adapted.RoamLodStats.Cbt.TopologyGeneration;
        Require(tick("paused").RoamLodStats.Cbt.TopologyGeneration == generation, "Pause advanced topology");
        Require(tick("paused-again").RoamLodStats.Cbt.TopologyGeneration == generation, "Pause advanced twice");
        renderer.RequestLodStep();
        Require(tick("step").RoamLodStats.Cbt.TopologyGeneration == generation + 1U, "Step did not advance once");
        settings.Wireframe = true;
        Require(renderer.ApplySettings(settings, &error), error);
        tick("wireframe", true);

        settings.CbtPaused = false;
        settings.Wireframe = false;
        Require(renderer.ApplySettings(settings, &error), error);
        view.CameraPosition = {-20.0F, 14.0F, 18.0F};
        view.CameraForward = glm::normalize(glm::vec3{0.0F, 1.5F, 0.0F} - view.CameraPosition);
        view.View = glm::lookAtRH(view.CameraPosition, view.CameraPosition + view.CameraForward, glm::vec3{0, 1, 0});
        for (int index = 0; index < 8; ++index)
        {
            tick("moved");
        }
        tick("moved-material", true);

        auto previousResource = renderer.Stats().RoamLodStats.Cbt.ResourceGeneration;
        for (int index = 0; index < 3; ++index)
        {
            renderer.ResetTerrainLodAlgorithm();
            Require(renderer.CurrentGpuOutputForDiagnostics() == nullptr, "Reset retained borrowed resources");
            const auto reset = tick("reset");
            Require(reset.RoamLodStats.Cbt.ResourceGeneration != previousResource, "Reset reused resource identity");
            previousResource = reset.RoamLodStats.Cbt.ResourceGeneration;
        }
        settings.Cbt.Capacity = TerrainLodCbtCapacity::Capacity256K;
        Require(renderer.ApplySettings(settings, &error), error);
        Require(tick("capacity").RoamLodStats.Cbt.CapacitySetting == 262144U, "Capacity setting not applied");
        Require(renderer.LoadHeightMap("assets/heightmaps/Hm_Terrain_Test_129.pgm", &error), error);
        tick("source-reload");

        settings.TerrainLodAlgorithm = TerrainLodAlgorithmId::DataOrientedCpuRoam;
        settings.RoamTriangleBudget = 4096;
        Require(renderer.ApplySettings(settings, &error), error);
        tick("return-dod");
        Require(renderer.CurrentGpuOutputForDiagnostics() == nullptr, "CPU switch retained GPU output");
        Require(renderer.CurrentCpuMeshForDiagnostics() != nullptr, "CPU switch lost its output");
        settings.TerrainLodAlgorithm = TerrainLodAlgorithmId::Cbt2024;
        settings.Cbt.Capacity = TerrainLodCbtCapacity::Capacity128K;
        Require(renderer.ApplySettings(settings, &error), error);
        tick("return-cbt");

        // 成功路径也审计设备诊断，避免只在异常发生后读取已积累的错误
        graphics->WaitForGpuIdle();
        Microsoft::WRL::ComPtr<ID3D12InfoQueue> messages;
        if (SUCCEEDED(backend->Device()->QueryInterface(IID_PPV_ARGS(&messages))))
        {
            for (std::uint64_t index = 0; index < messages->GetNumStoredMessages(); ++index)
            {
                SIZE_T size = 0;
                messages->GetMessage(index, nullptr, &size);
                std::vector<std::uint8_t> storage(size);
                auto* message = reinterpret_cast<D3D12_MESSAGE*>(storage.data());
                Require(SUCCEEDED(messages->GetMessage(index, message, &size)), "Cannot read device diagnostic");
                Require(message->Severity != D3D12_MESSAGE_SEVERITY_ERROR &&
                    message->Severity != D3D12_MESSAGE_SEVERITY_CORRUPTION, message->pDescription);
            }
        }

        cleanup();
        std::cout << "CBT platform lifecycle completed: " << opportunity << " opportunities\n";
        return 0;
    }
    catch (const std::exception& exception)
    {
        std::cerr << exception.what() << '\n';
        if (auto* backend = dynamic_cast<Render::D3D12GraphicsBackend*>(graphics.get()))
        {
            Microsoft::WRL::ComPtr<ID3D12InfoQueue> messages;
            if (backend->Device() && SUCCEEDED(backend->Device()->QueryInterface(IID_PPV_ARGS(&messages))))
            {
                for (std::uint64_t index = 0; index < messages->GetNumStoredMessages(); ++index)
                {
                    SIZE_T size = 0;
                    messages->GetMessage(index, nullptr, &size);
                    std::vector<std::uint8_t> storage(size);
                    auto* message = reinterpret_cast<D3D12_MESSAGE*>(storage.data());
                    if (SUCCEEDED(messages->GetMessage(index, message, &size)))
                    {
                        std::cerr << message->pDescription << '\n';
                    }
                }
            }
        }
        cleanup();
        return 1;
    }
}
}
