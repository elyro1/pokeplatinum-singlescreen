#ifndef POKEPLATINUM_POKEMON_SUMMARY_SCREEN_WIDE_H
#define POKEPLATINUM_POKEMON_SUMMARY_SCREEN_WIDE_H

#include <nitro.h>

#include "constants/heap.h"

#include "applications/pokemon_summary_screen/main.h"

#include "bg_window.h"
#include "narc.h"

// Widescreen layout for the PC summary screen: the left panel keeps its size against the
// left edge of the view, and the right panel takes the rest with its native content
// centered. Nothing here changes the screen unless the view is wider than 256 dots.
//
// Native dot x maps to view dot x + left, so the right panel's center stays put at any
// width. Only the left side moves (by -left). BG layers 1 and 3 are 512 dots wide and
// laid out in view coordinates, where the right panel's content moves by a whole number
// of tiles and so lands within 4 dots of exact.

enum SummaryWidePanel {
    SUMMARY_WIDE_PANEL_NONE = 0,
    SUMMARY_WIDE_PANEL_BATTLE_MOVE,
    SUMMARY_WIDE_PANEL_CONTEST_MOVE,
    SUMMARY_WIDE_PANEL_RIBBON,
};

// Before the BG layers are set up, and after they are torn down.
void SummaryWide_Begin(void);
void SummaryWide_End(void);
BOOL SummaryWide_IsActive(void);

// How far the layers scroll to put tile column 0 at the left edge of the view.
int SummaryWide_Left(void);
// How many tiles the right panel's content moves.
int SummaryWide_ContentShiftTiles(void);
// A width that reaches the view's right edge from anywhere on a layer.
int SummaryWide_FullRowTiles(void);

void SummaryWide_WidenTemplate(BgTemplate *template, GXBGScrBase screenBase);
void SummaryWide_PlaceWindow(WindowTemplate *template);
// After the BG tiles: adds the tiles that SummaryWide_LoadPageTilemap needs.
void SummaryWide_LoadConditionTiles(BgConfig *bgConfig, NARC *narc, u32 narcMemberIdx, enum HeapID heapID);
void SummaryWide_LoadPageTilemap(BgConfig *bgConfig, const u16 *nativeTilemap, BOOL isConditionPage);

// The slide-over panels. A wide view shows nearly all of the 512-dot layer, so it
// holds only the panel in use: the others would show beside it, and a panel scrolled
// out to the left would wrap around to the right edge of the view.
void SummaryWide_SavePanels(BgConfig *bgConfig);
void SummaryWide_ShowPanel(BgConfig *bgConfig, enum SummaryWidePanel panel);
int SummaryWide_PanelX(int nativeOffset);
int SummaryWide_RibbonPanelX(void);
// Where the move info panel starts sliding in from, and how far it slides out before
// it is removed, as native offsets.
int SummaryWide_MoveInfoStartOffset(void);
int SummaryWide_MoveInfoHideOffset(void);

// Draws the sprites at their places in the view. Their positions stay native otherwise.
void SummaryWide_DrawSprites(PokemonSummaryScreen *summaryScreen);
// The 3D layer is native width: it moves to the right panel for the condition graph,
// and to the left edge for the Pokémon, within one frame.
void SummaryWide_Set3DForContent(void);
void SummaryWide_Set3DForMon(void);

#endif // POKEPLATINUM_POKEMON_SUMMARY_SCREEN_WIDE_H
