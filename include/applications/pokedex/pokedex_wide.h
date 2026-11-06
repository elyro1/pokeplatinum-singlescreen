#ifndef POKEPLATINUM_POKEDEX_WIDE_H
#define POKEPLATINUM_POKEDEX_WIDE_H

#include <nitro.h>

#include "constants/heap.h"

#include "bg_window.h"

// Widescreen support for the PC Pokédex screens (grid, stats, moves, evolutions).
//
// The view is PokedexWide_Width() dots wide (256 outside the single-screen layout,
// up to 448). Wide screens lay their BG layers out in view coordinates: tile column 0
// is the left edge of the view, and the layers scroll by PokedexWide_Left() so the
// native screen stays centered. Sprites take native coordinates (view x - left).

#define POKEDEX_WIDE_MAX_WIDTH 448
#define POKEDEX_WIDE_MAX_TILES (POKEDEX_WIDE_MAX_WIDTH / 8)

// BG palette slot shared by the wide screens, and its colors.
#define POKEDEX_WIDE_PLTT 13 // unused by banner_sinnoh.pal, which the info screens share

enum PokedexWideColor {
    PDW_CLEAR = 0,
    PDW_GOLD,
    PDW_GOLD_DARK,
    PDW_OLIVE,
    PDW_OLIVE_LIGHT,
    PDW_CREAM,
    PDW_CREAM_LIGHT,
    PDW_GREEN,
    PDW_TEXT,
    PDW_TEXT_SHADOW,
    PDW_RED,
    PDW_RED_DARK,
    PDW_WHITE,
    PDW_GREY,
    PDW_ORANGE,
    PDW_DARK,
};

// Reads the "New Pokédex" setting. The Pokédex calls this as it opens and keeps the answer
// until it closes, since the setting can change in the Tab menu with the Pokédex up.
void PokedexWide_LatchEnabled(void);
// Whether this session of the Pokédex uses the widescreen screens or the classic ones.
BOOL PokedexWide_IsEnabled(void);

int PokedexWide_Begin(void);
void PokedexWide_End(void);
// Between Begin and End.
BOOL PokedexWide_IsActive(void);

// Main screen fades while the wide view is up (pokedex_graphics.c's blend transitions).
// Platinum's screens each fade a few BG layers of their own (BLDCNT), and the simulator
// applies BLDCNT brightness to BG layers only: sprites, the 3D Pokémon and the wide
// sides stayed lit. Here the fades set master brightness, which darkens the whole
// screen evenly, and the screen stays black from the end of one fade-out until the
// next fade-in starts, while the old screen is torn down and the new one drawn.
// Between an entry's pages the tab strip stays lit (SIM_SetMasterBrightnessFirstLine).
#define POKEDEX_WIDE_FADE_PLANES  (GX_BLEND_PLANEMASK_BG0 | GX_BLEND_PLANEMASK_BG1 | GX_BLEND_PLANEMASK_BG2 | GX_BLEND_PLANEMASK_BG3 | GX_BLEND_PLANEMASK_OBJ | GX_BLEND_PLANEMASK_BD)
#define POKEDEX_WIDE_FADE_TARGETS (GX_BLEND_PLANEMASK_BG0 | GX_BLEND_PLANEMASK_BG1 | GX_BLEND_PLANEMASK_BG2 | GX_BLEND_PLANEMASK_BG3 | GX_BLEND_PLANEMASK_BD)

#define POKEDEX_WIDE_CROSSFADE_FRAMES 6

typedef void (*PokedexWideBlackFunc)(void);

