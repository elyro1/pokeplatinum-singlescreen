#include <nitro.h>
#include <errno.h>
#include <simulator/config/config_check.h>
#include <simulator/config/ini.h>
#include <stdlib.h>

#include "port/config/sim_config_prj.h"
#include "constants/overworld_encounters.h"

#define SIM_CONFIG_PRJ_FILE_PATH "./sim_config_prj.ini"

typedef struct {
    SIM_Config_prj_type *config;
    BOOL hasPreset;
    int legacyAnimationLine;
    BOOL legacyAnimation;
    // The key being handled, for warnings about its value
    const char *section, *name;
    int line;
} ConfigLoadContext;

static SIM_ConfigCheck sCheck;

// Keys the writer no longer emits but that are still read
static const SIM_ConfigKeyName sLegacyKeys[] = {
    { "General", "BattleAnimationSpeedFix" },
};

static const char *sBattlePacingNames[BATTLE_PACING_COUNT] = { "Smooth", "Quick", "Turbo" };
static const char *sSpeedNames[PORT_SPEED_COUNT] = { "Fast", "Faster", "Instant", "Normal" };

static const char *sNicknameModeNames[NICKNAME_MODE_COUNT] = { "Ask", "Always", "Never" };
static const char *sPokedexUnitsNames[POKEDEX_UNITS_COUNT] = { "Metric", "Imperial" };
static const char *sRunningShoesNames[RUNNING_SHOES_COUNT] = { "HoldToRun", "AutoRun" };

static int IniHandler(void *user, const char *section, const char *name, const char *value, int line);
static const char * BoolToString(BOOL arg);
static BOOL ParseBool(ConfigLoadContext *context, const char *value, BOOL *out);
static BOOL ParseEnum(ConfigLoadContext *context, const char *value, const char *const *names, int count, int *out);
static void WriteConfig(SIM_ConfigText *text, const SIM_Config_prj_type *aConfig);

// Invalid values keep the setting's previous value, with a warning
#define SET_BOOL(field) do { BOOL parsed_; if (ParseBool(context, value, &parsed_)) (field) = parsed_; } while (0)
#define SET_ENUM(field, type, names, count) \
    do { int parsed_; if (ParseEnum(context, value, names, count, &parsed_)) (field) = (type)parsed_; } while (0)

static SIM_Config_prj_type s_SIM_config_prj;

