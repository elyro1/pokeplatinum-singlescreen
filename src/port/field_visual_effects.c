#include "port/field_visual_effects.h"

#include <math.h>
#include <nitro.h>
#include <nnsys.h>
#include <simulator/sim.h>
#include <string.h>

#include "constants/overworld_weather.h"

#include "field/field_system.h"
#include "port/field_lights.h"
#include "port/sim_config_prj.h"

#include "billboard.h"
#include "camera.h"
#include "field_overworld_state.h"
#include "map_header.h"
#include "map_matrix.h"
#include "map_object.h"
#include "map_tile_behavior.h"
#include "overlay005/ov5_021EB1A0.h"
#include "player_avatar.h"
#include "rtc.h"
#include "terrain_collision_manager.h"

#define WATER_LIMIT  2048
#define RIPPLE_LIMIT 16
#define CAST_LIMIT   32

// A character whose sprite casts shadows this frame.
typedef struct {
    Billboard *billboard;
    VecFx32 ground; // Under the feet: the contact shadow's center, on the terrain.
    float elevation, strength;
    float slopeX, slopeZ; // Ground slope on steps.
    // The contact shadow as drawn, on its own lowered plane; zero strength: none.
    float contactCenter[3], contactRadiusX, contactRadiusZ, contactStrength;
} CastShadow;

typedef struct {
    VecFx32 position;
    int tileX, tileZ;
    float age;
} Ripple;
static struct {
    FieldSystem *owner;
    int map, region, weather, cacheX, cacheZ, cacheAge;
    u32 cacheViewWidth;
    float waterCpuMs;
    FieldNightLighting night;
    BOOL exterior;
    VecFx32 lights[64];
    u8 lightKind[64];
    s8 lightAim[64]; // Downlights: x offset of the lit spot on the road.
    u8 lightWidth[64]; // Downlights: half width of the pool's quad.
    short lightDeck[64]; // Downlights: height of the painted patch's plane at the head.
    int lightCount;
    BOOL allowed, building, wasWet, modelScopeActive;
    BOOL dayEmission; // Something glows by day here; see FieldVisuals_HasDayEmission.
    float wetness; // Rain on world surfaces, fading in and out.
    float lavaPulse;
    int currentProp; // Prop model being drawn; -1 for the land mesh.
    u32 lastFrame;
    float time;
    float beaconAngle, beaconPulse; // Lighthouse beam rotation and lantern flare.
    float screenFlicker; // Brightness of video screens.
    VecFx32 lastRipplePosition;
    VecFx32 water[WATER_LIMIT];
    u32 waterCorners[WATER_LIMIT]; // Shore weights; see SIM_DrawWaterTiles.
    int waterCount, nextRipple;
    Ripple ripples[RIPPLE_LIMIT];
    int particlesDrawn, waterDrawn, ripplesDrawn;
    CastShadow casts[CAST_LIMIT];
    int castCount;
} sEffects;
// How clear the sky is for sun-cast shadows, and how wet the ground is,
// fading between maps.
static struct {
    FieldSystem *owner;
    BOOL outdoors;
    float clear, wet;
} sSunWeather;

static float Clamp(float x, float low, float high)
{
    return x < low ? low : (x > high ? high : x);
}
static fx32 Fixed(float x)
{
    return (fx32)(x * FX32_ONE);
}
static u32 Hash(int x, int z)
{
    u32 value = (u32)x * 0x9e3779b9u ^ (u32)z * 0x85ebca6bu;
    value ^= value >> 16;
    value *= 0x7feb352du;
    value ^= value >> 15;
    return value;
}
static BOOL ValidTile(FieldSystem *field, int x, int z)
{
    // Simple collision maps do not bounds-check their matrix access themselves.
    return x >= 0 && z >= 0 && x < MapMatrix_GetWidth(field->mapMatrix) * 32
        && z < MapMatrix_GetHeight(field->mapMatrix) * 32;
}
static BOOL WetTile(FieldSystem *field, int x, int z)
{
    if (!ValidTile(field, x, z)) {
        return FALSE;
    }
    u8 behavior = TerrainCollisionManager_GetTileBehavior(field, x, z);
    return behavior != 255 && !TileBehavior_IsBridgeOverWater(behavior) && !TileBehavior_IsWaterfall(behavior)
        && (TileBehavior_IsSurfable(behavior) || TileBehavior_IsPuddle(behavior) || TileBehavior_IsShallowWater(behavior));
}
static BOOL Ground(FieldSystem *field, VecFx32 *position)
{
    u8 source;
    position->y = TerrainCollisionManager_GetHeight(field, position->y, position->x, position->z, &source);
    position->y += FX32_ONE / 16;
    return source != CALCULATED_HEIGHT_SOURCE_NONE;
}

static void SetEffect(int mode, float phase)
{
    NNS_G3dGeFlushBuffer();
    SIM_SetFieldEffect(mode, phase);
}
static void SetEmissiveEffect(int mode, float phase, float bloom)
{
    NNS_G3dGeFlushBuffer();
    SIM_SetFieldEmissiveEffect(mode, phase, bloom / FIELD_BLOOM_PEAK);
}

