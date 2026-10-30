#include "overlay005/ov5_021EB1A0.h"

#include <nitro.h>
#include <string.h>

#include "generated/object_events_gfx.h"

#include "struct_decls/map_object.h"

#include "overlay005/field_effect_manager.h"
#include "overlay005/object_event_gfx_data.h"
#include "overlay005/ov5_021ECA70.h"
#include "overlay005/ov5_021ECC20.h"
#include "overlay005/ov5_021ECE40.h"
#include "overlay005/ov5_021F10E8.h"
#include "overlay005/ov5_021F23D0.h"
#include "overlay005/struct_ov5_021ED01C.h"

#include "billboard.h"
#include "easy3d.h"
#include "following_pokemon_interaction.h"
#include "heap.h"
#include "map_object.h"
#include "math_util.h"
#include "overworld_anim_manager.h"
#include "overworld_pokemon.h"
#include "overworld_pokemon_assets.h"
#include "simple3d.h"
#ifdef SDK_PORT
#include "port/sim_config_prj.h"
#endif

#include "res/graphics/following_pokemon/ball_effects.inc"
#include "res/graphics/following_pokemon/reaction_emotes.inc"

struct FollowerEmote {
    void *modelData, *textureData;
    NNSGfdTexKey textureKey;
    NNSGfdPlttKey paletteKey;
    BillboardList *list;
    Billboard *billboard;
};

void FollowingPokemon_FreeEmote(FollowerEmote *emote)
{
    if (emote == NULL) {
        return;
    }
    if (emote->list) {
        BillboardList_Delete(emote->list);
    }
    if (emote->textureKey) {
        NNS_GfdFreeTexVram(emote->textureKey);
    }
    if (emote->paletteKey) {
        NNS_GfdFreePlttVram(emote->paletteKey);
    }
    if (emote->modelData) {
        Heap_Free(emote->modelData);
    }
    if (emote->textureData) {
        Heap_Free(emote->textureData);
    }
    Heap_Free(emote);
}

void FollowingPokemon_UpdateEmote(FollowerEmote *emote, MapObject *object, int frame)
{
    if (emote == NULL) {
        return;
    }
    VecFx32 position;
    ov5_021ECDA0(object, &position);
    VecFx32 offset;
    if (FollowingPokemon_GetReactionOffset(object, &offset)) {
        position.x += offset.x;
        position.y += offset.y;
        position.z += offset.z;
    }
    // The bubble rises then settles; large cards need an additional screen-up
    // offset under Platinum's camera. Neither offset changes logical tiles.
    int rise = frame < 6 ? frame * (6 - frame) : 0;
    position.y += (32 + rise) * FX32_ONE;
    if (OverworldPokemonAssets_IsLarge(MapObject_GetGraphicsID(object))) {
        const MtxFx43 *camera = NNS_G3dGlbGetCameraMtx();
        position.x += camera->_01 * 11;
        position.y += camera->_11 * 11;
        position.z += camera->_21 * 11;
    }
    Billboard_SetPos(emote->billboard, &position);
    Billboard_SetFrameNum(emote->billboard, (frame < 10 || (frame >= 14 && frame < 22)) ? 0 : FX32_ONE * 4);
}

FollowerEmote *FollowingPokemon_CreateEmote(MapObject *object, int type)
{
    static const BillboardAnim animations[] = {
        { 0, 15, BILLBOARD_ANIM_TYPE_LOOP }, { 0, 0, BILLBOARD_ANIM_TYPE_TABLE_END }
    };
    if (type < 1 || type > NELEMS(sReactionEmoteTextures)) {
        return NULL;
    }
    FollowerEmote *emote = Heap_Alloc(HEAP_ID_FIELD1, sizeof(*emote));
    if (emote == NULL) {
        return NULL;
    }
    memset(emote, 0, sizeof(*emote));
    emote->modelData = Heap_Alloc(HEAP_ID_FIELD1, sizeof(sReactionEmoteModel));
    emote->textureData = Heap_Alloc(HEAP_ID_FIELD1, sReactionEmoteTextures[type - 1].size);
    if (!emote->modelData || !emote->textureData) {
        FollowingPokemon_FreeEmote(emote);
        return NULL;
    }
    memcpy(emote->modelData, sReactionEmoteModel, sizeof(sReactionEmoteModel));
    memcpy(emote->textureData, sReactionEmoteTextures[type - 1].data, sReactionEmoteTextures[type - 1].size);
    NNSG3dResTex *texture = NNS_G3dGetTex(emote->textureData);
    emote->textureKey = NNS_GfdAllocTexVram(NNS_G3dTexGetRequiredSize(texture), FALSE, 0);
    emote->paletteKey = NNS_GfdAllocPlttVram(NNS_G3dPlttGetRequiredSize(texture), FALSE, 0);
    if (!emote->textureKey || !emote->paletteKey) {
        FollowingPokemon_FreeEmote(emote);
        return NULL;
    }
    NNS_G3dTexSetTexKey(texture, emote->textureKey, 0);
    NNS_G3dPlttSetPlttKey(texture, emote->paletteKey);
    DC_FlushRange(texture, texture->header.size);
    NNS_G3dTexLoad(texture, TRUE);
    NNS_G3dPlttLoad(texture, TRUE);
    BillboardGfxSequence sequence;
    BillboardGfxSequence_SetData(sReactionEmoteSequence, &sequence);
    BillboardResources resources;
    BillboardResources_Set(&resources, emote->modelData, texture, animations, &sequence, emote->textureKey, 0, emote->paletteKey);
    // A private list and resource copies avoid taking fixed global effect slots.
    BillboardListParams params = { 1, HEAP_ID_FIELD1 };
    emote->list = BillboardList_New(&params);
    if (emote->list == NULL) {
        FollowingPokemon_FreeEmote(emote);
        return NULL;
    }
    BillboardTemplate template = { emote->list, &resources, { 0, 0, 0 }, {FX32_ONE,FX32_ONE,FX32_ONE} };
    emote->billboard = BillboardList_Append(&template);
    if (emote->billboard == NULL) {
        FollowingPokemon_FreeEmote(emote);
        return NULL;
    }
    FollowingPokemon_UpdateEmote(emote, object, 0);
    return emote;
}

typedef struct {
    Simple3DModel ballModel, burstModel;
    Simple3DRenderObj ball, burst;
    Simple3DAnimation animation;
    void *animationData;
    int ticks, duration;
} FollowerBallVisual;

// The field effect owns its models and animation, and keeps no MapObject
// pointer. Deletion/reuse of the actor cannot redirect a lingering effect.
typedef struct {
    FollowerBallVisual *visual;
} FollowerBallContext;

static void LoadFollowerModel(Simple3DModel *model, Simple3DRenderObj *render, const void *data, u32 size)
{
    model->modelSetHeader = Heap_Alloc(HEAP_ID_FIELD1, size);
    memcpy(model->modelSetHeader, data, size);
    Easy3D_LoadModelFromResource(&model->g3DModel, &model->modelSetHeader);
    model->modelSet = NNS_G3dGetMdlSet(model->modelSetHeader);
    model->texture = NNS_G3dGetTex(model->modelSetHeader);
    model->isTextureBound = TRUE;
    Simple3D_CreateRenderObject(render, model);
}

static BOOL InitFollowerBall(OverworldAnimManager *manager, void *data)
{
    FollowerBallContext *ctx = data;
    ctx->visual = Heap_Alloc(HEAP_ID_FIELD1, sizeof(FollowerBallVisual));
    FollowerBallVisual *v = ctx->visual;
    memset(v, 0, sizeof(*v));
    v->duration = 8;
#ifdef SDK_PORT
    if (SIM_Config_prj_GetConfig()->enable60fps) v->duration *= 2;
#endif
    LoadFollowerModel(&v->ballModel, &v->ball, sFollowerBall, sizeof(sFollowerBall));
    LoadFollowerModel(&v->burstModel, &v->burst, sFollowerBurst, sizeof(sFollowerBurst));
    v->animationData = Heap_Alloc(HEAP_ID_FIELD1, sizeof(sFollowerBurstAnim));
    memcpy(v->animationData, sFollowerBurstAnim, sizeof(sFollowerBurstAnim));
    Simple3D_LoadFromAllocatedSet(&v->animation, v->animationData, 0);
    Simple3D_BindModelToAnim(&v->animation, &v->burstModel, HEAP_ID_FIELD1);
    Simple3D_InitG3DAnimObject(&v->animation, &v->burstModel);
    Simple3D_BindAnimToRenderObj(&v->burst, &v->animation);
    return TRUE;
}

static void FreeFollowerBall(OverworldAnimManager *manager, void *data)
{
    FollowerBallVisual *v = ((FollowerBallContext *)data)->visual;
    Simple3D_FreeAnimObject(&v->animation);
    Heap_Free(v->animationData);
    Simple3D_FreeModel(&v->burstModel);
    Simple3D_FreeModel(&v->ballModel);
    Heap_Free(v);
}

