#ifndef POKEPLATINUM_PORT_CHEATS_H
#define POKEPLATINUM_PORT_CHEATS_H

#include <nitro/types.h>

// The Tab menu's cheats ([Cheats] in sim_config_prj.ini).
#ifdef SDK_PORT
#include "port/config/sim_config_prj.h"

static inline BOOL Cheats_WalkThroughWalls(void)
{
    return SIM_Config_prj_GetConfig()->walkThroughWalls;
}

// Covers grass and cave encounters and the visible overworld encounters.
static inline BOOL Cheats_NoRandomEncounters(void)
{
    return SIM_Config_prj_GetConfig()->disableRandomEncounters;
}

static inline BOOL Cheats_RunFromTrainerBattles(void)
{
    return SIM_Config_prj_GetConfig()->runFromTrainerBattles;
}
#else
static inline BOOL Cheats_WalkThroughWalls(void)
{
    return FALSE;
}

static inline BOOL Cheats_NoRandomEncounters(void)
{
    return FALSE;
}

static inline BOOL Cheats_RunFromTrainerBattles(void)
{
    return FALSE;
}
#endif

#endif
