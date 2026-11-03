#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "constants/overworld_weather.h"
#include "generated/map_headers.h"
#include "port/field_lights.h"
#include "port/field_visuals.h"
#include "overlay005/area_light.h"
#include "port/sim_config_prj.h"

#include "lighting_fixtures.h"

static int Channel(GXRgb color, int shift)
{
    return (color >> shift) & 31;
}

static void CameraPoint(const VecFx32 *position, const MtxFx43 *view, double result[3])
{
    const fx32 values[3] = { position->x, position->y, position->z };
    for (int axis = 0; axis < 3; axis++) {
        result[axis] = view->m[3][axis] / (double)FX32_ONE;
        for (int j = 0; j < 3; j++) result[axis] += values[j] / (double)FX32_ONE * view->m[j][axis] / FX32_ONE;
    }
}
static void CheckHaloAlignment(void)
{
    const VecFx32 window = { 5089 * FX32_ONE, 146 * FX32_ONE, 8352 * FX32_ONE };
    // Quantized tilted camera, moving the source across the full viewport.
    MtxFx43 view = {0};
    view._00 = FX32_ONE; view._11 = view._22 = 2107;
    view._12 = 3512; view._21 = -3512;
    for (int x = -400; x <= 400; x += 80) {
        for (int y = -200; y <= 200; y += 80) {
            view._30 = (x - 5089) * FX32_ONE;
            view._31 = y * FX32_ONE - (fx32)(146 * view._11 + 8352 * view._21);
            view._32 = -650 * FX32_ONE - (fx32)(146 * view._12 + 8352 * view._22);
            double source[3], moved[3];
            CameraPoint(&window, &view, source);
            for (int offset = 7; offset <= 79; offset += 24) {
                VecFx32 halo;
                float scale = FieldVisuals_PositionHalo(&window, &view, offset, TRUE, &halo);
                CameraPoint(&halo, &view, moved);
                assert(moved[2] > source[2]);
                assert(fabs(source[0] / source[2] - moved[0] / moved[2]) < 2e-6);
                assert(fabs(source[1] / source[2] - moved[1] / moved[2]) < 2e-6);
                assert(fabs(scale / moved[2] - 1 / source[2]) < 2e-8);
                scale = FieldVisuals_PositionHalo(&window, &view, offset, FALSE, &halo);
                CameraPoint(&halo, &view, moved);
                assert(scale == 1 && moved[2] > source[2]);
                assert(fabs(moved[0] - source[0]) < 0.001);
                assert(fabs(moved[1] - source[1]) < 0.001);
            }
        }
    }
    puts("PASS: halo center and apparent size stay aligned across moving perspective/orthographic views");
}

#define ROLE(name) (FieldVisuals_NightMaterial(name, 0, -1) ? FieldVisuals_NightMaterial(name, 0, -1)->role : FIELD_NIGHT_MATERIAL_NONE)