static void TickFollowerBall(OverworldAnimManager *manager, void *data)
{
    FollowerBallVisual *v = ((FollowerBallContext *)data)->visual;
    if (++v->ticks >= v->duration) {
        FieldEffectManager_FinishAnimManager(manager);
        return;
    }
    Simple3D_UpdateAnim(&v->animation, FX32_ONE, TRUE);
}

static void DrawFollowerBall(OverworldAnimManager *manager, void *data)
{
    FollowerBallVisual *v = ((FollowerBallContext *)data)->visual;
    VecFx32 position;
    OverworldAnimManager_GetPosition(manager, &position);
    BOOL release = OverworldAnimManager_GetUserInt(manager);
    if (!release || v->ticks < v->duration / 2) {
        Simple3D_DrawRenderObjWithPos(&v->ball, &position);
    }
    if (release || v->ticks >= v->duration / 2) {
        Simple3D_DrawRenderObjWithPos(&v->burst, &position);
    }
}

void FollowingPokemon_StartBallEffect(MapObject *object, BOOL release)
{
    static const OverworldAnimManagerFuncs funcs = {
        sizeof(FollowerBallContext), InitFollowerBall, FreeFollowerBall, TickFollowerBall, DrawFollowerBall
    };
    FieldEffectManager *effects = MapObject_GetFieldEffectManager(object);
    if (effects == NULL || effects->animManList == NULL) {
        return;
    }
    VecFx32 position;
    MapObject_GetPosPtr(object, &position);
    // Exhaustion skips optional presentation while retaining follower state.
    OverworldAnimManagerList_InitManager(effects->animManList, &funcs, &position, release, NULL,
        MapObject_CalculateTaskPriority(object, 2));
}

typedef struct {
    s16 unk_00;
    s8 unk_02;
    s8 unk_03;
    Billboard *unk_04;
    UnkStruct_ov5_021ED01C unk_08;
} UnkStruct_ov5_021EB2EC;

typedef struct {
    s8 unk_00;
    s8 unk_01;
    s8 unk_02;
    s8 unk_03;
    Billboard *unk_04;
    UnkStruct_ov5_021ED01C unk_08;
} UnkStruct_ov5_021EB7F8;

typedef struct {
    s8 unk_00;
    s8 unk_01;
    s8 unk_02;
    u8 unk_03_0 : 1;
    u8 unk_03_1 : 7;
    Billboard *unk_04;
    UnkStruct_ov5_021ED01C unk_08;
} UnkStruct_ov5_021EBA0C;

typedef struct {
    OverworldAnimManager *unk_00;
} UnkStruct_ov5_021EC760;

typedef struct {
    s16 unk_00;
    s8 unk_02;
    s8 unk_03;
    Billboard *unk_04;
    UnkStruct_ov5_021ED01C unk_08;
    u32 unk_10;
} UnkStruct_ov5_021EC804;

typedef struct {
    int unk_00;
    fx32 unk_04;
    Billboard *unk_08;
    UnkStruct_ov5_021ED01C unk_0C;
} UnkStruct_ov5_021EC938;

void ov5_021EB2EC(MapObject *mapObj);
void ov5_021EB314(MapObject *mapObj);
void ov5_021EB328(MapObject *mapObj);
void ov5_021EB354(MapObject *mapObj);
void ov5_021EB398(MapObject *mapObj);
void ov5_021EB40C(MapObject *mapObj);
static void ov5_021EB478(MapObject *mapObj, Billboard *param1, UnkStruct_ov5_021EB2EC *param2, int param3);
static void ov5_021EB4AC(MapObject *mapObj, Billboard *param1, UnkStruct_ov5_021EB2EC *param2, int param3);
static void ov5_021EB4E8(MapObject *mapObj, Billboard *param1, UnkStruct_ov5_021EB2EC *param2, int param3);
static void ov5_021EB524(MapObject *mapObj, Billboard *param1, UnkStruct_ov5_021EB2EC *param2, int param3);
static void ov5_021EB560(MapObject *mapObj, Billboard *param1, UnkStruct_ov5_021EB2EC *param2, int param3);
static void ov5_021EB59C(MapObject *mapObj, Billboard *param1, UnkStruct_ov5_021EB2EC *param2, int param3);
static void ov5_021EB620(MapObject *mapObj, Billboard *param1, UnkStruct_ov5_021EB2EC *param2, int param3);
static void ov5_021EC068(MapObject *mapObj, Billboard *param1, UnkStruct_ov5_021EBA0C *param2, int param3);
static void ov5_021EC0E4(MapObject *mapObj, Billboard *param1, UnkStruct_ov5_021EBA0C *param2, int param3);
void ov5_021EC734(MapObject *mapObj);
void ov5_021EC75C(MapObject *mapObj);
void ov5_021EC760(MapObject *mapObj);
void ov5_021EC778(MapObject *mapObj);
void ov5_021EC790(MapObject *mapObj);
static void ov5_021EC638(Billboard *param0, int param1);
static void ov5_021EC668(Billboard *param0);
static void ov5_021EC674(Billboard *param0, int param1, int param2);
static void ov5_021EC69C(Billboard *param0, int param1);
static void ov5_021EC6C0(MapObject *mapObj, Billboard *param1);
static void ov5_021EC374(MapObject *mapObj, Billboard *param1, int param2);

void (*const Unk_ov5_021FF3D4[])(MapObject *, Billboard *, UnkStruct_ov5_021EB2EC *, int);
void (*const Unk_ov5_021FF420[])(MapObject *, Billboard *, UnkStruct_ov5_021EB2EC *, int);
void (*const Unk_ov5_021FF3F8[])(MapObject *, Billboard *, UnkStruct_ov5_021EBA0C *, int);
static void (*const Unk_ov5_021FF448[11])(MapObject *, Billboard *, UnkStruct_ov5_021EBA0C *, int);

Billboard *ov5_021EB1A0(MapObject *mapObj)
{
    int v0 = MapObject_GetGraphicsID(mapObj);

    switch (v0) {
    case OBJ_EVENT_GFX_PLAYER_M:
    case OBJ_EVENT_GFX_PLAYER_F:
    case OBJ_EVENT_GFX_PLAYER_M_BIKE:
    case OBJ_EVENT_GFX_PLAYER_F_BIKE:
    case OBJ_EVENT_GFX_PLAYER_M_HOLDING_POKE_BALL:
    case OBJ_EVENT_GFX_PLAYER_F_HOLDING_POKE_BALL:
    case OBJ_EVENT_GFX_PLAYER_M_SURF:
    case OBJ_EVENT_GFX_PLAYER_F_SURF:
    case OBJ_EVENT_GFX_PLAYER_M_SPRAYDUCK:
    case OBJ_EVENT_GFX_PLAYER_F_SPRAYDUCK:
    case OBJ_EVENT_GFX_PLAYER_M_SAVE:
    case OBJ_EVENT_GFX_PLAYER_F_SAVE:
    case OBJ_EVENT_GFX_PLAYER_M_CONTEST:
    case OBJ_EVENT_GFX_PLAYER_F_CONTEST:
    case OBJ_EVENT_GFX_PLAYER_M_FISHING:
    case OBJ_EVENT_GFX_PLAYER_F_FISHING:
    case OBJ_EVENT_GFX_PLAYER_M_POKETCH:
    case OBJ_EVENT_GFX_PLAYER_F_POKETCH:
    case OBJ_EVENT_GFX_PLAYER_M_POKECENTER_HEAL:
    case OBJ_EVENT_GFX_PLAYER_F_POKECENTER_HEAL:
    case OBJ_EVENT_GFX_PLAYER_M_HEARTHOME_GYM:
    case OBJ_EVENT_GFX_PLAYER_F_HEARTHOME_GYM:
    case OBJ_EVENT_GFX_DIST_WORLD_PLAYER_M:
    case OBJ_EVENT_GFX_DIST_WORLD_PLAYER_F:
    case OBJ_EVENT_GFX_DIST_WORLD_PLAYER_M_SAVE:
    case OBJ_EVENT_GFX_DIST_WORLD_PLAYER_F_SAVE:
    case OBJ_EVENT_GFX_DIST_WORLD_PLAYER_M_POKETCH:
    case OBJ_EVENT_GFX_DIST_WORLD_PLAYER_F_POKETCH:
    case OBJ_EVENT_GFX_PLAYER_M_SAVE_HEARTHOME_GYM:
    case OBJ_EVENT_GFX_PLAYER_F_SAVE_HEARTHOME_GYM:
    case OBJ_EVENT_GFX_PLAYER_M_POKETCH_HEARTHOME_GYM:
    case OBJ_EVENT_GFX_PLAYER_F_POKETCH_HEARTHOME_GYM: {
        UnkStruct_ov5_021EBA0C *v1;

        v1 = sub_02062AF0(mapObj);
        return v1->unk_04;
    }
    case OBJ_EVENT_GFX_MAGIKARP: {
        UnkStruct_ov5_021EB7F8 *v2;

        v2 = sub_02062AF0(mapObj);
        return v2->unk_04;
    }
    case OBJ_EVENT_GFX_UXIE:
    case OBJ_EVENT_GFX_MESPRIT:
    case OBJ_EVENT_GFX_AZELF: {
        return ov5_021ECB80(mapObj);
    }
    case OBJ_EVENT_GFX_DIST_WORLD_B1F_MESPRIT: {
        UnkStruct_ov5_021EC804 *v3;

        v3 = sub_02062AF0(mapObj);
        return v3->unk_04;
    }
    case OBJ_EVENT_GFX_BERRY_SOIL: {
        return NULL;
    }
    default: {
        const ObjectEventGfxRenderDetailsEntry *v4;

        v4 = ov5_021ECD04(mapObj);

        if (v4->modelType == 1) {
            UnkStruct_ov5_021EB2EC *v5;

            v5 = sub_02062AF0(mapObj);
            return v5->unk_04;
        }
    }
    }

    return NULL;
}