static int IniHandler(void* user, const char* section, const char* name,
                   const char* value, int line)
{
    #define MATCH(s, n) strcmp(section, s) == 0 && strcmp(name, n) == 0

    ConfigLoadContext *context = user;
    SIM_Config_prj_type *config = context->config;

    if (!SIM_ConfigCheck_Key(&sCheck, section, name, line)) {
        return 1;
    }
    context->section = section;
    context->name = name;
    context->line = line;

    // General section
    if(MATCH("General", "60Fps")) {
        SET_BOOL(config->enable60fps);
    }
    if (MATCH("General", "BWMonSprites")) {
        SET_BOOL(config->bwMonSprites);
    }
    if (MATCH("General", "WidescreenPokedex")) {
        SET_BOOL(config->widescreenPokedex);
    }
    if (MATCH("General", "WidescreenSummary")) {
        SET_BOOL(config->widescreenSummary);
    }
    if (MATCH("General", "WidescreenEvolution")) {
        SET_BOOL(config->widescreenEvolution);
    }
    if (MATCH("General", "PokedexUnits")) {
        SET_ENUM(config->pokedexUnits, PokedexUnits, sPokedexUnitsNames, POKEDEX_UNITS_COUNT);
    }
    // Old Original pacing migrates to Smooth. Removed UI timing switches are ignored.
    if (MATCH("General", "BattlePacing")) {
        if (strcmp(value, "Original") == 0) {
            context->hasPreset = TRUE;
            config->battlePacing = BATTLE_PACING_SMOOTH;
        } else {
            int pacing;
            if (ParseEnum(context, value, sBattlePacingNames, BATTLE_PACING_COUNT, &pacing)) {
                context->hasPreset = TRUE;
                config->battlePacing = (BattlePacing)pacing;
            }
        }
    }
    if (MATCH("General", "BattleAnimationSpeedFix")) {
        if (ParseBool(context, value, &context->legacyAnimation)) {
            context->legacyAnimationLine = line;
        }
    }
    if (MATCH("General", "TextSpeed")) {
        SET_ENUM(config->textSpeed, PortSpeed, sSpeedNames, PORT_SPEED_COUNT);
    }
    if (MATCH("General", "BattleBarSpeed")) {
        int speed;
        // Instant bars were removed; keep old configurations on the fastest available setting.
        if (ParseEnum(context, value, sSpeedNames, PORT_SPEED_COUNT, &speed)) {
            config->battleBarSpeed = speed == PORT_SPEED_INSTANT ? PORT_SPEED_FASTER : (PortSpeed)speed;
        }
    }
    if (MATCH("General", "NicknameMode")) {
        SET_ENUM(config->nicknameMode, NicknameMode, sNicknameModeNames, NICKNAME_MODE_COUNT);
    }
    if (MATCH("General", "RunningShoes")) {
        SET_ENUM(config->runningShoes, RunningShoesMode, sRunningShoesNames, RUNNING_SHOES_COUNT);
    }
    if (MATCH("General", "FastWildBattleIntro")) {
        SET_BOOL(config->fastWildBattleIntro);
    }
    if (MATCH("General", "GridBag")) SET_BOOL(config->gridBag);
    if (MATCH("General", "DisableLowHPBeep")) SET_BOOL(config->disableLowHPBeep);
    if (MATCH("General", "BW2BattleVisuals")) {
        SET_BOOL(config->bw2BattleVisuals);
    }
    if (MATCH("General", "OverworldEncounters")) SET_BOOL(config->overworldEncounters);
    if (MATCH("General", "PokemonFollower")) SET_BOOL(config->pokemonFollower);
    if(MATCH("General", "EnableAsserts")) {
        SET_BOOL(config->enableAsserts);
    }
    if (MATCH("General", "BreakDebuggerOnAssert")) {
        SET_BOOL(config->breakDebuggerOnGfAssert);
    }
    if(MATCH("Graphics", "EnhancedDayNight")) SET_BOOL(config->enhancedDayNight);
    if(MATCH("Graphics", "RegionalAtmosphere")) SET_BOOL(config->regionalAtmosphere);
    if(MATCH("Graphics", "Antialias3D")) SET_BOOL(config->antialias3D);
    if(MATCH("Graphics", "CorrectLighting")) SET_BOOL(config->correctLighting);
    if(MATCH("Graphics", "EnvironmentalParticles")) SET_BOOL(config->environmentalParticles);
    if(MATCH("Graphics", "SoftContactShadows")) SET_BOOL(config->softContactShadows);
    if(MATCH("Graphics", "CastShadows")) SET_BOOL(config->castShadows);
    if(MATCH("Graphics", "WaterEffects")) SET_BOOL(config->waterEffects);
    if(MATCH("Graphics", "WeatherEffects")) SET_BOOL(config->weatherEffects);
    if (MATCH("Graphics", "NightLighting"))
        SET_BOOL(config->nightLighting);
    if (MATCH("Graphics", "NightGlows"))
        SET_BOOL(config->nightGlows);
    if (MATCH("Graphics", "EmissiveBloom"))
        SET_BOOL(config->emissiveBloom);

    if (MATCH("Developer", "Autosave")) {
        SET_BOOL(config->autosave);
    }
    if (MATCH("Developer", "OverworldEncounterCount")) {
        char *end;
        errno = 0;
        long count = strtol(value, &end, 10);
        if (end != value && *end == '\0' && errno != ERANGE
            && count >= 0 && count <= OVERWORLD_ENCOUNTER_CAPACITY) {
            config->overworldEncounterCount = (int)count;
        } else {
            char expected[48];
            snprintf(expected, sizeof(expected), "a number from 0 to %d", OVERWORLD_ENCOUNTER_CAPACITY);
            SIM_ConfigCheck_BadValue(&sCheck, section, name, value, line, expected);
        }
    }

    // Cheats section
    if(MATCH("Cheats", "WalkThroughWalls")) {
        SET_BOOL(config->walkThroughWalls);
    }

    if(MATCH("Cheats", "DisableRandomEncounters")) {
        SET_BOOL(config->disableRandomEncounters);
    }

    if(MATCH("Cheats", "RunFromTrainerBattles")) {
        SET_BOOL(config->runFromTrainerBattles);
    }
    return 1;
}