// Returns whether the entry's tab strip stays lit: Platinum's entry pages fade
// without BG2, the banner the strip replaces, while entering and leaving an entry
// (and the grid) fade it too.
BOOL PokedexWide_AdjustFade(u8 *numSteps, GXBlendPlaneMask *plane1, GXBlendPlaneMask *plane2);
// Each fade step's brightness (-16 is black). Returns FALSE when it is left to BLDCNT,
// while an app-level screen fade owns master brightness.
BOOL PokedexWide_FadeStep(int brightness, BOOL keepStrip);
// Page changes within an entry (keepStrip fades) crossfade instead: the fade-out
// holds a snapshot of the old page on screen (SIM_HoldMainScreen) and is done once
// it is up, the next fade-in blends from it to the new page and is done at once.
// Returns FALSE to leave the step to PokedexWide_FadeStep, e.g. from a black screen.
BOOL PokedexWide_CrossfadeStep(BOOL fadeIn, BOOL keepStrip, BOOL *done);
// The next fade-out holds the whole screen, so that the next screen crossfades in
// from it: opening an entry from the grid, and closing it again.
void PokedexWide_CrossfadeScreen(void);
BOOL PokedexWide_IsHoldingBlack(void);
// Runs func once the old screen is out of sight (black, or behind a held
// snapshot): now if it is, else when a fade-out ends.
void PokedexWide_RunAtBlack(PokedexWideBlackFunc func);
// In VBlank: keeps the screen black over whatever the screens set between fades.
void PokedexWide_ApplyHeldFade(void);
// When the Pokédex closes.
void PokedexWide_ResetFade(void);
int PokedexWide_Width(void);
int PokedexWide_Left(void);
int PokedexWide_TileWidth(void);
// Moves the native-width 3D layer (the Pokémon sprite) by dots within the view.
void PokedexWide_Set3DOffset(int dots);
// Main BG layers 1-3: 512-dot screens laid out in view coordinates, or back to the
// Pokédex's original 256-dot templates. Clears the layer either way.
void PokedexWide_ConfigureLayer(BgConfig *bgConfig, u8 layer, BOOL wide);
void PokedexWide_LoadPalette(void);

// A 4bpp pixel canvas, one byte per dot. PokedexCanvas_Upload slices it into tiles,
// merging identical ones, and puts it on a BG layer.
typedef struct PokedexCanvas {
    u8 *pixels;
    int width;
    int height;
    enum HeapID heapID;
} PokedexCanvas;

PokedexCanvas *PokedexCanvas_New(int width, int height, enum HeapID heapID);
void PokedexCanvas_Free(PokedexCanvas *canvas);
void PokedexCanvas_Fill(PokedexCanvas *canvas, int x, int y, int width, int height, u8 color);
void PokedexCanvas_Dot(PokedexCanvas *canvas, int x, int y, u8 color);
// A filled box with a one-dot border and cut corners.
void PokedexCanvas_Panel(PokedexCanvas *canvas, int x, int y, int width, int height, u8 fill, u8 border);
// Horizontal stripes two dots tall across a region.
void PokedexCanvas_Stripes(PokedexCanvas *canvas, int x, int y, int width, int height, u8 colorA, u8 colorB);
// Pixel art, one string per row: d dark, r/R red/dark red, w white, g grey, o orange,
// y/Y gold/dark gold, t text, s text shadow, G green, c cream; anything else is left alone.
void PokedexCanvas_Pattern(PokedexCanvas *canvas, int x, int y, const char *const *rows, int numRows);

#define POKEDEX_CANVAS_BALL_SIZE 11

// A Poké Ball, POKEDEX_CANVAS_BALL_SIZE dots square, with its top-left here.
void PokedexCanvas_Ball(PokedexCanvas *canvas, int x, int y);
void PokedexCanvas_Question(PokedexCanvas *canvas, int x, int y, u8 color, u8 shadow);
// Returns the number of tiles used, starting at tileStart (tile 0 of the layer stays blank).
int PokedexCanvas_Upload(PokedexCanvas *canvas, BgConfig *bgConfig, u8 layer, u16 tileStart, u16 maxTiles, u8 destX, u8 destY);

#endif // POKEPLATINUM_POKEDEX_WIDE_H