void ov5_021EB2EC(MapObject *mapObj)
{
    UnkStruct_ov5_021EB2EC *v0 = sub_02062ACC(mapObj, (sizeof(UnkStruct_ov5_021EB2EC)));
    v0->unk_02 = -1;

    ov5_021ECF04(mapObj, &v0->unk_04);

    if (v0->unk_04 != NULL) {
        sub_02062B68(mapObj);
    }
}

void ov5_021EB314(MapObject *mapObj)
{
    UnkStruct_ov5_021EB2EC *v0 = sub_02062AF0(mapObj);
    ov5_021ECFA4(mapObj, &v0->unk_04);
}

void ov5_021EB328(MapObject *mapObj)
{
    UnkStruct_ov5_021EB2EC *v0 = sub_02062AF0(mapObj);

    if (v0->unk_04 != NULL) {
        ov5_021ED01C(v0->unk_04, &v0->unk_08);
    }

    ov5_021ECFA4(mapObj, &v0->unk_04);
    MapObject_SetStatusFlagOn(mapObj, MAP_OBJ_STATUS_21);
}

void ov5_021EB354(MapObject *mapObj)
{
    int v0;
    UnkStruct_ov5_021EB2EC *v1 = sub_02062AF0(mapObj);

    if (ov5_021EDD94(mapObj) == 1) {
        return;
    }

    if (v1->unk_04 == NULL) {
        ov5_021ECF04(mapObj, &v1->unk_04);
    }

    if (v1->unk_04 != NULL) {
        ov5_021ED03C(v1->unk_04, &v1->unk_08);
        ov5_021EDEB4(mapObj, v1->unk_04);

        MapObject_SetStatusFlagOff(mapObj, MAP_OBJ_STATUS_21);
    }
}

void ov5_021EB398(MapObject *mapObj)
{
    int v0, v1;
    UnkStruct_ov5_021EB2EC *v2 = sub_02062AF0(mapObj);
    Billboard *v3 = v2->unk_04;

    v2 = sub_02062AF0(mapObj);

    if (ov5_021EDD94(mapObj) == 1) {
        return;
    }

    if (v3 == NULL) {
        return;
    }

    v0 = MapObject_GetFacingDir(mapObj);
    v1 = sub_02062A14(mapObj);

    GF_ASSERT(v1 < (0x8 + 1));
    Unk_ov5_021FF3D4[v1](mapObj, v3, v2, v0);

    v2->unk_02 = v0;
    v2->unk_03 = sub_02062A14(mapObj);

    ov5_021EDEB4(mapObj, v3);
    ov5_021EC6C0(mapObj, v3);
}

void OverworldPokemon_DrawUpdate(MapObject *mapObj)
{
    UnkStruct_ov5_021EB2EC *work = sub_02062AF0(mapObj);
    Billboard *billboard = work->unk_04;
    if (ov5_021EDD94(mapObj) || billboard == NULL) {
        return;
    }

    int facing = MapObject_GetFacingDir(mapObj);
    if (facing != work->unk_02) {
        Billboard_SetAnimNum(billboard, ov5_021EDF18(facing));
        Billboard_SetAnimFrameNum(billboard, 0);
    }
    // HG/SS keeps the same loop running at rest and while walking. Respect
    // the engine's animation/movement pause for menus, battles and scripts.
    Billboard_AdvanceAnim(billboard, ov5_021ECD38(mapObj) ? 0 : FX32_ONE);
    work->unk_02 = facing;
    work->unk_03 = sub_02062A14(mapObj);

    ov5_021EDEB4(mapObj, billboard);
    VecFx32 pos = *Billboard_GetPos(billboard);
    VecFx32 reaction;
    if (FollowingPokemon_GetReactionOffset(mapObj, &reaction)) {
        pos.x += reaction.x;
        pos.y += reaction.y;
        pos.z += reaction.z;
    } else {
        pos.y += OverworldPokemonAssets_GetBobOffset(MapObject_GetGraphicsID(mapObj), facing,
            Billboard_GetAnimFrameNum(billboard) / FX32_ONE) * FX32_ONE;
    }
    Billboard_SetPos(billboard, &pos);
    fx32 size = OverworldPokemon_GetVisualScale(mapObj);
    VecFx32 scale = { size, size, size };
    Billboard_SetScale(billboard, &scale);
    ov5_021EC6C0(mapObj, billboard);
}

void ov5_021EB40C(MapObject *mapObj)
{
    int v0, v1;
    UnkStruct_ov5_021EB2EC *v2;
    Billboard *v3;

    v2 = sub_02062AF0(mapObj);

    if (ov5_021EDD94(mapObj) == 1) {
        return;
    }

    v3 = v2->unk_04;

    if (v3 == NULL) {
        return;
    }

    ov5_021EDEB4(mapObj, v3);
    ov5_021EC6C0(mapObj, v3);
}

void ov5_021EB438(MapObject *mapObj)
{
    int v0, v1;
    UnkStruct_ov5_021EB2EC *v2;
    Billboard *v3;

    v2 = sub_02062AF0(mapObj);

    if (ov5_021EDD94(mapObj) == 1) {
        return;
    }

    v3 = v2->unk_04;

    if (v3 == NULL) {
        return;
    }

    if (ov5_021ECD38(mapObj) == 0) {
        Billboard_AdvanceAnim(v3, (FX32_ONE));
    }

    ov5_021EDEB4(mapObj, v3);
    ov5_021EC6C0(mapObj, v3);
}

static void ov5_021EB478(MapObject *mapObj, Billboard *param1, UnkStruct_ov5_021EB2EC *param2, int param3)
{
    if (param3 != param2->unk_02) {
        int v0 = ov5_021EDF18(param3);

        Billboard_SetAnimNum(param1, v0);
        Billboard_SetAnimFrameNum(param1, 0);
    } else {
        ov5_021EC668(param1);
    }

    Billboard_AdvanceAnim(param1, 0);
}

static void ov5_021EB4AC(MapObject *mapObj, Billboard *param1, UnkStruct_ov5_021EB2EC *param2, int param3)
{
    if (param3 != param2->unk_02) {
        int v0 = ov5_021EDF18(param3);

        Billboard_SetAnimNum(param1, v0);
        Billboard_SetAnimFrameNum(param1, 0);
    }

    if (ov5_021ECD38(mapObj) == 0) {
        Billboard_AdvanceAnim(param1, ((FX32_ONE) / 2));
    }
}

static void ov5_021EB4E8(MapObject *mapObj, Billboard *param1, UnkStruct_ov5_021EB2EC *param2, int param3)
{
    if (param3 != param2->unk_02) {
        int v0 = ov5_021EDF18(param3);

        Billboard_SetAnimNum(param1, v0);
        Billboard_SetAnimFrameNum(param1, 0);
    }

    if (ov5_021ECD38(mapObj) == 0) {
        Billboard_AdvanceAnim(param1, (FX32_ONE));
    }
}

static void ov5_021EB524(MapObject *mapObj, Billboard *param1, UnkStruct_ov5_021EB2EC *param2, int param3)
{
    if (param3 != param2->unk_02) {
        int v0 = ov5_021EDF18(param3);

        Billboard_SetAnimNum(param1, v0);
        Billboard_SetAnimFrameNum(param1, 0);
    }

    if (ov5_021ECD38(mapObj) == 0) {
        Billboard_AdvanceAnim(param1, ((FX32_ONE) * 2));
    }
}

static void ov5_021EB560(MapObject *mapObj, Billboard *param1, UnkStruct_ov5_021EB2EC *param2, int param3)
{
    if (param3 != param2->unk_02) {
        int v0 = ov5_021EDF18(param3);

        Billboard_SetAnimNum(param1, v0);
        Billboard_SetAnimFrameNum(param1, 0);
    }

    if (ov5_021ECD38(mapObj) == 0) {
        Billboard_AdvanceAnim(param1, ((FX32_ONE) * 4));
    }
}

