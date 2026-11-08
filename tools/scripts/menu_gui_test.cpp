// Exercise the real menu/SDL/ImGui code without loading a ROM or touching saves.
#include <simulator/sim.h>
#include <simulator/sim_gui.hpp>
#include <simulator/sim_net.h>
#include <simulator/sim_audio.h>
#include <simulator/imgui/imgui.hpp>
#include <simulator/glad/glad.h>
#include "imgui_internal.h"
#include "gui_internal.hpp"
#include "port/gameplay_config.h"
#include "port/sim_config_prj.h"

#include <cassert>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <string>
#include <vector>

static SIM_config_type config;
static SDL_Window *window;
static SIM_Net_State_t netState = SIM_NET_STATE_DISCONNECTED;
static SIM_Net_Player_t players[SIM_NET_MAX_PLAYERS];
static bool manualBack;

extern "C" {
SIM_config_type *SIM_GetConfigPtr() { return &config; }
SDL_Window *SIM_GetSDLWindow() { return window; }
u64 SIM_GetFullFrameTime() { return 16666666; }
u64 SIM_GetRenderFrameTime() { return 5000000; }
const char *SIM_GetGraphicsRenderer() { return "Menu test"; }
const char *SIM_GetLibntrGitHash() { return "test"; }
const char *SIM_GetProjectGitHash() { return "test"; }
void SIM_SetRenderGpuProfiling(BOOL) {}
float SIM_GetRenderGpuMs() { return 0; }
int SIM_GetRenderGpuSampleAge() { return 0; }
void SIM_SetWaterProfiling(BOOL) {}
float SIM_GetWaterGpuMs() { return 0; }
void SIM_SetInternalResolutionAfterRender(int scale) { config.internalResolutionScale = scale; }
void SIM_Audio_SetMasterVolume(int) {}
SIM_Net_State_t SIM_Net_GetCurrentState() { return netState; }
int SIM_Net_GetCurrentPlayerID() { return 0; }
int SIM_Net_GetPlayerCount() { return 0; }
const SIM_Net_Player_t *SIM_Net_GetPlayerList() { return players; }
BOOL SIM_Net_StartHost() { netState = SIM_NET_STATE_HOST; return TRUE; }
BOOL SIM_Net_StartClient(char *) { netState = SIM_NET_STATE_CLIENT; return TRUE; }
}
namespace SIM::GUI {
void GameplayContents();
void GraphicsContents();
void BattleSpeedContents();
void ProjectPage(Page page) {
    if (page == Page::Gameplay) GameplayContents();
    if (page == Page::Graphics) GraphicsContents();
    if (page == Page::SpeedText) BattleSpeedContents();
}
bool ProjectBack(Page) { if (!manualBack) return false; manualBack = false; return true; }
}

static bool Key(SDL_Keycode key, bool down = true, bool repeat = false) {
    SDL_Event e{};
    e.type = down ? SDL_KEYDOWN : SDL_KEYUP;
    e.key.windowID = SDL_GetWindowID(window);
    e.key.keysym.sym = key;
    e.key.keysym.scancode = SDL_GetScancodeFromKey(key);
    e.key.repeat = repeat;
    return SIM::GUI::ProcessEvent(&e);
}

static void Frame() {
    glClearColor(.035f, .045f, .065f, 1);
    glClear(GL_COLOR_BUFFER_BIT);
    SIM::GUI::NewFrame();
    SIM::GUI::Main();
    SIM::GUI::Render();
}

static void Capture(const char *name) {
    const char *directory = std::getenv("MENU_GUI_CAPTURE_DIR");
    if (!directory) return;
    int width, height;
    SDL_GL_GetDrawableSize(window, &width, &height);
    std::vector<unsigned char> pixels(width * height * 3);
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadPixels(0, 0, width, height, GL_RGB, GL_UNSIGNED_BYTE, pixels.data());
    const std::string path = std::string(directory) + "/" + name + ".ppm";
    FILE *file = std::fopen(path.c_str(), "wb");
    assert(file);
    std::fprintf(file, "P6\n%d %d\n255\n", width, height);
    for (int y = height - 1; y >= 0; --y)
        assert(std::fwrite(pixels.data() + y * width * 3, 3, width, file) == width);
    assert(std::fclose(file) == 0);
}

