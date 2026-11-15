#include "port/visuals/field_visuals.h"
#include "overlay005/area_light.h"

#include <string.h>
#include <math.h>

#include "constants/overworld_weather.h"
#include "generated/map_headers.h"
#include "port/visuals/field_lights.h"

#define DAY_SECONDS 86400

static const struct {
    const char *name;
    int seconds;
} sTimePreviews[] = {
    { "Real time", -1 },
    { "Night (00:00)", 0 },
    { "Dawn (06:00)", 6 * 3600 },
    { "Day (12:00)", 12 * 3600 },
    { "Sunset (18:00)", 18 * 3600 },
};
static unsigned int sTimePreview;

int FieldVisuals_GetTime(int realSeconds)
{
    return sTimePreviews[sTimePreview].seconds < 0 ? realSeconds : sTimePreviews[sTimePreview].seconds;
}

void FieldVisuals_CycleTimePreview(void)
{
    sTimePreview = (sTimePreview + 1) % (sizeof(sTimePreviews) / sizeof(sTimePreviews[0]));
}

void FieldVisuals_ResetTimePreview(void)
{
    sTimePreview = 0;
}

const char *FieldVisuals_GetTimePreviewName(void)
{
    return sTimePreviews[sTimePreview].name;
}

static float SmoothStep(float t)
{
    if (t < 0) {
        return 0;
    }
    if (t > 1) {
        return 1;
    }
    return t * t * (3 - 2 * t);
}

static int WrapTime(int seconds)
{
    seconds %= DAY_SECONDS;
    return seconds < 0 ? seconds + DAY_SECONDS : seconds;
}

static GXRgb BlendColor(GXRgb a, GXRgb b, float t)
{
    int r = (a & 31) + ((b & 31) - (a & 31)) * t + 0.5f;
    int g = ((a >> 5) & 31) + (((b >> 5) & 31) - ((a >> 5) & 31)) * t + 0.5f;
    int bl = ((a >> 10) & 31) + (((b >> 10) & 31) - ((a >> 10) & 31)) * t + 0.5f;
    return GX_RGB(r, g, bl);
}

static void BlendLight(const AreaLightTemplate *a, const AreaLightTemplate *b, float t, AreaLightTemplate *result)
{
    *result = *a;
    result->validLightsMask = a->validLightsMask | b->validLightsMask;
    for (int i = 0; i < GX_LIGHTS_COUNT; i++) {
        BOOL enabledA = (a->validLightsMask & (1 << i)) != 0;
        BOOL enabledB = (b->validLightsMask & (1 << i)) != 0;
        result->lightColors[i] = BlendColor(enabledA ? a->lightColors[i] : 0, enabledB ? b->lightColors[i] : 0, t);
        // Fade a newly enabled light's color, keeping its authored direction.
        const VecFx16 *va = enabledA ? &a->lightVectors[i] : &b->lightVectors[i];
        const VecFx16 *vb = enabledB ? &b->lightVectors[i] : va;
        result->lightVectors[i].x = va->x + (vb->x - va->x) * t;
        result->lightVectors[i].y = va->y + (vb->y - va->y) * t;
        result->lightVectors[i].z = va->z + (vb->z - va->z) * t;
    }
    result->diffuseReflectColor = BlendColor(a->diffuseReflectColor, b->diffuseReflectColor, t);
    result->ambientReflectColor = BlendColor(a->ambientReflectColor, b->ambientReflectColor, t);
    result->specularReflectColor = BlendColor(a->specularReflectColor, b->specularReflectColor, t);
    result->emissionColor = BlendColor(a->emissionColor, b->emissionColor, t);
}

