#include "overlay005/surf_mount_renderer.h"

#include <nitro.h>
#include <string.h>

#include "constants/field/field_effect_renderer.h"
#include "constants/map_object.h"
#include "constants/moves.h"

#include "struct_decls/map_object.h"

#include "field/field_system.h"
#include "overlay005/field_effect_manager.h"
#include "overlay005/object_event_gfx_data.h"
#include "overlay005/ov5_021EB1A0.h"
#include "overlay005/ov5_021ECC20.h"
#include "overlay005/ov5_021F17B8.h"

#include "camera.h"
#include "field_system.h"
#include "fx_util.h"
#include "heap.h"
#include "map_object.h"
#include "map_object_move.h"
#include "overworld_anim_manager.h"
#include "overworld_pokemon.h"
#include "party.h"
#include "player_avatar.h"
#include "pokemon.h"
#include "simple3d.h"
#include "sys_task_manager.h"
#include "unk_02054884.h"
#ifdef SDK_PORT
#include "port/assets/follower_assets.h"
#include "port/visuals/frame_rate.h"
#endif

#define RENDERER_MIN_Y_OFFSET     FX32_ONE
#define RENDERER_MAX_Y_OFFSET     (FX32_ONE * 4)
#define RENDERER_INITIAL_Y_OFFSET FX32_ONE
#define RENDERER_INITIAL_Y_DELTA  0x400

#define MAP_OBJECT_BASE_POS_OFFSET_X 0
#define MAP_OBJECT_BASE_POS_OFFSET_Y (FX32_ONE * 7)
#define MAP_OBJECT_BASE_POS_OFFSET_Z (FX32_ONE * 4)
#define POKEMON_MOUNT_POS_OFFSET_Z   (FX32_ONE * 6)
#define POKEMON_RIDER_HEIGHT         (FX32_ONE * 24)
#define POKEMON_LARGE_RIDER_HEIGHT   (FX32_ONE * 32)
// Surf seat, in texels: sideways the rider sits this far from the head toward
// the tail; facing north, this far down from the top of the outline.
#define SEAT_TAIL_PERCENT  62
#define SEAT_NORTH_PERCENT 35
// The rider sprite's legs hang this far below the seat; facing south they are
// partly hidden behind the head, so less of the rider drops below it.
#define RIDER_HIP             12
#define RIDER_HIP_BEHIND_HEAD 6

typedef struct SurfMountResources {
    FieldEffectManager *fieldEffMan;
    Simple3DModel surfMountModel;
    Simple3DRenderObj surfMountRenderObj;
} SurfMountResources;

typedef struct SurfMountUserData {
    int dir;
    FieldEffectManager *fieldEffMan;
    SurfMountResources *resources;
    MapObject *playerMapObj;
} SurfMountUserData;

// Private model/texture copies prevent palette and material changes on nearby
// Pokemon. Only this one requested texture is read on DS, never the bundle.
typedef struct SurfPokemonVisual {
    void *modelData;
    void *textureData;
    Billboard *billboard;
    Billboard *reflection;
    SysTask *uploadTask;
    NNSGfdTexKey texKey;
    NNSGfdTexKey tex4x4Key;
    NNSGfdPlttKey plttKey;
    u16 graphicsID;
    BOOL ready;
    MapObject *player;
    // Surf only: where the rider sits for each facing, in texels from the
    // sprite's bottom centre.
    BOOL seated;
    BOOL riderBehindHead;
    s8 seatX[4];
    s8 seatHeight[4];
    BOOL riderBehind;
} SurfPokemonVisual;

typedef struct SurfMountRenderer {
    BOOL inactive;
    int dirRaw;
    int dir;
    int mapObjLocalID;
    int mapHeaderID;
    int counter;
    BOOL syncPos;
    fx32 yOffset;
    fx32 yDelta;
    u16 selectedGraphicsID;
    SurfPokemonVisual *pokemon;
    SurfPokemonVisual *pendingPokemon;
    SurfMountUserData userData;
} SurfMountRenderer;

