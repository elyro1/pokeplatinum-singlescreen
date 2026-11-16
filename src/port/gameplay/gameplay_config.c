#include "port/gameplay/gameplay_config.h"

#include <errno.h>
#include <simulator/config/config_check.h>
#include <simulator/config/ini.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "constants/items.h"

static GameplayConfig sGameplayConfig;
static BOOL sUsesFileOverrides;
static SIM_ConfigCheck sCheck;

typedef struct {
    GameplayConfig *config;
    SIM_ConfigCheck *check; // NULL: no warnings, e.g. for the settings editor's text
} ParseContext;

void GameplayConfig_LoadDefaults(GameplayConfig *config)
{
    GameplayConfig_LoadPreset(config, GAMEPLAY_PRESET_SINGLE_SCREEN_MOD);
    // Set a ROM hack's built-in gameplay defaults here, e.g. config->prohibitBattleItems = TRUE;
    // docs/gameplay_config.md describes every option.
}

void GameplayConfig_LoadPreset(GameplayConfig *config, enum GameplayPreset preset)
{
    // Original Platinum rules, independent of a ROM hack's defaults above.
    memset(config, 0, sizeof(*config));
    config->bpPricePercent = 100;
    config->rareCandyBPPrice = 48;
    config->shinyRolls = 1;
    config->friendshipGainMultiplier = 1;
    config->eggHatchSpeedMultiplier = 1;
    config->catchRateMultiplier = 1;
    if (preset != GAMEPLAY_PRESET_SINGLE_SCREEN_MOD) {
        return;
    }

    // Less grinding and backtracking; battle rules and catch rates stay original.
    // gameplay_config.example.ini holds the same values.
    config->unlimitedVitaminEVs = TRUE;
    config->bpPricePercent = 25;
    config->rareCandyBPPrice = 1;
    config->forgettableHMs = TRUE;
    config->instantHoneyTrees = TRUE;
    config->guaranteedFishing = TRUE;
    config->infiniteTMs = TRUE;
    config->levelUpHeldItemEvolutions = TRUE;
    config->instantSpiritomb = TRUE;
    config->feebasWaterStoneEvolution = TRUE;
    config->tradeEvolutionLevel = 40;
    config->shinyRolls = 16;
    config->eggHatchSpeedMultiplier = 5;
}

const char *GameplayConfig_PresetName(enum GameplayPreset preset)
{
    return preset == GAMEPLAY_PRESET_SINGLE_SCREEN_MOD ? "Single-screen mod" : "Classic Platinum";
}

typedef enum {
    CONFIG_BOOL,
    CONFIG_U8,
    CONFIG_U16,
} ConfigFieldType;

typedef struct {
    const char *name;
    size_t offset;
    ConfigFieldType type;
    unsigned int maximum;
    const char *description;
    const char *label;
    const char *group;
} ConfigField;

#define FIELD(name, type, maximum, description, label, group) { #name, offsetof(GameplayConfig, name), type, maximum, description, label, group }