void FieldVisuals_SampleLight(const AreaLightTemplate *templates, int count, int seconds, AreaLightTemplate *result)
{
    memset(result, 0, sizeof(*result));
    if (count <= 0) {
        return;
    }
    int time = WrapTime(seconds);
    // Some archives start with a zero-length midnight entry.
    int first = templates[0].endTime == 0 && count > 1 ? 1 : 0;
    int active = first;
    while (active + 1 < count && time >= (int)templates[active].endTime * 2) {
        active++;
    }
    *result = templates[active];
    if (count - first < 2) {
        return;
    }

    int previous = active == first ? count - 1 : active - 1;
    int next = active + 1 == count ? first : active + 1;
    int start = active == first ? 0 : templates[active - 1].endTime * 2;
    int end = templates[active].endTime * 2;
    int previousStart = previous == first ? 0 : templates[previous - 1].endTime * 2;
    int nextStart = next == first ? 0 : end;
    int previousDuration = templates[previous].endTime * 2 - previousStart;
    int nextDuration = templates[next].endTime * 2 - nextStart;
    // A 30-minute blend centered on each boundary, shortened for brief entries.
    int startHalf = (end - start) / 2;
    if (startHalf > previousDuration / 2) {
        startHalf = previousDuration / 2;
    }
    if (startHalf > 900) {
        startHalf = 900;
    }
    int endHalf = (end - start) / 2;
    if (endHalf > nextDuration / 2) {
        endHalf = nextDuration / 2;
    }
    if (endHalf > 900) {
        endHalf = 900;
    }
    if (startHalf > 0 && time - start < startHalf) {
        BlendLight(&templates[previous], &templates[active], SmoothStep((float)(time - start + startHalf) / (2 * startHalf)), result);
    } else if (endHalf > 0 && end - time <= endHalf) {
        BlendLight(&templates[active], &templates[next], SmoothStep((float)(time - end + endHalf) / (2 * endHalf)), result);
    }
}

float FieldVisuals_TimeBrightness(int seconds)
{
    static const struct {
        int hour;
        float brightness;
    } keys[] = {
        { 0, 0.35f },
        { 4, 0.35f },
        { 6, 0.72f },
        { 9, 1.00f },
        { 16, 1.00f },
        { 18, 0.72f },
        { 20, 0.35f },
        { 24, 0.35f },
    };
    int time = WrapTime(seconds);
    int i = 0;
    while (time >= keys[i + 1].hour * 3600) {
        i++;
    }
    float t = SmoothStep((float)(time - keys[i].hour * 3600) / ((keys[i + 1].hour - keys[i].hour) * 3600));
    return keys[i].brightness + (keys[i + 1].brightness - keys[i].brightness) * t;
}

void FieldVisuals_IdentityGrade(FieldGrade *grade)
{
    *grade = (FieldGrade) { 1, 1, 1, { 1, 1, 1 }, { 1, 1, 1 } };
}

void FieldVisuals_Grade(int seconds, enum FieldAtmosphereRegion region, int weather, FieldGrade *grade)
{
    // The archived light palettes already darken and tint the scene; the
    // grade shapes them. Evenings get warm light with cool shadows rather
    // than one sepia cast, nights keep material colors apart instead of
    // turning uniformly blue.
    static const struct {
        float hour;
        FieldGrade grade;
    } keys[] = {
        { 0.0f, { 0.68f, 1.00f, 1.12f, { 0.54f, 0.64f, 1.06f }, { 0.88f, 0.92f, 1.04f } } },
        { 4.5f, { 0.68f, 1.00f, 1.12f, { 0.54f, 0.64f, 1.06f }, { 0.88f, 0.92f, 1.04f } } },
        { 6.5f, { 0.86f, 0.96f, 1.05f, { 0.78f, 0.80f, 1.06f }, { 1.07f, 0.98f, 0.94f } } },
        { 8.5f, { 1.00f, 1.00f, 1.00f, { 1.00f, 1.00f, 1.00f }, { 1.00f, 1.00f, 1.00f } } },
        { 16.5f, { 1.00f, 1.00f, 1.00f, { 1.00f, 1.00f, 1.00f }, { 1.00f, 1.00f, 1.00f } } },
        { 18.0f, { 0.97f, 0.98f, 1.07f, { 0.72f, 0.78f, 1.10f }, { 1.06f, 0.98f, 0.90f } } },
        { 19.5f, { 0.72f, 0.94f, 1.12f, { 0.64f, 0.72f, 1.08f }, { 1.00f, 0.94f, 0.96f } } },
        { 21.0f, { 0.68f, 1.00f, 1.12f, { 0.54f, 0.64f, 1.06f }, { 0.88f, 0.92f, 1.04f } } },
        { 24.0f, { 0.68f, 1.00f, 1.12f, { 0.54f, 0.64f, 1.06f }, { 0.88f, 0.92f, 1.04f } } },
    };
    float hour = WrapTime(seconds) / 3600.0f;
    int i = 0;
    while (hour >= keys[i + 1].hour) {
        i++;
    }
    const FieldGrade *a = &keys[i].grade, *b = &keys[i + 1].grade;
    float t = SmoothStep((hour - keys[i].hour) / (keys[i + 1].hour - keys[i].hour));
#define LERP(field) (a->field + (b->field - a->field) * t)
    grade->exposure = LERP(exposure);
    grade->saturation = LERP(saturation);
    grade->contrast = LERP(contrast);
    for (int c = 0; c < 3; c++) {
        grade->shadows[c] = LERP(shadows[c]);
        grade->highlights[c] = LERP(highlights[c]);
    }
#undef LERP
    if (weather >= OVERWORLD_WEATHER_RAINING && weather <= OVERWORLD_WEATHER_THUNDERSTORM) {
        // Overcast: duller colors, flatter light, slightly cooler shadows.
        static const float rainShadows[3] = { 0.96f, 0.98f, 1.03f };
        grade->saturation *= 0.85f;
        grade->contrast *= 0.96f;
        for (int c = 0; c < 3; c++)
            grade->shadows[c] *= rainShadows[c];
    } else if (region == FIELD_ATMOSPHERE_SNOW) {
        // Snow light stays crisp and slightly cool.
        static const float snowHighlights[3] = { 0.98f, 1.00f, 1.03f };
        for (int c = 0; c < 3; c++)
            grade->highlights[c] *= snowHighlights[c];
    }
}