static void SurfMountResources_Init(SurfMountResources *resources);
static void SurfMountResources_Free(SurfMountResources *resources);

static const OverworldAnimManagerFuncs sSurfMountRendererAnimFuncs;

// Same eligibility as the HM menu and shoreline prompt, so unlearnedHMs
// mounts compatible Pokemon too. Field HM use permits fainted Pokemon.
static BOOL SurfMountPokemon_CanSurf(Pokemon *mon)
{
    return Pokemon_CanUseFieldMove(mon, MOVE_SURF);
}

void SurfMountRenderer_SelectPokemon(FieldSystem *fieldSystem, int partySlot)
{
    Party *party = SaveData_GetParty(fieldSystem->saveData);
    fieldSystem->surfMountPokemonSelected = FALSE;
    if (partySlot < 0 || partySlot >= Party_GetCurrentCount(party)) {
        return;
    }
    Pokemon *mon = Party_GetPokemonBySlotIndex(party, partySlot);
    if (SurfMountPokemon_CanSurf(mon)) {
        fieldSystem->surfMountPersonality = Pokemon_GetValue(mon, MON_DATA_PERSONALITY, NULL);
        fieldSystem->surfMountOTID = Pokemon_GetValue(mon, MON_DATA_OT_ID, NULL);
        fieldSystem->surfMountPokemonSelected = TRUE;
    }
}

static void SurfMountPokemon_GetAppearance(FieldSystem *fieldSystem, OverworldPokemonAppearance *appearance)
{
    Party *party = SaveData_GetParty(fieldSystem->saveData);
    int selectedSlot = -1;
    memset(appearance, 0, sizeof(*appearance));
    for (int i = 0; i < Party_GetCurrentCount(party); i++) {
        Pokemon *mon = Party_GetPokemonBySlotIndex(party, i);
        if (!SurfMountPokemon_CanSurf(mon)) {
            continue;
        }
        if (selectedSlot == -1) {
            selectedSlot = i;
        }
        if (fieldSystem->surfMountPokemonSelected
            && Pokemon_GetValue(mon, MON_DATA_PERSONALITY, NULL) == fieldSystem->surfMountPersonality
            && Pokemon_GetValue(mon, MON_DATA_OT_ID, NULL) == fieldSystem->surfMountOTID) {
            selectedSlot = i;
            break;
        }
    }
    if (selectedSlot == -1) {
        fieldSystem->surfMountPokemonSelected = FALSE;
        return;
    }
    // Fresh save loads have no runtime selection. Use the same first Surf
    // knower as the shoreline script, then retain that identity across reorders.
    SurfMountRenderer_SelectPokemon(fieldSystem, selectedSlot);
    Pokemon *mon = Party_GetPokemonBySlotIndex(party, selectedSlot);
    appearance->species = Pokemon_GetValue(mon, MON_DATA_SPECIES, NULL);
    appearance->form = Pokemon_GetValue(mon, MON_DATA_FORM, NULL);
    appearance->gender = Pokemon_GetGender(mon);
    appearance->shiny = Pokemon_IsShiny(mon);
}

void PokemonMountVisual_Free(SurfPokemonVisual *visual)
{
    if (visual == NULL) {
        return;
    }
    if (visual->uploadTask != NULL) {
        SysTask_Done(visual->uploadTask);
    }
    if (visual->billboard != NULL) {
        if (visual->reflection != NULL) {
            Billboard_Delete(visual->reflection);
        }
        Billboard_Delete(visual->billboard);
    }
    if (visual->texKey != 0) {
        NNS_GfdFreeTexVram(visual->texKey);
    }
    if (visual->tex4x4Key != 0) {
        NNS_GfdFreeTexVram(visual->tex4x4Key);
    }
    if (visual->plttKey != 0) {
        NNS_GfdFreePlttVram(visual->plttKey);
    }
    if (visual->modelData != NULL) {
        Heap_Free(visual->modelData);
    }
    if (visual->textureData != NULL) {
        Heap_Free(visual->textureData);
    }
    Heap_Free(visual);
}

