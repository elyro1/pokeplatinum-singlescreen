#include "port/battle_timing.h"

#include "port/sim_config_prj.h"

static BOOL sActive;
static u32 sVisualEpoch = 1;
static BOOL sSmooth;
static BattlePacing sPacing;
static BOOL sExtraAnimationStep;
static u32 sFrame = 1;
static BOOL sTick = TRUE;
static BattleTimingDomain sDomain;

static void ReadPolicy(void)
{
    const SIM_Config_prj_type *config = SIM_Config_prj_GetConfig();
    BOOL smooth = config->enable60fps && config->battlePacing == BATTLE_PACING_SMOOTH;
    if (smooth != sSmooth || config->battlePacing != sPacing) {
        sVisualEpoch++;
        // A live change keeps task state and starts a fresh reference tick.
        sTick = TRUE;
    }
    sSmooth = smooth;
    sPacing = config->battlePacing;
}

void BattleTiming_SetActive(BOOL active)
{
    sVisualEpoch++;
    sActive = active;
    sExtraAnimationStep = FALSE;
    sTick = TRUE;
    ReadPolicy();
}

BOOL BattleTiming_IsActive(void)
{
    return sActive;
}

void BattleTiming_BeginFrame(void)
{
    sFrame++;
    sTick = !sTick;
    ReadPolicy();
}

u32 BattleTiming_GetFrame(void)
{
    return sFrame;
}

u32 BattleTiming_GetVisualEpoch(void)
{
    return sVisualEpoch;
}

BOOL BattleTiming_ShouldSmoothAnimations(void)
{
    return sActive && sSmooth;
}

BOOL BattleTiming_ShouldTick(BattleTimingDomain domain)
{
    return BattleTiming_StepCount(domain) != 0;
}

int BattleTiming_StepCount(BattleTimingDomain domain)
{
    if (sExtraAnimationStep) {
        return domain == BATTLE_TIMING_ANIMATION;
    }
    if (!sActive || domain != BATTLE_TIMING_ANIMATION) {
        return 1;
    }
    if (sSmooth) {
        return sTick ? 1 : 0;
    }
    const SIM_Config_prj_type *config = SIM_Config_prj_GetConfig();
    if (sDomain != BATTLE_TIMING_ANIMATION && config->enable60fps && sPacing == BATTLE_PACING_TURBO) {
        return sTick ? 2 : 1;
    }
    return 1;
}

BOOL BattleTiming_IsExtraAnimationStep(void)
{
    return sExtraAnimationStep;
}

BOOL BattleTiming_SetExtraAnimationStep(BOOL extra)
{
    BOOL previous = sExtraAnimationStep;
    sExtraAnimationStep = extra;
    return previous;
}

int BattleTiming_PresentationDelay(int frames)
{
    const SIM_Config_prj_type *config = SIM_Config_prj_GetConfig();
    if (sActive && config->enable60fps && config->battlePacing == BATTLE_PACING_TURBO) {
        int delay = (frames + 3) / 4;
        return delay > 4 ? 4 : delay;
    }
    return frames;
}

int BattleTiming_ExitDelay(int frames)
{
    const SIM_Config_prj_type *config = SIM_Config_prj_GetConfig();
    if (sActive && config->enable60fps) {
        if (config->battlePacing == BATTLE_PACING_QUICK) {
            return (frames + 1) / 2;
        }
        if (config->battlePacing == BATTLE_PACING_TURBO) {
            int delay = (frames + 3) / 4;
            return delay > 2 ? 2 : delay;
        }
    }
    return frames;
}

BattleTimingDomain BattleTiming_GetDomain(void)
{
    return sActive ? sDomain : BATTLE_TIMING_NONE;
}

BattleTimingDomain BattleTiming_SetDomain(BattleTimingDomain domain)
{
    BattleTimingDomain previous = sDomain;
    sDomain = sActive ? domain : BATTLE_TIMING_NONE;
    return previous;
}
