#include "port/evolution_wide.h"

#include <simulator/sim.h>
#include <stdlib.h>
#include <string.h>

#include "constants/narc.h"

#include "port/sim_config_prj.h"

#include "graphics.h"
#include "render_window.h"

// The scene's backdrop (egg_data) is a dome dithered over BG palettes 0-1: a
// ramp from dark grey through teal to a white floor. The wide view draws it
// as a smooth field of ramp positions instead, which the renderer colors from
// the live palette at window resolution, so fades and flashes still apply.
enum {
    NATIVE_WIDTH = 256,
    HEIGHT = 192,
    BACKDROP_ENTRIES = 32, // Palettes 0 and 1; entry 0 is the transparent backdrop.
    MESSAGE_BOX_BASE_TILE = 1,
};

// Ramp level (of 23) down the native art's center, per screen line.
static const struct {
    float line, level;
} sProfile[] = {
    { 0, 0.5f },
    { 16, 3.6f },
    { 32, 5.8f },
    { 48, 7.7f },
    { 64, 9.5f },
    { 80, 11.0f },
    { 96, 13.2f },
    { 112, 16.7f },
    { 128, 21.7f },
    { 136, 23.0f },
};
#define PROFILE_KNOTS (int)(sizeof(sProfile) / sizeof(sProfile[0]))
#define PROFILE_TOP 23.0f

// Contours sink this far toward the view's edges at the top, less toward the
// floor, so the floor is white along the whole message box row.
#define EDGE_DROP_TOP 12.0f
#define EDGE_DROP_FLOOR 4.0f

// Letterbox replacement: darkness grows with distance from the Pokémon
// (drawn at 128, 80), reaching VIGNETTE_STRENGTH as the bars fully close.
// The message box (BG1) and the yes/no menu (BG2) keep their brightness, as
// they did beside the bars.
#define LETTERBOX_DEPTH 40
#define VIGNETTE_X 128.0f
#define VIGNETTE_Y 80.0f
#define VIGNETTE_INNER 32.0f
#define VIGNETTE_OUTER 190.0f
#define VIGNETTE_STRENGTH 0.85f
#define VIGNETTE_KEEP_LAYERS (GX_PLANEMASK_BG1 | GX_PLANEMASK_BG2)

static BOOL sActive = FALSE;

static int ColorKey(u16 color)
{
    int r = color & 31, g = (color >> 5) & 31, b = (color >> 10) & 31;
    // Brightness first, then green: the ramp runs grey -> teal -> white.
    return ((r + g + b) << 10) | (g << 5) | b;
}

// The backdrop palettes' distinct colors, darkest first, as palette entries.
static int BuildRamp(const u16 *colors, u16 *entries)
{
    int count = 0;

    for (int entry = 1; entry < BACKDROP_ENTRIES; entry++) {
        u16 color = colors[entry] & 0x7FFF;
        int at = 0;

        while (at < count && ColorKey(colors[entries[at]] & 0x7FFF) < ColorKey(color)) {
            at++;
        }
        if (at < count && (colors[entries[at]] & 0x7FFF) == color) {
            continue;
        }
        memmove(entries + at + 1, entries + at, (count - at) * sizeof(entries[0]));
        entries[at] = (u16)entry;
        count++;
    }
    return count;
}

// Monotone cubic (Fritsch-Carlson) through the profile: no overshoot, so the
// gradient never brightens and darkens again.
static float ProfileLevel(float line)
{
    if (line <= sProfile[0].line) {
        return sProfile[0].level;
    }
    if (line >= sProfile[PROFILE_KNOTS - 1].line) {
        return sProfile[PROFILE_KNOTS - 1].level;
    }

    float secants[PROFILE_KNOTS - 1], tangents[PROFILE_KNOTS];
    for (int i = 0; i < PROFILE_KNOTS - 1; i++) {
        secants[i] = (sProfile[i + 1].level - sProfile[i].level) / (sProfile[i + 1].line - sProfile[i].line);
    }
    tangents[0] = secants[0];
    tangents[PROFILE_KNOTS - 1] = 0.0f; // The floor stays flat.
    for (int i = 1; i < PROFILE_KNOTS - 1; i++) {
        float a = secants[i - 1], b = secants[i];
        tangents[i] = a * b <= 0.0f ? 0.0f : 2.0f * a * b / (a + b);
    }

    int i = 0;
    while (line >= sProfile[i + 1].line) {
        i++;
    }
    float h = sProfile[i + 1].line - sProfile[i].line;
    float t = (line - sProfile[i].line) / h;
    float t2 = t * t, t3 = t2 * t;

    return (2 * t3 - 3 * t2 + 1) * sProfile[i].level
        + (t3 - 2 * t2 + t) * h * tangents[i]
        + (-2 * t3 + 3 * t2) * sProfile[i + 1].level
        + (t3 - t2) * h * tangents[i + 1];
}