// Sprites are drawn one texel per pixel, but a world height offset moves them
// by only the cosine of the camera's pitch on screen. Lift by texels instead.
static fx32 TexelsToHeight(const MapObject *player, int texels)
{
    const Camera *camera = MapObject_FieldSystem(player)->camera;
    VecFx32 target = Camera_GetTarget(camera), position = Camera_GetPosition(camera), view;
    VEC_Subtract(&position, &target, &view);
    VEC_Normalize(&view, &view);
    fx32 horizontal = FX_Sqrt(FX_Mul(view.x, view.x) + FX_Mul(view.z, view.z));
    if (horizontal < FX32_ONE / 8) {
        return texels * FX32_ONE;
    }
    return FX_Div(texels * FX32_ONE, horizontal);
}

static int TexelAt(const u8 *frame, int width, int x, int y)
{
    return frame[(y * width + x) / 2] >> ((x & 1) * 4) & 15;
}

// The seat follows the mount's outline: on its back behind the head, toward
// the tail when it faces sideways. Measured on each facing's first frame.
static void SurfPokemonVisual_MeasureSeats(SurfPokemonVisual *visual, const NNSG3dResTex *texture, const BillboardAnim *anims, const BillboardGfxSequence *sequence)
{
    const u8 *images = (const u8 *)texture + texture->texInfo.ofsTex;

    for (int dir = DIR_NORTH; dir <= DIR_EAST; dir++) {
        u8 textureIdx = BillboardGfxSequence_GetTexPlttIndexAt(sequence, anims[dir].startFrame).textureIdx;
        if (textureIdx >= texture->dict.numEntry) {
            return;
        }
        u32 param = NNS_G3dGetTexDataByIdx(texture, textureIdx)->texImageParam;
        int width = 8 << (param >> 20 & 7), height = 8 << (param >> 23 & 7);
        if ((param >> 26 & 7) != GX_TEXFMT_PLTT16) {
            return;
        }
        const u8 *frame = images + ((param & 0xffff) << 3);
        int left = width, right = -1, top = height, bottom = -1;
        for (int y = 0; y < height; y++) {
            for (int x = 0; x < width; x++) {
                if (TexelAt(frame, width, x, y) != 0) {
                    left = x < left ? x : left;
                    right = x > right ? x : right;
                    top = y < top ? y : top;
                    bottom = y > bottom ? y : bottom;
                }
            }
        }
        if (right < 0) {
            return;
        }
        int seatX = (left + right + 1) / 2;
        if (dir == DIR_WEST) {
            seatX = left + (right - left + 1) * SEAT_TAIL_PERCENT / 100;
        } else if (dir == DIR_EAST) {
            seatX = right + 1 - (right - left + 1) * SEAT_TAIL_PERCENT / 100;
        }
        int seatRow = bottom;
        // Large mounts' outlines peak at the back or tail, not the head, so
        // facing south the rider sits on the back in front, as when facing north.
        BOOL behindHead = dir == DIR_SOUTH && height <= 32;
        if (dir == DIR_SOUTH) {
            visual->riderBehindHead = behindHead;
        }
        if (dir == DIR_NORTH || (dir == DIR_SOUTH && !behindHead)) {
            seatRow = top + (bottom - top) * SEAT_NORTH_PERCENT / 100;
        } else {
            for (int x = seatX - 2; x <= seatX + 2; x++) {
                for (int y = 0; y < height && x >= 0 && x < width; y++) {
                    if (TexelAt(frame, width, x, y) != 0) {
                        seatRow = y < seatRow ? y : seatRow;
                        break;
                    }
                }
            }
        }
        visual->seatX[dir] = seatX - width / 2;
        visual->seatHeight[dir] = height - 1 - seatRow;
    }
    visual->seated = TRUE;
}

