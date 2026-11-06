#include "applications/pokedex/pokedex_reference.h"

#include <nitro.h>
#include <string.h>

#include "constants/forms.h"
#include "constants/narc.h"
#include "constants/species.h"
#include "generated/evolution_methods.h"
#include "generated/items.h"

#include "heap.h"
#include "item.h"
#include "narc.h"
#include "pokemon.h"
#include "tutor_movesets.h"

#include "res/pokemon/species_egg_moves.h"
#include "res/pokemon/species_learnsets_by_tutor.h"

#define NUM_TMS     92
#define NUM_TMS_HMS 100

PokedexEvoTable *PokedexReference_NewEvoTable(enum HeapID heapID)
{
    PokedexEvoTable *table = Heap_Alloc(heapID, sizeof(PokedexEvoTable));
    NARC *narc = NARC_ctor(NARC_INDEX_POKETOOL__PERSONAL__EVO, heapID);

    memset(table, 0, sizeof(PokedexEvoTable));

    for (int species = 1; species <= NATIONAL_DEX_COUNT; species++) {
        NARC_ReadWholeMember(narc, species, table->evolutions[species]);
    }

    NARC_dtor(narc);

    for (int species = 1; species <= NATIONAL_DEX_COUNT; species++) {
        for (int i = 0; i < MAX_EVOLUTIONS; i++) {
            const SpeciesEvolution *evolution = &table->evolutions[species][i];

            if (evolution->method != EVO_NONE && evolution->targetSpecies > 0 && evolution->targetSpecies <= NATIONAL_DEX_COUNT
                && table->preEvolution[evolution->targetSpecies] == SPECIES_NONE) {
                table->preEvolution[evolution->targetSpecies] = species;
            }
        }
    }

    return table;
}

void PokedexReference_FreeEvoTable(PokedexEvoTable *table)
{
    Heap_Free(table);
}

static int FirstStage(const PokedexEvoTable *table, int species)
{
    // Bounded, in case the data ever loops.
    for (int steps = 0; steps < 8 && table->preEvolution[species] != SPECIES_NONE; steps++) {
        species = table->preEvolution[species];
    }

    return species;
}

static void AddEvoNodes(const PokedexEvoTable *table, PokedexEvoChain *chain, int parentIndex)
{
    const int species = chain->nodes[parentIndex].species;

    for (int i = 0; i < MAX_EVOLUTIONS; i++) {
        const SpeciesEvolution *evolution = &table->evolutions[species][i];

        if (evolution->method == EVO_NONE || evolution->targetSpecies == SPECIES_NONE || evolution->targetSpecies > NATIONAL_DEX_COUNT) {
            continue;
        }

        if (chain->count >= POKEDEX_MAX_EVO_NODES || chain->nodes[parentIndex].depth >= 3) {
            return;
        }

        // Some species list the same target twice (e.g. by day and by night item); show both.
        PokedexEvoNode *node = &chain->nodes[chain->count];

        node->species = evolution->targetSpecies;
        node->method = evolution->method;
        node->param = evolution->param;
        node->depth = chain->nodes[parentIndex].depth + 1;
        node->parent = parentIndex;
        chain->count++;

        AddEvoNodes(table, chain, chain->count - 1);
    }
}

void PokedexReference_LoadEvoChain(const PokedexEvoTable *table, int species, PokedexEvoChain *chain)
{
    memset(chain, 0, sizeof(PokedexEvoChain));

    if (species <= SPECIES_NONE || species > NATIONAL_DEX_COUNT) {
        return;
    }

    chain->nodes[0].species = FirstStage(table, species);
    chain->nodes[0].parent = -1;
    chain->count = 1;

    AddEvoNodes(table, chain, 0);
}