static BOOL SetSmoothBackdrop(u32 width, const u16 *colors)
{
    u16 entries[BACKDROP_ENTRIES];
    int numEntries = BuildRamp(colors, entries);
    u16 *field = malloc(width * HEIGHT * sizeof(u16));

    if (numEntries < 2 || field == NULL) {
        free(field);
        return FALSE;
    }

    for (u32 y = 0; y < HEIGHT; y++) {
        float line = y + 0.5f;
        float floorBlend = line < 144.0f ? line / 144.0f : 1.0f;
        float drop = EDGE_DROP_TOP + (EDGE_DROP_FLOOR - EDGE_DROP_TOP) * floorBlend;

        for (u32 x = 0; x < width; x++) {
            float u = (x + 0.5f - width * 0.5f) / (width * 0.5f);
            float level = ProfileLevel(line - drop * u * u) / PROFILE_TOP;

            level = level < 0.0f ? 0.0f : (level > 1.0f ? 1.0f : level);
            field[y * width + x] = (u16)(level * 65535.0f + 0.5f);
        }
    }

    SIM_SetWideUISmoothBg(BG_LAYER_MAIN_3, field, width, HEIGHT, entries, numEntries);
    free(field);
    return TRUE;
}

BOOL EvolutionWide_Begin(void)
{
    if (!SIM_Config_prj_GetConfig()->widescreenEvolution) {
        return FALSE;
    }
    if (SIM_SetWideUI(TRUE) <= NATIVE_WIDTH) {
        SIM_SetWideUI(FALSE);
        return FALSE;
    }
    // Particles that the native screen's edges cut off carry on to the view's.
    SIM_SetWideUI3D(TRUE);
    sActive = TRUE;
    return TRUE;
}

void EvolutionWide_Setup(BgConfig *bgConfig, PaletteData *paletteData, int messageBoxFrame, enum HeapID heapID)
{
    if (!sActive) {
        return;
    }
    // The view can't change width between Begin and here; this reads it back.
    if (!SetSmoothBackdrop(SIM_SetWideUI(TRUE), PaletteData_GetUnfadedBuffer(paletteData, PLTTBUF_MAIN_BG))) {
        return;
    }
    // The clamp window (BG1 and sprites only outside it) cut the backdrop off
    // under the message box and drew the letterbox bars. The dome now runs
    // down behind the box, and the bars are a vignette, so nothing is clipped.
    G2_SetWndOutsidePlane(GX_WND_PLANEMASK_BG0 | GX_WND_PLANEMASK_BG1 | GX_WND_PLANEMASK_BG2 | GX_WND_PLANEMASK_BG3 | GX_WND_PLANEMASK_OBJ, 0);
    // The box's rounded corners were filled with a dark color for the black
    // band behind it; the dome's floor shows through them instead.
    Graphics_LoadTilesToBgLayer(NARC_INDEX_GRAPHIC__PL_WINFRAME, GetMessageBoxTilesNARCMember(messageBoxFrame), bgConfig, BG_LAYER_MAIN_1, MESSAGE_BOX_BASE_TILE, 0, FALSE, heapID);
}

void EvolutionWide_ReplaceLetterbox(int windowTop)
{
    if (!sActive) {
        return;
    }
    float closed = windowTop <= 0 ? 0.0f : (windowTop >= LETTERBOX_DEPTH ? 1.0f : windowTop / (float)LETTERBOX_DEPTH);

    SIM_SetVignette(VIGNETTE_X, VIGNETTE_Y, VIGNETTE_INNER, VIGNETTE_OUTER, VIGNETTE_STRENGTH * closed, VIGNETTE_KEEP_LAYERS);
}

void EvolutionWide_End(void)
{
    if (!sActive) {
        return;
    }
    SIM_SetWideUISmoothBg(-1, NULL, 0, 0, NULL, 0);
    SIM_SetVignette(0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0);
    SIM_SetWideUI(FALSE);
    sActive = FALSE;
}
