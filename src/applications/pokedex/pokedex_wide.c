#include "applications/pokedex/pokedex_wide.h"

#include <nitro.h>
#include <string.h>

#ifdef SDK_PORT
#include <simulator/sim.h>

#include "port/sim_config_prj.h"
#endif

#include "applications/pokedex/pokedex_tabs.h"

#include "bg_window.h"
#include "heap.h"
#include "render_view.h"
#include "screen_fade.h"

static int sWidth = 256;
static BOOL sEnabled = FALSE;
static BOOL sActive = FALSE;
static BOOL sHoldBlack = FALSE;
// A tab change's snapshot is up (SIM_HoldMainScreen), or asked for.
static BOOL sHeld = FALSE;
// The next fade-out holds the whole screen, strip and all: opening an entry from the
// grid, or closing it, crossfades between the two like changing tabs does.
static BOOL sCrossfadeScreen = FALSE;
// A fade-in blended from a hold since the last fade-out: the screens' other fade-ins
// (the entry's strip and its page each have one) are done at once, not from black.
static BOOL sCrossfadedIn = FALSE;
static PokedexWideBlackFunc sBlackFunc = NULL;

static void SetLitStrip(BOOL keepStrip);

void PokedexWide_LatchEnabled(void)
{
#ifdef SDK_PORT
    sEnabled = SIM_Config_prj_GetConfig()->widescreenPokedex;
#endif
}

BOOL PokedexWide_IsEnabled(void)
{
    return sEnabled;
}

int PokedexWide_Begin(void)
{
    sActive = TRUE;
#ifdef SDK_PORT
    sWidth = SIM_SetWideUI(TRUE);
    RenderView_SetHorizontalMargin((sWidth - 256) / 2);
#else
    sWidth = 256;
#endif
    return sWidth;
}

void PokedexWide_End(void)
{
#ifdef SDK_PORT
    // Leaving an entry under a page's hold: the snapshot covers the strip as well
    // while the entry is torn down, until the next screen fades in from it.
    if (sHeld) {
        SIM_SetHeldMainScreenFirstLine(0);
    }

    SIM_SetWideUI3DOffset(0);
    SIM_SetWideUI(FALSE);
    RenderView_SetHorizontalMargin(0);
#endif
    sWidth = 256;
    sActive = FALSE;
    SetLitStrip(FALSE);
    // The screen stays black (sHoldBlack) into the next screen's fade-in.
}

BOOL PokedexWide_IsActive(void)
{
    return sActive;
}

BOOL PokedexWide_AdjustFade(u8 *numSteps, GXBlendPlaneMask *plane1, GXBlendPlaneMask *plane2)
{
    const BOOL keepStrip = (*plane1 & GX_BLEND_PLANEMASK_BG2) == 0;

    *plane1 = POKEDEX_WIDE_FADE_PLANES;
    *plane2 = POKEDEX_WIDE_FADE_TARGETS;

    // Platinum's info screens cut some fades to one step.
    if (*numSteps < 3) {
        *numSteps = 3;
    }

    return keepStrip;
}

static void SetLitStrip(BOOL keepStrip)
{
#ifdef SDK_PORT
    SIM_SetMasterBrightnessFirstLine(keepStrip ? POKEDEX_TAB_STRIP_HEIGHT : 0);
#endif
}

static void RunBlackFunc(void)
{
    if (sBlackFunc) {
        PokedexWideBlackFunc func = sBlackFunc;

        sBlackFunc = NULL;
        func();
    }
}

static void ReleaseHold(u32 frames)
{
    if (sHeld) {
#ifdef SDK_PORT
        SIM_ReleaseMainScreen(frames);
#endif
        sHeld = FALSE;
    }
}

static BOOL IsHidden(void)
{
#ifdef SDK_PORT
    if (sHeld && SIM_IsMainScreenHeld()) {
        return TRUE;
    }
#endif
    return sHoldBlack;
}