static void ov5_021EB59C(MapObject *mapObj, Billboard *param1, UnkStruct_ov5_021EB2EC *param2, int param3)
{
    if (param3 != param2->unk_02) {
        int v0 = ov5_021EDF18(param3);

        Billboard_SetAnimNum(param1, v0);
        Billboard_SetAnimFrameNum(param1, 0);

        param2->unk_00 = 0;
    }

    if (param2->unk_03 != 0x6) {
        param2->unk_00 = 0;
    }

    if (ov5_021ECD38(mapObj) == 0) {
        fx32 v1[] = {
            (FX32_ONE),
            (FX32_ONE),
            ((FX32_ONE) * 2),
            (FX32_ONE),
            (FX32_ONE),
            ((FX32_ONE) * 2),
            0
        };

        Billboard_AdvanceAnim(param1, v1[param2->unk_00]);
        param2->unk_00++;

        if (v1[param2->unk_00] == 0) {
            param2->unk_00 = 0;
        }
    }
}

static void ov5_021EB620(MapObject *mapObj, Billboard *param1, UnkStruct_ov5_021EB2EC *param2, int param3)
{
    if (param3 != param2->unk_02) {
        int v0 = ov5_021EDF18(param3);

        Billboard_SetAnimNum(param1, v0);
        Billboard_SetAnimFrameNum(param1, 0);

        param2->unk_00 = 0;
    }

    if (param2->unk_03 != 0x7) {
        param2->unk_00 = 0;
    }

    if (ov5_021ECD38(mapObj) == 0) {
        fx32 v1[] = {
            ((FX32_ONE) * 3),
            ((FX32_ONE) * 2),
            ((FX32_ONE) * 3),
            0
        };

        Billboard_AdvanceAnim(param1, v1[param2->unk_00]);
        param2->unk_00++;

        if (v1[param2->unk_00] == 0) {
            param2->unk_00 = 0;
        }
    }
}

static void ov5_021EB69C(MapObject *mapObj, Billboard *param1, UnkStruct_ov5_021EB2EC *param2, int param3)
{
    if (param3 != param2->unk_02) {
        int v0 = ov5_021EDF18(param3);

        Billboard_SetAnimNum(param1, v0);
        Billboard_SetAnimFrameNum(param1, 0);

        param2->unk_00 = 0;
    }

    if (param2->unk_03 != 0x8) {
        param2->unk_00 = 0;
    }

    if (ov5_021ECD38(mapObj) == 0) {
        fx32 v1[] = {
            ((FX32_ONE) * 2),
            (FX32_ONE),
            (FX32_ONE),
            (FX32_ONE),
            (FX32_ONE),
            (FX32_ONE),
            (FX32_ONE),
            0
        };

        Billboard_AdvanceAnim(param1, v1[param2->unk_00]);

        param2->unk_00++;
        if (v1[param2->unk_00] == 0) {
            param2->unk_00 = 0;
        }
    }
}

#ifdef SDK_BUILD_ARM
static
#endif
void (*const Unk_ov5_021FF3D4[])(
    MapObject *, Billboard *, UnkStruct_ov5_021EB2EC *, int)
    = {
          ov5_021EB478,
          ov5_021EB4AC,
          ov5_021EB4AC,
          ov5_021EB4E8,
          ov5_021EB524,
          ov5_021EB560,
          ov5_021EB59C,
          ov5_021EB620,
          ov5_021EB69C
      };

void ov5_021EB720(MapObject *mapObj)
{
    int v0, v1;
    UnkStruct_ov5_021EB2EC *v2 = sub_02062AF0(mapObj);
    Billboard *v3 = v2->unk_04;

    v2 = sub_02062AF0(mapObj);

    if (ov5_021EDD94(mapObj) == 1) {
        return;
    }

    if (v3 == NULL) {
        return;
    }

    v0 = MapObject_GetFacingDir(mapObj);
    v1 = sub_02062A14(mapObj);

    GF_ASSERT(v1 < (0x9 + 1));

    Unk_ov5_021FF420[v1](mapObj, v3, v2, v0);

    v2->unk_02 = v0;
    v2->unk_03 = sub_02062A14(mapObj);

    ov5_021EDEB4(mapObj, v3);
    ov5_021EC6C0(mapObj, v3);
}

static void ov5_021EB794(MapObject *mapObj, Billboard *param1, UnkStruct_ov5_021EB2EC *param2, int param3)
{
    if ((param3 != param2->unk_02) || (param2->unk_03 == 0x9)) {
        int v0 = ov5_021EDF18(param3);

        Billboard_SetAnimNum(param1, v0);
        Billboard_SetAnimFrameNum(param1, 0);
    } else {
        ov5_021EC668(param1);
    }

    Billboard_AdvanceAnim(param1, 0);
}

static void ov5_021EB7D0(MapObject *mapObj, Billboard *param1, UnkStruct_ov5_021EB2EC *param2, int param3)
{
    if (param2->unk_03 != 0x9) {
        Billboard_SetAnimNum(param1, 4);
        Billboard_SetAnimFrameNum(param1, 0);
    }

    Billboard_AdvanceAnim(param1, (FX32_ONE));
}

#ifdef SDK_BUILD_ARM
static
#endif
void (*const Unk_ov5_021FF420[])(MapObject *, Billboard *, UnkStruct_ov5_021EB2EC *, int) = {
    ov5_021EB794,
    ov5_021EB4AC,
    ov5_021EB4AC,
    ov5_021EB4E8,
    ov5_021EB524,
    ov5_021EB560,
    ov5_021EB59C,
    ov5_021EB620,
    ov5_021EB69C,
    ov5_021EB7D0
};

void ov5_021EB7F8(MapObject *mapObj)
{
    UnkStruct_ov5_021EB7F8 *v0 = sub_02062ACC(mapObj, (sizeof(UnkStruct_ov5_021EB7F8)));

    v0->unk_00 = -1;
    v0->unk_02 = LCRNG_Next() % 16;

    ov5_021ECF04(mapObj, &v0->unk_04);

    if (v0->unk_04 != NULL) {
        sub_02062B68(mapObj);
    }
}

void ov5_021EB834(MapObject *mapObj)
{
    UnkStruct_ov5_021EB7F8 *v0 = sub_02062AF0(mapObj);
    ov5_021ECFA4(mapObj, &v0->unk_04);
}

void ov5_021EB848(MapObject *mapObj)
{
    UnkStruct_ov5_021EB7F8 *v0 = sub_02062AF0(mapObj);

    if (v0->unk_04 != NULL) {
        ov5_021ED01C(v0->unk_04, &v0->unk_08);
    }

    ov5_021ECFA4(mapObj, &v0->unk_04);
    MapObject_SetStatusFlagOn(mapObj, MAP_OBJ_STATUS_21);
}

void ov5_021EB874(MapObject *mapObj)
{
    int v0;
    UnkStruct_ov5_021EB7F8 *v1 = sub_02062AF0(mapObj);

    if (ov5_021EDD94(mapObj) == 1) {
        return;
    }

    if (v1->unk_04 == NULL) {
        ov5_021ECF04(mapObj, &v1->unk_04);
    }

    if (v1->unk_04 != NULL) {
        ov5_021ED03C(v1->unk_04, &v1->unk_08);
        ov5_021EDEB4(mapObj, v1->unk_04);
        MapObject_SetStatusFlagOff(mapObj, MAP_OBJ_STATUS_21);
    }
}

void ov5_021EB8B8(MapObject *mapObj)
{
    int v0, v1;
    UnkStruct_ov5_021EB7F8 *v2 = sub_02062AF0(mapObj);
    Billboard *v3 = v2->unk_04;

    v2 = sub_02062AF0(mapObj);

    if (ov5_021EDD94(mapObj) == 1) {
        return;
    }

    if (v3 == NULL) {
        return;
    }

    v0 = MapObject_GetFacingDir(mapObj);
    v1 = sub_02062A14(mapObj);

    if (v2->unk_02) {
        v2->unk_02--;
        Billboard_AdvanceAnim(v3, 0);
    } else {
        Billboard_AdvanceAnim(v3, (FX32_ONE));
        v2->unk_01++;

        if (v2->unk_01 >= 32) {
            v2->unk_01 = 0;
            v2->unk_02 = LCRNG_Next() % 16;
        }
    }

    v2->unk_00 = v0;
    v2->unk_03 = sub_02062A14(mapObj);

    ov5_021EDEB4(mapObj, v3);
    ov5_021EC6C0(mapObj, v3);
}

void ov5_021EB944(MapObject *mapObj)
{
    int v0, v1;
    UnkStruct_ov5_021EB2EC *v2 = sub_02062AF0(mapObj);
    Billboard *v3 = v2->unk_04;

    v2 = sub_02062AF0(mapObj);

    if (ov5_021EDD94(mapObj) == 1) {
        return;
    }

    if (v3 == NULL) {
        return;
    }

    v0 = MapObject_GetFacingDir(mapObj);
    v1 = sub_02062A14(mapObj);

    switch (v1) {
    case 0x0: {
        int v4;

        if ((v0 == 0) || (v0 == 1)) {
            v4 = 0;
        } else {
            v4 = 1;
        }

        if ((Billboard_GetAnimNum(v3) != v4) || (v2->unk_03 == 0x0)) {
            Billboard_SetAnimNum(v3, v4);
            Billboard_SetAnimFrameNum(v3, 0);
            Billboard_AdvanceAnim(v3, 0);
        }

        Billboard_AdvanceAnim(v3, 0);
    } break;
    default: {
        int v5;
        fx32 v6 = 0;

        if ((v0 == 0) || (v0 == 1)) {
            v5 = 0;
        } else {
            v5 = 1;
        }

        if (Billboard_GetAnimNum(v3) != v5) {
            Billboard_SetAnimNum(v3, v5);
            Billboard_SetAnimFrameNum(v3, 0);
        }

        Billboard_AdvanceAnim(v3, FX32_ONE );
    } break;
    }

    v2->unk_02 = v0;
    v2->unk_03 = sub_02062A14(mapObj);

    ov5_021EDEB4(mapObj, v3);
    ov5_021EC6C0(mapObj, v3);
}

