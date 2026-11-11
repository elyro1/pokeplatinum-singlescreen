#include <nitro.h>
#include <simulator/sim.h>
#include <simulator/sim_gui.hpp>
#include <simulator/imgui/imgui.hpp>

#include "sim_gui_prj.hpp"

#include "port/sim_config_prj.h"
#include "port/follower_assets.h"
#include "port/bw_mon_sprites.h"
#include "port/field_lights.h"
#include "port/field_visuals.h"
#include "port/field_visual_effects.h"
#include "constants/overworld_encounters.h"
extern "C" {
#include "rtc.h"
}

#include "bwbattle/bwbattle.h"

#include <string>

namespace SIM::GUI {

static void ConfigCheckBox(const char *label, BOOL *configVar, bool &configChanged, bool available = true)
{
    SettingsRow(label);
    bool checked = available && *configVar != FALSE;
    ImGui::PushID(configVar);
    if (ImGui::Checkbox("##Enabled", &checked)) {
        *configVar = checked ? TRUE : FALSE;
        configChanged = true;
    }
    ImGui::PopID();
}

static std::string sBw2ExtractMessage;

void BattleSpeedContents() {
    SIM_Config_prj_type * myConfig = SIM_Config_prj_GetConfig();
    bool configChanged = false;

    if (!BeginSettings("BattleSpeed")) return;
    ConfigCheckBox("Fast wild battle intro", &myConfig->fastWildBattleIntro, configChanged);
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Shortens eligible wild battle openings with BW2 battle visuals. Applies to the next battle.");
    ImGui::BeginDisabled(!myConfig->enable60fps);
    SettingsRow("Battle pacing", "Battle effects and presentation pauses.");
    int pacing = myConfig->battlePacing;
    if (ImGui::Combo("##Pacing", &pacing, "Smooth\0Quick\0Turbo\0")) {
        myConfig->battlePacing = static_cast<BattlePacing>(pacing);
        configChanged = true;
    }
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
        ImGui::SetTooltip("Smooth: original effect durations with sprite interpolation.\n"
            "Quick: 2x effects. Turbo: 3x effects, minimal pauses and near-instant fades.\n"
            "Text and HP/EXP bar speeds are independent. Requires 60 FPS.");
    }
    ImGui::EndDisabled();
    SettingsRow("Text speed", "Overrides the in-game text setting.");
    const PortSpeed textSpeeds[] = { PORT_SPEED_NORMAL, PORT_SPEED_FAST, PORT_SPEED_FASTER, PORT_SPEED_INSTANT };
    int textSpeed = myConfig->textSpeed == PORT_SPEED_NORMAL ? 0 : static_cast<int>(myConfig->textSpeed) + 1;
    if (ImGui::Combo("##TextSpeed", &textSpeed, "Normal\0Fast\0Faster\0Instant\0")) {
        myConfig->textSpeed = textSpeeds[textSpeed];
        configChanged = true;
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Normal matches the in-game Normal text speed. Applies in and out of battle.\n"
            "Faster reveals text twice as fast as Fast.\n"
            "Instant reveals each page immediately, keeping prompts and scripted pauses.");
    }
    SettingsRow("HP/EXP bar speed", "Damage, healing and experience.");
    const PortSpeed barSpeeds[] = { PORT_SPEED_NORMAL, PORT_SPEED_FAST, PORT_SPEED_FASTER };
    int barSpeed = myConfig->battleBarSpeed == PORT_SPEED_NORMAL ? 0 : static_cast<int>(myConfig->battleBarSpeed) + 1;
    if (ImGui::Combo("##BarSpeed", &barSpeed, "Normal\0Fast\0Faster\0")) {
        myConfig->battleBarSpeed = barSpeeds[barSpeed];
        configChanged = true;
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Normal: half the speed of Fast. Faster: shorter changes.\n"
            "Independent of battle pacing and text.");
    }
    ConfigCheckBox("Disable low-HP beep", &myConfig->disableLowHPBeep, configChanged);
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Silences the low-HP warning in Platinum and BW2 battles. Applies immediately.");
    SettingsRow("Ask for a nickname", "Automatic naming offers for caught, hatched and received Pokemon.");
    const NicknameMode nicknameModes[] = { NICKNAME_MODE_ALWAYS, NICKNAME_MODE_NEVER, NICKNAME_MODE_ASK };
    int nicknameMode = myConfig->nicknameMode == NICKNAME_MODE_ASK ? 2 : static_cast<int>(myConfig->nicknameMode) - 1;
    if (ImGui::Combo("##NicknameMode", &nicknameMode, "Always\0Never\0Ask\0")) {
        myConfig->nicknameMode = nicknameModes[nicknameMode];
        configChanged = true;
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Always: open the naming screen directly. Never: keep the species name.\n"
            "Ask: show the original Yes/No prompt. Applies to the next naming offer.");
    }
    ConfigCheckBox("Widescreen Pok\xC3\xA9" "dex", &myConfig->widescreenPokedex, configChanged);
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Turn off to use the classic Pok\xC3\xA9" "dex. Applies the next time it opens.");
    }
    ConfigCheckBox("Widescreen summary", &myConfig->widescreenSummary, configChanged);
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Turn off to use the classic Pok\xC3\xA9" "mon summary. Applies the next time it opens.");
    }
    ConfigCheckBox("Single-screen bag", &myConfig->gridBag, configChanged);
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("On: with the single-screen layout, the bag shows its items as a grid of icons\n"
            "or a list with names (X/Y). Off: the classic bag. Applies the next time the bag opens.");
    }
    SettingsRow("Pok\xC3\xA9" "dex units", "Heights and weights in the Pok\xC3\xA9" "dex.");
    int pokedexUnits = myConfig->pokedexUnits;
    if (ImGui::Combo("##PokedexUnits", &pokedexUnits, "Metric\0Imperial\0")) {
        myConfig->pokedexUnits = static_cast<PokedexUnits>(pokedexUnits);
        configChanged = true;
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Metric: 0.5 m, 6.2 kg. Imperial: 1'08\", 13.7 lbs.\n"
            "The INFO tab changes at once; other screens the next time they open.");
    }
    ImGui::EndTable();

    if (configChanged) {
        SIM_Config_prj_SaveConfigFile(myConfig);
    }
}

