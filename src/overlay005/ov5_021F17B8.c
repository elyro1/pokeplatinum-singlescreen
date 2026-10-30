#include "overlay005/ov5_021F17B8.h"

#include <nitro.h>
#include <string.h>

#include "constants/field/field_effect_renderer.h"

#include "struct_decls/map_object.h"
#include "struct_decls/map_object_manager.h"

#include "field/field_system.h"
#include "overlay005/field_effect_manager.h"
#include "overlay005/ov5_021EB1A0.h"
#include "overlay005/ov5_021ECE40.h"
#include "overlay005/surf_mount_renderer.h"

#include "berry_patch_graphics.h"
#include "billboard.h"
#include "map_object.h"
#include "map_object_move.h"
#include "map_tile_behavior.h"
#include "overworld_anim_manager.h"
#include "overworld_pokemon_assets.h"
#include "terrain_collision_manager.h"

#ifdef SDK_PORT
#include "port/sim_config_prj.h"
#endif

typedef struct {
    FieldEffectManager *unk_00;
} UnkStruct_ov5_021F17E4;

typedef struct {
    FieldSystem *fieldSystem;
    FieldEffectManager *unk_04;
    UnkStruct_ov5_021F17E4 *unk_08;
    MapObject *unk_0C;
} UnkStruct_ov5_021F1800;

typedef struct {
    int unk_00;
    int unk_04;
    int unk_08;
    int unk_0C;
    int unk_10;
    UnkStruct_ov5_021F1800 unk_14;
    Billboard *unk_24;
    VecFx32 unk_28;
    fx32 unk_34;
} UnkStruct_ov5_021F1A24;

typedef struct {
    FieldSystem *fieldSystem;
    FieldEffectManager *unk_04;
    UnkStruct_ov5_021F17E4 *unk_08;
    BillboardResources unk_0C;
    const Billboard *unk_34;
} UnkStruct_ov5_021F1BEC;

typedef struct {
    int unk_00;
    UnkStruct_ov5_021F1BEC unk_04;
    Billboard *unk_3C;
    VecFx32 unk_40;
    fx32 unk_4C;
    VecFx32 unk_50;
} UnkStruct_ov5_021F1AD8;

static void ov5_021F17E4(UnkStruct_ov5_021F17E4 *param0);
static void ov5_021F17F4(UnkStruct_ov5_021F17E4 *param0);
static void ov5_021F1A24(UnkStruct_ov5_021F1A24 *param0, MapObject *param1, VecFx32 *param2);
static void ov5_021F1A8C(OverworldAnimManager *param0, UnkStruct_ov5_021F1A24 *param1);

static const OverworldAnimManagerFuncs Unk_ov5_02200338;

Billboard *PokemonReflection_New(FieldEffectManager *manager, const BillboardResources *resources, const VecFx32 *position)
{
    if (!FieldEffectManager_HasRenderer(manager, FIELD_EFFECT_RENDERER_01)) {
        return NULL;
    }
    BillboardResources reflected = *resources;
    reflected.modelRes = ov5_021DF9FC(manager, 2);
    Billboard *billboard = FieldEffectManager_TryAddBillboard(manager, &reflected, position);
    if (billboard != NULL) {
        // The template embeds a 32x32 placeholder texture. Use the source
        // frame's dimensions as well as its address when drawing 64x64 art.
        billboard->matchTextureDimensions = TRUE;
        Billboard_SetDrawFlag(billboard, FALSE);
    }
    return billboard;
}

void PokemonReflection_Update(Billboard *reflection, const Billboard *source, const VecFx32 *position, BOOL large)
{
    if (reflection == NULL) {
        return;
    }
    Billboard_SetDrawFlag(reflection, source != NULL && Billboard_GetDrawFlag(source));
    if (source == NULL) {
        return;
    }
    Billboard_SetPos(reflection, position);
    Billboard_SetAnimNum(reflection, Billboard_GetAnimNum(source));
    Billboard_SetFrameNum(reflection, Billboard_GetFrameNum(source));
    VecFx32 scale = *Billboard_GetScale(source);
    if (large) {
        // The original reflection quad is 32x32; imported frames may be 64x64.
        scale.x *= 2;
        scale.y *= 2;
        scale.z *= 2;
    }
    Billboard_SetScale(reflection, &scale);
}

