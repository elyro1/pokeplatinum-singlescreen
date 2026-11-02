#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

#include "../../src/battle/battle_script.c"
#include "../../src/bg_window.c"

static SIM_Config_prj_type sConfig;
static int sPartyCount;
static int sFreedBalls, sDeletedSprites, sBackdrops, sFades;
static BOOL sBlack, sDrawing, sCaught, sBusy, sBallBusy;
static u16 sFadeBuffers, sLastFadeBuffers;
static int sResumes, sCaughtBattler, sScore;

SIM_Config_prj_type *SIM_Config_prj_GetConfig(void) { return &sConfig; }
int BattleSystem_GetPartyCount(BattleSystem *sys, int battler) { return sPartyCount; }
u32 BattleSystem_GetBattleType(BattleSystem *sys) { return sys->battleType; }
Pokemon *BattleSystem_GetPartyPokemon(BattleSystem *sys, int battler, int slot) { return NULL; }
u32 Pokemon_GetValue(Pokemon *mon, enum PokemonDataParam param, void *dest) { return SPECIES_PONYTA; }
BOOL BattleSystem_HasCaughtSpecies(BattleSystem *sys, int species) { return sCaught; }
void BattleSystem_SetCaughtBattlerIndex(BattleSystem *sys, int battler) { sCaughtBattler = battler; }
void BattleSystem_TryIncrementTrainerScoreCaughtSpecies(BattleSystem *sys) { sScore++; }
PaletteAnimator *BattleSystem_GetPaletteAnimator(BattleSystem *sys) { return NULL; }
void sub_02015738(PaletteAnimator *animator, u8 mode) {}
void ov12_0223783C(BallRotation *ball) { sFreedBalls++; }
BOOL ov12_022368D0(BallRotation *ball, int command) { assert(command == 7); return sBallBusy; }
void PokemonSpriteManager_DeleteAll(PokemonSpriteManager *sprites) { sDeletedSprites++; }
void PokemonSpriteManager_StartFadeAll(PokemonSpriteManager *sprites, int start, int end, int delay, int color) {}
BOOL BwAdapter_IsDrawing(void) { return sDrawing; }
BOOL BwAdapter_IsBusy(void) { return sBusy; }
void BwAdapter_FinishCatch(void) { assert(sBlack); sResumes++; }
void SetScreenColorBrightness(enum DSScreen screen, u16 color)
{
    assert(screen == DS_SCREEN_MAIN && color == COLOR_BLACK);
    sBlack = TRUE;
}
void ResetScreenMasterBrightness(enum DSScreen screen)
{
    assert(screen == DS_SCREEN_MAIN);
    sBlack = FALSE;
}
void PaletteData_SetAutoTransparent(PaletteData *palette, BOOL enabled) {}
u16 PaletteData_GetSelectedBuffersMask(PaletteData *palette) { return sFadeBuffers; }
void BattleSystem_SetupBattleScreen(BattleSystem *sys, BOOL terrain)
{
    assert(0 && "Never must not create the nickname screen");
}
void Heap_Free(void *ptr)
{
    // The real game allocator dereferences its allocation header: NULL is invalid.
    assert(ptr != NULL);
    free(ptr);
}
u8 PaletteData_StartFade(PaletteData *palette, u16 buffers, u16 palettes, s8 wait, u8 start, u8 end, u16 target)
{
    assert((start == 16 && end == 0) || (start == 0 && end == 16));
    sLastFadeBuffers = buffers;
    sFades++;
    return TRUE;
}

static void CheckNicknameSkip(void)
{
    for (int mode = 0; mode < NICKNAME_MODE_COUNT; mode++) {
        for (int fromDex = 0; fromDex < 2; fromDex++) {
            for (int full = 0; full < 2; full++) {
                Window window = { .pixels = malloc(32) };
                BattleSystem sys = { .windows = &window };
                BattleScriptTaskData data = { .battleSys = &sys, .seqNum = SEQ_CATCH_MON_SET_CAUGHT_SPECIES };
                if (fromDex) {
                    Window_Remove(&window);
                }
                sConfig.nicknameMode = mode;
                sPartyCount = full ? MAX_PARTY_SIZE : MAX_PARTY_SIZE - 1;
                sFreedBalls = sDeletedSprites = sBackdrops = sFades = 0;
                sBlack = FALSE;
                BOOL skipped = BattleScript_SkipNicknameScreen(&data, NULL, fromDex);
                assert(skipped == (mode == NICKNAME_MODE_NEVER));
                if (!skipped) {
                    assert(data.seqNum == SEQ_CATCH_MON_SET_CAUGHT_SPECIES);
                    assert(!sBlack && !sFreedBalls && !sDeletedSprites && !sBackdrops && !sFades);
                    if (!fromDex) Window_Remove(&window);
                    continue;
                }
                assert(sFreedBalls == !fromDex);
                assert(sDeletedSprites == fromDex);
                assert(!sBlack && !sFades && !sBackdrops);
                assert(data.seqNum == (fromDex ? SEQ_CATCH_MON_FINISH_WITHOUT_NICKNAME_SCREEN : SEQ_CATCH_MON_DIDNT_GIVE_NICKNAME));
                if (fromDex) {
                    assert(window.pixels == NULL); // No replacement window was allocated.
                } else {
                    Window_Remove(&window);
                }
            }
        }
    }
}