void ov5_021EBA0C(MapObject *mapObj)
{
    UnkStruct_ov5_021EBA0C *v0 = sub_02062ACC(mapObj, (sizeof(UnkStruct_ov5_021EBA0C)));
    v0->unk_00 = -1;

    ov5_021ECF04(mapObj, &v0->unk_04);

    if (v0->unk_04 != NULL) {
        sub_02062B68(mapObj);
    }
}

void ov5_021EBA34(MapObject *mapObj)
{
    UnkStruct_ov5_021EBA0C *v0;
    VecFx32 v1 = { 0, 0, 0 };

    v0 = sub_02062AF0(mapObj);

    ov5_021ECFA4(mapObj, &v0->unk_04);
    MapObject_SetSpriteJumpOffset(mapObj, &v1);
}

void ov5_021EBA60(MapObject *mapObj)
{
    UnkStruct_ov5_021EBA0C *v0 = sub_02062AF0(mapObj);

    if (v0->unk_04 != NULL) {
        ov5_021ED01C(v0->unk_04, &v0->unk_08);
    }

    ov5_021ECFA4(mapObj, &v0->unk_04);
    MapObject_SetStatusFlagOn(mapObj, MAP_OBJ_STATUS_21);
}

void ov5_021EBA8C(MapObject *mapObj)
{
    int v0;
    UnkStruct_ov5_021EBA0C *v1 = sub_02062AF0(mapObj);

    if (ov5_021EDD94(mapObj) == 1) {
        return;
    }

    if (v1->unk_04 == NULL) {
        ov5_021ECF04(mapObj, &v1->unk_04);
    }

    if (v1->unk_04 != NULL) {
        ov5_021ED03C(v1->unk_04, &v1->unk_08);
        ov5_021EDEB4(mapObj, v1->unk_04);
        MapObject_SetStatusFlagOff(mapObj, MAP_OBJ_STATUS_21);
    }
}

void ov5_021EBAD0(MapObject *mapObj)
{
    int v0, v1;
    UnkStruct_ov5_021EBA0C *v2 = sub_02062AF0(mapObj);
    Billboard *v3 = v2->unk_04;

    v2 = sub_02062AF0(mapObj);

    if (ov5_021EDD94(mapObj) == 1) {
        return;
    }

    if (v3 == NULL) {
        return;
    }

    v0 = MapObject_GetFacingDir(mapObj);
    v1 = sub_02062A14(mapObj);

    GF_ASSERT(v1 < ((0x8 + 1) + 1));
    Unk_ov5_021FF3F8[v1](mapObj, v3, v2, v0);

    v2->unk_00 = v0;
    v2->unk_02 = sub_02062A14(mapObj);

    ov5_021EDEB4(mapObj, v3);
    ov5_021EC6C0(mapObj, v3);
}

static void ov5_021EBB44(MapObject *mapObj, Billboard *param1, UnkStruct_ov5_021EBA0C *param2, int param3)
{
    if (param3 != param2->unk_00) {
        int v0 = ov5_021EDF18(param3);

        Billboard_SetAnimNum(param1, v0);
        Billboard_SetAnimFrameNum(param1, 0);
        param2->unk_03_0 = 0;
    } else if (param2->unk_02 == (0x8 + 1)) {
        param2->unk_03_0 = 1;
    } else if (param2->unk_03_0 == 1) {
        ov5_021EC69C(param1, ov5_021EDF18(param3));
        param2->unk_03_0 = 0;
    } else {
        ov5_021EC668(param1);
    }
}

static void ov5_021EBBB4(MapObject *mapObj, Billboard *param1, UnkStruct_ov5_021EBA0C *param2, int param3)
{
    int v0;

    if (param3 != param2->unk_00) {
        v0 = ov5_021EDF18(param3);
        Billboard_SetAnimNum(param1, v0);
        Billboard_SetAnimFrameNum(param1, 0);
    } else if (param2->unk_02 == (0x8 + 1)) {
        param2->unk_03_0 = 1;
        return;
    } else if (param2->unk_03_0 == 1) {
        ov5_021EC69C(param1, ov5_021EDF18(param3));
        param2->unk_03_0 = 0;
    }

    if (ov5_021ECD38(mapObj) == 0) {
        Billboard_AdvanceAnim(param1, ((FX32_ONE) / 2));
    }
}

static void ov5_021EBC28(MapObject *mapObj, Billboard *param1, UnkStruct_ov5_021EBA0C *param2, int param3)
{
    int v0;

    if (param3 != param2->unk_00) {
        v0 = ov5_021EDF18(param3);
        Billboard_SetAnimNum(param1, v0);
        Billboard_SetAnimFrameNum(param1, 0);
    } else if (param2->unk_02 == (0x8 + 1)) {
        ov5_021EC69C(param1, ov5_021EDF18(param3));
    }

    if (ov5_021ECD38(mapObj) == 0) {
        Billboard_AdvanceAnim(param1, (FX32_ONE));
    }
}

static void ov5_021EBC7C(MapObject *mapObj, Billboard *param1, UnkStruct_ov5_021EBA0C *param2, int param3)
{
    if (param3 != param2->unk_00) {
        int v0 = ov5_021EDF18(param3);

        Billboard_SetAnimNum(param1, v0);
        Billboard_SetAnimFrameNum(param1, 0);
    } else if (param2->unk_02 == (0x8 + 1)) {
        ov5_021EC69C(param1, ov5_021EDF18(param3));
    }

    if (ov5_021ECD38(mapObj) == 0) {
        Billboard_AdvanceAnim(param1, ((FX32_ONE) * 2));
    }
}

static void ov5_021EBCD0(MapObject *mapObj, Billboard *param1, UnkStruct_ov5_021EBA0C *param2, int param3)
{
    if (param3 != param2->unk_00) {
        int v0 = ov5_021EDF18(param3);

        Billboard_SetAnimNum(param1, v0);
        Billboard_SetAnimFrameNum(param1, 0);
    } else if (param2->unk_02 == (0x8 + 1)) {
        ov5_021EC69C(param1, ov5_021EDF18(param3));
    }

    if (ov5_021ECD38(mapObj) == 0) {
        Billboard_AdvanceAnim(param1, ((FX32_ONE) * 4));
    }
}

static void ov5_021EBD24(MapObject *mapObj, Billboard *param1, UnkStruct_ov5_021EBA0C *param2, int param3)
{
    if (param3 != param2->unk_00) {
        int v0 = ov5_021EDF18(param3);

        Billboard_SetAnimNum(param1, v0);
        Billboard_SetAnimFrameNum(param1, 0);
        param2->unk_01 = 0;
    } else if (param2->unk_02 == (0x8 + 1)) {
        ov5_021EC69C(param1, ov5_021EDF18(param3));
    }

    if (param2->unk_02 != 0x6) {
        param2->unk_01 = 0;
    }

    if (ov5_021ECD38(mapObj) == 0) {
        fx32 v1[] = {
            (FX32_ONE),
            (FX32_ONE),
            ((FX32_ONE) * 2),
            (FX32_ONE),
            (FX32_ONE),
            ((FX32_ONE) * 2),
            0
        };

        Billboard_AdvanceAnim(param1, v1[param2->unk_01]);
        param2->unk_01++;

        if (v1[param2->unk_01] == 0) {
            param2->unk_01 = 0;
        }
    }
}

static void ov5_021EBDC4(MapObject *mapObj, Billboard *param1, UnkStruct_ov5_021EBA0C *param2, int param3)
{
    if (param3 != param2->unk_00) {
        int v0 = ov5_021EDF18(param3);

        Billboard_SetAnimNum(param1, v0);
        Billboard_SetAnimFrameNum(param1, 0);
        param2->unk_01 = 0;
    } else if (param2->unk_02 == (0x8 + 1)) {
        ov5_021EC69C(param1, ov5_021EDF18(param3));
    }

    if (param2->unk_02 != 0x7) {
        param2->unk_01 = 0;
    }

    if (ov5_021ECD38(mapObj) == 0) {
        fx32 v1[] = {
            ((FX32_ONE) * 3),
            ((FX32_ONE) * 2),
            ((FX32_ONE) * 3),
            0
        };

        Billboard_AdvanceAnim(param1, v1[param2->unk_01]);
        param2->unk_01++;

        if (v1[param2->unk_01] == 0) {
            param2->unk_01 = 0;
        }
    }
}

