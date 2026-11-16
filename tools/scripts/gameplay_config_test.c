#include <assert.h>
#include <stdio.h>

// Include the item implementation to exercise its internal EV update calculation.
#include "../../src/item_use_pokemon.c"

static void WriteConfig(const char *contents)
{
    FILE *file = fopen(GAMEPLAY_CONFIG_FILE_PATH, "w");
    assert(file != NULL);
    assert(fputs(contents, file) >= 0);
    assert(fclose(file) == 0);
}

static void AssertConfigContents(const char *contents)
{
    FILE *file = fopen(GAMEPLAY_CONFIG_FILE_PATH, "r");
    assert(file != NULL);
    while (*contents != '\0') {
        assert(fgetc(file) == (unsigned char)*contents++);
    }
    assert(fgetc(file) == EOF);
    assert(!ferror(file));
    assert(fclose(file) == 0);
}

static BOOL SameValues(const GameplayConfig *a, const GameplayConfig *b)
{
    for (unsigned int i = 0; i < GameplayConfig_OptionCount(); i++) {
        if (GameplayConfig_GetOptionValue(a, i) != GameplayConfig_GetOptionValue(b, i)) {
            return FALSE;
        }
    }
    return TRUE;
}

// The built-in defaults are the Single-screen mod preset.
static void AssertDefaults(void)
{
    const GameplayConfig *config = GameplayConfig_Get();
    GameplayConfig preset;
    GameplayConfig_LoadPreset(&preset, GAMEPLAY_PRESET_SINGLE_SCREEN_MOD);
    assert(SameValues(config, &preset));
    assert(config->forgettableHMs && config->infiniteTMs && config->levelUpHeldItemEvolutions);
    assert(config->feebasWaterStoneEvolution && config->instantHoneyTrees && config->guaranteedFishing);
    assert(config->instantSpiritomb && config->unlimitedVitaminEVs);
    assert(!config->unlearnedHMs && !config->freeMoveTutors && !config->prohibitBattleItems);
    assert(config->tradeEvolutionLevel == 40 && config->eggHatchSpeedMultiplier == 5);
    assert(config->shinyRolls == 16 && config->rareCandyBPPrice == 1 && config->bpPricePercent == 25);
    assert(config->friendshipGainMultiplier == 1 && config->catchRateMultiplier == 1);
}