BOOL FieldVisuals_IsOrdinaryWeather(int weather)
{
    return (weather >= OVERWORLD_WEATHER_CLEAR && weather <= OVERWORLD_WEATHER_HEAVY_SNOW)
        || weather == OVERWORLD_WEATHER_CLEAR_8 || weather == OVERWORLD_WEATHER_HAILING
        || weather == OVERWORLD_WEATHER_CLEAR_13;
}

void FieldVisuals_GroundMist(enum FieldAtmosphereRegion region, int weather, int seconds, float color[3], float *strength, float *depth)
{
    float hour = WrapTime(seconds) / 3600.0f;
    // Morning mist gathers before sunrise and burns off by mid-morning.
    float morning = SmoothStep((hour - 4.5f) / 1.0f) * (1 - SmoothStep((hour - 7.5f) / 1.5f));
    static const float white[3] = { 0.92f, 0.95f, 1.0f }, forest[3] = { 0.82f, 0.90f, 0.84f };
    memcpy(color, white, sizeof(white));
    *strength = 0;
    *depth = 24;
    if (region == FIELD_ATMOSPHERE_SNOW) {
        // Fine snow drifting low over the ground, more while it snows. Over
        // white snow it shows on everything darker standing in it.
        *strength = weather == OVERWORLD_WEATHER_SNOWING || weather == OVERWORLD_WEATHER_HEAVY_SNOW ? 0.45f : 0.32f;
        *depth = 16;
    } else if (region == FIELD_ATMOSPHERE_FOREST) {
        // Deep enough to wrap the trees, which hide the forest floor.
        memcpy(color, forest, sizeof(forest));
        *strength = 0.05f + 0.40f * morning;
        *depth = 56;
    } else if (region == FIELD_ATMOSPHERE_COAST) {
        *strength = 0.25f * morning;
        *depth = 56;
    }
    if (weather >= OVERWORLD_WEATHER_RAINING && weather <= OVERWORLD_WEATHER_THUNDERSTORM) {
        // Spray from the rain.
        *strength = fmaxf(*strength, 0.10f);
    }
}

void FieldVisuals_Atmosphere(enum FieldAtmosphereRegion region, int weather, int seconds, BOOL dayNight, float color[3], float *strength)
{
    static const float profiles[][4] = {
        { 0.68f, 0.76f, 0.84f, 0.15f },
        { 0.59f, 0.73f, 0.65f, 0.28f },
        { 0.76f, 0.84f, 0.94f, 0.30f },
        { 0.28f, 0.32f, 0.40f, 0.20f },
        { 0.68f, 0.80f, 0.86f, 0.22f },
    };
    if (region < FIELD_ATMOSPHERE_PLAIN || region > FIELD_ATMOSPHERE_COAST) {
        region = FIELD_ATMOSPHERE_PLAIN;
    }
    memcpy(color, profiles[region], sizeof(float) * 3);
    *strength = profiles[region][3];
    if (weather >= OVERWORLD_WEATHER_RAINING && weather <= OVERWORLD_WEATHER_THUNDERSTORM) {
        color[0] = 0.57f;
        color[1] = 0.64f;
        color[2] = 0.73f;
        *strength += 0.06f;
    }
    if (dayNight && region != FIELD_ATMOSPHERE_CAVE) {
        // Haze dims at night; the scene's grade supplies its tint.
        float brightness = FieldVisuals_TimeBrightness(seconds);
        for (int i = 0; i < 3; i++) {
            color[i] *= brightness;
        }
    }
}