static void BeginPatchPose(const VecFx32 *position)
{
    // Load camera space explicitly: previous models leave their base transform
    // in the shared NitroSystem globals. Local vertices stay within fx16 range.
    G3_MtxMode(GX_MTXMODE_POSITION_VECTOR);
    G3_PushMtx();
    G3_LoadMtx43(NNS_G3dGlbGetCameraMtx());
    G3_Translate(position->x, position->y, position->z);
}
static void SetPatchMaterial(GXRgb color, int polygonID)
{
    G3_TexImageParam(GX_TEXFMT_NONE, GX_TEXGEN_NONE, GX_TEXSIZE_S8, GX_TEXSIZE_T8, GX_TEXREPEAT_NONE, GX_TEXFLIP_NONE, GX_TEXPLTTCOLOR0_USE, 0);
    // Fog enabled so overlays carry the scene's fog instead of erasing it.
    G3_PolygonAttr(GX_LIGHTMASK_NONE, GX_POLYGONMODE_MODULATE, GX_CULL_NONE, polygonID, 30, GX_POLYGON_ATTR_MISC_FOG);
    G3_Color(color);
}
static void BeginPatch(const VecFx32 *position, GXRgb color, int polygonID)
{
    BeginPatchPose(position);
    SetPatchMaterial(color, polygonID);
}
static void EndPatch(void)
{
    G3_End();
    G3_PopMtx(1);
}
// slopeX/Z tilt the patch: its height changes by that much per unit of x/z.
static void TiltedGroundPatch(const VecFx32 *position, float radiusX, float radiusZ, float slopeX, float slopeZ, int mode, float phase, GXRgb color, int id)
{
    SetEffect(mode, phase);
    BeginPatch(position, color, id);
    // Keep the corner heights within fx16 range.
    float rise = fabsf(slopeX) * radiusX + fabsf(slopeZ) * radiusZ, scaleY = fmaxf(1, rise / 4);
    G3_Scale(Fixed(radiusX * 2), Fixed(scaleY), Fixed(radiusZ * 2));
    G3_Begin(GX_BEGIN_QUADS);
    const int signs[4][2] = { { -1, -1 }, { 1, -1 }, { 1, 1 }, { -1, 1 } };
    for (int i = 0; i < 4; i++) {
        G3_TexCoord((signs[i][0] + 1) * 4 * FX32_ONE, (signs[i][1] + 1) * 4 * FX32_ONE);
        float y = (slopeX * radiusX * signs[i][0] + slopeZ * radiusZ * signs[i][1]) / scaleY;
        G3_Vtx((fx16)(signs[i][0] * FX16_ONE / 2), (fx16)Fixed(y), (fx16)(signs[i][1] * FX16_ONE / 2));
    }
    EndPatch();
}
static void GroundPatch(const VecFx32 *position, float radiusX, float radiusZ, int mode, float phase, GXRgb color, int id)
{
    TiltedGroundPatch(position, radiusX, radiusZ, 0, 0, mode, phase, color, id);
}
// Steps are steep ramps that rise over a single tile. Samples the collision
// slope around a point; returns FALSE on level ground, or across a ledge,
// where the samples jump. On a slope, ground->y becomes the height there.
static BOOL TerrainSlope(FieldSystem *field, VecFx32 *ground, fx32 hint, float *slopeX, float *slopeZ)
{
    const float d = 4;
    float h[5];
    const float offsets[5][2] = { { 0, 0 }, { -d, 0 }, { d, 0 }, { 0, -d }, { 0, d } };
    for (int i = 0; i < 5; i++) {
        VecFx32 sample = { ground->x + Fixed(offsets[i][0]), hint, ground->z + Fixed(offsets[i][1]) };
        if (!Ground(field, &sample))
            return FALSE;
        h[i] = sample.y / (float)FX32_ONE;
    }
    *slopeX = (h[2] - h[1]) / (2 * d);
    *slopeZ = (h[4] - h[3]) / (2 * d);
    if (fabsf(*slopeX) + fabsf(*slopeZ) < 0.05f || fabsf(*slopeX) > 1.5f || fabsf(*slopeZ) > 1.5f)
        return FALSE;
    ground->y = Fixed(h[0]);
    return TRUE;
}
static void BillboardPatch(FieldSystem *field, const VecFx32 *position, float halfX, float halfY, int mode, float phase, GXRgb color, int id)
{
    const MtxFx43 *view = NNS_G3dGlbGetCameraMtx();
    VecFx32 right = { view->_00, view->_10, view->_20 }, up = { view->_01, view->_11, view->_21 };
    SetEffect(mode, phase);
    BeginPatch(position, color, id);
    // G3_Vtx uses signed fx16 with only +/-8 world units of range. Large
    // preview halos must scale a small local quad instead of wrapping vertices.
    float extent = fmaxf(halfX, halfY);
    if (extent > 4) {
        G3_Scale(Fixed(extent), Fixed(extent), Fixed(extent));
        halfX /= extent;
        halfY /= extent;
    }
    G3_Begin(GX_BEGIN_QUADS);
    const int signs[4][2] = { { -1, -1 }, { 1, -1 }, { 1, 1 }, { -1, 1 } };
    for (int i = 0; i < 4; i++) {
        G3_TexCoord((signs[i][0] + 1) * 4 *FX32_ONE, (signs[i][1] + 1) * 4 *FX32_ONE);
        G3_Vtx((fx16)(right.x * halfX * signs[i][0] + up.x * halfY * signs[i][1]),
            (fx16)(right.y * halfX * signs[i][0] + up.y * halfY * signs[i][1]),
            (fx16)(right.z * halfX * signs[i][0] + up.z * halfY * signs[i][1]));
    }
    EndPatch();
}

