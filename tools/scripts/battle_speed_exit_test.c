#include <assert.h>
#include <stdio.h>

#include "../../src/battle/battle_display.c"
#define PaletteData_StartFade RealPaletteData_StartFade
#define PaletteData_GetSelectedBuffersMask RealPaletteData_GetSelectedBuffersMask
#include "../../src/palette.c"
#undef PaletteData_StartFade
#undef PaletteData_GetSelectedBuffersMask
#include "../../src/port/battle_timing.c"

static SIM_Config_prj_type sConfig;
static u32 sBattleType;
static BattlerData sBattler;
static PokemonSprite sSprite;
static u16 sPaletteMask;
static BOOL sSpriteFading;
static int sPaletteWait, sSpriteDelay, sBgmFrames;
static int sFadeStarts, sCleared, sFreed, sDone;
static SysTask sPaletteTask;
static BOOL sCheckingPalette;

SysTask *SysTask_Start(SysTaskFunc callback, void *data, u32 priority)
{
    assert(callback == SysTask_FadePalette);
    sPaletteTask = (SysTask){ .callback = callback, .param = data };
    return &sPaletteTask;
}

static void CheckPaletteProgress(s8 wait, u8 initial, u8 target)
{
    PaletteData palette = { 0 };
    u16 unfaded[PALETTE_SIZE] = { GX_RGB(31, 31, 31) };
    u16 faded[PALETTE_SIZE] = { 0 };
    PaletteData_InitBuffer(&palette, PLTTBUF_MAIN_BG, unfaded, faded, sizeof(unfaded));
    sCheckingPalette = TRUE;
    assert(RealPaletteData_StartFade(&palette, PLTTBUF_MAIN_BG_F, 1, wait, initial, target, 0));
    int frames = 0;
    for (; frames < 40 && palette.selectedBuffers; frames++) {
        sPaletteTask.callback(&sPaletteTask, sPaletteTask.param);
    }
    assert(palette.selectedBuffers == 0 && !palette.fadeInProgress);
    assert(palette.buffers[PLTTBUF_MAIN_BG].selected.cur == target);
    assert(faded[0] == (target == 16 ? 0 : unfaded[0]));
    assert(wait != -13 || frames == 2);
    sCheckingPalette = FALSE;
}

static void CheckPaletteStepRange(void)
{
    // All accelerated waits must progress, including values that used to wrap
    // the four-bit step to zero (-14, -30, ...).
    for (int wait = -128; wait < 0; wait++) {
        CheckPaletteProgress(wait, 0, 16);
        CheckPaletteProgress(wait, 16, 0);
    }
}

