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

static void AssertOriginalDefaults(void)
{
    const GameplayConfig *config = GameplayConfig_Get();
    assert(config->prohibitBattleItems == FALSE);
    assert(config->forceSetBattleStyle == FALSE);
    assert(config->unlimitedVitaminEVs == FALSE);
    assert(config->bpPricePercent == 100);
    assert(config->rareCandyBPPrice == 48);
    assert(config->forgettableHMs == FALSE);
    assert(config->unlearnedHMs == FALSE);
    assert(config->freeMoveRelearning == FALSE);
    assert(config->freeMoveTutors == FALSE);
    assert(config->mainStoryMoveTutors == FALSE);
    assert(config->unlimitedGreatMarshSteps == FALSE);
    assert(config->zeroEggCycles == FALSE);
    assert(config->instantHoneyTrees == FALSE);
    assert(config->infiniteTMs == FALSE);
    assert(config->levelUpHeldItemEvolutions == FALSE);
    assert(config->instantSpiritomb == FALSE);
    assert(config->feebasWaterStoneEvolution == FALSE);
    assert(config->disableOverworldPoisonDamage == FALSE);
    assert(config->tradeEvolutionLevel == 0);
    assert(config->shinyRolls == 1);
    assert(config->friendshipGainMultiplier == 1);
    assert(config->eggHatchSpeedMultiplier == 1);
    assert(config->overworldEncounterCount == 8);
    assert(config->catchRateMultiplier == 1);
}