void GraphicsContents()
{
    SIM_Config_prj_type *myConfig = SIM_Config_prj_GetConfig();
    bool configChanged = false;

    ImGui::SeparatorText("Visual effects");
    if (!BeginSettings("GraphicsSettings")) return;
    ConfigCheckBox("Richer day/night lighting", &myConfig->enhancedDayNight, configChanged);
    ConfigCheckBox("Regional atmosphere", &myConfig->regionalAtmosphere, configChanged);
    // Keep the edge-smoothing control hidden for now. Antialias3D remains available in the INI.
    // ConfigCheckBox("Smooth 3D edges", &myConfig->antialias3D, configChanged);
    // if (myConfig->antialias3D) {
    //     int samples = SIM_Get3DAntialiasingSamples();
    //     if (samples) ImGui::TextDisabled("3D edges: %dx MSAA + downsampling", samples);
    //     else ImGui::TextDisabled("3D edges: high-resolution downsampling");
    // }
    ConfigCheckBox("Correct model lighting", &myConfig->correctLighting, configChanged);
    ConfigCheckBox("Environmental particles", &myConfig->environmentalParticles, configChanged);
    ConfigCheckBox("Soft character shadows", &myConfig->softContactShadows, configChanged);
    ConfigCheckBox("Character cast shadows", &myConfig->castShadows, configChanged);
    ConfigCheckBox("Water shimmer and ripples", &myConfig->waterEffects, configChanged);
    ConfigCheckBox("Weather effects", &myConfig->weatherEffects, configChanged);
    ConfigCheckBox("Coherent night lighting", &myConfig->nightLighting, configChanged);
    ConfigCheckBox("Night window and lamp glow", &myConfig->nightGlows, configChanged);
    ConfigCheckBox("Emissive bloom", &myConfig->emissiveBloom, configChanged);
    ImGui::EndTable();
    if (configChanged) SIM_Config_prj_SaveConfigFile(myConfig);
}

