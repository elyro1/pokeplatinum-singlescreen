#include <nitro.h>
#include <simulator/sim.h>
#include <simulator/sim_gui.hpp>
#include <simulator/imgui/imgui.hpp>

#include "sim_gui_prj.hpp"

#include "port/sim_config_prj.h"

#include "generated/items.h"
#include "gui_createitem_utils.h"


#include <string>
#include <vector>
#include <algorithm>
#include <cstdlib>

namespace SIM::GUI {

static std::vector<std::string> sItemNames;
static int sItemNum = 0;
static int sQuantity = 1;


void CreateItemInit() {
    sItemNames.clear();
    sItemNames.push_back("None");

    for(int i=1; i < MAX_ITEMS; i++) {
        const char * itemName = GUI_CreateItem_GetItemName(i);
        sItemNames.push_back(std::string(itemName));
        std::free((void *)itemName);
    }
}

void CreateItemContents() {

    if (!BeginSettings("GiveItems")) return;
    SettingsRow("Item");
    ImGui::Combo("##Item", &sItemNum, [](void * vec, int idx){
        std::vector<std::string>* vector = reinterpret_cast<std::vector<std::string>*>(vec);
        if(idx < 0 || idx >= static_cast<int>(vector->size())) {
            return "";
        }

        return vector->at(idx).c_str();
    }, reinterpret_cast<void*>(&sItemNames), sItemNames.size());

    SettingsRow("Quantity");
    ImGui::InputInt("##Quantity", &sQuantity);
    sQuantity = std::clamp(sQuantity, 1, 999);
    ImGui::EndTable();
    ImGui::BeginDisabled(sItemNum == 0);

    if(ImGui::Button("Give items")) {
        GUI_CreateItem_GenItems(sItemNum, sQuantity);
    }
    ImGui::EndDisabled();
    ImGui::TextWrapped("Save in-game to keep added items.");
}
}