void FieldVisualEffects_SetScene(FieldSystem *field, BOOL allowed, enum FieldAtmosphereRegion region, int weather)
{
    int map = field->location->mapHeaderID;
    if (sEffects.owner != field || sEffects.map != map || field->npcSceneRenderFrame <= sEffects.lastFrame) {
        memset(&sEffects, 0, sizeof(sEffects));
        sEffects.owner = field;
        sEffects.map = map;
        sEffects.cacheAge = 100;
        for (int i = 0; i < RIPPLE_LIMIT; i++) {
            sEffects.ripples[i].age = 2;
        }
    }
    sEffects.lastFrame = field->npcSceneRenderFrame;
    sEffects.currentProp = -1;
    sEffects.allowed = allowed && (MapHeader_IsOutdoors(map) || MapHeader_IsCave(map));
    // Buildings only get the soft contact shadow; every other effect here
    // checks allowed, so interiors keep their original look.
    sEffects.building = allowed && MapHeader_IsBuilding(map);
    sEffects.modelScopeActive = sEffects.allowed || sEffects.building;
    sEffects.exterior = MapHeader_IsOutdoors(map) || region == FIELD_ATMOSPHERE_FOREST || region == FIELD_ATMOSPHERE_COAST || region == FIELD_ATMOSPHERE_SNOW;
    FieldVisuals_NightLighting(FieldVisuals_GetTime(GetSecondsSinceMidnight()), &sEffects.night);
    sEffects.lightCount = 0;
    sEffects.castCount = 0;
    sEffects.dayEmission = FieldVisuals_HasDayEmission(map);
    sEffects.waterCpuMs = 0;
    // Lights in land meshes, which are not props, from the light table.
    const FieldLamp *sLamps;
    int lampCount = FieldLights_Lamps(&sLamps);
    for (int i = 0; sEffects.allowed && sEffects.exterior && i < lampCount && sEffects.lightCount < 64; i++) {
        if (sLamps[i].map != map)
            continue;
        VecFx32 lamp = { sLamps[i].x * FX32_ONE, sLamps[i].y * FX32_ONE, sLamps[i].z * FX32_ONE };
        // A downlight stays on while its pool on the road is in view, even
        // once its high head has left the top of the screen.
        VecFx32 pool = { (sLamps[i].x + sLamps[i].aim) * FX32_ONE, (sLamps[i].y - 83) * FX32_ONE, sLamps[i].z * FX32_ONE };
        int x = sLamps[i].x / 16, z = sLamps[i].z / 16;
        if ((FieldVisuals_WaterTileVisible(&lamp, &field->npcSceneViewMatrix, &field->npcSceneProjectionMatrix)
                || FieldVisuals_WaterTileVisible(&pool, &field->npcSceneViewMatrix, &field->npcSceneProjectionMatrix))
            && ValidTile(field, x, z) && TerrainCollisionManager_GetTileBehavior(field, x, z) != 255) {
            sEffects.lightKind[sEffects.lightCount] = sLamps[i].kind;
            sEffects.lightAim[sEffects.lightCount] = sLamps[i].aim;
            sEffects.lightWidth[sEffects.lightCount] = sLamps[i].width;
            sEffects.lightDeck[sEffects.lightCount] = sLamps[i].deck;
            sEffects.lights[sEffects.lightCount++] = lamp;
        }
    }
    sEffects.region = region;
    sEffects.weather = weather;
    sEffects.particlesDrawn = sEffects.waterDrawn = sEffects.ripplesDrawn = 0;
    // Sun-cast shadows need clear weather. Crossing into a map with other
    // weather fades them over 1.5 s instead of switching them; this outlives
    // the per-map state reset above. Entering the outdoors starts settled.
    float sunTarget = weather == OVERWORLD_WEATHER_CLEAR || weather == OVERWORLD_WEATHER_CLEAR_8
            || weather == OVERWORLD_WEATHER_CLEAR_13 ? 1 : 0;
    float wetTarget = weather >= OVERWORLD_WEATHER_RAINING && weather <= OVERWORLD_WEATHER_THUNDERSTORM ? 1 : 0;
    BOOL outdoors = sEffects.allowed && sEffects.exterior;
    if (sSunWeather.owner != field || !sSunWeather.outdoors || !outdoors) {
        sSunWeather.owner = field;
        sSunWeather.clear = sunTarget;
        sSunWeather.wet = wetTarget;
    } else {
        float step = (SIM_Config_prj_GetConfig()->enable60fps ? 1.0f / 60 : 1.0f / 30) / 1.5f;
        sSunWeather.clear += Clamp(sunTarget - sSunWeather.clear, -step, step);
        // The ground dries and gets wet more slowly than the sky changes.
        sSunWeather.wet += Clamp(wetTarget - sSunWeather.wet, -step / 3, step / 3);
    }
    sSunWeather.outdoors = outdoors;
    float wetness = outdoors && SIM_Config_prj_GetConfig()->weatherEffects ? sSunWeather.wet : 0;
    sEffects.wetness = wetness;
    SIM_Config_prj_type *config = SIM_Config_prj_GetConfig();
    // Lights mirrored in water need the night glow and the water effect.
    BOOL reflections = config->waterEffects && config->nightGlows && sEffects.night.emissionActivation > 0;
    BOOL perspective = field->camera->projection == CAMERA_PROJECTION_PERSPECTIVE;
    // Cast shadows meet the contact shadow at the feet by world position.
    BOOL shadows = config->castShadows && config->softContactShadows;
    if (outdoors && (config->weatherEffects || reflections || shadows) && perspective) {
        float matrix[9], offset[3], camera[3];
        FieldVisuals_WorldFromClip(&field->npcSceneViewMatrix, &field->npcSceneProjectionMatrix, matrix, offset, camera);
        SIM_SetFieldWorld(matrix, offset, camera, sEffects.time);
    }
    SIM_SetFieldWaterReflection(outdoors && reflections && perspective);
    if (outdoors && config->weatherEffects && field->camera->projection == CAMERA_PROJECTION_PERSPECTIVE) {
        float mist[3], mistStrength, mistDepth;
        SIM_SetFieldWetness(wetness);
        // The mist settles around the player's footing; lower ground holds more.
        FieldVisuals_GroundMist(region, weather, FieldVisuals_GetTime(GetSecondsSinceMidnight()), mist, &mistStrength, &mistDepth);
        if (mistStrength > 0)
            SIM_SetFieldMist(mist[0], mist[1], mist[2], mistStrength, PlayerAvatar_GetPos(field->playerAvatar)->y / (float)FX32_ONE - 4, mistDepth);
    }
    if (!sEffects.allowed) {
        sEffects.wasWet = FALSE;
        for (int i = 0; i < RIPPLE_LIMIT; i++) {
            sEffects.ripples[i].age = 2;
        }
        return;
    }
    float delta = SIM_Config_prj_GetConfig()->enable60fps ? 1.0f / 60 : 1.0f / 30;
    sEffects.time += delta;
    // The lighthouse turns once every eight seconds; its lantern flares as
    // either of the two opposite beams swings towards the camera (south).
    sEffects.beaconAngle = fmodf(sEffects.time * (2 * 3.14159265f / 8), 2 * 3.14159265f);
    float facing = cosf(sEffects.beaconAngle);
    sEffects.beaconPulse = 0.55f + 0.45f * powf(fabsf(facing), 6);
    // Two incommensurate waves read as changing broadcast brightness.
    sEffects.screenFlicker = 0.82f + 0.18f * sinf(sEffects.time * 1.7f) * sinf(sEffects.time * 3.1f + 1);
    // Lava brightens and dims slowly.
    sEffects.lavaPulse = 0.88f + 0.12f * sinf(sEffects.time * 1.1f) * sinf(sEffects.time * 0.43f + 2);
    sEffects.cacheAge++;
    for (int i = 0; i < RIPPLE_LIMIT; i++) {
        sEffects.ripples[i].age += delta / 1.25f;
    }
}

#define INDOOR_CONTACT_SHADOW 0.5f

BOOL FieldVisualEffects_DrawContactShadow(FieldSystem *field, MapObject *object, const VecFx32 *position, const VecFx32 *scale, const NNSG3dResMdlInfo *modelInfo)
{
    SIM_Config_prj_type *config = SIM_Config_prj_GetConfig();
    if (sEffects.owner != field || !sEffects.modelScopeActive
        || (!config->softContactShadows && !config->castShadows && !config->nightGlows)) {
        return FALSE;
    }
    int x = MapObject_GetX(object), z = MapObject_GetZ(object);
    if (!ValidTile(field, x, z) || WetTile(field, x, z)) {
        return FALSE;
    }
    // Use the original model's bounding footprint and its exact render anchor.
    // The field-effect projection, animation position and time-dependent scale
    // are already supplied by the original shadow renderer. Do not resample
    // terrain or add a second camera-facing offset here.
    VecFx32 center = *position;
    center.x += FX_Mul(FX_Mul(modelInfo->boxX + modelInfo->boxW / 2, modelInfo->boxPosScale), scale->x);
    center.y += FX_Mul(FX_Mul(modelInfo->boxY + modelInfo->boxH / 2, modelInfo->boxPosScale), scale->y);
    center.z += FX_Mul(FX_Mul(modelInfo->boxZ + modelInfo->boxD / 2, modelInfo->boxPosScale), scale->z);
    float radiusX = FX_Mul(FX_Mul(modelInfo->boxW / 2, modelInfo->boxPosScale), scale->x) / (float)FX32_ONE;
    float radiusZ = FX_Mul(FX_Mul(modelInfo->boxD / 2, modelInfo->boxPosScale), scale->z) / (float)FX32_ONE;
    if (radiusX <= 0 || radiusZ <= 0) {
        return FALSE;
    }
    VecFx32 offset;
    MapObject_GetSpritePosOffset(object, &offset);
    float elevation = Clamp(fabsf(offset.y / (float)FX32_ONE), 0, 24);
    // On steps, the original anchor lies inside the ramp: both shadows sit on
    // and tilt with the sampled slope instead.
    VecFx32 standing;
    MapObject_GetPosPtr(object, &standing);
    VecFx32 slopeGround = center;
    float slopeX = 0, slopeZ = 0;
    BOOL sloped = TerrainSlope(field, &slopeGround, standing.y + 8 * FX32_ONE, &slopeX, &slopeZ);
    // Characters cast shadows and catch nearby night light from the same
    // footing; see DrawCastShadows and BeginCharacter.
    Billboard *billboard = ov5_021EB1A0(object);
    CastShadow *cast = NULL;
    if (billboard && sEffects.allowed && sEffects.exterior && sEffects.castCount < CAST_LIMIT) {
        cast = &sEffects.casts[sEffects.castCount++];
        cast->billboard = billboard;
        // On the terrain itself, not the contact shadow's lowered anchor.
        cast->ground = center;
        cast->ground.y = (sloped ? slopeGround.y : standing.y) + FX32_ONE / 2;
        cast->slopeX = slopeX;
        cast->slopeZ = slopeZ;
        cast->elevation = elevation;
        cast->strength = 1 / (1 + elevation * 0.08f);
        cast->contactStrength = 0;
    }
    if (!config->softContactShadows) {
        return FALSE;
    }
    // Interior lighting does not follow the clock, so neither do its shadows.
    float strength = INDOOR_CONTACT_SHADOW;
    if (!sEffects.building) {
        int seconds = FieldVisuals_GetTime(GetSecondsSinceMidnight());
        float brightness = FieldVisuals_TimeBrightness(seconds);
        strength = SIM_Config_prj_GetConfig()->nightLighting && sEffects.exterior
            ? sEffects.night.contactShadowStrength
            : (0.38f + brightness * 0.20f);
    }
    strength /= 1 + elevation * 0.08f;
    float size = 1 + elevation * 0.015f;
    // Overlapping shadows that share a polygon ID cut each other off where
    // the first one's faint edge was drawn, leaving a hard arc. Neighboring
    // tiles alternate IDs so shadows of objects side by side blend instead.
    if (sloped) {
        center.y = slopeGround.y + FX32_ONE / 2;
    }
    if (cast) {
        cast->contactCenter[0] = center.x / (float)FX32_ONE;
        cast->contactCenter[1] = center.y / (float)FX32_ONE;
        cast->contactCenter[2] = center.z / (float)FX32_ONE;
        cast->contactRadiusX = radiusX * size;
        cast->contactRadiusZ = radiusZ * size;
        // The patch's polygon alpha is 30 of 31.
        cast->contactStrength = strength * 30 / 31;
    }
    TiltedGroundPatch(&center, radiusX * size, radiusZ * size, slopeX, slopeZ, 1, strength, GX_RGB(3, 4, 5), (x + z) & 1 ? 33 : 48);
    SetEffect(0, 0);
    return TRUE;
}