// Facing the camera, the rider sits behind the head, so the mount draws in front.
// Otherwise the rider covers the mount's back.
static void SurfPokemonVisual_UpdateDepth(Billboard *mount, void *context)
{
    SurfPokemonVisual *visual = context;
    PokemonReflection_SetLayer(mount, ov5_021EB1A0(visual->player), visual->riderBehind, NNS_G3dGlbGetCameraMtx(), NNS_G3dGlbGetProjectionMtx());
}

static void SurfPokemonVisual_Upload(SysTask *task, void *context)
{
    SurfPokemonVisual *visual = context;
    NNSG3dResTex *texture = NNS_G3dGetTex(visual->textureData);
    NNS_G3dTexSetTexKey(texture, visual->texKey, visual->tex4x4Key);
    NNS_G3dPlttSetPlttKey(texture, visual->plttKey);
    NNS_G3dTexLoad(texture, TRUE);
    NNS_G3dPlttLoad(texture, TRUE);
    visual->ready = TRUE;
    visual->uploadTask = NULL;
    SysTask_Done(task);
}

PokemonMountVisual *PokemonMountVisual_New(MapObject *player, u16 graphicsID, BOOL surf)
{
#ifdef SDK_PORT
    if (!FollowerAssets_Ready()) {
        return NULL;
    }
#endif
    if (!OverworldPokemonAssets_IsGraphicsID(graphicsID)) {
        return NULL;
    }
    FieldEffectManager *fieldEffMan = MapObject_GetFieldEffectManager(player);
    SurfPokemonVisual *visual = Heap_Alloc(HEAP_ID_FIELD1, sizeof(*visual));
    if (visual == NULL) {
        return NULL;
    }
    memset(visual, 0, sizeof(*visual));
    visual->graphicsID = graphicsID;
    visual->player = player;

    const ObjectEventGfxModelAnimEntry *entry = &gObjectEventGfxModelAnimsTable[graphicsID - OVERWORLD_POKEMON_GFX_BASE];
    const ObjectEventGfxFileEntry *model = gObjectEventGfxModelsTable;
    while (model->id != entry->modelID && model->id != BILLBOARD_MODEL_SENTINEL_ID) {
        model++;
    }
    if (model->id == BILLBOARD_MODEL_SENTINEL_ID) {
        PokemonMountVisual_Free(visual);
        return NULL;
    }
    const MapObjectManager *objects = MapObject_MapObjectManager(player);
    visual->modelData = ov5_021ECD68(objects, model->narcIdx, FALSE);
    visual->textureData = ov5_021ECD68(objects,
        OVERWORLD_POKEMON_RESOURCE_BASE + (graphicsID - OVERWORLD_POKEMON_GFX_BASE) / 2,
        FALSE);
    if (visual->modelData == NULL || visual->textureData == NULL) {
        PokemonMountVisual_Free(visual);
        return NULL;
    }
    NNSG3dResTex *texture = NNS_G3dGetTex(visual->textureData);
    u32 texSize = NNS_G3dTexGetRequiredSize(texture);
    u32 tex4x4Size = NNS_G3dTex4x4GetRequiredSize(texture);
    u32 plttSize = NNS_G3dPlttGetRequiredSize(texture);
    if (texSize != 0) {
        visual->texKey = NNS_GfdAllocTexVram(texSize, FALSE, 0);
    }
    if (tex4x4Size != 0) {
        visual->tex4x4Key = NNS_GfdAllocTexVram(tex4x4Size, TRUE, 0);
    }
    if (plttSize != 0) {
        visual->plttKey = NNS_GfdAllocPlttVram(plttSize, texture->tex4x4Info.flag & NNS_G3D_RESPLTT_USEPLTT4, 0);
    }
    if ((texSize != 0 && visual->texKey == 0)
        || (tex4x4Size != 0 && visual->tex4x4Key == 0)
        || (plttSize != 0 && visual->plttKey == 0)) {
        PokemonMountVisual_Free(visual);
        return NULL;
    }
    u32 sequenceSize;
    u32 sequenceID = (graphicsID - OVERWORLD_POKEMON_GFX_BASE) & 1
        ? OVERWORLD_POKEMON_SEQUENCE_SHINY
        : OVERWORLD_POKEMON_SEQUENCE_NORMAL;
    BillboardGfxSequence sequence;
    BillboardGfxSequence_SetData(OverworldPokemonAssets_GetResource(sequenceID, &sequenceSize), &sequence);
    if (surf) {
        SurfPokemonVisual_MeasureSeats(visual, texture, entry->animations, &sequence);
    }
    BillboardResources resources;
    BillboardResources_Set(&resources, visual->modelData, texture, entry->animations, &sequence, visual->texKey, visual->tex4x4Key, visual->plttKey);
    VecFx32 position = { 0, 0, 0 };
    visual->billboard = FieldEffectManager_TryAddBillboard(fieldEffMan, &resources, &position);
    if (visual->billboard == NULL) {
        PokemonMountVisual_Free(visual);
        return NULL;
    }
    Billboard_SetDrawFlag(visual->billboard, FALSE);
    if (surf) {
        Billboard_SetCallback(visual->billboard, SurfPokemonVisual_UpdateDepth, visual);
    }
    DC_FlushRange(texture, texture->header.size);
    visual->uploadTask = SysTask_ExecuteOnVBlank(SurfPokemonVisual_Upload, visual, 0xffff);
    if (visual->uploadTask == NULL) {
        PokemonMountVisual_Free(visual);
        return NULL;
    }
    if (surf) {
        visual->reflection = PokemonReflection_New(fieldEffMan, &resources, &position);
    }
    return visual;
}

