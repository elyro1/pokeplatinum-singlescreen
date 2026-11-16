#ifndef POKEPLATINUM_PORT_PORT_OPTIONS_H
#define POKEPLATINUM_PORT_PORT_OPTIONS_H

#include <nitro/types.h>

#include "port/config/sim_config_prj.h"

// The Tab menu's quality-of-life options that decomp code checks. Each returns the
// original behaviour on the DS build. (Gameplay rules are in gameplay_config.h.)
#ifdef SDK_PORT
#include "port/assets/follower_assets.h"
#include "port/gameplay/cheats.h"

// Automatic nickname offers; explicit renaming stays available.
static inline NicknameMode PortOptions_NicknameMode(void)
{
    return SIM_Config_prj_GetConfig()->nicknameMode;
}

static inline RunningShoesMode PortOptions_RunningShoes(void)
{
    return SIM_Config_prj_GetConfig()->runningShoes;
}

static inline PokedexUnits PortOptions_PokedexUnits(void)
{
    return SIM_Config_prj_GetConfig()->pokedexUnits;
}

static inline BOOL PortOptions_LowHPBeepDisabled(void)
{
    return SIM_Config_prj_GetConfig()->disableLowHPBeep;
}

static inline BOOL PortOptions_FastWildBattleIntro(void)
{
    return SIM_Config_prj_GetConfig()->fastWildBattleIntro;
}

// The wild Pokemon Pokétch app. It is never saved, so turning this off removes it cleanly.
static inline BOOL PortOptions_ImprovedPoketch(void)
{
    return SIM_Config_prj_GetConfig()->improvedPoketch;
}

// The setting alone. BwAdapter_IsEnabled also needs every BW2 battle archive; pieces such as
// the nickname backdrop check for their own archive.
static inline BOOL PortOptions_Bw2BattleVisuals(void)
{
    return SIM_Config_prj_GetConfig()->bw2BattleVisuals;
}

// Followers and overworld encounters also need the sprites from a BW2 ROM.
static inline BOOL PortOptions_FollowersEnabled(void)
{
    return SIM_Config_prj_GetConfig()->pokemonFollower && FollowerAssets_Ready();
}

static inline BOOL PortOptions_OverworldEncountersEnabled(void)
{
    return SIM_Config_prj_GetConfig()->overworldEncounters && FollowerAssets_Ready()
        && !Cheats_NoRandomEncounters();
}

// Maximum visible wild Pokémon, 0-30.
static inline int PortOptions_OverworldEncounterLimit(void)
{
    return SIM_Config_prj_GetConfig()->overworldEncounterCount;
}
#else
static inline NicknameMode PortOptions_NicknameMode(void)
{
    return NICKNAME_MODE_ASK;
}

static inline RunningShoesMode PortOptions_RunningShoes(void)
{
    return RUNNING_SHOES_HOLD;
}

static inline PokedexUnits PortOptions_PokedexUnits(void)
{
    return POKEDEX_UNITS_IMPERIAL; // The US release
}

static inline BOOL PortOptions_LowHPBeepDisabled(void)
{
    return FALSE;
}

static inline BOOL PortOptions_FastWildBattleIntro(void)
{
    return FALSE;
}

static inline BOOL PortOptions_Bw2BattleVisuals(void)
{
    return FALSE;
}

static inline BOOL PortOptions_FollowersEnabled(void)
{
    return FALSE;
}

static inline BOOL PortOptions_OverworldEncountersEnabled(void)
{
    return FALSE;
}

static inline int PortOptions_OverworldEncounterLimit(void)
{
    return 0;
}
#endif

#endif