static ImGuiWindow *Panel() {
    return ImGui::FindWindowByName("Settings & tools###PCSettings");
}

static void CheckPanelFits() {
    auto *panel = Panel();
    assert(panel && panel->Active);
    auto *view = ImGui::GetMainViewport();
    assert(panel->Pos.x >= view->WorkPos.x - 1);
    assert(panel->Pos.y >= view->WorkPos.y - 1);
    assert(panel->Pos.x + panel->Size.x <= view->WorkPos.x + view->WorkSize.x + 1);
    assert(panel->Pos.y + panel->Size.y <= view->WorkPos.y + view->WorkSize.y + 1);
    // The footer must remain inside the root even when content scrolls.
    assert(panel->ScrollMax.y < 1);
    assert(panel->Scroll.y < 1);
}

static void Mouse(float x, float y, bool down) {
    SDL_Event motion{};
    motion.type = SDL_MOUSEMOTION;
    motion.motion.windowID = SDL_GetWindowID(window);
    motion.motion.x = (int)x;
    motion.motion.y = (int)y;
    SIM::GUI::ProcessEvent(&motion);
    SDL_Event e{};
    e.type = down ? SDL_MOUSEBUTTONDOWN : SDL_MOUSEBUTTONUP;
    e.button.windowID = SDL_GetWindowID(window);
    e.button.button = SDL_BUTTON_LEFT;
    e.button.state = down ? SDL_PRESSED : SDL_RELEASED;
    e.button.x = (int)x;
    e.button.y = (int)y;
    SDL_WarpMouseInWindow(window, (int)x, (int)y);
    SIM::GUI::ProcessEvent(&e);
    Frame();
}

static void StartKeyboardCapture() {
    // Search the visible first binding row, using the actual child geometry.
    // The test tolerates changes in padding/font metrics.
    ImGuiWindow *page = nullptr;
    for (auto *candidate : ImGui::GetCurrentContext()->Windows)
        if (candidate->Active && candidate->ParentWindow == Panel() &&
            std::string(candidate->Name).find("/Page_") != std::string::npos)
            page = candidate;
    assert(page);
    const float x = page->Pos.x + page->Size.x * .8f;
    const float end = page->Pos.y + std::min(page->Size.y - 20, 230.0f);
    for (float y = page->Pos.y + 65; y < end && !SIM::GUI::AppPadIsCapturing(); y += 10) {
        Mouse(x, y, true);
        Mouse(x, y, false);
    }
    assert(SIM::GUI::AppPadIsCapturing());
}