fx32 PokemonMountVisual_RiderHeight(const PokemonMountVisual *visual)
{
    if (visual == NULL || !visual->ready) {
        return 0;
    }
    return OverworldPokemonAssets_IsLarge(visual->graphicsID)
        ? POKEMON_LARGE_RIDER_HEIGHT
        : POKEMON_RIDER_HEIGHT;
}

void PokemonMountVisual_Tick(PokemonMountVisual *visual, BOOL paused)
{
    if (visual != NULL && visual->ready && !paused) {
        Billboard_AdvanceAnim(visual->billboard, FX32_ONE);
    }
}

void PokemonMountVisual_Render(PokemonMountVisual *visual, const VecFx32 *position, int direction, BOOL visible)
{
    if (visual == NULL) {
        return;
    }
    Billboard *billboard = visual->billboard;
    visible = visible && visual->ready && direction >= DIR_NORTH && direction <= DIR_EAST;
    Billboard_SetDrawFlag(billboard, visible);
    if (visible) {
        VecFx32 anchor = *position;
        anchor.z += POKEMON_MOUNT_POS_OFFSET_Z;
        visual->riderBehind = visual->seated && visual->riderBehindHead && direction == DIR_SOUTH;
        Billboard_SetPos(billboard, &anchor);
        if (Billboard_GetAnimNum(billboard) != direction) {
            Billboard_SetAnimNum(billboard, direction);
            Billboard_SetAnimFrameNum(billboard, 0);
        }
    }
}

static void SurfMountRenderer_RefreshPokemon(SurfMountRenderer *renderer)
{
    OverworldPokemonAppearance appearance;
    SurfMountPokemon_GetAppearance(renderer->userData.fieldEffMan->fieldSystem, &appearance);
    u16 graphicsID = OverworldPokemonAssets_Resolve(&appearance);
    if (graphicsID != renderer->selectedGraphicsID) {
        renderer->selectedGraphicsID = graphicsID;
        PokemonMountVisual_Free(renderer->pendingPokemon);
        renderer->pendingPokemon = NULL;
        if (graphicsID != OVERWORLD_POKEMON_INVALID_GFX) {
            renderer->pendingPokemon = PokemonMountVisual_New(renderer->userData.playerMapObj, graphicsID, TRUE);
        }
        if (renderer->pendingPokemon == NULL) {
            PokemonMountVisual_Free(renderer->pokemon);
            renderer->pokemon = NULL;
        }
    }
}

