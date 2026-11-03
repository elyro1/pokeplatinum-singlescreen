#ifndef POKEPLATINUM_OVERWORLD_ENCOUNTERS_H
#define POKEPLATINUM_OVERWORLD_ENCOUNTERS_H

#include "constants/overworld_encounters.h"
#include "field/field_system_decl.h"

#include "pokemon.h"

// Keep only the opponents between spawning and contact. Player state and battle
// resources are initialized at contact, so menus/party changes cannot stale them.
typedef struct OverworldEncounterPokemon {
    Pokemon mons[2];
    u32 battleType;
} OverworldEncounterPokemon;

// Whether visible encounters currently replace ordinary step encounters.
BOOL OverworldEncounters_ReplaceRandom(FieldSystem *fieldSystem);
void OverworldEncounters_Update(FieldSystem *fieldSystem);
void OverworldEncounters_Clear(FieldSystem *fieldSystem);
void OverworldEncounters_Free(FieldSystem *fieldSystem);
BOOL OverworldEncounters_TryContact(FieldSystem *fieldSystem);

#endif
