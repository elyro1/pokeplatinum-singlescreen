#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "../../src/port/sim_config_prj.c"
#include "../../src/port/battle_timing.c"

static void Load(const char *text)
{
    FILE *file = fopen("sim_config_prj.ini", "w");
    assert(file);
    fputs(text, file);
    assert(fclose(file) == 0);
    SIM_Config_prj_LoadDefaults(&s_SIM_config_prj);
    assert(SIM_Config_prj_LoadConfigFile(&s_SIM_config_prj));
}

int main(void)
{
    SIM_Config_prj_type *config = SIM_Config_prj_GetConfig();
    SIM_Config_prj_LoadDefaults(config);
    assert(config->enable60fps && config->battlePacing == BATTLE_PACING_SMOOTH);
    assert(config->textSpeed == PORT_SPEED_FAST && config->battleBarSpeed == PORT_SPEED_FAST);
    assert(config->nicknameMode == NICKNAME_MODE_ASK);
    assert(config->fastWildBattleIntro == TRUE);
    assert(config->overworldEncounterCount == OVERWORLD_ENCOUNTER_DEFAULT_COUNT);
    const int encounterCounts[] = { 0, 12, OVERWORLD_ENCOUNTER_CAPACITY };
    for (unsigned i = 0; i < NELEMS(encounterCounts); i++) {
        char text[80];
        snprintf(text, sizeof(text), "[Developer]\nOverworldEncounterCount=%d\n", encounterCounts[i]);
        Load(text);
        assert(config->overworldEncounterCount == encounterCounts[i]);
        SIM_Config_prj_SaveConfigFile(config);
        SIM_Config_prj_LoadDefaults(config);
        assert(SIM_Config_prj_LoadConfigFile(config));
        assert(config->overworldEncounterCount == encounterCounts[i]);
    }
    const char *invalidCounts[] = { "-1", "31", "2oops", "2.5", "", "999999999999999999999999999999999999" };
    for (unsigned i = 0; i < NELEMS(invalidCounts); i++) {
        char text[120];
        snprintf(text, sizeof(text), "[Developer]\nOverworldEncounterCount=%s\n", invalidCounts[i]);
        Load(text);
        assert(config->overworldEncounterCount == OVERWORLD_ENCOUNTER_DEFAULT_COUNT);
    }
    const char *nicknameModes[] = { "Ask", "Always", "Never" };
    for (int mode = 0; mode < NICKNAME_MODE_COUNT; mode++) {
        char text[80];
        snprintf(text, sizeof(text), "[General]\nNicknameMode=%s\n", nicknameModes[mode]);
        Load(text);
        assert(config->nicknameMode == mode);
        SIM_Config_prj_SaveConfigFile(config);
        SIM_Config_prj_LoadDefaults(config);
        assert(SIM_Config_prj_LoadConfigFile(config));
        assert(config->nicknameMode == mode);
    }
    Load("[General]\nNicknameMode=invalid\n");
    assert(config->nicknameMode == NICKNAME_MODE_ASK);
    Load("[General]\n");
    assert(config->nicknameMode == NICKNAME_MODE_ASK);
    assert(config->fastWildBattleIntro == TRUE);
    Load("[General]\nFastWildBattleIntro=true\n");
    assert(config->fastWildBattleIntro == TRUE);
    SIM_Config_prj_SaveConfigFile(config);
    SIM_Config_prj_LoadDefaults(config);
    assert(SIM_Config_prj_LoadConfigFile(config) && config->fastWildBattleIntro == TRUE);
    Load("[General]\nFastWildBattleIntro=false\n");
    assert(config->fastWildBattleIntro == FALSE);
    SIM_Config_prj_SaveConfigFile(config);
    SIM_Config_prj_LoadDefaults(config);
    assert(SIM_Config_prj_LoadConfigFile(config) && config->fastWildBattleIntro == FALSE);
    Load("[General]\nBattlePacing=Original\nBattleUiSpeedFix=true\n");
    assert(config->battlePacing == BATTLE_PACING_SMOOTH);
    Load("[General]\nBattleAnimationSpeedFix=false\nSmoothBattleAnimations=true\n");
    assert(config->battlePacing == BATTLE_PACING_QUICK);
    Load("[General]\nBattleAnimationSpeedFix=false\nBattlePacing=Smooth\n");
    assert(config->battlePacing == BATTLE_PACING_SMOOTH);
    Load("[General]\nBattlePacing=bad\nTextSpeed=bad\nBattleBarSpeed=bad\n");
    assert(config->battlePacing == BATTLE_PACING_SMOOTH);
    assert(config->textSpeed == PORT_SPEED_FAST && config->battleBarSpeed == PORT_SPEED_FAST);
    Load("[General]\nTextSpeed=Faster\nTextSpeed=bad\nBattleBarSpeed=Instant\n");
    assert(config->textSpeed == PORT_SPEED_FASTER && config->battleBarSpeed == PORT_SPEED_FASTER);

    for (int pacing = 0; pacing < BATTLE_PACING_COUNT; pacing++) {
        for (int text = 0; text < PORT_SPEED_COUNT; text++) {
            for (int bar = 0; bar < PORT_SPEED_COUNT; bar++) {
                if (bar == PORT_SPEED_INSTANT) continue;
                for (int bw = 0; bw < 2; bw++) {
                    config->battlePacing = pacing;
                    config->textSpeed = text;
                    config->battleBarSpeed = bar;
                    config->bw2BattleVisuals = bw;
                    SIM_Config_prj_SaveConfigFile(config);
                    SIM_Config_prj_LoadDefaults(config);
                    assert(SIM_Config_prj_LoadConfigFile(config));
                    assert(config->battlePacing == pacing && config->textSpeed == text);
                    assert(config->battleBarSpeed == bar && config->bw2BattleVisuals == bw);
                    BattleTiming_SetActive(TRUE);
                    int animationTicks = 0;
                    int animationSteps = 0;
                    for (int i = 0; i < 60; i++) {
                        BattleTiming_BeginFrame();
                        animationTicks += BattleTiming_ShouldTick(BATTLE_TIMING_ANIMATION);
                        animationSteps += BattleTiming_StepCount(BATTLE_TIMING_ANIMATION);
                        assert(BattleTiming_ShouldTick(BATTLE_TIMING_UI));
                        assert(BattleTiming_ShouldTick(BATTLE_TIMING_NONE));
                    }
                    assert(animationTicks == (pacing == BATTLE_PACING_SMOOTH ? 30 : 60));
                    assert(animationSteps == (pacing == BATTLE_PACING_SMOOTH ? 30 : (pacing == BATTLE_PACING_QUICK ? 60 : 90)));
                    assert(BattleTiming_ShouldSmoothAnimations() == (pacing == BATTLE_PACING_SMOOTH));
                    assert(BattleTiming_PresentationDelay(30) == (pacing == BATTLE_PACING_TURBO ? 4 : 30));
                    assert(BattleTiming_PresentationDelay(1) == 1);
                    assert(BattleTiming_PresentationDelay(0) == 0);
                    assert(BattleTiming_ExitDelay(30) == (pacing == BATTLE_PACING_SMOOTH ? 30 : (pacing == BATTLE_PACING_QUICK ? 15 : 2)));
                    assert(BattleTiming_ExitDelay(1) == 1);
                    assert(BattleTiming_ExitDelay(0) == 0);
                }
            }
        }
    }
    // Switching during a battle starts on a reference tick, without resetting tasks.
    config->battlePacing = BATTLE_PACING_QUICK;
    BattleTiming_BeginFrame();
    u32 epoch = BattleTiming_GetVisualEpoch();
    config->battlePacing = BATTLE_PACING_SMOOTH;
    BattleTiming_BeginFrame();
    assert(BattleTiming_GetVisualEpoch() != epoch);
    assert(BattleTiming_ShouldTick(BATTLE_TIMING_ANIMATION));
    BattleTiming_SetActive(FALSE);
    assert(BattleTiming_PresentationDelay(30) == 30);
    assert(BattleTiming_ExitDelay(30) == 30);
    assert(!BattleTiming_ShouldSmoothAnimations());
    assert(BattleTiming_ShouldTick(BATTLE_TIMING_ANIMATION));
    puts("Speed config/timing: migration, persistence, 72 combinations and live changes passed.");
}
