#ifndef POKEPLATINUM_POKEDEX_PAGES_H
#define POKEPLATINUM_POKEDEX_PAGES_H

#include "applications/pokedex/pokedex_app.h"
#include "applications/pokedex/struct_ov21_021D4660.h"
#include "applications/pokedex/struct_ov21_021E68F4.h"

#ifdef SDK_PORT
#include "applications/pokedex/struct_ov21_021E8E0C.h"

#include "narc.h"
#include "sprite.h"
#include "sprite_resource.h"
#endif

// The PC Pokédex's widescreen entry pages: info, base stats, learnable moves and evolutions.
// All share the sprite frame on the left. Stats, moves and evolutions are main screens 10-12
// of the Pokédex app, entered from the entry's tabs; info takes Platinum's info screen, 2,
// whose transitions and sub screen stay.

enum PokedexWidePage {
    POKEDEX_WIDE_PAGE_STATS = 0,
    POKEDEX_WIDE_PAGE_MOVES,
    POKEDEX_WIDE_PAGE_EVOS,
    POKEDEX_WIDE_PAGE_INFO,
};

#define POKEDEX_WIDE_PAGE_FIRST_SCREEN 10

void PokedexInfoPage_Init(PokedexScreenManager *screenMan, PokedexApp *pokedexApp, enum HeapID heapID);
void PokedexStatsPage_Init(PokedexScreenManager *screenMan, PokedexApp *pokedexApp, enum HeapID heapID);
void PokedexMovesPage_Init(PokedexScreenManager *screenMan, PokedexApp *pokedexApp, enum HeapID heapID);
void PokedexEvosPage_Init(PokedexScreenManager *screenMan, PokedexApp *pokedexApp, enum HeapID heapID);
void PokedexWidePage_Free(PokedexScreenManager *screenMan);

#ifdef SDK_PORT
typedef struct PageGraphics PageGraphics;

PageGraphics *PokedexInfoPage_ShowCaught(const PokedexEntryScreenParams *params, PokemonSprite *sprite, NARC *narc, SpriteList *spriteList, SpriteResourceCollection **collections);
void PokedexInfoPage_FreeCaught(PageGraphics *graphics, BgConfig *bgConfig, SpriteResourceCollection **collections);
#endif

// An info screen tab state (ov21_021E29DC) that swaps the page slot to this page.
void PokedexWidePage_InitTabState(enum HeapID heapID, UnkStruct_ov21_021D4660 *state, PokedexApp *pokedexApp, int requestBit, int page);

#endif // POKEPLATINUM_POKEDEX_PAGES_H