int main(void)
{
    CheckHaloAlignment();
    assert(ROLE("h_kage") == FIELD_NIGHT_MATERIAL_SHADOW);
    assert(ROLE("h_kage_lm3") == FIELD_NIGHT_MATERIAL_SHADOW);
    assert(ROLE("h_mado") == FIELD_NIGHT_MATERIAL_WINDOW);
    assert(ROLE("light_a") == FIELD_NIGHT_MATERIAL_SOURCE);
    assert(ROLE("shadowchip_lm2") == FIELD_NIGHT_MATERIAL_SHADOW);
    assert(ROLE("tshadow_lm25") == FIELD_NIGHT_MATERIAL_PAINTED_SHADOW);
    assert(ROLE("conttree_b_lm3") == FIELD_NIGHT_MATERIAL_TREE);
    assert(ROLE("tree04_2_lm3") == FIELD_NIGHT_MATERIAL_TREE);
    assert(ROLE("conttree_t_lm3") == FIELD_NIGHT_MATERIAL_NONE);
    assert(ROLE("dun_shadow_lm3") == FIELD_NIGHT_MATERIAL_NONE);
    assert(ROLE("lamp03") == FIELD_NIGHT_MATERIAL_SOURCE);
    assert(ROLE("c1_o02_lm4") == FIELD_NIGHT_MATERIAL_LAMP);
    assert(ROLE("c1_o02b_lm6") == FIELD_NIGHT_MATERIAL_LAMP);
    assert(ROLE("c1_o02c") == FIELD_NIGHT_MATERIAL_NONE);
    assert(ROLE("lamp010") == FIELD_NIGHT_MATERIAL_NONE);
    assert(FieldVisuals_NightMaterial("light2", 0, -1)->bloom == 0.85f);
    assert(FieldVisuals_NightMaterial("light_a", 0, -1)->bloom == 0.85f);
    assert(FieldVisuals_NightMaterial("c1_o02_lm4", 0, -1)->bloom == 2.0f);
    // Twinleaf and Sandgem house lights are 40% dimmer than elsewhere.
    const FieldNightMaterialInfo *quiet = FieldVisuals_NightMaterial("light", MAP_HEADER_TWINLEAF_TOWN, 22);
    const FieldNightMaterialInfo *town = FieldVisuals_NightMaterial("light", MAP_HEADER_SOLACEON_TOWN, 22);
    assert(quiet->intensity == 0.6f && quiet->bloom == 2.0f * 0.6f && town->intensity == 1);
    assert(FieldVisuals_NightMaterial("light", MAP_HEADER_SANDGEM_TOWN, -1)->intensity == 0.6f);
    // The Cycling Road lenses: only in Route 206's land mesh, not in props there.
    assert(FieldVisuals_NightMaterial("lambert14", MAP_HEADER_ROUTE_206, -1)->role == FIELD_NIGHT_MATERIAL_SOURCE);
    assert(FieldVisuals_NightMaterial("lambert14", MAP_HEADER_ROUTE_206, 5) == NULL);
    assert(FieldVisuals_NightMaterial("lambert14", MAP_HEADER_JUBILIFE_CITY, -1) == NULL);
    // Generic material names are only tagged on the prop that was audited.
    assert(!FieldVisuals_NightMaterial("gyma", 0, 8) && FieldVisuals_NightMaterial("lambert5", 0, 8));
    assert(FieldVisuals_NightMaterial("h_mado", 0, 540)->intensity == 0.55f && FieldVisuals_NightMaterial("h_mado", 0, 9)->intensity == 1);
    assert(FieldVisuals_NightMaterial("c1_s02_4", 0, 13)->role == FIELD_NIGHT_MATERIAL_SCREEN);
    assert(FieldVisuals_NightMaterial("c1_b01_4_lm1", 0, 11)->role == FIELD_NIGHT_MATERIAL_WINDOW);
    assert(!FieldVisuals_NightMaterial("c1_s01_a", 0, 12));
    assert(FieldVisuals_NightMaterial("lambert2", 0, 207)->bloom == FIELD_BLOOM_PEAK);
    assert(FieldVisuals_NightMaterial("c1_b01_4_lm3", 0, 245)->role == FIELD_NIGHT_MATERIAL_WINDOW);
    assert(!FieldVisuals_NightMaterial("c1_b02_1_lm3", 0, 245));
    assert(FieldVisuals_NightMaterial("c1_school_2", 0, 17)->role == FIELD_NIGHT_MATERIAL_EMBLEM);
    assert(FieldVisuals_NightMaterial("lambert3", 0, 223)->role == FIELD_NIGHT_MATERIAL_BEACON);
    assert(FieldVisuals_NightMaterial("lambert3", 0, 452)->bloom == 0.85f);
    assert(FieldVisuals_NightMaterial("lambert2", 0, 207)->role == FIELD_NIGHT_MATERIAL_NEON);
    assert(FieldVisuals_NightMaterial("a", 0, 477)->role == FIELD_NIGHT_MATERIAL_KEYED_WINDOW);
    assert(!FieldVisuals_NightMaterial("lambert2", 0, -1) && !FieldVisuals_NightMaterial("lambert3", 0, 5));
    assert(!FieldVisuals_NightMaterial("a", 0, -1));
    assert(ROLE("light") == FIELD_NIGHT_MATERIAL_SOURCE);
    assert(ROLE("c5_light_lm3") == FIELD_NIGHT_MATERIAL_LAMP);
    assert(ROLE("r206_pol1_lm2") == FIELD_NIGHT_MATERIAL_NONE);
    assert(ROLE("r206_pol2_lm2") == FIELD_NIGHT_MATERIAL_NONE);
    assert(ROLE("h_kageWall") == FIELD_NIGHT_MATERIAL_NONE);
    assert(ROLE("c1_b02_1_lm3") == FIELD_NIGHT_MATERIAL_NONE);
    FieldNightLighting nightState, dayState, previousState;
    FieldVisuals_NightLighting(0, &nightState);
    FieldVisuals_NightLighting(12 * 3600, &dayState);
    assert(nightState.sunIntensity == 0 && nightState.emissionActivation == 1);
    assert(dayState.sunIntensity == 1 && dayState.emissionActivation == 0);
    assert(nightState.directionalShadowStrength < 0.05f);
    assert(nightState.contactShadowStrength > 0.3f);
    assert(nightState.treeShadowStrength > nightState.directionalShadowStrength && nightState.treeShadowStrength < 0.5f);
    assert(dayState.directionalShadowStrength == 1 && dayState.treeShadowStrength == 1);
    // Sun-cast shadows are shortest at noon and lengthen towards sunrise and sunset.
    assert(fabsf(FieldVisuals_SunShadowLength(12 * 3600) - 0.5f) < 0.001f);
    assert(fabsf(FieldVisuals_SunShadowLength(18 * 3600) - 0.9f) < 0.001f);
    for (int hour = 7; hour < 12; hour++)
        assert(FieldVisuals_SunShadowLength(hour * 3600) > FieldVisuals_SunShadowLength((hour + 1) * 3600));
    FieldVisuals_NightLighting(86399, &previousState);
    for (int second = 0; second < 86400; second++) {
        FieldNightLighting state;
        FieldVisuals_NightLighting(second, &state);
        assert(state.sunIntensity >= 0 && state.sunIntensity <= 1);
        assert(state.emissionActivation >= 0 && state.emissionActivation <= 1);
        assert(fabsf(state.sunIntensity - previousState.sunIntensity) < 0.001f);
        assert(fabsf(state.emissionActivation - previousState.emissionActivation) < 0.001f);
        assert(state.moonIntensity >= 0 && state.moonIntensity <= 0.15f);
        previousState = state;
    }
    VecFx16 key = FieldVisuals_OutdoorKeyLight();
    // West/north-facing walls face the light; east/south walls face away.
    assert(key.x > 0 && key.z > 0 && key.y < 0);
    double length = sqrt((double)key.x * key.x + (double)key.y * key.y + (double)key.z * key.z) / FX16_ONE;
    assert(fabs(length - 1) < 0.001);
    MtxFx43 view = {0};
    view._00 = view._11 = view._22 = FX32_ONE;
    MtxFx44 projection = {0};
    projection._00 = projection._11 = FX32_ONE;
    projection._23 = -FX32_ONE;
    VecFx32 tile = {0, 0, -100 * FX32_ONE};
    assert(FieldVisuals_WaterTileVisible(&tile, &view, &projection));
    tile.x = 119 * FX32_ONE; // Guard band retains partially visible shoreline.
    assert(FieldVisuals_WaterTileVisible(&tile, &view, &projection));
    tile.x = 121 * FX32_ONE;
    assert(!FieldVisuals_WaterTileVisible(&tile, &view, &projection));
    projection._00 = FX32_ONE / 2; // Widescreen includes more water.
    tile.x = 200 * FX32_ONE;
    assert(FieldVisuals_WaterTileVisible(&tile, &view, &projection));
    tile.x = 241 * FX32_ONE;
    assert(!FieldVisuals_WaterTileVisible(&tile, &view, &projection));
    tile.x = 0; tile.z = 100 * FX32_ONE;
    assert(!FieldVisuals_WaterTileVisible(&tile, &view, &projection));
    tile.z = 0; // No perspective division at the eye plane.
    assert(!FieldVisuals_WaterTileVisible(&tile, &view, &projection));
    // Preview changes render time only, cycles back to real time, and can reset.
    const int realTime = 13 * 3600 + 123;
    const int previewTimes[] = { 0, 6 * 3600, 12 * 3600, 18 * 3600, realTime };
    assert(FieldVisuals_GetTime(realTime) == realTime);
    assert(strcmp(FieldVisuals_GetTimePreviewName(), "Real time") == 0);
    for (int i = 0; i < sizeof(previewTimes) / sizeof(previewTimes[0]); i++) {
        FieldVisuals_CycleTimePreview();
        assert(FieldVisuals_GetTime(realTime) == previewTimes[i]);
    }
    FieldVisuals_CycleTimePreview();
    assert(strcmp(FieldVisuals_GetTimePreviewName(), "Night (00:00)") == 0);
    assert(FieldVisuals_GetTime(23 * 3600) == 0);
    FieldVisuals_ResetTimePreview();
    assert(FieldVisuals_GetTime(23 * 3600) == 23 * 3600);
    assert(strcmp(FieldVisuals_GetTimePreviewName(), "Real time") == 0);
    assert(FieldVisuals_IsOrdinaryWeather(0));
    assert(FieldVisuals_IsOrdinaryWeather(2));
    assert(FieldVisuals_IsOrdinaryWeather(6));
    // Blizzard, sandstorm, spirits, fog/deep fog and Flash must remain authored.
    const int protectedWeather[] = { 7, 9, 10, 12, 14, 15, 16, 23, 26, 32, -1 };
    for (int i = 0; i < sizeof(protectedWeather) / sizeof(protectedWeather[0]); i++) {
        assert(!FieldVisuals_IsOrdinaryWeather(protectedWeather[i]));
    }
    // Actual lighting archives: every second, every boundary and midnight.
    for (int archive = 0; archive < 4; archive++) {
        AreaLightTemplate previous, sample, midnight;
        FieldVisuals_SampleLight(fixtures[archive], counts[archive], 86399, &previous);
        FieldVisuals_SampleLight(fixtures[archive], counts[archive], 0, &midnight);
        for (int seconds = 0; seconds < 86400; seconds++) {
            FieldVisuals_SampleLight(fixtures[archive], counts[archive], seconds, &sample);
            for (int i = 0; i < GX_LIGHTS_COUNT; i++) {
                for (int shift = 0; shift <= 10; shift += 5) {
                    assert(abs(Channel(sample.lightColors[i], shift) - Channel(previous.lightColors[i], shift)) <= 1);
                }
                assert(sample.lightVectors[i].x >= -FX16_ONE && sample.lightVectors[i].x <= FX16_ONE);
                assert(sample.lightVectors[i].y >= -FX16_ONE && sample.lightVectors[i].y <= FX16_ONE);
                assert(sample.lightVectors[i].z >= -FX16_ONE && sample.lightVectors[i].z <= FX16_ONE);
            }
            for (int shift = 0; shift <= 10; shift += 5) {
                assert(abs(Channel(sample.ambientReflectColor, shift) - Channel(previous.ambientReflectColor, shift)) <= 1);
                assert(abs(Channel(sample.emissionColor, shift) - Channel(previous.emissionColor, shift)) <= 1);
            }
            previous = sample;
        }
        FieldVisuals_SampleLight(fixtures[archive], counts[archive], 86400, &sample);
        assert(memcmp(&sample, &midnight, sizeof(sample)) == 0);
        // Constant interior/cave archives must remain constant.
        if (archive != 0) {
            assert(sample.ambientReflectColor == fixtures[archive][0].ambientReflectColor);
        }
    }
    AreaLightTemplate empty;
    FieldVisuals_SampleLight(NULL, 0, 0, &empty);
    assert(empty.validLightsMask == 0);
    AreaLightTemplate singleton;
    FieldVisuals_SampleLight(fixtures[0], 1, 43200, &singleton);
    assert(memcmp(&singleton, fixtures[0], sizeof(singleton)) == 0);

    float priorBrightness = FieldVisuals_TimeBrightness(86399);
    FieldGrade prior, grade;
    FieldVisuals_Grade(86399, FIELD_ATMOSPHERE_PLAIN, 0, &prior);
    for (int seconds = 0; seconds < 86400; seconds++) {
        float brightness = FieldVisuals_TimeBrightness(seconds);
        assert(brightness >= 0.35f && brightness <= 1.0f && fabsf(brightness - priorBrightness) < 0.0001f);
        priorBrightness = brightness;
        // The grade changes continuously, including across midnight.
        FieldVisuals_Grade(seconds, FIELD_ATMOSPHERE_PLAIN, 0, &grade);
        assert(fabsf(grade.exposure - prior.exposure) < 0.001f && fabsf(grade.contrast - prior.contrast) < 0.001f);
        for (int c = 0; c < 3; c++)
            assert(fabsf(grade.shadows[c] - prior.shadows[c]) < 0.001f && fabsf(grade.highlights[c] - prior.highlights[c]) < 0.001f);
        prior = grade;
    }
    // Day is the original look; nights are darker with blue shadows;
    // sunsets have warm light and cool shadows.
    FieldGrade identity;
    FieldVisuals_IdentityGrade(&identity);
    FieldVisuals_Grade(12 * 3600, FIELD_ATMOSPHERE_PLAIN, 0, &grade);
    assert(memcmp(&grade, &identity, sizeof(grade)) == 0);
    FieldVisuals_Grade(0, FIELD_ATMOSPHERE_PLAIN, 0, &grade);
    assert(grade.exposure < 0.8f && grade.shadows[2] > grade.shadows[0]);
    FieldVisuals_Grade(18 * 3600, FIELD_ATMOSPHERE_PLAIN, 0, &grade);
    assert(grade.highlights[0] > grade.highlights[2] && grade.shadows[2] > grade.shadows[0]);
    FieldGrade rain;
    FieldVisuals_Grade(12 * 3600, FIELD_ATMOSPHERE_PLAIN, OVERWORLD_WEATHER_RAINING, &rain);
    assert(rain.saturation < 1 && rain.contrast < 1);

    float day[3], night[3], strength;
    for (int region = FIELD_ATMOSPHERE_PLAIN; region <= FIELD_ATMOSPHERE_COAST; region++) {
        FieldVisuals_Atmosphere(region, 0, 12 * 3600, TRUE, day, &strength);
        assert(strength > 0 && strength <= 0.3f);
        FieldVisuals_Atmosphere(region, 0, 0, TRUE, night, &strength);
        if (region == FIELD_ATMOSPHERE_CAVE) {
            assert(memcmp(day, night, sizeof(day)) == 0);
        } else {
            assert(night[0] < day[0] && night[1] < day[1] && night[2] < day[2]);
        }
    }
    SIM_Config_prj_type defaults = { 0 }, loaded = { 0 };
    SIM_Config_prj_LoadDefaults(&defaults);
    assert(defaults.enhancedDayNight && defaults.regionalAtmosphere && !defaults.antialias3D);
    assert(defaults.correctLighting && defaults.environmentalParticles && defaults.softContactShadows && defaults.castShadows && defaults.waterEffects
        && defaults.weatherEffects);
    assert(defaults.nightLighting && defaults.nightGlows && defaults.emissiveBloom);
    FILE *legacy = fopen("sim_config_prj.ini", "w");
    fputs("[General]\n60Fps=false\n[Cheats]\nWalkThroughWalls=false\n", legacy);
    fclose(legacy);
    loaded = defaults;
    assert(SIM_Config_prj_LoadConfigFile(&loaded));
    assert(!loaded.enable60fps && loaded.enhancedDayNight && loaded.regionalAtmosphere && !loaded.antialias3D);
    loaded.antialias3D = TRUE;
    SIM_Config_prj_SaveConfigFile(&loaded);
    SIM_Config_prj_type enabled = defaults;
    assert(SIM_Config_prj_LoadConfigFile(&enabled) && enabled.antialias3D);
    loaded.nightLighting = loaded.nightGlows = loaded.emissiveBloom = FALSE;
    loaded.enhancedDayNight = loaded.regionalAtmosphere = loaded.antialias3D = FALSE;
    loaded.correctLighting = loaded.environmentalParticles = loaded.softContactShadows = loaded.castShadows = loaded.waterEffects = loaded.weatherEffects = FALSE;
    SIM_Config_prj_SaveConfigFile(&loaded);
    SIM_Config_prj_type roundTrip = defaults;
    assert(SIM_Config_prj_LoadConfigFile(&roundTrip));
    assert(!roundTrip.enhancedDayNight && !roundTrip.regionalAtmosphere && !roundTrip.antialias3D);
    assert(!roundTrip.correctLighting && !roundTrip.environmentalParticles && !roundTrip.softContactShadows && !roundTrip.castShadows
        && !roundTrip.waterEffects && !roundTrip.weatherEffects);
    assert(!roundTrip.nightLighting && !roundTrip.nightGlows && !roundTrip.emissiveBloom);
    // World positions come back from clip x, y and w for any camera.
    {
        // A camera tilted 34 degrees down (row vectors), and a perspective
        // projection with clip w = -view z.
        const float sine = 0.5592f, cosine = 0.8290f;
        MtxFx43 view = { { FX32_ONE, 0, 0,
            0, (fx32)(cosine * FX32_ONE), (fx32)(sine * FX32_ONE),
            0, (fx32)(-sine * FX32_ONE), (fx32)(cosine * FX32_ONE),
            -2700 * FX32_ONE, -60 * FX32_ONE, -11800 * FX32_ONE } };
        MtxFx44 projection = { { (fx32)(1.2f * FX32_ONE), 0, 0, 0,
            0, (fx32)(1.6f * FX32_ONE), 0, 0,
            0, 0, (fx32)(-1.002f * FX32_ONE), -FX32_ONE,
            0, 0, (fx32)(-2.002f * FX32_ONE), 0 } };
        float matrix[9], offset[3], camera[3];
        FieldVisuals_WorldFromClip(&view, &projection, matrix, offset, camera);
        const float world[3] = { 2744, 31, 12040 };
        float clip[4] = { 0 };
        for (int j = 0; j < 4; j++) {
            for (int i = 0; i < 3; i++) {
                float v = 0;
                for (int k = 0; k < 3; k++)
                    v += world[k] * view.m[k][i] / 4096.0f;
                clip[j] += (v + view.m[3][i] / 4096.0f) * projection.m[i][j] / 4096.0f;
            }
            clip[j] += projection.m[3][j] / 4096.0f;
        }
        const float c[3] = { clip[0], clip[1], clip[3] };
        for (int i = 0; i < 3; i++) {
            float back = offset[i];
            for (int k = 0; k < 3; k++)
                back += matrix[i * 3 + k] * c[k];
            assert(fabsf(back - world[i]) < 0.05f);
        }
        // The camera is where the view transform is zero.
        for (int i = 0; i < 3; i++) {
            float v = view.m[3][i] / 4096.0f;
            for (int k = 0; k < 3; k++)
                v += camera[k] * view.m[k][i] / 4096.0f;
            assert(fabsf(v) < 0.05f);
        }
    }
    // Ground mist: forest and lake mist peaks at dawn and is gone by noon;
    // snow drifts all day; rain sprays.
    {
        float color[3], dawn, noon, depth;
        FieldVisuals_GroundMist(FIELD_ATMOSPHERE_FOREST, OVERWORLD_WEATHER_CLEAR, 6 * 3600, color, &dawn, &depth);
        FieldVisuals_GroundMist(FIELD_ATMOSPHERE_FOREST, OVERWORLD_WEATHER_CLEAR, 12 * 3600, color, &noon, &depth);
        assert(dawn > 0.4f && noon < 0.1f && depth > 40);
        FieldVisuals_GroundMist(FIELD_ATMOSPHERE_COAST, OVERWORLD_WEATHER_CLEAR, 12 * 3600, color, &noon, &depth);
        assert(noon == 0);
        FieldVisuals_GroundMist(FIELD_ATMOSPHERE_SNOW, OVERWORLD_WEATHER_CLEAR, 12 * 3600, color, &noon, &depth);
        assert(noon > 0.2f && depth < 24);
        FieldVisuals_GroundMist(FIELD_ATMOSPHERE_PLAIN, OVERWORLD_WEATHER_RAINING, 12 * 3600, color, &noon, &depth);
        assert(noon > 0);
        FieldVisuals_GroundMist(FIELD_ATMOSPHERE_PLAIN, OVERWORLD_WEATHER_CLEAR, 12 * 3600, color, &noon, &depth);
        assert(noon == 0);
    }
    assert(ROLE("lgreen_lm3") == FIELD_NIGHT_MATERIAL_PUDDLE_GROUND && ROLE("nsand") == FIELD_NIGHT_MATERIAL_PUDDLE_GROUND);
    assert(ROLE("ngrass_lm3") == FIELD_NIGHT_MATERIAL_NONE);
    // The light table: without field_lights.txt in the test's folder the
    // built-in copy is used; lamps, prop lights and day maps come from it.
    {
        assert(strstr(FieldLights_Status(), "built-in") != NULL);
        const FieldLamp *lamps;
        int lampCount = FieldLights_Lamps(&lamps), street = 0, down = 0;
        for (int i = 0; i < lampCount; i++) {
            street += lamps[i].kind == FIELD_LIGHT_STREET && lamps[i].map == MAP_HEADER_JUBILIFE_CITY;
            down += lamps[i].kind == FIELD_LIGHT_DOWNLIGHT && lamps[i].map == MAP_HEADER_ROUTE_206;
        }
        assert(street == 16 && down == 12);
        const FieldPropLight *props;
        int propCount = FieldLights_PropLights(&props), beacon = 0;
        for (int i = 0; i < propCount; i++)
            beacon += props[i].prop == 223 && props[i].kind == FIELD_LIGHT_BEACON && props[i].y == 132;
        assert(beacon == 1);
        assert(FieldVisuals_HasDayEmission(MAP_HEADER_ROUTE_227) && !FieldVisuals_HasDayEmission(MAP_HEADER_JUBILIFE_CITY));
        // A table with an error is rejected with its line and leaves the
        // current one in place.
        char error[160] = "";
        assert(!FieldLights_Parse("material sea water\nmaterial h_kage glow\n", error, sizeof(error)));
        assert(strstr(error, "line 2") && strstr(error, "role"));
        assert(!FieldLights_Parse("lamp street NOWHERE 0 0 0\n", error, sizeof(error)) && strstr(error, "map"));
        assert(ROLE("h_kage") == FIELD_NIGHT_MATERIAL_SHADOW);
        // A valid table replaces it: options, map names and first-match order.
        assert(FieldLights_Parse("# test\nmaterial glass window bloom=2 intensity=0.5 map=TWINLEAF_TOWN\n"
                                 "material glass source prop=land\nday_emission JUBILIFE_CITY\n", error, sizeof(error)));
        const FieldNightMaterialInfo *info = FieldVisuals_NightMaterial("glass_lm2", MAP_HEADER_TWINLEAF_TOWN, -1);
        assert(info && info->role == FIELD_NIGHT_MATERIAL_WINDOW && info->bloom == 2 && info->intensity == 0.5f);
        assert(FieldVisuals_NightMaterial("glass", MAP_HEADER_SANDGEM_TOWN, -1)->role == FIELD_NIGHT_MATERIAL_SOURCE);
        assert(FieldVisuals_NightMaterial("glass", MAP_HEADER_SANDGEM_TOWN, 5) == NULL);
        assert(ROLE("h_kage") == FIELD_NIGHT_MATERIAL_NONE && FieldVisuals_HasDayEmission(MAP_HEADER_JUBILIFE_CITY));
        assert(FieldLights_Reload() && ROLE("h_kage") == FIELD_NIGHT_MATERIAL_SHADOW);
    }
    puts("PASS: coherent night state, full-day lighting continuity, midnight, regional palettes and settings round trip");
    return 0;
}