// Shared by the reader and template writer so every option has the same name and range.
static const ConfigField sConfigFields[] = {
    FIELD(prohibitBattleItems, CONFIG_BOOL, 1, "Poke Balls still work; held items and opponents' items are unaffected.", "Block player battle items", "Battle rules"),
    FIELD(forceSetBattleStyle, CONFIG_BOOL, 1, NULL, "Always use Set battle style", "Battle rules"),
    FIELD(unlimitedVitaminEVs, CONFIG_BOOL, 1, NULL, "Vitamins past 100 EVs", "Progression & prices"),
    FIELD(bpPricePercent, CONFIG_U16, 200, "100 = original prices, 0 = free. Rounded up.", "BP price percentage", "Progression & prices"),
    FIELD(rareCandyBPPrice, CONFIG_U16, 100, "Overrides the BP price percentage. 0 = free.", "Rare Candy BP price", "Progression & prices"),
    FIELD(forgettableHMs, CONFIG_BOOL, 1, NULL, "Allow replacing HM moves", "Moves & tutors"),
    FIELD(unlearnedHMs, CONFIG_BOOL, 1, "Any compatible party Pokemon can use the field move; badges are still required.", "Use HMs without teaching them", "Moves & tutors"),
    FIELD(freeMoveRelearning, CONFIG_BOOL, 1, NULL, "Free move relearning", "Moves & tutors"),
    FIELD(freeMoveTutors, CONFIG_BOOL, 1, NULL, "Free shard move tutors", "Moves & tutors"),
    FIELD(mainStoryMoveTutors, CONFIG_BOOL, 1, "The Route 212 tutor teaches the moves of all three shard tutors, including the post-game ones.", "All tutor moves on Route 212", "Moves & tutors"),
    FIELD(unlimitedGreatMarshSteps, CONFIG_BOOL, 1, NULL, "Unlimited Great Marsh steps", "Encounters & events"),
    FIELD(zeroEggCycles, CONFIG_BOOL, 1, "New and existing eggs hatch at the next hatch check.", "Eggs ready at next hatch check", "Evolution & eggs"),
    FIELD(instantHoneyTrees, CONFIG_BOOL, 1, NULL, "Instant Honey Tree encounters", "Encounters & events"),
    FIELD(guaranteedFishing, CONFIG_BOOL, 1, NULL, "Guaranteed fishing", "Encounters & events"),
    FIELD(infiniteTMs, CONFIG_BOOL, 1, NULL, "Reusable TMs", "Moves & tutors"),
    FIELD(levelUpHeldItemEvolutions, CONFIG_BOOL, 1, "Trade and time-of-day item evolutions happen on level-up while holding the item; the item is used up.", "Held-item evolutions on level-up", "Evolution & eggs"),
    FIELD(instantSpiritomb, CONFIG_BOOL, 1, NULL, "Skip Spiritomb conversations", "Encounters & events"),
    FIELD(feebasWaterStoneEvolution, CONFIG_BOOL, 1, "Replaces the Beauty requirement.", "Evolve Feebas with a Water Stone", "Evolution & eggs"),
    FIELD(disableOverworldPoisonDamage, CONFIG_BOOL, 1, NULL, "Disable walking poison damage", "Progression & prices"),
    FIELD(tradeEvolutionLevel, CONFIG_U8, 100, "Trade evolutions without an item also happen on level-up from this level. 0 = off.", "Trade evolution level", "Evolution & eggs"),
    FIELD(shinyRolls, CONFIG_U16, 100, "Each roll has a 1 in 8192 chance. 1 = original odds.", "Shiny rolls", "Encounters & events"),
    FIELD(friendshipGainMultiplier, CONFIG_U8, 10, NULL, "Friendship gain multiplier", "Progression & prices"),
    FIELD(eggHatchSpeedMultiplier, CONFIG_U8, 10, "Divides the steps between hatch checks. 1 = original speed.", "Egg hatch speed multiplier", "Evolution & eggs"),
    FIELD(catchRateMultiplier, CONFIG_U8, 10, "Applied after ball and status bonuses. 1 = original rates.", "Catch rate multiplier", "Encounters & events"),
};

#undef FIELD

unsigned int GameplayConfig_OptionCount(void)
{
    return sizeof(sConfigFields) / sizeof(sConfigFields[0]);
}

BOOL GameplayConfig_GetOption(unsigned int index, GameplayConfigOption *option)
{
    if (index >= GameplayConfig_OptionCount() || option == NULL) return FALSE;
    const ConfigField *field = &sConfigFields[index];
    option->name = field->name;
    option->label = field->label;
    option->group = field->group;
    option->description = field->description;
    option->maximum = field->maximum;
    option->isBoolean = field->type == CONFIG_BOOL;
    return TRUE;
}

unsigned int GameplayConfig_GetOptionValue(const GameplayConfig *config, unsigned int index)
{
    if (config == NULL || index >= GameplayConfig_OptionCount()) return 0;
    const ConfigField *field = &sConfigFields[index];
    const char *source = (const char *)config + field->offset;
    switch (field->type) {
    case CONFIG_BOOL: return *(const BOOL *)source ? 1 : 0;
    case CONFIG_U8: return *(const u8 *)source;
    case CONFIG_U16: return *(const u16 *)source;
    }
    return 0;
}

