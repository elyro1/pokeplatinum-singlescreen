#include <nitro.h>
#include <simulator/sim.h>
#include <simulator/sim_gui.hpp>
#include <simulator/imgui/imgui.hpp>

#include "sim_gui_prj.hpp"

#include "port/sim_config_prj.h"

#include "generated/items.h"
#include "constants/items.h"
#include "gui_createitem_utils.h"


#include <string>
#include <vector>
#include <algorithm>
#include <cstdlib>

namespace SIM::GUI {

static std::vector<std::string> sItemNames;
static std::vector<int> sPocketItems[POCKET_MAX];
static int sPocket = 0;
static int sItemNum = 0;
static int sQuantity = 1;


void CreateItemInit() {
    sItemNames.clear();
    for (auto &items : sPocketItems) items.clear();
    sItemNames.push_back("None");

    for(int i=1; i < MAX_ITEMS; i++) {
        const char * itemName = GUI_CreateItem_GetItemName(i);
        sItemNames.push_back(std::string(itemName));
        std::free((void *)itemName);
        int pocket = GUI_CreateItem_GetPocket(i);
        if (pocket >= 0 && pocket < POCKET_MAX) sPocketItems[pocket].push_back(i);
    }
}

void CreateItemContents() {

    if (!BeginSettings("GiveItems")) return;
    static const char *pockets[] = { "Items", "Medicine", "Poke Balls", "TMs & HMs", "Berries", "Mail", "Battle items", "Key items" };
    static const char *result = "";
    SettingsRow("Category");
    if (ImGui::Combo("##Category", &sPocket, pockets, POCKET_MAX)) {
        sItemNum = 0;
        result = "";
    }
    SettingsRow("Item");
    const char *preview = sItemNum ? sItemNames[sItemNum].c_str() : "Select an item";
    if (ImGui::BeginCombo("##Item", preview)) {
        for (int id : sPocketItems[sPocket]) {
            ImGui::PushID(id);
            if (ImGui::Selectable(sItemNames[id].c_str(), id == sItemNum)) {
                sItemNum = id;
                result = "";
            }
            ImGui::PopID();
        }
        ImGui::EndCombo();
    }

    SettingsRow("Quantity");
    ImGui::InputInt("##Quantity", &sQuantity);
    sQuantity = std::clamp(sQuantity, 1, 999);
    ImGui::EndTable();
    ImGui::BeginDisabled(sItemNum == 0);

    if(ImGui::Button("Give items")) {
        result = GUI_CreateItem_GenItems(sItemNum, sQuantity)
            ? "Items added. Save in-game to keep them."
            : "Could not add items. The pocket may be full or the quantity may exceed the limit.";
    }
    ImGui::EndDisabled();
    ImGui::TextWrapped("%s", result);
}
}