// Cast shadows draw the sprite's own model again, flattened onto the ground.
// The billboard command would turn it back towards the camera, so it is
// skipped; the material keeps the current frame's texture, without culling.
// All cast shadows share one polygon ID, so where they overlap they do not
// darken twice. The contact shadow's ID differs; over it the shader shows
// the darker of the two (see SIM_SetFieldContactShadow).
static void SkipCommand(NNSG3dRS *state)
{
    NNS_G3dRSSetFlag(state, NNS_G3D_RSFLAG_SKIP);
}
static void CastMaterial(NNSG3dRS *state)
{
    G3_PolygonAttr(GX_LIGHTMASK_NONE, GX_POLYGONMODE_MODULATE, GX_CULL_NONE, 46, 31, GX_POLYGON_ATTR_MISC_FOG);
}
static void CastCallbacks(NNSG3dRS *state)
{
    NNS_G3dRSSetCallBack(state, SkipCommand, NNS_G3D_SBC_BB, NNS_G3D_SBC_CALLBACK_TIMING_A);
    NNS_G3dRSSetCallBack(state, SkipCommand, NNS_G3D_SBC_BBY, NNS_G3D_SBC_CALLBACK_TIMING_A);
    NNS_G3dRSSetCallBack(state, CastMaterial, NNS_G3D_SBC_MAT, NNS_G3D_SBC_CALLBACK_TIMING_C);
}
// offsetX/Z: ground offset per unit of sprite height, away from the light.
static void DrawCast(Billboard *billboard, const CastShadow *cast, float offsetX, float offsetZ, float opacity)
{
    if (opacity < 0.01f)
        return;
    const NNSG3dResMdlInfo *info = &billboard->model->info;
    // Overworld frames leave a row or two empty below the feet; the feet
    // stand on the contact shadow's center.
    float feet = (FX_Mul(info->boxY, info->boxPosScale) / (float)FX32_ONE + 2) * billboard->scale.y / FX32_ONE;
    // The sprite's height maps onto the ground, which may slope.
    float offsetY = cast->slopeX * offsetX + cast->slopeZ * offsetZ;
    VecFx32 position = cast->ground;
    position.x += Fixed((cast->elevation - feet) * offsetX);
    position.y += Fixed((cast->elevation - feet) * offsetY);
    position.z += Fixed((cast->elevation - feet) * offsetZ);
    MtxFx33 shear = { FX32_ONE, Fixed(cast->slopeX), 0, Fixed(offsetX), Fixed(offsetY), Fixed(offsetZ), 0, Fixed(cast->slopeZ), FX32_ONE };
    NNSG3dRenderObj *model = &billboard->renderObj;
    NNSG3dSbcCallBackFunc init = model->cbInitFunc;
    model->cbInitFunc = CastCallbacks;
    SetEffect(23, opacity);
    SIM_SetFieldContactShadow(cast->contactCenter, cast->contactRadiusX, cast->contactRadiusZ, cast->slopeX, cast->slopeZ, cast->contactStrength);
    // Without the depth bias field effects get, so trees and ledges next to
    // the character still hide the flat shadow.
    MtxFx44 biased = *NNS_G3dGlbGetProjectionMtx();
    NNS_G3dGlbSetProjectionMtx(&sEffects.owner->npcSceneProjectionMatrix);
    NNS_G3dGlbSetBaseTrans(&position);
    NNS_G3dGlbSetBaseRot(&shear);
    NNS_G3dGlbSetBaseScale(&billboard->scale);
    NNS_G3dGlbFlush();
    NNS_G3dDraw(model);
    NNS_G3dGeFlushBuffer();
    NNS_G3dGlbSetProjectionMtx(&biased);
    NNS_G3dGlbFlush();
    SIM_SetFieldContactShadow(cast->contactCenter, 0, 0, 0, 0, 0);
    model->cbInitFunc = init;
}
static const CastShadow *FindCharacter(Billboard *billboard)
{
    for (int i = 0; i < sEffects.castCount; i++)
        if (sEffects.casts[i].billboard == billboard)
            return &sEffects.casts[i];
    return NULL;
}
// How much warm light from lamps, lit windows, Pokémon Center entrances and
// the Cycling Road lights reaches a character at night: each source's pool
// on the ground lights a disc around it.
static float CharacterLight(const CastShadow *cast)
{
    float activation = sEffects.night.emissionActivation, light = 0;
    if (!SIM_Config_prj_GetConfig()->nightGlows || activation <= 0)
        return 0;
    for (int i = 0; i < sEffects.lightCount; i++) {
        int kind = sEffects.lightKind[i];
        static const float reach[] = { [FIELD_LIGHT_WINDOW] = 36, [FIELD_LIGHT_STREET] = 56, [FIELD_LIGHT_CENTER] = 48, [FIELD_LIGHT_DOWNLIGHT] = 44 };
        static const float strength[] = { [FIELD_LIGHT_WINDOW] = 0.6f, [FIELD_LIGHT_STREET] = 0.75f, [FIELD_LIGHT_CENTER] = 0.65f, [FIELD_LIGHT_DOWNLIGHT] = 0.75f };
        static const float poolZ[] = { [FIELD_LIGHT_WINDOW] = 12, [FIELD_LIGHT_CENTER] = 20, [FIELD_LIGHT_DOWNLIGHT] = 0 };
        if (kind > FIELD_LIGHT_DOWNLIGHT || reach[kind] == 0)
            continue;
        float dx = (cast->ground.x - sEffects.lights[i].x) / (float)FX32_ONE - (kind == FIELD_LIGHT_DOWNLIGHT ? sEffects.lightAim[i] : 0);
        float dz = (cast->ground.z - sEffects.lights[i].z) / (float)FX32_ONE - poolZ[kind];
        float near = (dx * dx + dz * dz) / (reach[kind] * reach[kind]);
        if (near < 1)
            light += strength[kind] * (1 - near) * (1 - near);
    }
    return fminf(light, 0.7f) * activation;
}
BOOL FieldVisualEffects_BeginCharacter(Billboard *billboard)
{
    if (!sEffects.modelScopeActive || !sEffects.allowed || !sEffects.exterior)
        return FALSE;
    const CastShadow *cast = FindCharacter(billboard);
    float light = cast ? CharacterLight(cast) : 0;
    if (light < 0.01f)
        return FALSE;
    SetEffect(25, light);
    return TRUE;
}
void FieldVisualEffects_EndCharacter(BOOL active)
{
    if (active)
        SetEffect(0, 0);
}
void FieldVisualEffects_DrawCastShadows(Billboard *billboard)
{
    if (!sEffects.modelScopeActive || !sEffects.allowed || !sEffects.exterior || !SIM_Config_prj_GetConfig()->castShadows)
        return;
    const CastShadow *cast = FindCharacter(billboard);
    if (!cast)
        return;
    // The sun casts shadows southeast, like the buildings' painted ones, on
    // clear days only, fading with them at dusk and across weather changes.
    float sun = SIM_Config_prj_GetConfig()->nightLighting ? sEffects.night.sunIntensity : 1;
    if (sSunWeather.clear > 0) {
        float length = FieldVisuals_SunShadowLength(FieldVisuals_GetTime(GetSecondsSinceMidnight()));
        DrawCast(billboard, cast, length, length, 0.32f * sun * sSunWeather.clear * cast->strength);
    }
    SetEffect(0, 0);
}