static fx32 SurfMountRenderer_RiderHeight(const SurfMountRenderer *renderer)
{
    if (renderer->pokemon == NULL) {
        return MAP_OBJECT_BASE_POS_OFFSET_Y;
    }
    return PokemonMountVisual_RiderHeight(renderer->pokemon);
}

void *SurfMountRenderer_New(FieldEffectManager *fieldEffMan)
{
    SurfMountResources *resources = FieldEffectManager_HeapAllocInit(fieldEffMan, sizeof(SurfMountResources), FALSE, 0);
    resources->fieldEffMan = fieldEffMan;

    SurfMountResources_Init(resources);
    return resources;
}

void SurfMountRenderer_Free(void *context)
{
    SurfMountResources *resources = context;

    SurfMountResources_Free(resources);
    FieldEffectManager_HeapFree(resources);
}

static void SurfMountResources_Init(SurfMountResources *resources)
{
    FieldEffectManager_LoadModel(resources->fieldEffMan, &resources->surfMountModel, 0, 81, FALSE);
    Simple3D_CreateRenderObject(&resources->surfMountRenderObj, &resources->surfMountModel);
}

static void SurfMountResources_Free(SurfMountResources *resources)
{
    Simple3D_FreeModel(&resources->surfMountModel);
}

OverworldAnimManager *SurfMountRenderer_HandleSurfBegin(MapObject *playerMapObj, int tileX, int tileZ, int dir, BOOL syncPos)
{
    SurfMountUserData userData;
    VecFx32 mapObjPos = { 0, 0, 0 };

    userData.dir = dir;
    userData.fieldEffMan = MapObject_GetFieldEffectManager(playerMapObj);
    userData.resources = FieldEffectManager_GetRendererContext(userData.fieldEffMan, FIELD_EFFECT_RENDERER_SURF_MOUNT);
    userData.playerMapObj = playerMapObj;

    if (!syncPos) {
        FieldSystem *fieldSystem = MapObject_FieldSystem(playerMapObj);

        mapObjPos.x = MAP_OBJECT_COORD_CENTER_TO_FX32(tileX);
        mapObjPos.z = MAP_OBJECT_COORD_CENTER_TO_FX32(tileZ);
        MapObject_RecalculatePositionHeight(fieldSystem, &mapObjPos);
    } else {
        VecFx32 spritePosOffset = { MAP_OBJECT_BASE_POS_OFFSET_X, MAP_OBJECT_BASE_POS_OFFSET_Y, MAP_OBJECT_BASE_POS_OFFSET_Z };

        MapObject_GetPosPtr(playerMapObj, &mapObjPos);
        MapObject_SetSpritePosOffset(playerMapObj, &spritePosOffset);
    }

    int taskPriority = MapObject_CalculateTaskPriority(playerMapObj, 2);
    return FieldEffectManager_InitAnimManager(userData.fieldEffMan, &sSurfMountRendererAnimFuncs, &mapObjPos, syncPos, &userData, taskPriority);
}

static BOOL SurfMountRenderer_AnimInit(OverworldAnimManager *animMan, void *context)
{
    SurfMountRenderer *renderer = context;
    const SurfMountUserData *userData = OverworldAnimManager_GetUserData(animMan);
    MapObject *playerMapObj = userData->playerMapObj;

    renderer->userData = *userData;
    renderer->mapObjLocalID = MapObject_GetLocalID(playerMapObj);
    renderer->mapHeaderID = MapObject_GetMapHeaderID(playerMapObj);
    renderer->dir = userData->dir;
    renderer->syncPos = OverworldAnimManager_GetUserInt(animMan);
    renderer->yOffset = RENDERER_INITIAL_Y_OFFSET;
    renderer->yDelta = RENDERER_INITIAL_Y_DELTA;
    renderer->selectedGraphicsID = OVERWORLD_POKEMON_INVALID_GFX;
    SurfMountRenderer_RefreshPokemon(renderer);
    FollowingPokemon_OnSurfBegin(userData->fieldEffMan->fieldSystem);

    return TRUE;
}