static const char * BoolToString(BOOL arg)
{
    const char * ret = "false";
    if(arg) {
        ret = "true";
    }

    return ret;
}

static BOOL ParseBool(ConfigLoadContext *context, const char *value, BOOL *out)
{
    if (strcmp(value, "true") == 0 || strcmp(value, "false") == 0) {
        *out = value[0] == 't';
        return TRUE;
    }
    SIM_ConfigCheck_BadValue(&sCheck, context->section, context->name, value, context->line, "true or false");
    return FALSE;
}

static BOOL ParseEnum(ConfigLoadContext *context, const char *value, const char *const *names, int count, int *out)
{
    char expected[128] = "";

    for (int i = 0; i < count; i++) {
        if (strcmp(value, names[i]) == 0) {
            *out = i;
            return TRUE;
        }
    }
    for (int i = 0; i < count; i++) {
        size_t used = strlen(expected);
        snprintf(expected + used, sizeof(expected) - used, "%s%s", i == 0 ? "" : i == count - 1 ? " or " : ", ", names[i]);
    }
    SIM_ConfigCheck_BadValue(&sCheck, context->section, context->name, value, context->line, expected);
    return FALSE;
}

// Initialize a SIM_Config_prj_type to default values
void SIM_Config_prj_LoadDefaults(SIM_Config_prj_type * aConfig)
{
  memset(aConfig, 0, sizeof(*aConfig));
  aConfig->enable60fps = TRUE;
  aConfig->battlePacing = BATTLE_PACING_SMOOTH;
  aConfig->nicknameMode = NICKNAME_MODE_ASK;
  aConfig->widescreenPokedex = TRUE;
  aConfig->widescreenSummary = TRUE;
  aConfig->widescreenEvolution = TRUE;
  aConfig->gridBag = TRUE;
  aConfig->pokedexUnits = POKEDEX_UNITS_METRIC;
  aConfig->runningShoes = RUNNING_SHOES_HOLD;
  aConfig->textSpeed = PORT_SPEED_FAST;
  aConfig->battleBarSpeed = PORT_SPEED_FAST;
  aConfig->fastWildBattleIntro = TRUE;
  aConfig->disableLowHPBeep = FALSE;
  aConfig->bw2BattleVisuals = FALSE;
  aConfig->bwMonSprites = TRUE;
  aConfig->overworldEncounters = TRUE;
  aConfig->overworldEncounterCount = OVERWORLD_ENCOUNTER_DEFAULT_COUNT;
  aConfig->autosave = TRUE;
  aConfig->pokemonFollower = TRUE;
  aConfig->enableAsserts = FALSE;
  aConfig->breakDebuggerOnGfAssert = FALSE;
  aConfig->enhancedDayNight = TRUE;
  aConfig->regionalAtmosphere = TRUE;
  aConfig->antialias3D = FALSE;
  aConfig->correctLighting = TRUE;
  aConfig->environmentalParticles = TRUE;
  aConfig->softContactShadows = TRUE;
  aConfig->castShadows = TRUE;
  aConfig->waterEffects = TRUE;
  aConfig->weatherEffects = TRUE;
  aConfig->nightLighting = TRUE;
  aConfig->nightGlows = TRUE;
  aConfig->emissiveBloom = TRUE;

  // Cheats section
  aConfig->walkThroughWalls = FALSE;
}

