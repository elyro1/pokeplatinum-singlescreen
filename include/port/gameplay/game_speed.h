#ifndef POKEPLATINUM_PORT_GAME_SPEED_H
#define POKEPLATINUM_PORT_GAME_SPEED_H

#include <nitro/types.h>

#include "port/config/sim_config_prj.h"

// The speed settings. Battle pacing's effects on animations and pauses are in
// port/gameplay/battle_timing.h; these return the chosen values.
#ifdef SDK_PORT
// Overrides the saved text speed, in and out of battle.
static inline PortSpeed GameSpeed_Text(void)
{
    return SIM_Config_prj_GetConfig()->textSpeed;
}

// HP and EXP bars, independent of effects and text.
static inline PortSpeed GameSpeed_BattleBars(void)
{
    return SIM_Config_prj_GetConfig()->battleBarSpeed;
}

static inline BattlePacing GameSpeed_BattlePacing(void)
{
    return SIM_Config_prj_GetConfig()->battlePacing;
}
#else
static inline PortSpeed GameSpeed_Text(void)
{
    return PORT_SPEED_NORMAL;
}

static inline PortSpeed GameSpeed_BattleBars(void)
{
    return PORT_SPEED_NORMAL;
}

static inline BattlePacing GameSpeed_BattlePacing(void)
{
    return BATTLE_PACING_SMOOTH;
}
#endif

#endif