BOOL PokemonReflection_IsSurface(u8 tile)
{
    return (TileBehavior_IsSurfable(tile) || TileBehavior_HasReflectiveSurface(tile))
        && !TileBehavior_IsBridgeOverWater(tile) && !TileBehavior_IsWaterfall(tile);
}

static void ReflectionClipDepth(const VecFx32 *position, const MtxFx43 *view, const MtxFx44 *projection, fx32 *z, fx32 *w)
{
    VecFx32 camera;
    camera.x = FX_Mul(position->x, view->_00) + FX_Mul(position->y, view->_10) + FX_Mul(position->z, view->_20) + view->_30;
    camera.y = FX_Mul(position->x, view->_01) + FX_Mul(position->y, view->_11) + FX_Mul(position->z, view->_21) + view->_31;
    camera.z = FX_Mul(position->x, view->_02) + FX_Mul(position->y, view->_12) + FX_Mul(position->z, view->_22) + view->_32;
    *z = FX_Mul(camera.x, projection->_02) + FX_Mul(camera.y, projection->_12) + FX_Mul(camera.z, projection->_22) + projection->_32;
    *w = FX_Mul(camera.x, projection->_03) + FX_Mul(camera.y, projection->_13) + FX_Mul(camera.z, projection->_23) + projection->_33;
}

void PokemonReflection_SetRiderLayer(Billboard *rider, Billboard *mount, const MtxFx43 *view, const MtxFx44 *projection)
{
    if (rider == NULL) {
        return;
    }
    rider->depthOffset = 0;
    if (mount != NULL && Billboard_GetDrawFlag(mount)) {
        // Match the mount's projected depth with a small foreground margin.
        // A fixed world-space bias can pull the rider above the water overlay,
        // especially with perspective. Preserve its X/Y, W and mirrored pose.
        fx32 riderZ, riderW, mountZ, mountW;
        ReflectionClipDepth(&rider->pos, view, projection, &riderZ, &riderW);
        ReflectionClipDepth(&mount->pos, view, projection, &mountZ, &mountW);
        if (riderW > 0 && mountW > 0 && projection->_22 != 0) {
            fx32 margin = riderW / 65536;
            if (margin < 1) {
                margin = 1;
            }
            fx32 clipDelta = (fx64)mountZ * riderW / mountW - riderZ - margin;
            fx32 offset = FX_Div(clipDelta, projection->_22);
            if (offset > 0) {
                rider->depthOffset = offset;
            }
        }
    }
}

static void UpdateSurfRiderReflectionDepth(Billboard *rider, void *context)
{
    FieldSystem *field = context;
    // Field effects may update their poses in either order. Resolve depth at
    // draw time, after the mount's current movement/bob position is available.
    PokemonReflection_SetRiderLayer(rider, SurfMountRenderer_GetReflection(field),
        NNS_G3dGlbGetCameraMtx(), NNS_G3dGlbGetProjectionMtx());
}

BOOL PokemonReflection_GetWaterPosition(FieldSystem *fieldSystem, const Billboard *source, const VecFx32 *anchor, VecFx32 *position)
{
    if (!Billboard_GetDrawFlag(source) || anchor->x < 0 || anchor->z < 0) {
        return FALSE;
    }
    u8 tile = TerrainCollisionManager_GetTileBehavior(fieldSystem,
        anchor->x / MAP_OBJECT_TILE_SIZE, anchor->z / MAP_OBJECT_TILE_SIZE);
    if (!PokemonReflection_IsSurface(tile)) {
        return FALSE;
    }
    *position = *anchor;
    if (!MapObject_RecalculatePositionHeight(fieldSystem, position)) {
        return FALSE;
    }
    fx32 elevation = Billboard_GetPos(source)->y - position->y;
    // Mount sprites sit at the waterline; bank-side walking reflections use
    // the original lower anchor to project down onto the next tile.
    position->y -= 6 * FX32_ONE + elevation;
    position->z -= 7 * FX32_ONE;
    return TRUE;
}

void *ov5_021F17B8(FieldEffectManager *param0)
{
    UnkStruct_ov5_021F17E4 *v0 = FieldEffectManager_HeapAllocInit(param0, sizeof(UnkStruct_ov5_021F17E4), 0, 0);
    v0->unk_00 = param0;

    ov5_021F17E4(v0);
    return v0;
}

void ov5_021F17D4(void *param0)
{
    UnkStruct_ov5_021F17E4 *v0 = param0;

    ov5_021F17F4(v0);
    FieldEffectManager_HeapFree(v0);
}