static void UpdateWaterCache(FieldSystem *field)
{
    int x = PlayerAvatar_GetXPos(field->playerAvatar), z = PlayerAvatar_GetZPos(field->playerAvatar);
    if (x == sEffects.cacheX && z == sEffects.cacheZ && sEffects.cacheViewWidth == field->npcSceneViewWidth && sEffects.cacheAge < 20) {
        return;
    }
    sEffects.cacheViewWidth = field->npcSceneViewWidth;
    sEffects.cacheX = x;
    sEffects.cacheZ = z;
    sEffects.cacheAge = 0;
    sEffects.waterCount = 0;
    // Walk each tile once rather than repeatedly traversing ring interiors.
    // A one-tile border lets every corner see all four tiles around it.
    enum { RADIUS_Z = 24, MAX_RADIUS_X = 48 };
    static u8 wet[(2 * RADIUS_Z + 3) * (2 * MAX_RADIUS_X + 3)];
    int radiusX = (int)Clamp(field->npcSceneViewWidth / 16.0f + 12, 24, MAX_RADIUS_X);
    int width = 2 * radiusX + 3;
    for (int dz = -RADIUS_Z - 1; dz <= RADIUS_Z + 1; dz++) {
        for (int dx = -radiusX - 1; dx <= radiusX + 1; dx++) {
            wet[(dz + RADIUS_Z + 1) * width + dx + radiusX + 1] = WetTile(field, x + dx, z + dz);
        }
    }
#define WET(dx, dz) wet[((dz) + RADIUS_Z + 1) * width + (dx) + radiusX + 1]
    for (int dz = -RADIUS_Z; dz <= RADIUS_Z; dz++) {
        for (int dx = -radiusX; dx <= radiusX && sEffects.waterCount < WATER_LIMIT; dx++) {
            if (!WET(dx, dz))
                continue;
            // A corner is open water only when all four tiles around it are,
            // so the effect fades out across shore tiles instead of spilling
            // onto banks that share a tile with the water.
            u32 corners = 0;
            for (int corner = 0; corner < 4; corner++) {
                int cx = dx + (corner == 1 || corner == 2 ? 0 : -1), cz = dz + (corner >= 2 ? 0 : -1);
                if (WET(cx, cz) && WET(cx + 1, cz) && WET(cx, cz + 1) && WET(cx + 1, cz + 1))
                    corners |= 255u << (corner * 8);
            }
            if (!corners)
                continue;
            int tx = x + dx, tz = z + dz;
            VecFx32 position = { (tx * 16 + 8) * FX32_ONE, PlayerAvatar_GetPos(field->playerAvatar)->y, (tz * 16 + 8) * FX32_ONE};
            if (Ground(field, &position) && FieldVisuals_WaterTileVisible(&position, &field->npcSceneViewMatrix, &field->npcSceneProjectionMatrix)) {
                sEffects.waterCorners[sEffects.waterCount] = corners;
                sEffects.water[sEffects.waterCount++] = position;
            }
        }
    }
#undef WET
}
static void UpdateInteractions(FieldSystem *field)
{
    int x = PlayerAvatar_GetXPos(field->playerAvatar), z = PlayerAvatar_GetZPos(field->playerAvatar);
    const VecFx32 *player = PlayerAvatar_GetPos(field->playerAvatar);
    BOOL wet = WetTile(field, x, z);
    float dx = (player->x - sEffects.lastRipplePosition.x) / (float)FX32_ONE;
    float dz = (player->z - sEffects.lastRipplePosition.z) / (float)FX32_ONE;
    if (wet && (!sEffects.wasWet || dx * dx + dz * dz >= 16)) {
        Ripple *ripple = &sEffects.ripples[sEffects.nextRipple];
        ripple->position = *player;
        if (Ground(field, &ripple->position)) {
            ripple->tileX = x;
            ripple->tileZ = z;
            ripple->age = 0;
            sEffects.nextRipple = (sEffects.nextRipple + 1) % RIPPLE_LIMIT;
            sEffects.lastRipplePosition = *player;
        }
    }
    sEffects.wasWet = wet;
}
static void DrawRipple(Ripple *ripple)
{
    float radius = 3 + 10 * ripple->age;
    // Keep rings inside their source water tile: never paint ripples over shore.
    float cx = ripple->position.x / (float)FX32_ONE, cz = ripple->position.z / (float)FX32_ONE;
    float left = fmaxf(-radius, ripple->tileX * 16 - cx), right = fminf(radius, (ripple->tileX + 1) * 16 - cx);
    float top = fmaxf(-radius, ripple->tileZ * 16 - cz), bottom = fminf(radius, (ripple->tileZ + 1) * 16 - cz);
    if (left >= right || top >= bottom) {
        return;
    }
    SetEffect(2, ripple->age);
    BeginPatch(&ripple->position, GX_RGB(24, 29, 31), 49 + sEffects.ripplesDrawn % 14);
    G3_Scale(Fixed(radius * 2),FX32_ONE, Fixed(radius * 2));
    G3_Begin(GX_BEGIN_QUADS);
    const float corners[4][2] = { { left, top }, { right, top }, { right, bottom }, { left, bottom } };
    for (int i = 0; i < 4; i++) {
        float u = (corners[i][0] / radius + 1) * 0.5f, v = (corners[i][1] / radius + 1) * 0.5f;
        G3_TexCoord(Fixed(u * 8), Fixed(v * 8));
        G3_Vtx((fx16)Fixed(corners[i][0] / (radius * 2)), 0, (fx16)Fixed(corners[i][1] / (radius * 2)));
    }
    EndPatch();
    sEffects.ripplesDrawn++;
}