// Loads the configuration file. If it does not exist, return false
BOOL SIM_Config_prj_LoadConfigFile(SIM_Config_prj_type * aConfig)
{
    ConfigLoadContext context = {
        .config = aConfig,
        .legacyAnimation = TRUE,
    };
    SIM_ConfigText text = { 0 };
    WriteConfig(&text, aConfig);
    SIM_ConfigCheck_Init(&sCheck, SIM_CONFIG_PRJ_FILE_PATH, SIM_ConfigText_Get(&text), sLegacyKeys, sizeof(sLegacyKeys) / sizeof(sLegacyKeys[0]));
    SIM_ConfigText_Free(&text);

    int result = ini_parse(SIM_CONFIG_PRJ_FILE_PATH, IniHandler, &context);
    SIM_ConfigCheck_ParseResult(&sCheck, result, "it is ignored (later such lines are not reported)");
    if (result < 0) {
        return FALSE;
    }
    if (!context.hasPreset && context.legacyAnimationLine > 0) {
        aConfig->battlePacing = !context.legacyAnimation ? BATTLE_PACING_QUICK
            : BATTLE_PACING_SMOOTH;
        SIM_ConfigCheck_SetSource(&sCheck, "General", "BattlePacing", context.legacyAnimationLine,
            "from the old BattleAnimationSpeedFix");
    }
    return TRUE;
}

void SIM_Config_prj_PrintEffective(FILE *out)
{
    SIM_ConfigText text = { 0 };
    WriteConfig(&text, &s_SIM_config_prj);
    fprintf(out, "; %s\n", SIM_CONFIG_PRJ_FILE_PATH + 2);
    SIM_ConfigCheck_Print(&sCheck, SIM_ConfigText_Get(&text), out);
    SIM_ConfigText_Free(&text);
}