static void SurfMountRenderer_AnimExit(OverworldAnimManager *animMan, void *context)
{
    SurfMountRenderer *renderer = context;
    MapObject *playerMapObj = renderer->userData.playerMapObj;
    VecFx32 spritePosOffset = { 0, 0, 0 };

    PokemonMountVisual_Free(renderer->pendingPokemon);
    PokemonMountVisual_Free(renderer->pokemon);
    if (sub_02062764(playerMapObj, renderer->mapObjLocalID, renderer->mapHeaderID)) {
        MapObject_SetSpritePosOffset(playerMapObj, &spritePosOffset);
    }
}

static void SurfMountRenderer_AnimTick(OverworldAnimManager *animMan, void *context)
{
    SurfMountRenderer *renderer = context;
    MapObject *playerMapObj = renderer->userData.playerMapObj;

    if (!sub_02062764(playerMapObj, renderer->mapObjLocalID, renderer->mapHeaderID)) {
        FieldEffectManager_FinishAnimManager(animMan);
        return;
    }

    FieldSystem *fieldSystem = renderer->userData.fieldEffMan->fieldSystem;
    if (renderer->syncPos && !fieldSystem->processManager->pause
        && !FieldSystem_IsRunningTask(fieldSystem) && !MapObject_IsMoving(playerMapObj)) {
        SurfMountRenderer_RefreshPokemon(renderer);
    }
    if (renderer->pendingPokemon != NULL && renderer->pendingPokemon->ready) {
        PokemonMountVisual_Free(renderer->pokemon);
        renderer->pokemon = renderer->pendingPokemon;
        renderer->pendingPokemon = NULL;
    }

    renderer->inactive = FALSE;
    renderer->dirRaw = MapObject_GetFacingDir(playerMapObj);

    if (renderer->dirRaw == DIR_NONE) {
        renderer->inactive = TRUE;
        return;
    }

    PokemonMountVisual_Tick(renderer->pokemon, ov5_021ECD38(playerMapObj));

    if (!renderer->syncPos) {
        return;
    }

    renderer->dir = renderer->dirRaw;
    if (!ov5_021ECD38(playerMapObj)) {
        fx32 delta = renderer->yDelta;
#ifdef SDK_PORT
        if (FrameRate_Is60Fps()) {
            delta /= 2;
        }
#endif
        renderer->yOffset += delta;
        if (renderer->yOffset >= RENDERER_MAX_Y_OFFSET) {
            renderer->yOffset = RENDERER_MAX_Y_OFFSET;
            renderer->yDelta = -renderer->yDelta;
        } else if (renderer->yOffset <= RENDERER_MIN_Y_OFFSET) {
            renderer->yOffset = RENDERER_MIN_Y_OFFSET;
            renderer->yDelta = -renderer->yDelta;
        }
    }

    VecFx32 spritePosOffset;
    spritePosOffset.x = MAP_OBJECT_BASE_POS_OFFSET_X;
    spritePosOffset.y = SurfMountRenderer_RiderHeight(renderer) + renderer->yOffset;
    if (renderer->pokemon != NULL && renderer->pokemon->ready && renderer->pokemon->seated) {
        const SurfPokemonVisual *mount = renderer->pokemon;
        spritePosOffset.x += mount->seatX[renderer->dir] * FX32_ONE;
        spritePosOffset.y = TexelsToHeight(playerMapObj, mount->seatHeight[renderer->dir] - (renderer->dir == DIR_SOUTH && mount->riderBehindHead ? RIDER_HIP_BEHIND_HEAD : RIDER_HIP)) + renderer->yOffset;
    }
    spritePosOffset.z = MAP_OBJECT_BASE_POS_OFFSET_Z;

    MapObject_SetSpritePosOffset(playerMapObj, &spritePosOffset);

    VecFx32 mapObjPos;

    MapObject_GetPosPtr(playerMapObj, &mapObjPos);
    mapObjPos.y += renderer->yOffset - FX32_ONE;
    OverworldAnimManager_SetPosition(animMan, &mapObjPos);
}

