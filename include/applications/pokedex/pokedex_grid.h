#ifndef POKEPLATINUM_POKEDEX_GRID_H
#define POKEPLATINUM_POKEDEX_GRID_H

#include "applications/pokedex/pokedex_graphics.h"
#include "applications/pokedex/pokedex_sort_data.h"

// The PC Pokédex overview: a grid of Pokémon icons across the wide view, replacing
// the list screen's graphics (ov21_021D5AEC). Input stays with the list screen.
typedef struct PokedexGrid PokedexGrid;

PokedexGrid *PokedexGrid_New(PokedexGraphicData *graphicData, const PokedexSortData *sortData, enum HeapID heapID);
// Call every frame; redraws when the selection or the listing changed.
void PokedexGrid_Update(PokedexGrid *grid, const PokedexSortData *sortData);
// Fades the grid in (fadeIn) or out; returns TRUE when done.
void PokedexGrid_StartFade(PokedexGrid *grid, BOOL fadeIn);
BOOL PokedexGrid_FadeStep(PokedexGrid *grid);
// Leaves the Pokémon sprite set to the selection, hidden, for the info screens.
void PokedexGrid_Free(PokedexGrid *grid, PokedexSortData *sortData);

// Y: the selected Pokémon's cry, once it has been seen (the entry has no CRY page).
void PokedexGrid_PlayCry(const PokedexSortData *sortData);

// D-pad and L/R movement across the grid. Returns TRUE when the selection moved.
BOOL PokedexGrid_HandleInput(PokedexSortData *sortData);

#endif // POKEPLATINUM_POKEDEX_GRID_H