// Writes every key, also to tell SIM_ConfigCheck which keys the file has
static void WriteConfig(SIM_ConfigText *configFile, const SIM_Config_prj_type *aConfig)
{
    SIM_ConfigText_Printf(configFile, "[General]\n");
    SIM_ConfigText_Printf(configFile, "60Fps=%s\n", BoolToString(aConfig->enable60fps));
    SIM_ConfigText_Printf(configFile, "BattlePacing=%s\n", sBattlePacingNames[aConfig->battlePacing]);
    SIM_ConfigText_Printf(configFile, "TextSpeed=%s\n", sSpeedNames[aConfig->textSpeed]);
    SIM_ConfigText_Printf(configFile, "BattleBarSpeed=%s\n", sSpeedNames[aConfig->battleBarSpeed]);
    SIM_ConfigText_Printf(configFile, "NicknameMode=%s\n", sNicknameModeNames[aConfig->nicknameMode]);
    SIM_ConfigText_Printf(configFile, "WidescreenPokedex=%s\n", BoolToString(aConfig->widescreenPokedex));
    SIM_ConfigText_Printf(configFile, "WidescreenSummary=%s\n", BoolToString(aConfig->widescreenSummary));
    SIM_ConfigText_Printf(configFile, "WidescreenEvolution=%s\n", BoolToString(aConfig->widescreenEvolution));
    SIM_ConfigText_Printf(configFile, "GridBag=%s\n", BoolToString(aConfig->gridBag));
    SIM_ConfigText_Printf(configFile, "PokedexUnits=%s\n", sPokedexUnitsNames[aConfig->pokedexUnits]);
    SIM_ConfigText_Printf(configFile, "RunningShoes=%s\n", sRunningShoesNames[aConfig->runningShoes]);
    SIM_ConfigText_Printf(configFile, "FastWildBattleIntro=%s\n", BoolToString(aConfig->fastWildBattleIntro));
    SIM_ConfigText_Printf(configFile, "DisableLowHPBeep=%s\n", BoolToString(aConfig->disableLowHPBeep));
    SIM_ConfigText_Printf(configFile, "BW2BattleVisuals=%s\n", BoolToString(aConfig->bw2BattleVisuals));
    SIM_ConfigText_Printf(configFile, "BWMonSprites=%s\n", BoolToString(aConfig->bwMonSprites));
    SIM_ConfigText_Printf(configFile, "OverworldEncounters=%s\n", BoolToString(aConfig->overworldEncounters));
    SIM_ConfigText_Printf(configFile, "PokemonFollower=%s\n", BoolToString(aConfig->pokemonFollower));
    SIM_ConfigText_Printf(configFile, "EnableAsserts=%s\n", BoolToString(aConfig->enableAsserts));
    SIM_ConfigText_Printf(configFile, "BreakDebuggerOnAssert=%s\n", BoolToString(aConfig->breakDebuggerOnGfAssert));
    SIM_ConfigText_Printf(configFile, "\n");

    SIM_ConfigText_Printf(configFile, "[Graphics]\n");
    SIM_ConfigText_Printf(configFile, "EnhancedDayNight=%s\n", BoolToString(aConfig->enhancedDayNight));
    SIM_ConfigText_Printf(configFile, "RegionalAtmosphere=%s\n", BoolToString(aConfig->regionalAtmosphere));
    SIM_ConfigText_Printf(configFile, "Antialias3D=%s\n", BoolToString(aConfig->antialias3D));
    SIM_ConfigText_Printf(configFile, "CorrectLighting=%s\n", BoolToString(aConfig->correctLighting));
    SIM_ConfigText_Printf(configFile, "EnvironmentalParticles=%s\n", BoolToString(aConfig->environmentalParticles));
    SIM_ConfigText_Printf(configFile, "SoftContactShadows=%s\n", BoolToString(aConfig->softContactShadows));
    SIM_ConfigText_Printf(configFile, "CastShadows=%s\n", BoolToString(aConfig->castShadows));
    SIM_ConfigText_Printf(configFile, "WaterEffects=%s\n", BoolToString(aConfig->waterEffects));
    SIM_ConfigText_Printf(configFile, "WeatherEffects=%s\n", BoolToString(aConfig->weatherEffects));
    SIM_ConfigText_Printf(configFile, "NightLighting=%s\n", BoolToString(aConfig->nightLighting));
    SIM_ConfigText_Printf(configFile, "NightGlows=%s\n", BoolToString(aConfig->nightGlows));
    SIM_ConfigText_Printf(configFile, "EmissiveBloom=%s\n\n", BoolToString(aConfig->emissiveBloom));

    SIM_ConfigText_Printf(configFile, "[Developer]\n");
    SIM_ConfigText_Printf(configFile, "Autosave=%s\n", BoolToString(aConfig->autosave));
    SIM_ConfigText_Printf(configFile, "OverworldEncounterCount=%d\n\n", aConfig->overworldEncounterCount);

    SIM_ConfigText_Printf(configFile, "[Cheats]\n");
    SIM_ConfigText_Printf(configFile, "WalkThroughWalls=%s\n", BoolToString(aConfig->walkThroughWalls));
    SIM_ConfigText_Printf(configFile, "DisableRandomEncounters=%s\n", BoolToString(aConfig->disableRandomEncounters));
    SIM_ConfigText_Printf(configFile, "RunFromTrainerBattles=%s\n", BoolToString(aConfig->runFromTrainerBattles));
}

// Save a SIM_Config_prj_type to the configuration file
void SIM_Config_prj_SaveConfigFile(SIM_Config_prj_type * aConfig)
{
    SIM_ConfigText text = { 0 };
    WriteConfig(&text, aConfig);
    SIM_ConfigText_Save(&text, SIM_CONFIG_PRJ_FILE_PATH);
    SIM_ConfigText_Free(&text);
}

void SIM_Config_prj_init() {
  //Load default config
  SIM_Config_prj_LoadDefaults(&s_SIM_config_prj);
  
  //Load configuration file
  if (!SIM_Config_prj_LoadConfigFile(&s_SIM_config_prj) && !SIM_ConfigCheck_PrintOnly()) {
    SIM_Config_prj_SaveConfigFile(&s_SIM_config_prj);
  }
}

SIM_Config_prj_type * SIM_Config_prj_GetConfig()
{
  return &s_SIM_config_prj;
}