BOOL PokedexWide_FadeStep(int brightness, BOOL keepStrip)
{
    // An app-level screen fade (entering or leaving the Pokédex) owns master brightness.
    if (!IsScreenFadeDone()) {
        return FALSE;
    }

    const BOOL black = brightness <= -16;

    SetLitStrip(keepStrip);
    GX_SetMasterBrightness(brightness);

    // A held snapshot fades with the screen; once it is black, nothing shows it.
    if (black) {
        ReleaseHold(0);
    }

    if (black && !sHoldBlack) {
        RunBlackFunc();
    }

    sHoldBlack = black;
    return TRUE;
}

BOOL PokedexWide_CrossfadeStep(BOOL fadeIn, BOOL keepStrip, BOOL *done)
{
#ifdef SDK_PORT
    if (!IsScreenFadeDone()) {
        return FALSE;
    }

    if (fadeIn) {
        // Whatever fades in after a hold blends in from the old screen.
        if (sHeld) {
            SetLitStrip(FALSE);
            GX_SetMasterBrightness(0);
            ReleaseHold(POKEDEX_WIDE_CROSSFADE_FRAMES);
            sCrossfadedIn = TRUE;
            *done = TRUE;
            return TRUE;
        }

        if (sCrossfadedIn) {
            *done = TRUE;
            return TRUE;
        }

        return FALSE;
    }

    sCrossfadedIn = FALSE;

    // A fade-out that started a hold sees it through, and so do the others under it.
    if (!sHeld && ((!keepStrip && !sCrossfadeScreen) || sHoldBlack)) {
        return FALSE;
    }

    if (!sHeld) {
        SIM_HoldMainScreen(sCrossfadeScreen ? 0 : POKEDEX_TAB_STRIP_HEIGHT);
        sHeld = TRUE;
        sCrossfadeScreen = FALSE;
    }

    *done = SIM_IsMainScreenHeld();

    if (*done) {
        RunBlackFunc();
    }

    return TRUE;
#else
    return FALSE;
#endif
}

void PokedexWide_CrossfadeScreen(void)
{
    sCrossfadeScreen = TRUE;
}

BOOL PokedexWide_IsHoldingBlack(void)
{
    return sHoldBlack;
}

void PokedexWide_RunAtBlack(PokedexWideBlackFunc func)
{
    if (IsHidden()) {
        func();
    } else {
        sBlackFunc = func;
    }
}

void PokedexWide_ApplyHeldFade(void)
{
    if (sHoldBlack) {
        GX_SetMasterBrightness(-16);
    }
}

void PokedexWide_ResetFade(void)
{
    ReleaseHold(0);
    sCrossfadeScreen = FALSE;
    sCrossfadedIn = FALSE;
    sHoldBlack = FALSE;
    sBlackFunc = NULL;
    SetLitStrip(FALSE);
}

int PokedexWide_Width(void)
{
    return sWidth;
}

int PokedexWide_Left(void)
{
    return (sWidth - 256) / 2;
}

int PokedexWide_TileWidth(void)
{
    return sWidth / 8;
}

void PokedexWide_Set3DOffset(int dots)
{
#ifdef SDK_PORT
    SIM_SetWideUI3DOffset(dots);
#endif
}

// The layout of pokedex_graphics.c's InitBackgrounds. The wide screens take the
// VRAM between the native screens and the first character base (0x4000): BG3 reuses
// its native screen at 0x1000, BG2 and BG1 sit at 0x2000 and 0x3000.
static const struct {
    u8 screenBase;
    u8 wideScreenBase;
    u8 charBase;
    u8 priority;
} sLayers[3] = {
    { GX_BG_SCRBASE_0x0000, GX_BG_SCRBASE_0x3000, GX_BG_CHARBASE_0x04000, 0 },
    { GX_BG_SCRBASE_0x0800, GX_BG_SCRBASE_0x2000, GX_BG_CHARBASE_0x0c000, 1 },
    { GX_BG_SCRBASE_0x1000, GX_BG_SCRBASE_0x1000, GX_BG_CHARBASE_0x14000, 3 },
};