static void ov5_021F17E4(UnkStruct_ov5_021F17E4 *param0)
{
    ov5_021DF9E0(param0->unk_00, 2, 20);
    // The original quad's UVs cover 32x32 texels. Enable the material's
    // identity texture matrix so binding a 64x64 frame applies magW/magH.
    // This is also identity for Platinum's original 32x32 reflections.
    void *resource = ov5_021DF9FC(param0->unk_00, 2);
    NNSG3dResMdl *model = NNS_G3dGetMdlByIdx(NNS_G3dGetMdlSet(resource), 0);
    NNSG3dResMat *materials = NNS_G3dGetMat(model);
    for (int i = 0; i < model->info.numMat; i++) {
        NNSG3dResMatData *material = NNS_G3dGetMatDataByIdx(materials, i);
        if (!(material->flag & NNS_G3D_MATFLAG_TEXMTX_USE)) {
            material->flag |= NNS_G3D_MATFLAG_TEXMTX_USE | NNS_G3D_MATFLAG_TEXMTX_SCALEONE
                | NNS_G3D_MATFLAG_TEXMTX_ROTZERO | NNS_G3D_MATFLAG_TEXMTX_TRANSZERO;
            material->texImageParam = (material->texImageParam & ~REG_G3_TEXIMAGE_PARAM_TGEN_MASK)
                | (GX_TEXGEN_TEXCOORD << REG_G3_TEXIMAGE_PARAM_TGEN_SHIFT);
        }
    }
}

static void ov5_021F17F4(UnkStruct_ov5_021F17E4 *param0)
{
    ov5_021DFA08(param0->unk_00, 2);
}

void ov5_021F1800(MapObject *param0, int param1)
{
    int v0;
    VecFx32 v1;
    UnkStruct_ov5_021F1800 v2;
    OverworldAnimManager *v3;

    v2.fieldSystem = MapObject_FieldSystem(param0);
    v2.unk_04 = MapObject_GetFieldEffectManager(param0);
    v2.unk_08 = FieldEffectManager_GetRendererContext(v2.unk_04, 1);
    v2.unk_0C = param0;

    MapObject_GetPosPtr(param0, &v1);

    v0 = MapObject_CalculateTaskPriority(param0, 2);
    v3 = FieldEffectManager_InitAnimManager(v2.unk_04, &Unk_ov5_02200338, &v1, param1, &v2, v0);
}

static int ov5_021F184C(OverworldAnimManager *param0, void *param1)
{
    VecFx32 v0;
    UnkStruct_ov5_021F1A24 *v1;
    const UnkStruct_ov5_021F1800 *v2;

    v1 = param1;
    v2 = OverworldAnimManager_GetUserData(param0);

    v1->unk_14 = *v2;
    v1->unk_10 = OverworldAnimManager_GetUserInt(param0);
    v1->unk_00 = MapObject_GetLocalID(v1->unk_14.unk_0C);
#ifdef SDK_PORT
    // Neighbor objects retain their event owner when they become scripted on
    // entry to that route. The staging map changes, but the reflection survives.
    v1->unk_04 = MapObject_GetEventOwner(v1->unk_14.unk_0C);
#else
    v1->unk_04 = MapObject_GetMapHeaderID(v1->unk_14.unk_0C);
#endif
    v1->unk_08 = MapObject_GetGraphicsID(v1->unk_14.unk_0C);

    if (BerryPatchGraphics_IsBerryPatch(v1->unk_08) == 1) {
        v1->unk_08 = BerryPatchGraphics_GetCurrentGraphicsResourceID(v1->unk_14.unk_0C);
    }

    v1->unk_28.x = FX32_ONE;
    v1->unk_28.y = FX32_ONE;
    v1->unk_28.z = FX32_ONE;
    #ifdef SDK_PORT
    SIM_Config_prj_type * myConfig = SIM_Config_prj_GetConfig();
    if(myConfig->enable60fps) {
        v1->unk_34 = (FX32_ONE / 64) >> 1;
    } else {
        v1->unk_34 = (FX32_ONE / 64);
    }
    #else
    v1->unk_34 = (FX32_ONE / 64);
    #endif

    if (v1->unk_10 == 2) {
        v1->unk_34 = 0;
    }

    ov5_021F1A24(v1, v1->unk_14.unk_0C, &v0);
    OverworldAnimManager_SetPosition(param0, &v0);
    ov5_021F1A8C(param0, v1);

    return 1;
}