BOOL GameplayConfig_SetOptionValue(GameplayConfig *config, unsigned int index, unsigned int value)
{
    if (config == NULL || index >= GameplayConfig_OptionCount()) return FALSE;
    const ConfigField *field = &sConfigFields[index];
    if (value > field->maximum) return FALSE;
    char *destination = (char *)config + field->offset;
    switch (field->type) {
    case CONFIG_BOOL: *(BOOL *)destination = value ? TRUE : FALSE; break;
    case CONFIG_U8: *(u8 *)destination = (u8)value; break;
    case CONFIG_U16: *(u16 *)destination = (u16)value; break;
    }
    return TRUE;
}

BOOL GameplayConfig_UsesFileOverrides(void)
{
    return sUsesFileOverrides;
}

static int GameplayConfig_IniHandler(void *user, const char *section, const char *name, const char *value, int line)
{
    ParseContext *context = user;

    if (context->check != NULL && !SIM_ConfigCheck_Key(context->check, section, name, line)) {
        return 1;
    }
    if (strcmp(section, "Gameplay") != 0) {
        return 1;
    }

    for (size_t i = 0; i < sizeof(sConfigFields) / sizeof(sConfigFields[0]); i++) {
        const ConfigField *field = &sConfigFields[i];
        if (strcmp(name, field->name) != 0) {
            continue;
        }

        unsigned long number;
        if (field->type == CONFIG_BOOL) {
            if (strcmp(value, "true") == 0 || strcmp(value, "TRUE") == 0 || strcmp(value, "1") == 0) {
                number = 1;
            } else if (strcmp(value, "false") == 0 || strcmp(value, "FALSE") == 0 || strcmp(value, "0") == 0) {
                number = 0;
            } else {
                // Ignore invalid values, retaining the default.
                if (context->check != NULL) {
                    SIM_ConfigCheck_BadValue(context->check, section, name, value, line, "true or false");
                }
                return 1;
            }
        } else {
            char *end = NULL;
            errno = 0;
            if (value[0] >= '0' && value[0] <= '9') {
                number = strtoul(value, &end, 10);
            }
            if (end == NULL || errno == ERANGE || *end != '\0' || number > field->maximum) {
                if (context->check != NULL) {
                    char expected[48];
                    snprintf(expected, sizeof(expected), "a number from 0 to %u", field->maximum);
                    SIM_ConfigCheck_BadValue(context->check, section, name, value, line, expected);
                }
                return 1;
            }
        }

        char *destination = (char *)context->config + field->offset;
        switch (field->type) {
        case CONFIG_BOOL:
            *(BOOL *)destination = number ? TRUE : FALSE;
            break;
        case CONFIG_U8:
            *(u8 *)destination = (u8)number;
            break;
        case CONFIG_U16:
            *(u16 *)destination = (u16)number;
            break;
        }
        break;
    }
    return 1; // Unknown keys are ignored for compatibility with newer configurations.
}

BOOL GameplayConfig_ParseText(const char *text, GameplayConfig *config)
{
    if (text == NULL || config == NULL) return FALSE;
    GameplayConfig parsed;
    GameplayConfig_LoadDefaults(&parsed);
    ParseContext context = { &parsed, NULL };
    if (ini_parse_string(text, GameplayConfig_IniHandler, &context) != 0) return FALSE;
    *config = parsed;
    return TRUE;
}

static BOOL GameplayConfig_GenerateFile(void)
{
    // Exclusive creation preserves a player's existing overrides.
    FILE *file = fopen(GAMEPLAY_CONFIG_FILE_PATH, "wx");
    if (file == NULL) {
        return FALSE;
    }

    GameplayConfig defaults;
    GameplayConfig_LoadDefaults(&defaults);
    fprintf(file, "; Gameplay overrides. Edit this file and restart the game to apply changes.\n"
                  "; Delete this file and restart to regenerate built-in defaults. Missing/invalid values keep their defaults.\n"
                  "; Booleans accept true/false or 1/0. Numeric values must be non-negative integers.\n\n"
                  "[Gameplay]\n");
    for (size_t i = 0; i < sizeof(sConfigFields) / sizeof(sConfigFields[0]); i++) {
        const ConfigField *field = &sConfigFields[i];
        const char *source = (const char *)&defaults + field->offset;
        fprintf(file, "\n; %s\n", field->description != NULL ? field->description : field->label);
        switch (field->type) {
        case CONFIG_BOOL:
            fprintf(file, "%s=%s\n", field->name, *(const BOOL *)source ? "true" : "false");
            break;
        case CONFIG_U8:
            fprintf(file, "; Range: 0-%u\n%s=%u\n", field->maximum, field->name, (unsigned int)*(const u8 *)source);
            break;
        case CONFIG_U16:
            fprintf(file, "; Range: 0-%u\n%s=%u\n", field->maximum, field->name, (unsigned int)*(const u16 *)source);
            break;
        }
    }
    BOOL success = !ferror(file);
    if (fclose(file) != 0) {
        success = FALSE;
    }
    if (!success) {
        remove(GAMEPLAY_CONFIG_FILE_PATH);
    }
    return success;
}

