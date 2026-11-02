#include <assert.h>
#include <stdio.h>

#include "../../src/bwbattle/adapter/bw_battle_view.c"
#include "../../src/port/battle_timing.c"

static SIM_Config_prj_type sConfig;
static u32 sType, sStatus;
SIM_Config_prj_type *SIM_Config_prj_GetConfig(void) { return &sConfig; }
u32 BattleSystem_GetBattleType(BattleSystem *sys) { return sType; }
u32 BattleSystem_GetBattleStatusMask(BattleSystem *sys) { return sStatus; }

int main(void)
{
    const u32 types[] = { 0, BATTLE_TYPE_DOUBLES, BATTLE_TYPE_DOUBLES | BATTLE_TYPE_TAG,
        BATTLE_TYPE_TRAINER, BATTLE_TYPE_TRAINER_DOUBLES,
        BATTLE_TYPE_TRAINER_DOUBLES | BATTLE_TYPE_2vs2 | BATTLE_TYPE_TAG,
        BATTLE_TYPE_TRAINER | BATTLE_TYPE_FRONTIER,
        BATTLE_TYPE_LINK, BATTLE_TYPE_TRAINER | BATTLE_TYPE_LINK,
        BATTLE_TYPE_SAFARI, BATTLE_TYPE_PAL_PARK, BATTLE_TYPE_CATCH_TUTORIAL };
    const u32 statuses[] = { 0, BATTLE_STATUS_RECORDED, BATTLE_STATUS_RECORDING,
        BATTLE_STATUS_FIRST_BATTLE, BATTLE_STATUS_GIRATINA };
    for (unsigned type = 0; type < NELEMS(types); type++) {
        for (unsigned status = 0; status < NELEMS(statuses); status++) {
            for (int fast = 0; fast < 2; fast++) {
                sType = types[type];
                sStatus = statuses[status];
                sConfig.fastWildBattleIntro = fast;
                assert(BwAdapter_ChooseFastIntro(NULL) == (fast && type < 3 && status == 0));
            }
        }
    }
    for (int pacing = 0; pacing < BATTLE_PACING_COUNT; pacing++) {
        for (int fps = 0; fps < 2; fps++) {
            sConfig.enable60fps = fps;
            sConfig.battlePacing = pacing;
            assert(BwAdapter_ViewFrames() == (fps ? pacing + 1 : 2));
            for (int active = 0; active < 2; active++) {
                BattleTiming_SetActive(active);
                for (unsigned type = 0; type < NELEMS(types); type++) {
                    sType = types[type];
                    BOOL overlap = active && fps && pacing != BATTLE_PACING_SMOOTH
                        && !(sType & BATTLE_TYPE_LINK);
                    assert(BwAdapter_ShouldOverlapSendOutCry(BW_EFFECT_PLAYER_SEND_OUT) == overlap);
                    assert(BwAdapter_ShouldOverlapSendOutCry(BW_EFFECT_TRAINER_SEND_OUT) == overlap);
                    assert(BwAdapter_ShouldOverlapSendOutCry(BW_EFFECT_SEND_OUT) == overlap);
                    assert(!BwAdapter_ShouldOverlapSendOutCry(BW_EFFECT_TRAINER_THROW));
                    assert(!BwAdapter_ShouldOverlapSendOutCry(BW_EFFECT_FAINT));
                }
            }
        }
    }
    puts("Intro policy: 120 cases; eligible wild shortening, full trainer openings and special-battle exclusions passed.");
}
