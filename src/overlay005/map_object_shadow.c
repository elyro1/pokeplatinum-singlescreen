#include "overlay005/map_object_shadow.h"

#include <nitro.h>

#include "constants/field/field_effect_renderer.h"

#include "struct_decls/map_object.h"

#include "field/field_system.h"
#include "overlay005/field_effect_manager.h"

#include "map_object.h"
#include "map_object_move.h"
#include "map_tile_behavior.h"
#include "overworld_anim_manager.h"
#include "rtc.h"
#include "simple3d.h"
#include "sys_task.h"
#include "sys_task_manager.h"
#ifdef SDK_PORT
#include "port/field_visual_effects.h"
#endif

enum ShadowAppearanceState {
    SHADOW_APPEARANCE_INIT = 0,
    SHADOW_APPEARANCE_IDLE,
    SHADOW_APPEARANCE_TRANSITION,
};

enum ShadowVisibility {
    SHADOW_VISIBLE = 0,
    SHADOW_HIDDEN,
    SHADOW_CONTACT_ONLY,
};

typedef struct MapObjectShadowRenderer {
    enum ShadowAppearanceState state;
    enum TimeOfDay timeOfDay;
    fx32 alpha;
    VecFx32 scale;
    FieldEffectManager *effectManager;
    SysTask *timeOfDayTask;
    Simple3DModel models[3];
    Simple3DRenderObj renderObjs[3];
} MapObjectShadowRenderer;

typedef struct MapObjectShadowParams {
    FieldEffectManager *effectManager;
    MapObjectShadowRenderer *renderer;
    MapObject *mapObj;
} MapObjectShadowParams;

typedef struct MapObjectShadow {
    int graphicsID;
    int localID;
    int mapHeaderID;
    enum ShadowVisibility visibility;
    MapObjectShadowParams params;
#ifdef SDK_PORT
    u32 actorHandle;
#endif
} MapObjectShadow;

static void StartTimeOfDayTask(MapObjectShadowRenderer *renderer);
static void StopTimeOfDayTask(MapObjectShadowRenderer *renderer);
static void InitTimeOfDayAppearance(MapObjectShadowRenderer *renderer, enum TimeOfDay timeOfDay);
static void StepTowardTarget(fx32 *value, fx32 target, fx32 step);
static void UpdateTimeOfDay(SysTask *task, void *data);
static void LoadShadowModels(MapObjectShadowRenderer *renderer);
static void FreeShadowModels(MapObjectShadowRenderer *renderer);
static void SetShadowAlpha(MapObjectShadowRenderer *renderer, int alpha);

static const OverworldAnimManagerFuncs sShadowFuncs;
static const OverworldAnimManagerFuncs sFixedShadowFuncs;
static const VecFx32 sShadowScalesByTimeOfDay[] = {
    { FX32_ONE, FX32_ONE, FX32_ONE },
    { FX32_ONE + (FX32_ONE / 4), FX32_ONE, FX32_ONE + (FX32_ONE / 4) },
    { FX32_ONE + (FX32_ONE / 4), FX32_ONE, FX32_ONE },
    { FX32_ONE + (FX32_ONE / 8), FX32_ONE, FX32_ONE },
    { FX32_ONE - (FX32_ONE / 8), FX32_ONE, FX32_ONE - (FX32_ONE / 8) }
};

static const fx32 sShadowAlphaByTimeOfDay[] = {
    FX32_ONE * 14,
    FX32_ONE * 18,
    FX32_ONE * 18,
    FX32_ONE * 8,
    FX32_ONE * 4,
};

static const int sShadowModelIDs[3] = {
    0x11,
    0x12,
    0x13
};

void *MapObjectShadowRenderer_New(FieldEffectManager *effectManager)
{
    MapObjectShadowRenderer *renderer = FieldEffectManager_HeapAllocInit(effectManager, sizeof(MapObjectShadowRenderer), 0, 0);
    renderer->effectManager = effectManager;

    LoadShadowModels(renderer);
    StartTimeOfDayTask(renderer);

    return renderer;
}

void MapObjectShadowRenderer_Free(void *data)
{
    MapObjectShadowRenderer *renderer = data;

    StopTimeOfDayTask(renderer);
    FreeShadowModels(renderer);
    FieldEffectManager_HeapFree(renderer);
}

