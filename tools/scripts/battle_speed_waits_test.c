#include <assert.h>
#include <stdio.h>

#include "../../src/battle/battle_script.c"
#include "../../src/port/battle_timing.c"

static SIM_Config_prj_type sConfig;
System gSystem;
SIM_Config_prj_type *SIM_Config_prj_GetConfig(void) { return &sConfig; }
u32 BattleSystem_GetBattleType(BattleSystem *sys) { return sys->battleType; }
BOOL TouchScreen_Tapped(void) { return FALSE; }
void ErrorHandling_AssertFail(void) { assert(0); }

static int Wait(BOOL buttonWait, u32 battleType, BOOL press, BOOL exiting)
{
    BattleSystem sys = { .battleType = battleType };
    BattleContext ctx = { 0 };
    ctx.battleScript[1] = 30;
    ctx.scriptNarc = NARC_INDEX_BATTLE__SKILL__SUB_SEQ;
    ctx.scriptFile = exiting ? subscript_escape : subscript_escape_failed;
    gSystem.pressedKeys = press ? PAD_BUTTON_A : 0;
    int updates = 0;
    do {
        BattleTiming_BeginFrame();
        if (buttonWait) BtlCmd_WaitButtonABTime(&sys, &ctx);
        else BtlCmd_WaitTime(&sys, &ctx);
        updates++;
        assert(updates <= 31);
    } while (ctx.scriptCursor == 0);
    assert(ctx.scriptCursor == 2 && ctx.waitCounter == 0);
    return updates;
}

static void CheckFastExit(u32 battleType, BOOL shouldSkip)
{
    BattleSystem sys = { .battleType = battleType };
    BattleContext ctx = { 0 };
    ctx.battleScript[1] = 7;
    ctx.battleMons[0].status = MON_CONDITION_POISON;
    ctx.battleMons[1].status = MON_CONDITION_BURN;
    BtlCmd_JumpIfFastExit(&sys, &ctx);
    assert(ctx.scriptCursor == (shouldSkip ? 9 : 2));
    // This only skips presentation; escape checks, status cleanup and results
    // remain owned by the controller and the surrounding escape script.
    assert(ctx.battleMons[0].status == MON_CONDITION_POISON);
    assert(ctx.battleMons[1].status == MON_CONDITION_BURN);
    assert(!ctx.battleProgressFlag);
}

int main(void)
{
    sConfig.enable60fps = TRUE;
    BattleTiming_SetActive(TRUE);
    for (int pacing = 0; pacing < BATTLE_PACING_COUNT; pacing++) {
        sConfig.battlePacing = pacing;
        for (int text = 0; text < PORT_SPEED_COUNT; text++) {
            for (int bars = 0; bars < PORT_SPEED_COUNT; bars++) {
                if (bars == PORT_SPEED_INSTANT) continue;
                sConfig.textSpeed = text;
                sConfig.battleBarSpeed = bars;
                for (int button = 0; button < 2; button++) {
                    assert(Wait(button, 0, FALSE, FALSE) == (pacing == BATTLE_PACING_TURBO ? 5 : 31));
                    assert(Wait(button, BATTLE_TYPE_LINK, FALSE, FALSE) == 16);
                }
                BOOL fastExit = pacing != BATTLE_PACING_SMOOTH;
                CheckFastExit(0, fastExit);
                CheckFastExit(BATTLE_TYPE_DOUBLES, fastExit);
                CheckFastExit(BATTLE_TYPE_SAFARI, fastExit);
                CheckFastExit(BATTLE_TYPE_LINK, FALSE);
                CheckFastExit(BATTLE_TYPE_FRONTIER, FALSE);
                CheckFastExit(BATTLE_TYPE_LINK | BATTLE_TYPE_DOUBLES, FALSE);
                // Escape pauses that are shown still scale and retain button skip.
                int exitUpdates = pacing == BATTLE_PACING_SMOOTH ? 31 : (pacing == BATTLE_PACING_QUICK ? 16 : 3);
                assert(Wait(TRUE, 0, FALSE, TRUE) == exitUpdates);
                assert(Wait(TRUE, 0, TRUE, TRUE) == 1);
                assert(Wait(TRUE, BATTLE_TYPE_LINK, FALSE, TRUE) == 16);
                assert(Wait(TRUE, BATTLE_TYPE_LINK, TRUE, TRUE) == 16);
                assert(Wait(TRUE, 0, TRUE, FALSE) == 1);
                assert(Wait(FALSE, 0, TRUE, FALSE) == (pacing == BATTLE_PACING_TURBO ? 5 : 31));
            }
        }
    }
    sConfig.enable60fps = FALSE;
    CheckFastExit(0, FALSE);
    assert(Wait(TRUE, 0, FALSE, TRUE) == 31);
    sConfig.enable60fps = TRUE;
    BattleTiming_SetActive(FALSE);
    CheckFastExit(0, FALSE);
    assert(Wait(TRUE, 0, FALSE, TRUE) == 31);
    puts("Battle waits: Quick/Turbo Run skips to fade; other exit pauses, text/bar independence, button skips and link/Frontier/30 FPS timing preserved.");
}