static void SurfMountRenderer_AnimRender(OverworldAnimManager *animMan, void *context)
{
    SurfMountRenderer *renderer = context;

    if (renderer->pokemon != NULL) {
        Billboard *billboard = renderer->pokemon->billboard;
        MapObject *player = renderer->userData.playerMapObj;
        BOOL visible = !renderer->inactive && renderer->dir != DIR_NONE && !MapObject_IsHidden(player);
        VecFx32 position;
        OverworldAnimManager_GetPosition(animMan, &position);
        PokemonMountVisual_Render(renderer->pokemon, &position, renderer->dir, visible);
        if (renderer->pokemon->reflection != NULL) {
            VecFx32 anchor, reflectionPos;
            OverworldAnimManager_GetPosition(animMan, &anchor);
            BOOL reflect = visible && PokemonReflection_GetWaterPosition(renderer->userData.fieldEffMan->fieldSystem, billboard, &anchor, &reflectionPos);
            if (reflect) {
                PokemonReflection_Update(renderer->pokemon->reflection, billboard, &reflectionPos, OverworldPokemonAssets_IsLarge(renderer->pokemon->graphicsID));
            } else {
                Billboard_SetDrawFlag(renderer->pokemon->reflection, FALSE);
            }
        }
        return;
    }

    if (renderer->pendingPokemon != NULL) {
        return;
    }

    if (renderer->inactive == TRUE || renderer->dir == DIR_NONE) {
        return;
    }

    renderer->counter++;

    u32 rotAngle = 0;
    MtxFx33 rotation;
    VecFx32 pos;
    VecFx32 scale = { FX32_ONE, FX32_ONE, FX32_ONE };
    Simple3DRenderObj *surfMountRenderObj = &renderer->userData.resources->surfMountRenderObj;

    switch (renderer->dir) {
    case DIR_NORTH:
        rotAngle = 180;
        break;

    case DIR_SOUTH:
        break;

    case DIR_WEST:
        rotAngle = 270;
        break;

    case DIR_EAST:
        rotAngle = 90;
        break;
    }

    MTX_Rot33Angles(&rotation, 0, rotAngle, 0);
    OverworldAnimManager_GetPosition(animMan, &pos);
    Simple3D_DrawRenderObj(surfMountRenderObj, &pos, &scale, &rotation);
}

void SurfMountRenderer_SetSyncPos(OverworldAnimManager *animMan, BOOL syncPos)
{
    SurfMountRenderer *renderer = OverworldAnimManager_GetFuncsContext(animMan);

    renderer->syncPos = syncPos;
    renderer->yOffset = RENDERER_INITIAL_Y_OFFSET;
    renderer->yDelta = RENDERER_INITIAL_Y_DELTA;
}

Billboard *SurfMountRenderer_GetReflection(FieldSystem *fieldSystem)
{
    PlayerAvatar *avatar = fieldSystem->playerAvatar;
    if (avatar == NULL || PlayerAvatar_GetPlayerState(avatar) != PLAYER_AVATAR_SURFING
        || PlayerAvatar_MapDistortionState(avatar) != AVATAR_DISTORTION_STATE_NONE) {
        return NULL;
    }
    OverworldAnimManager *manager = PlayerAvatar_GetSurfMountAnimManager(avatar);
    if (manager == NULL || !OverworldAnimManager_IsActive(manager)) {
        return NULL;
    }
    SurfMountRenderer *renderer = OverworldAnimManager_GetFuncsContext(manager);
    return renderer->pokemon != NULL ? renderer->pokemon->reflection : NULL;
}

static const OverworldAnimManagerFuncs sSurfMountRendererAnimFuncs = {
    sizeof(SurfMountRenderer),
    SurfMountRenderer_AnimInit,
    SurfMountRenderer_AnimExit,
    SurfMountRenderer_AnimTick,
    SurfMountRenderer_AnimRender
};