VecFx16 FieldVisuals_OutdoorKeyLight(void)
{
    // Incident rays travel southeast/down: the source is northwest/above.
    VecFx16 direction = { FX16_ONE / 2, -2896, FX16_ONE / 2 };
    return direction;
}

BOOL FieldVisuals_WaterTileVisible(const VecFx32 *position, const MtxFx43 *view, const MtxFx44 *projection)
{
    float world[4] = { position->x / (float)FX32_ONE, position->y / (float)FX32_ONE, position->z / (float)FX32_ONE, 1 };
    float camera[4] = { 0, 0, 0, 1 }, clip[4] = { 0 };
    for (int axis = 0; axis < 3; axis++) {
        for (int j = 0; j < 4; j++) {
            camera[axis] += world[j] * view->m[j][axis] / FX32_ONE;
        }
    }
    for (int axis = 0; axis < 4; axis++) {
        for (int j = 0; j < 4; j++) {
            clip[axis] += camera[j] * projection->m[j][axis] / FX32_ONE;
        }
    }
    // Conservative guard band includes tile corners and a small camera drift.
    float marginX = fabsf(projection->_00 / (float)FX32_ONE) * 20;
    float marginY = fabsf(projection->_11 / (float)FX32_ONE) * 20;
    return clip[3] > 0 && fabsf(clip[0]) <= clip[3] + marginX
        && fabsf(clip[1]) <= clip[3] + marginY;
}

void FieldVisuals_NightLighting(int seconds, FieldNightLighting *state)
{
    float hour = WrapTime(seconds) / 3600.0f;
    float dawn = SmoothStep((hour - 5.0f) / 2.0f);
    float dusk = 1.0f - SmoothStep((hour - 18.0f) / 2.0f);
    float sun = dawn < dusk ? dawn : dusk;
    state->sunIntensity = sun;
    state->moonIntensity = (1 - sun) * 0.14f;
    state->emissionActivation = 1 - sun;
    state->directionalShadowStrength = 0.04f + sun * 0.96f;
    state->contactShadowStrength = 0.34f + sun * 0.24f;
    state->treeShadowStrength = 0.35f + sun * 0.65f;
}

float FieldVisuals_SunShadowLength(int seconds)
{
    // 0.5 at noon, growing to 0.9 as the sun sets or rises.
    float hour = WrapTime(seconds) / 3600.0f;
    float altitude = sinf(3.14159265f * (hour - 6) / 12);
    return 0.5f * (1 + 0.8f * (1 - fmaxf(altitude, 0)));
}

