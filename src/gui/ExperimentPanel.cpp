#include "gui/ExperimentPanel.h"
#include <imgui.h>

namespace ParallelRoam::Gui
{
ExperimentPanelCommand ExperimentPanel::Draw(
    std::span<const Experiment::Infrastructure::TerrainAsset> assets,
    std::span<const Experiment::Infrastructure::MaterialPreset> materials,
    const std::filesystem::path& current, const std::string& error,
    std::size_t keys, std::size_t cursor, std::size_t frameCount)
{
    ExperimentPanelCommand command;
    ImGui::SetNextWindowPos(ImVec2(910, 20), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(345, 430), ImGuiCond_FirstUseEver);
    if (ImGui::Begin("实验资产与路线"))
    {
        if (ImGui::BeginCombo("地形（重置LOD）", current.filename().string().c_str()))
        {
            for (std::size_t index = 0; index < assets.size(); ++index)
                if (ImGui::Selectable(assets[index].Name.c_str(), assets[index].Path == current))
                    command.Terrain = static_cast<int>(index);
            ImGui::EndCombo();
        }
        if (ImGui::BeginCombo("观察材质", "选择预设"))
        {
            for (std::size_t index = 0; index < materials.size(); ++index)
                if (ImGui::Selectable(materials[index].Name.c_str()))
                    command.Material = static_cast<int>(index);
            ImGui::EndCombo();
        }
        ImGui::TextWrapped("材质只改变观察方式；切换地形会重置LOD");
        ImGui::Separator();
        ImGui::Text("关键帧 %zu | 播放 %zu / %zu", keys, cursor, frameCount);
        ImGui::SliderInt("停留机会", &_hold, 1, 64);
        command.Hold = _hold;
        command.Record = ImGui::Button("记录当前姿态");
        ImGui::SameLine(); command.Remove = ImGui::Button("删除最后点");
        command.Export = ImGui::Button("导出配方与冻结行");
        ImGui::InputText("路线CSV", _cameraPath, sizeof(_cameraPath));
        command.CameraPath = _cameraPath;
        command.Load = ImGui::Button("载入");
        ImGui::SameLine(); command.Play = ImGui::Button("播放");
        ImGui::SameLine(); command.Pause = ImGui::Button("暂停");
        command.Step = ImGui::Button("单步");
        ImGui::SameLine(); command.Restart = ImGui::Button("重放起点");
        ImGui::SameLine(); command.Stop = ImGui::Button("自由飞行");
        if (!error.empty()) ImGui::TextWrapped("%s", error.c_str());
    }
    ImGui::End();
    return command;
}
}