static void CheckBarSpeedControl() {
    ImGuiWindow *page = nullptr;
    for (auto *candidate : ImGui::GetCurrentContext()->Windows)
        if (candidate->Active && candidate->ParentWindow == Panel() &&
            std::string(candidate->Name).find("/Page_") != std::string::npos)
            page = candidate;
    assert(page);
    auto *table = ImGui::GetCurrentContext()->Tables.GetByKey(page->GetID("BattleSpeed"));
    assert(table && table->ColumnsCount == 2);
    // HP/EXP is the last row. Use the actual table geometry, not screen coordinates.
    float x = table->Columns[1].WorkMinX + 20;
    float y = table->RowPosY1 + ImGui::GetStyle().CellPadding.y + ImGui::GetFrameHeight() / 2;
    Mouse(x, y, true); Mouse(x, y, false);
    Key(SDLK_DOWN); Frame(); Key(SDLK_DOWN, false); Frame();
    Key(SDLK_DOWN); Frame(); Key(SDLK_DOWN, false); Frame();
    Key(SDLK_RETURN); Frame(); Key(SDLK_RETURN, false); Frame();
    auto *projectConfig = SIM_Config_prj_GetConfig();
    assert(projectConfig->battleBarSpeed == PORT_SPEED_NORMAL);
    assert(projectConfig->textSpeed == PORT_SPEED_FAST);
    assert(projectConfig->battlePacing == BATTLE_PACING_SMOOTH);
    // Selection automatically persists without changing the other speed controls.
    SIM_Config_prj_LoadDefaults(projectConfig);
    assert(SIM_Config_prj_LoadConfigFile(projectConfig));
    assert(projectConfig->battleBarSpeed == PORT_SPEED_NORMAL);
    assert(projectConfig->textSpeed == PORT_SPEED_FAST);
    assert(projectConfig->battlePacing == BATTLE_PACING_SMOOTH);
    assert(!projectConfig->disableLowHPBeep);
    projectConfig->disableLowHPBeep = TRUE;
    SIM_Config_prj_SaveConfigFile(projectConfig);
    SIM_Config_prj_LoadDefaults(projectConfig);
    assert(SIM_Config_prj_LoadConfigFile(projectConfig) && projectConfig->disableLowHPBeep);
    projectConfig->disableLowHPBeep = FALSE;
    SIM_Config_prj_SaveConfigFile(projectConfig);
}