static void DrawParticles(FieldSystem *field)
{
    BOOL rain = sEffects.weather >= OVERWORLD_WEATHER_RAINING && sEffects.weather <= OVERWORLD_WEATHER_THUNDERSTORM;
    int region = sEffects.region;
    if (region == FIELD_ATMOSPHERE_PLAIN && !rain) {
        return;
    }
    const VecFx32 *player = PlayerAvatar_GetPos(field->playerAvatar);
    int cellX = (int)floorf(player->x / (64.0f *FX32_ONE)), cellZ = (int)floorf(player->z / (64.0f *FX32_ONE));
    int hour = FieldVisuals_GetTime(GetSecondsSinceMidnight()) / 3600;
    BOOL fireflies = region == FIELD_ATMOSPHERE_FOREST && (hour < 5 || hour >= 19);
    for (int z = cellZ - 2; z <= cellZ + 2; z++) {
        for (int x = cellX - 2; x <= cellX + 2; x++) {
            u32 hash = Hash(x, z);
            float phase = fmodf((hash & 1023) / 1024.0f + sEffects.time / (region == FIELD_ATMOSPHERE_SNOW ? 5 : 9), 1.0f);
            VecFx32 position = { Fixed(x * 64 + 8 + ((hash >> 10) & 47) + sinf(sEffects.time + hash % 19) * 4), player->y, Fixed(z * 64 + 8 + ((hash >> 16) & 47)) };
            int tileX = (int)floorf(position.x / (16.0f * FX32_ONE));
            int tileZ = (int)floorf(position.z / (16.0f * FX32_ONE));
            if (!ValidTile(field, tileX, tileZ) || TerrainCollisionManager_GetTileBehavior(field, tileX, tileZ) == 255) {
                continue;
            }
            if (!Ground(field, &position)) {
                continue;
            }
            int mode = 4;
            float halfX = 0.40f, halfY = 0.40f;
            GXRgb color = GX_RGB(24, 23, 19);
            if (rain) {
                GroundPatch(&position, 1.5f + phase * 3, 1.5f + phase * 3, 2, phase, GX_RGB(21, 25, 28), 50 + hash % 13);
                sEffects.particlesDrawn++;
                continue;
            }
            if (region == FIELD_ATMOSPHERE_SNOW) {
                position.y += Fixed(26 * (1 - phase));
                halfX = halfY = 0.65f;
                color = GX_RGB(29, 30, 31);
            } else if (fireflies) {
                position.y += Fixed(5 + 4 * sinf(sEffects.time + hash % 23));
                mode = 6;
                halfX = halfY = 1.0f;
                color = GX_RGB(31, 28, 10);
            } else if (region == FIELD_ATMOSPHERE_FOREST) {
                position.y += Fixed(18 * (1 - phase));
                mode = 5;
                halfX = 1.25f;
                halfY = 0.65f;
                color = GX_RGB(21, 27, 9);
            } else {
                position.y += Fixed(4 + phase * 12);
            }
            BillboardPatch(field, &position, halfX, halfY, mode, phase, color, 50 + hash % 13);
            sEffects.particlesDrawn++;
        }
    }
}

static void DrawNightSources(FieldSystem *field);

void FieldVisualEffects_Render(FieldSystem *field)
{
    if (sEffects.owner != field || !sEffects.allowed) {
        return;
    }
    SIM_Config_prj_type *config = SIM_Config_prj_GetConfig();
    if (config->waterEffects) {
        u64 waterStart = SIM_VisualClockNs();
        UpdateWaterCache(field);
        UpdateInteractions(field);
        NNS_G3dGeFlushBuffer();
        SIM_DrawWaterTiles(sEffects.water, sEffects.waterCorners, sEffects.waterCount, sEffects.time, &field->npcSceneViewMatrix, &field->npcSceneProjectionMatrix);
        sEffects.waterDrawn = sEffects.waterCount;
        for (int i = 0; i < RIPPLE_LIMIT; i++) {
            if (sEffects.ripples[i].age < 1 && WetTile(field, sEffects.ripples[i].tileX, sEffects.ripples[i].tileZ)) {
                DrawRipple(&sEffects.ripples[i]);
            }
        }
        sEffects.waterCpuMs = (SIM_VisualClockNs() - waterStart) / 1000000.0f;
    } else {
        sEffects.wasWet = FALSE;
        for (int i = 0; i < RIPPLE_LIMIT; i++) {
            sEffects.ripples[i].age = 2;
        }
    }
    DrawNightSources(field);
    if (config->environmentalParticles) {
        DrawParticles(field);
    }
    SetEffect(0, 0);
}
int FieldVisualEffects_GetParticleCount(void)
{
    return sEffects.particlesDrawn;
}
int FieldVisualEffects_GetWaterCount(void)
{
    return sEffects.waterDrawn;
}
int FieldVisualEffects_GetRippleCount(void)
{
    return sEffects.ripplesDrawn;
}

float FieldVisualEffects_GetWaterCpuMs(void)
{
    return sEffects.waterCpuMs;
}
int FieldVisualEffects_GetLightCount(void)
{
    return sEffects.lightCount;
}