static void TestConfigFile(void)
{
    // First start creates a template containing the built-in defaults.
    GameplayConfig_Init();
    AssertDefaults();
    FILE *file = fopen(GAMEPLAY_CONFIG_FILE_PATH, "r");
    assert(file != NULL);
    char template[8192];
    size_t size = fread(template, 1, sizeof(template) - 1, file);
    template[size] = '\0';
    assert(fclose(file) == 0);
    assert(strstr(template, "[Gameplay]"));

    assert(!strstr(template, "fastWildBattleIntro="));
    assert(!strstr(template, "fastTrainerBattleIntro="));
    for (unsigned int i = 0; i < GameplayConfig_OptionCount(); i++) {
        GameplayConfigOption option;
        char line[96];
        assert(GameplayConfig_GetOption(i, &option));
        unsigned int value = GameplayConfig_GetOptionValue(GameplayConfig_Get(), i);
        if (option.isBoolean) {
            snprintf(line, sizeof(line), "\n%s=%s\n", option.name, value ? "true" : "false");
        } else {
            snprintf(line, sizeof(line), "\n%s=%u\n", option.name, value);
        }
        assert(strstr(template, line));
    }
    GameplayConfig_Init();
    AssertDefaults();

    // Every setting can be overridden, including numeric boundaries and all boolean forms.
    WriteConfig("[Gameplay]\n"
                "prohibitBattleItems=true\n"
                "forceSetBattleStyle=TRUE\n"
                "unlimitedVitaminEVs=1\n"
                "bpPricePercent=200\n"
                "rareCandyBPPrice=0\n"
                "forgettableHMs=1\n"
                "unlearnedHMs=true\n"
                "freeMoveRelearning=true\n"
                "freeMoveTutors=TRUE\n"
                "mainStoryMoveTutors=1\n"
                "unlimitedGreatMarshSteps=true\n"
                "zeroEggCycles=TRUE\n"
                "instantHoneyTrees=1\n"
                "guaranteedFishing=true\n"
                "infiniteTMs=true\n"
                "levelUpHeldItemEvolutions=TRUE\n"
                "instantSpiritomb=1\n"
                "feebasWaterStoneEvolution=true\n"
                "disableOverworldPoisonDamage=TRUE\n"
                "tradeEvolutionLevel=100\n"
                "shinyRolls=100\n"
                "friendshipGainMultiplier=10\n"
                "eggHatchSpeedMultiplier=0\n"
                "catchRateMultiplier=10\n");
    // Starting again must preserve the player's existing overrides.
    GameplayConfig_Init();
    GameplayConfig *config = GameplayConfig_Get();
    assert(config->prohibitBattleItems == TRUE);
    assert(config->forceSetBattleStyle == TRUE);
    assert(config->unlimitedVitaminEVs == TRUE);
    assert(config->bpPricePercent == 200);
    assert(config->rareCandyBPPrice == 0);
    assert(config->forgettableHMs == TRUE);
    assert(config->unlearnedHMs == TRUE);
    assert(config->freeMoveRelearning == TRUE);
    assert(config->freeMoveTutors == TRUE);
    assert(config->mainStoryMoveTutors == TRUE);
    assert(config->unlimitedGreatMarshSteps == TRUE);
    assert(config->zeroEggCycles == TRUE);
    assert(config->instantHoneyTrees == TRUE);
    assert(config->guaranteedFishing == TRUE);
    assert(config->infiniteTMs == TRUE);
    assert(config->levelUpHeldItemEvolutions == TRUE);
    assert(config->instantSpiritomb == TRUE);
    assert(config->feebasWaterStoneEvolution == TRUE);
    assert(config->disableOverworldPoisonDamage == TRUE);
    assert(config->tradeEvolutionLevel == 100);
    assert(config->shinyRolls == 100);
    assert(config->friendshipGainMultiplier == 10);
    assert(config->eggHatchSpeedMultiplier == 0);
    assert(config->catchRateMultiplier == 10);

    // Legacy trainer intro entries are ignored without affecting other defaults.
    WriteConfig("[Gameplay]\nfastTrainerBattleIntro=all\nfastTrainerBattleIntro=invalid\n");
    GameplayConfig_Init();
    AssertDefaults();

    // Partial files inherit defaults. Unknown names/sections and invalid values are ignored.
    WriteConfig("[Gameplay]\nprohibitBattleItems=1\nforceSetBattleStyle=TRUE\n"
                "freeMoveTutors=true\nfreeMoveRelearning=not-a-bool\n"
                "bpPricePercent=201\nrareCandyBPPrice=101\ntradeEvolutionLevel=101\n"
                "shinyRolls=999999999999999999999999999999999999\n"
                "friendshipGainMultiplier=11\neggHatchSpeedMultiplier=2.5\n"
                "catchRateMultiplier=2oops\nunknownOption=false\n"
                "[Other]\nprohibitBattleItems=false\n");
    GameplayConfig_Init();
    assert(config->prohibitBattleItems == TRUE);
    assert(config->forceSetBattleStyle == TRUE);
    assert(config->freeMoveTutors == TRUE);
    assert(config->freeMoveRelearning == FALSE);
    assert(config->bpPricePercent == 25);
    assert(config->rareCandyBPPrice == 1);
    assert(config->tradeEvolutionLevel == 40);
    assert(config->shinyRolls == 16);
    assert(config->friendshipGainMultiplier == 1);
    assert(config->eggHatchSpeedMultiplier == 5);
    assert(config->catchRateMultiplier == 1);

    WriteConfig("[Gameplay]\nprohibitBattleItems=true\n");
    GameplayConfig_Init();
    assert(config->prohibitBattleItems == TRUE);
    assert(config->forceSetBattleStyle == FALSE);
    assert(config->catchRateMultiplier == 1);

    // A syntax error discards the entire override file.
    WriteConfig("[Gameplay]\nprohibitBattleItems=true\ninvalid line\n");
    GameplayConfig_Init();
    AssertDefaults();
    AssertConfigContents("[Gameplay]\nprohibitBattleItems=true\ninvalid line\n");

    assert(remove(GAMEPLAY_CONFIG_FILE_PATH) == 0);
    GameplayConfig_Init();
    AssertDefaults();
    file = fopen(GAMEPLAY_CONFIG_FILE_PATH, "r");
    assert(file != NULL);
    assert(fclose(file) == 0);
}