// Every key with its value, for SIM_ConfigCheck and --print-config
static void GameplayConfig_WriteValues(SIM_ConfigText *text, const GameplayConfig *config)
{
    SIM_ConfigText_Printf(text, "[Gameplay]\n");
    for (unsigned int i = 0; i < GameplayConfig_OptionCount(); i++) {
        unsigned int value = GameplayConfig_GetOptionValue(config, i);
        if (sConfigFields[i].type == CONFIG_BOOL) {
            SIM_ConfigText_Printf(text, "%s=%s\n", sConfigFields[i].name, value ? "true" : "false");
        } else {
            SIM_ConfigText_Printf(text, "%s=%u\n", sConfigFields[i].name, value);
        }
    }
}

static void GameplayConfig_LoadFile(void)
{
    sUsesFileOverrides = TRUE;
    // Create the defaults template on first start; existing files are never overwritten.
    if (!SIM_ConfigCheck_PrintOnly()) {
        GameplayConfig_GenerateFile();
    }
    GameplayConfig overrides = sGameplayConfig;
    SIM_ConfigText text = { 0 };
    GameplayConfig_WriteValues(&text, &overrides);
    SIM_ConfigCheck_Init(&sCheck, GAMEPLAY_CONFIG_FILE_PATH, SIM_ConfigText_Get(&text), NULL, 0);
    SIM_ConfigText_Free(&text);

    ParseContext context = { &overrides, &sCheck };
    int result = ini_parse(GAMEPLAY_CONFIG_FILE_PATH, GameplayConfig_IniHandler, &context);
    // A malformed/unreadable file falls back to defaults, never a partially parsed config.
    SIM_ConfigCheck_ParseResult(&sCheck, result, "the whole file is ignored and every gameplay setting is at its default");
    if (result == 0) {
        sGameplayConfig = overrides;
    } else {
        for (int i = 0; i < sCheck.count; i++) {
            sCheck.entries[i].line = 0;
            sCheck.entries[i].rejectedLine = 0;
        }
    }
}

void GameplayConfig_PrintEffective(FILE *out)
{
    SIM_ConfigText text = { 0 };
    GameplayConfig_WriteValues(&text, &sGameplayConfig);
    fprintf(out, "; %s%s\n", GAMEPLAY_CONFIG_FILE_PATH + 2,
        sUsesFileOverrides ? "" : " (not read: this build uses only its built-in defaults)");
    SIM_ConfigCheck_Print(&sCheck, SIM_ConfigText_Get(&text), out);
    SIM_ConfigText_Free(&text);
}

void GameplayConfig_Init(void)
{
    sUsesFileOverrides = FALSE;
    GameplayConfig_LoadDefaults(&sGameplayConfig);

    // Comment out this call to use only the defaults above: no file is generated or read,
    // and even an existing gameplay_config.ini cannot override a ROM hack's settings.
    GameplayConfig_LoadFile();
}

GameplayConfig *GameplayConfig_Get(void)
{
    return &sGameplayConfig;
}

// Shared by price display, affordability checks, and payment through the shop lookup.
u16 GameplayConfig_GetBPPrice(u16 item, u16 originalPrice)
{
    const GameplayConfig *config = GameplayConfig_Get();

    if (item == ITEM_RARE_CANDY) {
        return config->rareCandyBPPrice;
    }

    u32 price = ((u32)originalPrice * config->bpPricePercent + 99) / 100;
    return price > 65535 ? 65535 : price;
}