static void ov5_021EBE5C(MapObject *mapObj, Billboard *param1, UnkStruct_ov5_021EBA0C *param2, int param3)
{
    if (param3 != param2->unk_00) {
        int v0 = ov5_021EDF18(param3);

        Billboard_SetAnimNum(param1, v0);
        Billboard_SetAnimFrameNum(param1, 0);
        param2->unk_01 = 0;
    } else if (param2->unk_02 == (0x8 + 1)) {
        ov5_021EC69C(param1, ov5_021EDF18(param3));
    }

    if (param2->unk_02 != 0x8) {
        param2->unk_01 = 0;
    }

    if (ov5_021ECD38(mapObj) == 0) {
        fx32 v1[] = {
            ((FX32_ONE) * 2),
            (FX32_ONE),
            (FX32_ONE),
            (FX32_ONE),
            (FX32_ONE),
            (FX32_ONE),
            (FX32_ONE),
            0
        };

        Billboard_AdvanceAnim(param1, v1[param2->unk_01]);
        param2->unk_01++;

        if (v1[param2->unk_01] == 0) {
            param2->unk_01 = 0;
        }
    }
}

static void ov5_021EBEFC(MapObject *mapObj, Billboard *param1, UnkStruct_ov5_021EBA0C *param2, int param3)
{
    int v0;

    if (param3 != param2->unk_00) {
        v0 = ov5_021EDF24(param3);
        Billboard_SetAnimNum(param1, v0);
        Billboard_SetAnimFrameNum(param1, 0);
    } else if (param2->unk_02 != (0x8 + 1)) {
        ov5_021EC674(
            param1, ov5_021EDF24(param3), 4);
    }

    if (ov5_021ECD38(mapObj) == 0) {
        Billboard_AdvanceAnim(param1, (FX32_ONE));
    }
}

#ifdef SDK_BUILD_ARM
static
#endif
void (*const Unk_ov5_021FF3F8[])(
    MapObject *, Billboard *, UnkStruct_ov5_021EBA0C *, int)
    = {
          ov5_021EBB44,
          ov5_021EBBB4,
          ov5_021EBBB4,
          ov5_021EBC28,
          ov5_021EBC7C,
          ov5_021EBCD0,
          ov5_021EBD24,
          ov5_021EBDC4,
          ov5_021EBE5C,
          ov5_021EBEFC
      };

void ov5_021EBF50(MapObject *mapObj)
{
    int v0, v1;
    UnkStruct_ov5_021EBA0C *v2 = sub_02062AF0(mapObj);
    Billboard *v3 = v2->unk_04;

    v2 = sub_02062AF0(mapObj);

    if (ov5_021EDD94(mapObj) == 1) {
        return;
    }

    if (v3 == NULL) {
        return;
    }

    v0 = MapObject_GetFacingDir(mapObj);
    v1 = sub_02062A14(mapObj);

    GF_ASSERT(v1 < (((0x8 + 1) + 1) + 1));

    if ((v2->unk_02 == 0x0) && (v1 != v2->unk_02) && (v2->unk_03_0 == 0)) {
        v2->unk_00 = -1;
    }

    Unk_ov5_021FF448[v1](mapObj, v3, v2, v0);

    v2->unk_00 = v0;
    v2->unk_02 = v1;

    ov5_021EDEB4(mapObj, v3);
    ov5_021EC6C0(mapObj, v3);
}

static void ov5_021EBFDC(MapObject *mapObj, Billboard *param1, UnkStruct_ov5_021EBA0C *param2, int param3)
{
    int v0[4] = { 6, 7, 8, 9 };
    int v1 = v0[param3];

    if (param3 != param2->unk_00) {
        Billboard_SetAnimNum(param1, v1);
        Billboard_SetAnimFrameNum(param1, 0);
        param2->unk_03_0 = 0;
    } else if (param2->unk_02 != 0x0) {
        param2->unk_03_0 = 1;
    } else if (param2->unk_03_0 == 1) {
        Billboard_SetAnimNum(param1, v1);
        Billboard_SetAnimFrameNum(param1, 0);
        param2->unk_03_0 = 0;
    } else {
        Billboard_AdvanceAnim(param1, (FX32_ONE));
    }
}

static void ov5_021EC068(MapObject *mapObj, Billboard *param1, UnkStruct_ov5_021EBA0C *param2, int param3)
{
    int v0 = ov5_021EDF30(param3);

    if ((param3 != param2->unk_00) || (param2->unk_02 != 0x0)) {
        Billboard_SetAnimNum(param1, v0);
        Billboard_SetAnimFrameNum(param1, 0);
    } else if (param2->unk_02 != (0x8 + 1)) {
        Billboard_SetAnimNum(param1, v0);
        Billboard_SetAnimFrameNum(param1, 0);
    } else if (Billboard_GetAnimNum(param1) != v0) {
        Billboard_SetAnimNum(param1, v0);
        Billboard_SetAnimFrameNum(param1, 0);
    }

    if (ov5_021ECD38(mapObj) == 0) {
        Billboard_AdvanceAnim(param1, ((FX32_ONE) / 2));
    }
}

static void ov5_021EC0E4(MapObject *mapObj, Billboard *param1, UnkStruct_ov5_021EBA0C *param2, int param3)
{
    int v0 = ov5_021EDF30(param3);

    if (param3 != param2->unk_00) {
        Billboard_SetAnimNum(param1, v0);
        Billboard_SetAnimFrameNum(param1, 0);
    } else if (param2->unk_02 != ((0x8 + 1) + 1)) {
        Billboard_SetAnimNum(param1, v0);
        Billboard_SetAnimFrameNum(param1, 0);
    } else if (Billboard_GetAnimNum(param1) != v0) {
        Billboard_SetAnimNum(param1, v0);
        Billboard_SetAnimFrameNum(param1, 0);
    }

    if (ov5_021ECD38(mapObj) == 0) {
        Billboard_AdvanceAnim(param1, (FX32_ONE));
    }
}

static void (*const Unk_ov5_021FF448[11])(
    MapObject *, Billboard *, UnkStruct_ov5_021EBA0C *, int)
    = {
          ov5_021EBFDC,
          ov5_021EBBB4,
          ov5_021EBBB4,
          ov5_021EBC28,
          ov5_021EBC7C,
          ov5_021EBCD0,
          ov5_021EBD24,
          ov5_021EBDC4,
          ov5_021EBE5C,
          ov5_021EC068,
          ov5_021EC0E4
      };

void (*const Unk_ov5_021FF31C[])(MapObject *, Billboard *, UnkStruct_ov5_021EBA0C *, int);

static const int Unk_ov5_021FF34C[] = {
    0x0,
    0x1,
    0x2,
    0x3
};

void ov5_021EC15C(MapObject *mapObj)
{
    int v0, v1;
    UnkStruct_ov5_021EBA0C *v2 = sub_02062AF0(mapObj);
    Billboard *v3 = v2->unk_04;

    v2 = sub_02062AF0(mapObj);

    if ((v3 == NULL) || (ov5_021EDD94(mapObj) == 1)) {
        return;
    }

    v0 = MapObject_GetFacingDir(mapObj);
    v1 = sub_02062A14(mapObj);

    GF_ASSERT(v1 < (0x3 + 1));

    Unk_ov5_021FF31C[v1](mapObj, v3, v2, v0);
    ov5_021EC374(mapObj, v3, v0);

    v2->unk_00 = v0;
    v2->unk_02 = sub_02062A14(mapObj);

    ov5_021EDEB4(mapObj, v3);
    ov5_021EC6C0(mapObj, v3);
}

static void ov5_021EC1D8(MapObject *mapObj, Billboard *param1, UnkStruct_ov5_021EBA0C *param2, int param3)
{
    fx32 v0;

    if ((param3 != param2->unk_00) || (param2->unk_02 != 0x0)) {
        Billboard_SetAnimNum(param1, Unk_ov5_021FF34C[param3]);
        Billboard_SetAnimFrameNum(param1, (FX32_ONE * 15));
    }

    v0 = Billboard_GetAnimFrameNum(param1);
    v0 -= (FX32_ONE);

    if (v0 < 0) {
        v0 = 0;
    }

    Billboard_SetAnimFrameNum(param1, v0);
    Billboard_AdvanceAnim(param1, 0);
}

static void ov5_021EC228(MapObject *mapObj, Billboard *param1, UnkStruct_ov5_021EBA0C *param2, int param3)
{
    fx32 v0;

    if ((param3 != param2->unk_00) || (param2->unk_02 != 0x1)) {
        Billboard_SetAnimNum(param1, Unk_ov5_021FF34C[param3]);
        Billboard_SetAnimFrameNum(param1, 0);
    }

    Billboard_AdvanceAnim(param1, (FX32_ONE));
}

