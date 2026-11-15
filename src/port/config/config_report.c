#include "port/config/config_report.h"

#include <stdlib.h>
#include <string.h>

#include <nitro/types.h>

#include "port/gameplay/gameplay_config.h"
#include "port/config/sim_config_prj.h"

#ifndef _WIN32
extern char **environ;
#endif

// Every PLATINUM_* variable the game or libntr reads; docs/environment_variables.md describes them.
// test_config_check.py keeps this list, the getenv calls and the docs in step
static const char *const sEnvironmentVariables[] = {
    "PLATINUM_AUTOPILOT",
    "PLATINUM_BWBATTLE_BG_CYCLE",
    "PLATINUM_BWBATTLE_MOVE_CYCLE",
    "PLATINUM_BWBATTLE_WEATHER",
    "PLATINUM_CACHE_MB",
    "PLATINUM_DEBUG_FOLLOWER_SAVE",
    "PLATINUM_DEBUG_FOLLOWER_TRACE",
    "PLATINUM_DEBUG_LEVEL_UP",
    "PLATINUM_DEBUG_MAP",
    "PLATINUM_DEBUG_MOVE",
    "PLATINUM_DEBUG_OVERWORLD_POKEMON",
    "PLATINUM_DEBUG_SURF",
    "PLATINUM_DEBUG_TRAINER",
    "PLATINUM_DEBUG_WILD",
    "PLATINUM_DEBUG_WILD_KIND",
    "PLATINUM_FRAME_TRACE",
    "PLATINUM_GPU_CACHE_MB",
    "PLATINUM_GPU_TRACE",
    "PLATINUM_LEGACY_LOADING",
    "PLATINUM_PREPARE_US",
    "PLATINUM_STORY_TEST_FD",
    "PLATINUM_UPLOAD_KB",
};

#define PREFIX "PLATINUM_"
#define MAX_NAME 64
#define ENV_COUNT (sizeof(sEnvironmentVariables) / sizeof(sEnvironmentVariables[0]))

static int EditDistance(const char *a, size_t lengthA, const char *b)
{
    size_t lengthB = strlen(b);
    int row[MAX_NAME + 1];

    if (lengthA > MAX_NAME || lengthB > MAX_NAME) {
        return MAX_NAME;
    }
    for (size_t j = 0; j <= lengthB; j++) {
        row[j] = (int)j;
    }
    for (size_t i = 1; i <= lengthA; i++) {
        int diagonal = row[0];
        row[0] = (int)i;
        for (size_t j = 1; j <= lengthB; j++) {
            int above = row[j];
            int best = diagonal + (a[i - 1] != b[j - 1]);
            if (above + 1 < best) {
                best = above + 1;
            }
            if (row[j - 1] + 1 < best) {
                best = row[j - 1] + 1;
            }
            row[j] = best;
            diagonal = above;
        }
    }
    return row[lengthB];
}

static BOOL IsKnown(const char *name, size_t length)
{
    for (size_t i = 0; i < ENV_COUNT; i++) {
        if (strlen(sEnvironmentVariables[i]) == length && strncmp(sEnvironmentVariables[i], name, length) == 0) {
            return TRUE;
        }
    }
    return FALSE;
}

void ConfigReport_CheckEnvironment(void)
{
    for (char **entry = environ; entry != NULL && *entry != NULL; entry++) {
        const char *equals = strchr(*entry, '=');
        size_t length = equals != NULL ? (size_t)(equals - *entry) : strlen(*entry);

        if (strncmp(*entry, PREFIX, strlen(PREFIX)) != 0 || IsKnown(*entry, length)) {
            continue;
        }
        const char *closest = NULL;
        int closestDistance = 4;
        for (size_t i = 0; i < ENV_COUNT; i++) {
            int distance = EditDistance(*entry, length, sEnvironmentVariables[i]);
            if (distance < closestDistance) {
                closest = sEnvironmentVariables[i];
                closestDistance = distance;
            }
        }
        if (closest != NULL) {
            fprintf(stderr, "[config] environment: %.*s is not read by the game; did you mean %s?\n", (int)length,
                *entry, closest);
        } else {
            fprintf(stderr, "[config] environment: %.*s is not read by the game (see docs/environment_variables.md)\n",
                (int)length, *entry);
        }
    }
}

void ConfigReport_Print(FILE *out)
{
    int set = 0;

    fprintf(out, "\n");
    SIM_Config_prj_PrintEffective(out);
    fprintf(out, "\n");
    GameplayConfig_PrintEffective(out);
    fprintf(out, "\n; Environment variables (docs/environment_variables.md)\n");
    for (size_t i = 0; i < ENV_COUNT; i++) {
        const char *value = getenv(sEnvironmentVariables[i]);
        if (value != NULL) {
            fprintf(out, "%s=%s\n", sEnvironmentVariables[i], value);
            set++;
        }
    }
    if (set == 0) {
        fprintf(out, "; none of the game's PLATINUM_* variables are set\n");
    }
    fprintf(out, "\n; imgui.ini only holds the settings menu's window positions\n");
}
