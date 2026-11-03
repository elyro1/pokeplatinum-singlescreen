#include "port/gameplay_config.h"

#include <errno.h>
#include <simulator/config/ini.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "constants/items.h"

static GameplayConfig sGameplayConfig;
static BOOL sUsesFileOverrides;

void GameplayConfig_LoadDefaults(GameplayConfig *config)
{
    // Set a ROM hack's built-in gameplay defaults here.
    // Block player medicine and battle items, while allowing captures and held-item effects.
    config->prohibitBattleItems = FALSE;

    // Always use Set battle style, regardless of the saved Options selection.
    config->forceSetBattleStyle = FALSE;

    // Let all six vitamins raise a stat past 100 EVs, up to the normal 255/510 limits.
    config->unlimitedVitaminEVs = FALSE;

    // Original BP prices, including Rare Candy's 48 BP price from the shop table.
    config->bpPricePercent = 100;
    config->rareCandyBPPrice = 48;

    // Allow HM moves to be replaced when learning another move, including after leveling up in battle.
    // FALSE restores HM replacement protection; field-use requirements and HM item reuse stay unchanged.
    config->forgettableHMs = FALSE;

    // HM compatibility is enough for field use; normal badge and location checks still apply.
    config->unlearnedHMs = FALSE;

    // Let the Pastoria City Move Maniac relearn moves without requiring or consuming a Heart Scale.
    // FALSE restores the Heart Scale requirement and payment; other move tutors are unaffected.
    config->freeMoveRelearning = FALSE;

    // Let all three shard move tutors teach compatible moves without requiring or consuming any shards.
    // FALSE restores their original shard prices; displayed costs and tutor greetings follow this setting.
    config->freeMoveTutors = FALSE;

    // Make the Route 212 tutor offer all three shard tutors' move lists, including the post-game Survival Area moves.
    // FALSE restores each tutor's original list; compatibility checks and the separate freeMoveTutors setting still apply.
    config->mainStoryMoveTutors = FALSE;

    // Stop counting Great Marsh steps and disable the 500-step exit, while retaining the entry fee and Safari Ball limit.
    // FALSE resumes step counting from the saved counter and restores the 500-step exit.
    config->unlimitedGreatMarshSteps = FALSE;

    // Create eggs with zero remaining hatch cycles and set existing party eggs to zero at their next hatch check.
    // FALSE uses normal cycles for new eggs but does not restore cycles already removed from existing eggs.
    config->zeroEggCycles = FALSE;

    // Make unexpired, slathered Honey Trees encounter-ready immediately when checked again, retaining encounter tables and failure chances.
    // FALSE restores the six-hour wait; Honey costs and the 24-hour expiration remain unchanged in either mode.
    config->instantHoneyTrees = FALSE;

    // Keep a TM in the Bag after successfully teaching its move; selling or discarding it still removes it normally.
    // FALSE consumes TMs after teaching, while HMs remain reusable in either mode.
    config->infiniteTMs = FALSE;

    // Plain trade evolutions also trigger on level-up at or above this level; 0 restores trade-only evolution.
    // Held-item trades use levelUpHeldItemEvolutions instead. Values above 100 cannot trigger.
    config->tradeEvolutionLevel = 0;

    // Evolve on the next level-up holding the required item, including trade items, with no day/night restriction.
    // The item is consumed on successful evolution. FALSE restores trade and time-of-day requirements.
    config->levelUpHeldItemEvolutions = FALSE;

    // Encounter Spiritomb immediately after inserting an Odd Keystone, skipping Underground conversations.
    // Already-inserted Keystones also work; each successful encounter still uses one Keystone.
    config->instantSpiritomb = FALSE;

    // Replace Feebas's Beauty level-up requirement with using a Water Stone.
    // FALSE restores the original Beauty evolution and disables the Water Stone alternative.
    config->feebasWaterStoneEvolution = FALSE;

    // Skip field poison damage and its visual/survival effects; poison status and battle damage remain.
    config->disableOverworldPoisonDamage = FALSE;

    // Set the total shiny rolls for newly generated eligible Pokemon: 1 keeps original odds, and each extra roll has a 1/8192 chance.
    // 0 also keeps original odds; extra rolls preserve existing Pokemon, nature, gender, ability, Wurmple's evolution branch, and shiny locks.
    config->shinyRolls = 1;

    // Multiply positive friendship gains after existing bonuses; friendship losses are unchanged.
    // 0 or 1 keeps original gains. Friendship still caps at 255; initial friendship and egg cycles are unaffected.
    config->friendshipGainMultiplier = 1;

    // Divide the normal steps per hatch check by this value, rounding down to at least one step; 5 reduces 255 steps to 51.
    // 0 or 1 keeps normal timing, ability bonuses still apply, and zeroEggCycles eggs hatch at the next check instead of completing normal cycles.
    config->eggHatchSpeedMultiplier = 1;

    // Multiply the final catch rate after ball, HP, status, and Safari modifiers; 255 or higher guarantees capture.
    // 0 or 1 keeps original rates; Master Balls, guaranteed catches, and trainer capture restrictions still apply.
    config->catchRateMultiplier = 1;
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
    FIELD(prohibitBattleItems, CONFIG_BOOL, 1, "Block player Bag items in battle except Poke Balls; held and opponent items are unaffected.", "Block player battle items", "Battle rules"),
    FIELD(forceSetBattleStyle, CONFIG_BOOL, 1, "Always use Set battle style, regardless of the saved Options selection.", "Always use Set battle style", "Battle rules"),
    FIELD(unlimitedVitaminEVs, CONFIG_BOOL, 1, "Vitamins work past 100 EVs; normal per-stat and total EV limits still apply.", "Vitamins past 100 EVs", "Progression & prices"),
    FIELD(bpPricePercent, CONFIG_U16, 65535, "Global BP price percentage, rounded up: 100 = original, 0 = free.", "BP price percentage", "Progression & prices"),
    FIELD(rareCandyBPPrice, CONFIG_U16, 65535, "Exact Rare Candy BP price, overriding the percentage: 0 = free.", "Rare Candy BP price", "Progression & prices"),
    FIELD(forgettableHMs, CONFIG_BOOL, 1, "Allow HM moves to be replaced when learning another move.", "Allow replacing HM moves", "Moves & tutors"),
    FIELD(unlearnedHMs, CONFIG_BOOL, 1, "Use HMs with compatible party Pokemon without teaching them; badge and location restrictions still apply.", "Use HMs without teaching them", "Moves & tutors"),
    FIELD(freeMoveRelearning, CONFIG_BOOL, 1, "The Move Maniac requires no Heart Scale.", "Free move relearning", "Moves & tutors"),
    FIELD(freeMoveTutors, CONFIG_BOOL, 1, "Shard move tutors require no shards.", "Free shard move tutors", "Moves & tutors"),
    FIELD(mainStoryMoveTutors, CONFIG_BOOL, 1, "The Route 212 tutor offers all three shard tutors' move lists.", "All tutor moves on Route 212", "Moves & tutors"),
    FIELD(unlimitedGreatMarshSteps, CONFIG_BOOL, 1, "Disable the Great Marsh step limit; entry fee and Safari Ball limit still apply.", "Unlimited Great Marsh steps", "Encounters & events"),
    FIELD(zeroEggCycles, CONFIG_BOOL, 1, "New and existing eggs become ready to hatch at the next hatch check.", "Eggs ready at next hatch check", "Evolution & eggs"),
    FIELD(instantHoneyTrees, CONFIG_BOOL, 1, "Skip the Honey Tree wait; normal encounter chances and expiration still apply.", "Instant Honey Tree encounters", "Encounters & events"),
    FIELD(infiniteTMs, CONFIG_BOOL, 1, "Teaching a move does not consume its TM.", "Reusable TMs", "Moves & tutors"),
    FIELD(levelUpHeldItemEvolutions, CONFIG_BOOL, 1, "Held-item evolutions trigger on level-up at any time of day; the item is consumed.", "Held-item evolutions on level-up", "Evolution & eggs"),
    FIELD(instantSpiritomb, CONFIG_BOOL, 1, "Skip Underground conversations for Spiritomb; an Odd Keystone is still required.", "Skip Spiritomb conversations", "Encounters & events"),
    FIELD(feebasWaterStoneEvolution, CONFIG_BOOL, 1, "Evolve Feebas with a Water Stone instead of Beauty.", "Evolve Feebas with a Water Stone", "Evolution & eggs"),
    FIELD(disableOverworldPoisonDamage, CONFIG_BOOL, 1, "Skip walking poison damage; poison status and battle damage remain.", "Disable walking poison damage", "Progression & prices"),
    FIELD(tradeEvolutionLevel, CONFIG_U8, 100, "Plain trade evolutions also trigger on level-up at this level: 1-100, or 0 to disable.", "Trade evolution level", "Evolution & eggs"),
    FIELD(shinyRolls, CONFIG_U16, 65535, "Total shiny rolls for new eligible Pokemon: 0 and 1 preserve original odds.", "Shiny rolls", "Encounters & events"),
    FIELD(friendshipGainMultiplier, CONFIG_U8, 255, "Multiply positive friendship gains: 0 and 1 preserve original gains.", "Friendship gain multiplier", "Progression & prices"),
    FIELD(eggHatchSpeedMultiplier, CONFIG_U8, 255, "Divide steps per hatch check by this value: 0 and 1 preserve original timing.", "Egg hatch speed multiplier", "Evolution & eggs"),
    FIELD(catchRateMultiplier, CONFIG_U8, 255, "Multiply final catch rate: 0 and 1 preserve original rates; trainer captures remain blocked.", "Catch rate multiplier", "Encounters & events"),
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

static int GameplayConfig_IniHandler(void *user, const char *section, const char *name, const char *value)
{
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
                return 1; // Ignore invalid values, retaining the default.
            }
        } else {
            if (value[0] < '0' || value[0] > '9') {
                return 1;
            }
            char *end;
            errno = 0;
            number = strtoul(value, &end, 10);
            if (errno == ERANGE || *end != '\0' || number > field->maximum) {
                return 1;
            }
        }

        char *destination = (char *)user + field->offset;
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
    if (ini_parse_string(text, GameplayConfig_IniHandler, &parsed) != 0) return FALSE;
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
        fprintf(file, "\n; %s\n", field->description);
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

static void GameplayConfig_LoadFile(void)
{
    sUsesFileOverrides = TRUE;
    // Create the defaults template on first start; existing files are never overwritten.
    GameplayConfig_GenerateFile();
    GameplayConfig overrides = sGameplayConfig;
    // A malformed/unreadable file falls back to defaults, never a partially parsed config.
    if (ini_parse(GAMEPLAY_CONFIG_FILE_PATH, GameplayConfig_IniHandler, &overrides) == 0) {
        sGameplayConfig = overrides;
    }
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