void GeneralContentsPrj()
{
    SIM_Config_prj_type *myConfig = SIM_Config_prj_GetConfig();
    bool configChanged = false;
    ImGui::SeparatorText("Black 2 / White 2 ROM features");
    bool ready = BwBattle_AssetsReady();
    if (BeginSettings("BattleVisuals")) {
        ImGui::BeginDisabled(!ready);
        ConfigCheckBox("BW2 battle visuals", &myConfig->bw2BattleVisuals, configChanged);
        ImGui::EndDisabled();
        const char *buttonLabel = ready ? "Re-extract BW2 ROM..." : "Select BW2 ROM...";
        const float buttonWidth = ImGui::CalcTextSize(buttonLabel).x + ImGui::GetStyle().FramePadding.x * 2;
        if (ImGui::GetContentRegionAvail().x >= ImGui::GetFrameHeight() + ImGui::GetStyle().ItemSpacing.x + buttonWidth)
            ImGui::SameLine();
        if (ImGui::Button(buttonLabel)) {
            OpenFileDialog([](std::string path) {
                auto result = BwBattle_ExtractAssets(path.c_str());
                FollowerAssets_Reload();
                BwMonSprites_Reload();
                sBw2ExtractMessage = BwBattle_ExtractResultMessage(result);
                if (result == BWBATTLE_EXTRACT_OK && BwBattle_AssetsReady()) {
                    auto *config = SIM_Config_prj_GetConfig();
                    if (!config->bw2BattleVisuals) {
                        config->bw2BattleVisuals = TRUE;
                        SIM_Config_prj_SaveConfigFile(config);
                    }
                }
            });
        }
        bool overworldReady = FollowerAssets_Ready();
        ImGui::BeginDisabled(!overworldReady);
        ConfigCheckBox("Overworld encounters", &myConfig->overworldEncounters, configChanged, overworldReady);
        ConfigCheckBox("Pokémon follower", &myConfig->pokemonFollower, configChanged, overworldReady);
        ImGui::EndDisabled();
        bool monSpritesReady = BwMonSprites_Ready();
        ImGui::BeginDisabled(!monSpritesReady);
        ConfigCheckBox("BW sprites", &myConfig->bwMonSprites, configChanged, monSpritesReady);
        ImGui::EndDisabled();
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
            ImGui::SetTooltip("Black 2 / White 2 art for the summary screen, the Pok\xC3\xA9" "dex and Pok\xC3\xA9" "mon icons. Applies to screens opened after changing it.");
        ImGui::EndTable();
    }
    if (!sBw2ExtractMessage.empty()) ImGui::TextWrapped("%s", sBw2ExtractMessage.c_str());
    if (!ready) ImGui::TextWrapped("BW2 graphics not extracted.");
    else if (!FollowerAssets_Ready())
        ImGui::TextWrapped("Re-extract your BW2 ROM to enable followers and overworld encounters.");
    else if (!BwMonSprites_Ready())
        ImGui::TextWrapped("Re-extract your BW2 ROM to enable BW sprites.");
    else if (!BwBattle_IrisAssetsReady() || !BwBattle_IrisVsReady())
        ImGui::TextWrapped("Re-extract your BW2 ROM to enable all Iris graphics.");


    if(configChanged) {
        SIM_Config_prj_SaveConfigFile(myConfig);
    }


}