static void StartTimeOfDayTask(MapObjectShadowRenderer *renderer)
{
    FieldSystem *fieldSystem = FieldEffectManager_GetFieldSystem(renderer->effectManager);
    int priority = MapObjectMan_GetTaskBasePriority(fieldSystem->mapObjMan) - 1;
    SysTask *task = SysTask_Start(UpdateTimeOfDay, renderer, priority);

    renderer->timeOfDayTask = task;
}

static void StopTimeOfDayTask(MapObjectShadowRenderer *renderer)
{
    SysTask_Done(renderer->timeOfDayTask);
}

static void InitTimeOfDayAppearance(MapObjectShadowRenderer *renderer, enum TimeOfDay timeOfDay)
{
    renderer->alpha = sShadowAlphaByTimeOfDay[timeOfDay];
    renderer->scale = sShadowScalesByTimeOfDay[timeOfDay];
}

static void StepTowardTarget(fx32 *value, fx32 target, fx32 step)
{
    if (*value < target) {
        *value += step;

        if (*value > target) {
            *value = target;
        }
    } else if (*value > target) {
        *value -= step;

        if (*value < target) {
            *value = target;
        }
    }
}

static void UpdateTimeOfDay(SysTask *task, void *data)
{
    enum TimeOfDay timeOfDay = GetTimeOfDay();
    MapObjectShadowRenderer *renderer = data;

    switch (renderer->state) {
    case SHADOW_APPEARANCE_INIT:
        renderer->timeOfDay = timeOfDay;
        InitTimeOfDayAppearance(renderer, renderer->timeOfDay);
        SetShadowAlpha(renderer, renderer->alpha / FX32_ONE);
        renderer->state = SHADOW_APPEARANCE_IDLE;
        break;
    case SHADOW_APPEARANCE_IDLE:
        if (renderer->timeOfDay == timeOfDay) {
            break;
        }

        renderer->state = SHADOW_APPEARANCE_TRANSITION;
        // Fall through to begin the transition immediately.
    case SHADOW_APPEARANCE_TRANSITION: {
        fx32 targetAlpha = sShadowAlphaByTimeOfDay[timeOfDay];
        VecFx32 targetScale = sShadowScalesByTimeOfDay[timeOfDay];

        StepTowardTarget(&renderer->scale.x, targetScale.x, FX32_ONE / 256);
        StepTowardTarget(&renderer->scale.y, targetScale.y, FX32_ONE / 256);
        StepTowardTarget(&renderer->scale.z, targetScale.z, FX32_ONE / 256);
        StepTowardTarget(&renderer->alpha, targetAlpha, FX32_ONE / 8);
        SetShadowAlpha(renderer, renderer->alpha / FX32_ONE);

        if ((targetScale.x == renderer->scale.x) && (targetScale.y == renderer->scale.y) && (targetScale.z == renderer->scale.z) && (targetAlpha == renderer->alpha)) {
            renderer->timeOfDay = timeOfDay;
            renderer->state = SHADOW_APPEARANCE_IDLE;
        }
    } break;
    }
}

static void LoadShadowModels(MapObjectShadowRenderer *renderer)
{
    int i;

    for (i = 0; i < 3; i++) {
        FieldEffectManager_LoadModel(renderer->effectManager, &renderer->models[i], 0, sShadowModelIDs[i], 0);
        Simple3D_CreateRenderObject(&renderer->renderObjs[i], &renderer->models[i]);
    }
}

static void FreeShadowModels(MapObjectShadowRenderer *renderer)
{
    int i;

    for (i = 0; i < 3; i++) {
        Simple3D_FreeModel(&renderer->models[i]);
    }
}

static void SetShadowAlpha(MapObjectShadowRenderer *renderer, int alpha)
{
    NNS_G3dMdlUseMdlAlpha(renderer->models[0].g3DModel);
    NNS_G3dMdlSetMdlAlphaAll(renderer->models[0].g3DModel, alpha);
}

