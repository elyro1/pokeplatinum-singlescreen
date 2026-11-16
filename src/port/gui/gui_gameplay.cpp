#include <simulator/sim_gui.hpp>
#include <simulator/imgui/imgui.hpp>
#include "port/gui/gameplay_config_editor.hpp"
#include "sim_gui_prj.hpp"

#include <algorithm>
#include <cstring>

namespace SIM::GUI {
namespace {
void PresetButtons(GameplayConfigEditor &editor)
{
    static GameplayPreset pending = GAMEPLAY_PRESET_COUNT;
    const GameplayPreset current = editor.MatchingPreset();
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted("Preset:");
    for (int i = 0; i < GAMEPLAY_PRESET_COUNT; ++i) {
        const auto preset = static_cast<GameplayPreset>(i);
        const char *name = GameplayConfig_PresetName(preset);
        if (ImGui::GetContentRegionAvail().x > ImGui::CalcTextSize(name).x + ImGui::GetFontSize() * 2)
            ImGui::SameLine();
        ImGui::BeginDisabled(preset == current);
        if (ImGui::Button(name)) {
            // Only custom values can be lost, so only they need a confirmation.
            if (current == GAMEPLAY_PRESET_COUNT) {
                pending = preset;
                ImGui::OpenPopup("Replace custom settings?");
            } else {
                editor.ApplyPreset(preset);
            }
        }
        ImGui::EndDisabled();
    }
    if (ImGui::BeginPopupModal("Replace custom settings?", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::Text("Replace your custom gameplay settings with %s?", GameplayConfig_PresetName(pending));
        if (ImGui::Button("Replace")) {
            editor.ApplyPreset(pending);
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel")) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
}
}

void GameplayContents()
{
    static GameplayConfigEditor editor;
    static bool attemptedLoad = false;
    if (!attemptedLoad) {
        editor.Load();
        attemptedLoad = true;
    }

    if (!editor.error.empty()) {
        ImGui::TextWrapped("%s", editor.error.c_str());
        // Changes save immediately, so reloading only matters after an outside edit or a bad file.
        if (ImGui::Button("Reload file")) editor.Load();
    }
    if (!editor.IsLoaded()) return;

    const bool restart = editor.NeedsRestart();
    if (restart) ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.82f, 0.25f, 1.0f));
    ImGui::TextWrapped("%s", restart ? "Saved. Restart the game to apply the changes." :
        "Changes save automatically and apply after a restart.");
    if (restart) ImGui::PopStyleColor();
    PresetButtons(editor);

    // Keep the presets visible while the options scroll inside the page.
    ImGui::BeginChild("GameplayOptions");
    const char *groups[] = {"Evolution & eggs", "Encounters & events", "Progression & prices",
                            "Moves & tutors", "Battle rules"};
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
            bool changed = false;
            if (option.isBoolean) {
                bool checked = value != 0;
                if (ImGui::Checkbox("##Value", &checked))
                    changed = GameplayConfig_SetOptionValue(&editor.values, i, checked);
            } else {
                int number = static_cast<int>(value);
                const int maximum = static_cast<int>(option.maximum);
                ImGui::SliderInt("##Value", &number, 0, maximum, "%d", ImGuiSliderFlags_AlwaysClamp);
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("Range: 0-%u. Ctrl+click to type an exact value.", option.maximum);
                GameplayConfig_SetOptionValue(&editor.values, i, std::clamp(number, 0, maximum));
                // Save once the drag or typed value is finished, not on every step.
                changed = ImGui::IsItemDeactivatedAfterEdit();
            }
            if (changed && editor.IsDirty()) editor.Save();
            ImGui::PopID();
        }
        ImGui::EndTable();
    }
    ImGui::EndChild();
}
}