// Authored shadows that fade at night, as opposed to emissive materials.
static BOOL IsShadow(enum FieldNightMaterial role)
{
    return role >= FIELD_NIGHT_MATERIAL_SHADOW && role <= FIELD_NIGHT_MATERIAL_TREE;
}
// Whether night materials (shadows, emission) may change now.
static BOOL NightMaterialsActive(void)
{
    return (sEffects.night.emissionActivation > 0 || sEffects.dayEmission)
        && (SIM_Config_prj_GetConfig()->nightLighting || SIM_Config_prj_GetConfig()->nightGlows);
}
void FieldVisualEffects_SetMaterial(const NNSG3dResMdl *model, int material)
{
    BOOL night = NightMaterialsActive();
    if (!sEffects.modelScopeActive || !sEffects.allowed || !sEffects.exterior || (!night && sEffects.wetness <= 0))
        return;
    int mode = 0;
    float strength = 0, bloom = 0;
    const FieldNightMaterialInfo *info;
    if (sEffects.allowed && sEffects.exterior && material >= 0 && material < model->info.numMat) {
        const NNSG3dResName *name = NNS_G3dGetMatNameByIdx(NNS_G3dGetMat(model), material);
        // Only explicitly named materials change; see FieldVisuals_NightMaterial.
        info = FieldVisuals_NightMaterial(name->name, sEffects.map, sEffects.currentProp);
        enum FieldNightMaterial role = info ? info->role : FIELD_NIGHT_MATERIAL_NONE;
        if (role == FIELD_NIGHT_MATERIAL_PUDDLE_GROUND) {
            if (sEffects.wetness > 0)
                mode = 26;
        } else if (!night) {
        } else if (SIM_Config_prj_GetConfig()->nightLighting && IsShadow(role)) {
            float shadow = role == FIELD_NIGHT_MATERIAL_SHADOW ? sEffects.night.directionalShadowStrength : sEffects.night.treeShadowStrength;
            if (shadow < 0.999f) {
                static const int modes[] = {
                    [FIELD_NIGHT_MATERIAL_SHADOW] = 8,
                    [FIELD_NIGHT_MATERIAL_PAINTED_SHADOW] = 21,
                    [FIELD_NIGHT_MATERIAL_TREE] = 22,
                };
                mode = modes[role];
                strength = shadow;
            }
        } else if (SIM_Config_prj_GetConfig()->nightGlows && role >= FIELD_NIGHT_MATERIAL_WINDOW) {
            static const int modes[] = {
                [FIELD_NIGHT_MATERIAL_WINDOW] = 9,
                [FIELD_NIGHT_MATERIAL_SOURCE] = 10,
                [FIELD_NIGHT_MATERIAL_LAMP] = 13,
                [FIELD_NIGHT_MATERIAL_LIGHT_BAR] = 18,
                [FIELD_NIGHT_MATERIAL_NEON] = 14,
                [FIELD_NIGHT_MATERIAL_KEYED_WINDOW] = 16,
                [FIELD_NIGHT_MATERIAL_BEACON] = 9,
                [FIELD_NIGHT_MATERIAL_SCREEN] = 10,
                [FIELD_NIGHT_MATERIAL_EMBLEM] = 20,
                [FIELD_NIGHT_MATERIAL_LAVA] = 10,
                [FIELD_NIGHT_MATERIAL_WATER] = 27,
            };
            mode = modes[role];
            if (role == FIELD_NIGHT_MATERIAL_WATER && !SIM_Config_prj_GetConfig()->waterEffects)
                mode = 0;
            strength = sEffects.night.emissionActivation * info->intensity;
            if (role == FIELD_NIGHT_MATERIAL_LAVA)
                strength = (0.45f + 0.25f * sEffects.night.emissionActivation) * sEffects.lavaPulse;
            if (role == FIELD_NIGHT_MATERIAL_BEACON)
                strength *= sEffects.beaconPulse;
            else if (role == FIELD_NIGHT_MATERIAL_SCREEN)
                strength *= sEffects.screenFlicker;
            bloom = info->bloom;
        }
    }
    SetEmissiveEffect(mode, strength, bloom);
}
static void NightMaterialCallback(NNSG3dRS *state)
{
    FieldVisualEffects_SetMaterial(state->pRenderObj->resMdl, state->currentMat);
}
BOOL FieldVisualEffects_BeginModel(NNSG3dRenderObj *model)
{
    BOOL night = NightMaterialsActive();
    if ((!night && sEffects.wetness <= 0) || !sEffects.modelScopeActive || !sEffects.allowed || !sEffects.exterior || model->cbFunc || model->cbInitFunc)
        return FALSE;
    BOOL tagged = FALSE;
    for (int i = 0; i < model->resMdl->info.numMat; i++) {
        const NNSG3dResName *name = NNS_G3dGetMatNameByIdx(NNS_G3dGetMat(model->resMdl), i);
        const FieldNightMaterialInfo *info = FieldVisuals_NightMaterial(name->name, sEffects.map, sEffects.currentProp);
        enum FieldNightMaterial role = info ? info->role : FIELD_NIGHT_MATERIAL_NONE;
        if ((role == FIELD_NIGHT_MATERIAL_PUDDLE_GROUND && sEffects.wetness > 0)
            || (night && IsShadow(role) && SIM_Config_prj_GetConfig()->nightLighting)
            || (night && role >= FIELD_NIGHT_MATERIAL_WINDOW && SIM_Config_prj_GetConfig()->nightGlows)) {
            tagged = TRUE;
            break;
        }
    }
    if (!tagged)
        return FALSE;
    model->cbFunc = NightMaterialCallback;
    model->cbCmd = NNS_G3D_SBC_MAT;
    model->cbTiming = NNS_G3D_SBC_CALLBACK_TIMING_C;
    return TRUE;
}
void FieldVisualEffects_EndModel(NNSG3dRenderObj *model, BOOL active)
{
    if (!active)
        return;
    SetEffect(0, 0);
    model->cbFunc = NULL;
    model->cbCmd = 0;
    model->cbTiming = NNS_G3D_SBC_CALLBACK_TIMING_NONE;
}
static void RegisterLight(const VecFx32 *position, const VecFx32 *scale, int x, int y, int z, int kind)
{
    if (sEffects.lightCount >= 64) return;
    VecFx32 light = *position;
    light.x += FX_Mul(x * FX32_ONE, scale->x);
    light.y += FX_Mul(y * FX32_ONE, scale->y);
    light.z += FX_Mul(z * FX32_ONE, scale->z);
    sEffects.lightKind[sEffects.lightCount] = kind;
    sEffects.lights[sEffects.lightCount++] = light;
}
void FieldVisualEffects_RegisterProp(int modelID, const VecFx32 *position, const VecFx32 *scale)
{
    sEffects.currentProp = modelID;
    if (!sEffects.modelScopeActive || !sEffects.allowed || !sEffects.exterior) return;
    const FieldPropLight *lights;
    int count = FieldLights_PropLights(&lights);
    for (int i = 0; i < count; i++)
        if (lights[i].prop == modelID)
            RegisterLight(position, scale, lights[i].x, lights[i].y, lights[i].z, lights[i].kind);
}

