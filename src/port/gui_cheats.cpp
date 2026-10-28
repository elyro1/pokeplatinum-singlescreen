#include <nitro.h>
#include <simulator/imgui/imgui.hpp>
#include <simulator/sim.h>
#include <simulator/sim_gui.hpp>

#include "generated/poketch_apps.h"

#include "port/debug_field.h"
#include "port/sim_config_prj.h"

#include "sim_gui_prj.hpp"

namespace SIM::GUI {

static void ConfigCheckBox(const char *label, BOOL *configVar, bool &configChanged)
{
    SettingsRow(label);
    bool value = *configVar != FALSE;
    ImGui::PushID(configVar);
    if (ImGui::Checkbox("##Enabled", &value)) {
        *configVar = value;
        configChanged = true;
    }
    ImGui::PopID();
}

void CheatsContents()
{
    SIM_Config_prj_type *myConfig = SIM_Config_prj_GetConfig();
    bool configChanged = false;

    if (!BeginSettings("CheatSettings")) return;
    ConfigCheckBox("Walk Through Walls", &myConfig->walkThroughWalls, configChanged);
    ConfigCheckBox("Disable Random Encounters", &myConfig->disableRandomEncounters, configChanged);
    ConfigCheckBox("Run from Trainer Battles", &myConfig->runFromTrainerBattles, configChanged);

    ImGui::EndTable();
    ImGui::SeparatorText("Poketch");
    int appCount = DEBUG_GetPoketchAppCount();
    ImGui::BeginDisabled(appCount < 0);
    if (ImGui::Button("Unlock all Poketch apps")) {
        DEBUG_UnlockAllPoketchApps();
        appCount = DEBUG_GetPoketchAppCount();
    }
    ImGui::EndDisabled();
    if (appCount >= 0) {
        ImGui::Text("Registered apps: %d / %d", appCount, POKETCH_APPID_MAX);
        ImGui::TextWrapped("Use L/R in the Poketch. Save in-game to keep the unlocks.");
    } else {
        ImGui::TextWrapped("Load a game and acquire the Poketch first.");
    }

    if (configChanged) {
        SIM_Config_prj_SaveConfigFile(myConfig);
    }

}
}
