#ifndef POKEPLATINUM_POKEMON_MOOD_H
#define POKEPLATINUM_POKEMON_MOOD_H

#include "pokemon.h"

// HG/SS stores mood in the high byte of Block D's reserved final word.
// Access through Pokemon_Get/SetValue to preserve encryption and checksums.
int PokemonMood_Get(Pokemon *mon);
void PokemonMood_Adjust(Pokemon *mon, int delta);
void PokemonMood_StepTowardsNeutral(Pokemon *mon);
void PokemonMood_OnFriendshipEvent(Pokemon *mon, int event);
void PokemonMood_OnItem(Pokemon *mon, u16 item);

#endif
