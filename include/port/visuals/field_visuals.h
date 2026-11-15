#ifndef POKEPLATINUM_FIELD_VISUALS_H
#define POKEPLATINUM_FIELD_VISUALS_H

#include <nitro/types.h>
#include <nitro/fx/fx_mtx.h>
#include <nitro/fx/fx_vec.h>

struct AreaLightTemplate;
typedef struct FieldNightLighting {
    float sunIntensity, moonIntensity, emissionActivation;
    float directionalShadowStrength, contactShadowStrength;
    // Painted tree shadows: they also ground the trees, so some remain at night.
    float treeShadowStrength;
} FieldNightLighting;

enum FieldAtmosphereRegion {
    FIELD_ATMOSPHERE_PLAIN,
    FIELD_ATMOSPHERE_FOREST,
    FIELD_ATMOSPHERE_SNOW,
    FIELD_ATMOSPHERE_CAVE,
    FIELD_ATMOSPHERE_COAST,
};

#include "port/config/sim_config_prj.h"

#ifdef __cplusplus
extern "C" {
#endif

// The field's visual settings that the decomp's field code checks.
static inline BOOL FieldVisuals_CorrectLightingEnabled(void)
{
    return SIM_Config_prj_GetConfig()->correctLighting;
}

static inline BOOL FieldVisuals_EnhancedDayNightEnabled(void)
{
    return SIM_Config_prj_GetConfig()->enhancedDayNight;
}

static inline BOOL FieldVisuals_RegionalAtmosphereEnabled(void)
{
    return SIM_Config_prj_GetConfig()->regionalAtmosphere;
}

// Bloom only draws the night glows, so it needs both settings.
static inline BOOL FieldVisuals_EmissiveBloomEnabled(void)
{
    const SIM_Config_prj_type *config = SIM_Config_prj_GetConfig();
    return config->nightGlows && config->emissiveBloom;
}

// Session-only preview; the saved/gameplay clock is never changed.
int FieldVisuals_GetTime(int realSeconds);
VecFx16 FieldVisuals_OutdoorKeyLight(void);
void FieldVisuals_NightLighting(int seconds, FieldNightLighting *state);
// Ground offset of a sun-cast shadow per unit of height, along each of x
// and z (the shadows fall southeast like the authored ones).
float FieldVisuals_SunShadowLength(int seconds);
enum FieldNightMaterial {
    FIELD_NIGHT_MATERIAL_NONE,
    FIELD_NIGHT_MATERIAL_SHADOW, // Authored ground shadow; fades at night.
    FIELD_NIGHT_MATERIAL_PAINTED_SHADOW, // Shadow disc painted into an opaque ground tile; fades at night.
    FIELD_NIGHT_MATERIAL_TREE, // Tree atlas whose stump cells paint the tree's shadow; it fades at night.
    FIELD_NIGHT_MATERIAL_PUDDLE_GROUND, // Paths and sand that collect puddles in rain.
    FIELD_NIGHT_MATERIAL_WINDOW, // Glass that glows warm at night.
    FIELD_NIGHT_MATERIAL_SOURCE, // Light fixture that keeps its own colors.
    FIELD_NIGHT_MATERIAL_LAMP, // Lamp mesh whose bright glass emits.
    FIELD_NIGHT_MATERIAL_LIGHT_BAR, // Only the brightest texels emit, inside white trim.
    FIELD_NIGHT_MATERIAL_NEON, // Shared texture whose saturated bulbs emit.
    FIELD_NIGHT_MATERIAL_KEYED_WINDOW, // Shared texture whose colored panes glow warm.
    FIELD_NIGHT_MATERIAL_BEACON, // Lighthouse lantern; pulses with its beams.
    FIELD_NIGHT_MATERIAL_SCREEN, // Video screen; keeps its colors and flickers gently.
    FIELD_NIGHT_MATERIAL_EMBLEM, // Shared texture whose yellow emblem emits.
    FIELD_NIGHT_MATERIAL_LAVA, // Glows in its own colors by day too, pulsing slowly.
    FIELD_NIGHT_MATERIAL_WATER, // Water surface; mirrors lights at night.
};
// The strongest per-material bloom; emission is stored relative to it.
#define FIELD_BLOOM_PEAK 3.0f
typedef struct FieldNightMaterialInfo {
    enum FieldNightMaterial role;
    float bloom; // At most FIELD_BLOOM_PEAK.
    float intensity; // Scales how strongly the visible surface lights up.
} FieldNightMaterialInfo;
// map is the current map header; prop is the prop model being drawn, or -1
// for the land mesh. Returns NULL for an untagged material.
const FieldNightMaterialInfo *FieldVisuals_NightMaterial(const char name[16], int map, int prop);
// Maps where something glows by day (lava), so emission and bloom run then too.
BOOL FieldVisuals_HasDayEmission(int map);
void FieldVisuals_SetEmissionDebug(BOOL enabled);
BOOL FieldVisuals_IsEmissionDebug(void);
float FieldVisuals_PositionHalo(const VecFx32 *source, const MtxFx43 *view, float offset, BOOL perspective, VecFx32 *halo);
BOOL FieldVisuals_WaterTileVisible(const VecFx32 *position, const MtxFx43 *view, const MtxFx44 *projection);
// Maps clip (x, y, w) back to world units: world = matrix * clip + offset
// (row-major 3x3), and gives the camera's world position.
void FieldVisuals_WorldFromClip(const MtxFx43 *view, const MtxFx44 *projection, float matrix[9], float offset[3], float camera[3]);
void FieldVisuals_CycleTimePreview(void);
void FieldVisuals_ResetTimePreview(void);
const char *FieldVisuals_GetTimePreviewName(void);

// seconds is local game time; template end times use two-second units.
void FieldVisuals_SampleLight(const struct AreaLightTemplate *templates, int count, int seconds, struct AreaLightTemplate *result);
// Daylight level for haze and contact shadows: 1 by day, 0.35 at night.
float FieldVisuals_TimeBrightness(int seconds);

// One color grade for the 3D world, keyed to the time of day with small
// weather and region adjustments. Day is the identity.
typedef struct FieldGrade {
    float exposure, saturation, contrast;
    float shadows[3], highlights[3];
} FieldGrade;
void FieldVisuals_Grade(int seconds, enum FieldAtmosphereRegion region, int weather, FieldGrade *grade);
void FieldVisuals_IdentityGrade(FieldGrade *grade);
BOOL FieldVisuals_IsOrdinaryWeather(int weather);
void FieldVisuals_Atmosphere(enum FieldAtmosphereRegion region, int weather, int seconds, BOOL dayNight, float color[3], float *strength);
// Mist low over the ground: drifting snow, forest and lake morning mist,
// rain spray. strength 0 means none; depth is how far up it reaches.
void FieldVisuals_GroundMist(enum FieldAtmosphereRegion region, int weather, int seconds, float color[3], float *strength, float *depth);

#ifdef __cplusplus
}
#endif

#endif