static void DrawBeam(const VecFx32 *center, float angle, float activation)
{
    // A horizontal fan from the lantern outwards: narrow at the glass,
    // widening and fading with distance.
    const float length = 170, nearWidth = 3, farWidth = 34;
    float dx = sinf(angle), dz = cosf(angle);
    const float corners[4][3] = {
        { -dz * nearWidth, 0, dx * nearWidth },
        { dx * length - dz * farWidth, 0, dz * length + dx * farWidth },
        { dx * length + dz * farWidth, 0, dz * length - dx * farWidth },
        { dz * nearWidth, 0, -dx * nearWidth },
    };
    const int uv[4][2] = { { 0, 0 }, { 8, 0 }, { 8, 8 }, { 0, 8 } };
    float extent = length + farWidth;
    SetEmissiveEffect(17, activation * 0.35f, 2.0f);
    BeginPatch(center, GX_RGB(31, 30, 24), 47);
    G3_Scale(Fixed(extent), Fixed(extent), Fixed(extent));
    G3_Begin(GX_BEGIN_QUADS);
    for (int i = 0; i < 4; i++) {
        G3_TexCoord(uv[i][0] * FX32_ONE, uv[i][1] * FX32_ONE);
        G3_Vtx((fx16)Fixed(corners[i][0] / extent), 0, (fx16)Fixed(corners[i][2] / extent));
    }
    EndPatch();
}
// Draws a quad with corners relative to center, in world units. uv spans
// 0..8 across it for the analytic shader modes.
static void DrawQuad(const VecFx32 *center, const float corners[4][3], GXRgb color, int id)
{
    static const int uv[4][2] = { { 0, 0 }, { 8, 0 }, { 8, 8 }, { 0, 8 } };
    float extent = 1;
    for (int i = 0; i < 4; i++)
        for (int axis = 0; axis < 3; axis++)
            extent = fmaxf(extent, fabsf(corners[i][axis]));
    // G3_Vtx takes fx16, so scale a unit-sized quad up.
    BeginPatch(center, color, id);
    G3_Scale(Fixed(extent), Fixed(extent), Fixed(extent));
    G3_Begin(GX_BEGIN_QUADS);
    for (int i = 0; i < 4; i++) {
        G3_TexCoord(uv[i][0] * FX32_ONE, uv[i][1] * FX32_ONE);
        G3_Vtx((fx16)Fixed(corners[i][0] / extent), (fx16)Fixed(corners[i][1] / extent), (fx16)Fixed(corners[i][2] / extent));
    }
    EndPatch();
}
// A Cycling Road light: a soft cone falls from the head (its lens glows
// through its own material) and a warm pool covers the patch of light the
// game paints on the road. head is the top of the lamp head over the arm's
// tip; aim is the patch's x offset from it, inwards over the road.
static void DrawDownlight(const VecFx32 *head, int aim, float rx, int patch, float activation, int id)
{
    // The pool follows the deck, which climbs and levels out along the road.
    // Sampled from below the head, the collision lookup finds the deck rather
    // than the ground under the bridge. The visible road runs a little above
    // its collision, and in places several units above it; there the painted
    // patch's plane, which rises 5.2 units over 67 northwards, is used.
    enum { SEGMENTS = 8 };
    const float rz = 52;
    float deck[SEGMENTS + 1];
    VecFx32 pool = *head;
    pool.x += aim * FX32_ONE;
    for (int i = 0; i <= SEGMENTS; i++) {
        VecFx32 sample = { pool.x, head->y - 60 * FX32_ONE, pool.z + Fixed(-rz + 2 * rz * i / SEGMENTS) };
        deck[i] = (Ground(sEffects.owner, &sample) ? sample.y / (float)FX32_ONE : head->y / (float)FX32_ONE - 83) + 3;
        if (patch)
            deck[i] = fmaxf(deck[i], patch + 0.7f + (5.2f / 67) * -(sample.z - head->z) / (float)FX32_ONE);
    }
    pool.y = Fixed(deck[SEGMENTS / 2]);
    VecFx32 lens = *head;
    lens.y -= 4 * FX32_ONE;
    float height = (lens.y - pool.y) / (float)FX32_ONE;
    const MtxFx43 *view = NNS_G3dGlbGetCameraMtx();
    float right[2] = { view->_00 / (float)FX32_ONE, view->_20 / (float)FX32_ONE };
    float length = sqrtf(right[0] * right[0] + right[1] * right[1]);
    right[0] /= length;
    right[1] /= length;
    // The cone's foot is as wide as the painted patch.
    const float nearWidth = 3, farWidth = rx * 0.6f;
    const float cone[4][3] = {
        { -right[0] * nearWidth, 0, -right[1] * nearWidth },
        { aim - right[0] * farWidth, -height, -right[1] * farWidth },
        { aim + right[0] * farWidth, -height, right[1] * farWidth },
        { right[0] * nearWidth, 0, right[1] * nearWidth },
    };
    SetEmissiveEffect(24, activation * 0.22f, 1.0f);
    DrawQuad(&lens, cone, GX_RGB(31, 29, 22), 47);
    // The pool's soft falloff reaches the patch's edges about 0.6 of the way
    // out, as a strip of quads on the sampled deck.
    SetEffect(7, activation * 0.40f);
    BeginPatch(&pool, GX_RGB(31, 27, 18), id);
    const float extent = rz;
    G3_Scale(Fixed(extent), Fixed(extent), Fixed(extent));
    G3_Begin(GX_BEGIN_QUAD_STRIP);
    for (int i = 0; i <= SEGMENTS; i++) {
        float z = -rz + 2 * rz * i / SEGMENTS, y = deck[i] - deck[SEGMENTS / 2];
        fx32 v = Fixed(8.0f * i / SEGMENTS);
        G3_TexCoord(0, v);
        G3_Vtx((fx16)Fixed(-rx / extent), (fx16)Fixed(y / extent), (fx16)Fixed(z / extent));
        G3_TexCoord(8 * FX32_ONE, v);
        G3_Vtx((fx16)Fixed(rx / extent), (fx16)Fixed(y / extent), (fx16)Fixed(z / extent));
    }
    EndPatch();
}
static void DrawNightSources(FieldSystem *field)
{
    if (!SIM_Config_prj_GetConfig()->nightGlows || !sEffects.exterior || sEffects.night.emissionActivation <= 0)
        return;
    float activation = sEffects.night.emissionActivation;
    BOOL bloom = SIM_Config_prj_GetConfig()->emissiveBloom;
    for (int i = 0; i < sEffects.lightCount; i++) {
        const VecFx32 *light = &sEffects.lights[i];
        int kind = sEffects.lightKind[i];
        if (kind == FIELD_LIGHT_BEACON) {
            DrawBeam(light, sEffects.beaconAngle, activation);
            DrawBeam(light, sEffects.beaconAngle + 3.14159265f, activation);
            continue;
        }
        if (kind == FIELD_LIGHT_DOWNLIGHT) {
            DrawDownlight(light, sEffects.lightAim[i], sEffects.lightWidth[i], sEffects.lightDeck[i], activation, 34 + i % 6);
            continue;
        }
        GXRgb glowColor = kind == FIELD_LIGHT_CENTER ? GX_RGB(31, 27, 24) : GX_RGB(31, 24, 12);
        // Every source's material emits, so bloom spreads its actual shape.
        // Without bloom, windows and the Center logo keep a restrained halo,
        // brought clear of recessed glass/frame geometry but still depth
        // tested against nearer objects. Lamp glass emits without a halo.
        if (!bloom && kind != FIELD_LIGHT_STREET) {
            VecFx32 halo;
            float haloScale = FieldVisuals_PositionHalo(light, NNS_G3dGlbGetCameraMtx(), 7,
                field->camera->projection == CAMERA_PROJECTION_PERSPECTIVE, &halo);
            BillboardPatch(field, &halo, 3.0f * haloScale, 3.0f * haloScale, 7, activation * 0.55f, glowColor, 40 + i % 6);
        }
        VecFx32 pool = *light;
        if (kind == FIELD_LIGHT_WINDOW || kind == FIELD_LIGHT_CENTER)
            pool.z += (kind == FIELD_LIGHT_CENTER ? 20 : 12) * FX32_ONE;
        else
            pool.y -= 26 * FX32_ONE;
        int x = (int)floorf(pool.x / (16.0f * FX32_ONE));
        int z = (int)floorf(pool.z / (16.0f * FX32_ONE));
        if (ValidTile(field, x, z) && !WetTile(field, x, z) && Ground(field, &pool)) {
            pool.y += 2 * FX32_ONE; // Authored pavement sits slightly above collision height.
            GroundPatch(&pool, kind == FIELD_LIGHT_STREET ? 28 : kind == FIELD_LIGHT_CENTER ? 24 : 18, kind == FIELD_LIGHT_STREET ? 24 : 14, 7, activation * 0.30f, glowColor, 34 + i % 6);
        }
    }
}

void FieldVisualEffects_EndProp(void)
{
    sEffects.currentProp = -1;
}

void FieldVisualEffects_EndScene(void)
{
    sEffects.modelScopeActive = FALSE;
    // Billboards drawn outside the field never match a stale character.
    sEffects.castCount = 0;
}
