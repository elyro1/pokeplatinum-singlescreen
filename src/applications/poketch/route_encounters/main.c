#include <nitro.h>
#include <string.h>

#include "applications/poketch/poketch_button.h"
#include "applications/poketch/poketch_graphics.h"
#include "applications/poketch/poketch_system.h"
#include "applications/poketch/route_encounters/data.h"
#include "applications/poketch/route_encounters/graphics.h"
#include "field/field_system.h"

#include "bg_window.h"
#include "heap.h"
#include "location.h"
#include "map_header.h"
#include "map_header_data.h"
#include "pokedex.h"
#include "rtc.h"
#include "savedata.h"
#include "sys_task.h"
#include "sys_task_manager.h"

#include "res/sound/pl_sound_data.naix"

// The list is rebuilt on a timer, not hooked, so it follows the player across
// zone borders, the time of day and new Pokedex entries while the app is open.
#define REFRESH_POLL_FRAMES 15

typedef struct PoketchRouteEncounters {
    u8 state;
    u8 subState;
    u8 shouldExit;
    u8 pollTimer;
    RouteEncountersView view;
    RouteEncountersView pendingView;
    RouteEncountersGraphics *graphics;
    PoketchSystem *poketchSys;
} PoketchRouteEncounters;

enum RouteEncountersState {
    STATE_LOAD_APP = 0,
    STATE_UPDATE_LOOP,
    STATE_REFRESH,
    STATE_SHUTDOWN,
};

typedef BOOL (*StateFunc)(PoketchRouteEncounters *);

static BOOL New(void **appData, PoketchSystem *poketchSys, BgConfig *bgConfig, u32 appID);
static void Exit(void *appData);
static void Task_Main(SysTask *task, void *appData);
static BOOL State_LoadApp(PoketchRouteEncounters *appData);
static BOOL State_UpdateApp(PoketchRouteEncounters *appData);
static BOOL State_Refresh(PoketchRouteEncounters *appData);
static BOOL State_UnloadApp(PoketchRouteEncounters *appData);

void PoketchAppStaticInit_RouteEncounters()
{
    PoketchSystem_SetAppFunctions(New, Exit);
}

static BOOL HasSeen(void *pokedex, u16 species)
{
    return Pokedex_HasSeenSpecies(pokedex, species);
}

static BOOL HasCaught(void *pokedex, u16 species)
{
    return Pokedex_HasCaughtSpecies(pokedex, species);
}

static void BuildView(PoketchRouteEncounters *appData, RouteEncountersView *view, enum RouteEncounterMethod preferredMethod)
{
    FieldSystem *fieldSystem = PoketchSystem_GetFieldSystem(appData->poketchSys);
    Pokedex *pokedex = SaveData_GetPokedex(PoketchSystem_GetSaveData(appData->poketchSys));
    const WildEncounters *encounters = MapHeaderData_GetWildEncounters(fieldSystem);
    enum MapHeaderID mapHeaderID = fieldSystem->location->mapHeaderID;

    memset(view, 0, sizeof(*view));
    view->isCave = MapHeader_IsCave(mapHeaderID);
    view->methods = MapHeader_HasWildEncounters(mapHeaderID) ? RouteEncounters_AvailableMethods(encounters) : 0;
    view->method = preferredMethod;

    if (view->methods == 0) {
        return;
    }

    // Keep the shown method across refreshes while the zone still has it.
    if (!(view->methods & (1 << preferredMethod))) {
        view->method = RouteEncounters_StepMethod(view->methods, ROUTE_ENCOUNTER_METHOD_COUNT - 1, 1);
    }

    RouteEncounters_BuildList(encounters, view->method, GetTimeOfDay(), HasSeen, HasCaught, pokedex, &view->list);
}

static BOOL Init(PoketchRouteEncounters *appData, PoketchSystem *poketchSys, BgConfig *bgConfig)
{
    appData->poketchSys = poketchSys;
    BuildView(appData, &appData->view, ROUTE_ENCOUNTER_METHOD_WALK);

    if (PoketchRouteEncountersGraphics_New(&appData->graphics, &appData->view, bgConfig)) {
        appData->state = STATE_LOAD_APP;
        appData->subState = 0;
        appData->shouldExit = FALSE;
        appData->pollTimer = REFRESH_POLL_FRAMES;
        return TRUE;
    }

    return FALSE;
}

static BOOL New(void **appData, PoketchSystem *poketchSys, BgConfig *bgConfig, u32 appID)
{
    PoketchRouteEncounters *routeEncounters = Heap_Alloc(HEAP_ID_POKETCH_APP, sizeof(PoketchRouteEncounters));

    if (routeEncounters != NULL) {
        memset(routeEncounters, 0, sizeof(*routeEncounters));

        if (Init(routeEncounters, poketchSys, bgConfig)) {
            if (SysTask_Start(Task_Main, routeEncounters, 1) != NULL) {
                *appData = routeEncounters;
                return TRUE;
            }

            PoketchRouteEncountersGraphics_Free(routeEncounters->graphics);
        }

        Heap_Free(routeEncounters);
    }

    return FALSE;
}

static void Free(PoketchRouteEncounters *appData)
{
    PoketchRouteEncountersGraphics_Free(appData->graphics);
    Heap_Free(appData);
}