void PokedexWide_ConfigureLayer(BgConfig *bgConfig, u8 layer, BOOL wide)
{
    GF_ASSERT(layer >= BG_LAYER_MAIN_1 && layer <= BG_LAYER_MAIN_3);

    const int index = layer - BG_LAYER_MAIN_1;
    BgTemplate template = {
        .x = 0,
        .y = 0,
        .bufferSize = wide ? 0x1000 : 0x800,
        .baseTile = 0,
        .screenSize = wide ? BG_SCREEN_SIZE_512x256 : BG_SCREEN_SIZE_256x256,
        .colorMode = GX_BG_COLORMODE_16,
        .screenBase = wide ? sLayers[index].wideScreenBase : sLayers[index].screenBase,
        .charBase = sLayers[index].charBase,
        .bgExtPltt = GX_BG_EXTPLTT_01,
        .priority = sLayers[index].priority,
        .areaOver = 0,
        .mosaic = 0
    };

    // Cleared tilemaps show tile 0, which other Pokédex screens may have drawn into.
    static const u8 blankTile[32] = { 0 };

    Bg_FreeTilemapBuffer(bgConfig, layer);
    Bg_InitFromTemplate(bgConfig, layer, &template, BG_TYPE_STATIC);
    Bg_LoadTiles(bgConfig, layer, (void *)blankTile, sizeof(blankTile), 0);
    Bg_ClearTilemap(bgConfig, layer);
    Bg_SetOffset(bgConfig, layer, BG_OFFSET_UPDATE_SET_X, wide ? PokedexWide_Left() : 0);
    Bg_SetOffset(bgConfig, layer, BG_OFFSET_UPDATE_SET_Y, 0);
}

static const u16 sPalette[16] = {
    [PDW_CLEAR] = GX_RGB(0, 0, 0),
    [PDW_GOLD] = GX_RGB(214 >> 3, 189 >> 3, 99 >> 3),
    [PDW_GOLD_DARK] = GX_RGB(165 >> 3, 140 >> 3, 66 >> 3),
    [PDW_OLIVE] = GX_RGB(115 >> 3, 115 >> 3, 82 >> 3),
    [PDW_OLIVE_LIGHT] = GX_RGB(132 >> 3, 132 >> 3, 99 >> 3),
    [PDW_CREAM] = GX_RGB(239 >> 3, 230 >> 3, 197 >> 3),
    [PDW_CREAM_LIGHT] = GX_RGB(255 >> 3, 255 >> 3, 230 >> 3),
    [PDW_GREEN] = GX_RGB(99 >> 3, 190 >> 3, 90 >> 3), // stat bars
    [PDW_TEXT] = GX_RGB(82 >> 3, 82 >> 3, 82 >> 3),
    [PDW_TEXT_SHADOW] = GX_RGB(189 >> 3, 181 >> 3, 189 >> 3),
    [PDW_RED] = GX_RGB(255 >> 3, 107 >> 3, 115 >> 3),
    [PDW_RED_DARK] = GX_RGB(198 >> 3, 57 >> 3, 66 >> 3),
    [PDW_WHITE] = GX_RGB(31, 31, 31),
    [PDW_GREY] = GX_RGB(214 >> 3, 206 >> 3, 189 >> 3),
    [PDW_ORANGE] = GX_RGB(255 >> 3, 165 >> 3, 57 >> 3),
    [PDW_DARK] = GX_RGB(57 >> 3, 49 >> 3, 41 >> 3),
};

void PokedexWide_LoadPalette(void)
{
    Bg_LoadPalette(BG_LAYER_MAIN_1, (void *)sPalette, sizeof(sPalette), POKEDEX_WIDE_PLTT * sizeof(sPalette));
}

PokedexCanvas *PokedexCanvas_New(int width, int height, enum HeapID heapID)
{
    PokedexCanvas *canvas = Heap_Alloc(heapID, sizeof(PokedexCanvas));

    canvas->width = (width + 7) & ~7;
    canvas->height = (height + 7) & ~7;
    canvas->heapID = heapID;
    canvas->pixels = Heap_Alloc(heapID, canvas->width * canvas->height);
    memset(canvas->pixels, 0, canvas->width * canvas->height);

    return canvas;
}

