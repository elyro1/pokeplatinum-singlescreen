#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <simulator/config/config_check.h>
#include <simulator/config/sim_config.h>

#include "constants/overworld_encounters.h"
#include "port/gameplay_config.h"
#include "port/sim_config_prj.h"

static void Write(const char *path, const char *text)
{
    FILE *file = fopen(path, "w");
    assert(file != NULL);
    fputs(text, file);
    fclose(file);
}

static char *Read(const char *path)
{
    static char buffer[65536];
    FILE *file = fopen(path, "r");
    assert(file != NULL);
    size_t length = fread(buffer, 1, sizeof(buffer) - 1, file);
    fclose(file);
    buffer[length] = '\0';
    return buffer;
}

// The line of out that starts with key=
static const char *PrintedLine(const char *out, const char *key)
{
    static char line[256];
    size_t length = strlen(key);
    for (const char *p = out; p != NULL && *p; p = strchr(p, '\n') ? strchr(p, '\n') + 1 : NULL) {
        if (strncmp(p, key, length) == 0 && p[length] == '=') {
            const char *end = strchr(p, '\n');
            size_t size = end ? (size_t)(end - p) : strlen(p);
            assert(size < sizeof(line));
            memcpy(line, p, size);
            line[size] = '\0';
            return line;
        }
    }
    fprintf(stderr, "%s not printed\n", key);
    abort();
}

static void CheckProjectConfig(void)
{
    SIM_Config_prj_type config;

    // The trap from AGENTS.md: a key appended at the end lands in [Cheats]
    Write("sim_config_prj.ini",
        "[General]\n"
        "GridBag=false\n"
        "TextSpeed=Faster\n"
        "BreakDebuggerOnAssert=true\n"
        "[Cheats]\n"
        "WalkThroughWalls=true\n"
        "BW2BattleVisuals=true\n");
    int warnings = SIM_ConfigCheck_WarningCount();
    SIM_Config_prj_LoadDefaults(&config);
    assert(SIM_Config_prj_LoadConfigFile(&config));
    assert(SIM_ConfigCheck_WarningCount() == warnings + 1);
    assert(!config.bw2BattleVisuals && !config.gridBag && config.walkThroughWalls);
    assert(config.textSpeed == PORT_SPEED_FASTER);
    // Was written but never read back
    assert(config.breakDebuggerOnGfAssert);

    // Invalid values keep the previous value instead of turning into false
    Write("sim_config_prj.ini",
        "[General]\n"
        "PokemonFollower=yes\n"
        "NicknameMode=Sometimes\n"
        "pokemonfollower=false\n"
        "GridBag=true\n"
        "GridBag=false\n"
        "[Developer]\n"
        "OverworldEncounterCount=-3\n");
    warnings = SIM_ConfigCheck_WarningCount();
    SIM_Config_prj_LoadDefaults(&config);
    assert(SIM_Config_prj_LoadConfigFile(&config));
    // yes, Sometimes, the wrong case, the repeated GridBag and -3
    assert(SIM_ConfigCheck_WarningCount() == warnings + 5);
    assert(config.pokemonFollower && config.nicknameMode == NICKNAME_MODE_ASK && !config.gridBag);
    assert(config.overworldEncounterCount == OVERWORLD_ENCOUNTER_DEFAULT_COUNT);

    // The legacy key still migrates, silently
    Write("sim_config_prj.ini", "[General]\nBattleAnimationSpeedFix=false\n");
    warnings = SIM_ConfigCheck_WarningCount();
    SIM_Config_prj_LoadDefaults(&config);
    assert(SIM_Config_prj_LoadConfigFile(&config));
    assert(SIM_ConfigCheck_WarningCount() == warnings && config.battlePacing == BATTLE_PACING_QUICK);
    *SIM_Config_prj_GetConfig() = config;
    FILE *out = fopen("printed.txt", "w");
    SIM_Config_prj_PrintEffective(out);
    fclose(out);
    char *printed = Read("printed.txt");
    assert(strstr(PrintedLine(printed, "BattlePacing"), "; sim_config_prj.ini:2 (from the old BattleAnimationSpeedFix)"));
    assert(strstr(PrintedLine(printed, "GridBag"), "; default") && !strstr(PrintedLine(printed, "GridBag"), "no "));

    // Every saved key reads back
    SIM_Config_prj_LoadDefaults(&config);
    config.breakDebuggerOnGfAssert = TRUE;
    config.bw2BattleVisuals = TRUE;
    config.overworldEncounterCount = 3;
    config.runningShoes = RUNNING_SHOES_AUTO_RUN;
    SIM_Config_prj_SaveConfigFile(&config);
    char saved[65536];
    strcpy(saved, Read("sim_config_prj.ini"));
    warnings = SIM_ConfigCheck_WarningCount();
    SIM_Config_prj_type loaded;
    SIM_Config_prj_LoadDefaults(&loaded);
    assert(SIM_Config_prj_LoadConfigFile(&loaded) && SIM_ConfigCheck_WarningCount() == warnings);
    SIM_Config_prj_SaveConfigFile(&loaded);
    assert(strcmp(saved, Read("sim_config_prj.ini")) == 0);
    assert(loaded.breakDebuggerOnGfAssert && loaded.bw2BattleVisuals && loaded.overworldEncounterCount == 3);

    // Without a file, --print-config says so
    remove("sim_config_prj.ini");
    SIM_Config_prj_LoadDefaults(&config);
    assert(!SIM_Config_prj_LoadConfigFile(&config));
    *SIM_Config_prj_GetConfig() = config;
    out = fopen("printed.txt", "w");
    SIM_Config_prj_PrintEffective(out);
    fclose(out);
    assert(strstr(PrintedLine(Read("printed.txt"), "60Fps"), "; default (no sim_config_prj.ini)"));
}

