#ifndef POKEPLATINUM_PORT_WIDESCREEN_H
#define POKEPLATINUM_PORT_WIDESCREEN_H

#include <nitro/types.h>

// The single-screen layout and the wide screens that build on it. Each screen reads
// its setting when it opens, so a change applies the next time it does.
#ifdef SDK_PORT
#include <simulator/sim.h>

#include "port/config/sim_config_prj.h"

// Only the top screen is shown (the field then widens to fit the window).
static inline BOOL Widescreen_IsSingleScreen(void)
{
    return SIM_GetConfigPtr()->screenLayout == SIM_CONFIG_SCREEN_LAYOUT_TOP_ONLY;
}

static inline BOOL Widescreen_AreScreensSwapped(void)
{
    return SIM_GetConfigPtr()->swapScreens;
}

static inline BOOL Widescreen_PokedexEnabled(void)
{
    return SIM_Config_prj_GetConfig()->widescreenPokedex;
}

static inline BOOL Widescreen_SummaryEnabled(void)
{
    return SIM_Config_prj_GetConfig()->widescreenSummary;
}

static inline BOOL Widescreen_EvolutionEnabled(void)
{
    return SIM_Config_prj_GetConfig()->widescreenEvolution;
}

// The grid bag replaces the classic bag only in the single-screen layout.
static inline BOOL Widescreen_GridBagEnabled(void)
{
    return SIM_Config_prj_GetConfig()->gridBag && Widescreen_IsSingleScreen();
}
#else
static inline BOOL Widescreen_IsSingleScreen(void)
{
    return FALSE;
}

static inline BOOL Widescreen_AreScreensSwapped(void)
{
    return FALSE;
}

static inline BOOL Widescreen_PokedexEnabled(void)
{
    return FALSE;
}

static inline BOOL Widescreen_SummaryEnabled(void)
{
    return FALSE;
}

static inline BOOL Widescreen_EvolutionEnabled(void)
{
    return FALSE;
}

static inline BOOL Widescreen_GridBagEnabled(void)
{
    return FALSE;
}
#endif

#endif