void PokedexCanvas_Free(PokedexCanvas *canvas)
{
    Heap_Free(canvas->pixels);
    Heap_Free(canvas);
}

void PokedexCanvas_Fill(PokedexCanvas *canvas, int x, int y, int width, int height, u8 color)
{
    int x0 = x < 0 ? 0 : x;
    int y0 = y < 0 ? 0 : y;
    int x1 = x + width > canvas->width ? canvas->width : x + width;
    int y1 = y + height > canvas->height ? canvas->height : y + height;

    for (int row = y0; row < y1; row++) {
        if (x1 > x0) {
            memset(canvas->pixels + row * canvas->width + x0, color, x1 - x0);
        }
    }
}

void PokedexCanvas_Dot(PokedexCanvas *canvas, int x, int y, u8 color)
{
    if (x >= 0 && y >= 0 && x < canvas->width && y < canvas->height) {
        canvas->pixels[y * canvas->width + x] = color;
    }
}

void PokedexCanvas_Panel(PokedexCanvas *canvas, int x, int y, int width, int height, u8 fill, u8 border)
{
    PokedexCanvas_Fill(canvas, x + 1, y, width - 2, height, border);
    PokedexCanvas_Fill(canvas, x, y + 1, width, height - 2, border);
    PokedexCanvas_Fill(canvas, x + 1, y + 1, width - 2, height - 2, fill);
}

void PokedexCanvas_Stripes(PokedexCanvas *canvas, int x, int y, int width, int height, u8 colorA, u8 colorB)
{
    for (int row = 0; row < height; row++) {
        PokedexCanvas_Fill(canvas, x, y + row, width, 1, ((row >> 1) & 1) ? colorB : colorA);
    }
}

static u8 PatternColor(char c)
{
    switch (c) {
    case 'd':
        return PDW_DARK;
    case 'r':
        return PDW_RED;
    case 'R':
        return PDW_RED_DARK;
    case 'w':
        return PDW_WHITE;
    case 'g':
        return PDW_GREY;
    case 'o':
        return PDW_ORANGE;
    case 'y':
        return PDW_GOLD;
    case 'Y':
        return PDW_GOLD_DARK;
    case 't':
        return PDW_TEXT;
    case 's':
        return PDW_TEXT_SHADOW;
    case 'G':
        return PDW_GREEN;
    case 'c':
        return PDW_CREAM;
    default:
        return PDW_CLEAR;
    }
}

void PokedexCanvas_Pattern(PokedexCanvas *canvas, int x, int y, const char *const *rows, int numRows)
{
    for (int row = 0; row < numRows; row++) {
        for (int col = 0; rows[row][col] != '\0'; col++) {
            const u8 color = PatternColor(rows[row][col]);

            if (color != PDW_CLEAR) {
                PokedexCanvas_Dot(canvas, x + col, y + row, color);
            }
        }
    }
}

// An 11x11 Poké Ball.
static const char *const sBall[POKEDEX_CANVAS_BALL_SIZE] = {
    "...ddddd...",
    ".ddrrrrrdd.",
    ".drwwrrrrd.",
    "drwrrrrrrRd",
    "drrrrdddrRd",
    "ddddddwdddd",
    "dwwwwdddwgd",
    "dwwwwwwwwgd",
    ".dwwwwwwgd.",
    ".ddgwwwggd.",
    "...ddddd...",
};

void PokedexCanvas_Ball(PokedexCanvas *canvas, int x, int y)
{
    PokedexCanvas_Pattern(canvas, x, y, sBall, POKEDEX_CANVAS_BALL_SIZE);
}

// A 10x16 question mark, drawn with a one-dot shadow.
static const char *sQuestion[14] = {
    "..######..",
    ".########.",
    "###....###",
    "##......##",
    "........##",
    ".......###",
    "......###.",
    ".....###..",
    "....###...",
    "....##....",
    "....##....",
    "..........",
    "....##....",
    "....##....",
};