int main() {
    using namespace SIM::GUI;
    GameplayConfig_Init();
    SIM_Config_prj_LoadDefaults(SIM_Config_prj_GetConfig());
    SIM_Config_LoadDefaults(&config);
    assert(config.menuScalePercent == 0 && !config.pauseWhileMenuOpen);
    // Old configs inherit readable-menu defaults.
    std::ofstream("sim_config.ini") << "[General]\nMasterVolume=72\n";
    SIM_Config_LoadConfigFile(&config);
    assert(config.masterVolume == 72 && config.menuScalePercent == 0 && !config.pauseWhileMenuOpen);
    for (const char *bad : {"-1", "99", "101", "174", "201", "nan", "150oops", "99999999999999999999999"}) {
        std::ofstream("sim_config.ini") << "[Interface]\nMenuScalePercent=" << bad << '\n';
        SIM_Config_LoadConfigFile(&config);
        assert(config.menuScalePercent == 0);
    }
    config.menuScalePercent = 175;
    config.pauseWhileMenuOpen = FALSE;
    SIM_Config_SaveConfigFile(&config);
    SIM_Config_LoadDefaults(&config);
    SIM_Config_LoadConfigFile(&config);
    assert(config.menuScalePercent == 175 && !config.pauseWhileMenuOpen);
    config.pauseWhileMenuOpen = TRUE;
    SIM_Config_SaveConfigFile(&config);
    SIM_Config_LoadDefaults(&config);
    SIM_Config_LoadConfigFile(&config);
    assert(config.pauseWhileMenuOpen);
    config.menuScalePercent = 100;
    config.pauseWhileMenuOpen = FALSE;

    assert(SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMECONTROLLER) == 0);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 4);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 2);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    window = SDL_CreateWindow("Menu regression", 0, 0, 1024, 576, SDL_WINDOW_OPENGL | SDL_WINDOW_HIDDEN);
    assert(window);
    SDL_GLContext gl = SDL_GL_CreateContext(window);
    assert(gl);
    assert(gladLoadGLLoader(SDL_GL_GetProcAddress));
    Init(window, gl);
    assert(ImGui::GetIO().ConfigFlags & ImGuiConfigFlags_NavEnableKeyboard);
    assert(!IsEnabled() && !IsGameLogicPaused());
    assert(Key(SDLK_TAB) && IsEnabled() && !IsGameLogicPaused());
    config.pauseWhileMenuOpen = TRUE;
    assert(IsGameLogicPaused());
    assert(ConsumeInputReset() && !ConsumeInputReset());
    assert(Key(SDLK_TAB, true, true) && IsEnabled()); // Holding TAB must not flicker.
    Key(SDLK_TAB, false);
    Frame(); Frame();

    SelectPage(Page::SpeedText);
    Frame(); Frame();
    Capture("battle-speed-1024");
    CheckBarSpeedControl();

    // Both pause modes and an active connection still isolate gameplay input.
    assert(Key(SDLK_z) && Key(SDLK_z, false));
    config.pauseWhileMenuOpen = FALSE;
    assert(!IsGameLogicPaused() && Key(SDLK_RIGHT));
    Key(SDLK_RIGHT, false);
    config.pauseWhileMenuOpen = TRUE;
    netState = SIM_NET_STATE_CLIENT;
    assert(!IsGameLogicPaused());
    netState = SIM_NET_STATE_DISCONNECTED;
    assert(IsGameLogicPaused());

    // The explicit Developer pause is independent of automatic menu pause.
    config.pauseWhileMenuOpen = FALSE;
    SelectPage(Page::Developer);
    Frame(); Frame();
    Mouse(275, 242, true); Mouse(275, 242, false); Frame(); Frame();
    assert(IsGameLogicPaused());
    Toggle();
    assert(!IsEnabled() && IsGameLogicPaused());
    Enable(); Frame(); Frame();
    Mouse(275, 242, true); Mouse(275, 242, false); Frame(); Frame();
    assert(!IsGameLogicPaused());
    config.pauseWhileMenuOpen = TRUE;

    const int sizes[][2] = {{1024,576}, {1280,800}, {1920,1080}, {3840,2160}};
    for (const auto &size : sizes) {
        SDL_SetWindowSize(window, size[0], size[1]);
        for (int scale : {0,100,125,150,175,200}) {
            config.menuScalePercent = scale;
            for (int page = 0; page < static_cast<int>(Page::Count); ++page) {
                SelectPage(static_cast<Page>(page));
                Frame(); Frame();
                CheckPanelFits();
            }
        }
    }
    config.menuScalePercent = 100;
    SDL_SetWindowSize(window, 1024,576);
    SelectPage(Page::Controls);
    Frame(); Frame();
    const int oldA = config.padSettings.aKey;
    StartKeyboardCapture();
    assert(Key(SDLK_ESCAPE));
    assert(IsEnabled() && !AppPadIsCapturing() && config.padSettings.aKey == oldA);
    Key(SDLK_ESCAPE, false);
    StartKeyboardCapture();
    assert(Key(SDLK_TAB)); // Binding capture owns TAB before the menu shortcut.
    assert(IsEnabled() && !AppPadIsCapturing());
    assert(config.padSettings.aKey == SDLK_TAB);
    Key(SDLK_TAB, false);

    manualBack = true;
    assert(Key(SDLK_ESCAPE) && IsEnabled() && !manualBack);
    Key(SDLK_ESCAPE, false);
    assert(Key(SDLK_ESCAPE) && !IsEnabled() && !IsGameLogicPaused());
    Key(SDLK_ESCAPE, false);
    assert(ConsumeInputReset());

    // A key held in the menu is blocked until release after closing.
    Enable();
    Key(SDLK_z);
    Toggle();
    assert(Key(SDLK_z, true, true));
    assert(Key(SDLK_z, false));
    assert(!Key(SDLK_z));
    Key(SDLK_z, false);
    // Platform events must still reach the application.
    Enable();
    SDL_Event event{};
    event.type = SDL_WINDOWEVENT;
    event.window.event = SDL_WINDOWEVENT_FOCUS_LOST;
    assert(!ProcessEvent(&event));
    event.type = SDL_QUIT;
    assert(!ProcessEvent(&event));
    std::puts("Menu GUI: config migration, scaling/layout, pause, network policy, capture and input ownership passed.");
}