static void ov5_021F18D0(OverworldAnimManager *param0, void *param1)
{
    UnkStruct_ov5_021F1A24 *v0 = param1;

    if (v0->unk_0C == 1) {
        Billboard_Delete(v0->unk_24);
    }
}

static void ov5_021F18E0(OverworldAnimManager *param0, void *param1)
{
    int v0;
    UnkStruct_ov5_021F1A24 *v1 = param1;
    MapObject *v2 = v1->unk_14.unk_0C;
    v0 = MapObject_GetGraphicsID(v2);

    if (BerryPatchGraphics_IsBerryPatch(v0) == 1) {
        v0 = BerryPatchGraphics_GetCurrentGraphicsResourceID(v1->unk_14.unk_0C);
    }

    if ((v1->unk_08 != v0) || (sub_02062764(v2, v1->unk_00, v1->unk_04) == 0) || (sub_02062F64(v2) == 0)) {
        FieldEffectManager_FinishAnimManager(param0);
        return;
    }

    v1->unk_28.x += v1->unk_34;

    if (v1->unk_28.x >= (FX32_ONE + (FX32_ONE / 8))) {
        v1->unk_28.x = (FX32_ONE + (FX32_ONE / 8));
        v1->unk_34 = -v1->unk_34;
    } else if (v1->unk_28.x <= (FX32_ONE - (FX32_ONE / 8))) {
        v1->unk_28.x = (FX32_ONE - (FX32_ONE / 8));
        v1->unk_34 = -v1->unk_34;
    }

    {
        VecFx32 v3;

        ov5_021F1A24(v1, v2, &v3);
        OverworldAnimManager_SetPosition(param0, &v3);
    }

    {
        if (v1->unk_0C == 0) {
            ov5_021F1A8C(param0, v1);
        }
    }
}

static void ov5_021F1978(OverworldAnimManager *param0, void *param1)
{
    UnkStruct_ov5_021F1A24 *v0 = param1;
    MapObject *v1 = v0->unk_14.unk_0C;

    {
        int v2 = MapObject_GetGraphicsID(v1);

        if (BerryPatchGraphics_IsBerryPatch(v2) == 1) {
            v2 = BerryPatchGraphics_GetCurrentGraphicsResourceID(v0->unk_14.unk_0C);
        }

        if ((v0->unk_08 != v2) || (sub_02062764(v1, v0->unk_00, v0->unk_04) == 0) || (sub_02062F64(v1) == 0)) {
            FieldEffectManager_FinishAnimManager(param0);
            return;
        }
    }

    if (v0->unk_0C == 0) {
        return;
    }

    if (OverworldPokemonAssets_IsGraphicsID(v0->unk_08)) {
        Billboard *source = ov5_021EB1A0(v1);
        if (MapObject_IsStatusOnElevatedBridge(v1)) {
            source = NULL;
        }
        VecFx32 position;
        OverworldAnimManager_GetPosition(param0, &position);
        if (source != NULL) {
            VecFx32 base;
            MapObject_GetPosPtr(v1, &base);
            // Reflect render-only bobbing, reactions and recall scale too.
            position.y -= Billboard_GetPos(source)->y - base.y;
        }
        PokemonReflection_Update(v0->unk_24, source, &position, OverworldPokemonAssets_IsLarge(v0->unk_08));
        VecFx32 scale = *Billboard_GetScale(v0->unk_24);
        scale.x = FX_Mul(scale.x, v0->unk_28.x);
        Billboard_SetScale(v0->unk_24, &scale);
        return;
    }

    if (MapObject_IsHidden(v1) == 1) {
        Billboard_SetDrawFlag(v0->unk_24, 0);
    } else {
        Billboard_SetDrawFlag(v0->unk_24, 1);
    }

    {
        VecFx32 v3;

        OverworldAnimManager_GetPosition(param0, &v3);
        Billboard_SetPos(v0->unk_24, &v3);
    }

    Billboard_SetScale(v0->unk_24, &v0->unk_28);

    {
        Billboard *v4 = ov5_021EB1A0(v0->unk_14.unk_0C);

        Billboard_SetAnimNum(v0->unk_24, Billboard_GetAnimNum(v4));
        Billboard_SetFrameNum(v0->unk_24, Billboard_GetFrameNum(v4));
    }

    FieldSystem *field = v0->unk_14.fieldSystem;
    v0->unk_24->depthOffset = 0;
    if (field->playerAvatar != NULL && PlayerAvatar_GetMapObject(field->playerAvatar) == v1) {
        // Reflection effects may have been created before the Surf mount, or
        // before its appearance changed. Keep the rider over the current mount
        // without moving either sprite or retaining a pointer to an old mount.
        Billboard_DrawAfter(v0->unk_24, SurfMountRenderer_GetReflection(field));
        Billboard_SetCallback(v0->unk_24, UpdateSurfRiderReflectionDepth, field);
    }
}