void DeveloperContentsPrj()
{
    auto *myConfig = SIM_Config_prj_GetConfig();
    bool configChanged = false;
    ImGui::SeparatorText("Saving");
    if (BeginSettings("AutosaveSettings")) {
        ConfigCheckBox("Autosave", &myConfig->autosave, configChanged);
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Archive progress every two minutes when overworld control is safe. Keeps the newest ten autosaves per slot.");
        ImGui::EndTable();
    }
    ImGui::SeparatorText("Frame rate");
    if (BeginSettings("FrameRateSettings")) {
        SettingsRow("Frame rate");
        int frameRate = myConfig->enable60fps ? 1 : 0;
        if (ImGui::Combo("##FrameRate", &frameRate, "30 FPS\0" "60 FPS\0")) {
            myConfig->enable60fps = frameRate == 1;
            configChanged = true;
        }
        ImGui::EndTable();
    }
    ImGui::SeparatorText("Overworld encounters");
    if (BeginSettings("OverworldEncounterSettings")) {
        SettingsRow("Overworld encounter count", "Maximum visible wild Pokemon at once. 0 stops new spawns; existing Pokemon remain until they leave.");
        if (ImGui::SliderInt("##OverworldEncounterCount", &myConfig->overworldEncounterCount,
                0, OVERWORLD_ENCOUNTER_CAPACITY, "%d", ImGuiSliderFlags_AlwaysClamp))
            configChanged = true;
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Ctrl+click to type an exact value.");
        ImGui::TextDisabled("Range: 0-%d", OVERWORLD_ENCOUNTER_CAPACITY);
        ImGui::EndTable();
    }
    ImGui::SeparatorText("Assertions");
    if (BeginSettings("Assertions")) {
        ConfigCheckBox("Enable GF_Asserts", &myConfig->enableAsserts, configChanged);
        ConfigCheckBox("Break debugger on assertion", &myConfig->breakDebuggerOnGfAssert, configChanged);
        ImGui::EndTable();
    }
    if (configChanged) SIM_Config_prj_SaveConfigFile(myConfig);
    ImGui::SeparatorText("Rendering diagnostics");
    if (ImGui::Button("Reload light table"))
        FieldLights_Reload();
    ImGui::TextWrapped("%s", FieldLights_Status());
    static bool emissionOnly;
    if (ImGui::Checkbox("Show emission only", &emissionOnly))
        FieldVisuals_SetEmissionDebug(emissionOnly);
    static bool frameTimings;
    if (ImGui::Checkbox("Measure frame timings", &frameTimings))
        SIM_SetRenderGpuProfiling(frameTimings);
    if (frameTimings) {
        ImGui::TextDisabled("CPU work: %.2f ms; total frame: %.2f ms", SIM_GetRenderFrameTime() / 1000000.0, SIM_GetFullFrameTime() / 1000000.0);
        float gpu = SIM_GetRenderGpuMs();
        if (gpu < 0) ImGui::TextDisabled("GPU: waiting for timing result");
        else ImGui::TextDisabled("GPU elapsed: %.2f ms (%d frames old)", gpu, SIM_GetRenderGpuSampleAge());
        ImGui::TextWrapped("Renderer: %s", SIM_GetGraphicsRenderer());
    }
    static bool waterTimings;
    ImGui::Checkbox("Measure water timings", &waterTimings);
    SIM_SetWaterProfiling(waterTimings);
    if (waterTimings)
        ImGui::TextDisabled("Water: %.3f ms CPU, %.3f ms GPU", FieldVisualEffects_GetWaterCpuMs(), FieldVisualEffects_GetWaterCount() ? SIM_GetWaterGpuMs() : 0);
    ImGui::TextDisabled("Night sources: %d", FieldVisualEffects_GetLightCount());
    FieldNightLighting night;
    FieldVisuals_NightLighting(FieldVisuals_GetTime(GetSecondsSinceMidnight()), &night);
    ImGui::TextDisabled("Sun %.2f, moon %.2f, emission %.2f", night.sunIntensity, night.moonIntensity, night.emissionActivation);
    ImGui::TextDisabled("Visible effects: %d particles, %d water tiles, %d ripples",
        FieldVisualEffects_GetParticleCount(), FieldVisualEffects_GetWaterCount(), FieldVisualEffects_GetRippleCount());

    ImGui::Separator();
    ImGui::Text("Lighting preview: %s", FieldVisuals_GetTimePreviewName());
    if (ImGui::Button("Cycle time of day")) {
        FieldVisuals_CycleTimePreview();
    }
    if (ImGui::Button("Use real time")) {
        FieldVisuals_ResetTimePreview();
    }
    ImGui::TextWrapped("Lighting only; resets on restart. Resume game logic if paused to see the preview.");

    ImGui::TextWrapped("Project build: %s", SIM_GetProjectGitHash());
}
}
