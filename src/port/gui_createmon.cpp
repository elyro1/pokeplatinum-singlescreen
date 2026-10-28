#include <nitro.h>
#include <simulator/sim.h>
#include <simulator/sim_gui.hpp>
#include <simulator/imgui/imgui.hpp>

#include "sim_gui_prj.hpp"

#include "port/sim_config_prj.h"

#include "species.h"
#include "gui_createmon_utils.h"

#include <string>
#include <vector>
#include <algorithm>
#include <cstdlib>

namespace SIM::GUI {

static std::vector<std::string> sSpeciesNames;
static int sSpeciesNum = 0;
static int sLevel = 1;


void CreateMonInit() {
    sSpeciesNames.clear();
    sSpeciesNames.push_back("None");

    for(int i=1; i <= SPECIES_ARCEUS; i++) {
        const char * pokemonName = GUI_CreateMon_GetSpeciesName(i);
        sSpeciesNames.push_back(std::string(pokemonName));
        std::free((void *)pokemonName);
    }
}

void CreateMonContents() {

    if (!BeginSettings("CreatePokemon")) return;
    SettingsRow("Species");
    ImGui::Combo("##Species", &sSpeciesNum, [](void * vec, int idx){
        std::vector<std::string>* vector = reinterpret_cast<std::vector<std::string>*>(vec);
        if(idx < 0 || idx >= static_cast<int>(vector->size())) {
            return "";
        }

        return vector->at(idx).c_str();
    }, reinterpret_cast<void*>(&sSpeciesNames), sSpeciesNames.size());

    SettingsRow("Level");
    ImGui::InputInt("##Level", &sLevel);
    sLevel = std::clamp(sLevel, 1, 100);
    ImGui::EndTable();
    static const char *result = "";
    ImGui::BeginDisabled(sSpeciesNum == 0);
    if(ImGui::Button("Create Pokemon")) {
        result = GUI_CreateMon_GenPokemon(sSpeciesNum, sLevel)
            ? "Pokemon added to your party. Save in-game to keep it." : "Your party is full.";
    }
    ImGui::EndDisabled();
    ImGui::TextWrapped("%s", result);
}
}