static const OverworldAnimManagerFuncs Unk_ov5_02200338 = {
    sizeof(UnkStruct_ov5_021F1A24),
    ov5_021F184C,
    ov5_021F18D0,
    ov5_021F18E0,
    ov5_021F1978
};

static void ov5_021F1A24(UnkStruct_ov5_021F1A24 *param0, MapObject *param1, VecFx32 *param2)
{
    int v0;
    fx32 v1, v2;
    fx32 v3[3] = {
        (FX32_ONE * 12),
        FX32_ONE * 16, (FX32_ONE * 12)
    };

    MapObject_GetSpriteJumpOffset(param1, param2);

    v1 = param2->x;
    v2 = -param2->z;

    MapObject_GetPosPtr(param1, param2);
    v0 = MapObject_RecalculatePositionHeight(param0->unk_14.fieldSystem, param2);

    param2->x += v1;
    param2->z += v2 + (FX32_ONE * -7);

    if (v0 == 0) {
        param2->y = 0;
    } else {
        param2->y -= v3[param0->unk_10];
    }
}

static void ov5_021F1A8C(OverworldAnimManager *param0, UnkStruct_ov5_021F1A24 *param1)
{
    int v0;
    void *v1;
    VecFx32 v2;
    BillboardResources v3;

    {
        const MapObjectManager *v4 = MapObject_MapObjectManager(param1->unk_14.unk_0C);

        if (ov5_021ED150(v4, param1->unk_08, &v3) == 0) {
            return;
        }

        if (ov5_021EDD94(param1->unk_14.unk_0C) == 1) {
            return;
        }
    }

    v1 = ov5_021DF9FC(param1->unk_14.unk_04, 2);
    v3.modelRes = v1;

    OverworldAnimManager_GetPosition(param0, &v2);

    if (OverworldPokemonAssets_IsGraphicsID(param1->unk_08)) {
        param1->unk_24 = PokemonReflection_New(param1->unk_14.unk_04, &v3, &v2);
    } else {
        param1->unk_24 = ov5_021DF7F8(param1->unk_14.unk_04, &v3, &v2);
    }
    if (param1->unk_24 != NULL) {
        // The model is shared with larger Pokemon reflections. Reapply this
        // source frame's dimensions and UV scale when drawing a native sprite.
        param1->unk_24->matchTextureDimensions = TRUE;
    }
    param1->unk_0C = param1->unk_24 != NULL;
}

static const OverworldAnimManagerFuncs Unk_ov5_0220034C;

static void ov5_021F1AD8(OverworldAnimManager *param0, UnkStruct_ov5_021F1AD8 *param1)
{
    int v0;
    void *v1;
    VecFx32 v2;
    BillboardResources v3 = param1->unk_04.unk_0C;

    v1 = ov5_021DF9FC(param1->unk_04.unk_04, 2);
    v3.modelRes = v1;

    OverworldAnimManager_GetPosition(param0, &v2);
    param1->unk_3C = ov5_021DF7F8(param1->unk_04.unk_04, &v3, &v2);
    param1->unk_3C->matchTextureDimensions = TRUE;

    {
        const Billboard *v4 = param1->unk_04.unk_34;

        Billboard_SetAnimNum(param1->unk_3C, Billboard_GetAnimNum(v4));
        Billboard_SetAnimFrameNum(param1->unk_3C, Billboard_GetAnimFrameNum(v4));
        Billboard_SetFrameNum(param1->unk_3C, Billboard_GetFrameNum(v4));
        Billboard_AdvanceAnim(param1->unk_3C, 0);
        Billboard_TryRequestVRAMTransfer(param1->unk_3C);
    }
}

