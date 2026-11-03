#include <simulator/sim_gui.hpp>
#include <simulator/imgui/imgui.hpp>
#include "port/gameplay_config_editor.hpp"
#include "sim_gui_prj.hpp"

#include <algorithm>
#include <cstring>

namespace SIM::GUI {
void GameplayContents()
{
    static GameplayConfigEditor editor;
    static bool attemptedLoad = false;
    if (!attemptedLoad) {
        editor.Load();
        attemptedLoad = true;
    }

    const bool pending = editor.IsDirty() || editor.NeedsRestart();
    if (pending) ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.82f, 0.25f, 1.0f));
    ImGui::TextWrapped("%s", editor.IsDirty() ? "Unsaved changes - save for next restart." :
        editor.NeedsRestart() ? "Saved changes apply after restart." : "Changes apply after restart.");
    if (pending) ImGui::PopStyleColor();
    ImGui::BeginDisabled(!editor.IsDirty());
    if (ImGui::Button("Save changes")) editor.Save();
    ImGui::EndDisabled();
    if (ImGui::GetContentRegionAvail().x > ImGui::GetFontSize() * 19) ImGui::SameLine();
    if (ImGui::Button("Reload / discard")) editor.Load();
    if (ImGui::GetContentRegionAvail().x > ImGui::GetFontSize() * 16) ImGui::SameLine();
    ImGui::BeginDisabled(!editor.IsLoaded());
    if (ImGui::Button("Defaults")) editor.RestoreDefaults();
    ImGui::EndDisabled();
    if (!editor.error.empty()) ImGui::TextWrapped("%s", editor.error.c_str());
    if (!editor.IsLoaded()) return;

    // Keep Save/Discard visible while the options scroll inside the page.
    ImGui::BeginChild("GameplayOptions");
    const char *groups[] = {"Battle rules", "Moves & tutors", "Evolution & eggs",
                            "Encounters & events", "Progression & prices"};
    for (const char *group : groups) {
        if (!ImGui::CollapsingHeader(group, ImGuiTreeNodeFlags_DefaultOpen)) continue;
        if (!BeginSettings(group)) continue;
        for (unsigned i = 0; i < GameplayConfig_OptionCount(); ++i) {
            GameplayConfigOption option;
            GameplayConfig_GetOption(i, &option);
            if (std::strcmp(option.group, group)) continue;
            SettingsRow(option.label, option.description);
            ImGui::PushID(option.name);
            unsigned value = GameplayConfig_GetOptionValue(&editor.values, i);
            if (option.isBoolean) {
                bool checked = value != 0;
                if (ImGui::Checkbox("##Value", &checked))
                    GameplayConfig_SetOptionValue(&editor.values, i, checked);
            } else {
                int number = static_cast<int>(value);
                const int maximum = static_cast<int>(option.maximum);
                const bool changed = ImGui::SliderInt("##Value", &number, 0, maximum,
                    "%d", ImGuiSliderFlags_AlwaysClamp);
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("Ctrl+click to type an exact value.");
                if (changed)
                    GameplayConfig_SetOptionValue(&editor.values, i,
                        std::clamp(number, 0, maximum));
                ImGui::TextDisabled("Range: 0-%u", option.maximum);
            }
            ImGui::PopID();
        }
        ImGui::EndTable();
    }
    ImGui::EndChild();
}
}
