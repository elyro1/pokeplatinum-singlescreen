#include "port/debug/crash_context.h"

#include <SDL2/SDL.h>
#include <nitro.h>
#include <simulator/config/sim_config.h>
#include <simulator/sim.h>
#include <simulator/sim_crash.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "field/field_system.h"
#include "port/config/config_report.h"
#include "port/debug/debug_field.h"
#include "port/gameplay/gameplay_config.h"

#include "location.h"
#include "player_avatar.h"
#include "system.h"

#define MAX_APPLICATIONS 4

enum DebugCrashKind {
    DEBUG_CRASH_NONE,
    DEBUG_CRASH_SEGV,
    DEBUG_CRASH_NULL_CALL,
    DEBUG_CRASH_ABORT,
    DEBUG_CRASH_STALL,
};

// Copied every frame on the game thread, so reports never follow game pointers that may be the thing that broke
static volatile u32 sFrame;
static OverlayFunc volatile sApplications[MAX_APPLICATIONS];
static volatile int sApplicationCount;
static volatile int sMapHeader = -1;
static volatile int sPlayerX, sPlayerZ;
static volatile BOOL sFieldRunning;

static enum DebugCrashKind sDebugCrash;
static u32 sDebugCrashFrame;

static void PrintConfig(FILE *out)
{
    SIM_Config_PrintEffective(SIM_GetConfigPtr(), out);
    ConfigReport_Print(out);
}

static void WriteContext(void)
{
    SIM_Crash_Printf("game: frame %u, vblank %u\n", (unsigned)sFrame, (unsigned)gSystem.vblankCounter);
    for (int i = 0; i < sApplicationCount; i++) {
        SIM_Crash_PrintAddress(i == 0 ? "application: " : "  child application: ", (const void *)sApplications[i]);
    }
    if (sMapHeader >= 0) {
        SIM_Crash_Printf("map header %d, player at %d,%d, field map %s\n", sMapHeader, sPlayerX, sPlayerZ, sFieldRunning ? "running" : "paused (menu, battle or transition)");
    } else {
        SIM_Crash_Printf("map: no field system\n");
    }
}

static void ParseDebugCrash(void)
{
    const char *value = getenv("PLATINUM_DEBUG_CRASH");
    if (value == NULL || *value == '\0') {
        return;
    }
    static const struct {
        const char *name;
        enum DebugCrashKind kind;
    } kinds[] = {
        { "segv", DEBUG_CRASH_SEGV },
        { "nullcall", DEBUG_CRASH_NULL_CALL },
        { "abort", DEBUG_CRASH_ABORT },
        { "stall", DEBUG_CRASH_STALL },
    };
    const char *colon = strchr(value, ':');
    size_t length = colon ? (size_t)(colon - value) : strlen(value);
    for (size_t i = 0; i < sizeof(kinds) / sizeof(kinds[0]); i++) {
        if (strlen(kinds[i].name) == length && strncmp(kinds[i].name, value, length) == 0) {
            sDebugCrash = kinds[i].kind;
        }
    }
    if (sDebugCrash == DEBUG_CRASH_NONE) {
        fprintf(stderr, "[config] PLATINUM_DEBUG_CRASH=%s: expected segv, nullcall, abort or stall, optionally :<frame>\n", value);
        return;
    }
    sDebugCrashFrame = colon ? (u32)strtoul(colon + 1, NULL, 10) : 60;
}

// Not inlined, so the test can find it in the stack.
__attribute__((noinline)) static void CrashContext_DebugCrash(enum DebugCrashKind kind)
{
    fprintf(stderr, "PLATINUM_DEBUG_CRASH: triggering at frame %u\n", (unsigned)sFrame);
    switch (kind) {
    case DEBUG_CRASH_SEGV:
        *(volatile int *)(uintptr_t)16 = 1;
        break;
    case DEBUG_CRASH_NULL_CALL: {
        void (*volatile function)(void) = NULL;
        function();
        break;
    }
    case DEBUG_CRASH_ABORT:
        abort();
        break;
    case DEBUG_CRASH_STALL: {
        int seconds = SIM_GetConfigPtr()->stallWatchdogSeconds;
        Uint32 end = SDL_GetTicks() + (Uint32)((seconds > 0 ? seconds : 5) + 2) * 1000;
        while ((Sint32)(end - SDL_GetTicks()) > 0) {
            SDL_Delay(10);
        }
        break;
    }
    default:
        break;
    }
}

void CrashContext_Init(void)
{
    ParseDebugCrash();
    SIM_Crash_SetContextWriter(WriteContext);
    SIM_Log_WatchConfigFile("sim_config_prj.ini");
    SIM_Log_WatchConfigFile(GAMEPLAY_CONFIG_FILE_PATH);
    SIM_Log_SetConfigPrinter(PrintConfig);
}

void CrashContext_Update(const ApplicationManager *application)
{
    sFrame++;

    int count = 0;
    for (; application != NULL && count < MAX_APPLICATIONS; application = application->child) {
        sApplications[count++] = application->template.main;
    }
    sApplicationCount = count;

    FieldSystem *fieldSystem = DEBUG_GetFieldSystem();
    if (fieldSystem != NULL && fieldSystem->location != NULL) {
        sMapHeader = fieldSystem->location->mapHeaderID;
        sFieldRunning = fieldSystem->runningFieldMap;
        if (fieldSystem->playerAvatar != NULL) {
            sPlayerX = PlayerAvatar_GetXPos(fieldSystem->playerAvatar);
            sPlayerZ = PlayerAvatar_GetZPos(fieldSystem->playerAvatar);
        } else {
            sPlayerX = fieldSystem->location->x;
            sPlayerZ = fieldSystem->location->z;
        }
    } else {
        sMapHeader = -1;
    }

    if (sDebugCrash != DEBUG_CRASH_NONE && sFrame == sDebugCrashFrame) {
        enum DebugCrashKind kind = sDebugCrash;
        sDebugCrash = DEBUG_CRASH_NONE;
        CrashContext_DebugCrash(kind);
    }
}
