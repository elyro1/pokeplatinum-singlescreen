#include <assert.h>
#include <stdio.h>

#include "../../src/battle/battle_display.c"
#include "../../src/port/battle_timing.c"

static SIM_Config_prj_type sConfig;
static u32 sType;
static BOOL sBw;
static int sFrames, sAnimations, sCries;
SIM_Config_prj_type *SIM_Config_prj_GetConfig(void) { return &sConfig; }
u32 BattleSystem_GetBattleType(BattleSystem *sys) { return sType; }
BOOL BwAdapter_IsDrawing(void) { return sBw; }
PokemonAnimManager *BattleSystem_GetPokemonAnimManager(BattleSystem *sys) { return NULL; }
ChatotCry *BattleSystem_GetChatotCry(BattleSystem *sys, int battler) { return NULL; }
void PokemonSprite_SetAttribute(PokemonSprite *sprite, enum PokemonSpriteAttribute attribute, int value)
{
    assert(attribute == MON_SPRITE_SHADOW_IS_AFFINE && value == FALSE);
}
void PokemonSprite_InitAnim(PokemonSprite *sprite, int dummy) { sFrames++; }
void PokemonSprite_LoadAnim(NARC *narc, PokemonAnimManager *manager, PokemonSprite *sprite,
    u16 species, int face, int flip, int battler) { sAnimations++; }
void PokemonSprite_LoadCryDelay(NARC *narc, u8 *delay, u16 species, u16 type) { *delay = 0; }
void Species_PlayDelayedCry(ChatotCry *cry, enum PokemonCryMod mod, u16 species, int form,
    int pan, int volume, int force, enum HeapID heap, u8 delay) { sCries++; }

int main(void)
{
    for (int active = 0; active < 2; active++) {
        for (int fps = 0; fps < 2; fps++) {
            for (int pacing = 0; pacing < BATTLE_PACING_COUNT; pacing++) {
                for (int link = 0; link < 2; link++) {
                    for (int breakout = 0; breakout < 2; breakout++) {
                        sConfig.enable60fps = fps;
                        sConfig.battlePacing = pacing;
                        sType = link ? BATTLE_TYPE_LINK : 0;
                        BattleTiming_SetActive(active);
                        BOOL fast = active && fps && pacing != BATTLE_PACING_SMOOTH && !link && breakout;
                        assert(BattleDisplay_ShouldShortenBreakout(NULL, breakout) == fast);
                        BattlerData battler = { 0 };
                        MonShowData data = { .battlerData = &battler, .face = FACE_FRONT, .fastBreakout = fast };
                        for (int bw = 0; bw < 2; bw++) {
                            sBw = bw;
                            sFrames = sAnimations = sCries = 0;
                            BattleDisplay_StartBreakoutEntrance(&data);
                            assert(sFrames == !fast && sAnimations == !fast);
                            assert(sCries == !bw); // BW2 owns its visible cry cue.
                        }
                    }
                }
            }
        }
    }
    puts("Breakout presentation: Quick/Turbo skip the extra entrance animation; normal send-outs, cries and Smooth/link/30 FPS timing preserved.");
}