void PokedexCanvas_Question(PokedexCanvas *canvas, int x, int y, u8 color, u8 shadow)
{
    for (int pass = 0; pass < 2; pass++) {
        for (int row = 0; row < 14; row++) {
            for (int col = 0; col < 10; col++) {
                if (sQuestion[row][col] == '#') {
                    PokedexCanvas_Dot(canvas, x + col + (pass ? 0 : 1), y + row + (pass ? 0 : 1), pass ? color : shadow);
                }
            }
        }
    }
}

#define CANVAS_TILE_BYTES 32
#define CANVAS_HASH_SIZE  2048

int PokedexCanvas_Upload(PokedexCanvas *canvas, BgConfig *bgConfig, u8 layer, u16 tileStart, u16 maxTiles, u8 destX, u8 destY)
{
    const int tilesWide = canvas->width / 8;
    const int tilesHigh = canvas->height / 8;
    const int count = tilesWide * tilesHigh;

    u8 *tiles = Heap_Alloc(canvas->heapID, (maxTiles + 1) * CANVAS_TILE_BYTES);
    u16 *tilemap = Heap_Alloc(canvas->heapID, count * sizeof(u16));
    s16 *buckets = Heap_Alloc(canvas->heapID, CANVAS_HASH_SIZE * sizeof(s16));
    s16 *chain = Heap_Alloc(canvas->heapID, (maxTiles + 1) * sizeof(s16));
    u8 tile[CANVAS_TILE_BYTES];
    int used = 0;

    for (int i = 0; i < CANVAS_HASH_SIZE; i++) {
        buckets[i] = -1;
    }

    for (int ty = 0; ty < tilesHigh; ty++) {
        for (int tx = 0; tx < tilesWide; tx++) {
            u32 hash = 2166136261u;
            BOOL blank = TRUE;

            for (int row = 0; row < 8; row++) {
                const u8 *src = canvas->pixels + (ty * 8 + row) * canvas->width + tx * 8;

                for (int col = 0; col < 8; col += 2) {
                    u8 packed = (src[col] & 0xF) | ((src[col + 1] & 0xF) << 4);

                    tile[row * 4 + col / 2] = packed;
                    hash = (hash ^ packed) * 16777619u;
                    blank &= packed == 0;
                }
            }

            u16 entry = 0;

            if (!blank) {
                int bucket = hash & (CANVAS_HASH_SIZE - 1);
                int found = -1;

                for (int i = buckets[bucket]; i >= 0; i = chain[i]) {
                    if (memcmp(tiles + i * CANVAS_TILE_BYTES, tile, CANVAS_TILE_BYTES) == 0) {
                        found = i;
                        break;
                    }
                }

                if (found < 0 && used < maxTiles) {
                    found = used++;
                    memcpy(tiles + found * CANVAS_TILE_BYTES, tile, CANVAS_TILE_BYTES);
                    chain[found] = buckets[bucket];
                    buckets[bucket] = found;
                }

                GF_ASSERT(found >= 0);
                entry = found >= 0 ? (tileStart + found) | (POKEDEX_WIDE_PLTT << 12) : 0;
            }

            tilemap[ty * tilesWide + tx] = entry;
        }
    }

    if (used > 0) {
        Bg_LoadTiles(bgConfig, layer, tiles, used * CANVAS_TILE_BYTES, tileStart);
    }

    // The tiles went to VRAM just now and their indices change between redraws, so the
    // tilemap has to go now too: one deferred to VBlank would show a frame of mismatched tiles.
    Bg_LoadToTilemapRect(bgConfig, layer, tilemap, destX, destY, tilesWide, tilesHigh);
    Bg_CopyTilemapBufferToVRAM(bgConfig, layer);

    Heap_Free(chain);
    Heap_Free(buckets);
    Heap_Free(tilemap);
    Heap_Free(tiles);

    return used;
}