static void ov5_021EC260(MapObject *mapObj, Billboard *param1, UnkStruct_ov5_021EBA0C *param2, int param3)
{
    fx32 v0;

    if ((param3 != param2->unk_00) || (param2->unk_02 != 0x2)) {
        Billboard_SetAnimNum(param1, Unk_ov5_021FF34C[param3]);
        Billboard_SetAnimFrameNum(param1, (FX32_ONE * 15));
        param2->unk_03_1 = 0;
    }

    v0 = Billboard_GetAnimFrameNum(param1);

    switch (param2->unk_03_1) {
    case 0:
        v0 -= (FX32_ONE);

        if (v0 <= (FX32_ONE * 10)) {
            param2->unk_03_1++;
        }
        break;
    case 1:
        v0 += (FX32_ONE);

        if (v0 >= (FX32_ONE * 15)) {
            param2->unk_03_1++;
            param2->unk_01 = 0;
        }
        break;
    case 2:
        param2->unk_01++;

        if (param2->unk_01 >= 30) {
            param2->unk_01 = 0;
            param2->unk_03_1 = 0;
        }
    }

    Billboard_SetAnimFrameNum(param1, v0);
    Billboard_AdvanceAnim(param1, 0);
}

static void ov5_021EC324(MapObject *mapObj, Billboard *param1, UnkStruct_ov5_021EBA0C *param2, int param3)
{
    fx32 v0;

    if ((param3 != param2->unk_00) || (param2->unk_02 != 0x3)) {
        Billboard_SetAnimNum(param1, Unk_ov5_021FF34C[param3]);
        Billboard_SetAnimFrameNum(param1, (FX32_ONE * 15));
    }

    v0 = Billboard_GetAnimFrameNum(param1) - (FX32_ONE);

    if (v0 < 0) {
        v0 = 0;
    }

    Billboard_SetAnimFrameNum(param1, v0);
    Billboard_AdvanceAnim(param1, 0);
}

static void ov5_021EC374(MapObject *mapObj, Billboard *param1, int param2)
{
    VecFx32 v0 = { 0, 0, 0 };
    int v1 = (Billboard_GetFrameNum(param1) / FX32_ONE);

    switch (param2) {
    case 0:
        break;
    case 1:
        if (v1 >= 24) {
            v0.z = (FX32_ONE * 6);
        }
        break;
    case 2:
        if (v1 < 40) {
            v0.x = (FX32_ONE * 1);
        } else {
            v0.x = (FX32_ONE * -5);
        }
        break;
    case 3:
        if (v1 < 56) {
            v0.x = (FX32_ONE * -1);
        } else {
            v0.x = (FX32_ONE * 5);
        }
        break;
    }

    MapObject_SetSpriteJumpOffset(mapObj, &v0);
}

#ifdef SDK_BUILD_ARM
static
#endif
void (*const Unk_ov5_021FF31C[])(MapObject *, Billboard *, UnkStruct_ov5_021EBA0C *, int) = {
    ov5_021EC1D8,
    ov5_021EC228,
    ov5_021EC260,
    ov5_021EC324
};

void ov5_021EC3F0(MapObject *mapObj)
{
    int v0;
    UnkStruct_ov5_021EBA0C *v1 = sub_02062AF0(mapObj);
    Billboard *v2 = v1->unk_04;

    v1 = sub_02062AF0(mapObj);

    if (ov5_021EDD94(mapObj) == 1) {
        return;
    }

    if (v2 == NULL) {
        return;
    }

    v0 = MapObject_GetFacingDir(mapObj);

    v1->unk_00 = v0;
    v1->unk_02 = sub_02062A14(mapObj);

    {
        int v3 = ov5_021EDF18(v0);

        Billboard_SetAnimNum(v2, v3);
        Billboard_SetAnimFrameNum(v2, 0);
        Billboard_AdvanceAnim(v2, 0);
    }

    ov5_021EDEB4(mapObj, v2);
    ov5_021EC6C0(mapObj, v2);
}

void ov5_021EC454(MapObject *mapObj)
{
    int v0;
    UnkStruct_ov5_021EBA0C *v1 = sub_02062AF0(mapObj);
    Billboard *v2 = v1->unk_04;

    v1 = sub_02062AF0(mapObj);

    if (ov5_021EDD94(mapObj) == 1) {
        return;
    }

    if (v2 == NULL) {
        return;
    }

    v0 = MapObject_GetFacingDir(mapObj);

    v1->unk_00 = v0;
    v1->unk_02 = sub_02062A14(mapObj);

    if (Billboard_GetAnimNum(v2) != 0) {
        Billboard_SetAnimNum(v2, 0);
        Billboard_SetAnimFrameNum(v2, 0);
    }

    Billboard_AdvanceAnim(v2, (FX32_ONE));

    ov5_021EDEB4(mapObj, v2);
    ov5_021EC6C0(mapObj, v2);
}

void ov5_021EC4BC(MapObject *mapObj)
{
    int v0, v1;
    UnkStruct_ov5_021EBA0C *v2 = sub_02062AF0(mapObj);
    Billboard *v3 = v2->unk_04;

    v2 = sub_02062AF0(mapObj);

    if (ov5_021EDD94(mapObj) == 1) {
        return;
    }

    if (v3 == NULL) {
        return;
    }

    v0 = MapObject_GetFacingDir(mapObj);
    v1 = sub_02062A14(mapObj);

    switch (v1) {
    case 0x0:
        if (Billboard_GetAnimNum(v3) != 1) {
            Billboard_SetAnimNum(v3, 1);
            Billboard_SetAnimFrameNum(v3, 0);
        }

        Billboard_AdvanceAnim(v3, (FX32_ONE));
        break;
    default:
        if (Billboard_GetAnimNum(v3) != 0) {
            Billboard_SetAnimNum(v3, 0);
            Billboard_SetAnimFrameNum(v3, 0);
        }

        Billboard_AdvanceAnim(v3, (FX32_ONE));
    }

    v2->unk_00 = v0;
    v2->unk_02 = sub_02062A14(mapObj);

    ov5_021EDEB4(mapObj, v3);
    ov5_021EC6C0(mapObj, v3);
}

void ov5_021EC554(MapObject *mapObj)
{
    int v0, v1;
    UnkStruct_ov5_021EBA0C *v2 = sub_02062AF0(mapObj);
    Billboard *v3 = v2->unk_04;

    v2 = sub_02062AF0(mapObj);

    if (ov5_021EDD94(mapObj) == 1) {
        return;
    }

    if (v3 == NULL) {
        return;
    }

    v0 = MapObject_GetFacingDir(mapObj);

    if (v0 != v2->unk_00) {
        v1 = ov5_021EDF18(v0);
        Billboard_SetAnimNum(v3, v1);
        Billboard_SetAnimFrameNum(v3, 0);
    }

    Billboard_AdvanceAnim(v3, (FX32_ONE));
    ov5_021EDEB4(mapObj, v3);
    ov5_021EC6C0(mapObj, v3);

    v2->unk_00 = v0;
    v2->unk_02 = sub_02062A14(mapObj);
}

void ov5_021EC5C0(MapObject *mapObj)
{
    int v0, v1;
    UnkStruct_ov5_021EBA0C *v2 = sub_02062AF0(mapObj);
    Billboard *v3 = v2->unk_04;

    v2 = sub_02062AF0(mapObj);

    if (ov5_021EDD94(mapObj) == 1) {
        return;
    }

    if (v3 == NULL) {
        return;
    }

    v0 = MapObject_GetFacingDir(mapObj);
    v1 = sub_02062A14(mapObj);

    if (v1 != v2->unk_02) {
        if (v1 == 0x0) {
            Billboard_SetAnimNum(v3, 0);
        } else {
            Billboard_SetAnimNum(v3, 1);
        }

        Billboard_SetAnimFrameNum(v3, 0);
    }

    Billboard_AdvanceAnim(v3, (FX32_ONE));

    v2->unk_00 = v0;
    v2->unk_02 = v1;

    ov5_021EDEB4(mapObj, v3);
    ov5_021EC6C0(mapObj, v3);
}

static void ov5_021EC638(Billboard *param0, int param1)
{
    fx32 v0, v1;

    v0 = Billboard_GetAnimFrameNum(param0);
    v0 /= FX32_ONE;
    v1 = v0 % param1;
    v0 -= v1;
    v0 *= FX32_ONE;

    Billboard_SetAnimFrameNum(param0, v0);
    Billboard_AdvanceAnim(param0, 0);
}

static void ov5_021EC668(Billboard *param0)
{
    ov5_021EC638(param0, 8);
}

static void ov5_021EC674(Billboard *param0, int param1, int param2)
{
    fx32 v0;

    ov5_021EC638(param0, param2);
    v0 = Billboard_GetAnimFrameNum(param0);

    Billboard_SetAnimNum(param0, param1);
    Billboard_SetAnimFrameNum(param0, v0);
}

static void ov5_021EC69C(Billboard *param0, int param1)
{
    fx32 v0;

    ov5_021EC668(param0);
    v0 = Billboard_GetAnimFrameNum(param0);

    Billboard_SetAnimNum(param0, param1);
    Billboard_SetAnimFrameNum(param0, v0);
}

