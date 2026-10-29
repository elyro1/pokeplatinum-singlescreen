#include <nitro.h>
#include <simulator/sim_gui.hpp>
#include <simulator/imgui/imgui.hpp>
#include "sim_gui_prj.hpp"
#include "port/debug_field.h"

namespace SIM::GUI {
enum class Tool { Cheats, Pokemon, Items, Map };
static Tool sTool = Tool::Cheats;
static bool sMonInitialized;
static bool sItemsInitialized;

bool ProjectBack(Page page) {
    if (page != Page::Tools || sTool == Tool::Cheats) return false;
    sTool = Tool::Cheats;
    return true;
}

static void ToolsContents() {
    if (sTool != Tool::Cheats) {
        if (ImGui::Button("< Back to cheats & tools")) sTool = Tool::Cheats;
        ImGui::Separator();
    }
    const bool fieldReady = DEBUG_CanUseFieldTools();
    if (sTool == Tool::Cheats) {
        CheatsContents();
        ImGui::SeparatorText("Creation & travel");
        if (!fieldReady)
            ImGui::TextWrapped("Load a game and return to the overworld to use these tools.");
        ImGui::BeginDisabled(!fieldReady);
        if (ImGui::Button("Create Pokemon", ImVec2(-1, 0))) sTool = Tool::Pokemon;
        if (ImGui::Button("Give items", ImVec2(-1, 0))) sTool = Tool::Items;
        if (ImGui::Button("Jump to map", ImVec2(-1, 0))) sTool = Tool::Map;
        ImGui::EndDisabled();
        return;
    }
    if (!fieldReady) {
        ImGui::TextWrapped("Return to the overworld to use this tool.");
        return;
    }
    switch (sTool) {
    case Tool::Pokemon:
        if (!sMonInitialized) { CreateMonInit(); sMonInitialized = true; }
        ImGui::SeparatorText("Create Pokemon");
        CreateMonContents();
        break;
    case Tool::Items:
        if (!sItemsInitialized) { CreateItemInit(); sItemsInitialized = true; }
        ImGui::SeparatorText("Give items");
        CreateItemContents();
        break;
    case Tool::Map:
        ImGui::SeparatorText("Jump to map");
        MapJumpContents();
        break;
    default: break;
    }
}

void ProjectPage(Page page) {
    switch (page) {
    case Page::Graphics: GraphicsContents(); break;
    case Page::GameSpeed: GameSpeedContents(); break;
    case Page::Gameplay: GameplayContents(); break;
    case Page::Tools: ToolsContents(); break;
    case Page::Developer: DeveloperContentsPrj(); break;
    default: break;
    }
}
}