SIM_Config_prj_type *SIM_Config_prj_GetConfig(void) { return &sConfig; }
u32 BattleSystem_GetBattleType(BattleSystem *sys) { return sBattleType; }
PaletteData *BattleSystem_GetPaletteData(BattleSystem *sys) { return NULL; }
PokemonSpriteManager *BattleSystem_GetPokemonSpriteManager(BattleSystem *sys) { return NULL; }
PaletteAnimator *BattleSystem_GetPaletteAnimator(BattleSystem *sys) { return NULL; }
int BattleSystem_GetMaxBattlers(BattleSystem *sys) { return 1; }
BattlerData *BattleSystem_GetBattlerData(BattleSystem *sys, int battler) { return &sBattler; }
BOOL PokemonSprite_IsFadeActive(PokemonSprite *sprite) { return sSpriteFading; }
BOOL PokemonSprite_IsActive(PokemonSprite *sprite) { return sprite->active; }
u16 PaletteData_GetSelectedBuffersMask(PaletteData *palette) { return sPaletteMask; }
void sub_02015738(PaletteAnimator *animator, u8 value) { assert(value == 1); }
u8 PaletteData_StartFade(PaletteData *palette, u16 buffers, u16 palettes, s8 wait, u8 initial, u8 target, u16 color)
{
    assert(buffers == (PLTTBUF_MAIN_BG_F | PLTTBUF_SUB_BG_F | PLTTBUF_MAIN_OBJ_F | PLTTBUF_SUB_OBJ_F)
        || buffers == (PLTTBUF_MAIN_BG_F | PLTTBUF_MAIN_OBJ_F)
        || buffers == (PLTTBUF_SUB_BG_F | PLTTBUF_SUB_OBJ_F));
    assert(palettes == 0xFFFF && color == 0);
    CheckPaletteProgress(wait, initial, target);
    sPaletteWait = wait;
    sPaletteMask = buffers;
    sFadeStarts++;
    return TRUE;
}
void PokemonSpriteManager_StartFadeAll(PokemonSpriteManager *manager, int initial, int target, int delay, int color)
{
    assert(color == 0);
    sSpriteDelay = delay;
    sSpriteFading = TRUE;
}
void Sound_FadeOutBGM(int volume, int frames) { assert(volume == 0); sBgmFrames = frames; }
void BattleController_EmitClearCommand(BattleSystem *sys, int battler, int command) { sCleared++; }
void Heap_Free(void *data) { sFreed++; }
void SysTask_Done(SysTask *task) { if (!sCheckingPalette) sDone++; }

static void CheckExit(BOOL link, BOOL enable60fps, int pacing)
{
    FadeOutData data = { 0 };
    sBattler.monSprite = &sSprite;
    sSprite.active = TRUE;
    sBattleType = link ? BATTLE_TYPE_LINK : 0;
    sConfig.enable60fps = enable60fps;
    sConfig.battlePacing = pacing;
    BattleTiming_SetActive(TRUE);
    sFadeStarts = sCleared = sFreed = sDone = 0;

    Task_FadeOut(NULL, &data);
    int speed = !link && enable60fps ? (pacing == BATTLE_PACING_QUICK ? 2 : (pacing == BATTLE_PACING_TURBO ? 8 : 1)) : 1;
    assert(sPaletteWait == (speed == 1 ? 1 : (speed == 2 ? 0 : -13)));
    assert(sSpriteDelay == (speed == 1 ? 0 : (speed == 2 ? -1 : -14)));
    assert(sBgmFrames == 16 / speed);
    assert(data.state == 1 && sCleared == 0);

    // Completion still waits for every palette and the Pokémon/shadow fade.
    sSpriteFading = FALSE;
    Task_FadeOut(NULL, &data);
    assert(data.state == 1 && sCleared == 0);
    sPaletteMask = 0;
    sSpriteFading = TRUE;
    Task_FadeOut(NULL, &data);
    assert(data.state == 1 && sCleared == 0);
    sSpriteFading = FALSE;
    Task_FadeOut(NULL, &data);
    assert(data.state == 2 && sCleared == 0);
    Task_FadeOut(NULL, &data);
    assert(sFadeStarts == 1 && sCleared == 1 && sFreed == 1 && sDone == 1);

    sFadeStarts = 0;
    BattleDisplay_StartMenuFade(NULL, 0, 16);
    BOOL turbo = !link && enable60fps && pacing == BATTLE_PACING_TURBO;
    assert(sFadeStarts == 2 && sPaletteWait == (turbo ? -13 : -8) && sSpriteDelay == (turbo ? -14 : -8));
    BattleDisplay_StartMenuFade(NULL, 16, 0);
}

int main(void)
{
    CheckPaletteStepRange();
    for (int pacing = 0; pacing < BATTLE_PACING_COUNT; pacing++) {
        for (int fps = 0; fps < 2; fps++) {
            for (int link = 0; link < 2; link++) {
                CheckExit(link, fps, pacing);
            }
        }
    }
    puts("Battle exits: real palette fades in both directions, Quick/Turbo fade and audio settings, palette/sprite completion and link/30 FPS timing passed.");
}
