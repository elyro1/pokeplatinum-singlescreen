#include <nitro.h>
#include <simulator/sim.h>
#include <simulator/sim_gui.hpp>
#include <simulator/imgui/imgui.hpp>

#include "sim_gui_prj.hpp"

#include "port/sim_config_prj.h"
#include "port/field_lights.h"
#include "port/field_visuals.h"
#include "port/field_visual_effects.h"
extern "C" {
#include "rtc.h"
}

#include "bwbattle/bwbattle.h"

#include <string>

namespace SIM::GUI {

static void ConfigCheckBox(const char *label, BOOL *configVar, bool &configChanged)
{
    bool checked = *configVar != FALSE;
    if (ImGui::Checkbox(label, &checked)) {
        *configVar = checked ? TRUE : FALSE;
        configChanged = true;
    }
}

static std::string sBw2ExtractMessage;

// Black 2 / White 2's battle view (docs/bwbattle.md): the setting, and the extraction of its assets from the player's
// own ROM
static void Bw2BattleVisualsConfig(SIM_Config_prj_type *myConfig, bool &configChanged)
{
    bool ready = BwBattle_AssetsReady();

    ImGui::BeginDisabled(!ready);
    ConfigCheckBox("BW2 battle visuals", &myConfig->bw2BattleVisuals, configChanged);
    ImGui::EndDisabled();
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
        ImGui::SetTooltip("Show battles with Pokemon Black 2 / White 2's 3D arena and animated sprites.\n"
            "Needs graphics from your own US Black 2 or White 2 ROM. Takes effect from the next battle.");
    }
    ImGui::SameLine();
    if (ImGui::Button(ready ? "Re-extract BW2 ROM..." : "Select BW2 ROM...")) {
        OpenFileDialog([](std::string path) {
            sBw2ExtractMessage = BwBattle_ExtractResultMessage(BwBattle_ExtractAssets(path.c_str()));
        });
    }
    if (!sBw2ExtractMessage.empty()) {
        ImGui::TextUnformatted(sBw2ExtractMessage.c_str());
    } else if (!ready) {
        ImGui::TextUnformatted("BW2 battle graphics not extracted.");
    }
    if (ready && !BwBattle_IrisAssetsReady()) {
        ImGui::TextUnformatted("Re-extract your BW2 ROM to enable Iris on Iron Island.");
    } else if (ready && !BwBattle_IrisVsReady()) {
        ImGui::TextUnformatted("Re-extract your BW2 ROM to enable Iris's VS portrait.");
    }
}

void ConfigPrjMain(bool * openState) {
    ImGui::Begin("pokeplatinum Config", openState);
    SIM_Config_prj_type * myConfig = SIM_Config_prj_GetConfig();
    bool configChanged = false;

    int frameRate = myConfig->enable60fps ? 1 : 0;
    if (ImGui::Combo("Frame rate", &frameRate, "30 FPS\0" "60 FPS\0")) {
        myConfig->enable60fps = frameRate == 1;
        configChanged = true;
    }
    ImGui::BeginDisabled(!myConfig->enable60fps);
    int pacing = myConfig->battlePacing;
    if (ImGui::Combo("Battle pacing", &pacing, "Original\0Smooth\0Quick\0")) {
        myConfig->battlePacing = static_cast<BattlePacing>(pacing);
        configChanged = true;
    }
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
        ImGui::SetTooltip("Original: original animation timing.\n"
            "Smooth: original animation timing with sprite interpolation.\n"
            "Quick: faster battle animations.\n"
            "UI timing is controlled separately. At 30 FPS all presets use original timing.\n"
            "Interpolation adds up to one animation tick of visual delay.");
    }
    ConfigCheckBox("Normal Battle UI & Timing", &myConfig->battleUiSpeedFix, configChanged);
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
        ImGui::SetTooltip("Keep HP/EXP bars, messages and battle UI delays at normal speed at 60 FPS, independently of Battle pacing.");
    }
    ImGui::EndDisabled();
    Bw2BattleVisualsConfig(myConfig, configChanged);
    ConfigCheckBox("Enable GF_Asserts", &myConfig->enableAsserts, configChanged);
    ConfigCheckBox("DebugBreak on GF assertion fail", &myConfig->breakDebuggerOnGfAssert, configChanged);

    if (configChanged) {
        SIM_Config_prj_SaveConfigFile(myConfig);
    }
    ImGui::End();
}

void GraphicsPrjMain(bool *openState)
{
    ImGui::Begin("pokeplatinum Graphics", openState);
    SIM_Config_prj_type *myConfig = SIM_Config_prj_GetConfig();
    bool configChanged = false;

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
    if (ImGui::Button("Reload light table"))
        FieldLights_Reload();
    ImGui::SameLine();
    ImGui::TextDisabled("%s", FieldLights_Status());
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
    ImGui::SameLine();
    if (ImGui::Button("Use real time")) {
        FieldVisuals_ResetTimePreview();
    }
    ImGui::TextWrapped("Lighting only; resets on restart. Resume game logic if paused to see the preview.");

    if(configChanged) {
        SIM_Config_prj_SaveConfigFile(myConfig);
    }


    ImGui::End();
}
}