static void ov5_021EC6C0(MapObject *mapObj, Billboard *param1)
{
    int v0 = 1;

    if (MapObject_CheckStatusFlag(mapObj, MAP_OBJ_STATUS_HIDE) == 1) {
        v0 = 0;
    }

    if (MapObject_CheckStatusFlag(mapObj, MAP_OBJ_STATUS_12) == 1) {
        if (!MapObject_CheckStatusFlag(mapObj, MAP_OBJ_STATUS_13)) {
            v0 = 0;
        }
    }

    Billboard_SetDrawFlag(param1, v0);
}

static void ov5_021EC700(u32 param0, VecFx32 *param1)
{
    const ObjectEventGfxFieldAnimsInitialPosEntry *v0 = gObjectEventGfxOverworldAnimsInitialPosTable;

    while (v0->graphicsID != 0xffff) {
        if (v0->graphicsID == param0) {
            *param1 = v0->initialPos;
            return;
        }

        v0++;
    }

    GF_ASSERT(FALSE);
}

void ov5_021EC734(MapObject *mapObj)
{
    VecFx32 v0;
    UnkStruct_ov5_021EC760 *v1 = sub_02062ACC(mapObj, (sizeof(UnkStruct_ov5_021EC760)));
    ov5_021EC700(MapObject_GetEffectiveGraphicsID(mapObj), &v0);
    v1->unk_00 = ov5_021F121C(mapObj, &v0);
}

void ov5_021EC75C(MapObject *mapObj)
{
    return;
}

void ov5_021EC760(MapObject *mapObj)
{
    UnkStruct_ov5_021EC760 *v0 = sub_02062AF0(mapObj);

    if (v0->unk_00 != NULL) {
        FieldEffectManager_FinishAnimManager(v0->unk_00);
        v0->unk_00 = NULL;
    }
}

void ov5_021EC778(MapObject *mapObj)
{
    UnkStruct_ov5_021EC760 *v0 = sub_02062AF0(mapObj);

    if (v0->unk_00 != NULL) {
        FieldEffectManager_FinishAnimManager(v0->unk_00);
        v0->unk_00 = NULL;
    }
}

void ov5_021EC790(MapObject *mapObj)
{
    VecFx32 v0;
    UnkStruct_ov5_021EC760 *v1 = sub_02062AF0(mapObj);
    ov5_021EC700(MapObject_GetEffectiveGraphicsID(mapObj), &v0);
    v1->unk_00 = ov5_021F121C(mapObj, &v0);
}

void ov5_021EC7B8(MapObject *mapObj)
{
    UnkStruct_ov5_021EC760 *v0 = sub_02062ACC(mapObj, (sizeof(UnkStruct_ov5_021EC760)));
    v0->unk_00 = ov5_021F2438(mapObj);
}

void ov5_021EC7D0(MapObject *mapObj)
{
    UnkStruct_ov5_021EC760 *v0 = sub_02062AF0(mapObj);
    GF_ASSERT(v0->unk_00 == NULL);
    v0->unk_00 = ov5_021F2438(mapObj);
}

void ov5_021EC7F0(MapObject *mapObj)
{
    UnkStruct_ov5_021EC760 *v0 = sub_02062AF0(mapObj);
    ov5_021F247C(v0->unk_00);
    v0->unk_00 = NULL;
}

static void ov5_021EC804(Billboard *param0, void *param1)
{
    UnkStruct_ov5_021EC804 *v0 = param1;
    NNSG3dResMdl *v1 = Billboard_GetModel(param0);

    NNS_G3dMdlUseMdlAlpha(v1);
    NNS_G3dMdlSetMdlAlphaAll(v1, v0->unk_10);
}

void ov5_021EC824(MapObject *mapObj)
{
    UnkStruct_ov5_021EC804 *v0 = sub_02062ACC(mapObj, sizeof(UnkStruct_ov5_021EC804));
    v0->unk_02 = -1;

    ov5_021ECF04(mapObj, &v0->unk_04);

    if (v0->unk_04 != NULL) {
        Billboard_SetCallback(v0->unk_04, ov5_021EC804, v0);
        sub_02062B68(mapObj);
    }
}

void ov5_021EC858(MapObject *mapObj)
{
    UnkStruct_ov5_021EC804 *v0 = sub_02062AF0(mapObj);
    ov5_021ECFA4(mapObj, &v0->unk_04);
}

void ov5_021EC86C(MapObject *mapObj)
{
    UnkStruct_ov5_021EC804 *v0 = sub_02062AF0(mapObj);

    if (v0->unk_04 != NULL) {
        ov5_021ED01C(v0->unk_04, &v0->unk_08);
    }

    ov5_021ECFA4(mapObj, &v0->unk_04);
    MapObject_SetStatusFlagOn(mapObj, MAP_OBJ_STATUS_21);
}

void ov5_021EC898(MapObject *mapObj)
{
    int v0;
    UnkStruct_ov5_021EB2EC *v1 = sub_02062AF0(mapObj);

    if (ov5_021EDD94(mapObj) == 1) {
        return;
    }

    if (v1->unk_04 == NULL) {
        ov5_021ECF04(mapObj, &v1->unk_04);
    }

    if (v1->unk_04 != NULL) {
        ov5_021ED03C(v1->unk_04, &v1->unk_08);
        ov5_021EDEB4(mapObj, v1->unk_04);
        MapObject_SetStatusFlagOff(mapObj, MAP_OBJ_STATUS_21);
        Billboard_SetCallback(v1->unk_04, ov5_021EC804, v1);
    }
}

void ov5_021EC8EC(MapObject *mapObj)
{
    int v0, v1;
    UnkStruct_ov5_021EC804 *v2;
    Billboard *v3;

    v2 = sub_02062AF0(mapObj);

    if (ov5_021EDD94(mapObj) == 1) {
        return;
    }

    v3 = v2->unk_04;

    if (v3 == NULL) {
        return;
    }

    if (v2->unk_10 < 31) {
        v2->unk_10++;
    }

    if (ov5_021ECD38(mapObj) == 0) {
        Billboard_AdvanceAnim(v3, (FX32_ONE));
    }

    ov5_021EDEB4(mapObj, v3);
    ov5_021EC6C0(mapObj, v3);
}

void ov5_021EC938(MapObject *mapObj)
{
    UnkStruct_ov5_021EC938 *v0 = sub_02062ACC(mapObj, (sizeof(UnkStruct_ov5_021EC938)));
    ov5_021ECF04(mapObj, &v0->unk_08);

    if (v0->unk_08 != NULL) {
        sub_02062B68(mapObj);
    }
}

void ov5_021EC95C(MapObject *mapObj)
{
    UnkStruct_ov5_021EC938 *v0 = sub_02062AF0(mapObj);
    ov5_021ECFA4(mapObj, &v0->unk_08);
}

void ov5_021EC970(MapObject *mapObj)
{
    UnkStruct_ov5_021EC938 *v0 = sub_02062AF0(mapObj);

    if (v0->unk_08 != NULL) {
        ov5_021ED01C(v0->unk_08, &v0->unk_0C);
    }

    ov5_021ECFA4(mapObj, &v0->unk_08);
    MapObject_SetStatusFlagOn(mapObj, MAP_OBJ_STATUS_21);
}

void ov5_021EC9A0(MapObject *mapObj)
{
    int v0;
    UnkStruct_ov5_021EC938 *v1 = sub_02062AF0(mapObj);

    if (ov5_021EDD94(mapObj) == 1) {
        return;
    }

    if (v1->unk_08 == NULL) {
        ov5_021ECF04(mapObj, &v1->unk_08);
    }

    if (v1->unk_08 != NULL) {
        ov5_021ED03C(v1->unk_08, &v1->unk_0C);
        ov5_021EDEB4(mapObj, v1->unk_08);
        MapObject_SetStatusFlagOff(mapObj, MAP_OBJ_STATUS_21);
    }
}

void ov5_021EC9E8(MapObject *mapObj)
{
    int v0, v1;
    UnkStruct_ov5_021EC938 *v2;
    Billboard *v3;

    v2 = sub_02062AF0(mapObj);

    if (ov5_021EDD94(mapObj) == 1) {
        return;
    }

    v3 = v2->unk_08;

    if (v3 == NULL) {
        return;
    }

    if (ov5_021ECD38(mapObj) == 0) {
        Billboard_AdvanceAnim(v3, (FX32_ONE));
    }

    {
        VecFx32 v4;
        fx32 v5[16] = {
            0x0,
            0x800,
            0x1000,
            0x2000,
            0x4000,
            0x5800,
            0x7000,
            0x8000,
            0x7800,
            0x6800,
            0x5000,
            0x3000,
            0x2000,
            0x1000,
            0x800,
            0x0
        };

        MapObject_GetSpriteJumpOffset(mapObj, &v4);

        if (v4.y == v2->unk_04) {
            v4.y = v5[v2->unk_00 >> 1];
            v2->unk_00 = (v2->unk_00 + 1) & 0x1f;
            v2->unk_04 = v4.y;

            MapObject_SetSpriteJumpOffset(mapObj, &v4);
        }
    }

    ov5_021EDEB4(mapObj, v3);
    ov5_021EC6C0(mapObj, v3);
}
