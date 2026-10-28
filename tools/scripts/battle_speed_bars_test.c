#include <assert.h>
#include <stdio.h>
#include <string.h>

void *Test_GetOBJCharPtr(void);
#define G2_GetOBJCharPtr Test_GetOBJCharPtr
#include "../../src/battle/healthbox.c"

static SIM_Config_prj_type sConfig;
static u8 sVram[32768];
static NNSG2dImageProxy sProxy;
SIM_Config_prj_type *SIM_Config_prj_GetConfig(void) { return &sConfig; }
void *Test_GetOBJCharPtr(void) { return sVram; }
void MIi_CpuCopy16(const void *source, void *dest, u32 size) { memcpy(dest, source, size); }
NNSG2dImageProxy *Sprite_GetImageProxy(Sprite *sprite) { return &sProxy; }
u8 App_BarColor(u32 cur, u32 max) { return cur * 2 > max ? BARCOLOR_GREEN : cur * 5 > max ? BARCOLOR_YELLOW : BARCOLOR_RED; }

static int Check(int max, int start, int target, int gaugeType, int speed, int pacing, int type)
{
    ManagedSprite sprite = { 0 };
    HealthBox box = { .mainSprite = &sprite, .type = type, .maxHP = max, .curHP = start,
        .damage = start - target, .hpTemp = S32_MIN, .maxExp = max, .curExp = start,
        .expReward = start - target, .expTemp = S32_MIN };
    sConfig.battlePacing = pacing;
    sConfig.battleBarSpeed = speed;
    int previous = start;
    int updates = 0;
    for (;;) {
        int value = HealthBox_DrawGauge(&box, gaugeType);
        ++updates;
        int shown = gaugeType == HEALTHBOX_GAUGE_HP ? box.portShownHP : box.portShownExp;
        int held = gaugeType == HEALTHBOX_GAUGE_HP ? box.portShownHPHeld : box.portShownExpHeld;
        assert(shown >= 0 && shown <= max);
        assert(start <= target ? shown >= previous : shown <= previous);
        assert(held == (value == -1 ? target : start));
        previous = shown;
        if (value == -1) { assert(shown == target); break; }
        assert(updates < 40000);
    }
    if (speed == PORT_SPEED_FASTER && updates > 31) { fprintf(stderr, "max=%d start=%d target=%d gauge=%d updates=%d\n", max, start, target, gaugeType, updates); assert(updates <= 31); }
    return updates;
}

int main(void)
{
    for (int type = HEALTHBOX_TYPE_PLAYER_SOLO; type <= HEALTHBOX_TYPE_ENEMY_SLOT_2; type++) {
        for (int gauge = HEALTHBOX_GAUGE_HP; gauge <= HEALTHBOX_GAUGE_EXP; gauge++) {
            for (int maxIndex = 0; maxIndex < 4; maxIndex++) {
                const int maxima[] = { 1, 30, 999, 10000 };
                int max = maxima[maxIndex];
                const int starts[] = { max, 0, max, max / 2 };
                const int targets[] = { 0, max, max - 1, max / 2 };
                for (int change = 0; change < 4; change++) {
                    int fastUpdates = 0;
                    for (int speed = 0; speed < PORT_SPEED_COUNT; speed++) {
                        if (speed == PORT_SPEED_INSTANT) continue;
                        int reference = -1;
                        for (int pacing = 0; pacing < BATTLE_PACING_COUNT; pacing++) {
                            int updates = Check(max, starts[change], targets[change], gauge, speed, pacing, type);
                            if (reference == -1) reference = updates;
                            assert(updates == reference); // Effect pacing never changes bars.
                        }
                        if (speed == PORT_SPEED_FAST) fastUpdates = reference;
                        if (speed == PORT_SPEED_NORMAL) assert(reference == 2 * fastUpdates - 1);
                    }
                }
            }
        }
    }
    // EXP completion remains compatible with a caller starting the next level's segment.
    HealthBox box = { .maxExp = 100, .curExp = 80, .mainSprite = &(ManagedSprite){ 0 } };
    sConfig.battleBarSpeed = PORT_SPEED_NORMAL;
    HealthBox_CalcExp(&box, 1000);
    while (Healthbox_DrawExpBar(&box) != -1) {}
    assert(box.curExp == 100 && box.portShownExp == 100);
    box.maxExp = 150;
    box.curExp = 0;
    HealthBox_CalcExp(&box, 60);
    while (Healthbox_DrawExpBar(&box) != -1) {}
    assert(box.curExp == 60 && box.portShownExp == 60);
    puts("Bar speed: 1,728 cases cover both gauges, all box types, pacing independence, healing, fainting and level segments.");
}
