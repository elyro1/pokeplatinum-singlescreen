#ifndef POKEPLATINUM_POKEDEX_APP_H
#define POKEPLATINUM_POKEDEX_APP_H

#include "applications/pokedex/pokedex_graphics.h"
#include "applications/pokedex/pokedex_sort_data.h"
#include "applications/pokedex/pokedex_updater.h"
#include "applications/pokedex/struct_ov21_021D423C_decl.h"
#include "applications/pokedex/struct_ov21_021D4660.h"
#include "applications/pokedex/struct_ov21_021E68F4.h"

#include "pokedex_memory.h"

// Platinum's ten main screens, and on PC the widescreen stats, moves and evolution pages.
// (The widescreen info page takes over Platinum's info screen, 2.)
#ifdef SDK_PORT
#define POKEDEX_MAIN_SCREEN_COUNT 13
#else
#define POKEDEX_MAIN_SCREEN_COUNT 10
#endif

typedef struct PokedexApp {
    BOOL unk_00;
    enum HeapID heapID;
    PokedexMemory *pokedexMemory;
    PokedexSortData sortData;
    PokedexGraphicData graphicData;
    int unk_1A60;
    int unk_1A64;
    int unk_1A68;
    int unk_1A6C;
    UnkStruct_ov21_021D423C *unk_1A70;
    UnkStruct_ov21_021D423C *unk_1A74;
    UnkStruct_ov21_021D423C *unk_1A78;
    UnkStruct_ov21_021D423C *unk_1A7C;
    UnkStruct_ov21_021D4660 unk_1A80;
    PokedexScreenManager unk_1A94[POKEDEX_MAIN_SCREEN_COUNT];
    PokedexScreenManager unk_1C24[8];
    PokedexUpdater *unk_1D64;
    PokedexUpdater *unk_1D68;
    PokedexUpdater *unk_1D6C;
    PokedexUpdater *unk_1D70;
} PokedexApp;

#endif // POKEPLATINUM_POKEDEX_APP_H
