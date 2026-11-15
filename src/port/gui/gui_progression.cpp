#include <simulator/imgui/imgui.hpp>

#define POKEPLATINUM_GENERATED_LOOKUP
#define POKEPLATINUM_GENERATED_LOOKUP_IMPL
#include "generated/vars_flags.h"

#include "gui_progression_utils.h"
#include "sim_gui_prj.hpp"

#include <algorithm>

namespace SIM::GUI {
void ProgressionContents()
{
    ImGui::TextWrapped("Changes affect the current save. Save in-game to keep them. Progression shortcuts do not complete story events.");
    ImGui::SeparatorText("Gym badges");
    static const char *badges[] = { "Coal", "Forest", "Cobble", "Fen", "Relic", "Mine", "Icicle", "Beacon" };
    for (int i = 0; i < 8; i++) {
        ImGui::PushID(i);
        bool owned = Progression_GetBadge(i);
        ImGui::Text("%s: %s", badges[i], owned ? "obtained" : "missing");
        ImGui::SameLine();
        ImGui::BeginDisabled(owned);
        if (ImGui::Button("Give")) Progression_GiveBadge(i);
        ImGui::EndDisabled();
        ImGui::PopID();
    }
    if (ImGui::Button("Give all badges")) {
        for (int i = 0; i < 8; i++) Progression_GiveBadge(i);
    }
    ImGui::SeparatorText("Milestones");
    ImGui::Text("Pokedex: %s", Progression_HasDex(0) ? "obtained" : "missing");
    ImGui::SameLine();
    ImGui::BeginDisabled(Progression_HasDex(0));
    if (ImGui::Button("Give Pokedex")) Progression_GiveDex(0);
    ImGui::EndDisabled();
    ImGui::Text("National Dex: %s", Progression_HasDex(1) ? "obtained" : "missing");
    ImGui::SameLine();
    ImGui::BeginDisabled(Progression_HasDex(1));
    if (ImGui::Button("Give National Dex")) Progression_GiveDex(1);
    ImGui::EndDisabled();
    ImGui::Text("Game completed flag: %s", Progression_IsGameCompleted() ? "set" : "not set");
    ImGui::SameLine();
    ImGui::BeginDisabled(Progression_IsGameCompleted());
    if (ImGui::Button("Set completed flag")) Progression_SetGameCompleted();
    ImGui::EndDisabled();
    if (ImGui::Button("Unlock Fly destinations")) Progression_UnlockFlyLocations();
    ImGui::TextWrapped("Fly destinations are recorded as visited and can also affect other game events.");

    if (ImGui::CollapsingHeader("Advanced: saved flags and variables")) {
        ImGui::TextWrapped("Editing these values can change story state. IDs are decimal; map-local values may reset when changing maps.");
        static int flag = 1;
        static int var = -1;
        static int value = 0;
        if (var < 0) var = Progression_VarStart();
        static ImGuiTextFilter filter;
        filter.Draw("Find a flag or variable by name");
        if (filter.IsActive() && ImGui::BeginChild("FlagSearch", ImVec2(0, 150), true)) {
            for (long i = 0; i < lengthof__VarFlag; i++) {
                const auto &entry = lookup__VarFlag[i];
                bool isFlag = entry.value > 0 && entry.value < Progression_FlagLimit();
                bool isVar = entry.value >= Progression_VarStart() && entry.value < Progression_VarEnd();
                if ((!isFlag && !isVar) || !filter.PassFilter(entry.def)) continue;
                if (ImGui::Selectable(entry.def)) {
                    if (isFlag) flag = entry.value;
                    else var = entry.value;
                }
            }
        }
        if (filter.IsActive()) ImGui::EndChild();
        ImGui::InputInt("Flag ID", &flag);
        flag = std::clamp(flag, 1, Progression_FlagLimit() - 1);
        bool set = Progression_GetFlag(flag);
        if (ImGui::Checkbox("Flag set", &set)) Progression_SetFlag(flag, set);
        ImGui::TextDisabled("Saved flag IDs: 1-%d", Progression_FlagLimit() - 1);
        if (ImGui::InputInt("Variable ID", &var)) {
            var = std::clamp(var, Progression_VarStart(), Progression_VarEnd() - 1);
        }
        ImGui::Text("Current value: %d", Progression_GetVar(var));
        ImGui::InputInt("New value", &value);
        value = std::clamp(value, 0, 65535);
        if (ImGui::Button("Set variable")) Progression_SetVar(var, value);
        ImGui::TextDisabled("Saved variable IDs: %d-%d", Progression_VarStart(), Progression_VarEnd() - 1);
    }
}
}