static void TestPresets(void)
{
    GameplayConfig preset, example;
    GameplayConfig_LoadPreset(&preset, GAMEPLAY_PRESET_CLASSIC);
    assert(!preset.infiniteTMs && preset.bpPricePercent == 100 && preset.rareCandyBPPrice == 48);
    assert(preset.shinyRolls == 1 && preset.eggHatchSpeedMultiplier == 1 && preset.tradeEvolutionLevel == 0);

    // The repository example is the Single-screen mod preset as a file.
    FILE *file = fopen(GAMEPLAY_CONFIG_EXAMPLE_PATH, "rb");
    assert(file != NULL);
    static char text[16384];
    size_t length = fread(text, 1, sizeof(text) - 1, file);
    assert(length > 0 && length < sizeof(text) - 1 && fclose(file) == 0);
    text[length] = '\0';
    assert(GameplayConfig_ParseText(text, &example));
    GameplayConfig_LoadPreset(&preset, GAMEPLAY_PRESET_SINGLE_SCREEN_MOD);
    assert(SameValues(&example, &preset));
}

static void TestDefaultsOnly(void)
{
    // Commenting out the file call must disable both creation and existing overrides.
    GameplayConfig_Init();
    AssertDefaults();
    assert(fopen(GAMEPLAY_CONFIG_FILE_PATH, "r") == NULL);

    const char *overrides = "[Gameplay]\nprohibitBattleItems=true\nbpPricePercent=0\n";
    WriteConfig(overrides);
    GameplayConfig_Init();
    AssertDefaults();
    AssertConfigContents(overrides);
    assert(remove(GAMEPLAY_CONFIG_FILE_PATH) == 0);
}

int main(void)
{
#ifdef GAMEPLAY_CONFIG_DEFAULTS_ONLY
    TestPresets();
    TestDefaultsOnly();
#else
    TestConfigFile();
#endif

    GameplayConfig_Init();
    GameplayConfig *config = GameplayConfig_Get();
    AssertDefaults();
    GameplayConfig_LoadPreset(config, GAMEPLAY_PRESET_CLASSIC);
    assert(CalculateEVUpdate(95, 0, 10) == 100);
    assert(CalculateEVUpdate(100, 0, 10) == EV_UNCHANGED);
    assert(GameplayConfig_GetBPPrice(ITEM_RARE_CANDY, 48) == 48);
    assert(GameplayConfig_GetBPPrice(ITEM_CHOICE_BAND, 48) == 48);

    config->unlimitedVitaminEVs = TRUE;

    assert(CalculateEVUpdate(100, 0, 10) == 110);
    assert(CalculateEVUpdate(249, 0, 10) == 255);
    assert(CalculateEVUpdate(255, 0, 10) == EV_UNCHANGED);
    assert(CalculateEVUpdate(100, 405, 10) == 105);
    assert(CalculateEVUpdate(100, 410, 10) == EV_UNCHANGED);
    assert(CalculateEVUpdate(5, 0, -10) == 0);
    config->unlimitedVitaminEVs = FALSE;
    assert(CalculateEVUpdate(95, 0, 10) == 100);
    assert(CalculateEVUpdate(100, 0, 10) == EV_UNCHANGED);
    assert(CalculateEVUpdate(200, 0, 10) == EV_UNCHANGED);
    assert(CalculateEVUpdate(200, 0, -10) == 190);

    config->bpPricePercent = 100;
    config->rareCandyBPPrice = 1;
    assert(GameplayConfig_GetBPPrice(ITEM_RARE_CANDY, 48) == 1);
    assert(GameplayConfig_GetBPPrice(ITEM_PROTEIN, 1) == 1);
    assert(GameplayConfig_GetBPPrice(ITEM_CHOICE_BAND, 48) == 48);
    config->bpPricePercent = 50;
    assert(GameplayConfig_GetBPPrice(ITEM_CHOICE_BAND, 48) == 24);
    assert(GameplayConfig_GetBPPrice(ITEM_PROTEIN, 1) == 1);
    assert(GameplayConfig_GetBPPrice(ITEM_RARE_CANDY, 48) == 1);
    config->rareCandyBPPrice = 7;
    assert(GameplayConfig_GetBPPrice(ITEM_RARE_CANDY, 48) == 7);
    config->bpPricePercent = 0;
    assert(GameplayConfig_GetBPPrice(ITEM_CHOICE_BAND, 48) == 0);
    assert(GameplayConfig_GetBPPrice(ITEM_RARE_CANDY, 48) == 7);
    config->rareCandyBPPrice = 0;
    assert(GameplayConfig_GetBPPrice(ITEM_RARE_CANDY, 48) == 0);
    config->bpPricePercent = 65535;
    assert(GameplayConfig_GetBPPrice(ITEM_CHOICE_BAND, 65535) == 65535);
    puts("Gameplay config file, BP price, and vitamin EV checks passed.");
    return 0;
}
