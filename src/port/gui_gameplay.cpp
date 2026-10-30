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

    ImGui::TextWrapped("%s", editor.IsDirty() ? "Unsaved changes - save for next restart." :
        editor.NeedsRestart() ? "Saved changes apply after restart." : "Changes apply after restart.");
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
                            "Encounters & events", "Progression & prices", "Developer"};
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
                if (ImGui::InputInt("##Value", &number, 1, 10))
                    GameplayConfig_SetOptionValue(&editor.values, i,
                        std::clamp(number, 0, static_cast<int>(option.maximum)));
            }
            ImGui::PopID();
        }
        ImGui::EndTable();
    }
    ImGui::EndChild();
}
}