static void Task_Main(SysTask *task, void *appData)
{
    static const StateFunc stateFuncs[] = {
        State_LoadApp,
        State_UpdateApp,
        State_Refresh,
        State_UnloadApp,
    };

    PoketchRouteEncounters *routeEncounters = appData;

    if (routeEncounters->state < NELEMS(stateFuncs)) {
        if (stateFuncs[routeEncounters->state](routeEncounters)) {
            PoketchSystem *poketchSys = routeEncounters->poketchSys;

            Free(routeEncounters);
            SysTask_Done(task);
            PoketchSystem_NotifyAppUnloaded(poketchSys);
        }
    }
}

static void Exit(void *appData)
{
    ((PoketchRouteEncounters *)appData)->shouldExit = TRUE;
}

static void ChangeState(PoketchRouteEncounters *appData, enum RouteEncountersState newState)
{
    appData->state = appData->shouldExit ? STATE_SHUTDOWN : newState;
    appData->subState = 0;
}

static BOOL State_LoadApp(PoketchRouteEncounters *appData)
{
    switch (appData->subState) {
    case 0:
        PoketchRouteEncountersGraphics_StartTask(appData->graphics, ROUTE_ENCOUNTERS_GRAPHICS_INIT);
        appData->subState++;
        break;
    case 1:
        if (PoketchRouteEncountersGraphics_TaskIsNotActive(appData->graphics, ROUTE_ENCOUNTERS_GRAPHICS_INIT)) {
            PoketchSystem_NotifyAppLoaded(appData->poketchSys);
            ChangeState(appData, STATE_UPDATE_LOOP);
        }
        break;
    }

    return FALSE;
}

static int GetMethodStep(PoketchRouteEncounters *appData)
{
    u32 x, y;

#ifdef SDK_PORT
    u32 keys = PoketchSystem_GetMenuPressedKeys(appData->poketchSys);

    if (appData->poketchSys->menuActive) {
        PoketchGraphics_SetMenuFocus(PoketchSystem_GetTaskData(), 0, 0, FALSE);
    }

    if (keys & (PAD_BUTTON_A | PAD_KEY_RIGHT)) {
        return 1;
    }

    if (keys & PAD_KEY_LEFT) {
        return -1;
    }
#endif

    if (PoketchSystem_GetDisplayTappedCoords(&x, &y)) {
        return 1;
    }

    return 0;
}

static void StartRefresh(PoketchRouteEncounters *appData, const RouteEncountersView *view)
{
    appData->pendingView = *view;
    ChangeState(appData, STATE_REFRESH);
}

static BOOL State_UpdateApp(PoketchRouteEncounters *appData)
{
    if (appData->shouldExit) {
        ChangeState(appData, STATE_SHUTDOWN);
        return FALSE;
    }

    int step = GetMethodStep(appData);
    u32 methods = appData->view.methods;

    if (step != 0 && (methods & (methods - 1)) != 0) {
        RouteEncountersView view;

        BuildView(appData, &view, RouteEncounters_StepMethod(methods, appData->view.method, step));
        PoketchSystem_PlaySoundEffect(SEQ_SE_DP_POKETCH_003_sseq);
        StartRefresh(appData, &view);
        return FALSE;
    }

    if (--appData->pollTimer == 0) {
        appData->pollTimer = REFRESH_POLL_FRAMES;

        // Rebuilding is cheap; comparing the result also catches a zone's
        // table loading after its ID changes, and new Pokedex entries.
        RouteEncountersView view;
        BuildView(appData, &view, appData->view.method);

        if (memcmp(&view, &appData->view, sizeof(view)) != 0) {
            StartRefresh(appData, &view);
        }
    }

    return FALSE;
}

static BOOL State_Refresh(PoketchRouteEncounters *appData)
{
    static const enum RouteEncountersGraphicsTask sSteps[] = {
        ROUTE_ENCOUNTERS_GRAPHICS_SET_SPRITES,
        ROUTE_ENCOUNTERS_GRAPHICS_SET_TILEMAP,
        ROUTE_ENCOUNTERS_GRAPHICS_LOAD_TILES,
    };

    if (appData->subState == 0) {
        // The graphics read appData->view, so it only changes between refreshes.
        appData->view = appData->pendingView;
    } else if (!PoketchRouteEncountersGraphics_TaskIsNotActive(appData->graphics, sSteps[appData->subState - 1])) {
        return FALSE;
    }

    if (appData->subState < NELEMS(sSteps)) {
        PoketchRouteEncountersGraphics_StartTask(appData->graphics, sSteps[appData->subState]);
        appData->subState++;
    } else {
        ChangeState(appData, STATE_UPDATE_LOOP);
    }

    return FALSE;
}

static BOOL State_UnloadApp(PoketchRouteEncounters *appData)
{
    switch (appData->subState) {
    case 0:
        PoketchRouteEncountersGraphics_StartTask(appData->graphics, ROUTE_ENCOUNTERS_GRAPHICS_FREE);
        appData->subState++;
        break;
    case 1:
        if (PoketchRouteEncountersGraphics_NoActiveTasks(appData->graphics)) {
            return TRUE;
        }
        break;
    }

    return FALSE;
}
