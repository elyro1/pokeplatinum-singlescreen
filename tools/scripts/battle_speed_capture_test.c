#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../../src/battle/battle_display.c"
#include "../../src/pokemon_sprite.c"
#include "../../src/palette.c"
#include "../../src/port/battle_timing.c"
#include "../../src/sys_task_manager.c"
#include "../../src/sys_task.c"

static SIM_Config_prj_type sConfig;
static BattlerData sBattlers[2];
static PaletteData sPalette;
static PokemonSpriteManager *sSprites;
static int sCleared;
System gSystem;
SIM_Config_prj_type *SIM_Config_prj_GetConfig(void) { return &sConfig; }
void ErrorHandling_AssertFail(void) { assert(0); }
void *Heap_Alloc(u32 heap, u32 size) { return calloc(1, size); }
void Heap_Free(void *data) { free(data); }
int BattleSystem_GetMaxBattlers(BattleSystem *sys) { return 2; }
BattlerData *BattleSystem_GetBattlerData(BattleSystem *sys, int battler) { return &sBattlers[battler]; }
u32 BattleSystem_GetBattleType(BattleSystem *sys) { return 0; }
PaletteData *BattleSystem_GetPaletteData(BattleSystem *sys) { return &sPalette; }
PokemonSpriteManager *BattleSystem_GetPokemonSpriteManager(BattleSystem *sys) { return sSprites; }
PaletteAnimator *BattleSystem_GetPaletteAnimator(BattleSystem *sys) { return NULL; }
void sub_02015738(PaletteAnimator *animator, u8 value) { assert(value == 1); }
void Sound_FadeOutBGM(int volume, int frames) { assert(volume == 0 && frames > 0); }
void BattleController_EmitClearCommand(BattleSystem *sys, int battler, int command)
{
    assert(battler == 1 && command == 17);
    sCleared++;
}
void *NARC_AllocAndReadWholeMemberByIndexPair(enum NarcID narc, int member, enum HeapID heap)
{
    assert(0); // This fixture already has sprite palettes buffered.
    return NULL;
}
BOOL NNS_G2dGetUnpackedPaletteData(void *data, NNSG2dPaletteData **palette) { assert(0); return FALSE; }

static void CheckCaptureSprite(int pacing, BOOL enable60fps, int yOffset, BOOL liveChange)
{
    void *memory = calloc(1, SysTaskManager_GetRequiredSize(8));
    gSystem.mainTaskMgr = SysTaskManager_Init(8, memory);
    u16 unfaded[PALETTE_SIZE * 8] = { 0 }, faded[PALETTE_SIZE * 8] = { 0 };
    PokemonSpriteManager sprites = { .heapID = HEAP_ID_BATTLE, .plttRawData = faded, .plttRawDataUnfaded = unfaded };
    sSprites = &sprites;
    sPalette = (PaletteData){ 0 };
    u16 sceneUnfaded[4][PALETTE_SIZE] = { 0 }, sceneFaded[4][PALETTE_SIZE] = { 0 };
    for (int i = 0; i < 4; i++) {
        PaletteData_InitBuffer(&sPalette, i, sceneUnfaded[i], sceneFaded[i], sizeof(sceneUnfaded[i]));
    }
    for (int i = 0; i < 2; i++) {
        sprites.sprites[i].active = TRUE;
        sprites.sprites[i].transforms.scaleX = sprites.sprites[i].transforms.scaleY = 0x100;
        sBattlers[i].monSprite = &sprites.sprites[i];
        sBattlers[i].battler = i;
    }
    sConfig.enable60fps = enable60fps;
    sConfig.battlePacing = pacing;
    BattleTiming_SetActive(TRUE);
    BattleTimingDomain previous = BattleTiming_SetDomain(BATTLE_TIMING_UI);
    OpenCaptureBallMessage message = { .command = 17, .yOffset = yOffset, .ball = ITEM_POKE_BALL };
    sCleared = 0;
    BattleDisplay_InitTaskOpenCaptureBall(NULL, &sBattlers[1], &message);
    BattleTiming_SetDomain(previous);
    for (int frame = 0; frame < 80 && !sCleared; frame++) {
        if (liveChange && frame % 3 == 0) sConfig.battlePacing = (frame / 3) % BATTLE_PACING_COUNT;
        BattleTiming_BeginFrame();
        SysTaskManager_ExecuteTasks(gSystem.mainTaskMgr);
        BufferPokemonSpritePlttData(&sprites);
    }
    assert(sCleared == 1 && !sprites.sprites[1].active);
    // Shrinking into the ball deletes the hidden sprite before its fade ends.
    // That sprite can no longer update and must not block the next scene fade.
    assert(sprites.sprites[1].transforms.fadeActive);
    assert(BattleDisplay_IsSceneFadeComplete(NULL));

    previous = BattleTiming_SetDomain(BATTLE_TIMING_UI);
    PokemonSpriteManager_StartFadeAll(&sprites, 0, 16, -14, 0);
    BattleTiming_SetDomain(previous);
    assert(!BattleDisplay_IsSceneFadeComplete(NULL)); // The live player sprite still matters.
    for (int frame = 0; frame < 4 && !BattleDisplay_IsSceneFadeComplete(NULL); frame++) {
        BattleTiming_BeginFrame();
        BufferPokemonSpritePlttData(&sprites);
    }
    assert(BattleDisplay_IsSceneFadeComplete(NULL));
    assert(sCleared == 1);

    // Bag and party menus use this shared fade, owned by an unscaled input task.
    for (int direction = 0; direction < 2; direction++) {
        BattleDisplay_StartMenuFade(NULL, direction ? 16 : 0, direction ? 0 : 16);
        int frames = 0;
        do {
            BattleTiming_BeginFrame();
            SysTaskManager_ExecuteTasks(gSystem.mainTaskMgr);
            BufferPokemonSpritePlttData(&sprites);
            frames++;
        } while (frames < 80 && (sPalette.selectedBuffers || !BattleDisplay_IsSceneFadeComplete(NULL)));
        assert(sPalette.selectedBuffers == 0 && BattleDisplay_IsSceneFadeComplete(NULL));
    }

    // The final exit must also clear its command with the caught sprite gone.
    sBattlers[1].data[0] = 17;
    BattleDisplay_InitTaskFadeOut(NULL, &sBattlers[1]);
    for (int frame = 0; frame < 80 && sCleared == 1; frame++) {
        BattleTiming_BeginFrame();
        SysTaskManager_ExecuteTasks(gSystem.mainTaskMgr);
        BufferPokemonSpritePlttData(&sprites);
    }
    assert(sCleared == 2);
    assert(gSystem.mainTaskMgr->stackPointer == 0);
    free(memory);
}

int main(void)
{
    for (int pacing = 0; pacing < BATTLE_PACING_COUNT; pacing++) {
        for (int fps = 0; fps < 2; fps++) {
            for (int offset = 0; offset < 3; offset++) {
                CheckCaptureSprite(pacing, fps, offset * 10, FALSE);
                CheckCaptureSprite(pacing, fps, offset * 10, TRUE);
            }
        }
    }
    puts("Capture/menu flows: 36 cases, real capture tasks, Bag/party fades, exit cleanup, deleted sprites and live pacing changes passed.");
}
