#ifndef SIM_CONFIG_PRJ_H
#define SIM_CONFIG_PRJ_H

#include <nitro/types.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    BATTLE_PACING_SMOOTH,
    BATTLE_PACING_QUICK,
    BATTLE_PACING_TURBO,
    BATTLE_PACING_COUNT,
} BattlePacing;

typedef enum {
    PORT_SPEED_FAST,
    PORT_SPEED_FASTER,
    PORT_SPEED_INSTANT,
    PORT_SPEED_NORMAL,
    PORT_SPEED_COUNT,
} PortSpeed;

typedef enum {
    NICKNAME_MODE_ASK,
    NICKNAME_MODE_ALWAYS,
    NICKNAME_MODE_NEVER,
    NICKNAME_MODE_COUNT,
} NicknameMode;

typedef enum {
    POKEDEX_UNITS_METRIC,
    POKEDEX_UNITS_IMPERIAL,
    POKEDEX_UNITS_COUNT,
} PokedexUnits;

typedef struct {
    BOOL enable60fps; // Speed correction is automatic at 60 FPS.
    BattlePacing battlePacing;
    PortSpeed textSpeed; // Overrides the saved text setting, in and out of battle.
    PortSpeed battleBarSpeed; // HP/EXP progression, independent of effects and text.
    NicknameMode nicknameMode; // Automatic nickname offers; explicit renaming remains available.
    BOOL newPokedex; // The widescreen Pokédex; off keeps the classic screens. Read when the Pokédex opens.
    PokedexUnits pokedexUnits; // Heights and weights in the Pokédex.
    BOOL fastWildBattleIntro; // Shorten eligible BW2 wild battle intros.
    BOOL bwMonSprites; // BW2's art for the summary screen, the Pokédex and the icons, when its assets are extracted
    BOOL bw2BattleVisuals; // Show battles with Black 2 / White 2's view when its assets are extracted (docs/bwbattle.md)
    BOOL overworldEncounters;
    int overworldEncounterCount; // Maximum visible wild Pokemon, 0-30.
    BOOL autosave; // Periodically archive progress separately from manual saves.
    BOOL pokemonFollower;
    BOOL enableAsserts; // Should asserts cause a communication error?
    BOOL breakDebuggerOnGfAssert; // When true, break the debugger when a GF_ASSERT fails
    BOOL enhancedDayNight;
    BOOL regionalAtmosphere;
    BOOL antialias3D;
    BOOL correctLighting;
    BOOL environmentalParticles;
    BOOL softContactShadows;
    BOOL castShadows;
    BOOL waterEffects;
    BOOL weatherEffects;
    BOOL nightLighting;
    BOOL nightGlows;
    BOOL emissiveBloom;

    // Cheats section
    BOOL walkThroughWalls; // When enabled, allows the player to walk through walls
    BOOL disableRandomEncounters; // When enabled, all random encounters are turned off
    BOOL runFromTrainerBattles; // When enabled, allows running away from trainer battles
} SIM_Config_prj_type;

void SIM_Config_prj_LoadDefaults(SIM_Config_prj_type * aConfig);
BOOL SIM_Config_prj_LoadConfigFile(SIM_Config_prj_type * aConfig);
void SIM_Config_prj_SaveConfigFile(SIM_Config_prj_type * aConfig);
void SIM_Config_prj_init();
SIM_Config_prj_type * SIM_Config_prj_GetConfig();

#ifdef __cplusplus
}
#endif

#endif