static int TutorMoveset(int species, int form)
{
    switch (species) {
    case SPECIES_DEOXYS:
        return form == 1 ? MOVESET_FORM_DEOXYS_ATTACK : form == 2 ? MOVESET_FORM_DEOXYS_DEFENSE : form == 3 ? MOVESET_FORM_DEOXYS_SPEED : species;
    case SPECIES_WORMADAM:
        return form == 1 ? MOVESET_FORM_WORMADAM_SANDY : form == 2 ? MOVESET_FORM_WORMADAM_TRASH : species;
    case SPECIES_GIRATINA:
        return form == 1 ? MOVESET_FORM_GIRATINA_ORIGIN : species;
    case SPECIES_SHAYMIN:
        return form == 1 ? MOVESET_FORM_SHAYMIN_SKY : species;
    case SPECIES_ROTOM:
        switch (form) {
        case 1:
            return MOVESET_FORM_ROTOM_HEAT;
        case 2:
            return MOVESET_FORM_ROTOM_WASH;
        case 3:
            return MOVESET_FORM_ROTOM_FROST;
        case 4:
            return MOVESET_FORM_ROTOM_FAN;
        case 5:
            return MOVESET_FORM_ROTOM_MOW;
        }
        return species;
    default:
        return species;
    }
}

static int FindEggMoves(int species)
{
    for (int i = 0; i < (int)NELEMS(sEggMoves); i++) {
        if (sEggMoves[i] == EGG_MOVES_SPECIES_OFFSET + species) {
            return i + 1;
        }
    }

    return -1;
}

static void AddMove(PokedexMoveList *list, int source, u16 move, u8 value)
{
    if (list->count >= POKEDEX_MAX_MOVE_ENTRIES || move == MOVE_NONE) {
        return;
    }

    list->entries[list->count].move = move;
    list->entries[list->count].source = source;
    list->entries[list->count].value = value;
    list->count++;
    list->sourceCount[source]++;
}

void PokedexReference_LoadMoves(const PokedexEvoTable *table, int species, int form, PokedexMoveList *list, enum HeapID heapID)
{
    memset(list, 0, sizeof(PokedexMoveList));

    if (species <= SPECIES_NONE || species > NATIONAL_DEX_COUNT) {
        return;
    }

    u16 *levelUp = Heap_Alloc(heapID, sizeof(SpeciesLearnset));

    list->sourceStart[POKEDEX_MOVE_LEVEL] = list->count;
    Pokemon_LoadLevelUpMovesOf(species, form, levelUp);

    for (int i = 0; i <= MAX_LEARNSET_ENTRIES && levelUp[i] != LEARNSET_SENTINEL_ENTRY; i++) {
        AddMove(list, POKEDEX_MOVE_LEVEL, levelUp[i] & 0x1FF, levelUp[i] >> 9);
    }

    Heap_Free(levelUp);

    list->sourceStart[POKEDEX_MOVE_TM] = list->count;

    for (int tm = 0; tm < NUM_TMS_HMS; tm++) {
        if (CanPokemonFormLearnTM(species, form, tm)) {
            const u16 item = tm < NUM_TMS ? ITEM_TM01 + tm : ITEM_HM01 + tm - NUM_TMS;
            AddMove(list, POKEDEX_MOVE_TM, Item_MoveForTMHM(item), tm + 1);
        }
    }

    list->sourceStart[POKEDEX_MOVE_TUTOR] = list->count;

    const MovesetMask *tutorMask = &sSpeciesLearnsetsByTutor[TutorMoveset(species, form) - 1];

    for (int i = 0; i < (int)NELEMS(sTeachableMoves); i++) {
        if ((tutorMask->maskData[i / 8] >> (i % 8)) & 1) {
            AddMove(list, POKEDEX_MOVE_TUTOR, sTeachableMoves[i].moveID, 0);
        }
    }

    list->sourceStart[POKEDEX_MOVE_EGG] = list->count;

    // Egg moves are listed under the stage that hatches; walk down the family to find it.
    int eggStart = -1;

    for (int stage = species, steps = 0; stage != SPECIES_NONE && steps < 8 && eggStart < 0; stage = table->preEvolution[stage], steps++) {
        eggStart = FindEggMoves(stage);
    }

    if (eggStart >= 0) {
        for (int i = eggStart; i < (int)NELEMS(sEggMoves) && sEggMoves[i] < EGG_MOVES_SPECIES_OFFSET; i++) {
            AddMove(list, POKEDEX_MOVE_EGG, sEggMoves[i], 0);
        }
    }
}
