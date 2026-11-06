#ifndef POKEPLATINUM_POKEDEX_REFERENCE_H
#define POKEPLATINUM_POKEDEX_REFERENCE_H

#include <nitro.h>

#include "constants/heap.h"
#include "constants/species.h"

#include "struct_defs/species.h"

// Reference data for the PC Pokédex pages: learnable moves and evolution chains.

enum PokedexMoveSource {
    POKEDEX_MOVE_LEVEL = 0,
    POKEDEX_MOVE_TM,
    POKEDEX_MOVE_TUTOR,
    POKEDEX_MOVE_EGG,
    POKEDEX_MOVE_SOURCE_COUNT,
};

typedef struct PokedexMoveEntry {
    u16 move;
    u8 source;
    // The level for level-up moves; 1-92 for TMs and 93-100 for HMs.
    u8 value;
} PokedexMoveEntry;

#define POKEDEX_MAX_MOVE_ENTRIES 192

typedef struct PokedexMoveList {
    int count;
    // The index of each source's first entry; sources appear in enum order.
    int sourceStart[POKEDEX_MOVE_SOURCE_COUNT];
    int sourceCount[POKEDEX_MOVE_SOURCE_COUNT];
    PokedexMoveEntry entries[POKEDEX_MAX_MOVE_ENTRIES];
} PokedexMoveList;

#define POKEDEX_MAX_EVO_NODES 16

typedef struct PokedexEvoNode {
    u16 species;
    // How the parent evolves into this species (none for the first stage).
    u16 method;
    u16 param;
    u8 depth;
    s8 parent;
} PokedexEvoNode;

typedef struct PokedexEvoChain {
    int count;
    PokedexEvoNode nodes[POKEDEX_MAX_EVO_NODES];
} PokedexEvoChain;

// Every species' evolutions, read once, and each species' pre-evolution.
typedef struct PokedexEvoTable {
    SpeciesEvolution evolutions[NATIONAL_DEX_COUNT + 1][MAX_EVOLUTIONS];
    u16 preEvolution[NATIONAL_DEX_COUNT + 1];
} PokedexEvoTable;

PokedexEvoTable *PokedexReference_NewEvoTable(enum HeapID heapID);
void PokedexReference_FreeEvoTable(PokedexEvoTable *table);
// The whole family of species, first stage first, in depth-first order.
void PokedexReference_LoadEvoChain(const PokedexEvoTable *table, int species, PokedexEvoChain *chain);
void PokedexReference_LoadMoves(const PokedexEvoTable *table, int species, int form, PokedexMoveList *list, enum HeapID heapID);

#endif // POKEPLATINUM_POKEDEX_REFERENCE_H