void MapObjectShadow_Start(MapObject *mapObj)
{
    int priority;
    MapObjectShadowParams params;
    FieldEffectManager *effectManager;
    VecFx32 position;

    effectManager = MapObject_GetFieldEffectManager(mapObj);

    params.effectManager = effectManager;
    params.renderer = FieldEffectManager_GetRendererContext(effectManager, FIELD_EFFECT_RENDERER_SHADOW);
    params.mapObj = mapObj;

    MapObject_GetPosPtr(mapObj, &position);

    priority = MapObject_CalculateTaskPriority(mapObj, 2);

    FieldEffectManager_InitAnimManager(effectManager, &sShadowFuncs, &position, 0, &params, priority);
}

static BOOL InitShadow(OverworldAnimManager *animManager, void *data)
{
    MapObjectShadow *shadow = data;
    const MapObjectShadowParams *params = OverworldAnimManager_GetUserData(animManager);

    shadow->params = *params;
    shadow->graphicsID = MapObject_GetGraphicsID(shadow->params.mapObj);
    shadow->localID = MapObject_GetLocalID(shadow->params.mapObj);

    if (sub_02062E94(shadow->params.mapObj) == TRUE) {
        shadow->mapHeaderID = sub_02062C18(shadow->params.mapObj);
    } else {
        shadow->mapHeaderID = MapObject_GetMapHeaderID(shadow->params.mapObj);
    }

#ifdef SDK_PORT
    shadow->actorHandle = MapObject_GetPokemonActorHandle(shadow->params.mapObj);
#endif

    return TRUE;
}

// A Pokémon actor respawned within one frame reuses the object slot, graphics
// and local ID, so only its handle tells this shadow its owner has been replaced.
static BOOL ShadowOwnerExists(const MapObjectShadow *shadow)
{
    const MapObject *mapObj = shadow->params.mapObj;

    if (sub_020627B4(mapObj, shadow->graphicsID, shadow->localID, shadow->mapHeaderID) == FALSE) {
        return FALSE;
    }

#ifdef SDK_PORT
    if (MapObject_GetPokemonActorHandle(mapObj) != shadow->actorHandle) {
        return FALSE;
    }
#endif

    return TRUE;
}

#ifdef SDK_PORT
// An object over terrain with no height data is frozen and its sprite hidden
// (see ov5_021EC6C0); its shadow must not stay behind on its own.
static BOOL ShadowOwnerSpriteHidden(const MapObject *mapObj)
{
    return MapObject_CheckStatusFlag(mapObj, MAP_OBJ_STATUS_12)
        && !MapObject_CheckStatusFlag(mapObj, MAP_OBJ_STATUS_13);
}
#endif

static void UpdateShadow(OverworldAnimManager *animManager, void *data)
{
    MapObjectShadow *shadow = data;
    MapObject *mapObj = shadow->params.mapObj;

    if (!ShadowOwnerExists(shadow)) {
        FieldEffectManager_FinishAnimManager(animManager);
        return;
    }

    if (MapObject_CheckManagerStatus(mapObj, MAP_OBJ_STATUS_END_MOVEMENT)) {
        FieldEffectManager_FinishAnimManager(animManager);
        return;
    }

    shadow->visibility = SHADOW_VISIBLE;

#ifdef SDK_PORT
    if (ShadowOwnerSpriteHidden(mapObj)) {
        shadow->visibility = SHADOW_HIDDEN;
        return;
    }
#endif

    if (MapObject_CheckStatusFlag(mapObj, MAP_OBJ_STATUS_HIDE | MAP_OBJ_STATUS_HIDE_SHADOW) == TRUE) {
        shadow->visibility = SHADOW_HIDDEN;
#ifdef SDK_PORT
        // Keep enhanced shadows in tall grass, where the original blob hides.
        // Stepping out of it, the game keeps the shadow hidden until the
        // step ends; keep them then too, unless the new tile hides shadows.
        u32 currTileBehavior = MapObject_GetCurrTileBehavior(mapObj);
        u32 prevTileBehavior = MapObject_GetPrevTileBehavior(mapObj);
        if (!MapObject_CheckStatusFlag(mapObj, MAP_OBJ_STATUS_HIDE)
            && (TileBehavior_IsTallGrass(currTileBehavior)
                || (TileBehavior_IsTallGrass(prevTileBehavior) && !MapObject_TileHidesShadow(mapObj, currTileBehavior)))) {
            shadow->visibility = SHADOW_CONTACT_ONLY;
        }
#endif
        if (shadow->visibility == SHADOW_HIDDEN) {
            return;
        }
    }

    VecFx32 position;

    MapObject_GetPosPtr(mapObj, &position);
    OverworldAnimManager_SetPosition(animManager, &position);
}

