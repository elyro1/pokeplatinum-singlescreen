#ifndef POKEPLATINUM_PORT_GAMEPLAY_CONFIG_H
#define POKEPLATINUM_PORT_GAMEPLAY_CONFIG_H

#include <nitro/types.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    BOOL prohibitBattleItems; // Player Bag items except Poke Balls; held items and trainer items are unaffected.
    BOOL forceSetBattleStyle;
    BOOL unlimitedVitaminEVs; // Retains the normal per-stat and total EV limits.
    u16 bpPricePercent; // Global BP price scale, rounded up; 100 = original, 0 = free.
    u16 rareCandyBPPrice; // Exact price; takes precedence over bpPricePercent. 0 = free.
    BOOL forgettableHMs;
    BOOL unlearnedHMs; // Compatible non-egg party members can use HMs without learning them.
    BOOL freeMoveRelearning;
    BOOL freeMoveTutors;
    BOOL mainStoryMoveTutors;
    BOOL unlimitedGreatMarshSteps;
    BOOL zeroEggCycles; // Eggs start ready to hatch; also applies to existing eggs at the next hatch check.
    BOOL instantHoneyTrees;
    BOOL infiniteTMs;
    BOOL levelUpHeldItemEvolutions;
    BOOL instantSpiritomb;
    BOOL feebasWaterStoneEvolution;
    BOOL disableOverworldPoisonDamage;
    u8 tradeEvolutionLevel; // Plain trade evolutions: 1-100; 0 disables level-up evolution.
    u16 shinyRolls; // Total rolls for new eligible Pokemon; 0 and 1 preserve original odds.
    u8 friendshipGainMultiplier; // Positive friendship gains only; 0 and 1 preserve original gains.
    u8 eggHatchSpeedMultiplier; // 1 = original speed; 0 also falls back to original speed.
    u8 overworldEncounterCount; // Maximum visible wild Pokemon, 0-30.
    u8 catchRateMultiplier; // Final catch rate: 2-255 boosts it; 0 and 1 preserve original rates.
} GameplayConfig;

// Relative to the game working directory, like sim_config_prj.ini and the save files.
#define GAMEPLAY_CONFIG_FILE_PATH "./gameplay_config.ini"

void GameplayConfig_LoadDefaults(GameplayConfig *config);
// Load defaults, create a missing config file, and apply file overrides.
// See GameplayConfig_Init's implementation to disable all file overrides for a ROM hack.
void GameplayConfig_Init(void);
GameplayConfig *GameplayConfig_Get(void);
u16 GameplayConfig_GetBPPrice(u16 item, u16 originalPrice);

// Shared metadata and validated values for the settings editor.
typedef struct {
    const char *name;
    const char *label;
    const char *group;
    const char *description;
    unsigned int maximum;
    BOOL isBoolean;
} GameplayConfigOption;

unsigned int GameplayConfig_OptionCount(void);
BOOL GameplayConfig_GetOption(unsigned int index, GameplayConfigOption *option);
unsigned int GameplayConfig_GetOptionValue(const GameplayConfig *config, unsigned int index);
BOOL GameplayConfig_SetOptionValue(GameplayConfig *config, unsigned int index, unsigned int value);
BOOL GameplayConfig_ParseText(const char *text, GameplayConfig *config);
BOOL GameplayConfig_UsesFileOverrides(void);

#ifdef __cplusplus
}
#endif

#endif