static void CheckDirectNaming(void)
{
    for (int mode = 0; mode < NICKNAME_MODE_COUNT; mode++) {
        BattleScriptTaskData data = { .seqNum = SEQ_CATCH_MON_CLOSE_POKEDEX };
        sConfig.nicknameMode = mode;
        sFades = sBackdrops = 0;
        sBlack = FALSE;
        BOOL direct = BattleScript_StartDirectNaming(&data, NULL);
        assert(direct == (mode == NICKNAME_MODE_ALWAYS));
        assert(!sBlack && !sBackdrops);
        assert(sFades == direct);
        if (direct) {
            assert(data.seqNum == SEQ_CATCH_MON_START_NAMING_SCREEN);
            // Never fade the main screen back in: its prompt scene was skipped.
            assert(sLastFadeBuffers == (PLTTBUF_SUB_BG_F | PLTTBUF_SUB_OBJ_F));
        } else {
            assert(data.seqNum == SEQ_CATCH_MON_CLOSE_POKEDEX);
        }
    }
}

static void CheckDirectPokedex(void)
{
    BattleContext ctx = { 0 };
    BattleSystem sys = { 0 };
    BattleScriptTaskData data = { .battleSys = &sys, .battleCtx = &ctx };
    for (int drawing = 0; drawing < 2; drawing++) {
        for (int caught = 0; caught < 2; caught++) {
            const u32 types[] = { 0, BATTLE_TYPE_PAL_PARK, BATTLE_TYPE_CATCH_TUTORIAL };
            for (int type = 0; type < 3; type++) {
                sDrawing = drawing;
                sCaught = caught;
                sFades = sResumes = sScore = 0;
                sCaughtBattler = -1;
                sBlack = FALSE;
                sys.battleType = types[type];
                BOOL direct = BattleScript_BeginDirectPokedex(&data, BATTLER_ENEMY_1, NULL, NULL);
                assert(direct == (drawing && !caught && type == 0));
                assert(sResumes == 0); // Hold the successful-catch camera until the fade finishes.
                assert(sFades == direct && sScore == direct);
                if (!direct) continue;
                assert(sCaughtBattler == BATTLER_ENEMY_1);
                assert(data.seqNum == SEQ_CATCH_MON_BW_FADE_FOR_POKEDEX);
                sFadeBuffers = PLTTBUF_MAIN_BG_F;
                BattleScript_UpdateDirectPokedex(&data, NULL);
                assert(!sBlack && !sResumes);
                assert(data.seqNum == SEQ_CATCH_MON_BW_FADE_FOR_POKEDEX);
                sFadeBuffers = 0;
                BattleScript_UpdateDirectPokedex(&data, NULL);
                assert(sBlack && sResumes == 1);
                assert(data.seqNum == SEQ_CATCH_MON_BW_FINISH_CATCH_FOR_POKEDEX);
                sBusy = TRUE;
                sBallBusy = FALSE;
                BattleScript_UpdateDirectPokedex(&data, NULL);
                assert(data.seqNum == SEQ_CATCH_MON_BW_FINISH_CATCH_FOR_POKEDEX);
                sBusy = FALSE;
                sBallBusy = TRUE;
                BattleScript_UpdateDirectPokedex(&data, NULL);
                assert(data.seqNum == SEQ_CATCH_MON_BW_FINISH_CATCH_FOR_POKEDEX);
                sBallBusy = FALSE;
                BattleScript_UpdateDirectPokedex(&data, NULL);
                assert(data.seqNum == SEQ_CATCH_MON_SET_POKEDEX_DATA);
                assert(sBlack && sResumes == 1 && sScore == 1);
            }
        }
    }
}

int main(void)
{
    CheckNicknameSkip();
    CheckDirectPokedex();
    CheckDirectNaming();
    puts("Capture presentation: 27 routes passed; direct Always keyboard, no Never screen allocation or masking, and direct BW2 Pokedex transitions.");
}