static void DrawShadow(OverworldAnimManager *animManager, void *data)
{
    MapObjectShadow *shadow = data;

    if (shadow->visibility == SHADOW_HIDDEN) {
        return;
    }

    VecFx32 position;
    VecFx32 scale = shadow->params.renderer->scale;
    MtxFx33 rotation = { FX32_ONE, 0, 0, 0, FX32_ONE, 0, 0, 0, FX32_ONE };
    Simple3DRenderObj *renderObj = &shadow->params.renderer->renderObjs[0];

    OverworldAnimManager_GetPosition(animManager, &position);

    position.x -= FX32_ONE / 2;
    position.y -= 4 * FX32_ONE;
    position.z += FX32_ONE;

#ifdef SDK_PORT
    FieldSystem *fieldSystem = FieldEffectManager_GetFieldSystem(shadow->params.effectManager);
    if (FieldVisualEffects_DrawContactShadow(fieldSystem, shadow->params.mapObj, &position, &scale, &renderObj->g3DRenderObj.resMdl->info)
        || shadow->visibility == SHADOW_CONTACT_ONLY) {
        return;
    }
#endif
    Simple3D_DrawRenderObj(renderObj, &position, &scale, &rotation);
}

static const OverworldAnimManagerFuncs sShadowFuncs = {
    sizeof(MapObjectShadow),
    InitShadow,
    OverworldAnimManager_DummyExitFunc,
    UpdateShadow,
    DrawShadow
};

OverworldAnimManager *MapObjectShadow_StartFixed(MapObject *mapObj, int modelIndex)
{
    int priority;
    MapObjectShadowParams params;
    FieldEffectManager *effectManager;
    VecFx32 position;

    effectManager = MapObject_GetFieldEffectManager(mapObj);

    params.effectManager = effectManager;
    params.renderer = FieldEffectManager_GetRendererContext(effectManager, FIELD_EFFECT_RENDERER_SHADOW);
    params.mapObj = mapObj;

    MapObject_GetPosPtr(mapObj, &position);

    priority = MapObject_CalculateTaskPriority(mapObj, 2);
    return FieldEffectManager_InitAnimManager(effectManager, &sFixedShadowFuncs, &position, modelIndex, &params, priority);
}

static void UpdateFixedShadow(OverworldAnimManager *animManager, void *data)
{
    MapObjectShadow *shadow = data;
    MapObject *mapObj = shadow->params.mapObj;

    if (!ShadowOwnerExists(shadow)) {
        FieldEffectManager_FinishAnimManager(animManager);
        return;
    }

    shadow->visibility = SHADOW_VISIBLE;

    if (MapObject_CheckStatusFlag(mapObj, MAP_OBJ_STATUS_HIDE | MAP_OBJ_STATUS_HIDE_SHADOW) == TRUE) {
        shadow->visibility = SHADOW_HIDDEN;
        return;
    }

    VecFx32 position;

    MapObject_GetPosPtr(mapObj, &position);
    OverworldAnimManager_SetPosition(animManager, &position);
}

static void DrawFixedShadow(OverworldAnimManager *animManager, void *data)
{
    MapObjectShadow *shadow = data;

    if (shadow->visibility == SHADOW_HIDDEN) {
        return;
    }

    int modelIndex = OverworldAnimManager_GetUserInt(animManager);
    VecFx32 position;
    Simple3DRenderObj *renderObj = &shadow->params.renderer->renderObjs[modelIndex];

    OverworldAnimManager_GetPosition(animManager, &position);
    position.x -= FX32_ONE / 2;
    position.y -= 4 * FX32_ONE;
    position.z += FX32_ONE;
    Simple3D_DrawRenderObjWithPos(renderObj, &position);
}

static const OverworldAnimManagerFuncs sFixedShadowFuncs = {
    sizeof(MapObjectShadow),
    InitShadow,
    OverworldAnimManager_DummyExitFunc,
    UpdateFixedShadow,
    DrawFixedShadow
};
