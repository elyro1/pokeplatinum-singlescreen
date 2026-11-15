#include <nitro.h>
#include <simulator/sim.h>
#include <simulator/sim_gui.hpp>
#include <simulator/imgui/imgui.hpp>

#include "sim_gui_prj.hpp"

#include "gui_mapjump_utils.h"

namespace SIM::GUI {

static int sMapId = 0;

static void WarpButton(const char * label, int mapId, int x, int z) {
    ImGui::TableNextColumn();
    if(ImGui::Button(label, ImVec2(-1, 0))) {
        sMapId = mapId;
        GUI_MapJump_JumpToMap(mapId, x, z);
    }
}

void MapJumpContents() {
    if (ImGui::BeginTable("Destinations", ImGui::GetContentRegionAvail().x > ImGui::GetFontSize() * 28 ? 2 : 1)) {
        WarpButton("Jubilife City", 3, 180, 777);
        WarpButton("Oreburgh City", 45, 303, 757);
        WarpButton("Eterna City", 65, 305, 531);
        WarpButton("Hearthome City", 86, 465, 698);
        WarpButton("Veilstone City", 132, 717, 612);
        WarpButton("Pastoria City", 120, 600, 816);
        WarpButton("Canalave City", 33, 58, 723);
        WarpButton("Snowpoint City", 165, 379, 234);
        WarpButton("Sunnyshore City", 150, 860, 785);
        WarpButton("Pokemon League", 172, 847, 560);
        WarpButton("Fight Area", 188, 647, 430);
        WarpButton("Resort Area", 457, 802, 473);
        WarpButton("Survival Area", 450, 659, 339);
        WarpButton("Battle Frontier", 559, 49, 35);
        ImGui::EndTable();
    }
    ImGui::Separator();

    if (BeginSettings("MapID")) {
        SettingsRow("Map ID");
        ImGui::InputInt("##MapID", &sMapId, 1, 10);
        ImGui::EndTable();
    }

    if(ImGui::Button("Go")) {
        GUI_MapJump_JumpToMap(sMapId, 0, 0);
    }
}
}
