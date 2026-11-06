#ifndef POKEPLATINUM_POKEDEX_TABS_H
#define POKEPLATINUM_POKEDEX_TABS_H

#include "applications/pokedex/pokedex_graphics.h"
#include "applications/pokedex/pokedex_sort_data.h"

// The PC Pokédex entry's tab strip, across the top of the wide view.
//
// Tabs keep the info screen's IDs (ov21_021E29DC): 0-4 are Platinum's INFO, AREA, CRY,
// SIZE and FORMS pages, 5-7 the widescreen STATS, MOVES and EVOLUTIONS pages. The strip
// shows them in display order: INFO, STATS, MOVES, EVOS, AREA, SIZE, FORMS. The CRY page
// is left out; the grid plays cries instead (Y).

enum PokedexTab {
    POKEDEX_TAB_INFO = 0,
    POKEDEX_TAB_AREA,
    POKEDEX_TAB_CRY,
    POKEDEX_TAB_SIZE,
    POKEDEX_TAB_FORMS,
    POKEDEX_TAB_STATS,
    POKEDEX_TAB_MOVES,
    POKEDEX_TAB_EVOS,
    POKEDEX_TAB_COUNT,
};

// The tabs on the strip.
#define POKEDEX_TAB_SHOWN 7

#define POKEDEX_TAB_STRIP_HEIGHT 16

// Positions wrap around the strip.
int PokedexTabs_TabAt(int position);
// -1 for a tab not on the strip.
int PokedexTabs_Position(int tab);
// Whether the current entry can open the tab: SIZE needs the Pokémon caught, FORMS
// the form detector. The strip grays out the rest.
BOOL PokedexTabs_IsAvailable(const PokedexSortData *sortData, int tab);
BOOL PokedexTabs_IsWidePage(int tab);
u32 PokedexTabs_LabelMessage(int tab);
// The native x of a label of this width, centered in its tab.
int PokedexTabs_LabelX(int tab, int labelWidth);

// Begins the wide view and takes BG2 for the strip (and the side panels around
// Platinum's native-width pages); Teardown gives both back.
void PokedexTabs_Setup(PokedexGraphicData *graphicData, enum HeapID heapID);
void PokedexTabs_Draw(PokedexGraphicData *graphicData, int selectedTab, enum HeapID heapID);
void PokedexTabs_Teardown(PokedexGraphicData *graphicData);

#endif // POKEPLATINUM_POKEDEX_TABS_H
