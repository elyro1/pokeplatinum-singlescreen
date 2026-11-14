#ifndef POKEPLATINUM_POKETCH_ROUTE_ENCOUNTERS_GRAPHICS_H
#define POKEPLATINUM_POKETCH_ROUTE_ENCOUNTERS_GRAPHICS_H

#include "applications/poketch/poketch_animation.h"
#include "applications/poketch/poketch_task.h"
#include "applications/poketch/route_encounters/data.h"

#include "bg_window.h"

#define ROUTE_ENCOUNTERS_TASK_SLOTS 4

typedef struct RouteEncountersView {
    RouteEncounterList list;
    u32 methods;
    enum RouteEncounterMethod method;
    BOOL isCave;
} RouteEncountersView;

typedef struct RouteEncountersGraphics {
    const RouteEncountersView *view;
    BgConfig *bgConfig;
    u32 activeTasks[POKETCH_TASK_SLOT_BASE + ROUTE_ENCOUNTERS_TASK_SLOTS];
    PoketchAnimation_AnimationManager *animMan;
    PoketchAnimation_AnimatedSpriteData *sprites[ROUTE_ENCOUNTERS_MAX_MONS];
    PoketchAnimation_AnimatedSpriteData *balls[ROUTE_ENCOUNTERS_MAX_MONS];
    PoketchAnimation_SpriteData spriteData;
    u32 iconIndices[ROUTE_ENCOUNTERS_MAX_MONS];
} RouteEncountersGraphics;

enum RouteEncountersGraphicsTask {
    ROUTE_ENCOUNTERS_GRAPHICS_INIT = 0,
    // A refresh runs these three on consecutive frames; see SetSpriteAttributes.
    ROUTE_ENCOUNTERS_GRAPHICS_SET_SPRITES,
    ROUTE_ENCOUNTERS_GRAPHICS_SET_TILEMAP,
    ROUTE_ENCOUNTERS_GRAPHICS_LOAD_TILES,
    ROUTE_ENCOUNTERS_GRAPHICS_FREE,
};

BOOL PoketchRouteEncountersGraphics_New(RouteEncountersGraphics **dest, const RouteEncountersView *view, BgConfig *bgConfig);
void PoketchRouteEncountersGraphics_Free(RouteEncountersGraphics *graphics);
void PoketchRouteEncountersGraphics_StartTask(RouteEncountersGraphics *graphics, enum RouteEncountersGraphicsTask taskID);
BOOL PoketchRouteEncountersGraphics_TaskIsNotActive(RouteEncountersGraphics *graphics, enum RouteEncountersGraphicsTask taskID);
BOOL PoketchRouteEncountersGraphics_NoActiveTasks(RouteEncountersGraphics *graphics);

#endif // POKEPLATINUM_POKETCH_ROUTE_ENCOUNTERS_GRAPHICS_H
