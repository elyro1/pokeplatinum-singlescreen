#ifndef POKEPLATINUM_OVERWORLD_ENCOUNTERS_H
#define POKEPLATINUM_OVERWORLD_ENCOUNTERS_H

#include "field/field_system_decl.h"

#include "pokemon.h"

#define OVERWORLD_ENCOUNTER_CAPACITY 30
#define OVERWORLD_ENCOUNTER_DEFAULT_COUNT 8

// Keep only the opponents between spawning and contact. Player state and battle
// resources are initialized at contact, so menus/party changes cannot stale them.
typedef struct OverworldEncounterPokemon {
    Pokemon mons[2];
    u32 battleType;
} OverworldEncounterPokemon;

void OverworldEncounters_Update(FieldSystem *fieldSystem);
void OverworldEncounters_Clear(FieldSystem *fieldSystem);
void OverworldEncounters_Free(FieldSystem *fieldSystem);
BOOL OverworldEncounters_TryContact(FieldSystem *fieldSystem);

#endif