static void ov5_021F1B4C(UnkStruct_ov5_021F1AD8 *param0, VecFx32 *param1)
{
    int v0;
    VecFx32 v1;
    fx32 v2;
    fx32 v3[3] = {
        FX32_ONE * 12,
        FX32_ONE * 16,
        FX32_ONE * 12,
    };

    *param1 = param0->unk_50;
    v0 = MapObject_RecalculatePositionHeight(param0->unk_04.fieldSystem, param1);
    param1->z += (FX32_ONE * -7);

    if (v0 == 0) {
        param1->y = 0;
    } else {
        param1->y -= v3[param0->unk_00];
    }
}

OverworldAnimManager *ov5_021F1BA4(FieldEffectManager *param0, const BillboardResources *param1, const Billboard *param2, const VecFx32 *param3, int param4, int param5)
{
    OverworldAnimManager *v0;
    UnkStruct_ov5_021F1BEC v1;

    v1.fieldSystem = FieldEffectManager_GetFieldSystem(param0);
    v1.unk_04 = param0;
    v1.unk_08 = FieldEffectManager_GetRendererContext(param0, 1);
    v1.unk_0C = *param1;
    v1.unk_34 = param2;

    v0 = FieldEffectManager_InitAnimManager(param0, &Unk_ov5_0220034C, param3, param4, &v1, param5);
    return v0;
}

static int ov5_021F1BEC(OverworldAnimManager *param0, void *param1)
{
    VecFx32 v0;
    UnkStruct_ov5_021F1AD8 *v1;
    const UnkStruct_ov5_021F1BEC *v2;

    v1 = param1;
    v2 = OverworldAnimManager_GetUserData(param0);

    v1->unk_04 = *v2;
    v1->unk_00 = OverworldAnimManager_GetUserInt(param0);
    v1->unk_40.x = FX32_ONE;
    v1->unk_40.y = FX32_ONE;
    v1->unk_40.z = FX32_ONE;
    #ifdef SDK_PORT
    SIM_Config_prj_type * myConfig = SIM_Config_prj_GetConfig();
    if(myConfig->enable60fps) {
        v1->unk_4C = (FX32_ONE / 64) >> 1;
    } else {
        v1->unk_4C = (FX32_ONE / 64);
    }
    #else
    v1->unk_4C = (FX32_ONE / 64);
    #endif

    if (v1->unk_00 == 2) {
        v1->unk_4C = 0;
    }

    OverworldAnimManager_GetPosition(param0, &v1->unk_50);
    ov5_021F1B4C(v1, &v0);

    OverworldAnimManager_SetPosition(param0, &v0);
    ov5_021F1AD8(param0, v1);

    OverworldAnimManager_CallTickFunc(param0);
    return 1;
}

static void ov5_021F1C54(OverworldAnimManager *param0, void *param1)
{
    UnkStruct_ov5_021F1AD8 *v0 = param1;
    Billboard_Delete(v0->unk_3C);
}

static void ov5_021F1C60(OverworldAnimManager *param0, void *param1)
{
    UnkStruct_ov5_021F1AD8 *v0 = param1;

    v0->unk_40.x += v0->unk_4C;

    if (v0->unk_40.x >= (FX32_ONE + (FX32_ONE / 8))) {
        v0->unk_40.x = (FX32_ONE + (FX32_ONE / 8));
        v0->unk_4C = -v0->unk_4C;
    } else if (v0->unk_40.x <= (FX32_ONE - (FX32_ONE / 8))) {
        v0->unk_40.x = (FX32_ONE - (FX32_ONE / 8));
        v0->unk_4C = -v0->unk_4C;
    }

    {
        VecFx32 v1;

        ov5_021F1B4C(v0, &v1);
        OverworldAnimManager_SetPosition(param0, &v1);
    }
}

static void ov5_021F1CA4(OverworldAnimManager *param0, void *param1)
{
    UnkStruct_ov5_021F1AD8 *v0 = param1;

    {
        VecFx32 v1;

        OverworldAnimManager_GetPosition(param0, &v1);
        Billboard_SetPos(v0->unk_3C, &v1);
    }

    Billboard_SetScale(v0->unk_3C, &v0->unk_40);
}

static const OverworldAnimManagerFuncs Unk_ov5_0220034C = {
    sizeof(UnkStruct_ov5_021F1A24),
    ov5_021F1BEC,
    ov5_021F1C54,
    ov5_021F1C60,
    ov5_021F1CA4
};