static void CheckSimConfig(void)
{
    SIM_config_type config;

    SIM_Config_LoadDefaults(&config);
    strcpy(config.netSettings.serverIp, "10.0.0.7");
    config.osNvramConfig.ncd.alarm.minute = 12;
    config.osNvramConfig.ncd.alarm.second = 34;
    SIM_Config_SaveConfigFile(&config);

    int warnings = SIM_ConfigCheck_WarningCount();
    SIM_config_type loaded;
    SIM_Config_LoadDefaults(&loaded);
    assert(SIM_Config_LoadConfigFile(&loaded) && SIM_ConfigCheck_WarningCount() == warnings);
    // Written to [LAN] but used to be read from [Net]
    assert(strcmp(loaded.netSettings.serverIp, "10.0.0.7") == 0);
    // Alarm_Second used to overwrite the minute
    assert(loaded.osNvramConfig.ncd.alarm.minute == 12 && loaded.osNvramConfig.ncd.alarm.second == 34);

    Write("sim_config.ini",
        "[General]\n"
        "MasterVolume=loud\n"
        "ScreenLayout=sideways\n"
        "FullScreen=1\n"
        "WindowWidth=2160\n"
        "[Net]\n"
        "ServerIp=10.0.0.8\n"
        "[PAD]\n"
        "WindowHeight=1000\n");
    warnings = SIM_ConfigCheck_WarningCount();
    SIM_Config_LoadDefaults(&loaded);
    assert(SIM_Config_LoadConfigFile(&loaded));
    assert(SIM_ConfigCheck_WarningCount() == warnings + 4);
    assert(loaded.masterVolume == 50 && loaded.screenLayout == SIM_CONFIG_SCREEN_LAYOUT_TOP_ONLY && !loaded.fullScreen);
    assert(loaded.windowWidth == 2160 && loaded.windowHeight == 0);
    assert(strcmp(loaded.netSettings.serverIp, "10.0.0.8") == 0);

    FILE *out = fopen("printed.txt", "w");
    SIM_Config_PrintEffective(&loaded, out);
    fclose(out);
    char *printed = Read("printed.txt");
    assert(strstr(PrintedLine(printed, "WindowWidth"), "; sim_config.ini:5"));
    assert(strstr(PrintedLine(printed, "MasterVolume"), "; default (sim_config.ini:2 has an invalid value)"));
    assert(strstr(PrintedLine(printed, "ServerIp"), "; sim_config.ini:7 (from [Net])"));
}

static void CheckGameplayConfig(void)
{
    remove(GAMEPLAY_CONFIG_FILE_PATH);
    SIM_ConfigCheck_SetPrintOnly(TRUE);
    GameplayConfig_Init();
    // --print-config doesn't create the template
    assert(fopen(GAMEPLAY_CONFIG_FILE_PATH, "r") == NULL);
    SIM_ConfigCheck_SetPrintOnly(FALSE);

    Write(GAMEPLAY_CONFIG_FILE_PATH,
        "[Gameplay]\n"
        "shinyRolls=8\n"
        "catchRateMultiplier=999\n"
        "infiniteTMs=maybe\n"
        "guaranteedFishing=true\n"
        "[General]\n"
        "zeroEggCycles=true\n");
    int warnings = SIM_ConfigCheck_WarningCount();
    GameplayConfig_Init();
    GameplayConfig *config = GameplayConfig_Get();
    assert(SIM_ConfigCheck_WarningCount() == warnings + 3);
    assert(config->shinyRolls == 8 && config->catchRateMultiplier == 1 && !config->infiniteTMs);
    assert(config->guaranteedFishing && !config->zeroEggCycles);
    FILE *out = fopen("printed.txt", "w");
    GameplayConfig_PrintEffective(out);
    fclose(out);
    char *printed = Read("printed.txt");
    assert(strstr(PrintedLine(printed, "shinyRolls"), "; gameplay_config.ini:2"));
    assert(strstr(PrintedLine(printed, "catchRateMultiplier"), "invalid value"));

    // A syntax error discards the whole file, so nothing is credited to it
    Write(GAMEPLAY_CONFIG_FILE_PATH, "[Gameplay]\nshinyRolls=8\nnot ini\n");
    warnings = SIM_ConfigCheck_WarningCount();
    GameplayConfig_Init();
    assert(SIM_ConfigCheck_WarningCount() == warnings + 1 && GameplayConfig_Get()->shinyRolls == 1);
    out = fopen("printed.txt", "w");
    GameplayConfig_PrintEffective(out);
    fclose(out);
    assert(strstr(PrintedLine(Read("printed.txt"), "shinyRolls"), "; default"));

    // The settings editor's text parse stays quiet
    GameplayConfig parsed;
    warnings = SIM_ConfigCheck_WarningCount();
    assert(GameplayConfig_ParseText("[Gameplay]\nunknownKey=1\ninfiniteTMs=maybe\n", &parsed));
    assert(SIM_ConfigCheck_WarningCount() == warnings);
}

int main(void)
{
    CheckProjectConfig();
    CheckSimConfig();
    CheckGameplayConfig();
    puts("config check tests passed");
    return 0;
}