static void TestConfigFile(void)
{
    // First start creates a template containing the built-in defaults.
    GameplayConfig_Init();
    AssertOriginalDefaults();
    FILE *file = fopen(GAMEPLAY_CONFIG_FILE_PATH, "r");
    assert(file != NULL);
    char template[8192];
    size_t size = fread(template, 1, sizeof(template) - 1, file);
    template[size] = '\0';
    assert(fclose(file) == 0);
    assert(strstr(template, "[Gameplay]"));

    assert(strstr(template, "prohibitBattleItems=false\n"));
    assert(strstr(template, "forceSetBattleStyle=false\n"));
    assert(strstr(template, "unlimitedVitaminEVs=false\n"));
    assert(strstr(template, "bpPricePercent=100\n"));
    assert(strstr(template, "rareCandyBPPrice=48\n"));
    assert(strstr(template, "forgettableHMs=false\n"));
    assert(strstr(template, "unlearnedHMs=false\n"));
    assert(strstr(template, "freeMoveRelearning=false\n"));
    assert(strstr(template, "freeMoveTutors=false\n"));
    assert(strstr(template, "mainStoryMoveTutors=false\n"));
    assert(strstr(template, "unlimitedGreatMarshSteps=false\n"));
    assert(strstr(template, "zeroEggCycles=false\n"));
    assert(strstr(template, "instantHoneyTrees=false\n"));
    assert(strstr(template, "infiniteTMs=false\n"));
    assert(strstr(template, "levelUpHeldItemEvolutions=false\n"));
    assert(strstr(template, "instantSpiritomb=false\n"));
    assert(strstr(template, "feebasWaterStoneEvolution=false\n"));
    assert(strstr(template, "disableOverworldPoisonDamage=false\n"));
    assert(strstr(template, "tradeEvolutionLevel=0\n"));
    assert(strstr(template, "shinyRolls=1\n"));
    assert(strstr(template, "friendshipGainMultiplier=1\n"));
    assert(strstr(template, "eggHatchSpeedMultiplier=1\n"));
    assert(strstr(template, "catchRateMultiplier=1\n"));
    assert(strstr(template, "overworldEncounterCount=8\n"));
    GameplayConfig_Init();
    AssertOriginalDefaults();

    // Every setting can be overridden, including numeric boundaries and all boolean forms.
    WriteConfig("[Gameplay]\n"
                "prohibitBattleItems=true\n"
                "forceSetBattleStyle=TRUE\n"
                "unlimitedVitaminEVs=1\n"
                "bpPricePercent=65535\n"
                "rareCandyBPPrice=0\n"
                "forgettableHMs=1\n"
                "unlearnedHMs=true\n"
                "freeMoveRelearning=true\n"
                "freeMoveTutors=TRUE\n"
                "mainStoryMoveTutors=1\n"
                "unlimitedGreatMarshSteps=true\n"
                "zeroEggCycles=TRUE\n"
                "instantHoneyTrees=1\n"
                "infiniteTMs=true\n"
                "levelUpHeldItemEvolutions=TRUE\n"
                "instantSpiritomb=1\n"
                "feebasWaterStoneEvolution=true\n"
                "disableOverworldPoisonDamage=TRUE\n"
                "tradeEvolutionLevel=100\n"
                "shinyRolls=65535\n"
                "friendshipGainMultiplier=255\n"
                "eggHatchSpeedMultiplier=0\n"
                "catchRateMultiplier=255\n"
                "overworldEncounterCount=0\n");
    // Starting again must preserve the player's existing overrides.
    GameplayConfig_Init();
    GameplayConfig *config = GameplayConfig_Get();
    assert(config->prohibitBattleItems == TRUE);
    assert(config->forceSetBattleStyle == TRUE);
    assert(config->unlimitedVitaminEVs == TRUE);
    assert(config->bpPricePercent == 65535);
    assert(config->rareCandyBPPrice == 0);
    assert(config->forgettableHMs == TRUE);
    assert(config->unlearnedHMs == TRUE);
    assert(config->freeMoveRelearning == TRUE);
    assert(config->freeMoveTutors == TRUE);
    assert(config->mainStoryMoveTutors == TRUE);
    assert(config->unlimitedGreatMarshSteps == TRUE);
    assert(config->zeroEggCycles == TRUE);
    assert(config->instantHoneyTrees == TRUE);
    assert(config->infiniteTMs == TRUE);
    assert(config->levelUpHeldItemEvolutions == TRUE);
    assert(config->instantSpiritomb == TRUE);
    assert(config->feebasWaterStoneEvolution == TRUE);
    assert(config->disableOverworldPoisonDamage == TRUE);
    assert(config->tradeEvolutionLevel == 100);
    assert(config->shinyRolls == 65535);
    assert(config->friendshipGainMultiplier == 255);
    assert(config->eggHatchSpeedMultiplier == 0);
    assert(config->catchRateMultiplier == 255);
    assert(config->overworldEncounterCount == 0);

    // Partial files inherit defaults. Unknown names/sections and invalid values are ignored.
    WriteConfig("[Gameplay]\nprohibitBattleItems=1\nforceSetBattleStyle=TRUE\n"
                "freeMoveTutors=true\nfreeMoveRelearning=not-a-bool\n"
                "bpPricePercent=-1\nrareCandyBPPrice=65536\ntradeEvolutionLevel=101\n"
                "shinyRolls=999999999999999999999999999999999999\n"
                "friendshipGainMultiplier=256\neggHatchSpeedMultiplier=2.5\n"
                "catchRateMultiplier=2oops\noverworldEncounterCount=31\nunknownOption=false\n"
                "[Other]\nprohibitBattleItems=false\n");
    GameplayConfig_Init();
    assert(config->prohibitBattleItems == TRUE);
    assert(config->forceSetBattleStyle == TRUE);
    assert(config->freeMoveTutors == TRUE);
    assert(config->overworldEncounterCount == 8);
    assert(config->freeMoveRelearning == FALSE);
    assert(config->bpPricePercent == 100);
    assert(config->rareCandyBPPrice == 48);
    assert(config->tradeEvolutionLevel == 0);
    assert(config->shinyRolls == 1);
    assert(config->friendshipGainMultiplier == 1);
    assert(config->eggHatchSpeedMultiplier == 1);
    assert(config->catchRateMultiplier == 1);

    WriteConfig("[Gameplay]\nprohibitBattleItems=true\n");
    GameplayConfig_Init();
    assert(config->prohibitBattleItems == TRUE);
    assert(config->forceSetBattleStyle == FALSE);
    assert(config->catchRateMultiplier == 1);

    // A syntax error discards the entire override file.
    WriteConfig("[Gameplay]\nprohibitBattleItems=true\ninvalid line\n");
    GameplayConfig_Init();
    AssertOriginalDefaults();
    AssertConfigContents("[Gameplay]\nprohibitBattleItems=true\ninvalid line\n");

    assert(remove(GAMEPLAY_CONFIG_FILE_PATH) == 0);
    GameplayConfig_Init();
    AssertOriginalDefaults();
    file = fopen(GAMEPLAY_CONFIG_FILE_PATH, "r");
    assert(file != NULL);
    assert(fclose(file) == 0);
}

static void TestDefaultsOnly(void)
{
    // Commenting out the file call must disable both creation and existing overrides.
    GameplayConfig_Init();
    AssertOriginalDefaults();
    assert(fopen(GAMEPLAY_CONFIG_FILE_PATH, "r") == NULL);

    const char *overrides = "[Gameplay]\nprohibitBattleItems=true\nbpPricePercent=0\n";
    WriteConfig(overrides);
    GameplayConfig_Init();
    AssertOriginalDefaults();
    AssertConfigContents(overrides);
    assert(remove(GAMEPLAY_CONFIG_FILE_PATH) == 0);
}

int main(void)
{
#ifdef GAMEPLAY_CONFIG_DEFAULTS_ONLY
    TestDefaultsOnly();
#else
    TestConfigFile();
#endif

    GameplayConfig_Init();
    GameplayConfig *config = GameplayConfig_Get();
    AssertOriginalDefaults();
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