// The materials live in the light table, field_lights.txt.
const FieldNightMaterialInfo *FieldVisuals_NightMaterial(const char name[16], int map, int prop)
{
    return FieldLights_Material(name, map, prop);
}
static void Invert3(const double m[3][3], double out[3][3])
{
    double det = m[0][0] * (m[1][1] * m[2][2] - m[1][2] * m[2][1]) - m[0][1] * (m[1][0] * m[2][2] - m[1][2] * m[2][0])
        + m[0][2] * (m[1][0] * m[2][1] - m[1][1] * m[2][0]);
    for (int i = 0; i < 3; i++)
        for (int j = 0; j < 3; j++) {
            int a = (j + 1) % 3, b = (j + 2) % 3, c = (i + 1) % 3, d = (i + 2) % 3;
            out[i][j] = (m[a][c] * m[b][d] - m[a][d] * m[b][c]) / det;
        }
}
void FieldVisuals_WorldFromClip(const MtxFx43 *view, const MtxFx44 *projection, float matrix[9], float offset[3], float camera[3])
{
    // Row vectors: clip = [world 1] * view * projection. Clip x, y and w are
    // affine in the world position, so they invert to it.
    double v[4][4] = { { 0 } }, vp[4][4] = { { 0 } };
    for (int i = 0; i < 4; i++) {
        for (int j = 0; j < 3; j++)
            v[i][j] = view->m[i][j] / (double)FX32_ONE;
        v[i][3] = i == 3;
    }
    for (int i = 0; i < 4; i++)
        for (int j = 0; j < 4; j++)
            for (int k = 0; k < 4; k++)
                vp[i][j] += v[i][k] * projection->m[k][j] / (double)FX32_ONE;
    const int columns[3] = { 0, 1, 3 };
    double b[3][3], inverse[3][3], t[3];
    for (int k = 0; k < 3; k++) {
        for (int i = 0; i < 3; i++)
            b[i][k] = vp[i][columns[k]];
        t[k] = vp[3][columns[k]];
    }
    // clip = world * B + t, so world = (clip - t) * B^-1; as a column-vector
    // matrix that is world = M * clip + offset with M = (B^-1)^T.
    Invert3(b, inverse);
    for (int i = 0; i < 3; i++) {
        offset[i] = 0;
        for (int k = 0; k < 3; k++) {
            matrix[i * 3 + k] = (float)inverse[k][i];
            offset[i] -= (float)(inverse[k][i] * t[k]);
        }
    }
    // The camera sits where the view transform is zero: world * R + T = 0.
    double r[3][3], rInverse[3][3];
    for (int i = 0; i < 3; i++)
        for (int j = 0; j < 3; j++)
            r[i][j] = v[i][j];
    Invert3(r, rInverse);
    for (int j = 0; j < 3; j++) {
        camera[j] = 0;
        for (int k = 0; k < 3; k++)
            camera[j] -= (float)(v[3][k] * rInverse[k][j]);
    }
}

BOOL FieldVisuals_HasDayEmission(int map)
{
    return FieldLights_HasDayEmission(map);
}
static BOOL sEmissionDebug;
void FieldVisuals_SetEmissionDebug(BOOL enabled)
{
    sEmissionDebug = enabled;
}
BOOL FieldVisuals_IsEmissionDebug(void)
{
    return sEmissionDebug;
}

// Move toward the camera without changing the source's projected center. The
// returned scale also preserves the halo's apparent size in perspective views.
float FieldVisuals_PositionHalo(const VecFx32 *source, const MtxFx43 *view, float offset, BOOL perspective, VecFx32 *halo)
{
    *halo = *source;
    double r[3][3], camera[3], cross[3][3];
    for (int axis = 0; axis < 3; axis++) {
        camera[axis] = view->m[3][axis] / (double)FX32_ONE;
        for (int j = 0; j < 3; j++) {
            r[axis][j] = view->m[j][axis] / (double)FX32_ONE;
            camera[axis] += ((const fx32 *)source)[j] / (double)FX32_ONE * r[axis][j];
        }
    }
    // Invert the actual fixed-point rotation rather than assuming it is an
    // exactly orthonormal matrix after quantization.
    for (int axis = 0; axis < 3; axis++) {
        int a = (axis + 1) % 3, b = (axis + 2) % 3;
        for (int j = 0; j < 3; j++) {
            int x = (j + 1) % 3, y = (j + 2) % 3;
            cross[axis][j] = r[a][x] * r[b][y] - r[a][y] * r[b][x];
        }
    }
    double det = r[0][0] * cross[0][0] + r[0][1] * cross[0][1] + r[0][2] * cross[0][2];
    double distance = sqrt(camera[0] * camera[0] + camera[1] * camera[1] + camera[2] * camera[2]);
    if (fabs(det) < 1e-8 || distance < 1 || offset <= 0) return 1;
    double amount = fmin(offset, distance * 0.5) / distance;
    double delta[3] = { -camera[0] * amount, -camera[1] * amount, -camera[2] * amount };
    if (!perspective) {
        delta[0] = delta[1] = 0;
        delta[2] = -copysign(fmin(offset, fabs(camera[2]) * 0.5), camera[2]);
    }
    for (int j = 0; j < 3; j++) {
        double move = (delta[0] * cross[0][j] + delta[1] * cross[1][j] + delta[2] * cross[2][j]) / det;
        ((fx32 *)halo)[j] += (fx32)llround(move * FX32_ONE);
    }
    return perspective ? (float)(1 - amount) : 1;
}
